#include "axe/audio/audio_world.hpp"
#include "axe/audio/audio_engine.hpp"
#include "axe/audio/audio_clip.hpp"
#include "axe/audio/sound_cue.hpp"
#include "axe/audio/audio_source_component.hpp"
#include "axe/audio/audio_listener_component.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/log/log.hpp"

#include <entt/entt.hpp>

namespace axe
{
	namespace
	{
		// Parametros de uma voice ja resolvida.
		//
		// `rs` carrega o que o Sound Cue decidiu (qual wave, que
		// multiplicadores, que atenuacao). Para um .wav cru ele vem neutro,
		// e a conta abaixo da exatamente o que dava antes do cue existir.
		// ── Velocidade derivada ──────────────────────────────────────────
		//
		// Deriva de posicao entre frames, com duas defesas:
		//
		//   1. Primeiro frame nao produz velocidade — nao ha "anterior".
		//   2. Salto grande demais e TELEPORTE, nao movimento. Respawn, troca
		//      de cena ou cutscene moveriam a fonte dezenas de metros num
		//      frame, e o Doppler traduziria isso num guincho absurdo. Melhor
		//      perder o efeito num frame do que produzir um artefato.
		glm::vec3 DeriveVelocity(const glm::vec3& current, glm::vec3& last,
			bool& hasLast, float dt)
		{
			if (dt <= 1e-5f || !hasLast)
			{
				last = current;
				hasLast = true;
				return glm::vec3(0.0f);
			}

			const glm::vec3 delta = current - last;
			last = current;

			// 3 unidades num frame a 60Hz sao 180 u/s — acima de qualquer
			// coisa que ande de verdade numa cena.
			if (glm::length(delta) > 3.0f)
				return glm::vec3(0.0f);

			return delta / dt;
		}

		VoiceParams BuildParams(const AudioSourceComponent& src,
			const ResolvedSound& rs,
			const glm::vec3& position)
		{
			VoiceParams p;
			p.Position = position;

			// MULTIPLICA. O volume do componente e o do cue compoem: o cue diz
			// "este passo saiu 8% mais baixo que o normal", o componente diz
			// "esta fonte toda e mais baixa". Nenhum dos dois anula o outro.
			p.Volume = src.Volume * rs.VolumeMultiplier;
			p.Pitch = src.Pitch * rs.PitchMultiplier;

			p.Bus = rs.Bus;
			p.Priority = rs.Priority;
			p.DopplerFactor = src.DopplerFactor;
			p.Spatialized = rs.OverrideAttenuation ? rs.Is3D : src.Is3D;
			p.MinDistance = rs.OverrideAttenuation ? rs.MinDistance : src.MinDistance;
			p.MaxDistance = rs.OverrideAttenuation ? rs.MaxDistance : src.MaxDistance;

			// LowPassCutoff / DirectGain / ReverbSend ficam no neutro: quem
			// os preenche e a camada de propagacao acustica (A5), que ainda
			// nao existe. O AudioWorld e exatamente o lugar onde ela vai
			// entrar — entre ler o ECS e mandar pro AudioEngine.
			return p;
		}
	}

