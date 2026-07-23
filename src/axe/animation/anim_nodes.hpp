#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/anim_node.hpp"
#include "axe/animation/anim_pose_graph.hpp"
#include "axe/animation/anim_graph.hpp"      // AnimTransition, AnimCondition
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/blend_space_1d.hpp"
#include "axe/animation/bone_mask.hpp"

#include <memory>
#include <string>
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

	// ── Foot IK ─────────────────────────────────────────────────────────────
	//
	// Cola os pés no chão. A animação de idle/walk foi feita pra chão plano;
	// numa rampa ou degrau os pés atravessam ou flutuam. Este nó, pra cada
	// perna, faz um raycast pra baixo a partir do pé, e:
	//
	//   - se o chão está ACIMA de onde a animação pôs o pé (rampa subindo),
	//     levanta o alvo do pé até lá e dobra o joelho pra alcançar
	//     (Two-Bone IK: coxa -> canela -> pé);
	//   - alinha o pé à NORMAL do chão (o pé acompanha a inclinação);
	//   - abaixa o QUADRIL o suficiente pra que a perna mais esticada ainda
	//     alcance — senão uma perna estica reto e a outra fica no ar.
	//
	// É um nó como qualquer outro: entra uma pose, sai uma pose. A diferença
	// é que ele consulta o mundo (PhysicsSystem::Get().Raycast) e por isso
	// precisa da WorldTransform do contexto — o único nó que sai do espaço
	// local.
	//
	// Genérico em N pernas (não fixo em esq/dir): um quadrúpede é só quatro
	// pernas na lista. Cada perna são três ossos nomeados, casados por nome
	// com o esqueleto — sem hardcode de "mixamorig:LeftFoot".
	class AXE_API AnimNode_FootIK : public AnimNode
	{
	public:
		const char* TypeName() const override { return "FootIK"; }

		std::unique_ptr<AnimNode> Clone() const override
		{
			auto c = std::make_unique<AnimNode_FootIK>(*this);
			CopyCommonTo(*c);
			return c;
		}

		int InputCount() const override { return 1; }
		const char* InputName(int) const override { return "Pose"; }

		// Uma perna: os três ossos da cadeia IK, do quadril ao pé.
		//
		//   Upper = coxa (a raiz que roda)   ex: LeftUpLeg
		//   Lower = canela (o joelho)        ex: LeftLeg
		//   Foot  = pé (recebe o alinhamento) ex: LeftFoot
		struct Leg
		{
			std::string Upper;
			std::string Lower;
			std::string Foot;
		};

		std::vector<Leg> Legs;

		// Quanto o pé pode subir/descer do plano da animação, em metros. Passou
		// disso, o nó desiste daquele pé (buraco fundo, parede) em vez de
		// esticar a perna de forma grotesca.
		float MaxReach = 0.5f;

		// Trim vertical fino, em metros, somado ao deslocamento de TODO pe.
		// 0 = a sola encosta exatamente onde a animacao a poe. Positivo levanta
		// (util se a malha do pe crava um pouco no chao).
		float FootHeight = 0.0f;

		// Suavizacao temporal (1/s). O alvo do pe e a normal do chao sao
		// perseguidos exponencialmente em vez de saltarem: raycast que muda de
		// superficie entre um frame e outro deixa de dar POP.
		float Smoothing = 12.0f;

		// Abaixar o quadril quando um pé não alcança (pelvis dip). Sem isso, o
		// pé baixo estica a perna reto e o corpo parece "puxado" pra cima.
		bool  DipPelvis = true;

		// Osso do quadril/raiz que desce no pelvis dip. Vazio = usa o pai comum
		// das pernas, resolvido no primeiro Evaluate.
		std::string PelvisBone;

		// Alpha inline 1.0: um Foot IK recém-criado age INTEIRO. Se começasse
		// em 0, você ligaria tudo certo e não veria efeito nenhum.
		AnimNode_FootIK()
		{
			AddFloatPin("Alpha", 1.0f);

			// Bípede por padrão, com os nomes da Mixamo (sem o prefixo
			// "mixamorig:", que o loader do AXE já remove). Casa direto com o
			// Y Bot; qualquer outro rig, troca nos combos do painel.
			Legs.push_back({ "LeftUpLeg",  "LeftLeg",  "LeftFoot" });
			Legs.push_back({ "RightUpLeg", "RightLeg", "RightFoot" });
		}

		void Serialize(nlohmann::json& j) const override;
		void Deserialize(const nlohmann::json& j) override;

		void Update(AnimEvalContext& ctx) override;
		void Evaluate(AnimEvalContext& ctx, Pose& out) override;
		void Reset() override;

	private:
		// Resolve os índices de osso uma vez (casamento por nome é caro pra
		// fazer por frame). Invalida quando a lista de pernas muda no editor.
		struct ResolvedLeg { int Upper = -1, Lower = -1, Foot = -1; };

		// Estado SUAVIZADO por perna. Sem ele o IK reage instantaneamente a
		// cada raycast, e qualquer variacao de superficie vira tremor.
		struct LegState
		{
			float     Offset = 0.0f;              // deslocamento vertical do pe
			glm::vec3 Normal{ 0.0f, 1.0f, 0.0f }; // normal do chao
			float     Weight = 0.0f;              // 0 = pe no ar, 1 = pe no chao
			bool      Init = false;
		};

		void ResolveBones(const Skeleton& skel);

		std::vector<ResolvedLeg> m_Resolved;
		std::vector<LegState>    m_State;

		float m_PelvisOffset = 0.0f;

		// DeltaTime do ultimo Update. O contexto do Evaluate vem com
		// DeltaTime = 0 (so o Update avanca tempo), mas a suavizacao precisa
		// de dt — e o Update SEMPRE roda antes do Evaluate no mesmo frame.
		float m_LastDt = 0.0f;
		int  m_PelvisIdx = -1;
		bool m_BonesResolved = false;

		// Assinatura da última resolução — detecta troca de osso no editor sem
		// precisar de um Reset() explícito.
		std::size_t m_ResolvedHash = 0;
	};

	// Fábrica por nome de tipo — usada pelo carregador do .axeanim.
	AXE_API std::unique_ptr<AnimNode> CreateAnimNode(const std::string& typeName);

} // namespace axe