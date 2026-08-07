#include "axe/audio/audio_engine.hpp"
#include "axe/audio/audio_clip.hpp"
#include "axe/audio/sound_cue.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/log/log.hpp"

#include <unordered_map>
#include <algorithm>

namespace axe
{
	namespace
	{
		// Estado no .cpp, e nao como membro estatico da classe, pelo mesmo
		// motivo documentado no ParticleWorld: exportar std::unordered_map
		// via AXE_API e fonte de dor no MSVC. A facade e estatica por fora e
		// nao tem estado exportado por dentro.
		std::unique_ptr<AudioDevice>                             s_Device;
		std::unordered_map<std::string, std::shared_ptr<AudioClip>> s_ClipCache;

		// nome -> UUID. Separado do cache de clipes de proposito: um nome que
		// nao existe tambem merece nao ser procurado de novo a cada tiro.
		std::unordered_map<std::string, std::string>                s_NameIndex;
		std::unordered_map<std::string, std::shared_ptr<SoundCueAsset>> s_CueCache;
		float                                                    s_MasterVolume = 1.0f;

		// Pulsos ativos. Vetor simples, e nao mapa: a lista tem dezenas de
		// entradas no pior caso, e o consumidor (o renderer) quer justamente
		// percorrer tudo — um mapa so tornaria isso mais lento e mais chato.
		std::vector<SoundEvent>                                  s_Sounds;

		// Teto duro. Um script em loop chamando PlaySound2D todo frame
		// encheria a lista sem limite; melhor perder pulso antigo do que
		// crescer sem fim.
		constexpr std::size_t kMaxSounds = 256;

		void PushSound(const glm::vec3& pos, float volume, float maxDistance,
			bool spatialized, SoundCategory category, float lifetime)
		{
			if (s_Sounds.size() >= kMaxSounds)
				s_Sounds.erase(s_Sounds.begin());

			SoundEvent e;
			e.Position = pos;
			e.Volume = volume;
			e.MaxDistance = maxDistance;
			e.Spatialized = spatialized;
			e.Category = category;
			e.Lifetime = lifetime;
			s_Sounds.push_back(e);
		}

		bool EnsureInit()
		{
			if (s_Device)
				return true;

			return AudioEngine::Init();
		}
	}

	bool AudioEngine::Init()
	{
		if (s_Device)
			return true;

		s_Device = AudioDevice::Create();

		if (!s_Device)
			return false;

		if (!s_Device->Initialize())
		{
			AXE_CORE_ERROR("AudioEngine: falha ao inicializar o device. Audio desligado.");
			s_Device.reset();
			return false;
		}

		s_Device->SetMasterVolume(s_MasterVolume);

		AXE_CORE_INFO("AudioEngine inicializado.");
		return true;
	}

	void AudioEngine::Shutdown()
	{
		if (!s_Device)
		{
			s_ClipCache.clear();
			return;
		}

		// Ordem obrigatoria: matar as voices ANTES de soltar os clips.
		// Enquanto uma voice existe ela segura o PCM; limpar o cache primeiro
		// so trocaria o dono, nunca liberaria cedo demais — mas parar antes
		// deixa o shutdown deterministico em vez de dependente de refcount.
		s_Device->StopAll();
		s_Device->Reap();

		s_ClipCache.clear();
		s_NameIndex.clear();
		s_CueCache.clear();
		s_Sounds.clear();

		s_Device->Shutdown();
		s_Device.reset();

		AXE_CORE_INFO("AudioEngine finalizado.");
	}

	bool AudioEngine::IsInitialized()
	{
		return s_Device != nullptr;
	}

	void AudioEngine::Update(float deltaTime)
	{
		if (s_Device)
			s_Device->Reap();

		// Envelhece e descarta. Pulso de voice persistente e refrescado pelo
		// AudioWorld ANTES desta chamada (a ordem no OnUpdate garante isso),
		// entao envelhecer aqui so mata o que realmente parou.
		for (auto& e : s_Sounds)
			e.Age += deltaTime;

		s_Sounds.erase(std::remove_if(s_Sounds.begin(), s_Sounds.end(),
			[](const SoundEvent& e) { return e.Age >= e.Lifetime; }), s_Sounds.end());
	}

