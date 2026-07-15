#include "skeleton.hpp"
#include "axe/log/log.hpp"

namespace axe
{
	int Skeleton::AddBone(const std::string& name,
		int parentIndex,
		const glm::mat4& inverseBindPose,
		const glm::mat4& localBindPose)
	{
		// Bone repetido — devolve o existente em vez de duplicar.
		auto it = m_NameToIndex.find(name);
		if (it != m_NameToIndex.end())
			return it->second;

		const int index = static_cast<int>(m_Bones.size());

		// Guarda de invariante topológica. Se isto disparar, o loader
		// inseriu um filho antes do pai e TODA a pose global sai errada
		// — melhor gritar aqui do que debugar mesh explodindo depois.
		if (parentIndex >= index)
		{
			AXE_CORE_ERROR("Skeleton: bone '{}' (idx {}) tem pai idx {} >= si mesmo — ordem topologica violada!",
				name, index, parentIndex);
		}

		Bone bone;
		bone.Name = name;
		bone.ParentIndex = parentIndex;
		bone.InverseBindPose = inverseBindPose;
		bone.LocalBindPose = localBindPose;

		m_Bones.push_back(bone);
		m_NameToIndex[name] = index;

		return index;
	}

	int Skeleton::FindBone(const std::string& name) const
	{
		auto it = m_NameToIndex.find(name);
		return (it != m_NameToIndex.end()) ? it->second : -1;
	}

} // namespace axe