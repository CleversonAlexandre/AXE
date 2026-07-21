#include "anim_graph_instance.hpp"
#include "anim_graph_asset.hpp"
#include "anim_nodes.hpp"
#include "axe/log/log.hpp"

namespace axe
{
	void AnimGraphInstance::SetAsset(const std::shared_ptr<AnimGraphAsset>& asset)
	{
		m_Asset = asset;

		if (!asset)
		{
			m_Graph = AnimPoseGraph{};
			return;
		}

		// CLONA. Ver o comentário do m_Graph no header — este clone é a
		// diferença entre dois personagens animando independentes e dois
		// personagens andando em sincronia como marionetes.
		m_Graph = asset->GetRoot().Clone();

		// O blackboard nasce com os defaults declarados no asset.
		//
		// Importa mais do que parece: se "Speed" começa em 0 mas o personagem
		// spawna correndo, o grafo passa um frame no estado errado — e você vê
		// um piscar de idle.
		asset->SeedParameters(Params);

		m_AssetVersion = asset->GetVersion();

		m_Graph.Reset();
	}

	void AnimGraphInstance::Update(const Skeleton& skeleton, float deltaTime, bool advanceTime)
	{
		if (!m_Asset)
			return;

		// O asset foi salvo com edicoes novas? Re-clona ANTES de avaliar.
		// E isto que faz o personagem da CENA acompanhar o editor: sem o
		// re-clone, ele tocaria pra sempre o grafo de quando o componente
		// foi criado. Custo: um clone por save — nada por frame.
		//
		// Nota: SetAsset re-semeia os parametros com os defaults. Gameplay
		// que escreve por frame nem percebe; um valor setado UMA vez ha
		// minutos volta ao default — comportamento igual ao da Unreal ao
		// recompilar um Anim Blueprint.
		if (m_AssetVersion != m_Asset->GetVersion())
			SetAsset(m_Asset);

		m_FiredNotifies.clear();

		AnimEvalContext ctx;
		ctx.NotifySink = &m_FiredNotifies;
		ctx.Skel = &skeleton;
		ctx.Params = &Params;
		ctx.DeltaTime = deltaTime;
		ctx.AdvanceTime = advanceTime;
		ctx.Pool = &m_Pool;

		m_Graph.Update(ctx);

		// NÃO há "limpar triggers" aqui — e é de propósito.
		//
		// O trigger é consumido pela TRANSIÇÃO QUE O USOU (ConsumeTrigger), e
		// só quando ela vence. Um clear global aqui anularia esse desenho: um
		// trigger disparado pelo gameplay entre dois Updates morreria antes de
		// qualquer transição enxergá-lo, e o ataque simplesmente não sairia —
		// de forma intermitente, dependendo do frame. O pior tipo de bug.
	}

	void AnimGraphInstance::Evaluate(const Skeleton& skeleton, Pose& out)
	{
		if (!m_Asset)
		{
			Pose::FromBindPose(skeleton, out);
			return;
		}

		AnimEvalContext ctx;
		ctx.Skel = &skeleton;
		ctx.Params = &Params;
		ctx.DeltaTime = 0.0f;      // Evaluate NUNCA avança tempo. Quem avança é o Update.
		ctx.AdvanceTime = false;
		ctx.Pool = &m_Pool;

		m_Graph.Evaluate(ctx, out);

		// Devolve os buffers emprestados pelos nós. Uma vez por frame, no fim —
		// os nós pegam poses emprestadas em pilha (a avaliação é uma árvore), e
		// liberar tudo de uma vez é mais simples e mais rápido que rastrear cada
		// empréstimo.
		m_Pool.ReleaseAll();
	}

	void AnimGraphInstance::Reset()
	{
		m_Graph.Reset();
	}

	std::string AnimGraphInstance::GetCurrentStateName() const
	{
		// A PRIMEIRA máquina de estados do grafo raiz.
		//
		// Só do raiz, e não recursivo: uma máquina aninhada dentro de um estado
		// responderia "qual estado?" com o estado de um sub-nível, o que é mais
		// confuso do que útil.
		for (const auto& n : m_Graph.GetNodes())
		{
			auto* sm = dynamic_cast<AnimNode_StateMachine*>(n.get());

			if (!sm)
				continue;

			const int cur = sm->GetCurrentState();

			if (cur >= 0 && cur < (int)sm->States.size())
				return sm->States[cur].Name;

			return {};
		}

		return {};
	}

} // namespace axe