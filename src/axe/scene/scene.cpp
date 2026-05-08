#include "scene.hpp"
#include "axe/log/log.hpp"

namespace axe
{
	entt::entity Scene::CreateEntity(const std::string& name)
	{
		entt::entity entity = m_Registry.create();

		m_Registry.emplace<NameComponent>(entity, name);
		m_Registry.emplace<TransformComponent>(entity);

		return entity;
	}

	entt::entity Scene::CreateLight(const std::string& name)
	{
		entt::entity entity = CreateEntity(name);

		auto& light = m_Registry.emplace<LightComponent>(entity);
		light.Data = std::make_shared<DirectionalLight>();

		auto& transform = m_Registry.get<TransformComponent>(entity);
		transform.Data.Position = { 0.0f, 5.0f, 0.0f };
		transform.Data.Rotation = { glm::radians(-70.0f), glm::radians(-20.0f), 0.0f };

		return entity;
	}


	void Scene::DestroyEntity(entt::entity entity)
	{
		if (m_Registry.valid(entity))
			m_Registry.destroy(entity);
	}

	entt::entity Scene::DuplicateEntity(entt::entity entity)
	{
		if (!m_Registry.valid(entity))
			return entt::null;

		entt::entity copy = m_Registry.create();

		// Copia NameComponent
		if (auto* c = m_Registry.try_get<NameComponent>(entity))
			m_Registry.emplace<NameComponent>(copy, c->Name + " (Copy)");

		// Copia TransformComponent
		if (auto* c = m_Registry.try_get<TransformComponent>(entity))
			m_Registry.emplace<TransformComponent>(copy, *c);

		// Copia MeshComponent — mesh é shared, não duplica o asset
		if (auto* c = m_Registry.try_get<MeshComponent>(entity))
			m_Registry.emplace<MeshComponent>(copy, *c);

		// Copia MaterialComponent — material é shared, não duplica
		if (auto* c = m_Registry.try_get<MaterialComponent>(entity))
			m_Registry.emplace<MaterialComponent>(copy, *c);

		// Luz não é duplicada — faz sentido ter só uma por vez por enquanto

		return copy;
	}
}//namespace axe