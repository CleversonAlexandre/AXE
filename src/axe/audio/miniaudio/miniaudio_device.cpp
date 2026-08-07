#include "axe/audio/miniaudio/miniaudio_device.hpp"
#include "axe/log/log.hpp"

// O unico .cpp da engine (junto com miniaudio_impl.cpp) que enxerga a lib.
// A implementacao propriamente dita mora em miniaudio_impl.cpp, que define
// MINIAUDIO_IMPLEMENTATION — mesma divisao que stb_image_impl.cpp ja usa.
#include "miniaudio.h"

#include <unordered_map>
#include <vector>

namespace axe
{
	// ── Estado interno do backend ────────────────────────────────────────────
	struct MiniAudioDevice::Impl
	{
		// Uma voice viva. A ORDEM DOS CAMPOS IMPORTA na destruicao:
		// ma_sound aponta pro ma_audio_buffer_ref, que aponta pro PCM do
		// shared_ptr. Destruir e o caminho inverso — por isso Reap() faz
		// ma_sound_uninit ANTES de deixar a Voice sair do vetor.
		struct Voice
		{
			VoiceHandle         Id = InvalidVoice;
			ma_sound            Sound{};
			ma_audio_buffer_ref BufferRef{};

			// A copia do shared_ptr que garante o PCM vivo enquanto a audio
			// thread le dele. Nao e redundancia com o cache do AudioEngine:
			// o cache pode ser invalidado a qualquer momento (hot reload) e
			// esta copia e a unica coisa que segura o dado antigo de pe.
			AudioPcmRef         Pcm;

			bool                Initialized = false;

			// Marcada por Stop(). Reap() destroi; a audio thread nunca.
			bool                PendingDestroy = false;

			// Pausada por SetAllPaused — e nao pelo usuario. So estas
			// voltam a tocar no resume.
			bool                PausedByWorld = false;

			// Instante (relogio do engine, em frames) em que um fade-out
			// termina. Zero = sem fade agendado.
			//
			// Precisa existir porque ma_sound_stop_with_fade agenda a parada
			// no futuro: a voice continua tocando durante a rampa, mas
			// is_playing ja responde false e at_end nunca vira true. Sem esta
			// data, o Reap ou mataria a voice antes da rampa acabar (corte
			// seco, justamente o que o fade evita) ou nunca a mataria.
			ma_uint64           FadeOutEndFrame = 0;

			// Ultimos parametros aplicados. Guardados porque o despejo
			// precisa comparar voz com voz — e a comparacao usa posicao,
			// volume, alcance e prioridade, que so existiam de passagem.
			VoiceParams         Params{};
		};

		ma_engine Engine{};
		bool      EngineReady = false;

		// Um grupo por bus, exceto Master — que E o endpoint, e por isso nao
		// tem grupo: voice em Master toca direto.
		ma_sound_group Groups[(int)AudioBus::Count]{};
		bool           GroupsReady = false;
		float          BusVolume[(int)AudioBus::Count] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

		ma_sound_group* GroupFor(AudioBus bus)
		{
			if (!GroupsReady || bus == AudioBus::Master)
				return nullptr;

			return &Groups[(int)bus];
		}

		// unique_ptr por voice, e nao Voice por valor: ma_sound guarda
		// ponteiros pra si mesmo dentro do node graph do miniaudio. Se o
		// vector realocasse, os ma_sound MUDARIAM DE ENDERECO enquanto a
		// audio thread os percorre — corrupcao imediata e nao determinista.
		// Este e o tipo de bug que so aparece quando o jogo tem muitas
		// vozes simultaneas, ou seja, na demo.
		std::vector<std::unique_ptr<Voice>> Voices;

		VoiceHandle NextId = 1;

		int MaxVoices = 32;

		// Pose do listener, guardada para o calculo de audibilidade. O device
		// ja a recebe todo frame; nao guarda-la obrigaria o despejo a decidir
		// no escuro qual voz esta longe demais para importar.
		glm::vec3 ListenerPos{ 0.0f };

		int ActiveCount() const
		{
			int n = 0;

			for (const auto& v : Voices)
				if (v->Initialized && !v->PendingDestroy)
					++n;

			return n;
		}

