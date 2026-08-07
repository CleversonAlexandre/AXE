#pragma once
#include "axe/core/types.hpp"
#include "axe/audio/audio_device.hpp"
#include "axe/audio/sound_event.hpp"

#include <memory>
#include <string>

namespace axe
{
	class AudioClip;

	// ── AudioSourceComponent ─────────────────────────────────────────────────
	//
	// Uma fonte de som presa a uma entidade. Referencia o clipe por UUID —
	// mesmo modelo do ParticleSystemComponent e do MaterialComponent: o
	// componente guarda a REFERENCIA e um cache runtime, nunca o dado.
	//
	// Os campos com prefixo `_` sao runtime puro: nao vao pro .axescene, e
	// atravessam o Play/Stop apenas porque o snapshot e um clone de memoria.
	struct AXE_API AudioSourceComponent
	{
		std::string ClipAssetUUID;

		// Cache resolvido sob demanda pelo AudioWorld. Deliberadamente NAO
		// resolvido no SceneSerializer, ao contrario do ParticleSystem:
		// decodificar cada .wav durante o load travaria a abertura da cena
		// proporcionalmente ao numero de fontes, e o device de audio pode
		// nem existir na hora do load. Resolver no primeiro uso custa o
		// mesmo total e nao bloqueia nada.
		std::shared_ptr<AudioClip> Data;

		float Volume = 1.0f;
		float Pitch = 1.0f;
		bool  Loop = false;

		// Dispara sozinha ao entrar em Play. Musica de ambiente e ruido de
		// maquina querem isso; som de tiro nao.
		bool  PlayOnStart = true;

		// 3D: posicao da entidade + atenuacao por distancia. Desligue para
		// musica e narracao, que devem soar iguais em qualquer lugar.
		bool  Is3D = true;
		float MinDistance = 1.0f;
		float MaxDistance = 100.0f;

		// Categoria para a visualizacao. Um Sound Cue apontado por esta fonte
		// SOBRESCREVE isto — a categoria e propriedade do som, nao de quem o
		// toca; este campo serve para .wav cru, que nao tem onde guardar.
		SoundCategory Category = SoundCategory::Generic;

		// Bus de mixagem. Um Sound Cue apontado por esta fonte sobrescreve,
		// pela mesma razao da categoria.
		AudioBus Bus = AudioBus::SFX;

		// Prioridade no limite de vozes. Sobrescrita pelo cue, se houver.
		float Priority = 0.5f;

		// Doppler: 0 desliga, 1 e o efeito correto. Fica em 1 por padrao
		// porque uma fonte parada tem velocidade zero e o efeito nao aparece
		// — quem nao se move nao paga nada por ele estar ligado.
		float DopplerFactor = 1.0f;

		// Rampas de volume, em segundos. Zero = corte seco.
		//
		// Ficam no COMPONENTE e nao no cue porque sao propriedade de COMO
		// esta fonte entra e sai da cena — a mesma musica pode entrar suave
		// numa area e cortar seca noutra.
		float FadeInTime = 0.0f;
		float FadeOutTime = 0.0f;

		// ── Runtime ──────────────────────────────────────────────────────
		VoiceHandle _Voice = InvalidVoice;

		// Arestas de comando consumidas pelo AudioWorld no proximo tick.
		//
		// Sao flags, e nao chamadas diretas ao AudioEngine, porque quem
		// aperta o botao (Inspector, e o script no A3) nao sabe — e nao deve
		// saber — se a fonte ja tem voice viva, se a cena esta pausada, ou
		// se o clipe ja foi resolvido. Quem sabe disso e o AudioWorld.
		bool _PlayRequested = false;
		bool _StopRequested = false;

		// O que o Sound Cue sorteou NO DISPARO desta voice.
		//
		// Guardado porque o AudioWorld reenvia os parametros todo frame: sem
		// isto, ou o cue seria reavaliado por frame (pitch novo a cada frame
		// = tremolo), ou os multiplicadores se perderiam no segundo frame e o
		// som pularia de volume. Para .wav cru fica tudo neutro.
		float _ActiveVolumeMul = 1.0f;
		float _ActivePitchMul = 1.0f;
		bool  _ActiveOverrideAtten = false;
		float _ActiveMinDistance = 1.0f;
		float _ActiveMaxDistance = 100.0f;
		bool  _ActiveIs3D = true;
		SoundCategory _ActiveCategory = SoundCategory::Generic;
		AudioBus      _ActiveBus = AudioBus::SFX;
		float         _ActivePriority = 0.5f;

		// Posicao do frame anterior, para derivar velocidade quando a
		// entidade nao tem corpo fisico. _HasLastPos evita que o PRIMEIRO
		// frame produza uma velocidade absurda a partir de um "anterior" que
		// nunca existiu.
		glm::vec3 _LastPos{ 0.0f };
		bool      _HasLastPos = false;
	};

} // namespace axe