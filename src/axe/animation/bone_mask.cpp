#include "bone_mask.hpp"
#include "axe/log/log.hpp"

#include <algorithm>

namespace axe
{
	namespace
	{
		// Marca quem pertence à subárvore de `root` e a que PROFUNDIDADE.
		// -1 = fora do ramo.
		//
		// Um único loop pra frente resolve a subárvore inteira, sem recursão
		// e sem fila. Só funciona por causa da invariante topológica do
		// Skeleton (pai sempre em índice menor): quando chegamos no osso i,
		// o pai dele já foi classificado.
		std::vector<int> BranchDepths(const Skeleton& skeleton, int root)
		{
			const auto& bones = skeleton.GetBones();

			std::vector<int> depth(bones.size(), -1);
			depth[root] = 0;

			for (std::size_t i = static_cast<std::size_t>(root) + 1; i < bones.size(); ++i)
			{
				const int parent = bones[i].ParentIndex;
				if (parent >= 0 && depth[parent] >= 0)
					depth[i] = depth[parent] + 1;
			}

			return depth;
		}
	}

	void BoneMask::Reset(const Skeleton& skeleton, float weight)
	{
		m_Weights.assign(skeleton.GetBoneCount(), glm::clamp(weight, 0.0f, 1.0f));
	}

	void BoneMask::SetBone(int boneIndex, float weight)
	{
		if (boneIndex < 0 || boneIndex >= static_cast<int>(m_Weights.size()))
			return;

		m_Weights[boneIndex] = glm::clamp(weight, 0.0f, 1.0f);
	}

	bool BoneMask::SetBranch(const Skeleton& skeleton,
		const std::string& rootBoneName,
		float weight)
	{
		if (m_Weights.size() != skeleton.GetBoneCount())
			Reset(skeleton, 0.0f);

		const int root = skeleton.FindBone(rootBoneName);
		if (root < 0)
		{
			AXE_CORE_WARN("BoneMask: osso '{}' nao existe no esqueleto '{}'.",
				rootBoneName, skeleton.GetName());
			return false;
		}

		const float w = glm::clamp(weight, 0.0f, 1.0f);
		const std::vector<int> depth = BranchDepths(skeleton, root);

		// Pertencimento vem da HIERARQUIA, nunca do peso anterior — senão
		// chamar SetBranch duas vezes (ou depois de um Reset(1.0)) daria
		// resultado diferente conforme a ordem.
		for (std::size_t i = 0; i < depth.size(); ++i)
			if (depth[i] >= 0)
				m_Weights[i] = w;

		return true;
	}

	void BoneMask::Feather(const Skeleton& skeleton,
		const std::string& rootBoneName,
		int depth)
	{
		if (depth <= 0 || m_Weights.size() != skeleton.GetBoneCount())
			return;

		const int root = skeleton.FindBone(rootBoneName);
		if (root < 0)
			return;

		const std::vector<int> d = BranchDepths(skeleton, root);

		// Rampa: na raiz do ramo o peso é ~0 e sobe até o peso cheio depois
		// de `depth` ossos. Multiplica o peso existente (não sobrescreve),
		// então Feather é sempre aplicado DEPOIS de SetBranch.
		//
		// Sem isso, a máscara pula de 0 pra 1 numa junta só, e o tronco
		// "quebra" visivelmente ali — o defeito nº1 de máscara feita à mão.
		for (std::size_t i = 0; i < d.size(); ++i)
		{
			if (d[i] < 0)
				continue;

			const float ramp = glm::min(1.0f,
				static_cast<float>(d[i]) / static_cast<float>(depth));

			m_Weights[i] *= ramp;
		}
	}

} // namespace axe