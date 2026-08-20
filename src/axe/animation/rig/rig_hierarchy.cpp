#include "rig_hierarchy.hpp"
#include "axe/log/log.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace axe
{
	namespace
	{
		// Sufixo apos o ultimo separador de namespace. "mixamorig:LeftFoot" ->
		// "LeftFoot". Ver FindFlexible.
		std::string SuffixOf(const std::string& s)
		{
			const std::size_t p = s.find_last_of(":|");
			return (p == std::string::npos) ? s : s.substr(p + 1);
		}

		// Comparacao EXATA de proposito: Value so sai da identidade por
		// atribuicao (gizmo, load do asset, Sequencer), nunca por acumulo
		// numerico. Uma tolerancia aqui so serviria pra mascarar um bug de
		// outro lugar.
		//
		// Precisa morar AQUI, no topo: HasValue e ResetToInitial usam esta
		// funcao e estao em pontos distantes do arquivo. Definida junto do
		// segundo uso, o primeiro nao a enxerga — em C++ isso e erro duro, e o
		// sintoma no MSVC e enganoso: a TU inteira falha, a dll e linkada a
		// partir do .obj ANTERIOR, e o editor reclama de "unresolved external"
		// nos simbolos novos, apontando pra um problema de link que nao existe.
		bool IsIdentityTransform(const BoneTransform& t)
		{
			return t.Translation == glm::vec3(0.0f)
				&& t.Scale == glm::vec3(1.0f)
				&& t.Rotation.w == 1.0f
				&& t.Rotation.x == 0.0f
				&& t.Rotation.y == 0.0f
				&& t.Rotation.z == 0.0f;
		}
	}

	int RigHierarchy::Add(const std::string& name, RigElementType type, int parent)
	{
		if (name.empty())
		{
			AXE_CORE_WARN("RigHierarchy: elemento sem nome recusado.");
			return -1;
		}

		if (Find(name, type) >= 0)
		{
			AXE_CORE_WARN("RigHierarchy: ja existe um elemento '{}' deste tipo.", name);
			return -1;
		}

		// O pai precisa vir ANTES: e o que mantem a ordem topologica de que o
		// loop de globais depende.
		if (parent >= (int)m_Elements.size())
		{
			AXE_CORE_WARN("RigHierarchy: pai invalido para '{}'.", name);
			return -1;
		}

		RigElement e;
		e.Name = name;
		e.Type = type;
		e.Parent = parent;

		m_Elements.push_back(std::move(e));
		MarkDirty();

		return (int)m_Elements.size() - 1;
	}

	void RigHierarchy::Clear()
	{
		m_Elements.clear();
		m_Globals.clear();
		MarkDirty();
	}

	std::vector<int> RigHierarchy::CollectDescendants(int index) const
	{
		std::vector<int> out;

		if (index < 0 || index >= (int)m_Elements.size())
			return out;

		// Um unico passe pra frente basta: o pai sempre tem indice menor, entao
		// quando chegamos num filho o pai dele ja foi classificado.
		std::vector<bool> inSubtree(m_Elements.size(), false);
		inSubtree[index] = true;

		for (std::size_t i = index + 1; i < m_Elements.size(); ++i)
		{
			const int p = m_Elements[i].Parent;

			if (p >= 0 && inSubtree[p])
			{
				inSubtree[i] = true;
				out.push_back((int)i);
			}
		}

		return out;
	}

	// ═══ Pose do animador (Value) ════════════════════════════════════════════

	void RigHierarchy::SetValueFromGlobal(int index, const glm::mat4& wanted)
	{
		if (index < 0 || index >= (int)m_Elements.size())
			return;

		// Osso nao tem pose de animador: ele RECEBE a animacao via ApplyPose e
		// e saida do solve. Aceitar aqui criaria um segundo dono do mesmo dado.
		if (m_Elements[(std::size_t)index].Type == RigElementType::Bone)
			return;

		const glm::mat4 now = GetGlobal(index);
		const glm::mat4 val = m_Elements[(std::size_t)index].Value.ToMatrix();

		m_Elements[(std::size_t)index].Value =
			BoneTransform::FromMatrix(val * glm::inverse(now) * wanted);

		// Current acompanha na hora, senao o gizmo "escorregaria" ate o proximo
		// solve — voce arrastaria e a forma ficaria pra tras um frame.
		m_Elements[(std::size_t)index].Current = BoneTransform::FromMatrix(
			m_Elements[(std::size_t)index].Initial.ToMatrix()
			* m_Elements[(std::size_t)index].Value.ToMatrix());

		MarkDirty();
	}

	void RigHierarchy::ClearValue(int index)
	{
		if (index < 0 || index >= (int)m_Elements.size())
			return;

		m_Elements[(std::size_t)index].Value = BoneTransform{};
		m_Elements[(std::size_t)index].Current = m_Elements[(std::size_t)index].Initial;

		MarkDirty();
	}

	bool RigHierarchy::HasValue(int index) const
	{
		if (index < 0 || index >= (int)m_Elements.size())
			return false;

		return !IsIdentityTransform(m_Elements[(std::size_t)index].Value);
	}

	// ═══ Voltar ao repouso conhecido ═════════════════════════════════════════

	int RigHierarchy::ResetBonesToBindPose(const Skeleton& skeleton)
	{
		const std::vector<Bone>& bones = skeleton.GetBones();

		int restored = 0;

		for (RigElement& e : m_Elements)
		{
			if (e.Type != RigElementType::Bone)
				continue;

			int b = skeleton.FindBone(e.Name);

			// Tolerante a prefixo de namespace, pela mesma razao do
			// FindFlexible: o rig pode ter sido montado com "LeftFoot" e o
			// esqueleto trazer "mixamorig:LeftFoot".
			if (b < 0)
			{
				for (std::size_t k = 0; k < bones.size(); ++k)
				{
					const std::string& bn = bones[k].Name;
					const std::size_t  at = bn.rfind(':');

					if (at != std::string::npos && bn.substr(at + 1) == e.Name)
					{
						b = (int)k;
						break;
					}
				}
			}

			if (b < 0)
				continue;

			e.Initial = BoneTransform::FromMatrix(bones[(std::size_t)b].LocalBindPose);
			e.Current = e.Initial;

			++restored;
		}

		MarkDirty();
		return restored;
	}

	bool RigHierarchy::ResetToSourceBone(int index)
	{
		if (index < 0 || index >= (int)m_Elements.size())
			return false;

		RigElement& e = m_Elements[index];

		if (e.Type == RigElementType::Bone || e.SourceBone.empty())
			return false;

		const int bone = Find(e.SourceBone, RigElementType::Bone);

		if (bone < 0)
		{
			AXE_CORE_WARN("RigHierarchy: '{}' nasceu do osso '{}', que nao existe "
				"mais na hierarquia.", e.Name, e.SourceBone);
			return false;
		}

		SetInitialGlobal(index, GetInitialGlobal(bone));

		m_Elements[index].Current = m_Elements[index].Initial;

		MarkDirty();
		return true;
	}

	int RigHierarchy::SnapControlsToCurrentBones()
	{
		EnsureGlobals();

		const std::size_t n = m_Elements.size();
		if (n == 0)
			return 0;

		// Globais de ANTES do snap. Congelados porque o laco abaixo escreve
		// locais enquanto le globais: sem a copia, mover um controle mudaria o
		// alvo que o proximo iria ler.
		const std::vector<glm::mat4> g0 = m_Globals;

		// Globais de REPOUSO (Initial), num unico passe pra frente — a ordem
		// topologica garante que o pai ja esta pronto quando chegamos no filho.
		std::vector<glm::mat4> initG(n);

		for (std::size_t i = 0; i < n; ++i)
		{
			const glm::mat4 local = m_Elements[i].Initial.ToMatrix();
			const int p = m_Elements[i].Parent;
			initG[i] = (p < 0) ? local : initG[p] * local;
		}

		// ── DUAS SAIDAS POR ELEMENTO, E ELAS PRECISAM SER DUAS ───────────────
		//
		//   rest[i] — onde o elemento fica com Value NEUTRO. E a "nova bind
		//             pose", agora acompanhando a animacao.
		//   cur[i]  — onde ele fica de fato, ja com o Value do animador E com o
		//             dos ancestrais.
		//
		// Calcular so `cur` (que era o que esta funcao fazia antes) tinha um
		// efeito colateral grave: cada controle era fixado no proprio osso, um
		// por um, e isso APAGAVA o movimento que o pai tinha acabado de
		// propagar. Girar ctrl_Spine nao mexia em ctrl_Spine1 — a hierarquia
		// dos controles simplesmente deixava de existir.
		//
		// Separando os dois, o local de repouso do filho e medido contra o
		// REPOUSO do pai, e o global final e composto contra o CURRENT do pai:
		//
		//     cur[i] = cur[pai] * (inverse(rest[pai]) * rest[i]) * Value[i]
		//
		// Com todos os Value neutros isto colapsa em cur[i] == rest[i] — os
		// controles pousam exatos na animacao. Com o pai posado, o filho vai
		// junto, que e o comportamento de qualquer rig.
		std::vector<glm::mat4> rest(n, glm::mat4(1.0f));
		std::vector<glm::mat4> cur(n, glm::mat4(1.0f));

		int moved = 0;

		for (std::size_t i = 0; i < n; ++i)
		{
			RigElement& e = m_Elements[i];

			// Osso nao segue osso: ele JA e a animacao. Aceitar aqui criaria um
			// segundo dono do mesmo dado — a mesma razao pela qual o
			// SetValueFromGlobal recusa Bone.
			if (e.Type == RigElementType::Bone)
			{
				rest[i] = initG[i];
				cur[i] = g0[i];
				continue;
			}

			const int p = e.Parent;

			// Pai fora do nosso conjunto (raiz, ou um OSSO): ele nao se move, e
			// os dois referenciais sao o mesmo — o global que ele ja tem.
			const bool parentHandled = (p >= 0 && m_Elements[p].Type != RigElementType::Bone);

			const glm::mat4 restP = (p < 0) ? glm::mat4(1.0f)
				: (parentHandled ? rest[p] : g0[p]);
			const glm::mat4 curP = (p < 0) ? glm::mat4(1.0f)
				: (parentHandled ? cur[p] : g0[p]);

			const int bone = e.SourceBone.empty()
				? -1 : Find(e.SourceBone, RigElementType::Bone);

			if (bone >= 0)
			{
				// ── O OFFSET AUTORADO E PRESERVADO ───────────────────────────
				//
				// Nao se cola o controle EM CIMA do osso: cola-se ele na mesma
				// posicao relativa que o autor lhe deu, agora medida a partir do
				// osso animado.
				//
				// Para um controle de FK, montado exatamente sobre o osso, o
				// offset e a identidade e o resultado e o mesmo de antes.
				//
				// Para um POLE VECTOR e outra historia, e e a diferenca entre
				// funcionar e nao funcionar: um pole vector vive DESLOCADO da
				// junta (30cm atras do cotovelo). Colado em cima dela, o Two
				// Bone IK fica com a direcao do polo degenerada e o membro
				// torce para um lado arbitrario.
				const glm::mat4 offset = glm::inverse(initG[bone]) * initG[i];

				rest[i] = g0[bone] * offset;
				++moved;
			}
			else
			{
				// Sem osso de origem: mantem o repouso autorado, relativo ao pai
				// — que pode ter se movido. Nao se adivinha um osso.
				rest[i] = restP * e.Initial.ToMatrix();
			}

			cur[i] = curP * (glm::inverse(restP) * rest[i]) * e.Value.ToMatrix();

			// LOCAL, e nao SetGlobal: o pai deste elemento ja foi escrito neste
			// mesmo laco, entao o local sai direto de cur[pai]. SetGlobal
			// recalcularia o cache de globais a cada chamada — O(n^2) sem
			// necessidade — e leria um estado meio atualizado.
			m_Elements[i].Current = BoneTransform::FromMatrix(
				glm::inverse(curP) * cur[i]);
		}

		MarkDirty();
		return moved;
	}

	// ═══ Espelhamento ════════════════════════════════════════════════════════

	namespace
	{
		// ONDE o token pode aparecer. Nao e preciosismo: "l_" solto no meio de
		// um nome casa dentro de "Null_1", e o espelho viraria "Nulr_1". Um
		// prefixo so vale no comeco; um sufixo, so no fim.
		enum class MirrorWhere
		{
			Anywhere,
			Prefix,
			Suffix
		};

		struct MirrorToken
		{
			const char* A;
			const char* B;
			MirrorWhere Where;
		};

		// Testados NESTA ORDEM — do mais especifico pro mais curto. As
		// palavras inteiras precisam ganhar dos sufixos de uma letra, senao
		// "Left_arm" seria resolvido pelo "_l" errado.
		//
		// Cobre as convencoes que aparecem na pratica: Mixamo e Unreal usam
		// Left/Right, Blender usa .L/.R, e rig de estudio costuma usar _L/_R
		// ou l_/r_.
		const MirrorToken kMirrorTokens[] =
		{
			{ "Left",  "Right", MirrorWhere::Anywhere },
			{ "left",  "right", MirrorWhere::Anywhere },
			{ "LEFT",  "RIGHT", MirrorWhere::Anywhere },
			{ "_L",    "_R",    MirrorWhere::Suffix   },
			{ "_l",    "_r",    MirrorWhere::Suffix   },
			{ ".L",    ".R",    MirrorWhere::Suffix   },
			{ ".l",    ".r",    MirrorWhere::Suffix   },
			{ "L_",    "R_",    MirrorWhere::Prefix   },
			{ "l_",    "r_",    MirrorWhere::Prefix   },
		};

		// Troca `a` por `b` respeitando a posicao exigida. Devolve vazio quando
		// nao houve troca — e assim que o chamador sabe que este par nao serve
		// e pode tentar o proximo.
		std::string SwapToken(const std::string& in, const std::string& a,
			const std::string& b, MirrorWhere where)
		{
			if (a.empty() || in.size() < a.size())
				return std::string();

			if (where == MirrorWhere::Prefix)
			{
				if (in.compare(0, a.size(), a) != 0)
					return std::string();

				return b + in.substr(a.size());
			}

			if (where == MirrorWhere::Suffix)
			{
				const std::size_t at = in.size() - a.size();

				if (in.compare(at, a.size(), a) != 0)
					return std::string();

				return in.substr(0, at) + b;
			}

			if (in.find(a) == std::string::npos)
				return std::string();

			// TODAS as ocorrencias. "LeftHandLeftThumb" tem lado nos dois
			// pedacos, e trocar so o primeiro daria um nome hibrido que nao
			// corresponde a nada.
			std::string out;
			out.reserve(in.size());

			std::size_t i = 0;

			while (i < in.size())
			{
				if (i + a.size() <= in.size() && in.compare(i, a.size(), a) == 0)
				{
					out += b;
					i += a.size();
				}
				else
				{
					out += in[i];
					++i;
				}
			}

			return out;
		}
	}

	std::string RigHierarchy::MirrorName(const std::string& name,
		const std::string& search, const std::string& replace)
	{
		if (name.empty())
			return std::string();

		// Par explicito manda, e vale nos dois sentidos.
		if (!search.empty() && !replace.empty())
		{
			std::string r = SwapToken(name, search, replace, MirrorWhere::Anywhere);

			if (!r.empty())
				return r;

			return SwapToken(name, replace, search, MirrorWhere::Anywhere);
		}

		for (const auto& t : kMirrorTokens)
		{
			std::string r = SwapToken(name, t.A, t.B, t.Where);

			if (!r.empty())
				return r;

			r = SwapToken(name, t.B, t.A, t.Where);

			if (!r.empty())
				return r;
		}

		return std::string();
	}

	int RigHierarchy::Mirror(int index, const RigMirrorSettings& settings)
	{
		if (index < 0 || index >= (int)m_Elements.size())
			return -1;

		if (m_Elements[index].Type == RigElementType::Bone)
		{
			AXE_CORE_WARN("RigHierarchy: Bone nao se espelha — o lado oposto ja "
				"vem do esqueleto.");
			return -1;
		}

		const int axis = glm::clamp(settings.Axis, 0, 2);

		// ── A REFLEXAO ───────────────────────────────────────────────────────
		//
		// S troca o sinal de um eixo. G' = S * G * S reflete o frame INTEIRO:
		// a translacao vira S*t, e a rotacao vira S*R*S — que continua sendo
		// uma rotacao propria (o determinante -1 do S aparece duas vezes e se
		// cancela).
		//
		// So espelhar a POSICAO daria o controle do outro lado apontando pro
		// lado errado: o anel do pe direito nasceria virado ao contrario.
		glm::mat4 S(1.0f);
		S[axis][axis] = -1.0f;

		// Ordem topologica garantida: `index` primeiro, descendentes em ordem
		// crescente. Isso importa porque o espelho de um filho precisa que o
		// espelho do PAI ja exista pra se pendurar nele.
		std::vector<int> sources;
		sources.push_back(index);

		if (settings.IncludeChildren)
		{
			const std::vector<int> kids = CollectDescendants(index);
			sources.insert(sources.end(), kids.begin(), kids.end());
		}

		int result = -1;

		for (const int src : sources)
		{
			// Add() so ACRESCENTA no fim, entao os indices de origem seguem
			// validos durante o laco inteiro. Se um dia Add passar a inserir
			// no meio, isto aqui quebra — e o comentario existe pra que a
			// quebra seja encontrada.
			const RigElement source = m_Elements[src];

			if (source.Type == RigElementType::Bone)
				continue;

			const std::string name =
				MirrorName(source.Name, settings.Search, settings.Replace);

			if (name.empty() || name == source.Name)
			{
				AXE_CORE_WARN("RigHierarchy: '{}' nao tem lado no nome (Left/Right, "
					"_L/_R, .l/.r) — nao da pra espelhar.", source.Name);
				continue;
			}

			// ── ONDE PENDURAR O ESPELHO ──────────────────────────────────────
			//
			// No espelho do pai, quando existir. Um controle de pe esquerdo
			// pendurado no osso LeftFoot tem que virar um controle de pe
			// direito pendurado no RightFoot — herdar o pai original deixaria
			// o lado direito preso ao esquerdo, e o rig andaria de lado.
			//
			// Sem contraparte (o pai e a raiz, ou o Hips), fica no mesmo pai.
			int parent = source.Parent;

			if (parent >= 0)
			{
				const std::string mirroredParent =
					MirrorName(m_Elements[parent].Name, settings.Search, settings.Replace);

				if (!mirroredParent.empty() && mirroredParent != m_Elements[parent].Name)
				{
					const int p = Find(mirroredParent, m_Elements[parent].Type);

					if (p >= 0)
						parent = p;
				}
			}

			int dst = Find(name, source.Type);

			if (dst < 0)
				dst = Add(name, source.Type, parent);

			if (dst < 0)
				continue;

			// Tudo que e autoral vem junto: forma, cor, tamanho e o offset do
			// desenho. O offset NAO e refletido — ele e local ao elemento, e o
			// elemento ja esta espelhado, entao refletir de novo desfaria.
			RigElement& mirror = m_Elements[dst];

			mirror.ValueType = source.ValueType;
			mirror.BoolValue = source.BoolValue;
			mirror.FloatValue = source.FloatValue;
			mirror.Shape = source.Shape;
			mirror.ShapeColor = source.ShapeColor;
			mirror.ShapeSize = source.ShapeSize;
			mirror.ShapeOffset = source.ShapeOffset;

			// O osso de origem tambem tem lado.
			if (!source.SourceBone.empty())
			{
				const std::string bone =
					MirrorName(source.SourceBone, settings.Search, settings.Replace);

				mirror.SourceBone = bone.empty() ? source.SourceBone : bone;
			}

			// Reflete o GLOBAL e deixa o SetInitialGlobal converter de volta
			// pro local do pai novo — que e o passo que faz o espelho cair no
			// lugar certo mesmo com pai diferente.
			const glm::mat4 g = GetInitialGlobal(src);

			SetInitialGlobal(dst, S * g * S);

			m_Elements[dst].Current = m_Elements[dst].Initial;

			if (src == index)
				result = dst;
		}

		MarkDirty();
		return result;
	}


	int RigHierarchy::Remove(int index)
	{
		if (index < 0 || index >= (int)m_Elements.size())
			return 0;

		std::vector<bool> doomed(m_Elements.size(), false);
		doomed[index] = true;

		for (int d : CollectDescendants(index))
			doomed[d] = true;

		// old -> new. -1 marca quem morreu.
		std::vector<int> remap(m_Elements.size(), -1);

		std::vector<RigElement> kept;
		kept.reserve(m_Elements.size());

		for (std::size_t i = 0; i < m_Elements.size(); ++i)
		{
			if (doomed[i])
				continue;

			remap[i] = (int)kept.size();
			kept.push_back(std::move(m_Elements[i]));
		}

		const int removed = (int)m_Elements.size() - (int)kept.size();

		// REINDEXA os pais. Sem isto o rig passa a mexer no osso errado, em
		// silencio — ver o comentario no header.
		for (auto& e : kept)
			if (e.Parent >= 0)
				e.Parent = remap[e.Parent];

		m_Elements = std::move(kept);
		MarkDirty();

		return removed;
	}

	int RigHierarchy::Reparent(int index, int newParent)
	{
		const int n = (int)m_Elements.size();

		if (index < 0 || index >= n || newParent >= n || newParent == index)
			return -1;

		if (m_Elements[index].Parent == newParent)
			return -1;

		// CICLO: o novo pai nao pode estar dentro do galho que estamos movendo.
		// Sem esta guarda a hierarquia fica sem raiz e QUALQUER loop que a
		// percorre trava — inclusive o do proprio solve.
		for (int p = newParent; p >= 0; p = m_Elements[p].Parent)
			if (p == index)
			{
				AXE_CORE_WARN("RigHierarchy: '{}' nao pode virar filho do proprio "
					"descendente.", m_Elements[index].Name);
				return -1;
			}

		// A pose e preservada: guardamos onde o elemento ESTA antes de trocar o
		// pai e devolvemos depois. Sem isso ele saltaria pra outro lugar, e
		// reparentear viraria uma operacao que voce tem que desfazer a mao.
		const glm::mat4 keep = GetInitialGlobal(index);

		m_Elements[index].Parent = newParent;

		// ── REORDENA ─────────────────────────────────────────────────────────
		//
		// Todo o resto do sistema depende de PAI ANTES DE FILHO (o loop unico
		// que monta os globais, o Remove, o CollectDescendants). Trocar o pai
		// pode quebrar essa ordem, entao refazemos por DFS em pre-ordem.
		std::vector<std::vector<int>> children((std::size_t)n);
		std::vector<int> stack;

		for (int i = n - 1; i >= 0; --i)
		{
			const int p = m_Elements[i].Parent;

			if (p >= 0)
				children[(std::size_t)p].push_back(i);
			else
				stack.push_back(i);
		}

		std::vector<int> order;
		order.reserve((std::size_t)n);

		while (!stack.empty())
		{
			const int i = stack.back();
			stack.pop_back();

			order.push_back(i);

			for (int k = (int)children[(std::size_t)i].size() - 1; k >= 0; --k)
				stack.push_back(children[(std::size_t)i][k]);
		}

		// Elemento inalcancavel (nao deveria existir) seria PERDIDO no rebuild;
		// melhor abortar do que apagar dado do usuario em silencio.
		if ((int)order.size() != n)
		{
			AXE_CORE_ERROR("RigHierarchy: hierarquia inconsistente, reparent abortado.");
			return -1;
		}

		std::vector<int> newIndex((std::size_t)n, -1);

		for (std::size_t k = 0; k < order.size(); ++k)
			newIndex[(std::size_t)order[k]] = (int)k;

		std::vector<RigElement> rebuilt;
		rebuilt.reserve((std::size_t)n);

		for (const int old : order)
		{
			RigElement e = std::move(m_Elements[(std::size_t)old]);

			if (e.Parent >= 0)
				e.Parent = newIndex[(std::size_t)e.Parent];

			rebuilt.push_back(std::move(e));
		}

		m_Elements = std::move(rebuilt);
		MarkDirty();

		const int moved = newIndex[(std::size_t)index];

		// Devolve o elemento pra onde ele estava, agora em relacao ao novo pai.
		SetInitialGlobal(moved, keep);
		m_Elements[(std::size_t)moved].Current = m_Elements[(std::size_t)moved].Initial;

		MarkDirty();
		return moved;
	}

	bool RigHierarchy::Rename(int index, const std::string& newName)
	{
		if (index < 0 || index >= (int)m_Elements.size() || newName.empty())
			return false;

		const int clash = Find(newName, m_Elements[index].Type);

		if (clash >= 0 && clash != index)
		{
			AXE_CORE_WARN("RigHierarchy: ja existe '{}' deste tipo.", newName);
			return false;
		}

		m_Elements[index].Name = newName;
		MarkDirty();

		return true;
	}

	void RigHierarchy::ImportFromSkeleton(const Skeleton& skeleton)
	{
		Clear();

		const auto& bones = skeleton.GetBones();

		m_Elements.reserve(bones.size());

		for (const auto& b : bones)
		{
			RigElement e;
			e.Name = b.Name;
			e.Type = RigElementType::Bone;
			e.Parent = b.ParentIndex;

			// A bind pose e o repouso do rig. Initial e Current nascem iguais:
			// um rig recem-criado nao deforma nada.
			e.Initial = BoneTransform::FromMatrix(b.LocalBindPose);
			e.Current = e.Initial;

			m_Elements.push_back(std::move(e));
		}

		MarkDirty();

		AXE_CORE_INFO("RigHierarchy - CONTROLRIG_V1: {} ossos importados do esqueleto.",
			m_Elements.size());
	}

	int RigHierarchy::Find(const std::string& name) const
	{
		for (std::size_t i = 0; i < m_Elements.size(); ++i)
			if (m_Elements[i].Name == name)
				return (int)i;

		return -1;
	}

	int RigHierarchy::Find(const std::string& name, RigElementType type) const
	{
		for (std::size_t i = 0; i < m_Elements.size(); ++i)
			if (m_Elements[i].Type == type && m_Elements[i].Name == name)
				return (int)i;

		return -1;
	}

	int RigHierarchy::FindFlexible(const std::string& name) const
	{
		if (name.empty())
			return -1;

		const int exact = Find(name);

		if (exact >= 0)
			return exact;

		const std::string want = SuffixOf(name);

		int found = -1;
		int matches = 0;

		for (std::size_t i = 0; i < m_Elements.size(); ++i)
		{
			if (SuffixOf(m_Elements[i].Name) != want)
				continue;

			++matches;

			if (found < 0)
				found = (int)i;
		}

		return (matches == 1) ? found : -1;
	}

	void RigHierarchy::EnsureGlobals() const
	{
		if (!m_GlobalsDirty)
			return;

		const std::size_t n = m_Elements.size();
		m_Globals.resize(n);

		// Um unico loop pra frente: como o pai tem indice menor, quando
		// chegamos no filho o global do pai JA esta pronto.
		for (std::size_t i = 0; i < n; ++i)
		{
			const glm::mat4 local = m_Elements[i].Current.ToMatrix();
			const int parent = m_Elements[i].Parent;

			m_Globals[i] = (parent < 0) ? local : m_Globals[parent] * local;
		}

		m_GlobalsDirty = false;
	}

	glm::mat4 RigHierarchy::GetGlobal(int i) const
	{
		if (i < 0 || i >= (int)m_Elements.size())
			return glm::mat4(1.0f);

		EnsureGlobals();
		return m_Globals[i];
	}

	glm::mat4 RigHierarchy::GetControlShapeMatrix(int i) const
	{
		if (i < 0 || i >= (int)m_Elements.size())
			return glm::mat4(1.0f);

		return GetGlobal(i) * m_Elements[i].ShapeOffset.ToMatrix();
	}

	glm::mat4 RigHierarchy::GetInitialGlobal(int i) const
	{
		if (i < 0 || i >= (int)m_Elements.size())
			return glm::mat4(1.0f);

		glm::mat4 m = m_Elements[i].Initial.ToMatrix();

		for (int p = m_Elements[i].Parent; p >= 0; p = m_Elements[p].Parent)
			m = m_Elements[p].Initial.ToMatrix() * m;

		return m;
	}

	bool RigHierarchy::SameInitialFrame(int a, int b, float eps) const
	{
		if (a == b)
			return true;

		// GetInitialGlobal ja devolve identidade pra indice invalido, entao -1
		// (espaco do mundo) entra na conta sem caso especial — e por isso um
		// elemento identidade na raiz empata com "sem pai".
		const glm::mat4 ga = GetInitialGlobal(a);
		const glm::mat4 gb = GetInitialGlobal(b);

		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (std::abs(ga[c][r] - gb[c][r]) > eps)
					return false;

		return true;
	}

	void RigHierarchy::SetInitialGlobal(int i, const glm::mat4& m)
	{
		if (i < 0 || i >= (int)m_Elements.size())
			return;

		const int parent = m_Elements[i].Parent;
		const glm::mat4 pg = (parent < 0) ? glm::mat4(1.0f) : GetInitialGlobal(parent);

		m_Elements[i].Initial = BoneTransform::FromMatrix(glm::inverse(pg) * m);
	}

	void RigHierarchy::SetLocal(int i, const BoneTransform& t)
	{
		if (i < 0 || i >= (int)m_Elements.size())
			return;

		m_Elements[i].Current = t;
		MarkDirty();
	}

	void RigHierarchy::SetGlobal(int i, const glm::mat4& m, bool propagateToChildren)
	{
		if (i < 0 || i >= (int)m_Elements.size())
			return;

		EnsureGlobals();

		// Sem propagacao: guardamos onde cada filho DIRETO estava, e devolvemos
		// eles pra la depois de mover o pai.
		std::vector<std::pair<int, glm::mat4>> keep;

		if (!propagateToChildren)
		{
			for (std::size_t c = i + 1; c < m_Elements.size(); ++c)
				if (m_Elements[c].Parent == i)
					keep.emplace_back((int)c, m_Globals[c]);
		}

		const int parent = m_Elements[i].Parent;
		const glm::mat4 pg = (parent < 0) ? glm::mat4(1.0f) : m_Globals[parent];

		m_Elements[i].Current = BoneTransform::FromMatrix(glm::inverse(pg) * m);

		// O global de i mudou; o cache inteiro abaixo dele esta velho.
		m_GlobalsDirty = true;

		if (keep.empty())
			return;

		EnsureGlobals();

		for (const auto& [child, oldGlobal] : keep)
		{
			m_Elements[child].Current =
				BoneTransform::FromMatrix(glm::inverse(m_Globals[i]) * oldGlobal);
		}

		m_GlobalsDirty = true;
	}

	void RigHierarchy::ResetToInitial()
	{
		for (auto& e : m_Elements)
		{
			// Caminho rapido: sem pose, Current = Initial, exatamente como
			// antes de Value existir. E o caso de TODO osso e de todo controle
			// que ninguem posou — ou seja, quase tudo, quase sempre.
			if (IsIdentityTransform(e.Value))
			{
				e.Current = e.Initial;
			}
			else
			{
				// Value e local ao REPOUSO, entao entra a direita: primeiro o
				// elemento vai pro lugar dele, depois a pose se aplica no
				// referencial dele mesmo. Trocar a ordem faria a pose girar em
				// torno do pai, e o controle sairia num arco.
				e.Current = BoneTransform::FromMatrix(
					e.Initial.ToMatrix() * e.Value.ToMatrix());
			}

			// Todo mundo volta a aparecer no comeco do solve; quem quiser
			// esconder alguem tem que dizer isso TODO frame. Sem esse reset,
			// desligar o no que escondia deixaria os controles sumidos pra
			// sempre, sem nada na tela explicando por que.
			e.Visible = true;
		}

		MarkDirty();
	}

	void RigHierarchy::EnsureBoneMap(const Skeleton& skeleton) const
	{
		if (m_MapSkeleton == &skeleton && m_BoneToElement.size() == skeleton.GetBones().size())
			return;

		const auto& bones = skeleton.GetBones();

		m_BoneToElement.assign(bones.size(), -1);

		// Casa por NOME, nao por indice. A hierarquia do rig pode ter Controls e
		// Nulls no meio, entao os indices NAO batem com os do esqueleto — assumir
		// que batem produz um personagem retorcido sem nenhuma mensagem de erro.
		for (std::size_t b = 0; b < bones.size(); ++b)
			m_BoneToElement[b] = Find(bones[b].Name, RigElementType::Bone);

		m_MapSkeleton = &skeleton;
	}

	void RigHierarchy::ApplyPose(const Skeleton& skeleton, const Pose& pose)
	{
		EnsureBoneMap(skeleton);

		for (std::size_t b = 0; b < m_BoneToElement.size() && b < pose.Size(); ++b)
		{
			const int e = m_BoneToElement[b];

			if (e >= 0)
				m_Elements[e].Current = pose[b];
		}

		// MarkDirty invalidaria o mapa que acabamos de montar; aqui so os globais
		// ficaram velhos.
		m_GlobalsDirty = true;
	}

	void RigHierarchy::WritePose(const Skeleton& skeleton, Pose& pose) const
	{
		EnsureBoneMap(skeleton);

		for (std::size_t b = 0; b < m_BoneToElement.size() && b < pose.Size(); ++b)
		{
			const int e = m_BoneToElement[b];

			if (e >= 0)
				pose[b] = m_Elements[e].Current;
		}
	}

} // namespace axe