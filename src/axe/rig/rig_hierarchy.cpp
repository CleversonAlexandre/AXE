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
			e.Current = e.Initial;

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