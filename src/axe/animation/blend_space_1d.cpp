#include "blend_space_1d.hpp"
#include "animation_sampler.hpp"

#include <algorithm>

namespace axe
{
	void BlendSpace1D::AddSample(const std::shared_ptr<AnimationClip>& clip, float value)
	{
		if (!clip)
			return;

		Sample s;
		s.Clip = clip;
		s.Value = value;

		// Inserção ordenada — FindNeighbors depende disso e é chamado todo
		// frame; ordenar aqui (raro) sai muito mais barato que lá.
		auto it = std::lower_bound(m_Samples.begin(), m_Samples.end(), value,
			[](const Sample& a, float v) { return a.Value < v; });

		m_Samples.insert(it, s);
	}

	void BlendSpace1D::FindNeighbors(float value, int& outA, int& outB, float& outT) const
	{
		const int count = static_cast<int>(m_Samples.size());

		outA = outB = 0;
		outT = 0.0f;

		if (count == 0)
			return;

		// Fora das pontas: fixa no extremo (não extrapola — extrapolar uma
		// animação produz pose sem sentido, ex: passada de 3 metros).
		if (count == 1 || value <= m_Samples.front().Value)
		{
			outA = outB = 0;
			return;
		}

		if (value >= m_Samples.back().Value)
		{
			outA = outB = count - 1;
			return;
		}

		for (int i = 0; i < count - 1; ++i)
		{
			const float lo = m_Samples[i].Value;
			const float hi = m_Samples[i + 1].Value;

			if (value >= lo && value <= hi)
			{
				outA = i;
				outB = i + 1;

				const float span = hi - lo;
				// Dois samples no MESMO valor (erro de autoria) — sem esta
				// guarda vira divisão por zero e a pose some.
				outT = (span > 1e-6f) ? (value - lo) / span : 0.0f;
				return;
			}
		}

		outA = outB = count - 1;
	}

	float BlendSpace1D::GetDurationAt(float value) const
	{
		if (m_Samples.empty())
			return 0.0f;

		int a, b; float t;
		FindNeighbors(value, a, b, t);

		const float da = m_Samples[a].Clip->GetDuration();
		const float db = m_Samples[b].Clip->GetDuration();

		return glm::mix(da, db, t);
	}

	void BlendSpace1D::Evaluate(const Skeleton& skeleton,
		float value,
		float time,
		Pose& outPose) const
	{
		if (m_Samples.empty() || skeleton.IsEmpty())
		{
			Pose::FromBindPose(skeleton, outPose);
			return;
		}

		int a, b; float t;
		FindNeighbors(value, a, b, t);

		// FASE, não tempo absoluto. A duração efetiva aqui é a média
		// ponderada — então `time` percorre 0..duration e phase percorre
		// 0..1, e AMBOS os clipes são amostrados nessa mesma fase.
		//
		// Resultado: o instante "pé esquerdo tocando o chão" acontece na
		// mesma fase em walk e em run, e o blend entre eles faz sentido
		// físico. Sem isso, o personagem patina.
		const float duration = GetDurationAt(value);
		const float phase = (duration > 1e-6f) ? glm::fract(time / duration) : 0.0f;

		Pose scratch;

		outPose.ResetAccumulation();

		// Um único clipe ativo (nas pontas, ou a==b).
		if (a == b)
		{
			const float clipTime = phase * m_Samples[a].Clip->GetDuration();
			AnimationSampler::SamplePose(skeleton, *m_Samples[a].Clip, clipTime, outPose);
			return;
		}

		// Dois vizinhos, cada um amostrado NA SUA duração, mas na MESMA fase.
		AnimationSampler::SamplePose(skeleton, *m_Samples[a].Clip,
			phase * m_Samples[a].Clip->GetDuration(), outPose);

		AnimationSampler::SamplePose(skeleton, *m_Samples[b].Clip,
			phase * m_Samples[b].Clip->GetDuration(), scratch);

		Pose::Blend(outPose, scratch, t, outPose);
	}

} // namespace axe