		// ── Criterio de despejo ──────────────────────────────────────────
		//
		// Menor pontuacao = primeira a ceder lugar.
		//
		// Prioridade DOMINA (peso 1000): ela e uma decisao de autoria, e uma
		// musica de fundo baixa nao pode ser derrubada por um passo perto so
		// porque o passo esta mais alto neste instante.
		//
		// Dentro da mesma prioridade, decide a AUDIBILIDADE — volume ja
		// atenuado pela distancia ate o listener. E a aproximacao certa de
		// "quem o jogador sente falta se sumir": o tiro do outro lado do mapa
		// custa uma voz e entrega quase nada.
		float EvictionScore(const Voice& v) const
		{
			const VoiceParams& p = v.Params;

			float audible = p.Volume;

			if (p.Spatialized)
			{
				const float d = glm::length(p.Position - ListenerPos);

				if (d > p.MaxDistance)
					audible = 0.0f;
				else if (d > p.MinDistance && d > 1e-4f)
					audible = p.Volume * (p.MinDistance / d);
			}

			return p.Priority * 1000.0f + audible;
		}

		Voice* Find(VoiceHandle id)
		{
			if (id == InvalidVoice)
				return nullptr;

			for (auto& v : Voices)
				if (v->Id == id)
					return v.get();

			return nullptr;
		}

		void Destroy(Voice& v)
		{
			if (v.Initialized)
			{
				ma_sound_uninit(&v.Sound);
				v.Initialized = false;
			}

			// ma_audio_buffer_ref nao aloca; nada a liberar alem do proprio
			// refcount do PCM, que cai junto com a Voice — SEMPRE na game
			// thread, nunca dentro do callback.
			v.Pcm.reset();
		}

		void Apply(Voice& v, const VoiceParams& p)
		{
			if (!v.Initialized)
				return;

			v.Params = p;

			// DirectGain e a transmission loss que a camada acustica (A5)
			// vai produzir. Ele multiplica o volume em vez de virar um node
			// separado porque, semanticamente, e exatamente isso: perda na
			// trajetoria direta. Hoje ninguem preenche — fica em 1.0.
			ma_sound_set_volume(&v.Sound, p.Volume * p.DirectGain);
			ma_sound_set_pitch(&v.Sound, p.Pitch);

			ma_sound_set_spatialization_enabled(&v.Sound, p.Spatialized ? MA_TRUE : MA_FALSE);

			if (p.Spatialized)
			{
				ma_sound_set_position(&v.Sound, p.Position.x, p.Position.y, p.Position.z);
				ma_sound_set_min_distance(&v.Sound, p.MinDistance);
				ma_sound_set_max_distance(&v.Sound, p.MaxDistance);

				ma_sound_set_velocity(&v.Sound, p.Velocity.x, p.Velocity.y, p.Velocity.z);
				ma_sound_set_doppler_factor(&v.Sound, p.DopplerFactor);
			}

			// LowPassCutoff e ReverbSend ficam sem efeito ate o A5.
			//
			// Nao e esquecimento: aplica-los exige inserir um ma_lpf_node e
			// um send por voice no node graph do miniaudio, o que muda o
			// custo de cada voice e so faz sentido quando existir quem
			// calcule os valores. O campo existe na interface pra que essa
			// mudanca seja ADITIVA — nao pra que ela ja tenha acontecido.
		}
	};

	// ─────────────────────────────────────────────────────────────────────────
	MiniAudioDevice::MiniAudioDevice()
		: m_Impl(std::make_unique<Impl>())
	{}

	MiniAudioDevice::~MiniAudioDevice()
	{
		Shutdown();
	}

	bool MiniAudioDevice::Initialize()
	{
		if (m_Impl->EngineReady)
			return true;

		ma_engine_config cfg = ma_engine_config_init();

		if (ma_engine_init(&cfg, &m_Impl->Engine) != MA_SUCCESS)
		{
			AXE_CORE_ERROR("MiniAudioDevice: ma_engine_init falhou.");
			return false;
		}

		m_Impl->EngineReady = true;

		// Um ma_sound_group por bus, todos filhos do endpoint. Master fica
		// de fora: ele e o proprio endpoint.
		bool groupsOk = true;

		for (int i = 1; i < (int)AudioBus::Count; ++i)
		{
			if (ma_sound_group_init(&m_Impl->Engine, 0, nullptr,
				&m_Impl->Groups[i]) != MA_SUCCESS)
			{
				AXE_CORE_ERROR("MiniAudioDevice: falha ao criar o bus '{}'.",
					AudioBusToString((AudioBus)i));
				groupsOk = false;
				break;
			}
		}

		m_Impl->GroupsReady = groupsOk;

		AXE_CORE_INFO("MiniAudioDevice: {} Hz, {} canais.",
			ma_engine_get_sample_rate(&m_Impl->Engine),
			ma_engine_get_channels(&m_Impl->Engine));

		return true;
	}

