#include "rig_nodes.hpp"
#include "axe/physics/physics_system.hpp"
#include "axe/log/log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <cmath>

namespace axe
{
	namespace
	{
		glm::vec3 MatPos(const glm::mat4& m) { return glm::vec3(m[3]); }
	}

	// ═══ Sequence ════════════════════════════════════════════════════════════

	void RigNode_Sequence::Execute(RigExecContext& ctx)
	{
		if (!ctx.Graph)
			return;

		// ── DE CIMA PRA BAIXO, TODAS ────────────────────────────────────────
		//
		// Antes eu rodava so 1..N aqui e deixava a saida 0 pro percurso
		// externo, que so a seguia DEPOIS deste Execute retornar. A ordem real
		// virava Then 1 -> Then 2 -> ... -> Then 0: a PRIMEIRA saida executava
		// por ULTIMO.
		//
		// Num rig isso inverte o resultado inteiro. Com o quadril no Then 0 e
		// as pernas nos seguintes, o IK resolvia primeiro e o quadril mexia
		// depois — entao os pes saiam dos alvos e o joelho nunca dobrava.
		//
		// Agora o no assume o fluxo (HandlesOwnFlow) e roda tudo na ordem que
		// esta na tela.
		for (std::size_t i = 0; i < ExecOut.size(); ++i)
			ctx.Graph->RunExecPin(ctx, Id, (int)i);
	}

	void RigNode_Sequence::Serialize(nlohmann::json& j) const
	{
		j["exec_out"] = ExecOut;
	}

	void RigNode_Sequence::Deserialize(const nlohmann::json& j)
	{
		if (j.contains("exec_out"))
			ExecOut = j["exec_out"].get<std::vector<std::string>>();
	}

	// ═══ Branch / For Each / Project to New Parent ═══════════════════════════

	void RigNode_Branch::Execute(RigExecContext& ctx)
	{
		if (!ctx.Graph)
			return;

		ctx.Graph->RunExecPin(ctx, Id, ReadBool(ctx, 0) ? 0 : 1);
	}

	void RigNode_ForEach::Execute(RigExecContext& ctx)
	{
		if (!ctx.Graph)
			return;

		const RigPinValue arr = Read(ctx, 0);

		m_Count = (int)arr.Items.size();

		for (int i = 0; i < m_Count; ++i)
		{
			m_Index = i;
			m_Element = arr.Items[(std::size_t)i];

			// LIMPA O CACHE a cada volta. Sem isto, um Get Transform ligado no
			// Element seria calculado UMA vez e todas as iteracoes receberiam o
			// primeiro item — o laco pareceria rodar mas mexeria sempre no
			// mesmo osso.
			ctx.Graph->InvalidateDataCache();

			ctx.Graph->RunExecPin(ctx, Id, 0);
		}

		ctx.Graph->InvalidateDataCache();

		// "Completed" so depois de TUDO: e onde se poe o que depende do laco
		// inteiro ter terminado.
		ctx.Graph->RunExecPin(ctx, Id, 1);
	}

	void RigNode_At::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		(void)pin;

		out = RigPinValue{};

		const RigPinValue arr = Read(ctx, 0);

		// O indice vem como float porque o grafo nao tem tipo inteiro; um For
		// Each sempre entrega um valor exato, entao arredondar e seguro.
		const int idx = (int)std::lround(ReadFloat(ctx, 1));

		if (idx < 0 || idx >= (int)arr.Items.size())
		{
			// Fora da faixa quase sempre significa listas de tamanhos
			// diferentes. Avisa UMA vez: dentro de um laco, avisar por volta
			// encheria o console em um segundo.
			if (!m_WarnedRange)
			{
				m_WarnedRange = true;

				AXE_CORE_WARN("At '{}': indice {} fora da lista ({} itens). "
					"As duas listas tem o mesmo tamanho?",
					Title, idx, arr.Items.size());
			}

			return;
		}

