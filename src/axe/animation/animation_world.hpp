#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/pose.hpp"

#include "axe/animation/animation_clip.hpp"   // AnimNotify
#include <entt/entt.hpp>
#include <vector>

namespace axe
{
	class Scene;

	// Avança o tempo de todos os SkeletalMeshComponent da cena e recalcula
	// as palettes de bone.
	//
	// Mesmo contrato do PhysicsWorld e do ParticleWorld — chamado uma vez
	// por frame no EditorLayer::OnUpdate, ANTES do render. Ordem importa: o
	// SceneCollector lê a palette que este sistema acabou de escrever.
	//
	// Roda 100% na CPU e não toca em GPU: é só matemática (AnimationSampler).
	// Quem sobe pra GPU é o SkinningPass, dentro do SceneRenderer.
	class AXE_API AnimationWorld
	{
	public:
		// inPlay = false: o tempo NÃO avança, mas a palette continua sendo
		// calculada. É o que permite o editor mostrar o personagem na pose
		// certa (bind pose, ou o frame onde você parou o scrub) em vez de
		// uma mesh colapsada na origem.
		void OnUpdate(Scene& scene, float deltaTime, bool inPlay);

	private:
		// FX spawnados por notify (particulas): vivem alguns segundos e o
		// proprio mundo os destroi. Só nascem em PLAY — entidades de Play
		// morrem com o restore do snapshot no Stop, entao a cena salva
		// nunca ganha lixo.
		struct NotifyFx
		{
			entt::entity Entity{ entt::null };
			float Ttl = 0.0f;
		};

		std::vector<NotifyFx> m_NotifyFx;

	public:
		// ── Modo audicao ─────────────────────────────────────────────────
		//
		// Liga o som dos notifies em 2D, sem posicao e sem atenuacao.
		//
		// Existe porque as janelas de preview (Animation Editor) rodam a
		// propria cena, com a propria camera — mas o LISTENER e global e
		// mora na camera do viewport principal. O personagem do preview toca
		// num canto do mundo e o ouvido esta em outro, entao a distancia
		// entre os dois derruba o volume, e o usuario ouve baixo um som que
		// no jogo esta correto.
		//
		// Espacializar num preview nao teria sentido de qualquer forma: ali
		// voce esta CONFERINDO o som, nao posicionando-o. Mesma escolha do
		// Preview do Sound Cue.
		//
		// Por INSTANCIA, e nao global: a AnimationWorld do jogo continua
		// espacializando normalmente.
		void SetSoundAudition(bool audition) { m_SoundAudition = audition; }
		bool IsSoundAudition() const { return m_SoundAudition; }

	private:
		bool m_SoundAudition = false;

		void DispatchNotifies(Scene& scene, entt::entity character,
			const std::vector<AnimNotify>& fired, bool inPlay);

		// SC43 — deposita a matriz de mundo do socket em cada
		// SocketAttachmentComponent da cena. Roda no FIM do OnUpdate, quando
		// toda pose do frame ja existe. Ver a nota no .cpp.
		void UpdateSocketAttachments(Scene& scene);

		// Buffer de pose reusado entre personagens.
		//
		// Um vector<BoneTransform> alocado por personagem, por frame, seria
		// morte por mil cortes — e o profiler apontaria pro malloc, não pra
		// animação. Como a avaliação é sequencial, um buffer só basta.
		Pose m_ScratchPose;
	};

} // namespace axe