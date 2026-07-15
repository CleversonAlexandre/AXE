#include "animation_clip.hpp"

#include <algorithm>
#include <cmath>

namespace axe
{
	namespace
	{
		// Localiza o par de chaves (k, k+1) que cerca `time` e devolve o
		// fator de interpolação em [0,1].
		//
		// Busca BINÁRIA (upper_bound) em vez de linear: um clipe de 30s a
		// 30fps tem ~900 chaves por canal; com ~60 bones e vários
		// personagens, a busca linear vira um custo real de CPU por frame.
		//
		// Retorna false se não houver nada para interpolar (0 chaves).
		template <typename KeyT>
		bool FindKeyPair(const std::vector<KeyT>& keys, float time,
			std::size_t& outA, std::size_t& outB, float& outT)
		{
			if (keys.empty())
				return false;

			if (keys.size() == 1 || time <= keys.front().Time)
			{
				outA = outB = 0;
				outT = 0.0f;
				return true;
			}

			if (time >= keys.back().Time)
			{
				outA = outB = keys.size() - 1;
				outT = 0.0f;
				return true;
			}

			// Primeira chave com Time > time.
			auto it = std::upper_bound(keys.begin(), keys.end(), time,
				[](float t, const KeyT& k) { return t < k.Time; });

			const std::size_t b = static_cast<std::size_t>(it - keys.begin());
			const std::size_t a = b - 1;

			const float span = keys[b].Time - keys[a].Time;

			// Chaves duplicadas no mesmo instante (acontece em exports
			// ruins) — divisão por zero vira NaN e a mesh some. Guarda.
			outT = (span > 1e-6f) ? (time - keys[a].Time) / span : 0.0f;
			outA = a;
			outB = b;
			return true;
		}
	}

	glm::vec3 BoneChannel::SamplePosition(float timeSeconds) const
	{
		std::size_t a, b; float t;
		if (!FindKeyPair(PositionKeys, timeSeconds, a, b, t))
			return glm::vec3(0.0f);

		return glm::mix(PositionKeys[a].Value, PositionKeys[b].Value, t);
	}

	glm::quat BoneChannel::SampleRotation(float timeSeconds) const
	{
		std::size_t a, b; float t;
		if (!FindKeyPair(RotationKeys, timeSeconds, a, b, t))
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

		// slerp (não lerp): interpolação linear de quaternions produz
		// velocidade angular NÃO-uniforme e encolhe o quaternion — a
		// junta "afunda" no meio da rotação. glm::slerp já escolhe o
		// caminho curto (checa o sinal do dot).
		const glm::quat q = glm::slerp(RotationKeys[a].Value, RotationKeys[b].Value, t);
		return glm::normalize(q);
	}

	glm::vec3 BoneChannel::SampleScale(float timeSeconds) const
	{
		std::size_t a, b; float t;
		if (!FindKeyPair(ScaleKeys, timeSeconds, a, b, t))
			return glm::vec3(1.0f);

		return glm::mix(ScaleKeys[a].Value, ScaleKeys[b].Value, t);
	}

	glm::mat4 BoneChannel::SampleLocal(float timeSeconds) const
	{
		const glm::vec3 t = SamplePosition(timeSeconds);
		const glm::quat r = SampleRotation(timeSeconds);
		const glm::vec3 s = SampleScale(timeSeconds);

		// T * R * S — a ordem importa. Construído à mão em vez de
		// translate()*mat4_cast()*scale() pra evitar 3 multiplicações
		// de matriz 4x4 por bone por frame.
		glm::mat4 m = glm::mat4_cast(r);

		m[0] *= s.x;
		m[1] *= s.y;
		m[2] *= s.z;

		m[3][0] = t.x;
		m[3][1] = t.y;
		m[3][2] = t.z;
		m[3][3] = 1.0f;

		return m;
	}

	void AnimationClip::AddChannel(const BoneChannel& channel)
	{
		if (channel.BoneIndex < 0 || channel.IsEmpty())
			return;

		const int slot = static_cast<int>(m_Channels.size());
		m_Channels.push_back(channel);
		m_BoneToChannel[channel.BoneIndex] = slot;
	}

	const BoneChannel* AnimationClip::FindChannel(int boneIndex) const
	{
		auto it = m_BoneToChannel.find(boneIndex);
		if (it == m_BoneToChannel.end())
			return nullptr;

		return &m_Channels[it->second];
	}

	float AnimationClip::WrapTime(float timeSeconds) const
	{
		if (m_Duration <= 0.0f)
			return 0.0f;

		if (!m_Looping)
			return glm::clamp(timeSeconds, 0.0f, m_Duration);

		// fmod com negativo devolve negativo — normaliza pra frente.
		float t = std::fmod(timeSeconds, m_Duration);
		if (t < 0.0f)
			t += m_Duration;

		return t;
	}

} // namespace axe