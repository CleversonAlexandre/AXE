#include "animation_player.hpp"
#include "animation_sampler.hpp"

namespace axe
{
	void AnimationPlayer::Play(const std::shared_ptr<AnimationClip>& clip, float blendTime)
	{
		// Já é o clipe atual — não faz nada.
		//
		// Isto parece detalhe e não é: gameplay quase sempre chama
		// Play(estadoAtual) TODO FRAME ("se está parado, toca idle"). Sem
		// esta guarda, o clipe reiniciaria a cada frame e o personagem
		// ficaria eternamente congelado no primeiro frame da animação.
		if (m_Current.Clip == clip)
			return;

		PlayRestart(clip, blendTime);
	}

	void AnimationPlayer::PlayRestart(const std::shared_ptr<AnimationClip>& clip, float blendTime)
	{
		if (!clip)
			return;

		if (blendTime <= 0.0f)
		{
			// Corte seco.
			m_Current.Clip = clip;
			m_Current.Time = 0.0f;
			m_Previous.Clip.reset();
			m_BlendDuration = 0.0f;
			m_BlendElapsed = 0.0f;
			return;
		}

		// TRANSIÇÃO INTERROMPIDA.
		//
		// Se já estávamos no meio de um blend (A→B) e agora pedem C, a
		// origem do novo blend NÃO é B — é a pose misturada que está na tela
		// AGORA. Usar B daria um salto visível no exato instante em que o
		// jogador trocou de ação, que é o pior momento possível.
		//
		// Guardar m_Result (a pose já resolvida do último frame) como origem
		// resolve isso: o novo blend parte de onde o personagem realmente
		// está. É o mesmo truque que a Unreal chama de "inertialization-lite".
		if (IsBlending() && !m_Result.IsEmpty())
		{
			m_PosePrevious = m_Result;
			m_Previous.Clip.reset();   // a origem agora é a pose, não um clipe
		}
		else
		{
			m_Previous = m_Current;
		}

		m_Current.Clip = clip;
		m_Current.Time = 0.0f;

		m_BlendDuration = blendTime;
		m_BlendElapsed = 0.0f;
	}

	void AnimationPlayer::SetLayer(const std::shared_ptr<AnimationClip>& clip,
		const BoneMask* mask,
		bool additive,
		float weight)
	{
		if (m_Layer.Clip != clip)
			m_Layer.Time = 0.0f;

		m_Layer.Clip = clip;
		m_Layer.Mask = mask;
		m_Layer.Additive = additive;
		m_Layer.Weight = glm::clamp(weight, 0.0f, 1.0f);
	}

	void AnimationPlayer::SetAdditiveReference(const std::shared_ptr<AnimationClip>& clip, float atTime)
	{
		m_AdditiveRefClip = clip;
		m_AdditiveRefTime = atTime;
	}

	float AnimationPlayer::GetBlendAlpha() const
	{
		if (m_BlendDuration <= 0.0f)
			return 1.0f;

		const float raw = glm::clamp(m_BlendElapsed / m_BlendDuration, 0.0f, 1.0f);

		// Smoothstep em vez de rampa linear.
		//
		// Blend linear tem derivada descontínua nas pontas: a velocidade da
		// transição "liga" e "desliga" de repente, e o olho pega isso como um
		// tranco no começo e no fim do crossfade. Smoothstep zera a derivada
		// nos extremos — a transição nasce e morre suave.
		return raw * raw * (3.0f - 2.0f * raw);
	}

	void AnimationPlayer::Update(const Skeleton& skeleton, float deltaTime, bool advanceTime)
	{
		if (skeleton.IsEmpty())
		{
			m_Result = Pose{};
			return;
		}

		// Sem clipe nenhum -> bind pose. NUNCA deixar a pose vazia: o Skin
		// Cache leria bones inexistentes e o personagem colapsaria num ponto.
		if (!m_Current.Clip)
		{
			Pose::FromBindPose(skeleton, m_Result);
			return;
		}

		const float dt = advanceTime && Playing ? deltaTime * PlayRate : 0.0f;

		m_Current.Time += dt;

		// ── 1. Pose base (com crossfade, se houver) ──────────────────────
		AnimationSampler::SamplePose(skeleton, *m_Current.Clip, m_Current.Time, m_PoseCurrent);

		if (IsBlending())
		{
			m_BlendElapsed += glm::abs(dt);

			// A animação DE SAÍDA continua correndo durante a transição.
			// Se ela congelasse, o personagem pararia de andar no meio do
			// crossfade — o pé escorregaria no chão de forma bem visível.
			if (m_Previous.Clip)
			{
				m_Previous.Time += dt;
				AnimationSampler::SamplePose(skeleton, *m_Previous.Clip,
					m_Previous.Time, m_PosePrevious);
			}
			// Se m_Previous.Clip é null, m_PosePrevious já contém a pose
			// congelada de onde a transição interrompida partiu.

			Pose::Blend(m_PosePrevious, m_PoseCurrent, GetBlendAlpha(), m_Result);

			// Blend terminou — solta o clipe de saída.
			if (m_BlendElapsed >= m_BlendDuration)
			{
				m_Previous.Clip.reset();
				m_BlendDuration = 0.0f;
			}
		}
		else
		{
			m_Result = m_PoseCurrent;
		}

		// ── 2. Camada de cima ────────────────────────────────────────────
		if (!m_Layer.Clip || m_Layer.Weight <= 0.0f)
			return;

		m_Layer.Time += dt;
		AnimationSampler::SamplePose(skeleton, *m_Layer.Clip, m_Layer.Time, m_PoseLayer);

		if (m_Layer.Additive)
		{
			// Referência do aditivo: um clipe específico, ou a bind pose.
			if (m_AdditiveRefClip)
			{
				AnimationSampler::SamplePose(skeleton, *m_AdditiveRefClip,
					m_AdditiveRefTime, m_PoseAdditiveRef);
			}
			else
			{
				Pose::FromBindPose(skeleton, m_PoseAdditiveRef);
			}

			Pose::MakeAdditive(m_PoseLayer, m_PoseAdditiveRef, m_PoseAdditive);

			if (m_Layer.Mask)
			{
				// Aditivo COM máscara: aplica o delta só nos ossos marcados.
				// Ex: um tranco de dano só no tronco, sem mexer nas pernas.
				Pose::ApplyAdditive(m_Result, m_PoseAdditive, m_Layer.Weight, m_PoseLayer);
				Pose::BlendMasked(m_Result, m_PoseLayer, *m_Layer.Mask, 1.0f, m_Result);
			}
			else
			{
				Pose::ApplyAdditive(m_Result, m_PoseAdditive, m_Layer.Weight, m_Result);
			}
		}
		else if (m_Layer.Mask)
		{
			Pose::BlendMasked(m_Result, m_PoseLayer, *m_Layer.Mask, m_Layer.Weight, m_Result);
		}
		else
		{
			// Camada sem máscara e sem aditivo = crossfade puro por cima.
			Pose::Blend(m_Result, m_PoseLayer, m_Layer.Weight, m_Result);
		}
	}

} // namespace axe