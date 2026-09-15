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

				// SC45 — o NOME de volta na mensagem.
				//
				// Esta rede de seguranca existe exatamente para o caso do
				// SocketAttachmentComponent, e nao serviu de nada: a linha com
				// o nome do tipo estava comentada, entao o console dizia
				// "algum componente sera PERDIDO" sem dizer qual. Um aviso que
				// nao identifica o culpado custa a mesma leitura e nao poupa
				// nenhuma investigacao.
				//
				// storage.type().name() e o nome mangled do compilador
				// ("struct axe::SocketAttachmentComponent" no MSVC) — feio,
				// mas suficiente para achar o tipo, que e o unico trabalho
				// desta mensagem.
				AXE_CORE_ERROR("SceneSnapshot: o componente '{}' existe na cena mas NAO esta em AllComponents (scene_snapshot.hpp)., storage.type().name()");
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
			//
			// ═══════════════════════════════════════════════════════════════
			//  GHOST_ENTITY_V1 — O GUARDA `valid()` NAO E ZELO, E O CONSERTO
			//
			//  ── O SINTOMA ─────────────────────────────────────────────────
			//
			//  Linhas "Entity" vazias na Hierarchy, aparecendo DEPOIS de
			//  apagar alguma coisa, sobrevivendo a apagar-e-salvar e voltando
			//  a cada boot. No `.axescene` elas sao entradas com
			//  `"components": null` e ids gigantes: 1048578, 1048590,
			//  1048591, 1048593.
			//
			//  ── O QUE ESSES NUMEROS SAO ───────────────────────────────────
			//
			//  1048576 e 2^20, e o entt parte o handle de 32 bits em 20 bits
			//  de INDICE e 12 de VERSAO. Entao 1048578 le-se "indice 2,
			//  versao 1" — e versao 1 quer dizer: este slot ja foi destruido
			//  uma vez. Sao os buracos deixados pelas delecoes dele.
			//
			//  ── A CAUSA ───────────────────────────────────────────────────
			//
			//  `src.storage<entt::entity>()` NAO itera apenas as entidades
			//  vivas: ele percorre o array inteiro, buracos inclusive, e
			//  devolve o handle ja com a versao incrementada. Medido:
			//  destruir 2 de 6 entidades e iterar o storage entrega 6 itens,
			//  dois deles com `valid() == false`.
			//
			//  Sem o guarda, o `dst.create(entity)` MATERIALIZA cada buraco
			//  como entidade viva de verdade no destino — e nua, porque o
			//  CopyAll logo abaixo so tem componentes para copiar das que
			//  eram reais. Cada Capture/Restore (undo, redo, Play/Stop)
			//  transformava os buracos da cena em entidades.
			//
			//  Dai em diante elas sao legitimas: o `if (!registry.valid())`
			//  do SceneSerializer as aprova, elas vao para o arquivo como
			//  `components: null`, e o loader (que cria toda entidade como
			//  `CreateEntity("Entity")` e so depois aplica o que veio no
			//  JSON) devolve exatamente a linha "Entity" vazia. O ciclo
			//  fechava sozinho — por isso apagar e salvar nao resolvia.
			//
			//  ── POR QUE O GUARDA NAO QUEBRA O QUE O COMENTARIO ACIMA PROTEGE
			//
			//  A preocupacao com os identificadores ORIGINAIS continua de pe,
			//  e continua atendida: pular um buraco nao muda o handle de
			//  ninguem — `create(hint)` poe cada entidade viva no seu indice
			//  e versao de origem, e os buracos voltam a ser buracos.
			//  Verificado lado a lado: com o guarda, as vivas saem com os
			//  MESMOS handles; sem ele, saem os mesmos MAIS os fantasmas.
			// ═══════════════════════════════════════════════════════════════
			for (auto entity : src.storage<entt::entity>())
			{
				if (!src.valid(entity)) continue;

				dst.create(entity);
			}

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