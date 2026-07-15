#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/anim_node.hpp"
#include "axe/animation/anim_pose_graph.hpp"
#include "axe/animation/anim_parameters.hpp"
#include "axe/animation/pose.hpp"

#include <memory>

namespace axe
{
	class AnimGraphAsset;

	// O runtime do AnimGraph, por personagem.
	//
	// ── O QUE ELE DEIXOU DE SER ──────────────────────────────────────────
	//
	// Na v1 esta classe CONTINHA a máquina de estados: o estado atual, o
	// anterior, o crossfade, a escolha de transição. Tudo aqui.
	//
	// Agora nada disso mora aqui — mora dentro do AnimNode_StateMachine, que é
	// só mais um nó do grafo. Esta classe virou o que devia ser desde o começo:
	// o dono do BLACKBOARD e do POOL, e quem chama Update/Evaluate na raiz.
	//
	// A consequência prática: um personagem pode ter DUAS máquinas de estados
	// (locomoção e braços) em ramos diferentes do mesmo grafo, blendadas por um
	// Layered Blend. Na v1 isso era impossível de expressar.
	class AXE_API AnimGraphInstance
	{
	public:
		// O asset é a fonte do grafo. Guardamos o shared_ptr porque o grafo
		// vive dentro dele — e porque o editor pode reabrir e re-resolver o
		// mesmo asset enquanto o jogo roda.
		void SetAsset(const std::shared_ptr<AnimGraphAsset>& asset);

		bool HasGraph() const { return m_Asset != nullptr; }

		// O blackboard. O gameplay escreve; os nós leem.
		AnimParameters Params;

		// Fase 1: avança tempo, decide transições, calcula pesos.
		// Fase 2: produz a pose.
		//
		// Separadas na interface também, e não só internamente, porque o
		// AnimationWorld precisa poder dar Update em todos os personagens antes
		// de avaliar qualquer um — é o que permitirá, depois, avaliar em
		// paralelo.
		void Update(const Skeleton& skeleton, float deltaTime, bool advanceTime = true);
		void Evaluate(const Skeleton& skeleton, Pose& out);

		void Reset();

		// Nome do estado atual da PRIMEIRA máquina de estados encontrada.
		//
		// "Primeira" porque agora pode haver várias — e a pergunta "em que
		// estado o personagem está?" deixou de ter uma resposta única. Serve pra
		// debug e pro nó Get Anim State; quem quiser precisão vai ter que
		// nomear qual máquina.
		std::string GetCurrentStateName() const;

	private:
		std::shared_ptr<AnimGraphAsset> m_Asset;

		// A CÓPIA do grafo do asset. Não um ponteiro pra ele.
		//
		// O grafo do asset é um MOLDE; os nós guardam estado de runtime (tempo
		// do clipe, estado atual da máquina, progresso do crossfade). Dois
		// personagens apontando pro mesmo grafo andariam em sincronia perfeita,
		// e um entrando em "Attack" mudaria o estado do outro.
		AnimPoseGraph m_Graph;

		PosePool m_Pool;
	};

} // namespace axe
