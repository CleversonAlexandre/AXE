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
			e.Current = e.Initial;

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