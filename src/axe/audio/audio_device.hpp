#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include <cstring>

namespace axe
{
	// ── PCM decodificado ─────────────────────────────────────────────────────
	//
	// O conteudo de um arquivo de audio depois de decodificado: float
	// intercalado, pronto pra ser lido pela audio thread.
	//
	// Vive SEMPRE atras de shared_ptr, e cada voice tocando segura uma copia.
	// Isso nao e estilo — e a unica coisa que impede o bug mais caro de um
	// sistema de audio: liberar o PCM enquanto o callback da audio thread
	// ainda esta lendo dele. Crash raro, sem stack util, irreproduzivel.
	// Com refcount, o dado fisicamente nao pode morrer debaixo de uma voice.
	//
	// Contrapartida: a voice tambem nao pode ser DESTRUIDA na audio thread,
	// senao o ultimo decremento do refcount libera memoria dentro do
	// callback — o pecado classico do audio. Por isso todo backend so
	// destroi voice em Reap(), chamado da game thread.
	struct AXE_API AudioPcmData
	{
		std::vector<float> Frames;          // intercalado: [L R L R ...]
		uint32_t           Channels = 0;
		uint32_t           SampleRate = 0;
		float              DurationSeconds = 0.0f;
	};

	using AudioPcmRef = std::shared_ptr<const AudioPcmData>;

	// Identificador opaco de uma voice em execucao. Zero = invalido.
	//
	// E um handle, e nao um ponteiro, pelo mesmo motivo que o resto da engine
	// referencia asset por UUID: quem guarda (componente, script, notify) nao
	// pode ficar dono de um objeto do backend cujo tempo de vida ele nao
	// controla. Voice que ja terminou vira handle morto, nao ponteiro solto.
	using VoiceHandle = uint32_t;
	inline constexpr VoiceHandle InvalidVoice = 0;

	// ── Buses ────────────────────────────────────────────────────────────────
	//
	// Grupos de mixagem. Existem por um motivo concreto: no minuto em que
	// alguem pede um menu de opcoes, "volume da musica" e "volume dos efeitos"
	// precisam ser controles separados — e sem bus a unica alternativa seria
	// varrer todas as voices vivas e multiplicar cada uma na mao, todo frame.
	//
	// Master nao e um grupo: e o volume do endpoint. Uma voice em Master toca
	// direto, sem passar por grupo nenhum — util para som de sistema que nao
	// deve obedecer aos sliders do jogador.
	enum class AudioBus : int
	{
		Master = 0,
		SFX,
		Music,
		Voice,
		UI,

		Count
	};

	inline const char* AudioBusToString(AudioBus b)
	{
		switch (b)
		{
		case AudioBus::Master: return "Master";
		case AudioBus::SFX:    return "SFX";
		case AudioBus::Music:  return "Music";
		case AudioBus::Voice:  return "Voice";
		case AudioBus::UI:     return "UI";
		default:               return "SFX";
		}
	}

	inline AudioBus AudioBusFromString(const char* s)
	{
		for (int i = 0; i < (int)AudioBus::Count; ++i)
			if (std::strcmp(AudioBusToString((AudioBus)i), s) == 0)
				return (AudioBus)i;

		return AudioBus::SFX;
	}

	// ── Parametros de uma voice ──────────────────────────────────────────────
	//
	// Sobre os tres ultimos campos (LowPassCutoff / DirectGain / ReverbSend):
	// eles nascem aqui SEM CONSUMIDOR. Nenhum codigo do A1 os preenche e o
	// backend do A1 so honra DirectGain.
	//
	// Existem agora porque a camada de propagacao acustica (A5 — Steam Audio
	// ou equivalente) nao produz som: ela produz exatamente estes numeros por
	// fonte, e alguem tem que carrega-los ate o backend. Se a struct nascesse
	// so com (posicao, volume, pitch), o A5 obrigaria a reescrever a interface
	// AudioDevice e todo backend que a implementa. Tres floats agora evitam
	// isso; o vocabulario segue Wwise/Steam Audio pra nao inventar termo novo.
	struct AXE_API VoiceParams
	{
		glm::vec3 Position{ 0.0f };
		float     Volume = 1.0f;
		float     Pitch = 1.0f;

