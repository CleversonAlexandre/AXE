#pragma once
#include "axe/core/types.hpp"

#include "axe/utils/glm_config.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"

#include "axe/renderer/scene_renderer.hpp"
#include "axe/scene/scene.hpp"

#include <memory>
#include <cstdint>

#include <imgui.h>
#include <ImGuizmo.h>
#include <string>

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
		void RenderToFramebuffer(Framebuffer& framebuffer, std::uint32_t width, std::uint32_t height, float timeSeconds);

		void OnMouseRotate(const glm::vec2& delta);
		void OnMousePan(const glm::vec2& delta);
		void OnMouseZoom(float delta);

		Scene* GetScene() { return m_Scene.get(); }
		const Scene* GetScene() const { return m_Scene.get(); }



		void SelectObject(std::uint32_t id) { m_SelectedObjectID = id; }
		void ClearSelection() { m_SelectedObjectID = 0; }

		std::uint32_t GetSelectedObjectID() const { return m_SelectedObjectID; }

		SceneObject* GetSelectedObject();
		const SceneObject* GetSelectedObject() const;
				
		

		
		void DrawGuizmo(const glm::vec2& boundsMin, const glm::vec2& boundsMax);
		ImGuizmo::OPERATION m_GuizmoOperation = ImGuizmo::TRANSLATE;
		std::unique_ptr<EditorCamera> m_Camera;
	private:
		
		std::unique_ptr<SceneRenderer> m_SceneRenderer;
		std::unique_ptr<Scene> m_Scene;
		
				
		std::uint32_t m_SelectedObjectID = 0;
	};
}