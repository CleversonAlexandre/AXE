#pragma once
#include "axe/audio/audio_device.hpp"

#include <memory>

namespace axe
{
	// ── MiniAudioDevice ──────────────────────────────────────────────────────
	//
	// Unica implementacao concreta de AudioDevice hoje. Mesma posicao que
	// OpenGLRendererAPI ocupa em graphics/opengl/.
	//
	// Note o que este header NAO tem: nenhum `#include "miniaudio.h"`, nenhum
	// tipo `ma_*`. Todo o estado do backend vive num Impl opaco declarado no
	// .cpp. E o mesmo motivo pelo qual a engine nao ve GLuint: o dia em que
	// este backend for substituido, nada acima dele precisa recompilar por
	// causa de um tipo de terceiro.
	//
	// Tambem nao leva AXE_API: a classe nunca cruza a fronteira da DLL. O
	// editor conhece AudioEngine e AudioDevice, jamais o backend.
	class MiniAudioDevice final : public AudioDevice
	{
	public:
		MiniAudioDevice();
		~MiniAudioDevice() override;

		bool Initialize() override;
		void Shutdown() override;

		AudioPcmRef Decode(const std::filesystem::path& file) override;

		VoiceHandle Play(const AudioPcmRef& pcm, const VoiceParams& params, bool loop) override;
		void        SetParams(VoiceHandle voice, const VoiceParams& params) override;
		void        Stop(VoiceHandle voice) override;
		bool        IsPlaying(VoiceHandle voice) const override;
		void        StopAll() override;
		void        SetVoicePaused(VoiceHandle voice, bool paused) override;
		void        SetAllPaused(bool paused) override;
		float       GetVoiceCursorSeconds(VoiceHandle voice) const override;

		void        SetMaxVoices(int maxVoices) override;
		int         GetMaxVoices() const override;
		int         GetActiveVoiceCount() const override;

		void        SetBusVolume(AudioBus bus, float volume) override;
		float       GetBusVolume(AudioBus bus) const override;
		void        FadeVoice(VoiceHandle voice, float from, float to, float seconds) override;
		void        StopWithFade(VoiceHandle voice, float seconds) override;
		void        Reap() override;

		void SetMasterVolume(float volume) override;
		void SetListenerVelocity(const glm::vec3& velocity) override;
		void SetListener(const glm::vec3& position,
			const glm::vec3& forward,
			const glm::vec3& up) override;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

} // namespace axe