#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/log/log.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/utils/glm_config.hpp"

// viewport_renderer.cpp — sem includes diretos de OpenGL
#include <imgui.h>
#include <ImGuizmo.h>

#include "axe/graphics/game_camera.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/mesh/mesh.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace {
	// Shader de silhueta para drag preview — cor sólida + alpha baixo
	static const char* s_GhostVert = R"(
	#version 460 core
	layout(location = 0) in vec3 a_Position;
	layout(location = 1) in vec3 a_Normal;
	uniform mat4 u_ViewProjection;
	uniform mat4 u_Model;
	void main() {
		gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
	}
)";
	static const char* s_GhostFrag = R"(
	#version 460 core
	out vec4 FragColor;
	uniform vec4 u_Color;
	void main() { FragColor = u_Color; }
)";
	static std::shared_ptr<axe::Shader> s_GhostShader;
} // namespace

namespace axe
{

	static bool DecomposeTransform(const glm::mat4& transform, glm::vec3& position,
		glm::vec3& rotation, glm::vec3& scale)
	{
		using namespace glm;
		vec3 skew; vec4 perspective; quat orientation;
		if (!decompose(transform, scale, orientation, position, skew, perspective))
			return false;
		rotation = eulerAngles(orientation);
		return true;
	}


	void ViewportRenderer::Initialize()
	{
		s_GhostShader = axe::Shader::Create(s_GhostVert, s_GhostFrag);
		m_SceneRenderer = std::make_unique<SceneRenderer>();
		m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 1000.0f);
		m_SkyboxRenderer.Initialize();
		m_GridRenderer.Initialize();
		m_ColliderDebugRenderer.Initialize();

		FramebufferSpecification hdrSpec;
		hdrSpec.Width = 1280;
		hdrSpec.Height = 720;
		hdrSpec.Attachments = {
			FramebufferTextureFormat::RGBA16F,
			FramebufferTextureFormat::DEPTH32F,
		};
		m_HDRFramebuffer = Framebuffer::Create(hdrSpec);

		m_PostProcess = PostProcessPass::Create();
		m_PostProcess->Initialize(1280, 720);