		// Grupo de mixagem. SFX por padrao: e o que a esmagadora maioria dos
		// sons e, e um default de Master faria todo som novo ignorar os
		// sliders do jogador sem ninguem perceber.
		AudioBus  Bus = AudioBus::SFX;

		// Espacializacao. Em 2D (false) a voice ignora Position e listener —
		// e o modo certo pra UI, musica e, no A1, pros one-shots, ja que
		// ainda nao existe listener posicionado (isso chega no A2).
		bool      Spatialized = false;

		// Prioridade para o limite de vozes: 0 = descartavel, 1 = nunca cede
		// lugar. Musica e diálogo alto; passo e impacto baixo.
		//
		// 0.5 de padrao, e nao 1.0, de proposito: um default no topo faria o
		// primeiro som a estourar o teto vencer todos os outros so por ter
		// chegado antes, e o sistema de prioridade nao teria efeito nenhum
		// ate alguem descobrir que o campo existe.
		float     Priority = 0.5f;

		// ── Doppler ──────────────────────────────────────────────────────
		//
		// Velocidade da fonte, em unidades por segundo. O miniaudio calcula o
		// desvio de frequencia a partir dela e da velocidade do listener — e
		// sem ambas o Doppler fica ligado e inerte, que e como o sistema
		// estava ate agora.
		glm::vec3 Velocity{ 0.0f };

		// 0 desliga; 1 e o efeito fisicamente correto. Valores acima de 1
		// exageram — as vezes e o que se quer num carro de corrida.
		float     DopplerFactor = 1.0f;

		float     MinDistance = 1.0f;    // dentro disso, volume cheio
		float     MaxDistance = 100.0f;  // fora disso, atenuacao maxima

		// ── Reservados pra camada acustica (A5) ──────────────────────────
		float     LowPassCutoff = 20000.0f; // obstruction: LPF na trajetoria direta
		float     DirectGain = 1.0f;        // occlusion: transmission loss
		float     ReverbSend = 0.0f;        // reflexao / late reverb
	};

	// ── AudioDevice ──────────────────────────────────────────────────────────
	//
	// O "RHI de audio": mesma posicao que RendererAPI ocupa no renderer.
	// Classe abstrata pura, com fabrica estatica e enum de API — trocar o
	// backend e adicionar um arquivo e mudar a fabrica, exatamente como
	// trocar GLFW por SDL em Window.
	//
	// REGRA DURA, irma da que ja vale pro renderer: nenhum simbolo `ma_*`
	// (ou de qualquer outra lib de audio) existe fora de audio/<backend>/.
	// Se a engine precisa de uma capacidade nova de audio, o caminho e:
	// metodo virtual puro aqui -> implementar no backend -> usar no frontend.
	// Nunca atalho.
	//
	// THREADING — parte do contrato, nao detalhe de implementacao:
	//
	//   1. Todo metodo desta interface e chamado da GAME THREAD. O backend
	//      roda sua propria audio thread internamente e e RESPONSAVEL PELA
	//      PROPRIA SINCRONIZACAO. Nenhuma linha de engine toca dado da audio
	//      thread — e por isso a regra "trate a AXE como single-threaded"
	//      (AI_CONTEXT §9) continua valendo com audio no ar.
	//
	//   2. Nenhuma implementacao pode alocar ou liberar memoria dentro do
	//      callback de audio. Destruicao de voice acontece so em Reap().
	class AXE_API AudioDevice
	{
	public:
		enum class API { None = 0, MiniAudio = 1 };

		virtual ~AudioDevice() = default;

		virtual bool Initialize() = 0;
		virtual void Shutdown() = 0;

		// Decodifica um arquivo (.wav/.mp3/.flac) para PCM float.
		//
		// Mora no device — e nao num loader do frontend — porque o decoder e
		// da lib de audio. Tirar isso daqui derramaria `ma_*` pra fora do
		// backend na primeira linha do sistema.
		virtual AudioPcmRef Decode(const std::filesystem::path& file) = 0;

