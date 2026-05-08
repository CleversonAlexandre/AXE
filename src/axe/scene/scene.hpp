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

		//Duplica uma entity
		entt::entity DuplicateEntity(entt::entity entity);

		//Acesso ao registy - SceneRenderer, HierarchyWindow, etc. usam diretamente
		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }

	private:
		entt::registry m_Registry;
	private:
		std::vector<SceneObject> m_Objects;
		std::uint32_t m_NextObjectID = 1;
	};
}