		// ✅ Inicializa SSAO aqui — contexto OpenGL garantido
		m_SceneRenderer->InitializeDeferredPasses(1280, 720);
	}


	void ViewportRenderer::SetPickingEnabled(bool enabled)
	{
		m_PickingEnabled = enabled;
	}

	void ViewportRenderer::RenderToFramebuffer(Framebuffer& framebuffer,
		std::uint32_t width, std::uint32_t height, float timeSeconds)
	{


		// 1. Resize HDR primeiro
		auto& hdrSpec = m_HDRFramebuffer->GetSpecification();
		if (hdrSpec.Width != width || hdrSpec.Height != height)
			m_HDRFramebuffer->Resize(width, height);

		if (!m_PostProcess->IsInitialized())
			m_PostProcess->Initialize(width, height);
		else
			m_PostProcess->Resize(width, height);

		// Configura SceneRenderer para editor camera OU game camera
		if (m_GameCamera)
		{
			m_SceneRenderer->SetTargetFramebuffer(m_HDRFramebuffer->GetRendererID());
			m_SceneRenderer->SetDeferredEnabled(true);
			m_SceneRenderer->SetDeferredSupported(true);
		}
		else if (m_Camera)
		{
			m_SceneRenderer->SetTargetFramebuffer(m_HDRFramebuffer->GetRendererID());
			m_SceneRenderer->SetDeferredEnabled(!m_PreviewMode);
			if (m_PreviewMode)
				m_SceneRenderer->SetDeferredSupported(false);
		}

		// Sincroniza HDRI ANTES de bindar o framebuffer —
		// LoadFromHDRI muda viewport e FBO internamente, deve rodar com estado limpo
		if (m_Scene)
		{
			auto& registry = m_Scene->GetRegistry();
			for (auto entity : registry.view<EnvironmentComponent>())
			{
				auto& ec = registry.get<EnvironmentComponent>(entity);
				if (m_Environment)
				{
					m_Environment->SkyboxRotation = ec.SkyboxRotation;
					if (!ec.HDRIPath.empty() && ec.HDRIPath != m_Environment->SkyboxPath)
						m_Environment->LoadHDRI(ec.HDRIPath);
				}
				break;
			}
		}

		// 3. Binda HDR e limpa
		m_HDRFramebuffer->Bind();
		RenderCommand::SetColorWrite(true);
		RenderCommand::SetDepthWrite(true);
		RenderCommand::SetViewport(0, 0, width, height);
		RenderCommand::SetClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		RenderCommand::Clear();

		entt::entity selected = m_SelectedEntity ? *m_SelectedEntity : entt::null;

		if (m_GameCamera)
		{
			float aspect = height > 0 ? (float)width / (float)height : 1.0f;

			// Lê PostProcess igual ao modo editor
			if (m_Scene)
			{
				auto& registry = m_Scene->GetRegistry();
				for (auto entity : registry.view<PostProcessComponent>())
				{
					auto& pp = registry.get<PostProcessComponent>(entity);
					m_PostProcessSettings = pp.Settings;
					if (m_SceneRenderer)
						m_SceneRenderer->SetSSAOSettings(pp.SSAO);
					break;
				}
			}

			if (m_SceneRenderer && m_Environment)
				m_SceneRenderer->SetEnvironment(m_Environment);

			if (m_SceneRenderer && m_Environment && m_Environment->HasSkybox())
			{
				m_SkyboxRenderer.SetCubemap(m_Environment->Skybox);
				m_SceneRenderer->SetSkyboxRenderer(
					&m_SkyboxRenderer,
					m_GameCamera->GetViewMatrix(),
					m_GameCamera->GetProjectionMatrix(aspect));
			}

			if (m_SceneRenderer && m_Scene)
				m_SceneRenderer->RenderScene(
					*m_Scene,
					m_GameCamera->GetViewMatrix(),
					m_GameCamera->GetProjectionMatrix(aspect),
					m_GameCamera->GetPosition(),
					entt::null);
		}
		else
		{
			if (m_Camera && height > 0)
			{
				m_Camera->SetAspectRatio((float)width / (float)height);
				m_Camera->SetViewportSize((float)width, (float)height);
			}
			if (m_SceneRenderer && m_Environment)
				m_SceneRenderer->SetEnvironment(m_Environment);

			// Lê PostProcessComponent antes de renderizar
			if (m_Scene)
			{
				auto& registry = m_Scene->GetRegistry();
				for (auto entity : registry.view<PostProcessComponent>())
				{
					auto& pp = registry.get<PostProcessComponent>(entity);
					m_PostProcessSettings = pp.Settings;
					if (m_SceneRenderer)
						m_SceneRenderer->SetSSAOSettings(pp.SSAO);
					break;
				}
			}

			// Skybox
			if (m_SceneRenderer && m_Environment && m_Environment->HasSkybox() && m_Camera)
			{
				m_SkyboxRenderer.SetCubemap(m_Environment->Skybox);
				m_SceneRenderer->SetSkyboxRenderer(
					&m_SkyboxRenderer,
					m_Environment->GetSkyboxView(m_Camera->GetViewMatrix()),
					m_Camera->GetProjectionMatrix());
			}
			else if (m_SceneRenderer)
			{
				m_SceneRenderer->SetSkyboxRenderer(nullptr, {}, {});
			}

			if (m_SceneRenderer && m_Scene && m_Camera)
				m_SceneRenderer->RenderScene(*m_Scene, *m_Camera, selected);

			// Grid — dentro do HDR framebuffer, com valores lineares baixos
			// que sobrevivem ao tone mapping sem saturar
			if (ShowGrid && m_Camera)
				m_GridRenderer.Render(
					m_Camera->GetViewMatrix(),
					m_Camera->GetProjectionMatrix());

			// Picking
			if (m_Scene && m_Camera && m_PickingEnabled)
			{
				m_PickingRenderer.Resize(width, height);
				m_PickingRenderer.Begin(m_Camera->GetViewProjectionMatrix());
				auto& registry = m_Scene->GetRegistry();
				for (auto entity : registry.view<TransformComponent>())
				{
					if (registry.any_of<LightComponent>(entity)) continue;
					auto& tc = registry.get<TransformComponent>(entity);
					glm::mat4 model = tc.Data.GetMatrix();
					auto* mc = registry.try_get<MeshComponent>(entity);
					std::uint32_t pickID = (std::uint32_t)entity;
					if (mc && mc->Data)
						m_PickingRenderer.DrawMesh(*mc->Data, model, pickID);
					else
						m_PickingRenderer.DrawCube(model, pickID);
				}
				m_PickingRenderer.End();
			}
		}

		m_HDRFramebuffer->Unbind();

		// 4. Post process
		framebuffer.Bind();
		RenderCommand::SetViewport(0, 0, width, height);

		m_PostProcess->Execute(
			m_HDRFramebuffer->GetColorAttachmentRendererID(),
			m_PostProcessSettings);

		// Collider wireframes — só no modo editor
		if (ShowColliders && m_Camera && !m_GameCamera && m_Scene)
			m_ColliderDebugRenderer.Render(
				*m_Scene,
				m_Camera->GetViewMatrix(),
				m_Camera->GetProjectionMatrix());

		// Raio de alcance das Point Lights — só no modo editor
		if (ShowLights && m_Camera && !m_GameCamera && m_Scene)
			m_ColliderDebugRenderer.RenderLights(
				*m_Scene,
				m_Camera->GetViewMatrix(),
				m_Camera->GetProjectionMatrix());

		// ── Ghost preview de drag & drop ────────────────────────────────────────
		// Renderizado no framebuffer final com blending, sobre tudo
		if (m_HasGhost && m_GhostMesh && m_Camera && s_GhostShader)
		{
			framebuffer.Bind();
			RenderCommand::SetViewport(0, 0, width, height);

			// Blending: silhueta azul-ciano semitransparente
			RenderCommand::SetBlend(true);
			RenderCommand::SetBlendFunc(0x0302, 0x0303); // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
			RenderCommand::SetDepthWrite(false); // não escreve depth — fica sempre visível
			RenderCommand::SetDepthTest(false);
			RenderCommand::SetCullFace(false);

			glm::mat4 vp = m_Camera->GetViewProjectionMatrix();
			s_GhostShader->Bind();
			s_GhostShader->SetMat4("u_ViewProjection", glm::value_ptr(vp));
			s_GhostShader->SetMat4("u_Model", glm::value_ptr(m_GhostTransform));
			s_GhostShader->SetFloat4("u_Color", { 0.3f, 0.7f, 1.0f, 0.45f }); // azul-ciano

			m_GhostMesh->GetVertexArray()->Bind();
			RenderCommand::DrawIndexedCount(m_GhostMesh->GetIndexCount());

			// Restaura estado
			RenderCommand::SetDepthTest(true);
			RenderCommand::SetDepthWrite(true);
			RenderCommand::SetBlend(false);
			RenderCommand::SetCullFace(true);

			framebuffer.Unbind();
		}
		else
		{
			framebuffer.Unbind();
		}
	}
	std::uint32_t ViewportRenderer::PickObject(float mouseX, float mouseY)
	{
		std::uint32_t height = m_PickingRenderer.GetFramebufferHeight();
		std::uint32_t x = static_cast<std::uint32_t>(mouseX);
		std::uint32_t y = static_cast<std::uint32_t>(height - mouseY - 1);
		return m_PickingRenderer.ReadPixel(x, y);
	}

	void ViewportRenderer::ResizePicking(std::uint32_t width, std::uint32_t height)
	{
		m_PickingRenderer.Resize(width, height);
	}

	void ViewportRenderer::OnMouseRotate(const glm::vec2& delta) { if (m_Camera) m_Camera->Rotate(delta); }
	void ViewportRenderer::OnMousePan(const glm::vec2& delta) { if (m_Camera) m_Camera->Pan(delta); }
	void ViewportRenderer::OnMouseZoom(float delta) { if (m_Camera) m_Camera->Zoom(delta); }

	void ViewportRenderer::DrawGuizmo(const glm::vec2& boundsMin, const glm::vec2& boundsMax)
	{

		if (!m_Scene || !m_SelectedEntity || *m_SelectedEntity == entt::null || !m_Camera)
			return;

		auto& registry = m_Scene->GetRegistry();
		if (!registry.valid(*m_SelectedEntity))
			return;

		// Ignora gizmo em luzes
		if (registry.any_of<LightComponent>(*m_SelectedEntity))
			return;

		auto* tc = registry.try_get<TransformComponent>(*m_SelectedEntity);
		if (!tc) return;

		float width = boundsMax.x - boundsMin.x;
		float height = boundsMax.y - boundsMin.y;
		if (width <= 0.0f || height <= 0.0f) return;

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(boundsMin.x, boundsMin.y, width, height);

		glm::mat4 view = m_Camera->GetViewMatrix();
		glm::mat4 projection = m_Camera->GetProjectionMatrix();
		glm::mat4 model = tc->Data.GetMatrix();

		// Snap
		float snapValues[3] = { SnapValue, SnapValue, SnapValue };
		if (m_GuizmoOperation == ImGuizmo::ROTATE)
			snapValues[0] = snapValues[1] = snapValues[2] = SnapAngle;
		else if (m_GuizmoOperation == ImGuizmo::SCALE)
			snapValues[0] = snapValues[1] = snapValues[2] = SnapScale;

		const float* snap = SnapEnabled ? snapValues : nullptr;

		ImGuizmo::Manipulate(
			glm::value_ptr(view),
			glm::value_ptr(projection),
			m_GuizmoOperation,
			ImGuizmo::LOCAL,
			glm::value_ptr(model),
			nullptr,
			snap
		);

		if (ImGuizmo::IsUsing())
		{
			// Salva transform antes da primeira modificação
			if (!m_GizmoWasUsing)
			{
				m_TransformSnapshot = tc->Data;
				m_GizmoWasUsing = true;
			}

			tc->Data.WorldMatrix = model;
			tc->Data.UseWorldMatrix = true;

			glm::vec3 position, rotation, scale;
			if (DecomposeTransform(model, position, rotation, scale))
			{
				tc->Data.Position = position;
				tc->Data.Rotation = rotation;
				tc->Data.Scale = scale;
			}
		}
		else if (m_GizmoWasUsing)
		{
			// Gizmo soltou — registra comando de undo
			m_GizmoWasUsing = false;

			if (m_CommandHistory)
			{
				Transform before = m_TransformSnapshot;
				Transform after = tc->Data;
				entt::entity ent = *m_SelectedEntity;
				auto* scene = m_Scene;

				m_CommandHistory->Push({
					"Mover objeto",
					[scene, ent, after]() {
						auto* t = scene->GetRegistry().try_get<TransformComponent>(ent);
						if (t) t->Data = after;
					},
					[scene, ent, before]() {
						auto* t = scene->GetRegistry().try_get<TransformComponent>(ent);
						if (t) t->Data = before;
					}
					});
			}
		}


	}

	void ViewportRenderer::DrawGrid()
	{
		if (!m_Camera) return;
		m_GridRenderer.Render(m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix());
	}

	void ViewportRenderer::Resize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0) return;
		if (m_HDRFramebuffer)
			m_HDRFramebuffer->Resize(width, height);
		if (m_PostProcess && m_PostProcess->IsInitialized())
			m_PostProcess->Resize(width, height);
	}

} // namespace axe