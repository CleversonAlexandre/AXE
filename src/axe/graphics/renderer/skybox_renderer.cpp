#include "skybox_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/log/log.hpp"
#include "axe/utils/glm_config.hpp"

#include <glad/glad.h>

namespace axe
{

	static const char* s_SkyboxVertSrc = R"(
	#version 460 core
	layout(location = 0) in vec3 a_Position;

	out vec3 v_TexCoords;

	uniform mat4 u_Projection;
	uniform mat4 u_View;

	void main()
	{
		v_TexCoords = a_Position;
		// Remove a translação da view matrix — skybox sempre ao redor da câmera
		vec4 pos    = u_Projection * mat4(mat3(u_View)) * vec4(a_Position, 1.0);
		// Truque: z = w para skybox sempre estar no far plane (depth = 1.0)
		gl_Position = pos.xyww;
	}
)";

	static const char* s_SkyboxFragSrc = R"(
	#version 460 core
	out vec4 FragColor;
	in vec3 v_TexCoords;

	uniform samplerCube u_Skybox;

	// Tone mapping simples para HDR
	vec3 ToneMap(vec3 hdr)
	{
		return hdr / (hdr + vec3(1.0)); // Reinhard
	}

	void main()
	{
		vec3 color = texture(u_Skybox, v_TexCoords).rgb;
		color      = ToneMap(color);
		color      = pow(color, vec3(1.0 / 2.2)); // Gamma correction
		FragColor  = vec4(color, 1.0);
	}
)";

	static float s_Vertices[] = {
		-1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
		-1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,
		-1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1, 1,
		 1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1, 1,
		-1,-1,-1,  1,-1,-1,  1,-1, 1, -1,-1, 1,
		-1, 1,-1,  1, 1,-1,  1, 1, 1, -1, 1, 1
	};

	static uint32_t s_Indices[] = {
		 0, 1, 2,  2, 3, 0,
		 4, 5, 6,  6, 7, 4,
		 8, 9,10, 10,11, 8,
		12,13,14, 14,15,12,
		16,17,18, 18,19,16,
		20,21,22, 22,23,20
	};

	SkyboxRenderer::SkyboxRenderer() = default;

	void SkyboxRenderer::Initialize()
	{
		m_Shader = Shader::Create(s_SkyboxVertSrc, s_SkyboxFragSrc);

		glGenVertexArrays(1, &m_VAO);
		glGenBuffers(1, &m_VBO);
		glGenBuffers(1, &m_EBO);

		glBindVertexArray(m_VAO);
		glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(s_Vertices), s_Vertices, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(s_Indices), s_Indices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glBindVertexArray(0);
	}

	SkyboxRenderer::~SkyboxRenderer()
	{
		glDeleteVertexArrays(1, &m_VAO);
		glDeleteBuffers(1, &m_VBO);
		glDeleteBuffers(1, &m_EBO);
	}

	void SkyboxRenderer::Render(const glm::mat4& view, const glm::mat4& projection)
	{
		if (!HasCubemap()) return;

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE); // ← adiciona — skybox é visto de dentro

		m_Shader->Bind();
		m_Shader->SetMat4("u_View", glm::value_ptr(view));
		m_Shader->SetMat4("u_Projection", glm::value_ptr(projection));
		m_Shader->SetInt("u_Skybox", 0);

		m_Cubemap->Bind(0);

		glBindVertexArray(m_VAO);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);

		glEnable(GL_CULL_FACE); // ← restaura
		glEnable(GL_DEPTH_TEST);
	}

} // namespace axe