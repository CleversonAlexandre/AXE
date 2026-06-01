#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/log/log.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/utils/glm_config.hpp"

#include <glad/glad.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include "axe/graphics/game_camera.hpp"
#include "axe/graphics/render_command.hpp"

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
		m_SceneRenderer = std::make_unique<SceneRenderer>();
		m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 1000.0f);
		m_SkyboxRenderer.Initialize();

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

		if (m_Camera)
		{
			
			m_SceneRenderer->SetTargetFramebuffer(m_HDRFramebuffer->GetRendererID());
			m_SceneRenderer->SetDeferredEnabled(!m_PreviewMode);
			if (m_PreviewMode)
				m_SceneRenderer->SetDeferredSupported(false);
		}

		// 3. Binda HDR e limpa
		m_HDRFramebuffer->Bind();
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);
		RenderCommand::SetViewport(0, 0, width, height);
		RenderCommand::SetClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		RenderCommand::Clear();

		entt::entity selected = m_SelectedEntity ? *m_SelectedEntity : entt::null;

		if (m_GameCamera)
		{
			float aspect = height > 0 ? (float)width / (float)height : 1.0f;
			if (m_SceneRenderer && m_Environment)
				m_SceneRenderer->SetEnvironment(m_Environment);

			// ✅ Seta skybox para o GameCamera também
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

			// Skybox
			if (m_SceneRenderer && m_Environment && m_Environment->HasSkybox() && m_Camera)
			{
				m_SkyboxRenderer.SetCubemap(m_Environment->Skybox);
				m_SceneRenderer->SetSkyboxRenderer(
					&m_SkyboxRenderer,
					m_Camera->GetViewMatrix(),
					m_Camera->GetProjectionMatrix());
			}
			else if (m_SceneRenderer)
			{
				m_SceneRenderer->SetSkyboxRenderer(nullptr, {}, {});
			}

			if (m_SceneRenderer && m_Scene && m_Camera)
				m_SceneRenderer->RenderScene(*m_Scene, *m_Camera, selected);

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

		if (m_Scene)
		{
			auto& registry = m_Scene->GetRegistry();
			for (auto entity : registry.view<PostProcessComponent>())
			{
				m_PostProcessSettings = registry.get<PostProcessComponent>(entity).Settings;
				break;
			}
		}

		m_PostProcess->Execute(
			m_HDRFramebuffer->GetColorAttachmentRendererID(),
			m_PostProcessSettings);
		framebuffer.Unbind();
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
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(boundsMin.x, boundsMin.y, width, height);

		glm::mat4 view = m_Camera->GetViewMatrix();
		glm::mat4 projection = m_Camera->GetProjectionMatrix();
		glm::mat4 model = tc->Data.GetMatrix();

		ImGuizmo::Manipulate(
			glm::value_ptr(view),
			glm::value_ptr(projection),
			m_GuizmoOperation,
			ImGuizmo::LOCAL,
			glm::value_ptr(model)
		);

		if (ImGuizmo::IsUsing())
		{
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

		
	}

	void ViewportRenderer::DrawGrid()
	{
		if (!m_Camera) return;

		static const float identityMatrix[16] = {
			1.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f
		};

		ImGuizmo::DrawGrid(
			glm::value_ptr(m_Camera->GetViewMatrix()),
			glm::value_ptr(m_Camera->GetProjectionMatrix()),
			identityMatrix, 100.f
		);
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