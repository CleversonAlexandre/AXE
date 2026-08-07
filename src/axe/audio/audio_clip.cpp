#include "axe/audio/audio_clip.hpp"
#include "axe/audio/audio_engine.hpp"
#include "axe/log/log.hpp"

#include <algorithm>
#include <cmath>

namespace axe
{
	std::shared_ptr<AudioClip> AudioClip::LoadFromFile(const std::filesystem::path& path)
	{
		AudioDevice* device = AudioEngine::GetDevice();

		if (!device)
		{
			AXE_CORE_WARN("AudioClip::LoadFromFile('{}'): audio nao inicializado.",
				path.string());
			return nullptr;
		}

		AudioPcmRef pcm = device->Decode(path);

		if (!pcm || pcm->Frames.empty())
		{
			AXE_CORE_ERROR("AudioClip::LoadFromFile: falha ao decodificar '{}'.",
				path.string());
			return nullptr;
		}

		auto clip = std::make_shared<AudioClip>();
		clip->m_Name = path.stem().string();
		clip->m_Pcm = std::move(pcm);
		clip->BuildPeaks();

		AXE_CORE_INFO("AudioClip '{}': {:.2f}s, {} canais, {} Hz.",
			clip->m_Name, clip->GetDuration(), clip->GetChannels(), clip->GetSampleRate());

		return clip;
	}

	void AudioClip::BuildPeaks()
	{
		m_Peaks.clear();

		if (!m_Pcm || m_Pcm->Frames.empty() || m_Pcm->Channels == 0)
			return;

		// Resolucao fixa, independente da duracao do som.
		//
		// Proposital: a onda e desenhada num espaco de largura conhecida (um
		// no do grafo, um painel), entao mais baldes que pixels seria trabalho
		// jogado fora, e menos deixaria um som longo com degraus visiveis.
		constexpr std::size_t kBuckets = 256;

		const std::size_t frames = m_Pcm->Frames.size() / m_Pcm->Channels;

		if (frames == 0)
			return;

		m_Peaks.resize(kBuckets, 0.0f);

		const std::size_t per = std::max<std::size_t>(1, frames / kBuckets);

		for (std::size_t b = 0; b < kBuckets; ++b)
		{
			const std::size_t start = b * per;

			if (start >= frames)
				break;

			const std::size_t end = std::min(frames, start + per);
			float peak = 0.0f;

			for (std::size_t f = start; f < end; ++f)
			{
				// Pico entre os canais, nao media: um som so no canal
				// esquerdo tem que aparecer com a altura que tem, e nao pela
				// metade.
				for (std::uint32_t c = 0; c < m_Pcm->Channels; ++c)
					peak = std::max(peak, std::abs(m_Pcm->Frames[f * m_Pcm->Channels + c]));
			}

			m_Peaks[b] = std::min(1.0f, peak);
		}
	}

} // namespace axe