	void AudioWorld::OnUpdate(Scene& scene, float deltaTime,
		bool inPlay, bool paused,
		const glm::vec3& listenerPos,
		const glm::vec3& listenerForward,
		const glm::vec3& listenerUp)
	{
		if (!AudioEngine::IsInitialized())
			return;

		// ── Pause ────────────────────────────────────────────────────────
		//
		// So age na TRANSICAO. O editor tem varios caminhos que voltam de
		// Pause pra Play (botao, tecla, Esc); detectar a borda aqui evita
		// ter que instrumentar cada um deles — e evita que um deles seja
		// esquecido, deixando o som mudo pro resto da sessao.
		if (paused != m_Paused)
		{
			m_Paused = paused;
			AudioEngine::SetAllPaused(paused);
		}

		if (paused)
			return;

		auto& registry = scene.GetRegistry();

		// ── Listener ─────────────────────────────────────────────────────
		//
		// Componente autorado vence; camera ativa e o fallback. Roda TAMBEM
		// em Edit, de proposito: e o que faz o preview de som do Animation
		// Editor sair do lugar certo em relacao a camera do viewport.
		glm::vec3 pos = listenerPos;
		glm::vec3 forward = listenerForward;
		glm::vec3 up = listenerUp;

		{
			entt::entity chosen = entt::null;
			int primaryCount = 0;

			for (auto entity : registry.view<AudioListenerComponent>())
			{
				const auto& lc = registry.get<AudioListenerComponent>(entity);

				if (!lc.IsPrimary)
					continue;

				++primaryCount;

				if (chosen == entt::null)
					chosen = entity;
			}

			if (primaryCount > 1 && !m_WarnedMultipleListeners)
			{
				m_WarnedMultipleListeners = true;
				AXE_CORE_WARN("AudioWorld: {} AudioListenerComponent marcados como Primary. "
					"O primeiro vence, os demais sao ignorados.", primaryCount);
			}

			if (chosen != entt::null)
			{
				const auto& lc = registry.get<AudioListenerComponent>(chosen);
				const glm::mat4 world = scene.GetWorldTransform(chosen);

				// Posicao SEMPRE do componente: e o motivo de ele existir.
				pos = glm::vec3(world[3]);

				// Orientacao so quando pedida. Com UseCameraOrientation
				// ligado, forward/up continuam sendo os do fallback (a camera
				// ativa), que ja chegaram preenchidos — nao ha nada a fazer.
				if (!lc.UseCameraOrientation)
				{
					// Convencao do resto da engine: frente e -Z (mesma da
					// EditorCamera). Normalizar porque a entidade pode estar
					// escalada, e escala nao pode virar orientacao.
					const glm::vec3 z = glm::vec3(world[2]);
					const glm::vec3 y = glm::vec3(world[1]);

					if (glm::length(z) > 1e-6f) forward = -glm::normalize(z);
					if (glm::length(y) > 1e-6f) up = glm::normalize(y);
				}
			}
		}

		AudioEngine::GetDevice()->SetListener(pos, forward, up);

		// Velocidade do listener, derivada da propria pose: nem a camera do
		// editor nem a GameCamera expoem velocidade, e derivar aqui evita
		// obrigar as duas a manter um campo que so o audio usaria.
		AudioEngine::GetDevice()->SetListenerVelocity(
			DeriveVelocity(pos, m_LastListenerPos, m_HasLastListenerPos, deltaTime));

		// ── Fontes ───────────────────────────────────────────────────────
		auto view = registry.view<AudioSourceComponent, TransformComponent>();

		for (auto entity : view)
		{
			auto& src = view.get<AudioSourceComponent>(entity);

			// Pedido explicito de parar tem prioridade sobre tudo.
			if (src._StopRequested)
			{
				src._StopRequested = false;
				src._PlayRequested = false;

				if (src._Voice != InvalidVoice)
				{
					AudioEngine::ClearVoiceReport(src._Voice);

					// Com fade, o device segura a voice ate a rampa acabar e
					// so entao a destroi. O componente solta o handle aqui de
					// qualquer forma: quem pediu Stop nao deve continuar
					// mandando na voice durante a saida.
					if (src.FadeOutTime > 0.0f)
						AudioEngine::StopWithFade(src._Voice, src.FadeOutTime);
					else
						AudioEngine::Stop(src._Voice);

					src._Voice = InvalidVoice;
				}

				continue;
			}

			if (src._PlayRequested)
			{
				src._PlayRequested = false;

				if (src._Voice != InvalidVoice)
				{
					AudioEngine::Stop(src._Voice);
					src._Voice = InvalidVoice;
				}

				// Resolve A CADA disparo, nunca em cache: se o UUID aponta
				// pra um Sound Cue, avaliar de novo e o que faz o no Random
				// sortear uma variacao diferente. Guardar o resultado
				// transformaria o cue num .wav fixo depois do primeiro play —
				// o bug seria "meu cue tem cinco passos e so toca o mesmo".
				const ResolvedSound rs = AudioEngine::ResolveSound(src.ClipAssetUUID);

				if (rs.IsValid())
				{
					// So pro Inspector mostrar duracao/canais do que esta
					// tocando agora. Nao e cache de resolucao.
					src.Data = rs.Clip;

					const glm::vec3 p = glm::vec3(scene.GetWorldTransform(entity)[3]);
					const VoiceParams startParams = BuildParams(src, rs, p);
					src._Voice = AudioEngine::Play(src.Data, startParams, src.Loop);

					// Rampa de entrada. Feita DEPOIS do Play porque a voice
					// precisa existir — e o volume alvo e o que os parametros
					// ja calcularam, para que o fade termine exatamente no
					// volume que a fonte teria sem ele.
					if (src.FadeInTime > 0.0f && src._Voice != InvalidVoice)
						AudioEngine::FadeVoice(src._Voice, 0.0f,
							startParams.Volume, src.FadeInTime);
					src._ActiveVolumeMul = rs.VolumeMultiplier;
					src._ActivePitchMul = rs.PitchMultiplier;
					src._ActiveOverrideAtten = rs.OverrideAttenuation;
					src._ActiveMinDistance = rs.MinDistance;
					src._ActiveMaxDistance = rs.MaxDistance;
					src._ActiveIs3D = rs.Is3D;

					// Cue manda na categoria; sem cue, vale a do componente.
					src._ActiveCategory = (rs.Category != SoundCategory::Generic)
						? rs.Category : src.Category;

					// Cue manda no bus; sem cue, vale o do componente.
					src._ActiveBus = (rs.Bus != AudioBus::SFX) ? rs.Bus : src.Bus;

					// Cue manda; sem cue (Priority no default), vale a do
					// componente.
					src._ActivePriority = (rs.Priority != 0.5f)
						? rs.Priority : src.Priority;
				}
			}

			// Em Edit, nada toca sozinho — mas uma voice pedida
			// explicitamente pelo preview do Inspector continua viva e
			// continua sendo atualizada abaixo.
			if (!inPlay && src._Voice == InvalidVoice)
				continue;

			if (src._Voice == InvalidVoice)
				continue;

			// A voice pode ter terminado sozinha (one-shot nao-loop): o
			// device ja a recolheu no Reap e o handle virou morto. Zerar
			// aqui e o que permite ao Inspector mostrar "parada" e ao
			// PlayOnStart nao achar que ainda esta tocando.
			if (!AudioEngine::IsPlaying(src._Voice))
			{
				src._Voice = InvalidVoice;
				continue;
			}

			// Reusa o que o cue decidiu NO DISPARO. Reavaliar por frame
			// sortearia um pitch novo a cada frame — o som viraria um
			// tremolo. O cue decide uma vez, no Play; o resto do tempo a
			// voice so acompanha a posicao.
			// Velocidade da fonte: a fisica sabe melhor que qualquer
			// derivada. So quando nao ha corpo fisico e que estimamos por
			// diferenca de posicao.
			const glm::vec3 curPos = glm::vec3(scene.GetWorldTransform(entity)[3]);
			glm::vec3 velocity(0.0f);

			if (auto* rb = registry.try_get<RigidbodyComponent>(entity))
				velocity = rb->CurrentVelocity;
			else if (auto* cc = registry.try_get<CharacterControllerComponent>(entity))
				velocity = cc->Velocity;
			else
				velocity = DeriveVelocity(curPos, src._LastPos, src._HasLastPos, deltaTime);

			ResolvedSound live;
			live.VolumeMultiplier = src._ActiveVolumeMul;
			live.PitchMultiplier = src._ActivePitchMul;
			live.OverrideAttenuation = src._ActiveOverrideAtten;
			live.MinDistance = src._ActiveMinDistance;
			live.MaxDistance = src._ActiveMaxDistance;
			live.Is3D = src._ActiveIs3D;
			live.Bus = src._ActiveBus;
			live.Priority = src._ActivePriority;

			VoiceParams vp = BuildParams(src, live, curPos);
			vp.Velocity = velocity;

			AudioEngine::SetVoiceParams(src._Voice, vp);

			// Refresca o pulso da visualizacao. Fonte 2D nao reporta: musica
			// nao tem lugar no mundo, e desenhar um circulo pra ela seria
			// ruido visual puro.
			if (vp.Spatialized)
				AudioEngine::ReportVoice(src._Voice, curPos, vp.Volume, vp.MaxDistance,
					true, src._ActiveCategory);
		}
	}

