#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "picking_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <imgui.h>
#include <ImGuizmo.h>

#include "axe/graphics/game_camera.hpp"
#include "skybox_renderer.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"

namespace axe
{

	class Framebuffer;
	class EditorCamera;
	class SceneRenderer;
	class Scene;


	class AXE_API ViewportRenderer
	{
	public:
		void Initialize();

		void SetScene(Scene* scene) { m_Scene = scene; }
		void SetSelectedEntity(entt::entity* e) { m_SelectedEntity = e; }

		void RenderToFramebuffer(Framebuffer& framebuffer, std::uint32_t width,
			std::uint32_t height, float timeSeconds);

		void OnMouseRotate(const glm::vec2& delta);
		void OnMousePan(const glm::vec2& delta);
		void OnMouseZoom(float delta);

		void DrawGuizmo(const glm::vec2& boundsMin, const glm::vec2& boundsMax);

		std::uint32_t PickObject(float mouseX, float mouseY);
		void ResizePicking(std::uint32_t width, std::uint32_t height);

		ImGuizmo::OPERATION m_GuizmoOperation = ImGuizmo::TRANSLATE;
		std::unique_ptr<EditorCamera> m_Camera;

		void SetGameCamera(GameCamera* cam) { m_GameCamera = cam; }

		void SetEnvironment(SceneEnvironment* env) { m_Environment = env; }
		void DrawGrid();
		void Resize(uint32_t width, uint32_t height);

		SceneRenderer* GetSceneRenderer() { return m_SceneRenderer.get(); }

	/// <summary>
	/// temp
	 Scene* GetScene() const { return m_Scene; }
	 void SetPickingEnabled(bool enabled);
	/// </summary>
	private:
		bool m_PickingEnabled = true;
		std::unique_ptr<SceneRenderer> m_SceneRenderer;
		PickingRenderer                m_PickingRenderer;

		Scene* m_Scene = nullptr;
		entt::entity* m_SelectedEntity = nullptr;

		GameCamera* m_GameCamera = nullptr;

		SkyboxRenderer   m_SkyboxRenderer;
		SceneEnvironment* m_Environment = nullptr;

		std::shared_ptr<PostProcessPass>  m_PostProcess;
		std::shared_ptr<Framebuffer>      m_HDRFramebuffer;
		PostProcessSettings               m_PostProcessSettings;
	};

} // namespace axe