	const std::vector<SoundEvent>& AudioEngine::GetActiveSounds()
	{
		return s_Sounds;
	}

	void AudioEngine::ReportVoice(VoiceHandle voice, const glm::vec3& position,
		float volume, float maxDistance, bool spatialized, SoundCategory category)
	{
		if (voice == InvalidVoice)
			return;

		for (auto& e : s_Sounds)
		{
			if (e.Voice != voice)
				continue;

			// Upsert: a fonte se move, o pulso vai junto, e a idade zera —
			// enquanto tocar, ele nao desbota.
			e.Position = position;
			e.Volume = volume;
			e.MaxDistance = maxDistance;
			e.Spatialized = spatialized;
			e.Category = category;
			e.Age = 0.0f;
			return;
		}

		if (s_Sounds.size() >= kMaxSounds)
			s_Sounds.erase(s_Sounds.begin());

		SoundEvent e;
		e.Position = position;
		e.Volume = volume;
		e.MaxDistance = maxDistance;
		e.Spatialized = spatialized;
		e.Category = category;

		// Lifetime curto para voice persistente: ele e renovado todo frame
		// enquanto o som existe, entao serve como PRAZO DE VALIDADE — a marca
		// some sozinha um instante depois de a fonte parar, sem ninguem
		// precisar avisar.
		e.Lifetime = 0.25f;
		e.Voice = voice;

		s_Sounds.push_back(e);
	}

	void AudioEngine::ClearVoiceReport(VoiceHandle voice)
	{
		if (voice == InvalidVoice)
			return;

		s_Sounds.erase(std::remove_if(s_Sounds.begin(), s_Sounds.end(),
			[voice](const SoundEvent& e) { return e.Voice == voice; }), s_Sounds.end());
	}

	AudioDevice* AudioEngine::GetDevice()
	{
		return s_Device.get();
	}

	std::shared_ptr<AudioClip> AudioEngine::GetClip(const std::string& uuid)
	{
		if (uuid.empty() || !EnsureInit())
			return nullptr;

		auto it = s_ClipCache.find(uuid);

		if (it != s_ClipCache.end())
			return it->second;

		const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);

		if (!rec)
		{
			AXE_CORE_WARN("AudioEngine: asset de audio '{}' nao encontrado no database.", uuid);
			return nullptr;
		}

		auto clip = AudioClip::LoadFromFile(rec->FilePath);

		if (!clip)
			return nullptr;

