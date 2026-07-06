#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/graphics/renderer/taa_pass.hpp"
#include "axe/graphics/renderer/ssr_pass.hpp"
#include "picking_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/scene/transform.hpp"
#include "axe/core/command_history.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <imgui.h>
#include <ImGuizmo.h>

#include "axe/graphics/renderer/grid_renderer.hpp"
#include "axe/graphics/renderer/collider_debug_renderer.hpp"
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

		void SetCommandHistory(CommandHistory* history) { m_CommandHistory = history; }

		bool  ShowGrid = true;
		bool  ShowColliders = true; // wireframe dos colliders no editor
		bool  ShowLights = true;    // wireframe do raio das Point Lights no editor
		bool  SnapEnabled = false;
		float SnapValue = 0.5f;   // unidades para translate
		float SnapAngle = 15.0f;  // graus para rotate
		float SnapScale = 0.1f;   // para scale

		ImGuizmo::OPERATION m_GuizmoOperation = ImGuizmo::TRANSLATE;
		std::unique_ptr<EditorCamera> m_Camera;

		void SetGameCamera(GameCamera* cam) { m_GameCamera = cam; }

		// Força recompilação do shader do Lighting Pass — ver
		// SceneRenderer::RecompileLightingShader.
		void RecompileLightingShader()
		{
			if (m_SceneRenderer) m_SceneRenderer->RecompileLightingShader();
		}

		void SetEnvironment(SceneEnvironment* env) { m_Environment = env; }
		void DrawGrid();
		void Resize(uint32_t width, uint32_t height);

		SceneRenderer* GetSceneRenderer() { return m_SceneRenderer.get(); }
		void SetPreviewMode(bool preview) { m_PreviewMode = preview; }

		// ── Drag & Drop ghost preview ─────────────────────────────────────────
		// Chame SetDragGhost antes de RenderToFramebuffer para mostrar silhueta.
		// Chame ClearDragGhost quando o drag terminar.
		void SetDragGhost(std::shared_ptr<Mesh> mesh, const glm::mat4& transform)
		{
			m_GhostMesh = mesh;
			m_GhostTransform = transform;
			m_HasGhost = true;
		}
		void ClearDragGhost() { m_HasGhost = false; m_GhostMesh = nullptr; }
		/// <summary>
		/// temp
		Scene* GetScene() const { return m_Scene; }
		void SetPickingEnabled(bool enabled);
		/// </summary>
	private:
		bool m_PickingEnabled = true;

		// Ghost preview
		bool                   m_HasGhost = false;
		std::shared_ptr<Mesh>  m_GhostMesh;
		glm::mat4              m_GhostTransform{ 1.0f };

		CommandHistory* m_CommandHistory = nullptr;
		bool            m_GizmoWasUsing = false;
		Transform       m_TransformSnapshot;
		std::unique_ptr<SceneRenderer> m_SceneRenderer;
		PickingRenderer                m_PickingRenderer;

		Scene* m_Scene = nullptr;
		entt::entity* m_SelectedEntity = nullptr;

		GameCamera* m_GameCamera = nullptr;

		SkyboxRenderer   m_SkyboxRenderer;
		GridRenderer            m_GridRenderer;
		ColliderDebugRenderer   m_ColliderDebugRenderer;
		SceneEnvironment* m_Environment = nullptr;

		std::shared_ptr<PostProcessPass>  m_PostProcess;
		std::shared_ptr<Framebuffer>      m_HDRFramebuffer;
		std::shared_ptr<TAAPass>          m_TAAPass;
		TAASettings                       m_TAASettings;
		std::shared_ptr<SSRPass>          m_SSRPass;
		SSRSettings                       m_SSRSettings;
		PostProcessSettings               m_PostProcessSettings;
		bool  m_PreviewMode = false;
		float m_LastTimeSeconds = 0.0f; // para cálculo de dt no Time of Day
	};

} // namespace axe