	void AudioWorld::OnScenePlay(Scene& scene)
	{
		if (!AudioEngine::IsInitialized())
			return;

		m_Paused = false;
		AudioEngine::SetAllPaused(false);

		auto& registry = scene.GetRegistry();

		for (auto entity : registry.view<AudioSourceComponent>())
		{
			auto& src = registry.get<AudioSourceComponent>(entity);

			// Handle vindo do snapshot ou de uma sessao anterior nao vale
			// nada aqui: as voices daquela sessao ja morreram no Stop.
			src._Voice = InvalidVoice;
			src._StopRequested = false;

			if (src.PlayOnStart)
				src._PlayRequested = true;
		}
	}

	void AudioWorld::OnSceneStop(Scene& scene)
	{
		if (!AudioEngine::IsInitialized())
			return;

		auto& registry = scene.GetRegistry();

		for (auto entity : registry.view<AudioSourceComponent>())
		{
			auto& src = registry.get<AudioSourceComponent>(entity);

			if (src._Voice != InvalidVoice)
			{
				AudioEngine::ClearVoiceReport(src._Voice);
				AudioEngine::Stop(src._Voice);
			}

			src._Voice = InvalidVoice;
			src._PlayRequested = false;
			src._StopRequested = false;
		}

		m_Paused = false;
		AudioEngine::SetAllPaused(false);
	}

} // namespace axe