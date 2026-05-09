#include "scene_renderer.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/scene/components.hpp"

namespace axe
{

	SceneRenderer::SceneRenderer() {}

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

		// Itera hierarquia a partir das raízes
		auto roots = const_cast<Scene&>(scene).GetRootEntities();
		for (auto entity : roots)
			RenderEntity(scene, entity, glm::mat4(1.0f), selectedEntity, light);

		m_MeshRenderer.End();
		m_LineRenderer.End();
		m_CubeRenderer.End();
	}

	void SceneRenderer::RenderScene(const Scene& scene,
		const glm::mat4& view, const glm::mat4& projection,
		const glm::vec3& cameraPosition, entt::entity selectedEntity)
	{
		glm::mat4 viewProjection = projection * view;
		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		const DirectionalLight* light = nullptr;
		for (auto entity : registry.view<LightComponent>())
		{
			auto& lc = registry.get<LightComponent>(entity);
			if (lc.Data) { light = lc.Data.get(); break; }
		}

		m_CubeRenderer.Begin(viewProjection);
		m_LineRenderer.Begin(viewProjection);
		m_MeshRenderer.Begin(viewProjection, cameraPosition);

		auto roots = const_cast<Scene&>(scene).GetRootEntities();
		for (auto entity : roots)
			RenderEntity(scene, entity, glm::mat4(1.0f), selectedEntity, light);

		m_MeshRenderer.End();
		m_LineRenderer.End();
		m_CubeRenderer.End();
	}

	
	void SceneRenderer::RenderEntity(const Scene& scene, entt::entity entity,
		const glm::mat4& parentTransform, entt::entity selectedEntity,
		const DirectionalLight* light)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();
		if (!registry.valid(entity)) return;

		if (registry.any_of<FolderComponent>(entity))
		{
			auto* rel = registry.try_get<RelationshipComponent>(entity);
			if (rel)
				for (auto child : rel->Children)
					RenderEntity(scene, child, glm::mat4(1.0f), selectedEntity, light);
			return;
		}

		if (registry.any_of<LightComponent>(entity)) return;

		auto* tc = registry.try_get<TransformComponent>(entity);
		// Usa transform próprio — NÃO acumula com pai (hierarquia é só organização)
		glm::mat4 worldTransform = tc ? tc->Data.GetMatrix() : glm::mat4(1.0f);

		bool selected = (entity == selectedEntity);
		auto* mc = registry.try_get<MeshComponent>(entity);
		auto* mat = registry.try_get<MaterialComponent>(entity);

		if (mc && mc->Data)
			m_MeshRenderer.DrawMesh(*mc->Data, worldTransform,
				mat ? mat->Data.get() : nullptr, light);
		else
			m_CubeRenderer.DrawCube(worldTransform, selected);

		if (selected)
			m_LineRenderer.DrawBoundingBox(worldTransform, { 1.0f, 0.0f, 0.0f, 1.0f });

		auto* rel = registry.try_get<RelationshipComponent>(entity);
		if (rel)
			for (auto child : rel->Children)
				RenderEntity(scene, child, glm::mat4(1.0f), selectedEntity, light);
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