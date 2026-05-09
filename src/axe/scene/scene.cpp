#include "scene.hpp"
#include "axe/log/log.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

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

	entt::entity Scene::CreateFolder(const std::string& name)
	{
		entt::entity entity = m_Registry.create();
		m_Registry.emplace<NameComponent>(entity, name);
		m_Registry.emplace<FolderComponent>(entity);

		return entity;
	}

	void Scene::SetParent(entt::entity child, entt::entity parent)
	{
		if (!m_Registry.valid(child)) return;

		RemoveParent(child);

		// Não converte transform — filho mantém posição absoluta
		auto& childRel = m_Registry.get_or_emplace<RelationshipComponent>(child);
		childRel.Parent = parent;

		if (m_Registry.valid(parent))
		{
			auto& parentRel = m_Registry.get_or_emplace<RelationshipComponent>(parent);
			parentRel.Children.push_back(child);
		}
	}

	void Scene::RemoveParent(entt::entity child)
	{
		if (!m_Registry.valid(child)) return;

		auto* rel = m_Registry.try_get<RelationshipComponent>(child);
		if (!rel || rel->Parent == entt::null) return;

		//Remove do vetor de filhos 
		auto* parentRel = m_Registry.try_get<RelationshipComponent>(rel->Parent);
		if (parentRel)
		{
			auto& children = parentRel->Children;
			children.erase(
				std::remove(children.begin(), children.end(), child),
				children.end()
			);
		}

		rel->Parent = entt::null;
	}

	glm::mat4 Scene::GetWorldTransform(entt::entity entity) const
	{
		if (!m_Registry.valid(entity))
			return glm::mat4(1.0f);

		// Pasta não tem transform
		if (m_Registry.any_of<FolderComponent>(entity))
			return glm::mat4(1.0f);

		auto* tc = m_Registry.try_get<TransformComponent>(entity);
		glm::mat4 local = tc ? tc->Data.GetMatrix() : glm::mat4(1.0f);

		// Acumula transform do pai
		auto* rel = m_Registry.try_get<RelationshipComponent>(entity);
		if (rel && rel->Parent != entt::null && m_Registry.valid(rel->Parent))
			return GetWorldTransform(rel->Parent) * local;

		return local;
	}

	std::vector<entt::entity> Scene::GetRootEntities() const
	{
		std::vector<entt::entity> roots;

		auto view = m_Registry.view<NameComponent>();
		for (auto entity : view)
		{
			auto* rel = m_Registry.try_get<RelationshipComponent>(entity);
			if (!rel || rel->Parent == entt::null)
				roots.push_back(entity);
		}

		return roots;
	}
}//namespace axe