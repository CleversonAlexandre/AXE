#include "axe/audio/audio_device.hpp"
#include "axe/audio/miniaudio/miniaudio_device.hpp"
#include "axe/log/log.hpp"

namespace axe
{
	AudioDevice::API AudioDevice::s_API = AudioDevice::API::MiniAudio;

	AudioDevice::API AudioDevice::GetAPI()
	{
		return s_API;
	}

	std::unique_ptr<AudioDevice> AudioDevice::Create()
	{
		switch (s_API)
		{
		case API::MiniAudio:
			return std::make_unique<MiniAudioDevice>();

		case API::None:
			AXE_CORE_WARN("AudioDevice::Create: API::None — audio desabilitado.");
			return nullptr;
		}

		AXE_CORE_ERROR("AudioDevice::Create: API desconhecida.");
		return nullptr;
	}

} // namespace axe