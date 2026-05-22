#pragma once
#include "axe/core/types.hpp"
#include "axe/scene/scene.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"
#include "axe/graphics/editor_camera.hpp"

#include "axe/graphics/renderer/line_renderer.hpp"
#include "axe/graphics/renderer/mesh_renderer.hpp"
#include <entt/entt.hpp>

#include "axe/lighting/directional_light.hpp"
namespace  axe
{
	
	class AXE_API SceneRenderer
	{
	public:
		SceneRenderer();		
		void RenderScene(const Scene& scene, const EditorCamera& camera, entt::entity selectedEntity);


		// Sobrecarga para GameCamera — recebe matrizes diretamente
		void RenderScene(const Scene& scene,
			const glm::mat4& view,
			const glm::mat4& projection,
			const glm::vec3& cameraPosition,
			entt::entity selectedEntity);

			MeshRenderer& GetMeshRenderer() { return m_MeshRenderer; }
	private:
		CubeRenderer m_CubeRenderer;
		MeshRenderer m_MeshRenderer;
		LineRenderer m_LineRenderer;

		

		void RenderEntity(const Scene& scene, entt::entity entity,
			const glm::mat4& parentTransform, entt::entity selectedEntity,
			const DirectionalLight* light);
	};
}