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
	// Irradiance Map — convolução difusa
	static const char* s_IrradianceVertSrc = R"(
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

	static const char* s_IrradianceFragSrc = R"(
	#version 460 core
	out vec4 FragColor;
	in vec3 v_LocalPos;
	uniform samplerCube u_EnvironmentMap;

	const float PI = 3.14159265359;

	void main()
	{
		vec3 normal = normalize(v_LocalPos);
		vec3 irradiance = vec3(0.0);

		vec3 up    = vec3(0.0, 1.0, 0.0);
		vec3 right = normalize(cross(up, normal));
		up         = normalize(cross(normal, right));

		float sampleDelta = 0.025;
		float nrSamples   = 0.0;

		for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
		{
			for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
			{
				vec3 tangentSample = vec3(sin(theta) * cos(phi),
										 sin(theta) * sin(phi),
										 cos(theta));
				vec3 sampleVec = tangentSample.x * right +
								 tangentSample.y * up    +
								 tangentSample.z * normal;

				irradiance += texture(u_EnvironmentMap, sampleVec).rgb
							* cos(theta) * sin(theta);
				nrSamples++;
			}
		}

		irradiance = PI * irradiance * (1.0 / float(nrSamples));
		FragColor  = vec4(irradiance, 1.0);
	}
)";

	// Prefiltered Map — convolução especular por roughness
	static const char* s_PrefilterFragSrc = R"(
	#version 460 core
	out vec4 FragColor;
	in vec3 v_LocalPos;
	uniform samplerCube u_EnvironmentMap;
	uniform float u_Roughness;

	const float PI = 3.14159265359;

	float DistributionGGX(vec3 N, vec3 H, float roughness)
	{
		float a      = roughness * roughness;
		float a2     = a * a;
		float NdotH  = max(dot(N, H), 0.0);
		float NdotH2 = NdotH * NdotH;
		float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
		return a2 / (PI * denom * denom);
	}

	float RadicalInverse_VdC(uint bits)
	{
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return float(bits) * 2.3283064365386963e-10;
	}

	vec2 Hammersley(uint i, uint N)
	{
		return vec2(float(i) / float(N), RadicalInverse_VdC(i));
	}

	vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
	{
		float a = roughness * roughness;
		float phi      = 2.0 * PI * Xi.x;
		float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
		float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

		vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

		vec3 up        = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
		vec3 tangent   = normalize(cross(up, N));
		vec3 bitangent = cross(N, tangent);

		return normalize(tangent * H.x + bitangent * H.y + N * H.z);
	}

	void main()
	{
		vec3 N = normalize(v_LocalPos);
		vec3 R = N;
		vec3 V = R;

		const uint SAMPLE_COUNT = 1024u;
		vec3  prefilteredColor  = vec3(0.0);
		float totalWeight       = 0.0;

		for (uint i = 0u; i < SAMPLE_COUNT; i++)
		{
			vec2 Xi = Hammersley(i, SAMPLE_COUNT);
			vec3 H  = ImportanceSampleGGX(Xi, N, u_Roughness);
			vec3 L  = normalize(2.0 * dot(V, H) * H - V);

			float NdotL = max(dot(N, L), 0.0);
			if (NdotL > 0.0)
			{
				float D       = DistributionGGX(N, H, u_Roughness);
				float NdotH   = max(dot(N, H), 0.0);
				float HdotV   = max(dot(H, V), 0.0);
				float pdf     = D * NdotH / (4.0 * HdotV) + 0.0001;
				float resolution = 512.0;
				float saTexel  = 4.0 * PI / (6.0 * resolution * resolution);
				float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
				float mipLevel = u_Roughness == 0.0 ? 0.0
							   : 0.5 * log2(saSample / saTexel);

				prefilteredColor += textureLod(u_EnvironmentMap, L, mipLevel).rgb * NdotL;
				totalWeight      += NdotL;
			}
		}

		prefilteredColor = prefilteredColor / totalWeight;
		FragColor = vec4(prefilteredColor, 1.0);
	}
)";

	// BRDF LUT
	static const char* s_BRDFVertSrc = R"(
	#version 460 core
	layout(location = 0) in vec3 a_Position;
	layout(location = 2) in vec2 a_TexCoord;
	out vec2 v_TexCoord;
	void main()
	{
		v_TexCoord  = a_TexCoord;
		gl_Position = vec4(a_Position.xy, 0.0, 1.0);
	}
)";

	static const char* s_BRDFFragSrc = R"(
	#version 460 core
	out vec2 FragColor;
	in vec2 v_TexCoord;

	const float PI = 3.14159265359;

	float RadicalInverse_VdC(uint bits)
	{
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return float(bits) * 2.3283064365386963e-10;
	}

	vec2 Hammersley(uint i, uint N)
	{
		return vec2(float(i)/float(N), RadicalInverse_VdC(i));
	}

	vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
	{
		float a = roughness * roughness;
		float phi      = 2.0 * PI * Xi.x;
		float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
		float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
		vec3 H = vec3(cos(phi)*sinTheta, sin(phi)*sinTheta, cosTheta);
		vec3 up        = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
		vec3 tangent   = normalize(cross(up, N));
		vec3 bitangent = cross(N, tangent);
		return normalize(tangent*H.x + bitangent*H.y + N*H.z);
	}

	float GeometrySchlickGGX(float NdotV, float roughness)
	{
		float a = roughness;
		float k = (a * a) / 2.0;
		return NdotV / (NdotV * (1.0 - k) + k);
	}

	float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
	{
		float NdotV = max(dot(N, V), 0.0);
		float NdotL = max(dot(N, L), 0.0);
		return GeometrySchlickGGX(NdotV, roughness) *
			   GeometrySchlickGGX(NdotL, roughness);
	}

	vec2 IntegrateBRDF(float NdotV, float roughness)
	{
		vec3 V = vec3(sqrt(1.0 - NdotV*NdotV), 0.0, NdotV);
		float A = 0.0, B = 0.0;
		vec3 N  = vec3(0.0, 0.0, 1.0);
		const uint SAMPLE_COUNT = 1024u;
		for (uint i = 0u; i < SAMPLE_COUNT; i++)
		{
			vec2 Xi = Hammersley(i, SAMPLE_COUNT);
			vec3 H  = ImportanceSampleGGX(Xi, N, roughness);
			vec3 L  = normalize(2.0 * dot(V, H) * H - V);
			float NdotL = max(L.z, 0.0);
			float NdotH = max(H.z, 0.0);
			float VdotH = max(dot(V, H), 0.0);
			if (NdotL > 0.0)
			{
				float G     = GeometrySmith(N, V, L, roughness);
				float G_Vis = (G * VdotH) / (NdotH * NdotV);
				float Fc    = pow(1.0 - VdotH, 5.0);
				A += (1.0 - Fc) * G_Vis;
				B += Fc * G_Vis;
			}
		}
		return vec2(A, B) / float(SAMPLE_COUNT);
	}

	void main()
	{
		FragColor = IntegrateBRDF(v_TexCoord.x, v_TexCoord.y);
	}
)";

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
		// Salva estado OpenGL antes de qualquer operação —
		// esse método pode ser chamado durante o render e não deve
		// corromper o framebuffer, viewport ou depth state ativos
		GLint prevFBO = 0;
		GLint prevViewport[4];
		GLboolean prevDepthMask;
		GLboolean prevDepthTest;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
		glGetIntegerv(GL_VIEWPORT, prevViewport);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
		glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);

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
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
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

		// Limpa erros GL pendentes de operações anteriores
		while (glGetError() != GL_NO_ERROR) {}

		// Desabilita cull face — a câmera está dentro do cubo e precisa
		// ver as faces internas para a captura equiretangular
		glDisable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);

		for (int i = 0; i < 6; i++)
		{
			equirectShader->SetMat4("u_View", glm::value_ptr(captureViews[i]));
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_RendererID, 0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glBindVertexArray(cubeVAO);
			glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
		}

		// Gera mipmaps DEPOIS de todas as faces estarem renderizadas
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

		// Gera os mapas IBL
		GenerateIrradianceMap(captureFBO, captureRBO, cubeVAO, captureViews, captureProjection);
		GeneratePrefilteredMap(captureFBO, captureRBO, cubeVAO, captureViews, captureProjection);
		GenerateBRDFLut(captureFBO, captureRBO);

		// 8. Limpeza
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &captureFBO);
		glDeleteRenderbuffers(1, &captureRBO);
		glDeleteTextures(1, &hdrTexture);
		glDeleteVertexArrays(1, &cubeVAO);
		glDeleteBuffers(1, &cubeVBO);
		glDeleteBuffers(1, &cubeEBO);

		// Restaura estado OpenGL anterior
		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
		glDepthMask(prevDepthMask);
		if (prevDepthTest) glEnable(GL_DEPTH_TEST);
		else               glDisable(GL_DEPTH_TEST);
		glBindVertexArray(0);
		glUseProgram(0);

		m_Loaded = true;
		AXE_CORE_INFO("CubemapTexture: cubemap gerado com sucesso.");
		return true;
	}

	void OpenGLCubemap::Bind(uint32_t slot) const
	{
		glBindTextureUnit(slot, m_RendererID);
	}

	void OpenGLCubemap::BindIrradiance(uint32_t slot) const
	{
		//AXE_CORE_INFO("BindIrradiance slot={} ID={}", slot, m_IrradianceID);
		glBindTextureUnit(slot, m_IrradianceID);
	}

	void OpenGLCubemap::BindPrefiltered(uint32_t slot) const
	{
		glBindTextureUnit(slot, m_PrefilteredID);
	}

	void OpenGLCubemap::BindBRDFLut(uint32_t slot) const
	{
		glBindTextureUnit(slot, m_BRDFLutID);
	}

	bool OpenGLCubemap::HasIBL() const
	{
		return m_IrradianceID != 0 && m_PrefilteredID != 0 && m_BRDFLutID != 0;
	}

	void OpenGLCubemap::GenerateIrradianceMap(uint32_t captureFBO, uint32_t captureRBO,
		uint32_t cubeVAO, glm::mat4* views,
		const glm::mat4& proj)
	{
		constexpr uint32_t size = 32;

		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_IrradianceID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_IrradianceID);
		for (int i = 0; i < 6; i++)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				size, size, 0, GL_RGB, GL_FLOAT, nullptr);

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
		glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);

		auto shader = Shader::Create(s_IrradianceVertSrc, s_IrradianceFragSrc);
		shader->Bind();
		shader->SetInt("u_EnvironmentMap", 0);
		shader->SetMat4("u_Projection", glm::value_ptr(proj));
		glBindTextureUnit(0, m_RendererID);

		glViewport(0, 0, size, size);
		glDisable(GL_CULL_FACE); // câmera dentro do cubo — precisa ver faces internas
		for (int i = 0; i < 6; i++)
		{
			shader->SetMat4("u_View", glm::value_ptr(views[i]));
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_IrradianceID, 0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glBindVertexArray(cubeVAO);
			glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
		}
	}

	void OpenGLCubemap::GeneratePrefilteredMap(uint32_t captureFBO, uint32_t captureRBO,
		uint32_t cubeVAO, glm::mat4* views,
		const glm::mat4& proj)
	{
		constexpr uint32_t size = 128;
		constexpr uint32_t numMips = 5;

		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_PrefilteredID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_PrefilteredID);
		for (int i = 0; i < 6; i++)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				size, size, 0, GL_RGB, GL_FLOAT, nullptr);

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

		auto shader = Shader::Create(s_IrradianceVertSrc, s_PrefilterFragSrc);
		shader->Bind();
		shader->SetInt("u_EnvironmentMap", 0);
		shader->SetMat4("u_Projection", glm::value_ptr(proj));
		glBindTextureUnit(0, m_RendererID);

		glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
		glDisable(GL_CULL_FACE); // câmera dentro do cubo — precisa ver faces internas

		for (uint32_t mip = 0; mip < numMips; mip++)
		{
			uint32_t mipW = static_cast<uint32_t>(size * std::pow(0.5f, mip));
			uint32_t mipH = mipW;

			glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipW, mipH);
			glViewport(0, 0, mipW, mipH);

			float roughness = (float)mip / (float)(numMips - 1);
			shader->SetFloat("u_Roughness", roughness);

			for (int i = 0; i < 6; i++)
			{
				shader->SetMat4("u_View", glm::value_ptr(views[i]));
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
					m_PrefilteredID, mip);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				glBindVertexArray(cubeVAO);
				glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
			}
		}
	}

	void OpenGLCubemap::GenerateBRDFLut(uint32_t captureFBO, uint32_t captureRBO)
	{
		constexpr uint32_t size = 512;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_BRDFLutID);
		glBindTexture(GL_TEXTURE_2D, m_BRDFLutID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, size, size, 0, GL_RG, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
		glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, m_BRDFLutID, 0);

		// Quad fullscreen
		float quadVerts[] = {
			-1,-1,0, 0,0,0,  0,0,
			 1,-1,0, 0,0,0,  1,0,
			 1, 1,0, 0,0,0,  1,1,
			-1, 1,0, 0,0,0,  0,1
		};
		uint32_t quadIdx[] = { 0,1,2, 2,3,0 };

		uint32_t vao, vbo, ebo;
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ebo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

		auto shader = Shader::Create(s_BRDFVertSrc, s_BRDFFragSrc);
		shader->Bind();

		glViewport(0, 0, size, size);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		glDeleteVertexArrays(1, &vao);
		glDeleteBuffers(1, &vbo);
		glDeleteBuffers(1, &ebo);
	}

}//namespace axe