	void MiniAudioDevice::Shutdown()
	{
		if (!m_Impl || !m_Impl->EngineReady)
			return;

		for (auto& v : m_Impl->Voices)
			m_Impl->Destroy(*v);

		m_Impl->Voices.clear();

		// Grupos morrem DEPOIS das voices: um grupo com voice viva dentro
		// seria destruido debaixo de quem o usa.
		if (m_Impl->GroupsReady)
		{
			for (int i = 1; i < (int)AudioBus::Count; ++i)
				ma_sound_group_uninit(&m_Impl->Groups[i]);

			m_Impl->GroupsReady = false;
		}

		ma_engine_uninit(&m_Impl->Engine);
		m_Impl->EngineReady = false;
	}

	AudioPcmRef MiniAudioDevice::Decode(const std::filesystem::path& file)
	{
		if (!m_Impl->EngineReady)
			return nullptr;

		// Decodifica para o formato do engine: converter agora, uma vez, em
		// vez de a cada frame no callback. Custa memoria e paga em latencia.
		ma_decoder_config cfg = ma_decoder_config_init(
			ma_format_f32,
			ma_engine_get_channels(&m_Impl->Engine),
			ma_engine_get_sample_rate(&m_Impl->Engine));

		ma_uint64 frameCount = 0;
		void* raw = nullptr;

		const std::string path = file.string();

		if (ma_decode_file(path.c_str(), &cfg, &frameCount, &raw) != MA_SUCCESS || !raw)
		{
			AXE_CORE_ERROR("MiniAudioDevice: falha ao decodificar '{}'.", path);
			return nullptr;
		}

		auto pcm = std::make_shared<AudioPcmData>();
		pcm->Channels = cfg.channels;
		pcm->SampleRate = cfg.sampleRate;
		pcm->DurationSeconds = cfg.sampleRate > 0
			? static_cast<float>(frameCount) / static_cast<float>(cfg.sampleRate)
			: 0.0f;

		const std::size_t sampleCount =
			static_cast<std::size_t>(frameCount) * static_cast<std::size_t>(cfg.channels);

		pcm->Frames.assign(static_cast<const float*>(raw),
			static_cast<const float*>(raw) + sampleCount);

		ma_free(raw, nullptr);

		return pcm;
	}

	VoiceHandle MiniAudioDevice::Play(const AudioPcmRef& pcm, const VoiceParams& params, bool loop)
	{
		if (!m_Impl->EngineReady || !pcm || pcm->Frames.empty() || pcm->Channels == 0)
			return InvalidVoice;

		// ── Teto de vozes ────────────────────────────────────────────────
		if (m_Impl->MaxVoices > 0 && m_Impl->ActiveCount() >= m_Impl->MaxVoices)
		{
			// Pontuacao que a voz NOVA teria, para comparar com a pior viva.
			VoiceParams probe = params;

			Impl::Voice candidate;
			candidate.Params = probe;

			const float incoming = m_Impl->EvictionScore(candidate);

			Impl::Voice* worst = nullptr;
			float worstScore = 0.0f;

			for (auto& v : m_Impl->Voices)
			{
				if (!v->Initialized || v->PendingDestroy)
					continue;

				const float sc = m_Impl->EvictionScore(*v);

				if (!worst || sc < worstScore)
				{
					worst = v.get();
					worstScore = sc;
				}
			}

			// Som que chega valendo MENOS que tudo que ja toca simplesmente
			// nao toca. Roubar lugar de algo mais importante para depois ser
			// roubado seria trocar seis por meia duzia, com um corte audivel
			// no meio.
			if (!worst || incoming <= worstScore)
				return InvalidVoice;

			ma_sound_stop(&worst->Sound);
			worst->PendingDestroy = true;
			worst->PausedByWorld = false;
		}

		auto voice = std::make_unique<Impl::Voice>();
		voice->Id = m_Impl->NextId++;
		voice->Pcm = pcm;   // <- a copia que segura o PCM vivo

		const ma_uint64 frameCount =
			static_cast<ma_uint64>(pcm->Frames.size() / pcm->Channels);

		if (ma_audio_buffer_ref_init(ma_format_f32, pcm->Channels,
			pcm->Frames.data(), frameCount, &voice->BufferRef) != MA_SUCCESS)
		{
			AXE_CORE_ERROR("MiniAudioDevice: ma_audio_buffer_ref_init falhou.");
			return InvalidVoice;
		}

		// Sem MA_SOUND_FLAG_ASYNC: o PCM ja esta em memoria, nao ha I/O a
		// esperar, e async so adicionaria um estado "carregando" que este
		// caminho nao tem como observar.
		if (ma_sound_init_from_data_source(&m_Impl->Engine, &voice->BufferRef,
			0, m_Impl->GroupFor(params.Bus), &voice->Sound) != MA_SUCCESS)
		{
			AXE_CORE_ERROR("MiniAudioDevice: ma_sound_init_from_data_source falhou.");
			return InvalidVoice;
		}

		voice->Initialized = true;

		ma_sound_set_looping(&voice->Sound, loop ? MA_TRUE : MA_FALSE);
		m_Impl->Apply(*voice, params);

		if (ma_sound_start(&voice->Sound) != MA_SUCCESS)
		{
			m_Impl->Destroy(*voice);
			AXE_CORE_ERROR("MiniAudioDevice: ma_sound_start falhou.");
			return InvalidVoice;
		}

		const VoiceHandle id = voice->Id;
		m_Impl->Voices.push_back(std::move(voice));

		return id;
	}

