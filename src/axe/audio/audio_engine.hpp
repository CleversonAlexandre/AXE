#pragma once
#include "axe/core/types.hpp"
#include "axe/audio/audio_device.hpp"
#include "axe/audio/sound_event.hpp"

#include <memory>
#include <string>
#include <vector>

namespace axe
{
	class AudioClip;
	class SoundCueAsset;

	// ── ResolvedSound ────────────────────────────────────────────────────────
	//
	// O que sai de "resolver um UUID de som". A costura polimorfica do
	// sistema: quem toca som (AudioSourceComponent, AnimNotify, script) passa
	// um UUID e recebe isto — sem nunca perguntar se aquilo era um .wav cru ou
	// um .axecue.
	//
	// E o equivalente do USoundBase da Unreal, do qual USoundWave e USoundCue
	// derivam. Aqui nao ha heranca: ha uma funcao de resolucao, porque os tres
	// call sites ja passavam por um funil so.
	struct AXE_API ResolvedSound
	{
		std::shared_ptr<AudioClip> Clip;

		// Multiplicadores vindos do cue. Compoem com o volume/pitch de quem
		// pediu — nao substituem.
		float VolumeMultiplier = 1.0f;
		float PitchMultiplier = 1.0f;

		// Um no de Attenuation no cue manda na distancia, sobrepondo o que
		// estiver no componente.
		bool  OverrideAttenuation = false;
		float MinDistance = 1.0f;
		float MaxDistance = 100.0f;
		bool  Is3D = true;

		// Categoria para a visualizacao. Vem do cue quando ha um; um .wav
		// cru e Generic.
		SoundCategory Category = SoundCategory::Generic;

		// Bus vindo do cue. .wav cru cai em SFX.
		AudioBus Bus = AudioBus::SFX;

		// Prioridade no limite de vozes. Vem do cue; .wav cru fica no meio.
		float Priority = 0.5f;

		bool IsValid() const;
	};

	// ── AudioEngine ──────────────────────────────────────────────────────────
	//
	// Facade estatica sobre o AudioDevice: mesma posicao que RenderCommand
	// ocupa em relacao a RendererAPI. Quem toca som (AnimationWorld, script,
	// futuramente AudioWorld) fala com esta classe, nunca com o backend.
	//
	// Alem do repasse, ela e dona de duas coisas:
	//
	//   1. O CACHE DE CLIPS POR UUID. Um .wav referenciado por vinte notifies
	//      e decodificado uma vez. Mesma moeda que material e cena usam.
	//
	//   2. A DISTINCAO ENTRE OS DOIS CAMINHOS DE DISPARO — que e a razao de
	//      PlayOneShot existir separado de Play:
	//
	//        one-shot  (AnimNotify, PlaySound de script): dispara AGORA, na
	//                  chamada, com a posicao amostrada naquele instante. Nao
	//                  espera tick de mundo nenhum. Passo e tiro nao podem
	//                  atrasar um frame em relacao ao visual — dessincronia
	//                  de passo e exatamente o que o Animation Editor existe
	//                  pra voce acertar.
	//
	//        persistente (AudioSourceComponent, no A2): voice de vida longa,
	//                  cuja posicao e volume o AudioWorld atualiza por frame,
	//                  DEPOIS que animacao e fisica resolveram os transforms.
	//
	//      Deferir one-shot pro tick "por consistencia" seria trocar um
	//      problema que nao existe por um bug audivel.
	class AXE_API AudioEngine
	{
	public:
		static bool Init();
		static void Shutdown();
		static bool IsInitialized();

		// GAME THREAD, uma vez por frame: destroi voices terminadas e
		// envelhece os pulsos da visualizacao.
		//
		// deltaTime ganhou lugar aqui no A5: sem ele o pulso de um one-shot
		// nunca apagaria, e a tela encheria de marcas permanentes.
		static void Update(float deltaTime);

		static AudioDevice* GetDevice();

		// ── Clips ────────────────────────────────────────────────────────
		// Resolve UUID -> AssetDatabase -> arquivo -> AudioClip (com cache).
		static std::shared_ptr<AudioClip> GetClip(const std::string& uuid);

		// Hot reload: descarta a entrada do cache. O UUID continua o mesmo —
		// o proximo GetClip decodifica de novo e devolve uma INSTANCIA NOVA.
		// Voices ja tocando seguram a antiga e terminam sem cortar.
		static void InvalidateClip(const std::string& uuid);
		static void ClearClipCache();

