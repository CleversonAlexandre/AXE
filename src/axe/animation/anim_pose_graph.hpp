#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/anim_node.hpp"

#include <memory>
#include <vector>

namespace axe
{
	// Um link é (nó de origem) -> (nó de destino, pino N).
	//
	// O pino de SAÍDA não tem índice: todo nó produz exatamente UM valor (uma
	// pose, ou um float, ou um bool). Um nó com duas saídas seria dois nós.
	//
	// ── POR QUE UMA LISTA SÓ PRA POSE E DADO ─────────────────────────────
	//
	// A tentação é guardar o link de dado dentro do próprio pino (um campo
	// LinkedNodeId no AnimDataPin) e deixar m_Links só pras poses.
	//
	// É uma armadilha: o editor teria que DESENHAR, CRIAR, APAGAR e
	// SERIALIZAR link de duas formas diferentes. Toda operação sobre grafo
	// (apagar um nó e limpar seus links, por exemplo) viraria dois caminhos —
	// e um deles seria esquecido.
	//
	// Um link é um link. O que muda é só em qual vetor do nó de destino ele
	// aterrissa na hora do Resolve().
	enum class AnimLinkKind
	{
		Pose,   // aterrissa em AnimNode::Inputs[ToPin]
		Data    // aterrissa em AnimNode::DataInputs[ToPin].Link
	};

	struct AXE_API AnimLink
	{
		int FromNode = -1;
		int ToNode = -1;
		int ToPin = 0;

		AnimLinkKind Kind = AnimLinkKind::Pose;
	};

	// Um grafo de poses. É o conteúdo do AnimGraph inteiro E de cada estado
	// dentro da máquina de estados — mesma classe, aninhada.
	//
	// Essa recursão é o que faz o sistema inteiro caber numa cabeça: não há
	// "grafo do topo" e "grafo do estado". Há grafo.
	class AXE_API AnimPoseGraph
	{
	public:
		AnimPoseGraph() = default;

		// ── COPIÁVEL, E A CÓPIA É UM CLONE PROFUNDO ──────────────────────────
		//
		// A tentação é deletar a cópia: os nós são unique_ptr, então "copiar não
		// faz sentido". Foi o que eu fiz — e o resultado foi o axe.dll parar de
		// compilar.
		//
		// Motivo: este grafo acaba dentro do SkeletalMeshComponent, e o EnTT e o
		// SceneSnapshot PRECISAM copiar componentes (o snapshot do Play/Stop é
		// literalmente um clone do registry). Um componente move-only quebra os
		// dois.
		//
		// E "cópia = clone profundo" não é uma concessão — é a semântica CERTA:
		// duplicar uma entidade tem que dar a ela o SEU próprio grafo, com o seu
		// próprio tempo de animação. Uma cópia rasa faria as duas marionetes.
		AnimPoseGraph(const AnimPoseGraph& other) { CopyFrom(other); }

		AnimPoseGraph& operator=(const AnimPoseGraph& other)
		{
			if (this != &other)
				CopyFrom(other);

			return *this;
		}

		AnimPoseGraph(AnimPoseGraph&&) = default;
		AnimPoseGraph& operator=(AnimPoseGraph&&) = default;

		// Toma posse. Devolve o Id atribuído.
		int AddNode(std::unique_ptr<AnimNode> node);
		void RemoveNode(int id);

		AnimNode* FindNode(int id) const;

		void AddLink(int fromNode, int toNode, int toPin, AnimLinkKind kind = AnimLinkKind::Pose);
		void RemoveLinksTo(int toNode, int toPin, AnimLinkKind kind);
		void RemoveLinksOf(int nodeId);

		// Resolve os ponteiros de AnimNode::Inputs a partir dos links.
		//
		// Tem que rodar depois de QUALQUER mudança de topologia (carregar,
		// ligar, apagar). Chamar de menos deixa ponteiros pendurados; chamar de
		// mais custa quase nada. Na dúvida, chame.
		void Resolve();

		// O nó Output. -1 = grafo sem saída (renderiza bind pose).
		int  GetOutputNode() const { return m_OutputNode; }
		void SetOutputNode(int id) { m_OutputNode = id; }

		void Update(AnimEvalContext& ctx);
		void Evaluate(AnimEvalContext& ctx, Pose& out);

		void Reset();

		// ── Serialização ─────────────────────────────────────────────────────
		//
		// RECURSIVA de propósito: um nó StateMachine contém estados, e cada
		// estado contém um AnimPoseGraph. Chamar ToJson na raiz salva a árvore
		// inteira, em qualquer profundidade — sem nenhum código especial por
		// nível.
		//
		// É a mesma recursão que faz o sistema caber numa cabeça: não existe
		// "grafo do topo" e "grafo do estado". Existe grafo.
		void ToJson(nlohmann::json& j) const;
		void FromJson(const nlohmann::json& j);

		// Cópia profunda. Ver o comentário em AnimNode::Clone.
		//
		// Mantido como método nomeado (além do copy-ctor) porque `g.Clone()` diz
		// no ponto de uso que ali acontece uma cópia CARA de uma árvore inteira.
		// `AnimPoseGraph g2 = g1;` esconde isso.
		AnimPoseGraph Clone() const { return *this; }

		const std::vector<std::unique_ptr<AnimNode>>& GetNodes() const { return m_Nodes; }
		const std::vector<AnimLink>& GetLinks() const { return m_Links; }

		bool IsEmpty() const { return m_Nodes.empty(); }

		int NextId() const { return m_NextId; }
		void SetNextId(int id) { m_NextId = id; }

	private:
		// Clone profundo: nós novos com os MESMOS Ids (os links são pares de Id,
		// então continuam válidos sem remapeação) + Resolve pra religar os
		// ponteiros internos ao grafo NOVO.
		void CopyFrom(const AnimPoseGraph& other);

		std::vector<std::unique_ptr<AnimNode>> m_Nodes;
		std::vector<AnimLink>                  m_Links;

		int m_OutputNode = -1;
		int m_NextId = 1;   // 0 e valores negativos ficam livres pra "inválido"
	};

} // namespace axe
