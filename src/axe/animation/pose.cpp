#include "pose.hpp"
#include "bone_mask.hpp"

#include <glm/gtx/matrix_decompose.hpp>

namespace axe
{
	glm::mat4 BoneTransform::ToMatrix() const
	{
		// T * R * S, construído à mão (mesma razão de BoneChannel::SampleLocal:
		// evita 3 multiplicações de mat4 por osso por frame).
		glm::mat4 m = glm::mat4_cast(Rotation);

		m[0] *= Scale.x;
		m[1] *= Scale.y;
		m[2] *= Scale.z;

		m[3][0] = Translation.x;
		m[3][1] = Translation.y;
		m[3][2] = Translation.z;
		m[3][3] = 1.0f;

		return m;
	}

	BoneTransform BoneTransform::FromMatrix(const glm::mat4& m)
	{
		BoneTransform t;

		glm::vec3 skew;
		glm::vec4 perspective;

		// decompose() falha em matriz degenerada (escala 0). Nesse caso fica
		// com a identidade em vez de propagar NaN pela hierarquia inteira.
		if (!glm::decompose(m, t.Scale, t.Rotation, t.Translation, skew, perspective))
			return BoneTransform{};

		return t;
	}

	void Pose::FromBindPose(const Skeleton& skeleton, Pose& out)
	{
		const auto& bones = skeleton.GetBones();
		out.Resize(bones.size());

		for (std::size_t i = 0; i < bones.size(); ++i)
			out[i] = BoneTransform::FromMatrix(bones[i].LocalBindPose);
	}

	void Pose::Blend(const Pose& a, const Pose& b, float alpha, Pose& out)
	{
		// Atalhos: crossfade passa MUITO tempo nos extremos (a maior parte
		// dos frames não está em transição). Copiar é mais barato que
		// interpolar osso a osso.
		if (alpha <= 0.0f) { out = a; return; }
		if (alpha >= 1.0f) { out = b; return; }

		const std::size_t count = glm::min(a.Size(), b.Size());
		out.Resize(count);

		for (std::size_t i = 0; i < count; ++i)
		{
			out[i].Translation = glm::mix(a[i].Translation, b[i].Translation, alpha);
			out[i].Scale = glm::mix(a[i].Scale, b[i].Scale, alpha);

			// slerp, não lerp. E glm::slerp já checa o sinal do dot pra
			// pegar o caminho CURTO — sem isso, uma transição de 10° poderia
			// dar a volta por 350°, e o braço do personagem gira todo.
			out[i].Rotation = glm::normalize(
				glm::slerp(a[i].Rotation, b[i].Rotation, alpha));
		}
	}

	void Pose::BlendMasked(const Pose& base, const Pose& layer,
		const BoneMask& mask, float alpha, Pose& out)
	{
		const std::size_t count = glm::min(base.Size(), layer.Size());
		out.Resize(count);

		for (std::size_t i = 0; i < count; ++i)
		{
			// Peso EFETIVO deste osso: a máscara diz "quanto deste osso
			// pertence à camada", e alpha faz o fade da camada inteira.
			const float w = mask.GetWeight(i) * alpha;

			if (w <= 0.0f) { out[i] = base[i];  continue; }
			if (w >= 1.0f) { out[i] = layer[i]; continue; }

			out[i].Translation = glm::mix(base[i].Translation, layer[i].Translation, w);
			out[i].Scale = glm::mix(base[i].Scale, layer[i].Scale, w);
			out[i].Rotation = glm::normalize(
				glm::slerp(base[i].Rotation, layer[i].Rotation, w));
		}
	}

	void Pose::MakeAdditive(const Pose& source, const Pose& reference, Pose& outAdditive)
	{
		const std::size_t count = glm::min(source.Size(), reference.Size());
		outAdditive.Resize(count);

		for (std::size_t i = 0; i < count; ++i)
		{
			outAdditive[i].Translation = source[i].Translation - reference[i].Translation;

			// A "diferença" entre duas rotações é a rotação que leva uma na
			// outra: ref⁻¹ * src. Para quaternion unitário, o inverso é o
			// conjugado — mais barato e numericamente estável.
			outAdditive[i].Rotation = glm::normalize(
				glm::conjugate(reference[i].Rotation) * source[i].Rotation);

			// Escala é multiplicativa, então a diferença é divisão.
			// Guarda contra escala 0 na referência (rig quebrado) — sem
			// isso vira inf e depois NaN.
			const glm::vec3& r = reference[i].Scale;
			outAdditive[i].Scale = glm::vec3(
				glm::abs(r.x) > 1e-6f ? source[i].Scale.x / r.x : 1.0f,
				glm::abs(r.y) > 1e-6f ? source[i].Scale.y / r.y : 1.0f,
				glm::abs(r.z) > 1e-6f ? source[i].Scale.z / r.z : 1.0f);
		}
	}

	void Pose::ApplyAdditive(const Pose& base, const Pose& additive,
		float alpha, Pose& out)
	{
		if (alpha <= 0.0f) { out = base; return; }

		const std::size_t count = glm::min(base.Size(), additive.Size());
		out.Resize(count);

		const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);

		for (std::size_t i = 0; i < count; ++i)
		{
			out[i].Translation = base[i].Translation + additive[i].Translation * alpha;

			// alpha escalando um quaternion não é multiplicação: é um slerp
			// da IDENTIDADE até o delta. alpha=0 → nenhuma rotação extra;
			// alpha=1 → o delta inteiro.
			const glm::quat delta = glm::slerp(identity, additive[i].Rotation, alpha);
			out[i].Rotation = glm::normalize(base[i].Rotation * delta);

			// Escala aditiva é multiplicativa; alpha interpola de 1 (neutro)
			// até o fator completo.
			out[i].Scale = base[i].Scale * glm::mix(glm::vec3(1.0f), additive[i].Scale, alpha);
		}
	}

	void Pose::ResetAccumulation()
	{
		m_AccumulatedWeight = 0.0f;
	}

	void Pose::Accumulate(const Pose& pose, float weight)
	{
		if (weight <= 1e-6f)
			return;

		if (m_AccumulatedWeight <= 0.0f)
		{
			// Primeiro clipe: vira a base. Não dá pra "somar" numa pose
			// vazia — quaternion não tem elemento neutro aditivo.
			m_Bones = pose.m_Bones;
			m_AccumulatedWeight = weight;
			return;
		}

		const std::size_t count = glm::min(Size(), pose.Size());

		// Slerp incremental: reponderar pelo total acumulado até agora.
		//
		// Depois de já ter absorvido peso W, adicionar um clipe de peso w
		// significa que ele deve responder por w/(W+w) do resultado. Fazer
		// isso a cada passo é matematicamente equivalente a um blend N-ário
		// — e é como todo engine faz, porque quaternions não se somam.
		const float total = m_AccumulatedWeight + weight;
		const float t = weight / total;

		for (std::size_t i = 0; i < count; ++i)
		{
			m_Bones[i].Translation = glm::mix(m_Bones[i].Translation, pose[i].Translation, t);
			m_Bones[i].Scale = glm::mix(m_Bones[i].Scale, pose[i].Scale, t);
			m_Bones[i].Rotation = glm::normalize(
				glm::slerp(m_Bones[i].Rotation, pose[i].Rotation, t));
		}

		m_AccumulatedWeight = total;
	}

} // namespace axe