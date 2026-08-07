#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/audio/sound_event.hpp"
#include "axe/audio/audio_device.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════════
	//  SOUND CUE (.axecue)
	//
	//  Um grafo de SELECAO E PARAMETROS — nao de DSP.
	//
	//  A distincao e a decisao central deste sistema, entao fica escrita aqui:
	//
	//    Sound Cue (isto)     avaliado UMA VEZ, na game thread, no instante do
	//                         Play. Responde "qual wave, com que volume, com
	//                         que pitch". Custo: alguns ifs.
	//
	//    MetaSounds           grafo de DSP, roda POR SAMPLE dentro do callback
	//                         de audio. E um sintetizador. Exige node graph
	//                         sem alocacao na audio thread e um modelo de
	//                         concorrencia que a AXE nao tem.
	//
	//  A AXE implementa o primeiro. Ele entrega quase toda a percepcao de
	//  riqueza sonora (variacao, aleatoriedade, camadas) sem tocar no backend:
	//  volume, pitch e escolha de clipe ja sao coisas que o AudioDevice faz.
	//  Nenhuma linha de miniaudio muda por causa deste arquivo.
	//
	//  O PROBLEMA QUE ISSO RESOLVE nao e volume — e REPETICAO. Cinco passos
	//  com o mesmo .wav soam roboticos por mais que o volume esteja certo.
	// ═════════════════════════════════════════════════════════════════════════

	enum class SoundCueNodeType : int
	{
		Output = 0,   // raiz; exatamente uma por cue
		WavePlayer = 1,   // uma folha: um .wav
		Random = 2,   // escolhe UM dos filhos, com peso
		Modulator = 3,   // multiplica volume/pitch por um valor sorteado na faixa
		Attenuation = 4    // sobrescreve a atenuacao 3D de quem tocar este cue
	};

	struct AXE_API SoundCueNode
	{
		int              Id = 0;
		SoundCueNodeType Type = SoundCueNodeType::WavePlayer;

		// Posicao no canvas. Dado de autoria, nao de runtime — mas mora no
		// asset porque o layout do grafo E parte do que o usuario criou.
		glm::vec2 EditorPos{ 0.0f, 0.0f };

		// ── WavePlayer ───────────────────────────────────────────────────
		std::string WaveUUID;

		// ── Random ───────────────────────────────────────────────────────
		// Paralelo a lista de entradas. Entrada sem peso correspondente vale
		// 1.0 — assim acrescentar um filho no editor nao exige lembrar de
		// mexer aqui.
		std::vector<float> Weights;

		// Evita tocar o mesmo filho duas vezes seguidas. E o que separa
		// "aleatorio" de "soa aleatorio": sorteio puro repete com frequencia
		// suficiente pra o ouvido notar.
		bool NoRepeat = true;

		// ── Modulator ────────────────────────────────────────────────────
		float VolumeMin = 1.0f;
		float VolumeMax = 1.0f;
		float PitchMin = 1.0f;
		float PitchMax = 1.0f;

		// ── Attenuation ──────────────────────────────────────────────────
		float MinDistance = 1.0f;
		float MaxDistance = 100.0f;
		bool  Is3D = true;

		// ── Runtime (nao serializado) ────────────────────────────────────
		// Ultimo filho escolhido por este Random. Mutavel porque Evaluate e
		// logicamente const — avaliar nao MODIFICA o cue, so lembra o que
		// saiu da ultima vez.
		mutable int _LastPick = -1;
	};

	struct AXE_API SoundCueLink
	{
		int Id = 0;
		int FromNodeId = 0;   // saida do filho
		int ToNodeId = 0;   // entrada do pai
		int ToInputIndex = 0;   // qual entrada do pai (so Random usa > 0)
	};

	// Resultado de UMA avaliacao. Um cue produz exatamente uma voice na v1.
	//
	// Mixer (tocar varias waves de uma vez) ficou DE FORA de proposito: ele
	// obrigaria VoiceHandle a virar grupo de voices, com Stop/SetParams em
	// lote, e isso muda o AudioSourceComponent, o AudioWorld e o proxy de
	// script. E feature legitima, mas e outro patch — nao um no a mais.
	struct AXE_API SoundCueResult
	{
		std::string WaveUUID;
		float Volume = 1.0f;
		float Pitch = 1.0f;

		bool  OverrideAttenuation = false;
		float MinDistance = 1.0f;
		float MaxDistance = 100.0f;
		bool  Is3D = true;

		bool IsValid() const { return !WaveUUID.empty(); }
	};

	class AXE_API SoundCueAsset
	{
	public:
		// Cue novo a partir de um .wav: Output <- Modulator <- Random <- Wave.
		//
		// O Random com um filho so e no-op HOJE, e esta ali de proposito: e o
		// ponto onde o usuario arrasta a segunda e a terceira variacao. Ja o
		// Modulator nasce com faixa util (pitch 0.95–1.05), entao o cue e
		// audivelmente melhor que a wave crua no instante em que e criado.
		static std::shared_ptr<SoundCueAsset> Create(const std::string& name,
			const std::string& waveUUID);

		static std::shared_ptr<SoundCueAsset> LoadFromFile(const std::filesystem::path& path);
		bool Save(const std::filesystem::path& path) const;

		std::string ToJson() const;
		bool        FromJson(const std::string& json);

		const std::string& GetName() const { return m_Name; }

		// Categoria para a visualizacao de som. Propriedade do CUE, e nao de
		// quem o toca: "isto e um passo" e uma verdade do asset — o mesmo cue
		// disparado por notify, por script ou por Audio Source continua sendo
		// um passo.
		SoundCategory GetCategory() const { return m_Category; }
		void SetCategory(SoundCategory c) { m_Category = c; }

		// Bus de mixagem. Tambem propriedade do CUE: "isto e musica" e uma
		// verdade do asset, nao de quem o dispara.
		AudioBus GetBus() const { return m_Bus; }
		void SetBus(AudioBus b) { m_Bus = b; }

		// Prioridade no limite de vozes: 0 = descartavel, 1 = intocavel.
		// Propriedade do CUE pelo mesmo motivo da categoria e do bus —
		// "musica nao pode ser cortada" e verdade do asset.
		float GetPriority() const { return m_Priority; }
		void  SetPriority(float p) { m_Priority = p; }
		void SetName(const std::string& n) { m_Name = n; }

		// Percorre do Output para baixo e devolve o que tocar. Cada chamada
		// pode devolver algo diferente — e esse o ponto do Random.
		bool Evaluate(SoundCueResult& out) const;

		// ── Acesso para o editor de nos (A4b) ────────────────────────────
		std::vector<SoundCueNode>& GetNodes() { return m_Nodes; }
		const std::vector<SoundCueNode>& GetNodes() const { return m_Nodes; }
		std::vector<SoundCueLink>& GetLinks() { return m_Links; }
		const std::vector<SoundCueLink>& GetLinks() const { return m_Links; }

		SoundCueNode* FindNode(int id);
		const SoundCueNode* FindNode(int id) const;
		int  GetOutputNodeId() const;
		int  AllocId() { return m_NextId++; }

		// Necessarios pro undo por snapshot do editor: restaurar nos e links
		// sem restaurar o contador faria o proximo no nascer com um id ja em
		// uso, e os links passariam a apontar pro no errado.
		int  GetNextId() const { return m_NextId; }
		void SetNextId(int id) { m_NextId = id; }

	private:
		// Avaliacao recursiva. `depth` existe pelo mesmo motivo que existe no
		// subgrafo do rig: um link circular criado no editor viraria recursao
		// infinita, e um travamento sem stack util e o pior jeito de
		// descobrir que dois nos se apontam.
		bool EvalNode(int nodeId, SoundCueResult& out, int depth) const;

		std::string               m_Name;
		SoundCategory             m_Category = SoundCategory::Generic;
		AudioBus                  m_Bus = AudioBus::SFX;
		float                     m_Priority = 0.5f;
		std::vector<SoundCueNode> m_Nodes;
		std::vector<SoundCueLink> m_Links;
		int                       m_NextId = 1;
	};

} // namespace axe