		s_ClipCache[uuid] = clip;
		return clip;
	}

	void AudioEngine::InvalidateClip(const std::string& uuid)
	{
		s_ClipCache.erase(uuid);
	}

	void AudioEngine::ClearClipCache()
	{
		s_ClipCache.clear();
		s_NameIndex.clear();
		s_CueCache.clear();
	}

	bool ResolvedSound::IsValid() const
	{
		return Clip && Clip->IsValid();
	}

	std::shared_ptr<SoundCueAsset> AudioEngine::GetCue(const std::string& uuid)
	{
		if (uuid.empty())
			return nullptr;

		auto it = s_CueCache.find(uuid);

		if (it != s_CueCache.end())
			return it->second;

		const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);

		if (!rec)
			return nullptr;

		auto cue = SoundCueAsset::LoadFromFile(rec->FilePath);

		if (!cue)
			return nullptr;

		s_CueCache[uuid] = cue;
		return cue;
	}

	ResolvedSound AudioEngine::ResolveSound(const std::string& nameOrUuid)
	{
		ResolvedSound out;

		const std::string uuid = ResolveAudioAsset(nameOrUuid);

		if (uuid.empty())
			return out;

		const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);

		if (!rec)
			return out;

		// Chaveado pela EXTENSAO, e nao por rec->Type, pela mesma razao que o
		// Asset Browser ja documenta: Type e estado derivado, persistido em
		// disco, e pode estar velho num database salvo por build anterior.
		// A extensao do arquivo e a verdade.
		if (rec->FilePath.extension() == ".axecue")
		{
			auto cue = GetCue(uuid);

			if (!cue)
				return out;

			SoundCueResult r;

			if (!cue->Evaluate(r))
				return out;

			out.Clip = GetClip(r.WaveUUID);
			out.VolumeMultiplier = r.Volume;
			out.PitchMultiplier = r.Pitch;
			out.OverrideAttenuation = r.OverrideAttenuation;
			out.MinDistance = r.MinDistance;
			out.MaxDistance = r.MaxDistance;
			out.Is3D = r.Is3D;
			out.Category = cue->GetCategory();
			out.Bus = cue->GetBus();
			out.Priority = cue->GetPriority();
			return out;
		}

		out.Clip = GetClip(uuid);
		return out;
	}

	std::string AudioEngine::ResolveAudioAsset(const std::string& nameOrUuid)
	{
		if (nameOrUuid.empty())
			return {};

		auto& db = AssetDatabase::Get();

		// Ja e um UUID valido? devolve direto — caminho barato e comum.
		if (db.GetByUUID(nameOrUuid) != nullptr)
			return nameOrUuid;

		auto it = s_NameIndex.find(nameOrUuid);

		if (it != s_NameIndex.end())
			return it->second;

		std::string found;
		int matches = 0;

		std::vector<const AssetRecord*> candidates = db.GetAllOfType(AssetType::Audio);

		// Cues entram na mesma busca por nome: pra quem chama PlaySound2D
		// nao existe diferenca entre um .wav e um .axecue, e nao deveria
		// existir na hora de achar o asset tambem.
		for (const AssetRecord* rec : db.GetAllOfType(AssetType::SoundCue))
			candidates.push_back(rec);

		for (const AssetRecord* rec : candidates)
		{
			if (!rec)
				continue;

			// Casa pelo Name do record OU pelo stem do arquivo: "Explosion"
			// acha tanto o asset chamado Explosion quanto Explosion.wav.
			const bool hit = (rec->Name == nameOrUuid)
				|| (rec->FilePath.stem().string() == nameOrUuid);

			if (!hit)
				continue;

			++matches;

			if (found.empty())
				found = rec->UUID;
		}

		if (found.empty())
		{
			AXE_CORE_WARN("AudioEngine: nenhum audio chamado '{}'.", nameOrUuid);
			return {};
		}

		if (matches > 1)
			AXE_CORE_WARN("AudioEngine: '{}' casa com {} audios. Usando o primeiro — "
				"renomeie para desambiguar.", nameOrUuid, matches);

		s_NameIndex[nameOrUuid] = found;
		return found;
	}

	VoiceHandle AudioEngine::PlayOneShot(const std::string& uuid, float volume, float pitch)
	{
		const ResolvedSound rs = ResolveSound(uuid);

		if (!rs.IsValid())
			return InvalidVoice;

		VoiceParams params;
		params.Volume = volume * rs.VolumeMultiplier;
		params.Pitch = pitch * rs.PitchMultiplier;
		params.Spatialized = false;
		params.Bus = rs.Bus;
		params.Priority = rs.Priority;

		// Som 2D nao tem lugar no mundo, entao nao gera pulso: desenhar um
		// circulo na origem para a musica de fundo seria ruido visual puro.
		return s_Device->Play(rs.Clip->GetPcm(), params, false);
	}

	VoiceHandle AudioEngine::PlayOneShotAt(const std::string& uuid,
		const glm::vec3& position, float volume, float pitch)
	{
		const ResolvedSound rs = ResolveSound(uuid);

		if (!rs.IsValid())
			return InvalidVoice;

		VoiceParams params;
		params.Position = position;
		params.Volume = volume * rs.VolumeMultiplier;
		params.Pitch = pitch * rs.PitchMultiplier;
		params.Spatialized = rs.OverrideAttenuation ? rs.Is3D : true;
		params.Bus = rs.Bus;
		params.Priority = rs.Priority;

		if (rs.OverrideAttenuation)
		{
			params.MinDistance = rs.MinDistance;
			params.MaxDistance = rs.MaxDistance;
		}

		// Pulso do one-shot: vive por um tempo fixo curto e some.
		//
		// Nao usamos a duracao do clipe. Um tiro de 2s deixaria a marca 2s na
		// tela e voce leria "o inimigo ainda esta ali" muito depois de ele ter
		// saido. A marca informa QUANDO o som aconteceu, nao quanto ele dura.
		if (params.Spatialized)
			PushSound(position, params.Volume, params.MaxDistance, true,
				rs.Category, 0.7f);

		return s_Device->Play(rs.Clip->GetPcm(), params, false);
	}

	VoiceHandle AudioEngine::Play(const std::shared_ptr<AudioClip>& clip,
		const VoiceParams& params, bool loop)
	{
		if (!clip || !clip->IsValid() || !EnsureInit())
			return InvalidVoice;

		return s_Device->Play(clip->GetPcm(), params, loop);
	}

	void AudioEngine::SetVoiceParams(VoiceHandle voice, const VoiceParams& params)
	{
		if (s_Device)
			s_Device->SetParams(voice, params);
	}

	void AudioEngine::Stop(VoiceHandle voice)
	{
		if (s_Device)
			s_Device->Stop(voice);
	}

	bool AudioEngine::IsPlaying(VoiceHandle voice)
	{
		return s_Device ? s_Device->IsPlaying(voice) : false;
	}

	void AudioEngine::StopAll()
	{
		if (s_Device)
			s_Device->StopAll();

		s_Sounds.clear();
	}

	void AudioEngine::SetVoicePaused(VoiceHandle voice, bool paused)
	{
		if (s_Device)
			s_Device->SetVoicePaused(voice, paused);
	}

	void AudioEngine::SetAllPaused(bool paused)
	{
		if (s_Device)
			s_Device->SetAllPaused(paused);
	}

	void AudioEngine::SetMaxVoices(int maxVoices)
	{
		if (s_Device)
			s_Device->SetMaxVoices(maxVoices);
	}

	int AudioEngine::GetMaxVoices()
	{
		return s_Device ? s_Device->GetMaxVoices() : 0;
	}

	int AudioEngine::GetActiveVoiceCount()
	{
		return s_Device ? s_Device->GetActiveVoiceCount() : 0;
	}

	void AudioEngine::SetBusVolume(AudioBus bus, float volume)
	{
		if (s_Device)
			s_Device->SetBusVolume(bus, volume);
	}

	float AudioEngine::GetBusVolume(AudioBus bus)
	{
		return s_Device ? s_Device->GetBusVolume(bus) : 1.0f;
	}

	void AudioEngine::FadeVoice(VoiceHandle voice, float from, float to, float seconds)
	{
		if (s_Device)
			s_Device->FadeVoice(voice, from, to, seconds);
	}

	void AudioEngine::StopWithFade(VoiceHandle voice, float seconds)
	{
		if (s_Device)
			s_Device->StopWithFade(voice, seconds);
	}

	float AudioEngine::GetVoiceCursorSeconds(VoiceHandle voice)
	{
		return s_Device ? s_Device->GetVoiceCursorSeconds(voice) : -1.0f;
	}

	void AudioEngine::SetMasterVolume(float volume)
	{
		s_MasterVolume = volume;

		if (s_Device)
			s_Device->SetMasterVolume(volume);
	}

	float AudioEngine::GetMasterVolume()
	{
		return s_MasterVolume;
	}

} // namespace axe