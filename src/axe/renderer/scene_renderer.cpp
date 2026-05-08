#include "scene_renderer.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/scene/components.hpp"

namespace axe
{

	SceneRenderer::SceneRenderer() {}

	void SceneRenderer::RenderScene(const Scene& scene,
		const glm::mat4& view,
		const glm::mat4& projection,
		const glm::vec3& cameraPosition,
		entt::entity selectedEntity)
	{
		glm::mat4 viewProjection = projection * view;

		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		// Detecta luz
		const DirectionalLight* light = nullptr;
		for (auto entity : registry.view<LightComponent>())
		{
			auto& lc = registry.get<LightComponent>(entity);
			if (lc.Data) { light = lc.Data.get(); break; }
		}

		m_CubeRenderer.Begin(viewProjection);
		m_LineRenderer.Begin(viewProjection);
		m_MeshRenderer.Begin(viewProjection, cameraPosition);

		for (auto entity : registry.view<TransformComponent>())
		{
			if (registry.any_of<LightComponent>(entity)) continue;

			auto& tc = registry.get<TransformComponent>(entity);
			glm::mat4 model = tc.Data.GetMatrix();
			bool selected = (entity == selectedEntity);

			auto* mc = registry.try_get<MeshComponent>(entity);
			auto* mat = registry.try_get<MaterialComponent>(entity);

			if (mc && mc->Data)
				m_MeshRenderer.DrawMesh(*mc->Data, model, mat ? mat->Data.get() : nullptr, light);
			else
				m_CubeRenderer.DrawCube(model, selected);

			if (selected)
				m_LineRenderer.DrawBoundingBox(model, { 1.0f, 0.0f, 0.0f, 1.0f });
		}

		m_MeshRenderer.End();
		m_LineRenderer.End();
		m_CubeRenderer.End();
	}

	void SceneRenderer::RenderScene(const Scene& scene, const EditorCamera& camera, entt::entity selectedEntity)
	{
		const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
		const glm::vec3 cameraPosition = camera.GetPosition();

		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		// Detecta luz
		const DirectionalLight* light = nullptr;
		for (auto entity : registry.view<LightComponent>())
		{
			auto& lc = registry.get<LightComponent>(entity);
			if (lc.Data) { light = lc.Data.get(); break; }
		}

		m_CubeRenderer.Begin(viewProjection);
		m_LineRenderer.Begin(viewProjection);
		m_MeshRenderer.Begin(viewProjection, cameraPosition);

		for (auto entity : registry.view<TransformComponent>())
		{
			if (registry.any_of<LightComponent>(entity)) continue;

			auto& tc = registry.get<TransformComponent>(entity);
			glm::mat4 model = tc.Data.GetMatrix();
			bool selected = (entity == selectedEntity);

			auto* mc = registry.try_get<MeshComponent>(entity);
			auto* mat = registry.try_get<MaterialComponent>(entity);

			if (mc && mc->Data)
				m_MeshRenderer.DrawMesh(*mc->Data, model, mat ? mat->Data.get() : nullptr, light);
			else
				m_CubeRenderer.DrawCube(model, selected);

			if (selected)
				m_LineRenderer.DrawBoundingBox(model, { 1.0f, 0.0f, 0.0f, 1.0f });
		}

		m_MeshRenderer.End();
		m_LineRenderer.End();
		m_CubeRenderer.End();
	}

} // namespace axe

  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  /*
* 
* Cor	Valor
Vermelho{1, 0, 0, 1}
Verde	{0, 1, 0, 1}
Azul	{0, 0, 1, 1}
Amarelo	{1, 1, 0, 1}
Branco	{1, 1, 1, 1}
Preto
*/