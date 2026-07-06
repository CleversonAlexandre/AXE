#pragma once
#include "axe/core/types.hpp"

#include "scene_objects.hpp"
#include "axe/scene/components.hpp"
#include <entt/entt.hpp>
#include <string>
#include <cstdint>


namespace axe
{
	class AXE_API Scene
	{
	public:

		//Cria uma entity ciom NameComponent e TransformComponent
		entt::entity CreateEntity(const std::string& name = "Entity");

		//Cria uma entity com LightComponent
		entt::entity CreateLight(const std::string& name = "Directional Light");

		//Destroi uma entity
		void DestroyEntity(entt::entity entity);

		// Busca entity pelo nome (primeiro match) — retorna entt::null se nao encontrar
		entt::entity FindByName(const std::string& name) const;

		//Duplica uma entity
		entt::entity DuplicateEntity(entt::entity entity);

		//Acesso ao registy - SceneRenderer, HierarchyWindow, etc. usam diretamente
		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }

		//cria uma pasta de organização
		entt::entity CreateFolder(const std::string& name = "Folder");

		//Define pai/filho
		void SetParent(entt::entity child, entt::entity parent,
			bool adjustTransform = true); // false durante load de cena

		//Remove de um pai
		void RemoveParent(entt::entity child);

		//Calcula tranform acumulado (pai * filho *...)
		glm::mat4 GetWorldTransform(entt::entity entity) const;

		//Retorna entities raiz (sem pai)
		std::vector<entt::entity> GetRootEntities() const;

	private:
		entt::registry m_Registry;
	private:
		std::vector<SceneObject> m_Objects;
		std::uint32_t m_NextObjectID = 1;
	};
}