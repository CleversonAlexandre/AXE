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

		// SKY_LIGHT_V1 — a luz de ambiente. Caminho UNICO de criacao, mesmo
		// motivo do CreatePostProcessVolume abaixo: menu da Hierarchy,
		// bootstrap do editor e a migracao do SceneSerializer chamam todos
		// daqui, entao nao ha como divergirem.
		entt::entity CreateSkyLight(const std::string& name = "Sky Light");

		// ── PPVOLUME_ONE_PATH_V1 ─────────────────────────────────────────────
		//
		// O "Post Process Volume" tinha DOIS caminhos de criacao que
		// divergiram: o menu da Hierarchy criava PostProcess + Interior Volume
		// + Probe Volume + Reflection Probe (os tres desativados) e escala
		// 10/4/10; o bootstrap do EditorLayer, quando nao ha cena inicial,
		// criava so o PostProcessComponent.
		//
		// O sintoma foi exatamente o esperado: a entidade que vem do projeto
		// aparece no Inspector SEM as secoes de Interior/Probe/Reflection, e a
		// criada na hora aparece com elas. Nao era bug de serializacao — os
		// tres sao gravados e lidos normalmente; eles nunca existiram naquela
		// entidade.
		//
		// Mesma familia das lambdas duplicadas de instanciar asset: quando a
		// mesma coisa e criada em dois lugares, um dos dois envelhece.
		entt::entity CreatePostProcessVolume(const std::string& name = "Post Process Volume");

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