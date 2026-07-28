#pragma once
#include "axe/animation/rig/rig_graph.hpp"

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════════
	//  NOS DE RIG — CONTROLRIG_V1
	//
	//  O conjunto minimo que ja resolve um rig de verdade: um evento, uma
	//  ordem, ler/escrever transform, IK de duas juntas, consulta ao chao e a
	//  matematica pra costurar tudo.
	// ═════════════════════════════════════════════════════════════════════════

	// Espaco em que um transform e lido ou escrito.
	//
	//  Global — relativo ao personagem (o "component space" da animacao). E o
	//           espaco em que se fala de POSICAO no mundo do rig: "ponha o pe
	//           AQUI" so faz sentido em global.
	//  Local  — relativo ao pai. E o espaco em que se fala de ROTACAO propria:
	//           "dobre o joelho 30 graus" e local.
	enum class RigSpace
	{
		Global,
		Local
	};

	// ── Evento: inicio do solve ──────────────────────────────────────────────
	//
	// Nao tem execucao de ENTRADA: e o comeco da corrente. E o "Forwards Solve"
	// da Unreal — a direcao que usa controles pra dirigir o esqueleto.
	class AXE_API RigNode_ForwardsSolve : public RigNode
	{
	public:
		RigNode_ForwardsSolve() { Title = "Forwards Solve"; ExecOut.push_back(""); }

		const char* TypeName() const override { return "ForwardsSolve"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_ForwardsSolve>(*this);
		}
	};

	// ── Sequence: roda varias correntes, em ordem ────────────────────────────
	class AXE_API RigNode_Sequence : public RigNode
	{
	public:
		RigNode_Sequence()
		{
			Title = "Sequence";
			HasExecIn = true;
			ExecOut = { "Then 0", "Then 1" };
		}

		const char* TypeName() const override { return "Sequence"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_Sequence>(*this);
		}

		// Roda TODAS as saidas, de cima pra baixo, aqui dentro.
		bool HandlesOwnFlow() const override { return true; }

		void Execute(RigExecContext& ctx) override;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;
	};

	// ── Branch ───────────────────────────────────────────────────────────────
	//
	// Segue UMA das duas saidas conforme a condicao. E o que permite ter IK e
	// FK no mesmo rig e escolher qual manda — sem isso, os dois escreveriam no
	// mesmo osso e o ultimo venceria.
	class AXE_API RigNode_Branch : public RigNode
	{
	public:
		RigNode_Branch()
		{
			Title = "Branch";
			HasExecIn = true;
			ExecOut = { "True", "False" };

			AddInBool("Condition", true);
		}

		const char* TypeName() const override { return "Branch"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_Branch>(*this);
		}

		// Escolhe a corrente aqui dentro: o percurso externo seguiria SEMPRE a
		// saida 0, que e justamente o que um branch nao pode fazer.
		bool HandlesOwnFlow() const override { return true; }

		void Execute(RigExecContext& ctx) override;
	};

	// ── For Each ─────────────────────────────────────────────────────────────
	//
	// Roda a corrente do corpo uma vez por item da lista, expondo o elemento e
	// o indice da volta atual.
	//
	// Junto com o Item Array, e o que evita repetir o mesmo trecho de grafo
	// tres vezes so porque o braco tem tres juntas.
	class AXE_API RigNode_ForEach : public RigNode
	{
	public:
		RigNode_ForEach()
		{
			Title = "For Each";
			HasExecIn = true;
			ExecOut = { "Loop Body", "Completed" };

			AddIn("Array", RigPinType::ItemArray);

			AddOut("Element", RigPinType::Item);
			AddOut("Index", RigPinType::Float);
			AddOut("Count", RigPinType::Float);
		}

		const char* TypeName() const override { return "ForEach"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_ForEach>(*this);
		}

		bool HandlesOwnFlow() const override { return true; }

		void Execute(RigExecContext& ctx) override;

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override
		{
			(void)ctx;

			out = RigPinValue{};

			switch (pin)
			{
			case 0:
				out.ItemName = m_Element.Name;
				out.ItemType = m_Element.Type;
				break;

			case 1: out.Float = (float)m_Index; break;
			case 2: out.Float = (float)m_Count; break;
			default: break;
			}
		}

	private:
		// Estado da volta ATUAL. So tem sentido enquanto o corpo do laco roda —
		// fora dele o Element fica no ultimo item, o que e inofensivo.
		RigItemRef m_Element;
		int        m_Index = 0;
		int        m_Count = 0;
	};

	// ── At (PURO) ────────────────────────────────────────────────────────────
	//
	// Devolve o elemento de uma lista numa posicao. Sozinho parece pouco; o
	// valor esta em percorrer DUAS listas em paralelo.
	//
	// O For Each anda por uma delas e entrega o Index; ligando esse mesmo Index
	// num At sobre a OUTRA lista, voce tem o par (controle, osso) da volta
	// atual — que e exatamente o que o Project to New Parent precisa.
	class AXE_API RigNode_At : public RigNode
	{
	public:
		RigNode_At()
		{
			Title = "At";

			AddIn("Array", RigPinType::ItemArray);
			AddInFloat("Index", 0.0f);

			AddOut("Element", RigPinType::Item);
		}

		const char* TypeName() const override { return "At"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_At>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

	private:
		bool m_WarnedRange = false;
	};

	// ── Project to New Parent (PURO) ─────────────────────────────────────────
	//
	// Pega onde um elemento esta EM RELACAO a um pai, e devolve onde ele
	// estaria se essa mesma relacao valesse para OUTRO pai.
	//
	// E a peca do casamento IK <-> FK: quando o IK move a mao, os controles de
	// FK precisam ir junto, senao trocar de modo faz o braco saltar. Voce
	// projeta o controle de FK usando o OSSO como novo pai — ele passa a
	// acompanhar o resultado do IK.
	class AXE_API RigNode_ProjectToNewParent : public RigNode
	{
	public:
		RigNode_ProjectToNewParent()
		{
			Title = "Project to New Parent";

			AddInItem("Child", RigElementType::Control);
			AddInBool("Child Initial", true);

			AddInItem("Old Parent", RigElementType::Bone);
			AddInBool("Old Parent Initial", true);

			AddInItem("New Parent", RigElementType::Bone);
			AddInBool("New Parent Initial", false);

			AddOut("Transform", RigPinType::Transform);
		}

		const char* TypeName() const override { return "ProjectToNewParent"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_ProjectToNewParent>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;
	};

	// ── Get Transform (PURO) ─────────────────────────────────────────────────
	//
	// Puro porque ler nao muda nada: nao precisa de lugar na ordem, so responde
	// quando alguem puxa o valor.
	class AXE_API RigNode_GetTransform : public RigNode
	{
	public:
		RigNode_GetTransform();

		const char* TypeName() const override { return "GetTransform"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_GetTransform>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		RigSpace Space = RigSpace::Global;

		// Ler o transform INICIAL em vez do atual. E como se pergunta "onde
		// isto estava em repouso?" — a referencia pra medir deslocamento.
		bool Initial = false;
	};

	// ── Set Transform ────────────────────────────────────────────────────────
	//
	// NAO pode ser puro: a hora em que escreve importa. Escrever no quadril
	// antes ou depois de resolver a perna da resultados diferentes.
	class AXE_API RigNode_SetTransform : public RigNode
	{
	public:
		RigNode_SetTransform();

		const char* TypeName() const override { return "SetTransform"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_SetTransform>(*this);
		}

		void Execute(RigExecContext& ctx) override;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		RigSpace Space = RigSpace::Global;

		// Levar os filhos junto. Ver RigHierarchy::SetGlobal.
		bool PropagateToChildren = true;
	};

	// ── Two Bone IK ──────────────────────────────────────────────────────────
	//
	// Coxa -> canela -> pe (ou ombro -> cotovelo -> mao). Dobra a junta do meio
	// pra ponta alcancar o alvo, pela lei dos cossenos.
	class AXE_API RigNode_TwoBoneIK : public RigNode
	{
	public:
		RigNode_TwoBoneIK();

		const char* TypeName() const override { return "TwoBoneIK"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_TwoBoneIK>(*this);
		}

		void Execute(RigExecContext& ctx) override;

	private:
		// Ja avisei que o Target esta zerado? Uma vez por no, senao viraria
		// spam de 60 linhas por segundo no console.
		bool m_WarnedZeroTarget = false;
	};

	// ── Ground Trace (PURO) ──────────────────────────────────────────────────
	//
	// Raycast pra baixo no mundo. O unico no que sai do espaco do personagem,
	// entao e ele quem cuida da conversao de escala — a licao do FOOTIK_V5:
	// parametro em metros nunca encosta em distancia de espaco de componente
	// sem conversao explicita.
	class AXE_API RigNode_GroundTrace : public RigNode
	{
	public:
		RigNode_GroundTrace();

		const char* TypeName() const override { return "GroundTrace"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_GroundTrace>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;
	};

	// ── Make / Break Transform (PUROS) ───────────────────────────────────────
	class AXE_API RigNode_MakeTransform : public RigNode
	{
	public:
		RigNode_MakeTransform();

		const char* TypeName() const override { return "MakeTransform"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_MakeTransform>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;
	};

	class AXE_API RigNode_BreakTransform : public RigNode
	{
	public:
		RigNode_BreakTransform();

		const char* TypeName() const override { return "BreakTransform"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_BreakTransform>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;
	};

	// ── Item Array (PURO) ────────────────────────────────────────────────────
	//
	// Uma LISTA de elementos, editada no painel Detalhes. Nao faz nada sozinha:
	// serve de entrada pros nos que operam sobre uma cadeia inteira.
	//
	// Existe pra evitar o grafo explodir. Rigar um braco em FK com nos avulsos
	// custa TRES Get Transform, TRES Set Transform e seis fios; com esta lista
	// e o FK Chain, custa dois nos.
	class AXE_API RigNode_ItemArray : public RigNode
	{
	public:
		RigNode_ItemArray()
		{
			Title = "Item Array";
			AddOut("Items", RigPinType::ItemArray);
		}

		const char* TypeName() const override { return "ItemArray"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_ItemArray>(*this);
		}

		std::vector<RigItemRef> Items;

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override
		{
			(void)ctx;
			(void)pin;

			out = RigPinValue{};
			out.Items = Items;
		}

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;
	};

	// ── FK Chain ─────────────────────────────────────────────────────────────
	//
	// Aplica FK numa cadeia inteira de uma vez: para cada par, o OSSO copia o
	// transform LOCAL do CONTROLE correspondente.
	//
	// A ordem das duas listas TEM que casar — o primeiro osso e dirigido pelo
	// primeiro controle, e assim por diante.
	//
	// Nao ha problema de ordem de execucao aqui: escrever um transform LOCAL
	// nao depende de onde o pai esta, entao os pares podem ser aplicados em
	// qualquer sequencia.
	class AXE_API RigNode_FKChain : public RigNode
	{
	public:
		RigNode_FKChain()
		{
			Title = "FK Chain";
			HasExecIn = true;
			ExecOut.push_back("");

			AddIn("Bones", RigPinType::ItemArray);
			AddIn("Controls", RigPinType::ItemArray);
			AddInFloat("Weight", 1.0f);
		}

		const char* TypeName() const override { return "FKChain"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_FKChain>(*this);
		}

		void Execute(RigExecContext& ctx) override;

	private:
		bool m_WarnedSize = false;
	};

	// ── Reroute ──────────────────────────────────────────────────────────────
	//
	// Um desvio: entra um valor (ou uma corrente), sai o mesmo. Nao faz nada
	// com ele. Serve pra ORGANIZAR — um fio que atravessa o grafo inteiro por
	// cima de cinco nos fica ilegivel; com dois reroutes ele contorna.
	//
	// ── UM NO SO PARA OS DOIS TIPOS DE FIO ───────────────────────────────
	//
	// No grafo, execucao e dados sao estruturalmente diferentes: um usa
	// HasExecIn/ExecOut, o outro usa Inputs/Outputs. Em vez de obrigar voce a
	// escolher na paleta qual reroute quer, o no nasce INDECISO mostrando os
	// dois tipos de pino — e no instante em que voce liga o primeiro fio ele
	// se decide e o outro par some.
	//
	// Isso e seguro porque a decisao acontece quando ele ainda nao tem NENHUM
	// fio: nao ha ligacao pra invalidar ao trocar os pinos.
	class AXE_API RigNode_Reroute : public RigNode
	{
	public:
		enum class Mode
		{
			Undecided,
			Data,
			Exec
		};

		RigNode_Reroute() { Title = "Reroute"; SetMode(Mode::Undecided); }

		const char* TypeName() const override { return "Reroute"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_Reroute>(*this);
		}

		Mode CurrentMode = Mode::Undecided;

		// Reconstroi os pinos conforme o modo. Preserva o tipo de dado ja
		// adotado, pra alternar nao perder a cor/validacao.
		void SetMode(Mode m)
		{
			const RigPinType keep = Inputs.empty() ? RigPinType::Wildcard : Inputs[0].Type;

			CurrentMode = m;

			Inputs.clear();
			Outputs.clear();
			ExecOut.clear();
			HasExecIn = false;

			if (m != Mode::Exec)
			{
				AddIn("", keep);
				AddOut("", keep);
			}

			if (m != Mode::Data)
			{
				HasExecIn = true;
				ExecOut.push_back("");
			}
		}

		// Chegou um fio de DADOS: o no vira reroute de dados com esse tipo.
		void AdoptType(RigPinType t) override
		{
			if (t == RigPinType::Wildcard)
				return;

			SetMode(Mode::Data);

			Inputs[0].Type = t;
			Outputs[0].Type = t;
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override
		{
			(void)pin;
			out = Read(ctx, 0);
		}

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;
	};

	// ── Comment ──────────────────────────────────────────────────────────────
	//
	// Nao executa nada e nao tem pino: e uma CAIXA com titulo, desenhada atras
	// dos outros nos, pra agrupar visualmente "isto aqui e a perna esquerda".
	//
	// Num rig de verdade o grafo passa de vinte nos rapido, e sem comentario
	// voce reconstroi o raciocinio na leitura toda vez.
	class AXE_API RigNode_Comment : public RigNode
	{
	public:
		RigNode_Comment() { Title = "Comment"; }

		const char* TypeName() const override { return "Comment"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_Comment>(*this);
		}

		// Tamanho da caixa, em unidades de canvas. E redimensionavel pelo
		// canto, como na Unreal.
		float SizeX = 320.0f;
		float SizeY = 180.0f;

		glm::vec3 Color{ 0.85f, 0.75f, 0.35f };

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;
	};

	// ── Matematica de vetor (PURO) ───────────────────────────────────────────
	class AXE_API RigNode_VectorOp : public RigNode
	{
	public:
		enum class Op { Add, Subtract, Scale, Lerp };

		RigNode_VectorOp();

		const char* TypeName() const override { return "VectorOp"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_VectorOp>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		Op Operation = Op::Add;
	};

} // namespace axe