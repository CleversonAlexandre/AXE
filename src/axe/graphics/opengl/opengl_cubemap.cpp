#include "opengl_cubemap.hpp"
#include "axe/log/log.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/buffer.hpp"

#include <glad/glad.h>
#include <stb/stb_image.h>
#include "axe/utils/glm_config.hpp"

namespace axe
{
	//Shader de conversão equiretangular -> cubemap
	static const char* s_EquirectVertSrc = R"(
	#version 460 core
	layout(location = 0) in vec3 a_Position;
	out vec3 v_LocalPos;
	uniform mat4 u_Projection;
	uniform mat4 u_View;
	void main()
	{
		v_LocalPos  = a_Position;
		gl_Position = u_Projection * u_View * vec4(a_Position, 1.0);
	}
)";

	static const char* s_EquirectFragSrc = R"(
	#version 460 core
	out vec4 FragColor;
	in vec3 v_LocalPos;
	uniform sampler2D u_EquirectMap;

	const vec2 invAtan = vec2(0.1591, 0.3183);

	vec2 SampleSphericalMap(vec3 v)
	{
		vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
		uv *= invAtan;
		uv += 0.5;
		return uv;
	}

	void main()
	{
		vec2 uv    = SampleSphericalMap(normalize(v_LocalPos));
		vec3 color = texture(u_EquirectMap, uv).rgb;
		FragColor  = vec4(color, 1.0);
	}
)";

	// Vértices do cubo unitário
	static float s_CubeVertices[] = {
		-1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
		-1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,
		-1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1, 1,
		 1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1, 1,
		-1,-1,-1,  1,-1,-1,  1,-1, 1, -1,-1, 1,
		-1, 1,-1,  1, 1,-1,  1, 1, 1, -1, 1, 1
	};

	static uint32_t s_CubeIndices[] = {
		 0, 1, 2,  2, 3, 0,
		 4, 5, 6,  6, 7, 4,
		 8, 9,10, 10,11, 8,
		12,13,14, 14,15,12,
		16,17,18, 18,19,16,
		20,21,22, 22,23,20
	};

	OpenGLCubemap::~OpenGLCubemap()
	{
		if (m_RendererID)
			glDeleteTextures(1, &m_RendererID);
	}

	bool OpenGLCubemap::LoadFromHDRI(const std::string& filepath)
	{
		// 1. Carrega o HDRI como float
		stbi_set_flip_vertically_on_load(true);
		int width, height, channels;
		float* data = stbi_loadf(filepath.c_str(), &width, &height, &channels, 0);

		if (!data)
		{
			AXE_CORE_ERROR("CubemapTexture: falha ao carregar HDRI '{}'", filepath);
			return false;
		}

		AXE_CORE_INFO("CubemapTexture: HDRI '{}' carregado ({}x{})", filepath, width, height);

		// 2. Cria textura 2D com o HDRI
		uint32_t hdrTexture;
		glCreateTextures(GL_TEXTURE_2D, 1, &hdrTexture);
		glBindTexture(GL_TEXTURE_2D, hdrTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		stbi_image_free(data);

		// 3. Cria o cubemap de destino (512x512 por face)
		uint32_t cubemapSize = 512;
		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_RendererID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
		for (int i = 0; i < 6; i++)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				cubemapSize, cubemapSize, 0, GL_RGB, GL_FLOAT, nullptr);

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		// 4. Converte HDRI para cubemap via framebuffer
		uint32_t captureFBO, captureRBO;
		glCreateFramebuffers(1, &captureFBO);
		glCreateRenderbuffers(1, &captureRBO);
		glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
		glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize, cubemapSize);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

		// 5. Matrizes de captura — 6 direções do cubo
		glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
		glm::mat4 captureViews[] = {
			glm::lookAt(glm::vec3(0), glm::vec3(1, 0, 0), glm::vec3(0,-1, 0)),
			glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
			glm::lookAt(glm::vec3(0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)),
			glm::lookAt(glm::vec3(0), glm::vec3(0,-1, 0), glm::vec3(0, 0,-1)),
			glm::lookAt(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(0,-1, 0)),
			glm::lookAt(glm::vec3(0), glm::vec3(0, 0,-1), glm::vec3(0,-1, 0)),
		};

		// 6. Shader de conversão
		auto equirectShader = Shader::Create(s_EquirectVertSrc, s_EquirectFragSrc);
		equirectShader->Bind();
		equirectShader->SetInt("u_EquirectMap", 0);
		equirectShader->SetMat4("u_Projection", glm::value_ptr(captureProjection));

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, hdrTexture);

		// 7. Renderiza as 6 faces
		glViewport(0, 0, cubemapSize, cubemapSize);
		glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

		// Cubo simples via VAO
		uint32_t cubeVAO, cubeVBO, cubeEBO;
		glGenVertexArrays(1, &cubeVAO);
		glGenBuffers(1, &cubeVBO);
		glGenBuffers(1, &cubeEBO);
		glBindVertexArray(cubeVAO);
		glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(s_CubeVertices), s_CubeVertices, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(s_CubeIndices), s_CubeIndices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

		for (int i = 0; i < 6; i++)
		{
			equirectShader->SetMat4("u_View", glm::value_ptr(captureViews[i]));
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_RendererID, 0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glBindVertexArray(cubeVAO);
			glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
		}

		// 8. Limpeza
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &captureFBO);
		glDeleteRenderbuffers(1, &captureRBO);
		glDeleteTextures(1, &hdrTexture);
		glDeleteVertexArrays(1, &cubeVAO);
		glDeleteBuffers(1, &cubeVBO);
		glDeleteBuffers(1, &cubeEBO);

		m_Loaded = true;
		AXE_CORE_INFO("CubemapTexture: cubemap gerado com sucesso.");
		return true;
	}

	void OpenGLCubemap::Bind(uint32_t slot) const
	{
		glBindTextureUnit(slot, m_RendererID);
	}

}//namespace axe