		// Cria e inicia uma voice. O backend guarda uma copia do AudioPcmRef
		// enquanto a voice viver (ver comentario de AudioPcmData).
		virtual VoiceHandle Play(const AudioPcmRef& pcm, const VoiceParams& params, bool loop) = 0;

		virtual void SetParams(VoiceHandle voice, const VoiceParams& params) = 0;
		virtual void Stop(VoiceHandle voice) = 0;
		virtual bool IsPlaying(VoiceHandle voice) const = 0;
		virtual void StopAll() = 0;

		// Pausa preserva a POSICAO de leitura; Stop rebobina. A diferenca
		// importa: retomar do Pause do editor no meio de uma explosao tem
		// que continuar a explosao, nao recomeca-la.
		virtual void SetVoicePaused(VoiceHandle voice, bool paused) = 0;

		// Pausa/retoma tudo que estava tocando. So retoma o que ELA mesma
		// pausou — voice parada de proposito nao ressuscita no resume.
		virtual void SetAllPaused(bool paused) = 0;

		// Destroi as voices que ja terminaram. GAME THREAD, sempre.
		// Sem isso, one-shot vaza — e uma voice vazada segura o PCM inteiro.
		virtual void Reap() = 0;

		// Posicao de leitura da voice, em segundos. Negativo = voice morta.
		//
		// So existe pra desenhar o cursor no preview da forma de onda. E a
		// UNICA informacao que o editor precisa perguntar ao backend de volta
		// — todo o resto do fluxo e de mao unica, da engine pro device.
		virtual float GetVoiceCursorSeconds(VoiceHandle voice) const = 0;

		// ── Limite de vozes ──────────────────────────────────────────────
		//
		// Teto de vozes simultaneas. Trinta inimigos atirando ao mesmo tempo
		// nao produzem "mais som": produzem lama — as ondas somam, o mixer
		// satura, e o custo de CPU sobe linearmente por um resultado que soa
		// PIOR que oito tiros bem escolhidos.
		//
		// Quando o teto estoura, o device escolhe uma voz para ceder lugar.
		// O criterio esta em EvictionScore, no backend.
		virtual void SetMaxVoices(int maxVoices) = 0;
		virtual int  GetMaxVoices() const = 0;

		// Vozes vivas AGORA — o que o Mixer mostra e o que torna o limite
		// observavel em vez de mágico.
		virtual int  GetActiveVoiceCount() const = 0;

		// ── Buses ────────────────────────────────────────────────────────
		virtual void  SetBusVolume(AudioBus bus, float volume) = 0;
		virtual float GetBusVolume(AudioBus bus) const = 0;

		// ── Fade ─────────────────────────────────────────────────────────
		//
		// Rampa de volume da voice. Corte seco em musica e o tipo de coisa
		// que soa amador sem que ninguem saiba dizer por que.
		virtual void FadeVoice(VoiceHandle voice, float fromVolume,
			float toVolume, float seconds) = 0;

		// Fade ate zero e DEPOIS destroi. A voice tem que sobreviver a rampa
		// inteira — parar primeiro e desvanecer depois nao existe.
		virtual void StopWithFade(VoiceHandle voice, float seconds) = 0;

		virtual void SetMasterVolume(float volume) = 0;

		// Velocidade do listener. Separada do SetListener de proposito: a
		// pose vem de uma camera, que a engine tem; a velocidade e derivada,
		// e derivar dentro do device esconderia a heuristica num lugar onde
		// ninguem procuraria por ela.
		virtual void SetListenerVelocity(const glm::vec3& velocity) = 0;

		// Pose do listener. No A1 ninguem chama: o listener fica na origem e
		// as voices tocam em 2D. O AudioWorld do A2 passa a alimentar isso a
		// cada frame, a partir da camera ativa.
		virtual void SetListener(const glm::vec3& position,
			const glm::vec3& forward,
			const glm::vec3& up) = 0;

		static API GetAPI();
		static std::unique_ptr<AudioDevice> Create();

	private:
		static API s_API;
	};

} // namespace axe