		// Traduz "nome do asset" -> UUID. Aceita tambem um UUID direto, e
		// nesse caso apenas o devolve.
		//
		// Existe por causa do script: ninguem digita um UUID num Blueprint
		// nem num .cpp de gameplay. `PlaySound2D("Explosion")` e o que uma
		// pessoa escreve; a alternativa seria um pin de asset no grafo, que
		// e trabalho proprio (renderizacao do pin, serializacao, cast) e nao
		// cabe neste patch.
		//
		// Nome ambiguo (dois .wav com o mesmo nome em pastas diferentes)
		// resolve pelo primeiro e avisa uma vez — silencio ali seria pior:
		// o som certo tocaria em uma maquina e o errado em outra, conforme
		// a ordem do scan.
		static std::string ResolveAudioAsset(const std::string& nameOrUuid);

		// Wave cru ou Sound Cue — quem chama nao precisa saber qual.
		//
		// IMPORTANTE: para um cue, isto AVALIA o grafo, entao chamadas
		// sucessivas com o mesmo UUID podem devolver waves diferentes. E o
		// ponto do no Random. Por isso nao ha cache do resultado, so do
		// asset — e por isso ninguem pode guardar um ResolvedSound e reusar.
		static ResolvedSound ResolveSound(const std::string& nameOrUuid);

		static std::shared_ptr<SoundCueAsset> GetCue(const std::string& uuid);

		// ── Disparo ──────────────────────────────────────────────────────
		// 2D: ignora posicao e listener. E o modo do A1 — sem listener
		// posicionado, espacializar e pior do que nao espacializar.
		static VoiceHandle PlayOneShot(const std::string& uuid,
			float volume = 1.0f, float pitch = 1.0f);

		// 3D: ja existe pra nao precisar mexer nos call sites no A2, mas so
		// vira audivelmente espacial quando o AudioWorld alimentar o listener.
		static VoiceHandle PlayOneShotAt(const std::string& uuid,
			const glm::vec3& position,
			float volume = 1.0f, float pitch = 1.0f);

		// Controle completo — usado pelas voices persistentes do A2.
		static VoiceHandle Play(const std::shared_ptr<AudioClip>& clip,
			const VoiceParams& params, bool loop);

		static void SetVoiceParams(VoiceHandle voice, const VoiceParams& params);
		static void Stop(VoiceHandle voice);
		static bool IsPlaying(VoiceHandle voice);
		static void StopAll();
		static void SetVoicePaused(VoiceHandle voice, bool paused);
		static void SetAllPaused(bool paused);

		// Cursor da voice em segundos; negativo se ela nao existe mais.
		static float GetVoiceCursorSeconds(VoiceHandle voice);

		static void SetMasterVolume(float volume);
		static float GetMasterVolume();

		// ── Limite de vozes ──────────────────────────────────────────────
		static void SetMaxVoices(int maxVoices);
		static int  GetMaxVoices();
		static int  GetActiveVoiceCount();

		// ── Buses ────────────────────────────────────────────────────────
		//
		// E aqui que um menu de opcoes encosta: um slider por bus, e pronto.
		static void  SetBusVolume(AudioBus bus, float volume);
		static float GetBusVolume(AudioBus bus);

		// ── Fade ─────────────────────────────────────────────────────────
		static void FadeVoice(VoiceHandle voice, float from, float to, float seconds);
		static void StopWithFade(VoiceHandle voice, float seconds);

		// ── Visualizacao de som ──────────────────────────────────────────
		//
		// A lista existe SEMPRE, mesmo com a visualizacao desligada: e barata
		// (uma struct por som ativo, dezenas no pior caso) e serve tambem a
		// depuracao. Quem decide desenhar e o renderer.
		static const std::vector<SoundEvent>& GetActiveSounds();

		// Refresca o pulso de uma voice PERSISTENTE. Chamado pelo AudioWorld
		// todo frame: a fonte se move, e o pulso tem que ir junto.
		static void ReportVoice(VoiceHandle voice, const glm::vec3& position,
			float volume, float maxDistance, bool spatialized,
			SoundCategory category);

		// Encerra o pulso de uma voice que parou. Sem isto, uma fonte em loop
		// parada por script deixaria a marca na tela ate o Lifetime expirar.
		static void ClearVoiceReport(VoiceHandle voice);
	};

} // namespace axe