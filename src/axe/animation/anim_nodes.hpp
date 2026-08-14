#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/anim_node.hpp"
#include "axe/animation/anim_pose_graph.hpp"
#include "axe/animation/anim_graph.hpp"      // AnimTransition, AnimCondition
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/blend_space_1d.hpp"
#include "axe/animation/bone_mask.hpp"
#include "axe/animation/rig/control_rig_asset.hpp"   // AnimNode_ControlRig

#include <cmath>     // std::fabs — nos logicos (AG4)
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace axe
{
	// ── Output Pose ───────────────────────────────────────────────────────────
	//
	// A raiz. Não faz nada além de repassar — e é justamente por isso que
	// existe: dá ao grafo um ponto de partida único e explícito. Sem ele, "qual
	// nó é o resultado?" viraria uma heurística (o último? o mais à direita?),
	// e heurística em ponto de entrada é fonte de bug eterno.
	class AXE_API AnimNode_Output : public AnimNode
	{
	public:
		const char* TypeName() const override { return "Output"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_Output>(*this);
			CopyCommonTo(*c);
			return c;
		}
		int InputCount() const override { return 1; }
		const char* InputName(int) const override { return "Result"; }

		void Update(AnimEvalContext& ctx) override { UpdateInput(ctx, 0); }
		void Evaluate(AnimEvalContext& ctx, Pose& out) override { EvalInput(ctx, 0, out); }
	};

	// ── Reroute ───────────────────────────────────────────────────────────────
	//
	// Um "no" no fio. Recebe uma pose e devolve a MESMA pose.
	//
	// POR QUE ELE EXISTE (AG3):
	//
	//   Grafo de locomocao real tem fios que atravessam a tela inteira e
	//   cruzam por cima de outros nos. O reroute e onde voce dobra o fio para
	//   que ele passe por baixo, por cima, ou contorne. E organizacao visual.
	//
	// POR QUE E UM NO DE RUNTIME, e nao decoracao de editor:
	//
	//   Ele esta NO CAMINHO da pose. Se existisse so no editor, o .axeanim
	//   precisaria gravar o link "resolvido" (pulando o reroute), e ai a
	//   posicao visual dele nao teria onde morar — ou moraria numa tabela
	//   paralela que precisa ser mantida em sincronia com os links. Um
	//   passthrough e mais barato que essa sincronia, e e o mesmo desenho
	//   que o Control Rig ja usa.
	//
	//   O custo em runtime e uma chamada de funcao por reroute por frame.
	//
	// SOBRE A GUARDA DO AG2: o reroute herda UpdateOnce como qualquer no, e
	// isso importa mais aqui do que na media — reroute existe justamente para
	// ser reusado, entao ele e um candidato natural a ter varios consumidores.
	class AXE_API AnimNode_Reroute : public AnimNode
	{
	public:
		const char* TypeName() const override { return "Reroute"; }

		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_Reroute>(*this);
			CopyCommonTo(*c);
			return c;
		}

		// AG3b — o reroute ADOTA o tipo do fio em que foi inserido.
		//
		// Sem isto ele seria sempre de pose, e um reroute num fio de dado
		// (Get Float -> Alpha) teria a segunda ponta recusada pela validacao
		// de link: sobraria um no solto no meio do grafo, com metade do fio
		// desligada e nenhuma mensagem dizendo por que.
		//
		// Serializado, porque um reroute de Float relido como Pose quebraria
		// o grafo no load — e o usuario veria um .axeanim que abriu diferente
		// de como foi salvo.
		void SetPinType(AnimPinType t) { m_PinType = t; }
		AnimPinType OutputType() const override { return m_PinType; }

		// Pose e dado moram em listas de pino SEPARADAS (Inputs x DataInputs),
		// entao o reroute troca de lista conforme o tipo. Quando e dado, o
		// InputCount de pose e zero — senao o no anunciaria um pino de pose
		// que ninguem alimenta.
		int InputCount() const override { return m_PinType == AnimPinType::Pose ? 1 : 0; }
		const char* InputName(int) const override { return ""; }

		void Update(AnimEvalContext& ctx) override
		{
			if (m_PinType == AnimPinType::Pose)
				UpdateInput(ctx, 0);
		}

		void Evaluate(AnimEvalContext& ctx, Pose& out) override
		{
			if (m_PinType == AnimPinType::Pose)
				EvalInput(ctx, 0, out);
		}

		// Repasse dos valores de dado. Le o pino 0 de DataInputs, que o
		// editor cria quando o tipo nao e pose.
		float EvaluateFloat(AnimEvalContext& ctx) override { return ReadFloat(ctx, 0); }
		bool  EvaluateBool(AnimEvalContext& ctx)  override { return ReadBool(ctx, 0); }

		void Serialize(nlohmann::json& j) const override
		{
			j["pin_type"] = (int)m_PinType;
		}

		void Deserialize(const nlohmann::json& j) override
		{
			m_PinType = (AnimPinType)j.value("pin_type", (int)AnimPinType::Pose);
			RebuildPins();
		}

		// Cria o pino de dado quando o tipo pede. Chamado no load e pelo
		// editor logo depois do SetPinType.
		void RebuildPins()
		{
			DataInputs.clear();

			if (m_PinType == AnimPinType::Float)      AddFloatPin("", 0.0f);
			else if (m_PinType == AnimPinType::Bool)  AddBoolPin("", false);
		}

		// GetNormalizedTime atravessa o reroute.
		//
		// Sem isto, um reroute entre um Clip Player e a maquina de estados
		// quebraria o ExitTime das transicoes: a maquina perguntaria o tempo
		// normalizado, receberia o default, e a transicao dispararia na hora
		// errada. Bug que so apareceria depois de alguem organizar o grafo —
		// o pior tipo de armadilha.
		float GetNormalizedTime() const override
		{
			return Inputs.empty() || !Inputs[0] ? 0.0f : Inputs[0]->GetNormalizedTime();
		}

	private:
		AnimPinType m_PinType = AnimPinType::Pose;
	};

	// ── Nós de VARIÁVEL ───────────────────────────────────────────────────────
	//
	// O "nó verde" da Unreal. Lê um parâmetro do blackboard e o entrega num
	// pino de dado.
	//
	// Por que um NÓ, e não apenas um nome digitado dentro do consumidor?
	//
	// Porque um nó pode ser reusado (um único "Speed" alimentando o blend space
	// E a condição de uma transição), e porque abre espaço pra pôr qualquer
	// coisa NO MEIO: um Clamp, um Lerp, uma curva de resposta. Com o nome
	// digitado dentro do nó consumidor, não existe "meio" — o grafo vira um
	// formulário com fios.
	class AXE_API AnimNode_GetFloat : public AnimNode
	{
	public:
		std::string Parameter = "Speed";

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		const char* TypeName() const override { return "GetFloat"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_GetFloat>(*this);
			CopyCommonTo(*c);
			return c;
		}
		AnimPinType OutputType() const override { return AnimPinType::Float; }

		// Nó de valor: não produz pose. Update/Evaluate são no-op — ele é
		// puxado pelo ReadFloat de quem o consome, não pela árvore de poses.
		void Update(AnimEvalContext&) override {}
		void Evaluate(AnimEvalContext&, Pose&) override {}

		float EvaluateFloat(AnimEvalContext& ctx) override
		{
			return ctx.GetFloat(Parameter);
		}
	};

	// ── Nos LOGICOS (AG4) ────────────────────────────────────────────────────
	//
	// POR QUE ELES EXISTEM
	//
	//   Ate aqui, toda decisao sobre um valor de dado tinha que ser tomada no
	//   Script Editor e chegar aqui pronta, como parametro. Isso funciona, mas
	//   espalha a logica de animacao por dois lugares: "o Alpha do Control Rig
	//   cai quando agachado" e regra de ANIMACAO, e vive melhor no grafo de
	//   animacao do que num script de gameplay.
	//
	//   Sao nos de DADO, como o GetFloat: Update e Evaluate sao no-op, e eles
	//   sao puxados pelo ReadFloat/ReadBool de quem os consome.
	//
	// SEM ESTADO, DE PROPOSITO
	//
	//   Nenhum destes nos guarda nada entre frames. Isso os torna imunes ao
	//   problema que o AG2 resolveu: ler duas vezes no mesmo frame devolve o
	//   mesmo valor e nao custa nada alem de recalcular. Por isso eles nao
	//   precisam da guarda de UpdateOnce — nao ha tempo para avancar.
	//
	//   Se algum dia entrar um no com estado (um Spring, um Delay), ele NAO
	//   pode seguir este molde: vai precisar de tempo, e portanto de Update
	//   de verdade.

	// Inverte um booleano.
	class AXE_API AnimNode_Not : public AnimNode
	{
	public:
		const char* TypeName() const override { return "Not"; }

		AnimNode_Not() { AddBoolPin("In", false); }

		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_Not>(*this);
			CopyCommonTo(*c);
			return c;
		}

		AnimPinType OutputType() const override { return AnimPinType::Bool; }

		void Update(AnimEvalContext&) override {}
		void Evaluate(AnimEvalContext&, Pose&) override {}

		bool EvaluateBool(AnimEvalContext& ctx) override { return !ReadBool(ctx, 0); }
	};

	// AND / OR / XOR num no so, com o operador no painel.
	//
	// Um no por operador daria tres entradas de menu quase identicas e
	// obrigaria a apagar e recriar para trocar de ideia. Com o combo, trocar
	// AND por OR e um clique — e o formato dos pinos nao muda.
	class AXE_API AnimNode_BoolOp : public AnimNode
	{
	public:
		enum class Op { And = 0, Or = 1, Xor = 2 };

		Op Operation = Op::And;

		const char* TypeName() const override { return "BoolOp"; }

		AnimNode_BoolOp()
		{
			AddBoolPin("A", false);
			AddBoolPin("B", false);
		}

		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_BoolOp>(*this);
			CopyCommonTo(*c);
			return c;
		}

		AnimPinType OutputType() const override { return AnimPinType::Bool; }

		void Update(AnimEvalContext&) override {}
		void Evaluate(AnimEvalContext&, Pose&) override {}

		bool EvaluateBool(AnimEvalContext& ctx) override
		{
			const bool a = ReadBool(ctx, 0);
			const bool b = ReadBool(ctx, 1);

			switch (Operation)
			{
			case Op::Or:  return a || b;
			case Op::Xor: return a != b;
			default:      return a && b;
			}
		}

		void Serialize(nlohmann::json& j) const override { j["op"] = (int)Operation; }
		void Deserialize(const nlohmann::json& j) override
		{
			Operation = (Op)j.value("op", (int)Op::And);
		}
	};

	// Compara dois floats e devolve bool. E a ponte entre os dois mundos:
	// um parametro Float vira condicao sem passar pelo script.
	class AXE_API AnimNode_CompareFloat : public AnimNode
	{
	public:
		enum class Op {
			Greater = 0, Less = 1, GreaterEqual = 2, LessEqual = 3,
			Equal = 4, NotEqual = 5
		};

		Op Operation = Op::Greater;

		// Tolerancia do Equal / NotEqual.
		//
		// Comparar float com == e pedir para nunca dar verdadeiro: um valor
		// que "deveria" ser 1.0 costuma ser 0.99999994 depois de um blend.
		// Sem esta folga, o no funcionaria em teoria e nunca na pratica.
		float Tolerance = 0.001f;

		const char* TypeName() const override { return "CompareFloat"; }

		AnimNode_CompareFloat()
		{
			AddFloatPin("A", 0.0f);
			AddFloatPin("B", 0.5f);
		}

		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_CompareFloat>(*this);
			CopyCommonTo(*c);
			return c;
		}

		AnimPinType OutputType() const override { return AnimPinType::Bool; }

		void Update(AnimEvalContext&) override {}
		void Evaluate(AnimEvalContext&, Pose&) override {}

		bool EvaluateBool(AnimEvalContext& ctx) override
		{
			const float a = ReadFloat(ctx, 0);
			const float b = ReadFloat(ctx, 1);

			switch (Operation)
			{
			case Op::Less:         return a < b;
			case Op::GreaterEqual: return a >= b;
			case Op::LessEqual:    return a <= b;
			case Op::Equal:        return std::fabs(a - b) <= Tolerance;
			case Op::NotEqual:     return std::fabs(a - b) > Tolerance;
			default:               return a > b;
			}
		}

		void Serialize(nlohmann::json& j) const override
		{
			j["op"] = (int)Operation;
			j["tol"] = Tolerance;
		}

		void Deserialize(const nlohmann::json& j) override
		{
			Operation = (Op)j.value("op", (int)Op::Greater);
			Tolerance = j.value("tol", 0.001f);
		}
	};

	// Escolhe entre dois floats conforme um bool.
	//
	// E o no que resolve o caso concreto que motivou este conjunto: o Alpha do
	// Control Rig indo a zero quando o personagem esta agachado.
	//
	//   Get Float "IsCrouching" -> Compare (> 0.5) -> Select(True=0, False=1)
	//     -> Alpha do Control Rig
	//
	// BlendTime existe porque a troca crua ESTALA: Alpha pulando de 1 para 0
	// num frame desliga o rig de uma vez, e isso aparece. Com tempo, o valor
	// persegue o alvo. Zero mantem o comportamento instantaneo para quem
	// quiser.
	class AXE_API AnimNode_SelectFloat : public AnimNode
	{
	public:
		float BlendTime = 0.15f;

		const char* TypeName() const override { return "SelectFloat"; }

		AnimNode_SelectFloat()
		{
			AddBoolPin("Condition", false);
			AddFloatPin("True", 1.0f);
			AddFloatPin("False", 0.0f);
		}

		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_SelectFloat>(*this);
			CopyCommonTo(*c);
			return c;
		}

		AnimPinType OutputType() const override { return AnimPinType::Float; }

		void Evaluate(AnimEvalContext&, Pose&) override {}

		// ATENCAO — este no TEM estado (m_Current), entao ele e a excecao a
		// regra do bloco la em cima: precisa de Update de verdade para avancar
		// o blend, e depende do UpdateOnce do AG2 para nao andar em dobro
		// quando tiver dois consumidores.
		//
		// Update so avanca; quem calcula o alvo e o EvaluateFloat.
		void Update(AnimEvalContext& ctx) override
		{
			m_Dt = ctx.DeltaTime;
			m_Advance = ctx.AdvanceTime;
		}

		float EvaluateFloat(AnimEvalContext& ctx) override
		{
			const float target = ReadBool(ctx, 0) ? ReadFloat(ctx, 1) : ReadFloat(ctx, 2);

			if (BlendTime <= 1e-5f || !m_Advance)
			{
				m_Current = target;
				return m_Current;
			}

			if (!m_Primed)
			{
				// Primeiro frame comeca JA no alvo. Sem isto, todo personagem
				// nasceria com o rig subindo de zero durante o primeiro blend,
				// visivel no instante em que ele aparece na cena.
				m_Current = target;
				m_Primed = true;
				return m_Current;
			}

			const float step = m_Dt / BlendTime;
			const float diff = target - m_Current;

			if (std::fabs(diff) <= step)
				m_Current = target;
			else
				m_Current += (diff > 0.0f ? step : -step);

			return m_Current;
		}

		void Reset() override
		{
			m_Current = 0.0f;
			m_Primed = false;
		}

		void Serialize(nlohmann::json& j) const override { j["blend"] = BlendTime; }
		void Deserialize(const nlohmann::json& j) override
		{
			BlendTime = j.value("blend", 0.15f);
		}

	private:
		float m_Current = 0.0f;
		float m_Dt = 0.0f;
		bool  m_Advance = true;
		bool  m_Primed = false;
	};

	// Aritmetica de float. Cobre o resto dos casos sem inventar um no por
	// conta: 1-x, escalar um parametro, limitar um valor.
	class AXE_API AnimNode_FloatMath : public AnimNode
	{
	public:
		enum class Op {
			Add = 0, Subtract = 1, Multiply = 2, Divide = 3,
			Min = 4, Max = 5
		};

		Op Operation = Op::Add;

		const char* TypeName() const override { return "FloatMath"; }

		AnimNode_FloatMath()
		{
			AddFloatPin("A", 0.0f);
			AddFloatPin("B", 1.0f);
		}

		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_FloatMath>(*this);
			CopyCommonTo(*c);
			return c;
		}

		AnimPinType OutputType() const override { return AnimPinType::Float; }

		void Update(AnimEvalContext&) override {}
		void Evaluate(AnimEvalContext&, Pose&) override {}

		float EvaluateFloat(AnimEvalContext& ctx) override
		{
			const float a = ReadFloat(ctx, 0);
			const float b = ReadFloat(ctx, 1);

			switch (Operation)
			{
			case Op::Subtract: return a - b;
			case Op::Multiply: return a * b;

				// Divisao por zero devolve A em vez de inf. Um inf entrando
				// num Alpha contamina a pose inteira com NaN e o personagem
				// some da tela — falha muito pior que um valor errado.
			case Op::Divide:   return std::fabs(b) < 1e-6f ? a : a / b;

			case Op::Min:      return a < b ? a : b;
			case Op::Max:      return a > b ? a : b;
			default:           return a + b;
			}
		}

		void Serialize(nlohmann::json& j) const override { j["op"] = (int)Operation; }
		void Deserialize(const nlohmann::json& j) override
		{
			Operation = (Op)j.value("op", (int)Op::Add);
		}
	};

	class AXE_API AnimNode_GetBool : public AnimNode
	{
	public:
		std::string Parameter = "IsGrounded";

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		const char* TypeName() const override { return "GetBool"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_GetBool>(*this);
			CopyCommonTo(*c);
			return c;
		}
		AnimPinType OutputType() const override { return AnimPinType::Bool; }

		void Update(AnimEvalContext&) override {}
		void Evaluate(AnimEvalContext&, Pose&) override {}

		bool EvaluateBool(AnimEvalContext& ctx) override
		{
			return ctx.GetBool(Parameter);
		}
	};

	// ── Clip Player ───────────────────────────────────────────────────────────
	class AXE_API AnimNode_ClipPlayer : public AnimNode
	{
	public:
		const char* TypeName() const override { return "ClipPlayer"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_ClipPlayer>(*this);
			CopyCommonTo(*c);
			return c;
		}

		std::string ClipName;                      // religado por NOME
		std::shared_ptr<AnimationClip> Clip;

		float PlayRate = 1.0f;
		bool  Loop = true;

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;

		float GetDuration(AnimEvalContext&) const override;
		float GetNormalizedTime() const override { return m_Normalized; }
		void  Reset() override { m_Time = 0.0f; m_Normalized = 0.0f; }

		float GetTime() const { return m_Time; }

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

	private:
		float m_Time = 0.0f;
		float m_Normalized = 0.0f;
	};

	// ── Blend Space Player ────────────────────────────────────────────────────
	//
	// Locomoção inteira em UM nó. idle/walk/run como três estados dá "pop" no
	// meio de qualquer aceleração; aqui a transição é contínua.
	class AXE_API AnimNode_BlendSpacePlayer : public AnimNode
	{
	public:
		const char* TypeName() const override { return "BlendSpacePlayer"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_BlendSpacePlayer>(*this);
			CopyCommonTo(*c);
			return c;
		}

		// Amostras por NOME de clipe (índice quebraria ao reordenar o .axeskel).
		std::vector<std::pair<std::string, float>> Samples;
		// O parametro virou PINO. Construtor declara; o editor liga ou digita.
		AnimNode_BlendSpacePlayer() { AddFloatPin("Speed", 0.0f); }

		std::shared_ptr<BlendSpace1D> Space;   // construído no Resolve do asset

		float PlayRate = 1.0f;

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;

		float GetDuration(AnimEvalContext& ctx) const override;
		float GetNormalizedTime() const override { return m_Normalized; }
		void  Reset() override { m_Time = 0.0f; m_Normalized = 0.0f; }

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

	private:
		float m_Time = 0.0f;
		float m_Normalized = 0.0f;
	};

	// ── Blend by Float ────────────────────────────────────────────────────────
	class AXE_API AnimNode_BlendByFloat : public AnimNode
	{
	public:
		const char* TypeName() const override { return "BlendByFloat"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_BlendByFloat>(*this);
			CopyCommonTo(*c);
			return c;
		}
		int InputCount() const override { return 2; }
		const char* InputName(int i) const override { return i == 0 ? "A" : "B"; }

		AnimNode_BlendByFloat() { AddFloatPin("Alpha", 0.0f); }
		float MinValue = 0.0f;   // -> pose A
		float MaxValue = 1.0f;   // -> pose B

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;

	private:
		float m_Alpha = 0.0f;
	};

	// ── Blend by Bool ─────────────────────────────────────────────────────────
	//
	// Com BlendTime: a troca não é um corte seco. Um bool que faz a pose saltar
	// é o defeito mais comum em rig de arma/agachamento.
	class AXE_API AnimNode_BlendByBool : public AnimNode
	{
	public:
		const char* TypeName() const override { return "BlendByBool"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_BlendByBool>(*this);
			CopyCommonTo(*c);
			return c;
		}
		int InputCount() const override { return 2; }
		const char* InputName(int i) const override { return i == 0 ? "False" : "True"; }

		AnimNode_BlendByBool() { AddBoolPin("Ativo", false); }
		float BlendTime = 0.2f;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;
		void Reset() override { m_Alpha = 0.0f; }

	private:
		float m_Alpha = 0.0f;
	};

	// ── Layered Blend per Bone ────────────────────────────────────────────────
	//
	// Corre com as pernas E atira com os braços. A máscara é construída a partir
	// de um osso raiz (ex: "Spine2") — tudo dali pra baixo na hierarquia vem da
	// camada; o resto vem da base.
	class AXE_API AnimNode_LayeredBlend : public AnimNode
	{
	public:
		const char* TypeName() const override { return "LayeredBlend"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_LayeredBlend>(*this);
			CopyCommonTo(*c);
			return c;
		}
		int InputCount() const override { return 2; }
		const char* InputName(int i) const override { return i == 0 ? "Base" : "Layer"; }

		std::string RootBone = "Spine";

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		// Suaviza a fronteira ao longo de N ossos. Sem feather, a junção entre
		// as duas animações fica uma dobra rígida no meio das costas.
		int   FeatherBones = 2;

		// Alpha inline em 1.0: uma camada recem-criada aparece INTEIRA.
		// Se comecasse em 0, voce ligaria tudo certo e nao veria nada acontecer.
		AnimNode_LayeredBlend() { AddFloatPin("Alpha", 1.0f); }

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;
		void Reset() override { m_MaskBuilt = false; }

	private:
		void BuildMask(const Skeleton& skeleton);

		BoneMask m_Mask;
		bool m_MaskBuilt = false;
		std::string m_BuiltFor;   // detecta troca de RootBone em runtime
	};

	// ── Apply Additive ────────────────────────────────────────────────────────
	class AXE_API AnimNode_ApplyAdditive : public AnimNode
	{
	public:
		const char* TypeName() const override { return "ApplyAdditive"; }

		// Copia-ctor implicito basta: todos os campos deste no sao copiaveis.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_ApplyAdditive>(*this);
			CopyCommonTo(*c);
			return c;
		}
		int InputCount() const override { return 2; }
		const char* InputName(int i) const override { return i == 0 ? "Base" : "Additive"; }

		AnimNode_ApplyAdditive() { AddFloatPin("Alpha", 1.0f); }

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;
	};

	// ── State Machine ─────────────────────────────────────────────────────────
	//
	// A máquina de estados que construímos no Milestone 4, agora como UM NÓ.
	//
	// A diferença que muda tudo: cada estado contém um SUB-GRAFO de poses, não
	// um clipe. "Estado com blend space" deixa de ser caso especial — é só um
	// Blend Space Player dentro do sub-grafo. E o sub-grafo pode ter blends,
	// camadas, o que for.
	struct AXE_API AnimSmState
	{
		std::string   Name;
		AnimPoseGraph Graph;

		float EditorX = 0.0f;
		float EditorY = 0.0f;
	};

	class AXE_API AnimNode_StateMachine : public AnimNode
	{
	public:
		const char* TypeName() const override { return "StateMachine"; }

		// Aqui o copia-ctor implicito NAO serve: AnimSmState contem um
		// AnimPoseGraph, que e nao-copiavel de proposito (os nos sao
		// unique_ptr). Entao a copia e escrita a mao — e desce recursivamente
		// pelo sub-grafo de cada estado.
		std::unique_ptr<AnimNode> Clone() const override;

		std::vector<AnimSmState>    States;
		std::vector<AnimTransition> Transitions;
		int EntryState = 0;

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;

		float GetDuration(AnimEvalContext& ctx) const override;
		float GetNormalizedTime() const override { return m_NormalizedTime; }
		void  Reset() override;

		int  GetCurrentState() const { return m_Current; }
		bool IsTransitioning() const { return m_BlendDuration > 0.0f && m_BlendElapsed < m_BlendDuration; }

		// Autoria
		int  AddState(const std::string& name);
		void RemoveState(int index);
		void RemoveTransition(int index);

	private:
		int  SelectTransition(AnimEvalContext& ctx) const;
		void BeginTransition(const AnimTransition& tr, AnimEvalContext& ctx);

		int   m_Current = -1;
		int   m_Previous = -1;

		float m_NormalizedTime = 0.0f;
		float m_BlendElapsed = 0.0f;
		float m_BlendDuration = 0.0f;

		// Pose congelada de onde uma transição INTERROMPIDA partiu. Sem ela,
		// interromper um blend no meio daria um salto visível — no exato
		// instante em que o jogador apertou o botão.
		Pose m_Snapshot;
		bool m_UseSnapshot = false;
	};

	// ── Foot IK — REMOVIDO (AG1) ────────────────────────────────────────────
	//
	// O `AnimNode_FootIK` morava aqui. Saiu porque o Control Rig ja faz IK, e
	// melhor: o rig e um grafo que o usuario monta, enquanto este no era uma
	// solucao fechada em C++ com combos de osso no painel. Manter os dois
	// significava duas implementacoes de two-bone IK divergindo, e duas
	// respostas diferentes para "por que o pe nao encosta no chao".
	//
	// O tipo "FootIK" continua sendo RECONHECIDO na desserializacao — ver
	// CreateAnimNode em anim_nodes.cpp. Ele nao cria mais nada, mas e tratado
	// como tipo APOSENTADO em vez de desconhecido: a diferenca aparece no log
	// que o usuario le ao abrir um .axeanim antigo.

	// ── Control Rig ───────────────────────────────────────────────────────────
	//
	// Roda um asset .axerig DENTRO do AnimGraph: entra uma pose, o Forwards
	// Solve do rig processa os ossos, sai a pose processada. É EXATAMENTE o
	// solve que o preview do editor de rig já rodava — a única diferença é o
	// ponto de partida:
	//
	//   - o PREVIEW parte do REPOUSO (ResetToInitial) pra você ver só o efeito
	//     isolado do grafo;
	//   - AQUI parte da pose da ANIMAÇÃO que chega no pino, então o rig trabalha
	//     POR CIMA do movimento (Foot IK, look-at, correções procedurais).
	//
	// Sem este nó o Control Rig só existia no preview do editor — não havia como
	// um personagem na cena usar o rig. Este é o ponto que fecha o ciclo.
	//
	// ── UMA CÓPIA DE TRABALHO POR PERSONAGEM ─────────────────────────────
	//
	// A hierarquia e o grafo do rig carregam ESTADO de runtime (o Current de
	// cada elemento, o cache de dados do grafo). Dois personagens dividindo a
	// mesma cópia deformariam um ao outro. Por isso o nó guarda o ASSET (o
	// molde, compartilhado entre as instâncias) e CLONA a própria cópia de
	// trabalho — re-clonando quando o asset muda de versão. É o mesmo mecanismo
	// que o próprio AnimGraph já usa pra se re-clonar ao salvar.
	class AXE_API AnimNode_ControlRig : public AnimNode
	{
	public:
		const char* TypeName() const override { return "ControlRig"; }

		// Clone MANUAL, e de propósito: NÃO copia a cópia de trabalho
		// (m_Hierarchy/m_Graph). Cada instância re-clona do asset no primeiro
		// Evaluate — se copiasse, dois personagens herdariam o mesmo Current e
		// o mesmo cache de dados, e um mexeria no outro. Só o molde (RigAsset)
		// e a autoria (RigUUID) atravessam o clone.
		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_ControlRig>();
			CopyCommonTo(*c);
			c->RigUUID = RigUUID;
			c->RigAsset = RigAsset;   // compartilha o molde; a cópia de trabalho nasce depois
			return c;
		}

		int InputCount() const override { return 1; }
		const char* InputName(int) const override { return "Pose"; }

		// Qual .axerig. Referência por UUID (o caminho muda quando o arquivo se
		// move; o UUID não). Religado a um ControlRigAsset real no Resolve do
		// .axeanim, exatamente como os clipes.
		std::string RigUUID;

		// O molde, resolvido a partir do UUID. NÃO vai pro disco (só o UUID vai)
		// e é COMPARTILHADO entre as instâncias — cada uma clona a própria cópia
		// de trabalho a partir dele.
		std::shared_ptr<ControlRigAsset> RigAsset;

		// Alpha inline 1.0: um Control Rig recém-criado age INTEIRO. Começar em
		// 0 faria você ligar tudo certo e não ver efeito nenhum — a mesma
		// armadilha do Foot IK.
		AnimNode_ControlRig() { AddFloatPin("Alpha", 1.0f); }

		// Carrega o RigAsset a partir do RigUUID via AssetDatabase. Chamado pelo
		// Resolve do AnimGraphAsset. Fora dele (UUID vazio, asset ausente) o nó
		// vira passagem — a pose passa intacta e um log explica.
		//
		// Recebe o esqueleto pra poder relatar a COBERTURA: quais ossos do
		// esqueleto o rig nao tem. Sem esse relatorio, um osso de fora do rig
		// e invisivel na autoria e o efeito dele so aparece como "a animacao
		// desse pedaco parou de funcionar".
		void ResolveRig(const Skeleton* skel, const char* graphName);

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;

		// Força re-clone no próximo Evaluate (entrar num estado, dar Stop).
		void Reset() override { m_Cloned = false; }

	private:
		// Garante que a cópia de trabalho existe e está na versão do asset.
		void EnsureWorkingCopy();

		// Cópia de trabalho POR INSTÂNCIA. Não é serializada nem clonada —
		// nasce do molde no primeiro Evaluate.
		RigHierarchy m_Hierarchy;
		RigGraph     m_Graph;

		// As funcoes tambem sao copia de trabalho. Um no Call resolve por nome
		// AQUI DENTRO, e nao no asset: se dois personagens dividissem os grafos
		// de funcao, o cache de solve de um invalidaria o do outro — o mesmo
		// motivo pelo qual a hierarquia e o grafo principal ja sao clonados.
		std::vector<std::pair<std::string, RigGraph>> m_Functions;
		bool         m_Cloned = false;
		uint32_t     m_ClonedVersion = 0;

		// dt do último Update. O contexto do Evaluate vem com DeltaTime = 0 (só
		// o Update avança tempo), mas um solve pode ter nós que dependem de dt.
		// Mesma lição do Foot IK: o Update SEMPRE roda antes do Evaluate no
		// mesmo frame.
		float m_LastDt = 0.0f;

		// Pose de trabalho reusada (o resultado do solve antes do blend por
		// Alpha). Membro pra não realocar por frame.
		Pose m_Solved;
	};

	// Fábrica por nome de tipo — usada pelo carregador do .axeanim.
	AXE_API std::unique_ptr<AnimNode> CreateAnimNode(const std::string& typeName);

} // namespace axe