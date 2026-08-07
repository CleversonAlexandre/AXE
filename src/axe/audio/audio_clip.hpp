#pragma once
#include "axe/core/types.hpp"
#include "axe/audio/audio_device.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace axe
{
	// ── AudioClip ────────────────────────────────────────────────────────────
	//
	// O MOLDE: o conteudo decodificado de um .wav/.mp3/.flac, compartilhado
	// por todas as voices que o tocam. Segue o padrao obrigatorio do resto da
	// engine (asset e molde, instancia e copia) — aqui a "instancia viva" e a
	// voice, que nao mora neste objeto.
	//
	// IMUTAVEL depois de carregado. Nao ha setter, nao ha reload in-place, e
	// isso e proposital:
	//
	//   Reimportar um .wav (trocar o arquivo no disco e reimportar) cria uma
	//   INSTANCIA NOVA de AudioClip. O UUID no AssetDatabase NAO MUDA — ele e
	//   a identidade do asset, e trocar o UUID quebraria silenciosamente toda
	//   cena e todo AnimNotify que referenciam aquele som (AI_CONTEXT §7).
	//   O que muda e para onde o cache aponta: Play() novo pega o clip novo;
	//   voices que ja estavam tocando seguram o shared_ptr antigo e terminam
	//   normalmente.
	//
	//   E o que evita o bug classico de "troquei a musica e a que estava
	//   tocando cortou no meio" — e, pior, o de liberar PCM debaixo da audio
	//   thread.
	class AXE_API AudioClip
	{
	public:
		// Decodifica via AudioDevice ativo. Devolve nullptr se falhar.
		static std::shared_ptr<AudioClip> LoadFromFile(const std::filesystem::path& path);

		const std::string& GetName() const { return m_Name; }
		const AudioPcmRef& GetPcm()  const { return m_Pcm; }

		float    GetDuration()   const { return m_Pcm ? m_Pcm->DurationSeconds : 0.0f; }
		uint32_t GetChannels()   const { return m_Pcm ? m_Pcm->Channels : 0u; }
		uint32_t GetSampleRate() const { return m_Pcm ? m_Pcm->SampleRate : 0u; }

		bool IsValid() const { return m_Pcm && !m_Pcm->Frames.empty(); }

		// Envelope pra desenhar a forma de onda: pico absoluto por balde,
		// normalizado em 0..1.
		//
		// Calculado UMA vez, no load. A alternativa — varrer o PCM a cada
		// frame que a onda aparece na tela — significaria percorrer centenas
		// de milhares de floats por nó visível, todo frame, para produzir
		// sempre o mesmo resultado.
		const std::vector<float>& GetPeaks() const { return m_Peaks; }

	private:
		void BuildPeaks();

		std::string  m_Name;
		AudioPcmRef  m_Pcm;
		std::vector<float> m_Peaks;
	};

} // namespace axe