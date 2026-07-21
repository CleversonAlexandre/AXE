#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace axe
{
	struct VectorKey
	{
		float     Time = 0.0f;   // em SEGUNDOS (já convertido de ticks no load)
		glm::vec3 Value{ 0.0f };
	};

	struct QuatKey
	{
		float     Time = 0.0f;   // em SEGUNDOS
		glm::quat Value{ 1.0f, 0.0f, 0.0f, 0.0f }; // (w, x, y, z)
	};

	// Curva de animação de UM bone. Os três canais (T/R/S) são
	// independentes e podem ter contagens de chaves diferentes — é assim
	// que o Assimp entrega e é assim que qualquer DCC exporta.
	struct AXE_API BoneChannel
	{
		int BoneIndex = -1;   // índice dentro do Skeleton alvo

		std::vector<VectorKey> PositionKeys;
		std::vector<QuatKey>   RotationKeys;
		std::vector<VectorKey> ScaleKeys;

		glm::vec3 SamplePosition(float timeSeconds) const;
		glm::quat SampleRotation(float timeSeconds) const;
		glm::vec3 SampleScale(float timeSeconds) const;

		// Monta o transform local completo: T * R * S.
		glm::mat4 SampleLocal(float timeSeconds) const;

		bool IsEmpty() const
		{
			return PositionKeys.empty() && RotationKeys.empty() && ScaleKeys.empty();
		}
	};

	// ── Notify ───────────────────────────────────────────────────────────────
	//
	// Um marcador num INSTANTE do clipe: "aqui o pe toca o chao", "aqui a
	// janela de dano abre". O runtime dispara quando o tempo do clipe cruza
	// Time; quem reage decide o que fazer (som, particula, script via
	// EventBus). O clipe so marca O QUANDO — nunca executa nada.
	struct AXE_API AnimNotify
	{
		enum class Kind : int { Event = 0, Sound = 1, Particle = 2 };

		float       Time = 0.0f;      // segundos, dentro de [0, Duration]
		std::string Name;             // nome do evento ("FootStep_L", "AttackOpen")
		Kind        Type = Kind::Event;

		// UUID do asset (som/particula) escolhido no AssetPicker — o mesmo
		// jeito que o resto do engine referencia assets. Vazio para Event.
		std::string Payload;

		// Em qual TRACK da timeline este notify mora (0-based). Tracks sao
		// organizacao de autoria, como na Unreal: passos numa, VFX noutra —
		// o runtime dispara todas igual.
		int Track = 0;

		// ── Ancoragem e transform (Sound/Particle), como na Unreal ───────
		std::string Socket;                      // osso de ancoragem ("" = origem do personagem)
		glm::vec3   LocationOffset{ 0.0f };
		glm::vec3   RotationOffset{ 0.0f };      // graus
		glm::vec3   Scale{ 1.0f };
		bool        Attached = true;             // segue o osso vs solta no mundo

		// Cor do losango na timeline. Nasce com a cor do TIPO e o usuario
		// pode personalizar — igual ao Notify Color da Unreal.
		glm::vec3   Color{ 0.47f, 0.75f, 1.0f };
	};

	class AXE_API AnimationClip
	{
	public:
		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& n) { m_Name = n; }

		// Duração em SEGUNDOS.
		float GetDuration() const { return m_Duration; }
		void  SetDuration(float d) { m_Duration = d; }

		bool  IsLooping() const { return m_Looping; }
		void  SetLooping(bool l) { m_Looping = l; }

		// Adiciona um canal e indexa por BoneIndex. Canal com
		// BoneIndex == -1 (bone não existe no esqueleto alvo) é
		// descartado silenciosamente — é o que permite tocar um clipe
		// exportado com bones extras num esqueleto mais enxuto.
		void AddChannel(const BoneChannel& channel);

		// nullptr se o clipe não anima este bone (o sampler cai na
		// LocalBindPose nesse caso).
		const BoneChannel* FindChannel(int boneIndex) const;

		const std::vector<BoneChannel>& GetChannels() const { return m_Channels; }
		std::size_t GetChannelCount() const { return m_Channels.size(); }

		// Aplica loop ou clamp em `time` de acordo com m_Looping.
		// Devolve um tempo sempre dentro de [0, Duration].
		float WrapTime(float timeSeconds) const;

		// ── Propriedades de autoria (editadas no Animation Editor) ────────
		//
		// Publicas de proposito: sao dados de autoria persistidos no
		// .axeskel (clip_meta), nao estado de runtime. RateScale e RootMotion
		// ainda nao sao consumidos pelo sampler — entram com o disparo de
		// notifies no runtime (proxima etapa); o editor ja os grava.
		float RateScale = 1.0f;
		bool  RootMotion = false;

		std::vector<AnimNotify> Notifies;   // mantidas ordenadas por Time

		// Quantas tracks a timeline deste clipe mostra (>=1).
		int NotifyTrackCount = 1;

	private:
		std::string              m_Name;
		float                    m_Duration = 0.0f;
		bool                     m_Looping = true;

		std::vector<BoneChannel> m_Channels;

		// BoneIndex → posição em m_Channels. Evita busca linear a cada
		// bone, a cada frame, pra cada personagem.
		std::unordered_map<int, int> m_BoneToChannel;
	};

} // namespace axe