	void MiniAudioDevice::SetParams(VoiceHandle voice, const VoiceParams& params)
	{
		if (auto* v = m_Impl->Find(voice))
			m_Impl->Apply(*v, params);
	}

	void MiniAudioDevice::Stop(VoiceHandle voice)
	{
		auto* v = m_Impl->Find(voice);

		if (!v || !v->Initialized)
			return;

		// Só para e MARCA. A destruicao acontece no Reap(), da game thread —
		// nunca aqui, que pode ser chamado de qualquer ponto do frame.
		ma_sound_stop(&v->Sound);
		ma_sound_seek_to_pcm_frame(&v->Sound, 0);
		v->PendingDestroy = true;
		v->PausedByWorld = false;
	}

	bool MiniAudioDevice::IsPlaying(VoiceHandle voice) const
	{
		auto* v = m_Impl->Find(voice);

		if (!v || !v->Initialized)
			return false;

		// Pausada conta como "tocando": a voice existe, tem posicao de
		// leitura e vai voltar. Reportar false faria o AudioWorld zerar o
		// handle no Pause e a fonte nunca mais voltar a tocar.
		if (v->PausedByWorld)
			return true;

		return ma_sound_is_playing(&v->Sound) == MA_TRUE;
	}

	void MiniAudioDevice::StopAll()
	{
		for (auto& v : m_Impl->Voices)
		{
			if (!v->Initialized)
				continue;

			ma_sound_stop(&v->Sound);
			v->PendingDestroy = true;
			v->PausedByWorld = false;
		}
	}

	void MiniAudioDevice::SetVoicePaused(VoiceHandle voice, bool paused)
	{
		auto* v = m_Impl->Find(voice);

		if (!v || !v->Initialized || v->PendingDestroy)
			return;

		if (paused)
		{
			// Sem seek: ma_sound_stop preserva o cursor de leitura.
			ma_sound_stop(&v->Sound);
			v->PausedByWorld = true;
		}
		else if (v->PausedByWorld)
		{
			ma_sound_start(&v->Sound);
			v->PausedByWorld = false;
		}
	}

	void MiniAudioDevice::SetAllPaused(bool paused)
	{
		for (auto& v : m_Impl->Voices)
		{
			if (!v->Initialized || v->PendingDestroy)
				continue;

			if (paused)
			{
				if (ma_sound_is_playing(&v->Sound) == MA_TRUE)
				{
					ma_sound_stop(&v->Sound);
					v->PausedByWorld = true;
				}
			}
			else if (v->PausedByWorld)
			{
				ma_sound_start(&v->Sound);
				v->PausedByWorld = false;
			}
		}
	}

