#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace axe
{
	struct VectorKey
	{
		float     Time = 0.0f;   // em SEGUNDOS (já convertido de ticks no load)
		glm::vec3 Value{ 0.0f };
	};

	struct QuatKey
	{
		float     Time = 0.0f;   // em SEGUNDOS
		glm::quat Value{ 1.0f, 0.0f, 0.0f, 0.0f }; // (w, x, y, z)
	};

	// Curva de animação de UM bone. Os três canais (T/R/S) são
	// independentes e podem ter contagens de chaves diferentes — é assim
	// que o Assimp entrega e é assim que qualquer DCC exporta.
	struct AXE_API BoneChannel
	{
		int BoneIndex = -1;   // índice dentro do Skeleton alvo

		std::vector<VectorKey> PositionKeys;
		std::vector<QuatKey>   RotationKeys;
		std::vector<VectorKey> ScaleKeys;

		glm::vec3 SamplePosition(float timeSeconds) const;
		glm::quat SampleRotation(float timeSeconds) const;
		glm::vec3 SampleScale(float timeSeconds) const;

		// Monta o transform local completo: T * R * S.
		glm::mat4 SampleLocal(float timeSeconds) const;

		bool IsEmpty() const
		{
			return PositionKeys.empty() && RotationKeys.empty() && ScaleKeys.empty();
		}
	};

	class AXE_API AnimationClip
	{
	public:
		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& n) { m_Name = n; }

		// Duração em SEGUNDOS.
		float GetDuration() const { return m_Duration; }
		void  SetDuration(float d) { m_Duration = d; }

		bool  IsLooping() const { return m_Looping; }
		void  SetLooping(bool l) { m_Looping = l; }

		// Adiciona um canal e indexa por BoneIndex. Canal com
		// BoneIndex == -1 (bone não existe no esqueleto alvo) é
		// descartado silenciosamente — é o que permite tocar um clipe
		// exportado com bones extras num esqueleto mais enxuto.
		void AddChannel(const BoneChannel& channel);

		// nullptr se o clipe não anima este bone (o sampler cai na
		// LocalBindPose nesse caso).
		const BoneChannel* FindChannel(int boneIndex) const;

		const std::vector<BoneChannel>& GetChannels() const { return m_Channels; }
		std::size_t GetChannelCount() const { return m_Channels.size(); }

		// Aplica loop ou clamp em `time` de acordo com m_Looping.
		// Devolve um tempo sempre dentro de [0, Duration].
		float WrapTime(float timeSeconds) const;

	private:
		std::string              m_Name;
		float                    m_Duration = 0.0f;
		bool                     m_Looping = true;

		std::vector<BoneChannel> m_Channels;

		// BoneIndex → posição em m_Channels. Evita busca linear a cada
		// bone, a cada frame, pra cada personagem.
		std::unordered_map<int, int> m_BoneToChannel;
	};

} // namespace axe