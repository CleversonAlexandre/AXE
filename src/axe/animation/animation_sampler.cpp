#include "animation_sampler.hpp"
#include "axe/log/log.hpp"

namespace axe
{
	namespace
	{
		// Scratch das matrizes globais, reaproveitado entre chamadas.
		// Sem isto seria um std::vector<glm::mat4> alocado e destruído por
		// personagem, por frame — lixo puro no heap a 60fps.
		// thread_local porque um dia o sampling pode ir pra job system.
		thread_local std::vector<glm::mat4> t_GlobalScratch;

		// Núcleo compartilhado entre Sample() e BindPose(). O único ponto de
		// divergência é como o transform LOCAL de cada bone é obtido, então
		// isso entra como callable e o compilador inlina.
		template <typename LocalFn>
		void BuildPose(const Skeleton& skeleton,
			std::vector<glm::mat4>& outSkinning,
			std::vector<glm::mat4>* outGlobals,
			LocalFn getLocal)
		{
			const auto& bones = skeleton.GetBones();
			const std::size_t count = bones.size();

			outSkinning.resize(count);

			std::vector<glm::mat4>& globals =
				outGlobals ? (outGlobals->resize(count), *outGlobals) : t_GlobalScratch;

			if (&globals == &t_GlobalScratch)
				globals.resize(count);

			const glm::mat4& globalInverse = skeleton.GetGlobalInverseTransform();

			// Loop ÚNICO e pra frente. Só funciona porque o Skeleton garante
			// ordem topológica (pai sempre em índice menor) — quando chegamos
			// no bone i, globals[ParentIndex] já está resolvido. É por isso que
			// Skeleton::AddBone berra se a invariante for violada: sem ela,
			// isto aqui lê lixo e a mesh vira espaguete.
			for (std::size_t i = 0; i < count; ++i)
			{
				const Bone& bone = bones[i];

				const glm::mat4 local = getLocal(static_cast<int>(i), bone);

				globals[i] = (bone.ParentIndex < 0)
					? local
					: globals[bone.ParentIndex] * local;

				outSkinning[i] = globalInverse * globals[i] * bone.InverseBindPose;
			}
		}
	}

	void AnimationSampler::SamplePose(const Skeleton& skeleton,
		const AnimationClip& clip,
		float timeSeconds,
		Pose& outPose)
	{
		const auto& bones = skeleton.GetBones();
		const std::size_t count = bones.size();

		outPose.Resize(count);

		const float t = clip.WrapTime(timeSeconds);

		for (std::size_t i = 0; i < count; ++i)
		{
			const BoneChannel* channel = clip.FindChannel(static_cast<int>(i));

			if (channel)
			{
				// FALLBACK POR COMPONENTE — e nao por canal.
				//
				// Um canal pode existir e mesmo assim NAO ter chaves de
				// posicao (muito comum: a maioria dos ossos so gira). Se
				// nesse caso a translacao virasse (0,0,0), o osso colapsaria
				// sobre o pai e o personagem se retorceria — mesmo com a
				// rotacao perfeitamente correta.
				//
				// Por isso cada um dos tres componentes cai na BIND POSE
				// separadamente quando o clipe nao o anima. Nao basta checar
				// "tem canal?": tem que checar "tem chave DESTE tipo?".
				const bool needBind =
					channel->PositionKeys.empty() ||
					channel->RotationKeys.empty() ||
					channel->ScaleKeys.empty();

				// Decompor a mat4 e caro; so paga quando falta algum canal.
				const BoneTransform bind = needBind
					? BoneTransform::FromMatrix(bones[i].LocalBindPose)
					: BoneTransform{};

				outPose[i].Translation = channel->PositionKeys.empty()
					? bind.Translation : channel->SamplePosition(t);

				outPose[i].Rotation = channel->RotationKeys.empty()
					? bind.Rotation : channel->SampleRotation(t);

				outPose[i].Scale = channel->ScaleKeys.empty()
					? bind.Scale : channel->SampleScale(t);
			}
			else
			{
				// Osso não animado por este clipe -> bind pose inteira.
				outPose[i] = BoneTransform::FromMatrix(bones[i].LocalBindPose);
			}
		}
	}

	void AnimationSampler::BuildSkinningMatrices(const Skeleton& skeleton,
		const Pose& pose,
		std::vector<glm::mat4>& outSkinning,
		std::vector<glm::mat4>* outGlobals)
	{
		if (skeleton.IsEmpty() || pose.IsEmpty())
		{
			outSkinning.clear();
			if (outGlobals) outGlobals->clear();
			return;
		}

		BuildPose(skeleton, outSkinning, outGlobals,
			[&pose](int boneIndex, const Bone& bone) -> glm::mat4
			{
				// A pose pode ser menor que o esqueleto se veio de um clipe
				// de outro rig — os ossos que faltam usam a bind pose.
				if (static_cast<std::size_t>(boneIndex) >= pose.Size())
					return bone.LocalBindPose;

				return pose[boneIndex].ToMatrix();
			});
	}

	void AnimationSampler::Sample(const Skeleton& skeleton,
		const AnimationClip& clip,
		float timeSeconds,
		std::vector<glm::mat4>& outSkinning,
		std::vector<glm::mat4>* outGlobals)
	{
		if (skeleton.IsEmpty())
		{
			outSkinning.clear();
			if (outGlobals) outGlobals->clear();
			return;
		}

		// Loop/clamp resolvido pelo próprio clipe — o chamador pode
		// simplesmente acumular deltaTime sem se preocupar.
		const float t = clip.WrapTime(timeSeconds);

		BuildPose(skeleton, outSkinning, outGlobals,
			[&clip, t](int boneIndex, const Bone& bone) -> glm::mat4
			{
				const BoneChannel* channel = clip.FindChannel(boneIndex);

				// Sem canal = o clipe não anima este bone. Cair na bind pose
				// (e NÃO na identidade) é o que permite tocar um clipe parcial
				// — ex: "acenar", que só tem chaves do braço.
				return channel ? channel->SampleLocal(t) : bone.LocalBindPose;
			});
	}

	void AnimationSampler::BindPose(const Skeleton& skeleton,
		std::vector<glm::mat4>& outSkinning,
		std::vector<glm::mat4>* outGlobals)
	{
		if (skeleton.IsEmpty())
		{
			outSkinning.clear();
			if (outGlobals) outGlobals->clear();
			return;
		}

		BuildPose(skeleton, outSkinning, outGlobals,
			[](int, const Bone& bone) -> glm::mat4
			{
				return bone.LocalBindPose;
			});
	}

	void AnimationSampler::Identity(std::vector<glm::mat4>& outSkinning, std::size_t count)
	{
		outSkinning.assign(count, glm::mat4(1.0f));
	}

} // namespace axe