	void MiniAudioDevice::Reap()
	{
		if (!m_Impl->EngineReady)
			return;

		for (auto it = m_Impl->Voices.begin(); it != m_Impl->Voices.end(); )
		{
			Impl::Voice& v = **it;

			// Voice em loop nunca "acaba": so sai daqui via Stop explicito.
			const bool looping = v.Initialized && ma_sound_is_looping(&v.Sound) == MA_TRUE;
			const bool ended = v.Initialized && !looping
				&& ma_sound_at_end(&v.Sound) == MA_TRUE;

			// NAO ha mais criterio "nao esta tocando -> morreu".
			//
			// Ele existia no A1 e teria virado bug assim que o Pause
			// chegasse: voice pausada tem is_playing == false, e seria
			// destruida aqui — o som nunca voltaria no resume, sem nenhum
			// erro em lugar nenhum. Morte de voice agora e SEMPRE explicita
			// (PendingDestroy) ou por fim natural (at_end).
			// Fade-out agendado que ja venceu: a rampa acabou e a voice pode
			// morrer.
			if (v.FadeOutEndFrame != 0
				&& ma_engine_get_time_in_pcm_frames(&m_Impl->Engine) >= v.FadeOutEndFrame)
				v.PendingDestroy = true;

			if (ended || v.PendingDestroy)
			{
				m_Impl->Destroy(v);
				it = m_Impl->Voices.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void MiniAudioDevice::SetMaxVoices(int maxVoices)
	{
		// Zero ou negativo desliga o teto. Util para depurar "sera que o
		// limite esta comendo meu som?" sem recompilar.
		m_Impl->MaxVoices = maxVoices;
	}

	int MiniAudioDevice::GetMaxVoices() const
	{
		return m_Impl->MaxVoices;
	}

	int MiniAudioDevice::GetActiveVoiceCount() const
	{
		return m_Impl->ActiveCount();
	}

	void MiniAudioDevice::SetBusVolume(AudioBus bus, float volume)
	{
		m_Impl->BusVolume[(int)bus] = volume;

		if (bus == AudioBus::Master)
		{
			if (m_Impl->EngineReady)
				ma_engine_set_volume(&m_Impl->Engine, volume);

			return;
		}

		if (m_Impl->GroupsReady)
			ma_sound_group_set_volume(&m_Impl->Groups[(int)bus], volume);
	}

	float MiniAudioDevice::GetBusVolume(AudioBus bus) const
	{
		return m_Impl->BusVolume[(int)bus];
	}

	void MiniAudioDevice::FadeVoice(VoiceHandle voice, float from, float to, float seconds)
	{
		auto* v = m_Impl->Find(voice);

		if (!v || !v->Initialized)
			return;

		ma_sound_set_fade_in_milliseconds(&v->Sound, from, to,
			(ma_uint64)(seconds * 1000.0f));
	}

	void MiniAudioDevice::StopWithFade(VoiceHandle voice, float seconds)
	{
		auto* v = m_Impl->Find(voice);

		if (!v || !v->Initialized)
			return;

		// Fade nao positivo nao e fade: e um Stop, e tratar como tal evita
		// uma voice presa esperando uma rampa de duracao zero.
		if (seconds <= 0.0f)
		{
			ma_sound_stop(&v->Sound);
			v->PendingDestroy = true;
			return;
		}

		ma_sound_stop_with_fade_in_milliseconds(&v->Sound, (ma_uint64)(seconds * 1000.0f));

		v->PausedByWorld = false;
		v->FadeOutEndFrame = ma_engine_get_time_in_pcm_frames(&m_Impl->Engine)
			+ (ma_uint64)(seconds * (float)ma_engine_get_sample_rate(&m_Impl->Engine));
	}

	float MiniAudioDevice::GetVoiceCursorSeconds(VoiceHandle voice) const
	{
		auto* v = m_Impl->Find(voice);

		if (!v || !v->Initialized)
			return -1.0f;

		float seconds = 0.0f;

		if (ma_sound_get_cursor_in_seconds(&v->Sound, &seconds) != MA_SUCCESS)
			return -1.0f;

		return seconds;
	}

	void MiniAudioDevice::SetMasterVolume(float volume)
	{
		if (m_Impl->EngineReady)
			ma_engine_set_volume(&m_Impl->Engine, volume);
	}

	void MiniAudioDevice::SetListenerVelocity(const glm::vec3& velocity)
	{
		if (!m_Impl->EngineReady)
			return;

		ma_engine_listener_set_velocity(&m_Impl->Engine, 0,
			velocity.x, velocity.y, velocity.z);
	}

	void MiniAudioDevice::SetListener(const glm::vec3& position,
		const glm::vec3& forward,
		const glm::vec3& up)
	{
		if (!m_Impl->EngineReady)
			return;

		m_Impl->ListenerPos = position;

		ma_engine_listener_set_position(&m_Impl->Engine, 0, position.x, position.y, position.z);
		ma_engine_listener_set_direction(&m_Impl->Engine, 0, forward.x, forward.y, forward.z);
		ma_engine_listener_set_world_up(&m_Impl->Engine, 0, up.x, up.y, up.z);
	}

} // namespace axe