		out.ItemName = arr.Items[(std::size_t)idx].Name;
		out.ItemType = arr.Items[(std::size_t)idx].Type;
	}

	void RigNode_ProjectToNewParent::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		(void)pin;

		out = RigPinValue{};

		if (!ctx.Hierarchy)
			return;

		const int child = ReadItem(ctx, 0);
		const int oldP = ReadItem(ctx, 2);
		const int newP = ReadItem(ctx, 4);

		if (child < 0 || oldP < 0 || newP < 0)
			return;

		RigHierarchy& h = *ctx.Hierarchy;

		auto pick = [&](int idx, bool initial)
			{
				return initial ? h.GetInitialGlobal(idx) : h.GetGlobal(idx);
			};

		const glm::mat4 childG = pick(child, ReadBool(ctx, 1));
		const glm::mat4 oldG = pick(oldP, ReadBool(ctx, 3));
		const glm::mat4 newG = pick(newP, ReadBool(ctx, 5));

		// A RELACAO do filho com o pai antigo, reaplicada sobre o pai novo.
		//
		// Os flags "Initial" existem porque quase sempre a relacao que interessa
		// e a do REPOUSO (onde as coisas nasceram), enquanto o pai novo e o
		// estado ATUAL — que ja foi movido pelo IK. Misturar os dois e o que
		// faz o controle de FK acompanhar o resultado do IK.
		const glm::mat4 rel = glm::inverse(oldG) * childG;

		out.Transform = BoneTransform::FromMatrix(newG * rel);
	}

	// ═══ Get Transform ═══════════════════════════════════════════════════════

	RigNode_GetTransform::RigNode_GetTransform()
	{
		Title = "Get Transform";

		AddInItem("Item", RigElementType::Bone);

		AddOut("Transform", RigPinType::Transform);

		// A posicao sai tambem solta. E redundante com o Transform, mas "pegue
		// a posicao do pe" e o que mais se faz num rig — obrigar um Break
		// Transform pra isso encheria o grafo de nos sem informacao nenhuma.
		AddOut("Location", RigPinType::Vector);
	}

	void RigNode_GetTransform::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		out = RigPinValue{};

		if (!ctx.Hierarchy)
			return;

		const int item = ReadItem(ctx, 0);

		if (item < 0)
			return;

		glm::mat4 m(1.0f);

		if (Space == RigSpace::Global)
		{
			m = Initial ? ctx.Hierarchy->GetInitialGlobal(item)
				: ctx.Hierarchy->GetGlobal(item);
		}
		else
		{
			const RigElement& e = (*ctx.Hierarchy)[item];
			m = (Initial ? e.Initial : e.Current).ToMatrix();
		}

		if (pin == 0)
			out.Transform = BoneTransform::FromMatrix(m);
		else
			out.Vector = MatPos(m);
	}

	void RigNode_GetTransform::Serialize(nlohmann::json& j) const
	{
		j["space"] = (int)Space;
		j["initial"] = Initial;
	}

	void RigNode_GetTransform::Deserialize(const nlohmann::json& j)
	{
		Space = (RigSpace)j.value("space", 0);
		Initial = j.value("initial", false);
	}

	// ═══ Set Transform ═══════════════════════════════════════════════════════

	RigNode_SetTransform::RigNode_SetTransform()
	{
		Title = "Set Transform";
		HasExecIn = true;
		ExecOut.push_back("");

		AddInItem("Item", RigElementType::Bone);
		AddIn("Transform", RigPinType::Transform);

		// Peso 1 por padrao: um Set recem-criado ESCREVE. Se nascesse em 0,
		// voce ligaria tudo certo e nao veria efeito nenhum.
		AddInFloat("Weight", 1.0f);
	}

	void RigNode_SetTransform::Execute(RigExecContext& ctx)
	{
		if (!ctx.Hierarchy)
			return;

		const int item = ReadItem(ctx, 0);

		if (item < 0)
			return;

		const float weight = glm::clamp(ReadFloat(ctx, 2), 0.0f, 1.0f);

		if (weight <= 0.0001f)
			return;

		const BoneTransform wanted = Read(ctx, 1).Transform;

		if (Space == RigSpace::Local)
		{
			const BoneTransform cur = ctx.Hierarchy->GetLocal(item);

			// Peso parcial mistura com o que ja estava la — e o que permite
			// somar um efeito por cima da animacao em vez de substituir.
			BoneTransform t;
			t.Translation = glm::mix(cur.Translation, wanted.Translation, weight);
			t.Rotation = glm::slerp(cur.Rotation, wanted.Rotation, weight);
			t.Scale = glm::mix(cur.Scale, wanted.Scale, weight);

			ctx.Hierarchy->SetLocal(item, t);
			return;
		}

		const glm::mat4 curG = ctx.Hierarchy->GetGlobal(item);
		const BoneTransform cur = BoneTransform::FromMatrix(curG);

		BoneTransform t;
		t.Translation = glm::mix(cur.Translation, wanted.Translation, weight);
		t.Rotation = glm::slerp(cur.Rotation, wanted.Rotation, weight);
		t.Scale = glm::mix(cur.Scale, wanted.Scale, weight);

		ctx.Hierarchy->SetGlobal(item, t.ToMatrix(), PropagateToChildren);
	}

	void RigNode_SetTransform::Serialize(nlohmann::json& j) const
	{
		j["space"] = (int)Space;
		j["propagate"] = PropagateToChildren;
	}

	void RigNode_SetTransform::Deserialize(const nlohmann::json& j)
	{
		Space = (RigSpace)j.value("space", 0);
		PropagateToChildren = j.value("propagate", true);
	}

	// ═══ Two Bone IK ═════════════════════════════════════════════════════════

	RigNode_TwoBoneIK::RigNode_TwoBoneIK()
	{
		Title = "Two Bone IK";
		HasExecIn = true;
		ExecOut.push_back("");

		AddInItem("Root", RigElementType::Bone);       // coxa / ombro
		AddInItem("Middle", RigElementType::Bone);     // canela / cotovelo
		AddInItem("Effector", RigElementType::Bone);   // pe / mao

		AddIn("Target", RigPinType::Vector);           // em espaco Global do rig
		AddIn("Pole Target", RigPinType::Vector);      // POSICAO que o joelho mira

		AddInFloat("Weight", 1.0f);

		// ── ESTICAR ─────────────────────────────────────────────────────────
		//
		// Sem isto, um membro que ja nasce RETO na pose de repouso (a perna em
		// T-pose e quase reta) nao tem pra onde estender: afastar o alvo um
		// centimetro ja passa do alcance e o efetor SOLTA do controle.
		//
		// Com Stretch ligado o osso ALONGA pra alcancar — e o "Allow Stretching"
		// da UE e o "Stretch" do constraint de IK do Blender. E o que faz o pe
		// continuar colado no controle quando o quadril sobe.
		AddInBool("Stretch", false);
		AddInFloat("Max Stretch", 1.5f);
	}

	void RigNode_TwoBoneIK::Execute(RigExecContext& ctx)
	{
		if (!ctx.Hierarchy)
			return;

		const int a = ReadItem(ctx, 0);
		const int b = ReadItem(ctx, 1);
		const int c = ReadItem(ctx, 2);

		if (a < 0 || b < 0 || c < 0)
			return;

		const float weight = glm::clamp(ReadFloat(ctx, 5), 0.0f, 1.0f);

		if (weight <= 0.0001f)
			return;

		RigHierarchy& h = *ctx.Hierarchy;

		glm::vec3 rootPos = MatPos(h.GetGlobal(a));
		glm::vec3 midPos = MatPos(h.GetGlobal(b));
		glm::vec3 tipPos = MatPos(h.GetGlobal(c));

		float upperLen = glm::length(midPos - rootPos);
		float lowerLen = glm::length(tipPos - midPos);

		if (upperLen < 1e-5f || lowerLen < 1e-5f)
			return;

		const glm::vec3 targetIn = ReadVector(ctx, 3);

		// ── DIAGNOSTICO: Target zerado ───────────────────────────────────────
		//
		// (0,0,0) e a ORIGEM do rig, entre os pes — praticamente nunca e onde
		// alguem quer o efetor. Mas zero SO e sintoma quando o pino esta
		// SOLTO: com um fio ligado, um zero e quase sempre transitorio (o
		// Ground Trace nao acertou neste frame, o solve ainda nao tem dado, um
		// Damp ainda nao esquentou).
		//
		// Avisar mesmo com o pino ligado foi um DEFEITO desta funcao: como o
		// aviso e uma-vez-e-nunca-mais, um unico frame ruim deixava no console
		// uma mensagem que parecia atual e acusava um erro que nao existia.
		if (!m_WarnedZeroTarget && glm::length2(targetIn) < 1e-8f)
		{
			bool linked = false;

			// So varremos os fios no caso raro (valor zerado e ainda sem
			// aviso), entao isto nao custa nada no frame normal.
			if (ctx.Graph)
			{
				for (const auto& l : ctx.Graph->GetDataLinks())
				{
					if (l.ToNode == Id && l.ToPin == 3)
					{
						linked = true;
						break;
					}
				}
			}

			if (!linked)
			{
				m_WarnedZeroTarget = true;

				AXE_CORE_WARN("Two Bone IK '{}': o pino Target esta SOLTO e o valor "
					"digitado nele e (0,0,0) — o efetor vai ser puxado pra origem do "
					"rig. Ligue a Location de um controle nesse pino.", Title);
			}
		}

		const glm::vec3 target = glm::mix(tipPos, targetIn, weight);

		const glm::vec3 toTarget = target - rootPos;
		const float rawDist = glm::length(toTarget);

		if (rawDist < 1e-5f)
			return;

		// Clamp do alcance: passar do comprimento total faria o acos estourar,
		// e colar no proprio quadril tambem nao tem solucao.
		// ── ESTICA, SE PEDIDO ────────────────────────────────────────────────
		//
		// Quando o alvo esta ALEM do alcance, alongamos os dois ossos na mesma
		// proporcao ate chegar la, respeitando o teto. Alongar = escalar a
		// TRANSLACAO LOCAL do filho, que e o que define o comprimento do osso;
		// a malha acompanha porque o skinning segue os ossos.
		if (ReadBool(ctx, 6))
		{
			const float total = upperLen + lowerLen;
			const float maxStretch = std::max(1.0f, ReadFloat(ctx, 7));

			if (total > 1e-5f && rawDist > total)
			{
				const float stretch = std::min(rawDist / total, maxStretch);

				if (stretch > 1.0001f)
				{
					BoneTransform lb = h.GetLocal(b);
					lb.Translation *= stretch;
					h.SetLocal(b, lb);

					BoneTransform lc = h.GetLocal(c);
					lc.Translation *= stretch;
					h.SetLocal(c, lc);

					// Os ossos mudaram de tamanho: reler tudo antes de resolver.
					rootPos = MatPos(h.GetGlobal(a));
					midPos = MatPos(h.GetGlobal(b));
					tipPos = MatPos(h.GetGlobal(c));

					upperLen *= stretch;
					lowerLen *= stretch;
				}
			}
		}

		const float maxLen = (upperLen + lowerLen) * 0.999f;
		const float minLen = std::abs(upperLen - lowerLen) * 1.001f + 1e-4f;
		const float dist = glm::clamp(rawDist, minLen, maxLen);

		const glm::vec3 dir = toTarget / rawDist;

		// Pole vector: pra onde a junta do meio dobra. Se nao vier nada
		// utilizavel, usamos a dobra que a pose JA tem — que e quase sempre a
		// resposta certa, e impede o joelho de estalar pro lado.
		// ── POLE: PRA ONDE A JUNTA DO MEIO DOBRA ─────────────────────────────
		//
		// E uma POSICAO no espaco do rig, nao uma direcao — o mesmo que a UE
		// chama de Pole Target. E o que permite ligar a Location de um CONTROLE
		// aqui e mirar o joelho arrastando um objeto na tela, em vez de
		// adivinhar componentes de vetor a mao.
		//
		// (0,0,0) = AUTOMATICO: cai na cadeia de recurso abaixo, que acerta
		// sozinha na maioria dos casos.
		const glm::vec3 poleTarget = ReadVector(ctx, 4);

		glm::vec3 poleRaw(0.0f);

		if (glm::length2(poleTarget) > 1e-8f)
		{
			// Direcao do quadril PARA o alvo do pole, tirando a parte que
			// corre ao longo do membro (essa nao diz nada sobre pra onde
			// dobrar).
			const glm::vec3 toPole = poleTarget - rootPos;
			poleRaw = toPole - dir * glm::dot(toPole, dir);
		}

		// 1) A dobra que a pose JA tem. Quase sempre a resposta certa, e o que
		//    impede o joelho de estalar pro outro lado.
		if (glm::length2(poleRaw) < 1e-8f)
		{
			const glm::vec3 midDir = midPos - rootPos;
			poleRaw = midDir - dir * glm::dot(midDir, dir);
		}

		// 2) Membro ESTICADO no repouso (a perna em T-pose e quase reta): a
		//    dobra atual e degenerada e nao informa nada. Usa o "pra frente"
		//    do proprio osso — e pra la que joelho e cotovelo dobram.
		if (glm::length2(poleRaw) < 1e-8f)
		{
			const glm::vec3 fwd = glm::vec3(h.GetGlobal(a)[2]);
			poleRaw = fwd - dir * glm::dot(fwd, dir);
		}

		// 3) Ultimo recurso: qualquer perpendicular serve. Melhor dobrar num
		//    plano arbitrario do que NAO RODAR — aqui havia um return, e o
		//    sintoma era o IK simplesmente nao fazer nada, sem nenhuma pista.
		if (glm::length2(poleRaw) < 1e-8f)
			poleRaw = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));

		if (glm::length2(poleRaw) < 1e-8f)
			poleRaw = glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f));

		const glm::vec3 poleN = glm::normalize(poleRaw);

		// Lei dos cossenos: com os dois comprimentos e a distancia ate o alvo,
		// o angulo da junta e unico.
		const float cosRoot = glm::clamp(
			(upperLen * upperLen + dist * dist - lowerLen * lowerLen)
			/ (2.0f * upperLen * dist), -1.0f, 1.0f);

		const float rootAngle = std::acos(cosRoot);

		const glm::vec3 newMid = rootPos
			+ (std::cos(rootAngle) * dir + std::sin(rootAngle) * poleN) * upperLen;

		const glm::vec3 newTip = rootPos + dir * dist;

		// Gira um elo de modo que a direcao elo->filho passe da antiga pra
		// nova. glm::rotation devolve o quaternion minimo entre dois unitarios.
		auto aim = [&](int bone, const glm::vec3& oldChild, const glm::vec3& newChild)
			{
				const glm::vec3 p = MatPos(h.GetGlobal(bone));

				const glm::vec3 oldV = oldChild - p;
				const glm::vec3 newV = newChild - p;

				if (glm::length2(oldV) < 1e-10f || glm::length2(newV) < 1e-10f)
					return;

				const glm::quat q =
					glm::rotation(glm::normalize(oldV), glm::normalize(newV));

				glm::mat4 g = h.GetGlobal(bone);

				g = glm::translate(glm::mat4(1.0f), p)
					* glm::mat4_cast(q)
					* glm::translate(glm::mat4(1.0f), -p) * g;

				h.SetGlobal(bone, g, true);
			};

		aim(a, midPos, newMid);

		// A raiz girou, entao meio e ponta se moveram junto: reler antes de
		// mirar o elo de baixo.
		midPos = MatPos(h.GetGlobal(b));
		tipPos = MatPos(h.GetGlobal(c));

		aim(b, tipPos, newTip);
	}

	// ═══ Ground Trace ════════════════════════════════════════════════════════

	RigNode_GroundTrace::RigNode_GroundTrace()
	{
		Title = "Ground Trace";

		AddIn("Origin", RigPinType::Vector);      // espaco Global do rig
		AddInFloat("Distance", 0.5f);             // METROS

		AddOut("Hit", RigPinType::Bool);
		AddOut("Location", RigPinType::Vector);   // espaco Global do rig
		AddOut("Normal", RigPinType::Vector);
		AddOut("Height", RigPinType::Float);      // desvio do plano de apoio
	}

	void RigNode_GroundTrace::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		out = RigPinValue{};

		if (!ctx.AllowWorldQueries && !ctx.UseEditorGround)
			return;

		const glm::vec3 originComp = ReadVector(ctx, 0);
		const float reach = std::max(0.01f, ReadFloat(ctx, 1));

		const glm::mat4& toWorld = ctx.WorldTransform;
		const glm::mat4  toLocal = glm::inverse(toWorld);

		// Escala do personagem: quantos metros do mundo vale 1 unidade de
		// espaco de componente. Sem esta conversao, "Alcance = 0.5 m" viraria
		// meia UNIDADE de componente — foi exatamente o bug do FOOTIK_V5.
		const float scale = std::max(1e-6f, glm::length(glm::vec3(toWorld[1])));

		const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		const glm::vec3 originWorld = glm::vec3(toWorld * glm::vec4(originComp, 1.0f));

		RaycastHit hit{};

		if (ctx.AllowWorldQueries)
		{
			hit = PhysicsSystem::Get().Raycast(
				originWorld + worldUp * reach, -worldUp, reach * 2.0f);
		}
		else
		{
			// Chao virtual do preview: plano horizontal em Y = 0, que e
			// exatamente o grid desenhado. So responde dentro do alcance, pra
			// se comportar como o raycast de verdade.
			if (std::abs(originWorld.y) <= reach)
			{
				hit.Hit = true;
				hit.Point = glm::vec3(originWorld.x, 0.0f, originWorld.z);
				hit.Normal = worldUp;
				hit.Distance = std::abs(originWorld.y);
			}
		}

		if (!hit.Hit)
			return;

		switch (pin)
		{
		case 0:
			out.Bool = true;
			break;

		case 1:
			out.Vector = glm::vec3(toLocal * glm::vec4(hit.Point, 1.0f));
			break;

		case 2:
			out.Vector = glm::normalize(glm::vec3(toLocal * glm::vec4(hit.Normal, 0.0f)));
			break;

		case 3:
		{
			// Altura do terreno relativa ao plano de apoio do personagem — a
			// origem do transform, que fica nos PES. E a medida certa pra IK de
			// pe: nao depende de onde o osso esta no rig.
			const glm::vec3 baseWorld = glm::vec3(toWorld[3]);
			out.Float = glm::dot(hit.Point - baseWorld, worldUp) / scale;
			break;
		}

		default:
			break;
		}
	}

	// ═══ Make / Break Transform ══════════════════════════════════════════════

	RigNode_MakeTransform::RigNode_MakeTransform()
	{
		Title = "Make Transform";

		AddIn("Location", RigPinType::Vector);
		AddIn("Rotation", RigPinType::Vector);    // Euler, graus
		AddIn("Scale", RigPinType::Vector);

		Inputs[2].Default.Vector = glm::vec3(1.0f);

		AddOut("Transform", RigPinType::Transform);
	}

	void RigNode_MakeTransform::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		(void)pin;

		out = RigPinValue{};

		out.Transform.Translation = ReadVector(ctx, 0);
		out.Transform.Rotation = glm::quat(glm::radians(ReadVector(ctx, 1)));

		const glm::vec3 s = ReadVector(ctx, 2);

		// Escala zero achataria o osso e nunca e o que se quis dizer.
		out.Transform.Scale = (glm::length2(s) < 1e-8f) ? glm::vec3(1.0f) : s;
	}

	RigNode_BreakTransform::RigNode_BreakTransform()
	{
		Title = "Break Transform";

		AddIn("Transform", RigPinType::Transform);

		AddOut("Location", RigPinType::Vector);
		AddOut("Rotation", RigPinType::Vector);
		AddOut("Scale", RigPinType::Vector);
	}

	void RigNode_BreakTransform::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		out = RigPinValue{};

		const BoneTransform t = Read(ctx, 0).Transform;

		switch (pin)
		{
		case 0: out.Vector = t.Translation; break;
		case 1: out.Vector = glm::degrees(glm::eulerAngles(t.Rotation)); break;
		case 2: out.Vector = t.Scale; break;
		default: break;
		}
	}

	// ═══ Vector Op ═══════════════════════════════════════════════════════════

	RigNode_VectorOp::RigNode_VectorOp()
	{
		Title = "Vector Op";

		AddIn("A", RigPinType::Vector);
		AddIn("B", RigPinType::Vector);
		AddInFloat("Factor", 1.0f);

		AddOut("Result", RigPinType::Vector);
	}

	void RigNode_VectorOp::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		(void)pin;

		out = RigPinValue{};

		const glm::vec3 a = ReadVector(ctx, 0);
		const glm::vec3 b = ReadVector(ctx, 1);
		const float     f = ReadFloat(ctx, 2);

		switch (Operation)
		{
		case Op::Add:      out.Vector = a + b; break;
		case Op::Subtract: out.Vector = a - b; break;
		case Op::Scale:    out.Vector = a * f; break;
		case Op::Lerp:     out.Vector = glm::mix(a, b, glm::clamp(f, 0.0f, 1.0f)); break;
		}
	}

	void RigNode_VectorOp::Serialize(nlohmann::json& j) const
	{
		j["op"] = (int)Operation;
	}

	void RigNode_VectorOp::Deserialize(const nlohmann::json& j)
	{
		Operation = (Op)j.value("op", 0);
	}

	// ═══ Reroute / Comment ═══════════════════════════════════════════════════

	void RigNode_Reroute::Serialize(nlohmann::json& j) const
	{
		// MODO e TIPO precisam sobreviver ao save: sem eles o reroute voltaria
		// Indeciso/Wildcard e os fios ligados nele seriam recusados na proxima
		// abertura.
		j["mode"] = (int)CurrentMode;
		j["type"] = Inputs.empty() ? (int)RigPinType::Wildcard : (int)Inputs[0].Type;
	}

	void RigNode_Reroute::Deserialize(const nlohmann::json& j)
	{
		SetMode((Mode)j.value("mode", (int)Mode::Undecided));

		const auto t = (RigPinType)j.value("type", (int)RigPinType::Wildcard);

		if (!Inputs.empty() && t != RigPinType::Wildcard)
		{
			Inputs[0].Type = t;
			Outputs[0].Type = t;
		}
	}

	void RigNode_Comment::Serialize(nlohmann::json& j) const
	{
		j["w"] = SizeX;
		j["h"] = SizeY;
		j["color"] = { Color.r, Color.g, Color.b };
	}

	void RigNode_Comment::Deserialize(const nlohmann::json& j)
	{
		SizeX = j.value("w", 320.0f);
		SizeY = j.value("h", 180.0f);

		if (j.contains("color") && j["color"].size() == 3)
			Color = { j["color"][0], j["color"][1], j["color"][2] };
	}

	// ═══ Item Array / FK Chain ═══════════════════════════════════════════════

	void RigNode_ItemArray::Serialize(nlohmann::json& j) const
	{
		j["items"] = nlohmann::json::array();

		for (const auto& it : Items)
			j["items"].push_back({ { "name", it.Name }, { "type", (int)it.Type } });
	}

	void RigNode_ItemArray::Deserialize(const nlohmann::json& j)
	{
		Items.clear();

		if (!j.contains("items"))
			return;

		for (const auto& ji : j["items"])
		{
			RigItemRef r;
			r.Name = ji.value("name", std::string());
			r.Type = (RigElementType)ji.value("type", 0);

			Items.push_back(std::move(r));
		}
	}

	namespace
	{
		// Resolve uma referencia da lista. Tenta o par NOME+TIPO exato e so
		// depois cai no casamento tolerante a prefixo (mixamorig:), pela mesma
		// razao do ReadItem: Bone e Control podem ter o mesmo nome.
		int ResolveRef(const RigHierarchy& h, const RigItemRef& r)
		{
			if (r.Name.empty())
				return -1;

			const int exact = h.Find(r.Name, r.Type);

			return (exact >= 0) ? exact : h.FindFlexible(r.Name);
		}
	}

	void RigNode_FKChain::Execute(RigExecContext& ctx)
	{
		if (!ctx.Hierarchy)
			return;

		const RigPinValue bones = Read(ctx, 0);
		const RigPinValue ctrls = Read(ctx, 1);

		const float weight = glm::clamp(ReadFloat(ctx, 2), 0.0f, 1.0f);

		if (weight <= 0.0001f)
			return;

		const std::size_t n = std::min(bones.Items.size(), ctrls.Items.size());

		// Listas de tamanhos diferentes quase sempre e um par esquecido. Avisa
		// UMA vez — o sintoma seria a ultima junta simplesmente nao responder,
		// sem nenhuma pista.
		if (!m_WarnedSize && bones.Items.size() != ctrls.Items.size())
		{
			m_WarnedSize = true;

			AXE_CORE_WARN("FK Chain '{}': as listas tem tamanhos diferentes "
				"({} ossos, {} controles). So os {} primeiros pares sao aplicados.",
				Title, bones.Items.size(), ctrls.Items.size(), n);
		}

		RigHierarchy& h = *ctx.Hierarchy;

		for (std::size_t i = 0; i < n; ++i)
		{
			const int b = ResolveRef(h, bones.Items[i]);
			const int c = ResolveRef(h, ctrls.Items[i]);

			if (b < 0 || c < 0)
				continue;

			const BoneTransform src = h.GetLocal(c);

			if (weight >= 0.9999f)
			{
				h.SetLocal(b, src);
				continue;
			}

			// Peso parcial mistura com o que ja esta la — permite somar o FK
			// por cima de uma animacao em vez de substituir.
			const BoneTransform cur = h.GetLocal(b);

			BoneTransform t;
			t.Translation = glm::mix(cur.Translation, src.Translation, weight);
			t.Rotation = glm::slerp(cur.Rotation, src.Rotation, weight);
			t.Scale = glm::mix(cur.Scale, src.Scale, weight);

			h.SetLocal(b, t);
		}
	}

	// ═══ Hide Controls ═══════════════════════════════════════════════════════

	void RigNode_HideControls::Execute(RigExecContext& ctx)
	{
		if (!ctx.Hierarchy)
			return;

		const RigPinValue list = Read(ctx, 0);

		// Active LIGADO = esconde. Desligado = mostra, o que permite ligar o
		// MESMO interruptor nos dois nos (um negado) e trocar os dois conjuntos
		// de controle de uma vez.
		const bool hide = ReadBool(ctx, 1);

		RigHierarchy& h = *ctx.Hierarchy;

		for (const auto& ref : list.Items)
		{
			const int idx = ResolveRef(h, ref);

			if (idx >= 0)
				h[idx].Visible = !hide;
		}
	}

	// ═══ Parent Constraint ═══════════════════════════════════════════════════

	void RigNode_ParentConstraint::Execute(RigExecContext& ctx)
	{
		if (!ctx.Hierarchy)
			return;

		const int child = ReadItem(ctx, 0);

		if (child < 0)
			return;

		const float weight = glm::clamp(ReadFloat(ctx, 3), 0.0f, 1.0f);

		if (weight <= 0.0001f)
			return;

		const bool maintain = ReadBool(ctx, 1);
		const RigPinValue parents = Read(ctx, 2);

		if (parents.Items.empty())
		{
			if (!m_WarnedEmpty)
			{
				m_WarnedEmpty = true;

				AXE_CORE_WARN("Parent Constraint '{}': a lista de pais esta vazia — "
					"o no nao vai fazer nada. Ligue um Item Array no pino Parents.",
					Title);
			}

			return;
		}

		RigHierarchy& h = *ctx.Hierarchy;

		const glm::mat4 childInit = h.GetInitialGlobal(child);

		glm::vec3 pos(0.0f);
		glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 scl(1.0f);

		int found = 0;

		for (const auto& ref : parents.Items)
		{
			const int p = ResolveRef(h, ref);

			if (p < 0)
				continue;

			glm::mat4 target = h.GetGlobal(p);

			if (maintain)
			{
				// A FOLGA de repouso: onde o filho estava EM RELACAO ao pai
				// quando os dois nasceram. Reaplicada sobre a posicao ATUAL do
				// pai, ela faz o filho acompanhar sem colar em cima dele.
				target = target * (glm::inverse(h.GetInitialGlobal(p)) * childInit);
			}

			const BoneTransform t = BoneTransform::FromMatrix(target);

			++found;

			if (found == 1)
			{
				pos = t.Translation;
				rot = t.Rotation;
				scl = t.Scale;
			}
			else
			{
				// Media incremental: com peso 1/n a cada novo pai, o resultado
				// e a media exata de todos, sem precisar guardar a lista.
				const float a = 1.0f / (float)found;

				pos = glm::mix(pos, t.Translation, a);
				rot = glm::slerp(rot, t.Rotation, a);
				scl = glm::mix(scl, t.Scale, a);
			}
		}

		if (found == 0)
			return;

		// Peso parcial mistura com onde o filho ja esta — e o que permite
		// atenuar a restricao em vez de so ligar e desligar.
		const BoneTransform cur = BoneTransform::FromMatrix(h.GetGlobal(child));

		BoneTransform result;
		result.Translation = glm::mix(cur.Translation, pos, weight);
		result.Rotation = glm::slerp(cur.Rotation, rot, weight);
		result.Scale = glm::mix(cur.Scale, scl, weight);

		h.SetGlobal(child, result.ToMatrix(), true);
	}

	// ═══ Float Math ══════════════════════════════════════════════════════════

	RigNode_FloatMath::RigNode_FloatMath()
	{
		Title = "Float Math";

		AddInFloat("A", 0.0f);
		AddInFloat("B", 0.0f);
		AddInFloat("C", 0.0f);

		AddOut("Result", RigPinType::Float);

		ApplyOperation();
	}

	void RigNode_FloatMath::ApplyOperation()
	{
		// Defensivo: o Deserialize chama isto, e um arquivo corrompido podia
		// chegar aqui antes dos pinos existirem.
		if (Inputs.size() < 3)
			return;

		switch (Operation)
		{
		case Op::Clamp:
			Inputs[0].Name = "Value";
			Inputs[1].Name = "Min";
			Inputs[2].Name = "Max";
			break;

		case Op::Lerp:
			Inputs[0].Name = "A";
			Inputs[1].Name = "B";
			Inputs[2].Name = "Alpha";
			break;

		case Op::Abs:
			Inputs[0].Name = "A";
			Inputs[1].Name = "-";
			Inputs[2].Name = "-";
			break;

		default:
			Inputs[0].Name = "A";
			Inputs[1].Name = "B";
			Inputs[2].Name = "-";
			break;
		}
	}

	void RigNode_FloatMath::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		(void)pin;

		out = RigPinValue{};

		const float a = ReadFloat(ctx, 0);
		const float b = ReadFloat(ctx, 1);
		const float c = ReadFloat(ctx, 2);

		switch (Operation)
		{
		case Op::Add:      out.Float = a + b; break;
		case Op::Subtract: out.Float = a - b; break;
		case Op::Multiply: out.Float = a * b; break;

			// Divisao por zero devolve A em vez de inf/nan. Um nan entrando num
			// transform contamina a pose inteira, e o sintoma e o personagem
			// SUMIR da tela — muito pior de rastrear que um valor inesperado.
		case Op::Divide:   out.Float = (glm::abs(b) > 1e-6f) ? (a / b) : a; break;

		case Op::Min:      out.Float = glm::min(a, b); break;
		case Op::Max:      out.Float = glm::max(a, b); break;

			// Min e Max trocados dariam resultado sem sentido em silencio; ordena.
		case Op::Clamp:    out.Float = glm::clamp(a, glm::min(b, c), glm::max(b, c)); break;

		case Op::Lerp:     out.Float = glm::mix(a, b, glm::clamp(c, 0.0f, 1.0f)); break;
		case Op::Abs:      out.Float = glm::abs(a); break;
		}
	}

	void RigNode_FloatMath::Serialize(nlohmann::json& j) const
	{
		j["op"] = (int)Operation;
	}

	void RigNode_FloatMath::Deserialize(const nlohmann::json& j)
	{
		Operation = (Op)j.value("op", 0);

		// Os nomes dos pinos seguem a operacao — sem isto um Clamp salvo
		// reabriria mostrando "A / B / -".
		ApplyOperation();
	}

	// ═══ Select Float ════════════════════════════════════════════════════════

	RigNode_SelectFloat::RigNode_SelectFloat()
	{
		Title = "Select Float";

		AddInBool("Condition", false);
		AddInFloat("True", 1.0f);
		AddInFloat("False", 0.0f);

		AddOut("Result", RigPinType::Float);
	}

	void RigNode_SelectFloat::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		(void)pin;

		out = RigPinValue{};
		out.Float = ReadBool(ctx, 0) ? ReadFloat(ctx, 1) : ReadFloat(ctx, 2);
	}

	// ═══ Damp Float / Damp Vector ════════════════════════════════════════════

	RigNode_DampFloat::RigNode_DampFloat()
	{
		Title = "Damp Float";

		AddInFloat("Value", 0.0f);

		// 12/s e o default calibrado na pratica no AnimNode_FootIK.
		AddInFloat("Speed", 12.0f);

		AddOut("Result", RigPinType::Float);
	}

	void RigNode_DampFloat::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		(void)pin;

		out = RigPinValue{};

		const float target = ReadFloat(ctx, 0);
		const float speed = ReadFloat(ctx, 1);

		if (!m_Init)
		{
			// PRIMEIRO frame entra DIRETO no valor.
			m_Value = target;
			m_Init = true;
		}
		else if (speed <= 0.0f)
		{
			// Suavizacao desligada = passa direto. Assim da pra comparar com e
			// sem suavizacao sem desmontar o fio.
			m_Value = target;
		}
		else if (ctx.DeltaTime > 0.0f)
		{
			// k = 1 - exp(-speed*dt): perseguicao exponencial INDEPENDENTE do
			// frame rate. Um lerp de fator fixo mudaria de velocidade conforme
			// o FPS, e o rig ficaria mais macio no PC lento — o tipo de bug
			// que so aparece na maquina de outra pessoa.
			const float k = 1.0f - std::exp(-speed * ctx.DeltaTime);
			m_Value += (target - m_Value) * k;
		}
		// dt == 0 (preview pausado): congela onde esta, nao salta.

		out.Float = m_Value;
	}

	RigNode_DampVector::RigNode_DampVector()
	{
		Title = "Damp Vector";

		AddIn("Value", RigPinType::Vector);
		AddInFloat("Speed", 12.0f);

		AddOut("Result", RigPinType::Vector);
	}

	void RigNode_DampVector::EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
	{
		(void)pin;

		out = RigPinValue{};

		const glm::vec3 target = ReadVector(ctx, 0);
		const float     speed = ReadFloat(ctx, 1);

		if (!m_Init)
		{
			m_Value = target;
			m_Init = true;
		}
		else if (speed <= 0.0f)
		{
			m_Value = target;
		}
		else if (ctx.DeltaTime > 0.0f)
		{
			const float k = 1.0f - std::exp(-speed * ctx.DeltaTime);
			m_Value = glm::mix(m_Value, target, k);
		}

		out.Vector = m_Value;
	}

	// ═══ Align To Vector ═════════════════════════════════════════════════════

	RigNode_AlignToVector::RigNode_AlignToVector()
	{
		Title = "Align To Vector";
		HasExecIn = true;
		ExecOut.push_back("");

		AddInItem("Item", RigElementType::Bone);

		AddIn("From", RigPinType::Vector);
		AddIn("To", RigPinType::Vector);

		// 45 graus: normal absurda de quina de colisor nao vira o pe de cabeca
		// pra baixo. Mesma trava do AnimNode_FootIK.
		AddInFloat("Max Angle", 45.0f);
		AddInFloat("Weight", 1.0f);

		// From e To comecam os DOIS em +Y: assim o no recem-criado e um no-op
		// (a rotacao entre um vetor e ele mesmo e identidade) em vez de torcer
		// o osso antes de voce ligar qualquer fio.
		Inputs[1].Default.Vector = glm::vec3(0.0f, 1.0f, 0.0f);
		Inputs[2].Default.Vector = glm::vec3(0.0f, 1.0f, 0.0f);
	}

	void RigNode_AlignToVector::Execute(RigExecContext& ctx)
	{
		if (!ctx.Hierarchy)
			return;

		const int i = ReadItem(ctx, 0);

		if (i < 0)
			return;

		const float weight = glm::clamp(ReadFloat(ctx, 4), 0.0f, 1.0f);

		if (weight <= 0.0001f)
			return;

		glm::vec3 from = ReadVector(ctx, 1);
		glm::vec3 to = ReadVector(ctx, 2);

		const float lenFrom = glm::length(from);
		const float lenTo = glm::length(to);

		// Vetor nulo nao tem direcao: girar por ele produz quaternion invalido
		// e a pose sai com nan. Avisa UMA vez — num solve por frame, avisar
		// sempre encheria o console em um segundo.
		if (lenFrom < 1e-6f || lenTo < 1e-6f)
		{
			if (!m_WarnedDegenerate)
			{
				m_WarnedDegenerate = true;

				AXE_CORE_WARN("Align To Vector '{}': From ou To e um vetor nulo, "
					"o no nao fez nada. Ligue a Normal de um Ground Trace no pino To.",
					Title);
			}

			return;
		}

		from /= lenFrom;
		to /= lenTo;

		const float d = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);

		// Ja alinhados: girar por um angulo de ~0 e so ruido numerico.
		if (d > 0.99999f)
			return;

		const float maxAngle = glm::radians(glm::max(ReadFloat(ctx, 3), 0.0f));

		float angle = std::acos(d);
		angle = glm::min(angle, maxAngle);

		if (angle < 1e-6f)
			return;

		// Eixo do giro. Com os vetores OPOSTOS o cross e nulo (eixo indefinido)
		// — ai qualquer perpendicular serve, e escolher uma e melhor que
		// devolver nan.
		glm::vec3 axis = glm::cross(from, to);

		if (glm::length(axis) < 1e-6f)
		{
			axis = glm::cross(from, glm::vec3(1.0f, 0.0f, 0.0f));

			if (glm::length(axis) < 1e-6f)
				axis = glm::cross(from, glm::vec3(0.0f, 0.0f, 1.0f));
		}

		axis = glm::normalize(axis);

		const glm::quat delta = glm::angleAxis(angle * weight, axis);

		RigHierarchy& h = *ctx.Hierarchy;

		// Gira EM TORNO DA PROPRIA POSICAO do elemento: zera a translacao,
		// gira, devolve. Girar a matriz inteira arrastaria o elemento num arco
		// ao redor da origem do rig — o osso sairia voando pro lado.
		glm::mat4 g = h.GetGlobal(i);

		const glm::vec3 pos = glm::vec3(g[3]);

		g[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

		glm::mat4 ng = glm::toMat4(delta) * g;
		ng[3] = glm::vec4(pos, 1.0f);

		h.SetGlobal(i, ng, true);
	}

	// ═══ Control Follow Bone ═════════════════════════════════════════════════

	RigNode_ControlFollowBone::RigNode_ControlFollowBone()
	{
		Title = "Control Follow Bone";
		HasExecIn = true;
		ExecOut.push_back("");

		AddInItem("Control", RigElementType::Control);
		AddInItem("Bone", RigElementType::Bone);

		// Weight 1 = o controle acompanha o osso inteiro. Em 0 ele fica no
		// repouso autorado, que e o comportamento antigo — da pra comparar os
		// dois sem desmontar fio.
		AddInFloat("Weight", 1.0f);
	}

	void RigNode_ControlFollowBone::Execute(RigExecContext& ctx)
	{
		if (!ctx.Hierarchy)
			return;

		RigHierarchy& h = *ctx.Hierarchy;

		const int ctrl = ReadItem(ctx, 0);

		if (ctrl < 0)
			return;

		int bone = ReadItem(ctx, 1);

		if (bone < 0)
		{
			// Pino do osso vazio: cai no osso de ORIGEM do controle. Assim o
			// caso comum ("controle criado sobre o osso") nao pede configuracao.
			const std::string& src = h.GetElements()[ctrl].SourceBone;

			if (!src.empty())
				bone = h.Find(src, RigElementType::Bone);
		}

		if (bone < 0)
		{
			if (!m_Warned)
			{
				m_Warned = true;

				AXE_CORE_WARN("Control Follow Bone '{}': nenhum osso resolvido. "
					"Escolha o osso no pino Bone (o controle deste rig nao tem osso "
					"de origem gravado).", Title);
			}

			return;
		}

		const float weight = glm::clamp(ReadFloat(ctx, 2), 0.0f, 1.0f);

		if (weight <= 0.0001f)
			return;

		// A correcao AUTORADA e a relacao de repouso entre os dois. Ela sai do
		// Initial de proposito: e ali que o gizmo do editor grava, entao e ali
		// que vive a intencao do animador.
		const glm::mat4 delta =
			glm::inverse(h.GetInitialGlobal(bone)) * h.GetInitialGlobal(ctrl);

		const glm::mat4 target = h.GetGlobal(bone) * delta;

		if (weight >= 0.9999f)
		{
			h.SetGlobal(ctrl, target, true);
			return;
		}

		// Blend parcial: interpola em TRS, nao na matriz. Interpolar matriz
		// linearmente produz shear e encolhe o elemento — mesma razao pela qual
		// a Pose do engine e local e nao mat4.
		const BoneTransform a = BoneTransform::FromMatrix(h.GetGlobal(ctrl));
		const BoneTransform b = BoneTransform::FromMatrix(target);

		BoneTransform r;
		r.Translation = glm::mix(a.Translation, b.Translation, weight);
		r.Rotation = glm::slerp(a.Rotation, b.Rotation, weight);
		r.Scale = glm::mix(a.Scale, b.Scale, weight);

		h.SetGlobal(ctrl, r.ToMatrix(), true);
	}

	// ═══ Fabrica ═════════════════════════════════════════════════════════════

	std::unique_ptr<RigNode> CreateRigNode(const std::string& t)
	{
		if (t == "ForwardsSolve")  return std::make_unique<RigNode_ForwardsSolve>();
		if (t == "Sequence")       return std::make_unique<RigNode_Sequence>();
		if (t == "Branch")         return std::make_unique<RigNode_Branch>();
		if (t == "ForEach")        return std::make_unique<RigNode_ForEach>();
		if (t == "ProjectToNewParent") return std::make_unique<RigNode_ProjectToNewParent>();
		if (t == "GetTransform")   return std::make_unique<RigNode_GetTransform>();
		if (t == "SetTransform")   return std::make_unique<RigNode_SetTransform>();
		if (t == "TwoBoneIK")      return std::make_unique<RigNode_TwoBoneIK>();
		if (t == "GroundTrace")    return std::make_unique<RigNode_GroundTrace>();
		if (t == "MakeTransform")  return std::make_unique<RigNode_MakeTransform>();
		if (t == "BreakTransform") return std::make_unique<RigNode_BreakTransform>();
		if (t == "VectorOp")       return std::make_unique<RigNode_VectorOp>();
		if (t == "FloatMath")      return std::make_unique<RigNode_FloatMath>();
		if (t == "SelectFloat")    return std::make_unique<RigNode_SelectFloat>();
		if (t == "DampFloat")      return std::make_unique<RigNode_DampFloat>();
		if (t == "DampVector")     return std::make_unique<RigNode_DampVector>();
		if (t == "AlignToVector")  return std::make_unique<RigNode_AlignToVector>();
		if (t == "ControlFollowBone") return std::make_unique<RigNode_ControlFollowBone>();
		if (t == "ItemArray")      return std::make_unique<RigNode_ItemArray>();
		if (t == "At")             return std::make_unique<RigNode_At>();
		if (t == "GetControlValue") return std::make_unique<RigNode_GetControlValue>();
		if (t == "FKChain")        return std::make_unique<RigNode_FKChain>();
		if (t == "ParentConstraint") return std::make_unique<RigNode_ParentConstraint>();
		if (t == "HideControls")   return std::make_unique<RigNode_HideControls>();
		if (t == "Reroute")        return std::make_unique<RigNode_Reroute>();

		// Compatibilidade: arquivos salvos quando o reroute de execucao era um
		// no separado. Vira o unificado ja no modo certo, em vez de sumir.
		if (t == "RerouteExec")
		{
			auto r = std::make_unique<RigNode_Reroute>();
			r->SetMode(RigNode_Reroute::Mode::Exec);
			return r;
		}

		if (t == "Comment")        return std::make_unique<RigNode_Comment>();

		AXE_CORE_WARN("CreateRigNode: tipo desconhecido '{}'.", t);
		return nullptr;
	}

} // namespace axe