#include "anim_nodes.hpp"
#include <nlohmann/json.hpp>
#include "animation_sampler.hpp"
#include "axe/log/log.hpp"
#include "axe/physics/physics_system.hpp"   // Foot IK: raycast no chao

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>            // glm::rotation, glm::slerp
#include <glm/gtx/norm.hpp>
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

	// ═══ Foot IK ═════════════════════════════════════════════════════════════
	//
	// A matemática do two-bone IK é a lei dos cossenos: sabendo o comprimento
	// da coxa (a) e da canela (b) e a distância do quadril ao alvo (c), o
	// ângulo do joelho é único. O resto é orientar a cadeia pra apontar do
	// quadril ao alvo, mantendo o joelho dobrando pra frente (o "pole vector").
	//
	// Trabalhamos em ESPAÇO DE COMPONENTE (globais do personagem, antes da
	// world transform): é onde a hierarquia de ossos vive. Só o raycast sai
	// pra mundo e volta.

	namespace
	{
		// Poses globais (component-space) da pose local, num loop pra frente.
		// Mesmo cálculo do AnimationSampler::BuildPose, mas guardando a matriz
		// global de cada osso — que é o que o IK precisa ler e reescrever.
		void ComputeGlobals(const Skeleton& skel, const Pose& pose,
			std::vector<glm::mat4>& globals)
		{
			const auto& bones = skel.GetBones();
			const std::size_t n = bones.size();
			globals.resize(n);

			for (std::size_t i = 0; i < n; ++i)
			{
				const glm::mat4 local = (i < pose.Size())
					? pose[i].ToMatrix()
					: bones[i].LocalBindPose;

				globals[i] = (bones[i].ParentIndex < 0)
					? local
					: globals[bones[i].ParentIndex] * local;
			}
		}

		glm::vec3 MatPos(const glm::mat4& m) { return glm::vec3(m[3]); }

		// Casa um nome de osso TOLERANDO prefixo de namespace.
		//
		// Os nomes vem do arquivo CRUS: a Mixamo grava "mixamorig:LeftUpLeg",
		// o Blender as vezes "Armature|LeftUpLeg", outro rig so "LeftUpLeg".
		// Skeleton::FindBone e exato — entao um nome escrito sem o prefixo (ou
		// com um prefixo diferente) simplesmente NAO acha, e o no fica MUDO,
		// sem erro visivel na tela. Foi exatamente o que aconteceu com os
		// defaults deste no.
		//
		// O loader ja faz esse mesmo casamento por sufixo pra ligar os canais
		// dos clipes aos ossos; aqui e a mesma politica, inclusive na recusa do
		// ambiguo — casar errado em silencio e pior que nao casar.
		int FindBoneFlexible(const Skeleton& skel, const std::string& name)
		{
			if (name.empty())
				return -1;

			const int exact = skel.FindBone(name);

			if (exact >= 0)
				return exact;

			auto suffixOf = [](const std::string& s) -> std::string
				{
					const std::size_t p = s.find_last_of(":|");
					return (p == std::string::npos) ? s : s.substr(p + 1);
				};

			const std::string want = suffixOf(name);

			const auto& bones = skel.GetBones();

			int found = -1;
			int matches = 0;

			for (std::size_t i = 0; i < bones.size(); ++i)
			{
				if (suffixOf(bones[i].Name) != want)
					continue;

				++matches;

				if (found < 0)
					found = (int)i;
			}

			return (matches == 1) ? found : -1;
		}

		// Reescreve o LOCAL de um osso a partir de um novo GLOBAL, dado o
		// global do pai — o inverso da concatenação de ComputeGlobals.
		// local = parentGlobal⁻¹ * newGlobal.
		BoneTransform LocalFromGlobal(const glm::mat4& newGlobal,
			const glm::mat4& parentGlobal)
		{
			return BoneTransform::FromMatrix(glm::inverse(parentGlobal) * newGlobal);
		}
	}

	void AnimNode_FootIK::ResolveBones(const Skeleton& skel)
	{
		// Carimbo de versao do lado da axe.dll. Aparece uma vez por no, quando
		// ele resolve os ossos — confirma que o runtime do FOOTIK_V1 esta em
		// uso (e nao so o editor recompilado).
		AXE_CORE_INFO("FootIK - FOOTIK_V5 ({} pernas)", Legs.size());

		m_Resolved.clear();
		m_Resolved.reserve(Legs.size());

		int failed = 0;

		for (const auto& leg : Legs)
		{
			ResolvedLeg r;
			r.Upper = FindBoneFlexible(skel, leg.Upper);
			r.Lower = FindBoneFlexible(skel, leg.Lower);
			r.Foot = FindBoneFlexible(skel, leg.Foot);

			if (r.Upper < 0 || r.Lower < 0 || r.Foot < 0)
			{
				// Diz QUAL osso falhou — "a perna nao casou" nao ajuda a
				// consertar. Um '?' marca o que nao foi encontrado.
				AXE_CORE_WARN("FootIK: perna IGNORADA — coxa '{}'{} canela '{}'{} pe '{}'{}",
					leg.Upper, r.Upper < 0 ? " (?)" : "",
					leg.Lower, r.Lower < 0 ? " (?)" : "",
					leg.Foot, r.Foot < 0 ? " (?)" : "");

				++failed;
			}
			else
			{
				AXE_CORE_INFO("FootIK: perna OK — {} / {} / {}",
					skel.GetBones()[r.Upper].Name,
					skel.GetBones()[r.Lower].Name,
					skel.GetBones()[r.Foot].Name);
			}

			m_Resolved.push_back(r);
		}

		// Nenhuma perna casou = o no nao vai fazer NADA. Sem este aviso o
		// sintoma e "o IK simplesmente nao funciona", sem pista nenhuma —
		// entao mostramos alguns nomes reais do esqueleto pra comparar.
		if (failed > 0 && failed == (int)Legs.size() && !skel.GetBones().empty())
		{
			std::string sample;

			for (std::size_t i = 0; i < skel.GetBones().size() && i < 6; ++i)
				sample += (i ? ", " : "") + skel.GetBones()[i].Name;

			AXE_CORE_ERROR("FootIK: NENHUMA perna casou com o esqueleto — o no "
				"nao fara nada. Ossos deste esqueleto comecam assim: {} ...", sample);
		}

		// Pélvis: o osso nomeado, ou o pai comum das coxas, ou a raiz.
		m_PelvisIdx = PelvisBone.empty() ? -1 : FindBoneFlexible(skel, PelvisBone);

		if (m_PelvisIdx < 0 && !m_Resolved.empty() && m_Resolved[0].Upper >= 0)
		{
			// Pai da primeira coxa é o candidato natural (quadril/pélvis).
			m_PelvisIdx = skel.GetBones()[m_Resolved[0].Upper].ParentIndex;
		}

		m_BonesResolved = true;
	}

	void AnimNode_FootIK::Update(AnimEvalContext& ctx)
	{
		// Guarda o dt aqui: o contexto do Evaluate vem com DeltaTime = 0 (so o
		// Update avanca tempo), mas a suavizacao precisa dele — e o Update
		// sempre roda antes do Evaluate no mesmo frame.
		m_LastDt = ctx.DeltaTime;

		UpdateInput(ctx, 0);
	}

	void AnimNode_FootIK::Reset()
	{
		m_State.clear();
		m_PelvisOffset = 0.0f;
	}

	void AnimNode_FootIK::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		if (!ctx.Skel || !ctx.Pool)
			return;

		// A pose de entrada é o ponto de partida SEMPRE — o IK só corrige.
		EvalInput(ctx, 0, out);

		// Sem física ativa (preview do editor), o IK não tem chão pra
		// consultar. Passa a pose intacta em vez de raycastar no mundo errado.
		if (!ctx.AllowWorldQueries)
			return;

		const float alpha = glm::clamp(ReadFloat(ctx, 0), 0.0f, 1.0f);

		if (alpha <= 0.001f || Legs.empty())
			return;

		const Skeleton& skel = *ctx.Skel;

		// Re-resolve os índices se a lista de pernas mudou no editor. A hash é
		// barata e evita FindBone (busca em mapa) por frame.
		std::size_t hash = Legs.size() * 1315423911u;
		for (const auto& l : Legs)
			hash ^= std::hash<std::string>{}(l.Upper) + std::hash<std::string>{}(l.Foot)
			+ 0x9e3779b9u + (hash << 6) + (hash >> 2);

		if (!m_BonesResolved || hash != m_ResolvedHash)
		{
			ResolveBones(skel);
			m_ResolvedHash = hash;
			m_State.clear();
		}

		if (m_State.size() != m_Resolved.size())
			m_State.assign(m_Resolved.size(), LegState{});

		// Globais da pose de entrada (component-space).
		std::vector<glm::mat4> globals;
		ComputeGlobals(skel, out, globals);

		const glm::mat4& toWorld = ctx.WorldTransform;
		const glm::mat4  toLocal = glm::inverse(toWorld);

		// O "pra cima" do mundo em espaço de componente.
		const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		const glm::vec3 compUp = glm::normalize(glm::vec3(toLocal * glm::vec4(worldUp, 0.0f)));

		// Quantos METROS do mundo vale 1 unidade de espaco de componente.
		// E o comprimento da coluna Y do transform (a escala vertical). Guarda
		// contra escala zero, que zeraria a divisao adiante.
		const float compScale = std::max(1e-6f, glm::length(glm::vec3(toWorld[1])));

		// Fator de perseguicao exponencial. Independente de framerate.
		const float k = (m_LastDt > 0.0f && Smoothing > 0.0f)
			? (1.0f - std::exp(-Smoothing * m_LastDt))
			: 1.0f;

		// Posicao ORIGINAL de cada pe (antes do pelvis dip) — o alvo do IK e
		// absoluto, entao precisa ser calculado antes de mexer no quadril.
		std::vector<glm::vec3> footOrig(m_Resolved.size());

		for (std::size_t li = 0; li < m_Resolved.size(); ++li)
		{
			const ResolvedLeg& r = m_Resolved[li];
			LegState& st = m_State[li];

			if (r.Upper < 0 || r.Lower < 0 || r.Foot < 0)
			{
				st.Weight += (0.0f - st.Weight) * k;
				continue;
			}

			const glm::vec3 footComp = MatPos(globals[r.Foot]);
			footOrig[li] = footComp;

			const glm::vec3 footWorld = glm::vec3(toWorld * glm::vec4(footComp, 1.0f));

			const glm::vec3 origin = footWorld + worldUp * MaxReach;
			const RaycastHit hit = PhysicsSystem::Get().Raycast(
				origin, -worldUp, MaxReach * 2.0f);

			float wantOffset = 0.0f;
			float wantWeight = 0.0f;
			glm::vec3 wantNormal = compUp;

			if (hit.Hit)
			{
				wantNormal = glm::normalize(glm::vec3(toLocal * glm::vec4(hit.Normal, 0.0f)));

				// ── O deslocamento e a ALTURA DO TERRENO sob este pe ──────────
				//
				// Medida em MUNDO (metros), relativa ao plano de apoio do
				// personagem — a origem do transform, que fica nos PES: o pivo
				// do modelo esta na sola e a fisica apoia a base da capsula no
				// chao (ver CHARCAPSULE_V1). Em chao plano isso da zero e a
				// pose passa intacta; numa rampa da o desvio do chao sob cada
				// pe.
				//
				// MEDIR EM MUNDO IMPORTA. MaxReach e FootHeight sao METROS,
				// mas a pose vive em espaco de COMPONENTE, que pode estar
				// escalado pelo transform do personagem. Comparar os dois
				// direto fazia o clamp cortar o deslocamento pra quase nada —
				// a perna nao mexia, enquanto o alinhamento do pe (rotacao
				// pura, imune a escala) continuava funcionando. Era esse o
				// sintoma de "so o pe se ajusta".
				const glm::vec3 baseWorld = glm::vec3(toWorld[3]);

				float groundMeters = glm::dot(hit.Point - baseWorld, worldUp) + FootHeight;
				groundMeters = glm::clamp(groundMeters, -MaxReach, MaxReach);

				// Metros -> unidades de componente (uma unica conversao).
				wantOffset = groundMeters / compScale;

				wantWeight = 1.0f;
			}

			// Primeiro frame entra direto no valor certo (senao o personagem
			// "assenta" visivelmente ao dar Play).
			if (!st.Init)
			{
				st.Offset = wantOffset;
				st.Normal = wantNormal;
				st.Weight = wantWeight;
				st.Init = true;
			}
			else
			{
				st.Offset += (wantOffset - st.Offset) * k;
				st.Weight += (wantWeight - st.Weight) * k;
				st.Normal = glm::normalize(glm::mix(st.Normal, wantNormal, k));
			}
		}

		// ── Pelvis dip ───────────────────────────────────────────────────────
		//
		// Abaixa o quadril pelo pé que mais precisa DESCER, pra que a perna não
		// tenha que esticar reto. Feito ANTES do two-bone IK; os alvos dos pés
		// são absolutos, então continuam no chão e as pernas passam a dobrar
		// naturalmente pra alcançá-los.
		{
			float wantDip = 0.0f;

			for (std::size_t li = 0; li < m_State.size(); ++li)
			{
				const float eff = m_State[li].Offset * m_State[li].Weight;

				if (eff < wantDip)
					wantDip = eff;
			}

			m_PelvisOffset += (wantDip - m_PelvisOffset) * k;

			if (DipPelvis && m_PelvisIdx >= 0 && m_PelvisOffset < -1e-4f)
			{
				glm::mat4 g = globals[m_PelvisIdx];
				g[3] += glm::vec4(compUp * (m_PelvisOffset * alpha), 0.0f);

				const int parent = skel.GetBones()[m_PelvisIdx].ParentIndex;
				const glm::mat4 pg = (parent < 0) ? glm::mat4(1.0f) : globals[parent];

				if (m_PelvisIdx < (int)out.Size())
					out[m_PelvisIdx] = LocalFromGlobal(g, pg);

				// A pélvis desceu e TODOS os filhos (as pernas) desceram junto.
				ComputeGlobals(skel, out, globals);
			}
		}

		// ── Two-Bone IK por perna ────────────────────────────────────────────
		for (std::size_t li = 0; li < m_Resolved.size(); ++li)
		{
			const ResolvedLeg& r = m_Resolved[li];
			const LegState& st = m_State[li];

			if (r.Upper < 0 || r.Lower < 0 || r.Foot < 0)
				continue;

			const float w = alpha * st.Weight;

			if (w <= 0.001f)
				continue;

			glm::vec3 hipPos = MatPos(globals[r.Upper]);
			glm::vec3 kneePos = MatPos(globals[r.Lower]);
			glm::vec3 footPos = MatPos(globals[r.Foot]);

			const float upperLen = glm::length(kneePos - hipPos);
			const float lowerLen = glm::length(footPos - kneePos);

			if (upperLen < 1e-5f || lowerLen < 1e-5f)
				continue;

			// Alvo ABSOLUTO: a posição original do pé mais o deslocamento
			// suavizado, misturado pelo peso.
			const glm::vec3 wanted = footOrig[li] + compUp * st.Offset;
			const glm::vec3 target = glm::mix(footPos, wanted, w);

			glm::vec3 toTarget = target - hipPos;
			const float rawDist = glm::length(toTarget);

			if (rawDist < 1e-5f)
				continue;

			// Não deixa o alvo passar do alcance total (senão acos estoura) nem
			// colar no quadril.
			const float maxLen = (upperLen + lowerLen) * 0.999f;
			const float minLen = std::abs(upperLen - lowerLen) * 1.001f + 1e-4f;
			const float dist = glm::clamp(rawDist, minLen, maxLen);

			const glm::vec3 dirToTarget = toTarget / rawDist;

			// Direção do joelho (pole): mantém a dobra que a animação já tem,
			// projetada fora da linha quadril->alvo. Sem isto o joelho estala
			// pro lado.
			glm::vec3 kneeDir = kneePos - hipPos;
			glm::vec3 poleRaw = kneeDir - dirToTarget * glm::dot(kneeDir, dirToTarget);

			if (glm::length2(poleRaw) < 1e-8f)
			{
				glm::vec3 fwd = glm::normalize(glm::vec3(globals[r.Upper][2]));
				poleRaw = fwd - dirToTarget * glm::dot(fwd, dirToTarget);

				if (glm::length2(poleRaw) < 1e-8f)
					poleRaw = glm::cross(dirToTarget, compUp);
			}

			if (glm::length2(poleRaw) < 1e-8f)
				continue;

			const glm::vec3 pole = glm::normalize(poleRaw);

			// Lei dos cossenos: ângulo no quadril entre (quadril->alvo) e
			// (quadril->joelho).
			const float cosHip = glm::clamp(
				(upperLen * upperLen + dist * dist - lowerLen * lowerLen)
				/ (2.0f * upperLen * dist), -1.0f, 1.0f);
			const float hipAngle = std::acos(cosHip);

			const glm::vec3 newKnee = hipPos
				+ (std::cos(hipAngle) * dirToTarget + std::sin(hipAngle) * pole) * upperLen;

			const glm::vec3 newFoot = hipPos + dirToTarget * dist;

			// Rotaciona um osso de modo que a direção osso->filho passe da
			// antiga para a nova. glm::rotation devolve o quaternion mínimo
			// entre dois vetores unitários.
			auto rotateBoneToward = [&](int bone, const glm::vec3& oldChild,
				const glm::vec3& newChild)
				{
					const glm::vec3 bonePos = MatPos(globals[bone]);

					const glm::vec3 oldV = oldChild - bonePos;
					const glm::vec3 newV = newChild - bonePos;

					if (glm::length2(oldV) < 1e-10f || glm::length2(newV) < 1e-10f)
						return;

					const glm::quat delta =
						glm::rotation(glm::normalize(oldV), glm::normalize(newV));

					glm::mat4 g = globals[bone];
					g = glm::translate(glm::mat4(1.0f), bonePos)
						* glm::mat4_cast(delta)
						* glm::translate(glm::mat4(1.0f), -bonePos) * g;
					globals[bone] = g;

					const int parent = skel.GetBones()[bone].ParentIndex;
					const glm::mat4 pg = (parent < 0) ? glm::mat4(1.0f) : globals[parent];

					if (bone < (int)out.Size())
						out[bone] = LocalFromGlobal(g, pg);
				};

			rotateBoneToward(r.Upper, kneePos, newKnee);
			ComputeGlobals(skel, out, globals);

			kneePos = MatPos(globals[r.Lower]);
			footPos = MatPos(globals[r.Foot]);

			rotateBoneToward(r.Lower, footPos, newFoot);
			ComputeGlobals(skel, out, globals);

			// ── Alinhar o pé à inclinação do chão ─────────────────────────────
			//
			// SEM supor qual eixo do osso é a sola.
			//
			// A versão anterior assumia que o +Y do osso do pé apontava pra
			// cima e girava esse eixo até a normal. Num rig Mixamo o osso do pé
			// aponta pro DEDO, não pra cima — então a correção era uma rotação
			// enorme e arbitrária, e o pé saía torto/invertido. Pior: quando os
			// dois vetores ficam quase opostos, glm::rotation é degenerado (o
			// eixo é indefinido) e o resultado tremia.
			//
			// O que vale pra QUALQUER rig: em chão plano a animação já orienta
			// o pé corretamente. Numa rampa, basta inclinar o pé pela MESMA
			// rotação que inclina o chão em relação ao plano — seja qual for a
			// orientação interna do osso.
			{
				const float dot = glm::clamp(glm::dot(compUp, st.Normal), -1.0f, 1.0f);

				// Chão praticamente plano: nada a fazer (e evita o caso
				// degenerado do quaternion).
				if (dot < 0.9999f)
				{
					glm::quat tilt = glm::rotation(compUp, st.Normal);

					// Trava em 45 graus. Uma normal absurda (parede, quina de
					// colisor) não deve virar o pé de cabeça pra baixo.
					const float angle = std::acos(dot);
					const float maxAngle = glm::radians(45.0f);

					float t = w;

					if (angle > maxAngle)
						t *= maxAngle / angle;

					tilt = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), tilt, t);

					const glm::mat4 fg = globals[r.Foot];
					const glm::vec3 pos = MatPos(fg);

					const glm::mat4 g = glm::translate(glm::mat4(1.0f), pos)
						* glm::mat4_cast(tilt)
						* glm::translate(glm::mat4(1.0f), -pos) * fg;

					const int parent = skel.GetBones()[r.Foot].ParentIndex;
					const glm::mat4 pg = (parent < 0) ? glm::mat4(1.0f) : globals[parent];

					if (r.Foot < (int)out.Size())
						out[r.Foot] = LocalFromGlobal(g, pg);

					globals[r.Foot] = g;
				}
			}
		}
	}

	void AnimNode_FootIK::Serialize(nlohmann::json& j) const
	{
		j["max_reach"] = MaxReach;
		j["foot_height"] = FootHeight;
		j["smoothing"] = Smoothing;
		j["dip_pelvis"] = DipPelvis;
		j["pelvis_bone"] = PelvisBone;

		j["legs"] = nlohmann::json::array();
		for (const auto& l : Legs)
			j["legs"].push_back({ {"upper", l.Upper}, {"lower", l.Lower}, {"foot", l.Foot} });
	}

	void AnimNode_FootIK::Deserialize(const nlohmann::json& j)
	{
		MaxReach = j.value("max_reach", 0.5f);
		FootHeight = j.value("foot_height", 0.0f);
		Smoothing = j.value("smoothing", 12.0f);
		DipPelvis = j.value("dip_pelvis", true);
		PelvisBone = j.value("pelvis_bone", std::string());

		if (j.contains("legs"))
		{
			Legs.clear();
			for (const auto& jl : j["legs"])
				Legs.push_back({
					jl.value("upper", std::string()),
					jl.value("lower", std::string()),
					jl.value("foot",  std::string()) });
		}

		m_BonesResolved = false;   // força re-resolver com os novos nomes
	}

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
		if (t == "FootIK")           return std::make_unique<AnimNode_FootIK>();
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