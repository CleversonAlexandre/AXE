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

	// ═════════════════════════════════════════════════════════════════════════
	//  FRONTEIRA DE FUNCAO — Entry e Return
	//
	//  Todo grafo de funcao tem os dois, e so eles: o Entry entrega os valores
	//  que ENTRARAM na chamada, o Return recolhe os que SAEM. Fora, cada uso e
	//  um no Call Function (ver mais abaixo).
	//
	//  ── COMO O DADO ATRAVESSA A FRONTEIRA ────────────────────────────────
	//
	//  O Entry nao tem dado proprio. Quando alguem la dentro le uma saida dele,
	//  ele pergunta ao grafo de FORA qual o valor daquele pino do no Call —
	//  pelo RigExecContext::Caller. Ou seja: o fio que voce ligou por fora e
	//  seguido normalmente, so que a leitura parte de dentro.
	//
	//  Na volta, o Call le o pino correspondente do Return, ja no grafo de
	//  dentro. Nenhum valor e COPIADO na fronteira; os dois lados usam o mesmo
	//  mecanismo de pull que o grafo plano sempre usou.
	//
	//  ── POR QUE O ENTRY E UM EVENTO ──────────────────────────────────────
	//
	//  Porque o motor ja sabe rodar uma corrente a partir de um evento nomeado:
	//  Execute(ctx, "Entry") reaproveita exatamente o mesmo caminho do Forwards
	//  Solve, com reset de cache e guarda de ciclo inclusos. Um mecanismo novo
	//  de entrada seria um segundo caminho pra manter em sincronia.
	// ═════════════════════════════════════════════════════════════════════════

	// ── Entry ────────────────────────────────────────────────────────────────
	//
	// As ENTRADAS do subgrafo, vistas de dentro. As saidas dele espelham as
	// entradas do Collapsed que o contem.
	class AXE_API RigNode_Entry : public RigNode
	{
	public:
		RigNode_Entry() { Title = "Entry"; ExecOut.push_back(""); }

		const char* TypeName() const override { return "Entry"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_Entry>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;
	};

	// ── Return ───────────────────────────────────────────────────────────────
	//
	// As SAIDAS do subgrafo. Nao executa nada: e um ponto de coleta, e o
	// Collapsed puxa dele quando alguem de fora le uma saida.
	//
	// Tem entrada de execucao e nenhuma saida — e o fim da corrente de dentro.
	class AXE_API RigNode_Return : public RigNode
	{
	public:
		RigNode_Return() { Title = "Return"; HasExecIn = true; }

		const char* TypeName() const override { return "Return"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_Return>(*this);
		}

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;
	};

	// ── Call Function ────────────────────────────────────────────────────────
	//
	// CHAMA uma funcao do rig. Os pinos espelham os Inputs e Outputs declarados
	// na definicao — quando ela muda, todo Call e reconstruido.
	//
	// A definicao e alcancada POR NOME, via RigExecContext::ResolveFunction. Um
	// ponteiro pra funcao ficaria pendurado no nada assim que o vector de
	// funcoes realocasse (adicionar uma funcao faz isso), e o sintoma seria
	// corrupcao aleatoria em vez de um no-op honesto.
	class AXE_API RigNode_CallFunction : public RigNode
	{
	public:
		RigNode_CallFunction();

		const char* TypeName() const override { return "CallFunction"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			auto c = std::make_unique<RigNode_CallFunction>(*this);
			c->m_LastSolve = 0;
			return c;
		}

		void Execute(RigExecContext& ctx) override;
		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		// Nome da funcao chamada. O editor troca isto pelo combo do Details, e
		// o RenameFunction do asset o atualiza em massa.
		std::string FunctionName;

	private:
		RigGraph* Resolve(RigExecContext& ctx);
		bool EnsureInner(RigExecContext& ctx, RigGraph& fn);

		std::uint64_t m_LastSolve = 0;

		bool m_WarnedMissing = false;
		bool m_WarnedDepth = false;
	};

	// ── Evento: Backward Solve ───────────────────────────────────────────────
	//
	// O caminho INVERSO: le os ossos ANIMADOS e poe os controles em cima deles.
	//
	// ── POR QUE UM EVENTO SEPARADO, E NAO UM NO ──────────────────────────
	//
	// Porque a diferenca nao e o que se faz, e QUANDO. O Forward Solve roda
	// todo frame; este roda SOB DEMANDA — quando voce carrega uma animacao pra
	// editar, ou aperta o botao no editor.
	//
	// Antes disto a engine tinha so o Forward, e a tentativa de resolver
	// "controle precisa saber onde a animacao pos o osso" virou um no que fazia
	// este trabalho DENTRO do Forward, todo frame. Funcionava e confundia: o
	// controle deixava de ser entrada e virava entrada-e-saida ao mesmo tempo,
	// e ninguem mais sabia quem mandava em quem.
	//
	// Com os dois eventos, o papel fica limpo:
	//
	//   Forward  — controle -> osso. O controle e ENTRADA, e fica parado
	//              esperando o gizmo. Isso e o certo, nao um defeito.
	//   Backward — osso -> controle. Roda uma vez, encosta os controles na
	//              pose, e sai da frente.
	//
	// Monte a corrente com os nos que ja existem: Get Transform no osso ->
	// Set Control Pose no controle.
	class AXE_API RigNode_BackwardsSolve : public RigNode
	{
	public:
		RigNode_BackwardsSolve() { Title = "Backward Solve"; ExecOut.push_back(""); }

		const char* TypeName() const override { return "BackwardsSolve"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_BackwardsSolve>(*this);
		}
	};

	// ── Set Control Pose ─────────────────────────────────────────────────────
	//
	// Poe um controle numa posicao e faz aquilo GRUDAR.
	//
	// O Set Transform comum escreve no Current, e o Current e apagado pelo
	// ResetToInitial no comeco de cada solve — o que e correto pro Forward
	// (todo frame parte do repouso) e inutil pro Backward: voce encostaria o
	// controle na pose e ele voltaria sozinho no frame seguinte.
	//
	// Este escreve no Value, que o ResetToInitial COMPOE em vez de apagar. E o
	// mesmo campo que um Sequencer vai keyar — ou seja, o Backward Solve e o
	// Sequencer escrevem no mesmo lugar, por construcao.
	class AXE_API RigNode_SetControlPose : public RigNode
	{
	public:
		RigNode_SetControlPose();

		const char* TypeName() const override { return "SetControlPose"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_SetControlPose>(*this);
		}

		void Execute(RigExecContext& ctx) override;

	private:
		bool m_WarnedBone = false;
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

	// ── Get Control Value (PURO) ─────────────────────────────────────────────
	//
	// Le o valor de um controle de CANAL. E a ponte entre "o animador mexeu no
	// interruptor" e o grafo reagir — sem ela, um Branch so poderia ser ligado
	// por um valor digitado no proprio no, que ninguem consegue animar depois.
	class AXE_API RigNode_GetControlValue : public RigNode
	{
	public:
		RigNode_GetControlValue()
		{
			Title = "Get Control Value";

			AddInItem("Control", RigElementType::Control);

			AddOut("Bool", RigPinType::Bool);
			AddOut("Float", RigPinType::Float);
		}

		const char* TypeName() const override { return "GetControlValue"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_GetControlValue>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override
		{
			out = RigPinValue{};

			if (!ctx.Hierarchy)
				return;

			const int idx = ReadItem(ctx, 0);

			if (idx < 0)
				return;

			const RigElement& e = (*ctx.Hierarchy)[idx];

			// As duas saidas sempre respondem, convertendo entre si: assim
			// ligar um canal Float num Branch, ou um Bool num peso, faz o que
			// se espera em vez de devolver zero calado.
			if (pin == 0)
				out.Bool = (e.ValueType == RigControlValue::Float)
				? (e.FloatValue > 0.5f)
				: e.BoolValue;
			else
				out.Float = (e.ValueType == RigControlValue::Bool)
				? (e.BoolValue ? 1.0f : 0.0f)
				: e.FloatValue;
		}
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

		// ── DIAGNOSTICO DE ALVO FORA DE ALCANCE ──────────────────────────────
		//
		// O clamp de alcance e correto (passar do comprimento total faria o
		// acos estourar), mas o efeito colateral e MUDO: o membro fica RETO e o
		// efetor nao chega no alvo, sem nenhuma pista.
		//
		// O contador exige PERSISTENCIA antes de falar. Fora de alcance por um
		// instante e rotina — o pe em transicao, um Damp ainda esquentando — e
		// avisar ali deixaria no console uma mensagem permanente sobre um
		// estado que ja passou. Foi exatamente esse o defeito do aviso de
		// Target zerado, e nao vamos repeti-lo aqui.
		bool m_WarnedOutOfReach = false;
		int  m_OutOfReach = 0;

		// Cadeia degenerada (osso repetido ou elo de comprimento zero). Uma vez
		// por no: e erro de montagem, nao estado transitorio, entao nao precisa
		// da contagem de persistencia que o aviso de alcance usa.
		bool m_WarnedChain = false;
	};

	// ── Hide Controls ────────────────────────────────────────────────────────
	//
	// Some com os controles da lista quando ligado, e devolve quando desligado.
	//
	// Serve pro par IK/FK: com o interruptor em IK, os controles de FK viram
	// ruido na tela — e pior, dao pra clicar e mexer sem efeito nenhum, porque
	// o ramo que os le nem esta rodando. Escondendo, sobra so o que de fato
	// controla alguma coisa naquele momento.
	class AXE_API RigNode_HideControls : public RigNode
	{
	public:
		RigNode_HideControls()
		{
			Title = "Hide Controls";
			HasExecIn = true;
			ExecOut.push_back("");

			AddIn("Controls", RigPinType::ItemArray);
			AddInBool("Active", true);
		}

		const char* TypeName() const override { return "HideControls"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_HideControls>(*this);
		}

		void Execute(RigExecContext& ctx) override;
	};

	// ── Parent Constraint ────────────────────────────────────────────────────
	//
	// Faz um elemento SEGUIR outro(s), como se fosse filho — sem realmente
	// reparentear.
	//
	// E a metade que faltava do casamento IK <-> FK. O Project to New Parent
	// resolve um lado (o FK acompanha o resultado do IK); este resolve o outro:
	// o controle de IK segue o osso movido pelo FK, entao virar a chave de FK
	// pra IK nao faz o membro saltar.
	//
	// Aceita VARIOS pais e mistura entre eles em partes iguais — e assim que se
	// faz uma mao "presa" a um volante ou a outro personagem sem trocar a
	// hierarquia de verdade.
	class AXE_API RigNode_ParentConstraint : public RigNode
	{
	public:
		RigNode_ParentConstraint()
		{
			Title = "Parent Constraint";
			HasExecIn = true;
			ExecOut.push_back("");

			AddInItem("Child", RigElementType::Control);

			// Ligado por padrao: sem manter a folga, o filho SALTA pra cima do
			// pai no instante em que o no roda — quase nunca e o que se quer.
			AddInBool("Maintain Offset", true);

			AddIn("Parents", RigPinType::ItemArray);
			AddInFloat("Weight", 1.0f);
		}

		const char* TypeName() const override { return "ParentConstraint"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_ParentConstraint>(*this);
		}

		void Execute(RigExecContext& ctx) override;

	private:
		bool m_WarnedEmpty = false;
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

	// ── Pelvis Dip ───────────────────────────────────────────────────────────
	//
	// ABAIXA a raiz do corpo pra que o pe MAIS BAIXO consiga alcancar o chao.
	//
	// ── O PROBLEMA QUE ELE RESOLVE ───────────────────────────────────────
	//
	// Numa rampa, os dois pes pedem coisas OPOSTAS: o de cima precisa que a
	// perna ENCURTE (o joelho dobra, e o Two Bone IK faz isso bem) e o de baixo
	// precisa que a perna ESTIQUE. Uma perna de Mixamo em idle ja esta a ~98%
	// da extensao, entao nao ha o que esticar: o clamp de alcance do Two Bone
	// IK satura, o membro fica RETO e o pe NAO chega no chao — em silencio,
	// porque o no rodou e resolveu, so nao chegou onde foi mandado.
	//
	// O sintoma classico e "uma perna faz IK e a outra nao", com os DOIS grafos
	// identicos. Nao e espelhamento nem erro de fio: e a perna de baixo.
	//
	// ── A SOLUCAO ────────────────────────────────────────────────────────
	//
	// A mesma do AnimNode_FootIK: desce o quadril pelo deficit do pe mais
	// baixo. O de baixo passa a alcancar, o de cima dobra mais — que e
	// exatamente a postura de quem esta de pe numa rampa.
	//
	// SO DESCE, nunca sobe. Levantar o quadril pra "alcancar" um pe alto faz o
	// personagem flutuar, e o pe alto ja e resolvido dobrando o joelho.
	//
	// ── ONDE POR NO GRAFO ────────────────────────────────────────────────
	//
	// ANTES dos Two Bone IK das duas pernas. Num Sequence, no Then 0 — que
	// desde a correcao do fluxo do Sequence roda PRIMEIRO de verdade.
	//
	// Ele le os dois Height dentro do proprio Execute, e isso resolve a ordem
	// de leitura por construcao: o m_Cache do RigGraph memoiza os traces com os
	// valores de ANTES do quadril descer, que e a referencia correta — o mesmo
	// motivo de o AnimNode_FootIK guardar footOrig antes do dip.
	class AXE_API RigNode_PelvisDip : public RigNode
	{
	public:
		RigNode_PelvisDip();

		const char* TypeName() const override { return "PelvisDip"; }

		// A copia nasce FRIA, como a do Damp Float: m_Init falso faz o primeiro
		// solve SNAPAR no valor em vez de subir de zero. Sem isso o personagem
		// afundaria visivelmente no instante do Play.
		std::unique_ptr<RigNode> Clone() const override
		{
			auto c = std::make_unique<RigNode_PelvisDip>(*this);
			c->m_Init = false;
			return c;
		}

		void Execute(RigExecContext& ctx) override;

		// Serialize / Deserialize NAO sao sobrescritos de proposito: todo o
		// estado configuravel vive em PINOS, e a base ja cuida deles. m_Dip e
		// m_Init sao estado de runtime — gravar a suavizacao de um frame no
		// .axerig seria errado.

	private:
		float m_Dip = 0.0f;
		bool  m_Init = false;

		// Item nao resolvido e no-op, nunca crash — mas MUDO nao. Este no
		// existe justamente pra tornar visivel uma falha silenciosa.
		bool  m_WarnedNoPelvis = false;
	};

	// ── Trace (PURO) ─────────────────────────────────────────────────────────
	//
	// Raycast em QUALQUER direcao. Irmao do Ground Trace, nao substituto.
	//
	// O Ground Trace e um no de CHAO: ele trava a direcao em -Y, comeca o raio
	// acima do ponto e mede altura relativa ao plano de apoio do personagem.
	// Esse trabalho extra e util pra pe e inutil pra parede — e generalizar a
	// direcao dele faria o pino Height passar a mentir.
	//
	// Serve pra sondar o ambiente: parede a frente pra o personagem estender a
	// mao, teto acima, beirada ao lado.
	//
	// ── O RAIO PODE ACERTAR O PROPRIO PERSONAGEM ─────────────────────────
	//
	// PhysicsSystem::Raycast nao tem filtro nem lista de ignorados: devolve o
	// corpo mais proximo, inclusive o seu. Um traco do peito pra frente comeca
	// DENTRO da capsula do personagem e acertaria ele mesmo todo frame.
	//
	// Por isso existe o pino Start Offset: ele empurra a origem ao longo da
	// direcao antes de disparar. Ponha um pouco mais que o raio da capsula.
	// A solucao definitiva e um parametro de ignore no proprio Raycast, mas
	// isso e mudanca no modulo de fisica e nao vale sem necessidade provada.
	//
	// No PREVIEW do editor nao ha mundo pra consultar e nao existe "parede
	// virtual" como existe o chao virtual — entao aqui dentro ele devolve
	// sempre "nao acertou". Isso e esperado; teste em Play.
	class AXE_API RigNode_Trace : public RigNode
	{
	public:
		RigNode_Trace();

		const char* TypeName() const override { return "Trace"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_Trace>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

	private:
		bool m_WarnedNoDirection = false;
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
		// APENDAR no fim, sempre. A operacao vai pro .axerig como INTEIRO
		// (j["op"] = (int)Operation), entao inserir no meio reinterpretaria
		// todo arquivo ja salvo: um Lerp viraria Scale em silencio.
		enum class Op { Add, Subtract, Scale, Lerp, Cross, Normalize };

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

	// ── Float Math (PURO) ────────────────────────────────────────────────────
	//
	// O grafo nao tinha NENHUMA matematica escalar — so o VectorOp. Sem isto
	// nao da pra montar um Foot IK como grafo: falta clampar o alcance, tirar
	// o minimo entre duas folgas, misturar dois pesos.
	//
	// Os pinos SE RENOMEIAM conforme a operacao (Clamp vira Value/Min/Max,
	// Lerp vira A/B/Alpha). Os fios referenciam o pino por INDICE, entao
	// renomear e seguro — e "A, B, C" num Clamp nao diria qual e o minimo.
	class AXE_API RigNode_FloatMath : public RigNode
	{
	public:
		enum class Op { Add, Subtract, Multiply, Divide, Min, Max, Clamp, Lerp, Abs };

		RigNode_FloatMath();

		const char* TypeName() const override { return "FloatMath"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_FloatMath>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		// Renomeia os pinos pra casarem com a operacao. Chamado pelo painel de
		// detalhes e pelo Deserialize.
		void ApplyOperation();

		Op Operation = Op::Add;
	};

	// ── Select Float (PURO) ──────────────────────────────────────────────────
	//
	// A ponte que faltava de Bool pra Float. O Ground Trace diz "acertou" num
	// bool, e o peso do Two Bone IK pede um float — sem este no, "pe no chao =
	// peso 1, pe no vazio = peso 0" so daria pra montar com um Branch e dois
	// ramos duplicados.
	class AXE_API RigNode_SelectFloat : public RigNode
	{
	public:
		RigNode_SelectFloat();

		const char* TypeName() const override { return "SelectFloat"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_SelectFloat>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;
	};

	// ── Damp Float / Damp Vector (PUROS, mas COM ESTADO) ─────────────────────
	//
	// Perseguem o valor de entrada em vez de saltar nele. Sao os UNICOS nos do
	// rig que lembram do frame anterior — e e por isso que existem: um raycast
	// que muda de superficie entre dois frames faz o pe dar POP sem eles.
	//
	// O estado sobrevive ao ResetToInitial de proposito (ele reseta a
	// HIERARQUIA, nao os nos). Cada personagem roda o proprio clone do grafo,
	// entao dois personagens nao dividem a suavizacao um do outro.
	class AXE_API RigNode_DampFloat : public RigNode
	{
	public:
		RigNode_DampFloat();

		const char* TypeName() const override { return "DampFloat"; }

		// A copia nasce FRIA: m_Init falso faz o primeiro frame SNAPAR no
		// valor em vez de subir de zero. Sem isso o personagem "assentaria"
		// visivelmente no instante do Play.
		std::unique_ptr<RigNode> Clone() const override
		{
			auto c = std::make_unique<RigNode_DampFloat>(*this);
			c->m_Init = false;
			return c;
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

	private:
		float m_Value = 0.0f;
		bool  m_Init = false;
	};

	class AXE_API RigNode_DampVector : public RigNode
	{
	public:
		RigNode_DampVector();

		const char* TypeName() const override { return "DampVector"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			auto c = std::make_unique<RigNode_DampVector>(*this);
			c->m_Init = false;
			return c;
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

	private:
		glm::vec3 m_Value{ 0.0f };
		bool      m_Init = false;
	};

	// ── Align To Vector ──────────────────────────────────────────────────────
	//
	// Inclina um elemento pela MESMA rotacao que leva um vetor a outro. E como
	// o pe acompanha a inclinacao da rampa.
	//
	// ── POR QUE NAO E UM "AIM" ───────────────────────────────────────────
	//
	// A tentacao e fazer "aponte o eixo +Y do pe pra normal do chao". Isso JA
	// FOI TENTADO no AnimNode_FootIK e deu errado: no rig Mixamo o osso do pe
	// aponta pro DEDO, nao pra cima, entao alinhar o +Y a normal girava o pe
	// por um angulo enorme e arbitrario — o pe saia deformado e invertido.
	//
	// A solucao rig-agnostica: em chao plano a animacao JA orienta o pe certo,
	// entao basta aplicar POR CIMA a inclinacao do chao em relacao ao plano.
	// Nao se supoe eixo nenhum do osso — so a diferenca entre dois vetores do
	// mundo. Ligue From = (0,1,0) e To = a Normal do Ground Trace.
	class AXE_API RigNode_AlignToVector : public RigNode
	{
	public:
		RigNode_AlignToVector();

		const char* TypeName() const override { return "AlignToVector"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_AlignToVector>(*this);
		}

		void Execute(RigExecContext& ctx) override;

	private:
		bool m_WarnedDegenerate = false;
	};


} // namespace axe