#include "anim_nodes.hpp"
#include <nlohmann/json.hpp>
#include "animation_sampler.hpp"
#include "axe/log/log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace axe
{
	// ═══ Clip Player ═════════════════════════════════════════════════════════

	void AnimNode_ClipPlayer::Update(AnimEvalContext& ctx)
	{
		if (!Clip)
			return;

		// Antes de avancar: m_Time SEMPRE sai deste Update dentro de
		// [0, dur], entao o valor de entrada ja e o "tempo de clipe"
		// anterior — a referencia pro cruzamento de notifies.
		const float prevW = m_Time;

		if (ctx.AdvanceTime)
			m_Time += ctx.DeltaTime * PlayRate;

		const float dur = Clip->GetDuration();

		if (dur > 1e-6f)
		{
			if (Loop)
			{
				// Wrap manual em vez de deixar o tempo crescer pra sempre: um
				// float em 60fps por uma hora ja perde precisao suficiente pra
				// a animacao tremer visivelmente.
				m_Time = std::fmod(m_Time, dur);
				if (m_Time < 0.0f) m_Time += dur;
			}
			else
			{
				m_Time = glm::clamp(m_Time, 0.0f, dur);
			}

			m_Normalized = m_Time / dur;

			// ── Notifies cruzados neste passo ────────────────────────────
			if (ctx.NotifySink && ctx.AdvanceTime && !Clip->Notifies.empty())
			{
				const float nowW = m_Time;

				for (const auto& n : Clip->Notifies)
				{
					const bool hit = (nowW >= prevW)
						? (n.Time > prevW && n.Time <= nowW)
						: (n.Time > prevW || n.Time <= nowW);   // deu a volta

					if (hit)
						ctx.NotifySink->push_back(n);
				}
			}
		}
		else
		{
			m_Normalized = 1.0f;
		}
	}

	void AnimNode_ClipPlayer::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		if (!Clip || !ctx.Skel)
		{
			if (ctx.Skel) Pose::FromBindPose(*ctx.Skel, out);
			return;
		}

		AnimationSampler::SamplePose(*ctx.Skel, *Clip, m_Time, out);
	}

	float AnimNode_ClipPlayer::GetDuration(AnimEvalContext&) const
	{
		return Clip ? Clip->GetDuration() : 0.0f;
	}

	// ═══ Blend Space Player ══════════════════════════════════════════════════

	void AnimNode_BlendSpacePlayer::Update(AnimEvalContext& ctx)
	{
		if (!Space || Space->IsEmpty())
			return;

		const float dur = Space->GetDurationAt(ReadFloat(ctx, 0));

		if (ctx.AdvanceTime)
			m_Time += ctx.DeltaTime * PlayRate;

		if (dur > 1e-6f)
		{
			m_Time = std::fmod(m_Time, dur);
			if (m_Time < 0.0f) m_Time += dur;

			m_Normalized = m_Time / dur;
		}
	}

	void AnimNode_BlendSpacePlayer::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		if (!Space || Space->IsEmpty() || !ctx.Skel)
		{
			if (ctx.Skel) Pose::FromBindPose(*ctx.Skel, out);
			return;
		}

		// O BlendSpace1D sincroniza por FASE, nao por tempo absoluto — e o que
		// impede o pe de escorregar quando walk e run tem duracoes diferentes.
		Space->Evaluate(*ctx.Skel, ReadFloat(ctx, 0), m_Time, out);
	}

	float AnimNode_BlendSpacePlayer::GetDuration(AnimEvalContext& ctx) const
	{
		if (!Space || Space->IsEmpty())
			return 0.0f;

		// A duracao VARIA com o parametro: quanto mais rapido o personagem,
		// mais curto o ciclo da passada. Assim o ExitTime de uma transicao
		// saindo daqui acompanha a velocidade sozinho.
		return Space->GetDurationAt(ReadFloat(ctx, 0));
	}

	// ═══ Blend by Float ══════════════════════════════════════════════════════

	void AnimNode_BlendByFloat::Update(AnimEvalContext& ctx)
	{
		const float v = ReadFloat(ctx, 0);
		const float range = MaxValue - MinValue;

		m_Alpha = (std::fabs(range) > 1e-6f)
			? glm::clamp((v - MinValue) / range, 0.0f, 1.0f)
			: 0.0f;

		// Os DOIS lados sao atualizados, mesmo com alpha 0 ou 1.
		//
		// Se o lado "escondido" nao avancasse o tempo, ele ficaria congelado —
		// e no instante em que o alpha comecasse a subir, a animacao saltaria do
		// frame onde parou. O custo de manter os dois rodando e baixo; o
		// artefato visual e enorme.
		UpdateInput(ctx, 0);
		UpdateInput(ctx, 1);
	}

	void AnimNode_BlendByFloat::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		if (!ctx.Skel || !ctx.Pool)
			return;

		// Atalhos nos extremos: nao paga o custo de amostrar e blendar duas
		// poses quando uma delas tem peso zero.
		if (m_Alpha <= 0.001f) { EvalInput(ctx, 0, out); return; }
		if (m_Alpha >= 0.999f) { EvalInput(ctx, 1, out); return; }

		Pose& a = ctx.Pool->Acquire(*ctx.Skel);
		Pose& b = ctx.Pool->Acquire(*ctx.Skel);

		EvalInput(ctx, 0, a);
		EvalInput(ctx, 1, b);

		Pose::Blend(a, b, m_Alpha, out);
	}

	// ═══ Blend by Bool ═══════════════════════════════════════════════════════

	void AnimNode_BlendByBool::Update(AnimEvalContext& ctx)
	{
		const float target = ReadBool(ctx, 0) ? 1.0f : 0.0f;

		if (BlendTime <= 1e-4f || !ctx.AdvanceTime)
		{
			m_Alpha = target;
		}
		else
		{
			const float step = ctx.DeltaTime / BlendTime;
			m_Alpha = (target > m_Alpha)
				? glm::min(m_Alpha + step, 1.0f)
				: glm::max(m_Alpha - step, 0.0f);
		}

		UpdateInput(ctx, 0);
		UpdateInput(ctx, 1);
	}

	void AnimNode_BlendByBool::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		if (!ctx.Skel || !ctx.Pool)
			return;

		if (m_Alpha <= 0.001f) { EvalInput(ctx, 0, out); return; }
		if (m_Alpha >= 0.999f) { EvalInput(ctx, 1, out); return; }

		Pose& a = ctx.Pool->Acquire(*ctx.Skel);
		Pose& b = ctx.Pool->Acquire(*ctx.Skel);

		EvalInput(ctx, 0, a);
		EvalInput(ctx, 1, b);

		// Smoothstep: blend linear tem derivada descontinua nas pontas, e o
		// olho le isso como um tranco no inicio e no fim.
		const float s = m_Alpha * m_Alpha * (3.0f - 2.0f * m_Alpha);
		Pose::Blend(a, b, s, out);
	}

	// ═══ Layered Blend per Bone ══════════════════════════════════════════════

	void AnimNode_LayeredBlend::BuildMask(const Skeleton& skeleton)
	{
		m_Mask.Reset(skeleton, 0.0f);

		// SetBranch pega o osso E TODOS os descendentes na hierarquia. E por
		// isso que basta dizer "Spine2": bracos, maos e dedos vao junto, sem
		// listar osso por osso.
		if (!m_Mask.SetBranch(skeleton, RootBone, 1.0f))
		{
			AXE_CORE_WARN("LayeredBlend: osso '{}' nao existe no esqueleto — "
				"a camada nao vai aparecer.", RootBone);
		}

		if (FeatherBones > 0)
			m_Mask.Feather(skeleton, RootBone, FeatherBones);

		m_MaskBuilt = true;
		m_BuiltFor = RootBone;
	}

	void AnimNode_LayeredBlend::Update(AnimEvalContext& ctx)
	{
		UpdateInput(ctx, 0);
		UpdateInput(ctx, 1);
	}

	void AnimNode_LayeredBlend::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		if (!ctx.Skel || !ctx.Pool)
			return;

		// Construida uma vez (e de novo se o usuario trocar o osso no editor).
		// Reconstruir por frame percorreria a hierarquia inteira a cada frame,
		// por personagem — desperdicio puro.
		if (!m_MaskBuilt || m_BuiltFor != RootBone)
			BuildMask(*ctx.Skel);

		// Alpha vem do pino 0: link se houver, senao o valor inline do proprio no.
		const float alpha = glm::clamp(ReadFloat(ctx, 0), 0.0f, 1.0f);

		if (alpha <= 0.001f) { EvalInput(ctx, 0, out); return; }

		Pose& base = ctx.Pool->Acquire(*ctx.Skel);
		Pose& layer = ctx.Pool->Acquire(*ctx.Skel);

		EvalInput(ctx, 0, base);
		EvalInput(ctx, 1, layer);

		Pose::BlendMasked(base, layer, m_Mask, alpha, out);
	}

	// ═══ Apply Additive ══════════════════════════════════════════════════════

	void AnimNode_ApplyAdditive::Update(AnimEvalContext& ctx)
	{
		UpdateInput(ctx, 0);
		UpdateInput(ctx, 1);
	}

	void AnimNode_ApplyAdditive::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		if (!ctx.Skel || !ctx.Pool)
			return;

		// Alpha vem do pino 0: link se houver, senao o valor inline do proprio no.
		const float alpha = glm::clamp(ReadFloat(ctx, 0), 0.0f, 1.0f);

		if (alpha <= 0.001f) { EvalInput(ctx, 0, out); return; }

		Pose& base = ctx.Pool->Acquire(*ctx.Skel);
		Pose& add = ctx.Pool->Acquire(*ctx.Skel);

		EvalInput(ctx, 0, base);
		EvalInput(ctx, 1, add);

		Pose::ApplyAdditive(base, add, alpha, out);
	}

	// ═══ State Machine ═══════════════════════════════════════════════════════

	int AnimNode_StateMachine::AddState(const std::string& name)
	{
		AnimSmState st;
		st.Name = name;

		// Todo estado nasce com um sub-grafo VALIDO: Output ligado a um Clip
		// Player vazio. Um estado sem Output renderizaria bind pose e o usuario
		// nao entenderia por que — e "crie o Output voce mesmo" e uma cerimonia
		// que ninguem deveria pagar.
		auto clip = std::make_unique<AnimNode_ClipPlayer>();
		clip->Title = "Clip";
		clip->EditorX = 60.0f;
		clip->EditorY = 120.0f;

		auto outNode = std::make_unique<AnimNode_Output>();
		outNode->Title = "Output Pose";
		outNode->EditorX = 340.0f;
		outNode->EditorY = 120.0f;

		const int clipId = st.Graph.AddNode(std::move(clip));
		const int outId = st.Graph.AddNode(std::move(outNode));

		st.Graph.SetOutputNode(outId);
		st.Graph.AddLink(clipId, outId, 0);

		States.push_back(std::move(st));
		return (int)States.size() - 1;
	}

	void AnimNode_StateMachine::RemoveState(int index)
	{
		if (index < 0 || index >= (int)States.size())
			return;

		// Remove as transicoes que TOCAM o estado, e REINDEXA as sobreviventes.
		//
		// Sem a reindexacao, apagar o estado 2 faria toda transicao que
		// apontava pro 3 passar a apontar pro estado errado — em silencio. E o
		// tipo de bug que so aparece semanas depois, num grafo grande.
		std::vector<AnimTransition> kept;

		for (const auto& tr : Transitions)
		{
			if (tr.From == index || tr.To == index)
				continue;

			AnimTransition t = tr;
			if (t.From > index) --t.From;
			if (t.To > index) --t.To;

			kept.push_back(t);
		}

		Transitions = std::move(kept);

		States.erase(States.begin() + index);

		if (EntryState == index)      EntryState = States.empty() ? 0 : 0;
		else if (EntryState > index)  --EntryState;

		Reset();
	}

	void AnimNode_StateMachine::RemoveTransition(int index)
	{
		if (index >= 0 && index < (int)Transitions.size())
			Transitions.erase(Transitions.begin() + index);
	}

	void AnimNode_StateMachine::Reset()
	{
		m_Current = States.empty() ? -1 : glm::clamp(EntryState, 0, (int)States.size() - 1);
		m_Previous = -1;

		m_NormalizedTime = 0.0f;
		m_BlendElapsed = 0.0f;
		m_BlendDuration = 0.0f;
		m_UseSnapshot = false;

		for (auto& s : States)
			s.Graph.Reset();
	}

	float AnimNode_StateMachine::GetDuration(AnimEvalContext& ctx) const
	{
		if (m_Current < 0 || m_Current >= (int)States.size())
			return 0.0f;

		// A duracao do ESTADO e a do no que alimenta o Output do sub-grafo.
		const AnimPoseGraph& g = States[m_Current].Graph;

		if (const AnimNode* out = g.FindNode(g.GetOutputNode()))
			if (!out->Inputs.empty() && out->Inputs[0])
				return out->Inputs[0]->GetDuration(ctx);

		return 0.0f;
	}

	int AnimNode_StateMachine::SelectTransition(AnimEvalContext& ctx) const
	{
		if (m_Current < 0 || !ctx.Params)
			return -1;

		int best = -1;
		int bestPriority = 0;

		for (std::size_t i = 0; i < Transitions.size(); ++i)
		{
			const auto& tr = Transitions[i];

			const bool applies = (tr.From == m_Current) || (tr.From == -1);
			if (!applies)
				continue;

			// Any-State apontando pro estado atual: por padrao NAO redispara.
			// Sem esta guarda, "levar dano" reiniciaria a cada frame enquanto o
			// trigger estivesse armado, e o personagem travaria no frame 0.
			if (tr.To == m_Current && !tr.CanRetriggerSelf)
				continue;

			// ExitTime: so pode sair depois de X% do ciclo. E o que impede um
			// ataque de ser cortado no primeiro frame.
			//
			// Any-State ignora exit time de proposito: morte e dano precisam
			// interromper na hora.
			if (tr.HasExitTime && tr.From != -1 && m_NormalizedTime < tr.ExitTime)
				continue;

			if (!tr.ConditionsMet(*ctx.Params))
				continue;

			if (best < 0 || tr.Priority > bestPriority)
			{
				best = (int)i;
				bestPriority = tr.Priority;
			}
		}

		return best;
	}

	void AnimNode_StateMachine::BeginTransition(const AnimTransition& tr, AnimEvalContext& ctx)
	{
		// Consome os triggers DESTA transicao — e so agora, depois que ela
		// venceu. Consumir durante a avaliacao faria a primeira transicao a
		// olhar roubar o pulso de outra com prioridade maior.
		if (ctx.Params)
			for (const auto& c : tr.Conditions)
				if (c.Op == AnimCompare::TriggerSet)
					ctx.Params->ConsumeTrigger(c.Parameter);

		const int target = tr.To;

		if (tr.Duration <= 0.0f)
		{
			m_Current = target;
			m_Previous = -1;
			m_BlendDuration = 0.0f;
			m_BlendElapsed = 0.0f;
			m_UseSnapshot = false;
			m_NormalizedTime = 0.0f;

			if (target >= 0 && target < (int)States.size())
				States[target].Graph.Reset();

			return;
		}

		// TRANSICAO INTERROMPIDA.
		//
		// Se ja estavamos blendando A->B e agora vamos pra C, a origem do novo
		// blend NAO e B — e a pose misturada que esta na tela AGORA. Usar B daria
		// um salto visivel no exato instante da troca.
		if (IsTransitioning() && !m_Snapshot.IsEmpty())
		{
			m_UseSnapshot = true;
			m_Previous = -1;
		}
		else
		{
			m_Previous = m_Current;
			m_UseSnapshot = false;
		}

		m_Current = target;
		m_NormalizedTime = 0.0f;

		m_BlendDuration = tr.Duration;
		m_BlendElapsed = 0.0f;

		if (target >= 0 && target < (int)States.size())
			States[target].Graph.Reset();
	}

	void AnimNode_StateMachine::Update(AnimEvalContext& ctx)
	{
		if (States.empty())
			return;

		if (m_Current < 0)
			Reset();

		// ── 1. tempo do estado atual ─────────────────────────────────────────
		States[m_Current].Graph.Update(ctx);

		const float dur = GetDuration(ctx);
		if (const AnimNode* n = States[m_Current].Graph.FindNode(States[m_Current].Graph.GetOutputNode()))
			if (!n->Inputs.empty() && n->Inputs[0])
				m_NormalizedTime = n->Inputs[0]->GetNormalizedTime();

		(void)dur;

		// ── 2. o estado de SAIDA continua correndo ───────────────────────────
		//
		// Se ele congelasse, o personagem pararia de andar no meio do crossfade
		// e o pe escorregaria no chao. Bem visivel.
		if (m_Previous >= 0 && m_Previous < (int)States.size())
			States[m_Previous].Graph.Update(ctx);

		// ── 3. decidir transicao ─────────────────────────────────────────────
		//
		// DEPOIS de avancar o tempo, porque o ExitTime depende do tempo
		// normalizado DESTE frame. Decidindo antes, um estado com ExitTime = 1.0
		// nunca chegaria la — ficaria sempre um frame aquem, e a transicao jamais
		// dispararia.
		const int chosen = SelectTransition(ctx);
		if (chosen >= 0)
			BeginTransition(Transitions[chosen], ctx);

		// ── 4. avancar o blend ───────────────────────────────────────────────
		if (IsTransitioning() && ctx.AdvanceTime)
			m_BlendElapsed += std::fabs(ctx.DeltaTime);

		if (m_BlendDuration > 0.0f && m_BlendElapsed >= m_BlendDuration)
		{
			m_Previous = -1;
			m_BlendDuration = 0.0f;
			m_UseSnapshot = false;
		}
	}

	void AnimNode_StateMachine::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		if (States.empty() || m_Current < 0 || !ctx.Skel || !ctx.Pool)
		{
			if (ctx.Skel) Pose::FromBindPose(*ctx.Skel, out);
			return;
		}

		if (!IsTransitioning())
		{
			States[m_Current].Graph.Evaluate(ctx, out);

			// Guarda a pose pra o caso de uma transicao ser interrompida mais
			// tarde a partir daqui.
			m_Snapshot = out;
			return;
		}

		Pose& cur = ctx.Pool->Acquire(*ctx.Skel);
		Pose& prev = ctx.Pool->Acquire(*ctx.Skel);

		States[m_Current].Graph.Evaluate(ctx, cur);

		if (m_UseSnapshot && !m_Snapshot.IsEmpty())
			prev = m_Snapshot;                                   // origem congelada
		else if (m_Previous >= 0 && m_Previous < (int)States.size())
			States[m_Previous].Graph.Evaluate(ctx, prev);
		else
			Pose::FromBindPose(*ctx.Skel, prev);

		const float raw = glm::clamp(m_BlendElapsed / m_BlendDuration, 0.0f, 1.0f);
		const float alpha = raw * raw * (3.0f - 2.0f * raw);   // smoothstep

		Pose::Blend(prev, cur, alpha, out);

		m_Snapshot = out;
	}

	// ═══ Fábrica ═════════════════════════════════════════════════════════════

	std::unique_ptr<AnimNode> CreateAnimNode(const std::string& t)
	{
		if (t == "Output")           return std::make_unique<AnimNode_Output>();

		// Nos de variavel
		if (t == "GetFloat")         return std::make_unique<AnimNode_GetFloat>();
		if (t == "GetBool")          return std::make_unique<AnimNode_GetBool>();

		if (t == "ClipPlayer")       return std::make_unique<AnimNode_ClipPlayer>();
		if (t == "BlendSpacePlayer") return std::make_unique<AnimNode_BlendSpacePlayer>();
		if (t == "BlendByFloat")     return std::make_unique<AnimNode_BlendByFloat>();
		if (t == "BlendByBool")      return std::make_unique<AnimNode_BlendByBool>();
		if (t == "LayeredBlend")     return std::make_unique<AnimNode_LayeredBlend>();
		if (t == "ApplyAdditive")    return std::make_unique<AnimNode_ApplyAdditive>();
		if (t == "StateMachine")     return std::make_unique<AnimNode_StateMachine>();

		AXE_CORE_ERROR("CreateAnimNode: tipo desconhecido '{}'.", t);
		return nullptr;
	}


	// ═════════════════════════════════════════════════════════════════════════
	//  SERIALIZAÇÃO
	//
	//  Cada nó grava só o que é DELE. Id, tipo, título, posição e os valores
	//  inline dos pinos de dado são gravados pelo AnimPoseGraph — se cada nó
	//  repetisse esse bloco, um deles esqueceria um campo.
	// ═════════════════════════════════════════════════════════════════════════

	void AnimNode_GetFloat::Serialize(nlohmann::json& j) const { j["param"] = Parameter; }
	void AnimNode_GetFloat::Deserialize(const nlohmann::json& j) { Parameter = j.value("param", std::string("Speed")); }

	void AnimNode_GetBool::Serialize(nlohmann::json& j) const { j["param"] = Parameter; }
	void AnimNode_GetBool::Deserialize(const nlohmann::json& j) { Parameter = j.value("param", std::string("IsGrounded")); }

	void AnimNode_ClipPlayer::Serialize(nlohmann::json& j) const
	{
		// O ponteiro do clipe NÃO vai pro disco — só o NOME. É o Resolve() do
		// asset que religa, contra o esqueleto do personagem.
		j["clip"] = ClipName;
		j["rate"] = PlayRate;
		j["loop"] = Loop;
	}

	void AnimNode_ClipPlayer::Deserialize(const nlohmann::json& j)
	{
		ClipName = j.value("clip", std::string{});
		PlayRate = j.value("rate", 1.0f);
		Loop = j.value("loop", true);
	}

	void AnimNode_BlendSpacePlayer::Serialize(nlohmann::json& j) const
	{
		j["rate"] = PlayRate;
		j["samples"] = nlohmann::json::array();

		for (const auto& sample : Samples)
		{
			nlohmann::json js;
			js["clip"] = sample.first;    // por NOME, nunca índice
			js["value"] = sample.second;
			j["samples"].push_back(js);
		}
	}

	void AnimNode_BlendSpacePlayer::Deserialize(const nlohmann::json& j)
	{
		PlayRate = j.value("rate", 1.0f);
		Samples.clear();

		if (!j.contains("samples"))
			return;

		for (const auto& js : j["samples"])
			Samples.emplace_back(js.value("clip", std::string{}), js.value("value", 0.0f));
	}

	void AnimNode_BlendByFloat::Serialize(nlohmann::json& j) const
	{
		j["min"] = MinValue;
		j["max"] = MaxValue;
	}

	void AnimNode_BlendByFloat::Deserialize(const nlohmann::json& j)
	{
		MinValue = j.value("min", 0.0f);
		MaxValue = j.value("max", 1.0f);
	}

	void AnimNode_BlendByBool::Serialize(nlohmann::json& j) const { j["blend_time"] = BlendTime; }
	void AnimNode_BlendByBool::Deserialize(const nlohmann::json& j) { BlendTime = j.value("blend_time", 0.2f); }

	void AnimNode_LayeredBlend::Serialize(nlohmann::json& j) const
	{
		j["root_bone"] = RootBone;
		j["feather"] = FeatherBones;
	}

	void AnimNode_LayeredBlend::Deserialize(const nlohmann::json& j)
	{
		RootBone = j.value("root_bone", std::string("Spine"));
		FeatherBones = j.value("feather", 2);
	}

	// ── State Machine ─────────────────────────────────────────────────────────
	//
	// Aqui a recursão aparece: cada estado contém um AnimPoseGraph inteiro, e
	// AnimPoseGraph::ToJson chama de volta este Serialize se houver outra
	// máquina de estados aninhada. Profundidade arbitrária, zero código
	// especial por nível.
	void AnimNode_StateMachine::Serialize(nlohmann::json& j) const
	{
		j["entry"] = EntryState;

		j["states"] = nlohmann::json::array();

		for (const auto& st : States)
		{
			nlohmann::json js;
			js["name"] = st.Name;
			js["x"] = st.EditorX;
			js["y"] = st.EditorY;

			nlohmann::json jg;
			st.Graph.ToJson(jg);          // <- recursão
			js["graph"] = jg;

			j["states"].push_back(js);
		}

		j["transitions"] = nlohmann::json::array();

		for (const auto& tr : Transitions)
		{
			nlohmann::json jt;
			jt["from"] = tr.From;          // -1 = Any State
			jt["to"] = tr.To;
			jt["duration"] = tr.Duration;
			jt["has_exit"] = tr.HasExitTime;
			jt["exit_time"] = tr.ExitTime;
			jt["priority"] = tr.Priority;
			jt["retrigger"] = tr.CanRetriggerSelf;

			jt["conditions"] = nlohmann::json::array();

			for (const auto& c : tr.Conditions)
			{
				nlohmann::json jc;
				jc["param"] = c.Parameter;
				jc["op"] = (int)c.Op;
				jc["value"] = c.Value;
				jt["conditions"].push_back(jc);
			}

			j["transitions"].push_back(jt);
		}
	}

	void AnimNode_StateMachine::Deserialize(const nlohmann::json& j)
	{
		EntryState = j.value("entry", 0);

		States.clear();
		Transitions.clear();

		if (j.contains("states"))
		{
			for (const auto& js : j["states"])
			{
				AnimSmState st;
				st.Name = js.value("name", std::string("Estado"));
				st.EditorX = js.value("x", 0.0f);
				st.EditorY = js.value("y", 0.0f);

				if (js.contains("graph"))
					st.Graph.FromJson(js["graph"]);   // <- recursão

				States.push_back(std::move(st));
			}
		}

		if (j.contains("transitions"))
		{
			for (const auto& jt : j["transitions"])
			{
				AnimTransition tr;
				tr.From = jt.value("from", -1);
				tr.To = jt.value("to", -1);
				tr.Duration = jt.value("duration", 0.2f);
				tr.HasExitTime = jt.value("has_exit", false);
				tr.ExitTime = jt.value("exit_time", 1.0f);
				tr.Priority = jt.value("priority", 0);
				tr.CanRetriggerSelf = jt.value("retrigger", false);

				if (jt.contains("conditions"))
				{
					for (const auto& jc : jt["conditions"])
					{
						AnimCondition c;
						c.Parameter = jc.value("param", std::string{});
						c.Op = (AnimCompare)jc.value("op", 0);
						c.Value = jc.value("value", 0.0f);
						tr.Conditions.push_back(c);
					}
				}

				Transitions.push_back(tr);
			}
		}
	}


	std::unique_ptr<AnimNode> AnimNode_StateMachine::Clone() const
	{
		auto c = std::make_unique<AnimNode_StateMachine>();

		CopyCommonTo(*c);

		c->EntryState = EntryState;
		c->Transitions = Transitions;   // POD + vetor de condicoes: copia direto

		c->States.reserve(States.size());

		for (const auto& st : States)
		{
			AnimSmState copy;
			copy.Name = st.Name;
			copy.EditorX = st.EditorX;
			copy.EditorY = st.EditorY;
			copy.Graph = st.Graph.Clone();   // <- recursao: o sub-grafo inteiro

			c->States.push_back(std::move(copy));
		}

		// O estado de runtime (m_Current, m_BlendElapsed...) NAO e copiado: a
		// copia nasce zerada, como um personagem que acabou de spawnar. Copiar
		// o estado do molde seria copiar o estado do ULTIMO personagem que o
		// tocou.
		c->Reset();

		return c;
	}

} // namespace axe