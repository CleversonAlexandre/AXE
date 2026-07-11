#include "opengl_point_shadow_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/renderer/render_queue.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace axe
{
	// Depth-only: escreve a DISTÂNCIA LINEAR até a luz, normalizada pelo
	// far plane (= Radius da luz), no R32F. Ver comentário no header
	// abstrato sobre por que distância linear e não depth perspectivo.
	static const char* kVS = R"(
		#version 460 core
		layout(location = 0) in vec3 a_Position;

		uniform mat4 u_Model;
		uniform mat4 u_ViewProjection;

		out vec3 v_WorldPos;

		void main()
		{
			vec4 world = u_Model * vec4(a_Position, 1.0);
			v_WorldPos = world.xyz;
			gl_Position = u_ViewProjection * world;
		}
	)";

	static const char* kFS = R"(
		#version 460 core
		in vec3 v_WorldPos;

		uniform vec3  u_LightPos;
		uniform float u_FarPlane;

		out float FragDist;

		void main()
		{
			FragDist = length(v_WorldPos - u_LightPos) / u_FarPlane;
		}
	)";

	OpenGLPointShadowPass::~OpenGLPointShadowPass()
	{
		if (m_CubeArray) glDeleteTextures(1, &m_CubeArray);
		if (m_DepthRBO) glDeleteRenderbuffers(1, &m_DepthRBO);
		if (m_FBO) glDeleteFramebuffers(1, &m_FBO);
	}

	void OpenGLPointShadowPass::Initialize(uint32_t resolution)
	{
		m_Resolution = resolution;

		// Cube map ARRAY R32F — kMaxShadowLights luzes x 6 faces em UMA
		// textura (um único texture unit no lighting). Distância no canal
		// vermelho; clear em 1.0 = "nada bloqueando até o far plane".
		glCreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &m_CubeArray);
		glTextureStorage3D(m_CubeArray, 1, GL_R32F,
			resolution, resolution, kMaxShadowLights * 6);
		glTextureParameteri(m_CubeArray, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_CubeArray, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_CubeArray, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(m_CubeArray, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(m_CubeArray, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glCreateRenderbuffers(1, &m_DepthRBO);
		glNamedRenderbufferStorage(m_DepthRBO, GL_DEPTH_COMPONENT24,
			resolution, resolution);

		glCreateFramebuffers(1, &m_FBO);
		glNamedFramebufferRenderbuffer(m_FBO, GL_DEPTH_ATTACHMENT,
			GL_RENDERBUFFER, m_DepthRBO);

		m_DepthShader = Shader::Create(kVS, kFS);
		if (!m_DepthShader)
		{
			AXE_CORE_ERROR("PointShadowPass: falha ao compilar depth shader.");
			return;
		}

		m_Initialized = true;
		AXE_CORE_INFO("PointShadowPass: inicializado ({}x{}, {} layers).",
			resolution, resolution, kMaxShadowLights);
	}

	void OpenGLPointShadowPass::RenderLightShadow(const RenderQueue& queue,
		int layer, const glm::vec3& lightPos, float farPlane)
	{
		if (!m_Initialized || layer < 0 || layer >= kMaxShadowLights) return;

		// Salva o alvo atual — quem clobbera estado, restaura estado
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_SavedFBO);
		glGetIntegerv(GL_VIEWPORT, m_SavedViewport);

		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glViewport(0, 0, m_Resolution, m_Resolution);

		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);

		// Renderiza as BACKFACES (cull front): o clássico anti-acne de
		// shadow mapping — a "casca de trás" da geometria empurra a
		// superfície de comparação pra dentro do objeto, longe da face
		// iluminada onde o acne apareceria.
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);

		// 6 faces do cubemap — direções/ups padrão GL
		struct Face { glm::vec3 Fwd, Up; };
		const Face faces[6] = {
			{ { 1, 0, 0}, {0,-1, 0} }, { {-1, 0, 0}, {0,-1, 0} },
			{ { 0, 1, 0}, {0, 0, 1} }, { { 0,-1, 0}, {0, 0,-1} },
			{ { 0, 0, 1}, {0,-1, 0} }, { { 0, 0,-1}, {0,-1, 0} },
		};

		// fov 90° + aspect 1 cobre exatamente 1 face do cubo; near curto
		// pra sombra de objetos colados na luz; far = Radius (nada além
		// do raio de influência importa)
		glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f,
			0.05f, glm::max(farPlane, 0.1f));

		m_DepthShader->Bind();
		m_DepthShader->SetFloat3("u_LightPos", lightPos);
		m_DepthShader->SetFloat("u_FarPlane", glm::max(farPlane, 0.1f));

		const float clearDist[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		for (int f = 0; f < 6; f++)
		{
			glNamedFramebufferTextureLayer(m_FBO, GL_COLOR_ATTACHMENT0,
				m_CubeArray, 0, layer * 6 + f);

			glClearBufferfv(GL_COLOR, 0, clearDist);
			glClear(GL_DEPTH_BUFFER_BIT);

			glm::mat4 view = glm::lookAt(lightPos,
				lightPos + faces[f].Fwd, faces[f].Up);
			m_DepthShader->SetMat4("u_ViewProjection", glm::value_ptr(proj * view));

			for (const auto& dc : queue.Meshes)
			{
				if (!dc.Mesh) continue;
				// Transparentes não bloqueiam luz (mesma regra do bake
				// de probes e do CSM)
				if (dc.Material && dc.Material->IsTransparent) continue;

				m_DepthShader->SetMat4("u_Model", glm::value_ptr(dc.Transform));
				dc.Mesh->GetVertexArray()->Bind();
				glDrawElements(GL_TRIANGLES, dc.Mesh->GetIndexCount(),
					GL_UNSIGNED_INT, nullptr);
			}
		}

		glCullFace(GL_BACK);
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_SavedFBO);
		glViewport(m_SavedViewport[0], m_SavedViewport[1],
			m_SavedViewport[2], m_SavedViewport[3]);
	}

} // namespace axe