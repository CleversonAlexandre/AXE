#include "animation_world.hpp"

#include "axe/animation/animation_sampler.hpp"
#include "axe/animation/pose.hpp"
#include "axe/animation/anim_graph_instance.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"

#include <entt/entt.hpp>

namespace axe
{
	void AnimationWorld::OnUpdate(Scene& scene, float deltaTime, bool inPlay)
	{
		auto& registry = scene.GetRegistry();

		auto view = registry.view<SkeletalMeshComponent>();

		for (auto entity : view)
		{
			auto& skel = view.get<SkeletalMeshComponent>(entity);

			const Skeleton* skeleton = skel.GetSkeleton();
			if (!skeleton || skeleton->IsEmpty())
			{
				skel.BonePalette.clear();
				continue;
			}

			// Buffer de pose reaproveitado entre personagens e entre frames.
			static thread_local Pose s_Pose;

			// O tempo corre em Play OU no preview do editor. Sem isto, o
			// personagem so anima depois de apertar Play — e conferir um
			// import recem-feito viraria um ciclo lento e irritante.
			const bool advance = inPlay || skel.PreviewInEditor;

			// ── Caminho A: AnimGraph (state machine) ─────────────────────
			//
			// Tem prioridade sobre tudo. O gameplay só escreve parâmetros; o
			// grafo decide estado, transição e blend.
			if (skel.GraphAsset)
			{
				// A instância clona o grafo do asset na primeira vez. Chamar
				// todo frame é no-op se o asset não mudou.
				if (!skel.GraphInstance.HasGraph())
					skel.GraphInstance.SetAsset(skel.GraphAsset);

				// DUAS FASES.
				//
				// Update avança o tempo e decide as transições; Evaluate produz
				// a pose. Separados porque, num crossfade, o estado que está
				// SAINDO precisa continuar avançando — senão ele congela no
				// meio da transição e o pé escorrega no chão, bem visível.
				skel.GraphInstance.Update(*skeleton, deltaTime, advance);
				skel.GraphInstance.Evaluate(*skeleton, m_ScratchPose);

				AnimationSampler::BuildSkinningMatrices(*skeleton,
					m_ScratchPose,
					skel.BonePalette,
					skel.ShowSkeleton ? &skel.BoneGlobals : nullptr);
				continue;
			}

			// ── Caminho B: Blend Space (locomoção contínua) ──────────────
			if (skel.BlendSpace && !skel.BlendSpace->IsEmpty())
			{
				// A duração do ciclo vem do PRÓPRIO blend space e varia com o
				// parâmetro: quanto mais rápido o personagem, mais curta a
				// passada. Sai de graça, sem tocar em PlayRate.
				if (advance)
					skel.BlendSpaceTime += deltaTime;

				skel.BlendSpace->Evaluate(*skeleton,
					skel.BlendParam,
					skel.BlendSpaceTime,
					s_Pose);

				AnimationSampler::BuildSkinningMatrices(*skeleton, s_Pose, skel.BonePalette,
					skel.ShowSkeleton ? &skel.BoneGlobals : nullptr);
				continue;
			}

			// ── Caminho C: clipe único, com crossfade na troca ───────────

			const bool validClip =
				skel.CurrentClip >= 0 &&
				skel.CurrentClip < static_cast<int>(skel.Clips.size()) &&
				skel.Clips[skel.CurrentClip] != nullptr;

			// Dispara a transição SÓ quando CurrentClip realmente muda.
			// Assim o Inspector (ou o Script Editor) só precisa escrever
			// CurrentClip = N, e o crossfade acontece sozinho.
			if (skel.CurrentClip != skel._AppliedClip)
			{
				if (validClip)
				{
					// Primeira aplicação = corte seco. Fazer crossfade a
					// partir do nada faria o personagem "brotar" da T-pose
					// no primeiro frame de todo Play.
					const bool first = (skel._AppliedClip == -2);

					skel.Player.Play(skel.Clips[skel.CurrentClip],
						first ? 0.0f : skel.BlendTime);
				}

				skel._AppliedClip = skel.CurrentClip;
			}

			// Avalia a pose mesmo com o tempo parado — é o que mostra o
			// personagem na pose certa fora do Play, em vez de colapsado.
			// Sem clipe nenhum, o Player devolve bind pose.
			skel.Player.Update(*skeleton, deltaTime, advance);

			AnimationSampler::BuildSkinningMatrices(*skeleton,
				skel.Player.GetPose(),
				skel.BonePalette,
				skel.ShowSkeleton ? &skel.BoneGlobals : nullptr);
		}
	}

} // namespace axe
