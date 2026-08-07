#include "animation_world.hpp"

#include "axe/animation/animation_sampler.hpp"
#include "axe/animation/pose.hpp"
#include "axe/animation/anim_graph_instance.hpp"

// RigView chega ao anim_node.hpp so como declaracao adiantada (animation nao
// deve depender do subsistema de rig no header). Aqui ela e HERDADA, entao
// precisa do tipo completo — e este e o unico .cpp que precisa.
#include "axe/animation/rig/rig_node_base.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/particles/particle_system_asset.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/audio/audio_engine.hpp"
#include "axe/log/log.hpp"

#include <entt/entt.hpp>

#include <algorithm>

namespace axe
{
	namespace
	{
		// Adaptador entre a camera da cena e a interface que o rig conhece.
		//
		// Existe porque animation nao deve depender de scene no HEADER — a
		// dependencia so vale aqui, no .cpp, onde o AnimationWorld ja tem a
		// cena em maos por natureza.
		struct SceneView final : RigView
		{
			glm::mat4 Cam{ 1.0f };
			float     Fov = 60.0f;

			glm::mat4 GetCameraWorld() const override { return Cam; }
			float     GetFovDegrees() const override { return Fov; }
		};

		// A camera PRIMARIA da cena. Mesma regra que o editor ja usa pra
		// escolher a camera de Play — a primeira com IsPrimary vence.
		//
		// Devolve false quando nao ha nenhuma: cena so de preview, ou uma em
		// que ninguem marcou a camera ainda. Os nos de camera do rig viram
		// no-op e o pino Valid sai false, que e o comportamento certo.
		bool FindPrimaryCamera(entt::registry& reg, SceneView& out)
		{
			for (auto e : reg.view<CameraComponent>())
			{
				const auto& cam = reg.get<CameraComponent>(e);

				if (!cam.IsPrimary)
					continue;

				const auto* tc = reg.try_get<TransformComponent>(e);

				if (!tc)
					continue;

				out.Cam = tc->Data.GetMatrix();
				out.Fov = cam.Fov;

				return true;
			}

			return false;
		}
	}

