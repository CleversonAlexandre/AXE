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

	// ── Evaluate ─────────────────────────────────────────────────────────────
	//
	// LE os pinos ligados nele e joga o resultado fora.
	//
	// Parece um no que nao faz nada, e do ponto de vista da POSE ele nao faz
	// mesmo. O que ele controla e QUANDO um valor e lido.
	//
	// ── POR QUE ISSO E UMA PRIMITIVA ─────────────────────────────────────
	//
	// O grafo tem dois mecanismos: execucao empurra (o evento chama o primeiro
	// no, que chama o proximo) e dado PUXA sob demanda, com memoizacao — a
	// primeira leitura calcula, as seguintes reaproveitam, ate o fim do solve.
	//
	// A memoizacao e o que faz um Ground Trace lido por tres nos custar UM
	// raycast. Mas ela tambem significa que o valor de um pino depende de
	// QUANDO ele foi lido pela primeira vez. E ate aqui nao havia como dizer
	// "leia isto agora": voce so conseguia forcar uma leitura escrevendo em
	// algum lugar, com um Set Transform num Null descartavel.
	//
	// Esse truque funcionava e era invisivel — ninguem descobre sozinho que um
	// no de escrita inutil e a forma de ordenar leituras. Um caminho que existe
	// mas nao pode ser encontrado esta fechado na pratica.
	//
	// ── O CASO CONCRETO ──────────────────────────────────────────────────
	//
	// Foot IK numa rampa: o quadril precisa descer pelo deficit do pe mais
	// baixo, e as alturas precisam ser lidas ANTES de o quadril se mover —
	// senao os traces respondem a partir de pes ja deslocados e o efeito conta
	// duas vezes.
	//
	//     Sequence
	//       Then 0 ─► Evaluate  (Height esquerdo, Height direito)
	//       Then 1 ─► Set Transform (Hips)
	//       Then 2 ─► Two Bone IK esquerda
	//       Then 3 ─► Two Bone IK direita
	//
	// E por causa desta lacuna que o RigNode_PelvisDip existe em C++: ele le as
	// alturas dentro do proprio Execute pra resolver a ordem por construcao.
	// Com o Evaluate, aquele no pode virar uma funcao de cinco nos.
	//
	// ── NAO CONFUNDIR COM O SEQUENCE ─────────────────────────────────────
	//
	// O Sequence ordena EXECUCAO: quem escreve primeiro. O Evaluate ordena
	// LEITURA: quando um valor e capturado. Sao eixos diferentes, e juntar os
	// dois no mesmo no faria o mais usado do grafo carregar um conceito que
	// quase nenhum grafo precisa.
	class AXE_API RigNode_Evaluate : public RigNode
	{
	public:
		RigNode_Evaluate()
		{
			Title = "Evaluate";
			HasExecIn = true;
			ExecOut.push_back("");

			// Wildcard: o no nao se importa com o TIPO do que le. Dois pinos
			// porque um so quase nunca e o caso — a ordenacao costuma envolver
			// um par (dois pes, duas maos).
			AddIn("A", RigPinType::Wildcard);
			AddIn("B", RigPinType::Wildcard);
		}

		const char* TypeName() const override { return "Evaluate"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_Evaluate>(*this);
		}

		void Execute(RigExecContext& ctx) override
		{
			// Le e descarta. A leitura em si e o efeito: ela grava no cache do
			// grafo, e quem ler depois recebe ESTE valor, capturado agora.
			for (std::size_t i = 0; i < Inputs.size(); ++i)
				(void)Read(ctx, (int)i);
		}

		// O numero de pinos e autoral, entao precisa sobreviver ao arquivo.
		// Mesmo motivo do exec_out do Sequence.
		void Serialize(nlohmann::json& j) const override
		{
			j["ins"] = SaveRigPinLayout(Inputs);
		}

		void Deserialize(const nlohmann::json& j) override
		{
			if (j.contains("ins"))
				LoadRigPinLayout(j["ins"], Inputs);
		}
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

	// ── Get Camera Transform (PURO) ──────────────────────────────────────────
	//
	// Onde esta a camera?
	//
	// Ja convertido pro espaco do rig, como todo o resto — voce liga a Location
	// direto num Two Bone IK ou a Forward num Align To Vector, sem conversao no
	// meio. O no cuida disso porque e ele quem sabe que o valor veio de fora.
	//
	// ── O QUE ISSO DESTRAVA ──────────────────────────────────────────────
	//
	// Look-at: a cabeca acompanha quem olha. Align To Vector no pescoco, From =
	// o "pra frente" da cabeca em repouso, To = a direcao ate a camera.
	//
	// Aim offset procedural: o torso gira conforme a mira, em qualquer angulo —
	// e nao so nos que alguem animou.
	//
	// LOD de rig: Distance entre a camera e o quadril, um Clamp, e o peso dos
	// IKs cai sozinho quando o personagem esta longe. Performance expressa em
	// tres nos, sem nenhum sistema novo.
	//
	// Correcao de silhueta: a arma que atravessa o ombro num certo angulo, so
	// naquele angulo.
	//
	// ── SO LEITURA ───────────────────────────────────────────────────────
	//
	// Nao existe Set Camera Transform. O rig consulta a engine e nunca a
	// comanda — mover a camera e trabalho do gameplay ou do Sequencer.
	class AXE_API RigNode_GetCameraTransform : public RigNode
	{
	public:
		RigNode_GetCameraTransform()
		{
			Title = "Get Camera Transform";

			AddOut("Transform", RigPinType::Transform);
			AddOut("Location", RigPinType::Vector);

			// O "pra frente" da camera, ja normalizado. E o que se liga num
			// Align To Vector; tira-lo do Transform exigiria Break + montar a
			// coluna a mao, que ninguem faz sem errar o sinal.
			AddOut("Forward", RigPinType::Vector);

			AddOut("Fov", RigPinType::Float);

			// Ha camera? Sem isto, um preview sem camera devolve zeros e o
			// personagem olha pra origem do rig sem que nada explique.
			AddOut("Valid", RigPinType::Bool);
		}

		const char* TypeName() const override { return "GetCameraTransform"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_GetCameraTransform>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;
	};

	// ── Get Gameplay Variable (PURO) ─────────────────────────────────────────
	//
	// Le uma variavel do blackboard do AnimGraph — o mesmo quadro que a State
	// Machine usa pra decidir transicoes, onde o gameplay escreve "Speed = 320",
	// "IsGrounded = false", "AimWeight = 0.7".
	//
	// ── QUAL PERGUNTA ELE RESPONDE ───────────────────────────────────────
	//
	// "Qual o estado atual do corpo?" — e nao "o que o personagem deveria
	// fazer". A distincao decide o que pode entrar aqui:
	//
	//   BOM: velocidade, se esta no chao, altura do agachamento, peso de mira.
	//        Descrevem o corpo AGORA. O rig usa pra ajustar a pose.
	//
	//   RUIM: AttackState, CurrentCombo, "vai pular no proximo frame". Sao
	//         DECISAO, e decisao mora na maquina de estados do AnimGraph. Um
	//         Branch num estado de combate dentro do rig e regra de negocio no
	//         lugar errado.
	//
	// O teste que separa os dois: este valor ainda faria sentido se o rig fosse
	// avaliado DUAS VEZES no mesmo frame, ou num preview sem jogo rodando?
	// Velocidade sim; "acabou de disparar" nao.
	//
	// ── SO LEITURA ───────────────────────────────────────────────────────
	//
	// Nao existe Set Gameplay Variable, e nao vai existir. O rig consulta a
	// engine e nunca a comanda — a unica coisa que ele escreve e a pose.
	// Triggers tambem ficam de fora: consumi-los e efeito colateral, e um rig
	// avaliado duas vezes no mesmo frame acharia o pulso ja gasto na segunda.
	class AXE_API RigNode_GetGameplayVariable : public RigNode
	{
	public:
		RigNode_GetGameplayVariable()
		{
			Title = "Get Gameplay Variable";

			// O nome vem de um pino, e nao de um campo no Details, pra poder
			// ser CALCULADO — um For Each sobre uma lista de nomes, por
			// exemplo. Pino de texto ainda nao existe no grafo, entao por ora
			// e um campo; quando existir, vira pino sem quebrar nada.
			AddOut("Float", RigPinType::Float);
			AddOut("Bool", RigPinType::Bool);

			// "Existe no blackboard?" separado do valor. Sem isto, um nome
			// digitado errado devolve 0 e voce nao tem como distinguir de um
			// zero legitimo — o tipo de silencio que este rig evita.
			AddOut("Found", RigPinType::Bool);
		}

		const char* TypeName() const override { return "GetGameplayVariable"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_GetGameplayVariable>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

		void Serialize(nlohmann::json& j) const override { j["var"] = VariableName; }

		void Deserialize(const nlohmann::json& j) override
		{
			VariableName = j.value("var", std::string());

			// O titulo segue o nome enquanto voce nao renomear: cinco "Get
			// Gameplay Variable" na tela nao dizem nada.
			if (!VariableName.empty())
				Title = VariableName;
		}

		std::string VariableName;

	private:
		bool m_WarnedMissing = false;
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

	// ── Offset Location ──────────────────────────────────────────────────────
	//
	// DESLOCA um elemento por um vetor, sem tocar em rotacao nem escala.
	//
	// ── POR QUE ISTO E PRIMITIVA, E NAO ATALHO ───────────────────────────
	//
	// Deslocar um osso exigia quatro nos: Get Transform, Vector Add, Make
	// Transform e Set Transform. Nao era so verbosidade — o Make Transform
	// CONSTROI um transform novo a partir de zero, entao a rotacao e a escala
	// que o elemento tinha se perdem, a nao ser que voce as leia e reponha a
	// mao com um Break Transform no meio.
	//
	// O que se quer dizer e "empurre isto um pouco pra la". Nenhuma composicao
	// dos nos existentes diz isso sem tambem dizer, sem querer, "e esqueca como
	// ele estava girado".
	//
	// ── ONDE APARECE ─────────────────────────────────────────────────────
	//
	// O dip do quadril numa rampa, o recuo da mao antes da parede, qualquer
	// correcao de altura. Tudo que e "a pose esta certa, so precisa sair um
	// pouco do lugar".
	class AXE_API RigNode_OffsetLocation : public RigNode
	{
	public:
		RigNode_OffsetLocation();

		const char* TypeName() const override { return "OffsetLocation"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_OffsetLocation>(*this);
		}

		void Execute(RigExecContext& ctx) override;

		void Serialize(nlohmann::json& j) const override
		{
			j["space"] = (int)Space;
			j["propagate"] = PropagateToChildren;
		}

		void Deserialize(const nlohmann::json& j) override
		{
			Space = (RigSpace)j.value("space", 0);
			PropagateToChildren = j.value("propagate", true);
		}

		RigSpace Space = RigSpace::Global;

		// Ligado por padrao: deslocar o quadril tem que levar as pernas junto.
		bool PropagateToChildren = true;
	};

	// ── Meters To Component (PURO) ───────────────────────────────────────────
	//
	// Converte METROS do mundo em unidades de ESPACO DE COMPONENTE.
	//
	// ── POR QUE ISTO PRECISA EXISTIR ─────────────────────────────────────
	//
	// O rig fala duas linguas. Ground Trace e Trace recebem distancia em
	// METROS, porque conversam com a fisica; todo o resto trabalha em espaco de
	// componente, onde 1 unidade depende da escala do personagem.
	//
	// Os nos que atravessam a fronteira ja fazem a conversao por dentro — o
	// Ground Trace divide pela escala antes de devolver o Height. Mas quando
	// VOCE monta a mesma logica em grafo, nao ha como fazer a conta: a escala
	// do personagem nao esta exposta em lugar nenhum.
	//
	// O sintoma sem este no e cruel: funciona no personagem em escala 1 e
	// desanda em qualquer outro, sem nada apontar pra causa. Foi o unico ponto
	// em que a versao em grafo do Pelvis Dip nao empatava com o no em C++.
	//
	// Componente -> metros e o mesmo no com Invert ligado.
	class AXE_API RigNode_MetersToComponent : public RigNode
	{
	public:
		RigNode_MetersToComponent()
		{
			Title = "Meters To Component";

			AddInFloat("Meters", 1.0f);
			AddInBool("Invert", false);

			AddOut("Result", RigPinType::Float);

			// A escala do personagem, exposta pra quem precisar dela crua —
			// comparar tamanhos, normalizar um limiar. Sem isto ela continuaria
			// escondida dentro dos nos de fronteira.
			AddOut("Scale", RigPinType::Float);
		}

		const char* TypeName() const override { return "MetersToComponent"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_MetersToComponent>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;
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
	//
	// ── DIVIDA CONHECIDA ─────────────────────────────────────────────────
	//
	// Este no SABE DEMAIS: que existem dois pes, que o mais baixo manda, e que
	// descer e certo mas subir nao. Isso e conhecimento de bipede em terreno
	// irregular — uma SOLUCAO, nao uma capacidade do motor. Pelo criterio de
	// nos novos ("a composicao equivalente seria correta e legivel?"), ele nao
	// deveria existir: Float Math (Min) + Get Transform + Vector Add + Set
	// Transform fazem o mesmo, de forma legivel.
	//
	// Ele nasceu porque faltava uma primitiva de ORDENACAO DE LEITURA — e a
	// unica forma de forcar a captura das alturas era um Set Transform num Null
	// descartavel, truque que ninguem descobre sozinho.
	//
	// Essa primitiva agora existe: o no Evaluate. A rota de saida esta aberta:
	//
	//   1. reimplementar como FUNCAO EMBARCADA (cinco nos, usando Evaluate);
	//   2. depreciar este no com migracao no load, virando reroute — o mesmo
	//      caminho ja usado com ControlFollowBone e Collapsed, que aposentam um
	//      tipo sem quebrar nenhum .axerig.
	//
	// Ate la ele fica: remove-lo hoje fecharia um caminho na pratica, e a
	// premissa do rig e nunca fechar caminho. Mas NAO use este no como modelo
	// pra nos novos — ele e a excecao registrada, nao o padrao.
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
	// ── LOCAL OU GLOBAL, E POR QUE ISTO PRECISOU EXISTIR ─────────────────────
	//
	// O no so copiava LOCAL. Transform local e um numero medido no referencial
	// do PAI, entao copia-lo de um elemento para outro so quer dizer a mesma
	// coisa se os dois tiverem pais equivalentes.
	//
	// Isso vale no MIOLO de uma cadeia (ctrl_Spine1 pende de ctrl_Spine, como
	// Spine1 pende de Spine) e falha justamente na RAIZ dela: `ctrl_Hips` pende
	// de `ctrl_RootNode` — um controle de rig criado na origem — enquanto
	// `mixamorig:Hips` pende de `RootNode`, o osso que carrega a conversao de
	// eixo do FBX. Os dois referenciais diferem por uma rotacao fixa, e o
	// quadril inteiro sai girado por ela.
	//
	// Nada no grafo denuncia isso: as listas estao pareadas, os nomes estao
	// certos, e o resultado esta torto. E o no nao tinha nem como ser corrigido
	// de fora — as entradas sao ItemArray, e nao havia onde escolher o espaco.
	//
	// Em GLOBAL a pergunta nem se coloca: o osso vai para ONDE O CONTROLE ESTA,
	// e de quem cada um pende deixa de importar.
	//
	// Default LOCAL para nenhum `.axerig` mudar de comportamento ao abrir. Quem
	// tiver o problema acima troca para Global; a auditoria do Sequencer aponta
	// os pares afetados pelo nome.
	//
	// Ordem de execucao: em LOCAL nao importa (escrever local nao depende do
	// pai). Em GLOBAL tambem nao, e por um motivo diferente — TODO osso da lista
	// e escrito explicitamente, entao um pai escrito depois do filho arrasta o
	// filho e o filho ja foi (ou sera) posto no lugar certo de qualquer forma.
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

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		RigSpace Space = RigSpace::Local;

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

	// ── Vector To Float (PURO) ───────────────────────────────────────────────
	//
	// Mede vetores. Entra vetor, sai NUMERO — e e por isso que nao cabe no
	// Vector Op: la a saida e sempre Vector, e fazer o tipo dela mudar conforme
	// a operacao quebraria os fios ja ligados a cada troca no combo.
	//
	// ── O QUE ELE DESTRAVA ───────────────────────────────────────────────
	//
	// Sem medir distancia, o rig so consegue liga-desliga: o Hit de um trace e
	// bool, entao o braco sobe inteiro ou nao sobe. Com Distance + Float Math
	// (Clamp), o braco sobe PROPORCIONALMENTE conforme a parede se aproxima —
	// e a diferenca entre um IK que "pipoca" e um que responde.
	//
	// Dot resolve a outra classe de pergunta: "o quanto estas duas direcoes
	// concordam?". Com dois vetores unitarios ele vale 1 apontando junto, 0
	// perpendicular, -1 opostos. E como se pergunta se a parede esta a frente
	// ou atras, ou o quanto o chao esta inclinado.
	class AXE_API RigNode_VectorToFloat : public RigNode
	{
	public:
		enum class Op { Length, Distance, Dot, X, Y, Z };

		RigNode_VectorToFloat()
		{
			Title = "Vector Length";

			AddIn("A", RigPinType::Vector);
			AddIn("B", RigPinType::Vector);

			AddOut("Result", RigPinType::Float);
		}

		const char* TypeName() const override { return "VectorToFloat"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_VectorToFloat>(*this);
		}

		void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out) override;

		void Serialize(nlohmann::json& j) const override { j["op"] = (int)Operation; }

		void Deserialize(const nlohmann::json& j) override
		{
			Operation = (Op)j.value("op", 0);
		}

		// APENDAR no fim, sempre: a operacao vai pro .axerig como INTEIRO, e
		// inserir no meio reinterpretaria todo arquivo ja salvo.
		Op Operation = Op::Length;
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