#include "scene_snapshot.hpp"

#include "axe/scene/scene.hpp"
#include "axe/log/log.hpp"

#include <unordered_set>

namespace axe
{
	namespace
	{
		// Copia todos os componentes de um tipo, preservando a entidade dona.
		template <typename Component>
		void CopyOne(entt::registry& src, entt::registry& dst)
		{
			auto view = src.view<Component>();

			for (auto entity : view)
				dst.emplace_or_replace<Component>(entity, view.template get<Component>(entity));
		}

		// Expande a lista em tempo de compilação — um CopyOne por tipo.
		template <typename... Component>
		void CopyAll(ComponentGroup<Component...>, entt::registry& src, entt::registry& dst)
		{
			(CopyOne<Component>(src, dst), ...);
		}

		template <typename... Component>
		std::unordered_set<entt::id_type> BuildCoveredSet(ComponentGroup<Component...>)
		{
			return { entt::type_hash<Component>::value()... };
		}

		// ── REDE DE SEGURANÇA ────────────────────────────────────────────
		//
		// O EnTT 3.11 permite percorrer os storages do registry e perguntar o
		// TIPO de cada um. Comparando com AllComponents, descobrimos em tempo
		// de execução qualquer componente que exista na cena mas não esteja na
		// lista.
		//
		// É isto que troca "perder trabalho em silêncio" por "o console te
		// avisa na primeira vez que você aperta Play".
		void VerifyCoverage(entt::registry& src)
		{
			static const std::unordered_set<entt::id_type> covered = BuildCoveredSet(AllComponents{});

			// Só avisa uma vez por tipo, por sessão — senão vira spam a cada
			// Play e você para de ler o console (que é como o aviso morre).
			static std::unordered_set<entt::id_type> alreadyWarned;

			// Sem structured binding de proposito: o MSVC as vezes tropeca no
			// tipo do par que o EnTT devolve aqui. `curr.first` / `curr.second`
			// e mais chato de ler, mas compila em qualquer versao.
			for (auto&& curr : src.storage())
			{
				const entt::id_type id = curr.first;
				auto& storage = curr.second;

				// O storage das próprias entidades não é um componente.
				if (id == entt::type_hash<entt::entity>::value())
					continue;

				if (storage.empty())
					continue;

				if (covered.count(id) != 0)
					continue;

				if (alreadyWarned.count(id) != 0)
					continue;

				alreadyWarned.insert(id);

				//AXE_CORE_ERROR("SceneSnapshot: o componente '{}' existe na cena mas NAO esta em "
				//	"AllComponents (scene_snapshot.hpp).", storage.type().name());
				AXE_CORE_ERROR("  -> Ele sera PERDIDO ao dar Stop. Adicione o tipo na lista.");
			}
		}

		void CloneRegistry(entt::registry& src, entt::registry& dst)
		{
			// Registry NOVO, e não clear().
			//
			// clear() destrói as entidades mas mantém a free-list com as
			// versões incrementadas — e aí create(hint) pode não conseguir
			// devolver o MESMO identificador. Sem os mesmos IDs, o
			// RelationshipComponent (que guarda entt::entity dos pais e
			// filhos) aponta pra lugar nenhum e a hierarquia se desmonta.
			dst = entt::registry{};

			// Recria as entidades com os identificadores ORIGINAIS (índice +
			// versão). É o que mantém pais/filhos e a seleção do editor
			// válidos depois do restore.
			for (auto entity : src.storage<entt::entity>())
				dst.create(entity);

			CopyAll(AllComponents{}, src, dst);
		}
	}

	void SceneSnapshot::Capture(Scene& scene)
	{
		entt::registry& src = scene.GetRegistry();

		VerifyCoverage(src);

		CloneRegistry(src, m_Registry);

		m_Empty = false;
	}

	void SceneSnapshot::Restore(Scene& scene)
	{
		if (m_Empty)
			return;

		CloneRegistry(m_Registry, scene.GetRegistry());
	}

	void SceneSnapshot::Clear()
	{
		m_Registry = entt::registry{};
		m_Empty = true;
	}

} // namespace axe