	void AnimationWorld::OnUpdate(Scene& scene, float deltaTime, bool inPlay)
	{
		auto& registry = scene.GetRegistry();

		// UMA busca por frame, e nao uma por personagem: a camera e a mesma
		// pra todo mundo na cena.
		// Nome distinto de propósito: mais abaixo há um `auto view =
		// registry.view<SkeletalMeshComponent>()`, e duas variáveis `view` no
		// mesmo escopo fazem a segunda esconder a primeira — o erro real vira
		// "SceneView não tem begin()", que aponta pro lugar errado.
		SceneView sceneView;
		const bool hasCamera = FindPrimaryCamera(registry, sceneView);

		// FX de notify expiram por conta propria. O guard de valid() cobre o
		// Stop: o restore do snapshot ja destruiu as entidades de Play, e a
		// lista so precisa esquecer os handles mortos.
		if (!m_NotifyFx.empty())
		{
			m_NotifyFx.erase(
				std::remove_if(m_NotifyFx.begin(), m_NotifyFx.end(),
					[&](NotifyFx& fx)
					{
						if (!registry.valid(fx.Entity))
							return true;

						fx.Ttl -= deltaTime;

						if (fx.Ttl > 0.0f)
							return false;

						scene.DestroyEntity(fx.Entity);
						return true;
					}),
				m_NotifyFx.end());
		}

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

				// Transform do personagem no mundo — para o Foot IK converter
				// a posição do pé pra espaço de mundo, consultar o chão via
				// raycast e voltar. Nós que não tocam o mundo ignoram.
				//
				// try_get: um personagem sem TransformComponent (não deveria
				// acontecer, mas o editor permite estados estranhos) cai na
				// identidade e o IK trabalha como se estivesse na origem, em
				// vez de crashar.
				glm::mat4 worldXform(1.0f);

				if (auto* tc = registry.try_get<TransformComponent>(entity))
					worldXform = tc->Data.GetMatrix();

				// DUAS FASES.
				//
				// Update avança o tempo e decide as transições; Evaluate produz
				// a pose. Separados porque, num crossfade, o estado que está
				// SAINDO precisa continuar avançando — senão ele congela no
				// meio da transição e o pé escorrega no chão, bem visível.
				// allowWorldQueries = inPlay: só a cena REAL em Play tem física
				// ativa pro Foot IK consultar. No preview do editor (inPlay
				// false) o raycast cairia no mundo Jolt da cena principal —
				// então o IK fica inerte lá, e a pose passa intacta.
				// Sem camera na cena, fica nulo — o rig trata isso e nao inventa
				// uma posicao de observador que nao existe.
				skel.GraphInstance.View = hasCamera ? &sceneView : nullptr;

				skel.GraphInstance.Update(*skeleton, deltaTime, advance, worldXform, inPlay);
				skel.GraphInstance.Evaluate(*skeleton, m_ScratchPose, worldXform, inPlay);

				DispatchNotifies(scene, entity,
					skel.GraphInstance.GetFiredNotifies(), inPlay);

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
			//
			// Notifies do caminho manual: mesmo cruzamento do ClipPlayer do
			// grafo, so que aqui em volta do AnimationPlayer (cujo tempo e
			// CRU — wrap dos dois lados antes de comparar).
			const auto& manualClip = validClip ? skel.Clips[skel.CurrentClip] : nullptr;
			const float manualPrev = manualClip ? manualClip->WrapTime(skel.Player.GetTime()) : 0.0f;

			skel.Player.Update(*skeleton, deltaTime, advance);

			if (manualClip && advance && !manualClip->Notifies.empty())
			{
				const float manualNow = manualClip->WrapTime(skel.Player.GetTime());

				static thread_local std::vector<AnimNotify> s_ManualFired;
				s_ManualFired.clear();

				for (const auto& n : manualClip->Notifies)
				{
					const bool hit = (manualNow >= manualPrev)
						? (n.Time > manualPrev && n.Time <= manualNow)
						: (n.Time > manualPrev || n.Time <= manualNow);

					if (hit)
						s_ManualFired.push_back(n);
				}

				if (!s_ManualFired.empty())
					DispatchNotifies(scene, entity, s_ManualFired, inPlay);
			}

			AnimationSampler::BuildSkinningMatrices(*skeleton,
				skel.Player.GetPose(),
				skel.BonePalette,
				skel.ShowSkeleton ? &skel.BoneGlobals : nullptr);
		}
	}

	void AnimationWorld::DispatchNotifies(Scene& scene, entt::entity character,
		const std::vector<AnimNotify>& fired, bool inPlay)
	{
		if (fired.empty())
			return;

		auto& registry = scene.GetRegistry();

		for (const auto& n : fired)
		{
			switch (n.Type)
			{
			case AnimNotify::Kind::Particle:
			{
				if (n.Payload.empty())
					break;

				// So em PLAY: entidades de Play morrem no restore do Stop —
				// a cena salva nunca ganha lixo. Em Edit, o lugar de ver o
				// efeito e o Animation Editor.
				if (!inPlay)
				{
					static bool s_HintOnce = false;

					if (!s_HintOnce)
					{
						AXE_CORE_INFO("AnimNotify '{}': particulas na CENA aparecem em Play. (Em Edit, use o Animation Editor.)", n.Name);
						s_HintOnce = true;
					}

					break;
				}

				const AssetRecord* rec = AssetDatabase::Get().GetByUUID(n.Payload);

				if (!rec)
				{
					AXE_CORE_WARN("AnimNotify '{}': asset de particula nao encontrado.", n.Name);
					break;
				}

				auto psAsset = ParticleSystemAsset::LoadFromFile(rec->FilePath);

				if (!psAsset)
					break;

				auto e = scene.CreateEntity("NotifyFX");

				// CreateEntity ja adiciona o Transform — pegar, nao emplace.
				auto& tc = registry.get<TransformComponent>(e);

				glm::vec3 basePos{ 0.0f };
				glm::vec3 baseScale{ 1.0f };

				if (auto* charTc = registry.try_get<TransformComponent>(character))
				{
					basePos = charTc->Data.Position;
					baseScale = charTc->Data.Scale;
				}

				// Offset autorado no espaco do PERSONAGEM: escala junto com
				// ele (personagem 0.015 nao pode jogar o FX a metros).
				// Ancoragem no OSSO (Socket/Attached) e a proxima etapa.
				tc.Data.Position = basePos + n.LocationOffset * baseScale;
				tc.Data.Rotation = glm::radians(n.RotationOffset);
				tc.Data.Scale = n.Scale;

				auto& ps = registry.emplace<ParticleSystemComponent>(e);
				ps.Data = psAsset;
				ps.ParticleAssetUUID = n.Payload;
				ps.Playing = true;
				ps.EmitterRuntimes.resize(psAsset->Emitters.size());

				m_NotifyFx.push_back({ e, 5.0f });
				break;
			}

			case AnimNotify::Kind::Event:
				// Proxima etapa: EventBus -> Script Editor. Por ora o log
				// prova que o cruzamento aconteceu no frame certo.
				AXE_CORE_INFO("AnimNotify (event): '{}'", n.Name);
				break;

			case AnimNotify::Kind::Sound:
			{
				if (n.Payload.empty())
					break;

				// Dispara AQUI, no frame do cruzamento, e nao numa fila
				// consumida por algum tick depois.
				//
				// Som de passo atrasado um frame em relacao ao pe tocando o
				// chao e audivel — e acertar esse instante e literalmente
				// pra isso que a timeline de notify existe. One-shot nao
				// espera mundo nenhum; voice persistente (AudioSourceComponent)
				// e que sera atualizada pelo AudioWorld, no A2.
				//
				// Toca em Edit TAMBEM, ao contrario do notify de particula.
				// A regra la e "so em Play" porque particula CRIA ENTIDADE na
				// cena e sujaria o arquivo salvo. Som nao cria nada: e
				// fire-and-forget. Silenciar o preview do Animation Editor
				// seria tirar do usuario justamente o feedback que ele abriu
				// a janela pra ter.
				//
				// Agora ESPACIAL: o AudioWorld alimenta a pose do listener
				// todo frame (camera ativa, ou AudioListenerComponent), e
				// o som sai de onde o personagem esta.
				//
				// A posicao segue exatamente a mesma regra do notify de
				// particula, logo acima: base do personagem + LocationOffset
				// escalado pela escala DELE. Um personagem em 0.015 nao pode
				// ter o som deslocado a metros por um offset autorado em
				// centimetros.
				//
				// Ancoragem no OSSO (Socket/Attached) continua pendente —
				// para som E para particula. Fazer so para um dos dois
				// deixaria o efeito visual e o efeito sonoro do MESMO notify
				// em lugares diferentes, que e pior do que os dois estarem
				// igualmente aproximados.
				glm::vec3 soundPos{ 0.0f };
				glm::vec3 soundScale{ 1.0f };

				if (auto* charTc = registry.try_get<TransformComponent>(character))
				{
					soundPos = charTc->Data.Position;
					soundScale = charTc->Data.Scale;
				}

				soundPos += n.LocationOffset * soundScale;

				// Em janela de preview, 2D: ver comentario de
				// SetSoundAudition. No jogo, espacializado normalmente.
				if (m_SoundAudition)
					AudioEngine::PlayOneShot(n.Payload, n.Volume, n.Pitch);
				else
					AudioEngine::PlayOneShotAt(n.Payload, soundPos, n.Volume, n.Pitch);

				break;
			}
			}
		}
	}

} // namespace axe