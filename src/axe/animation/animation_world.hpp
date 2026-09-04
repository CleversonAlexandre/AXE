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

		// ═══════════════════════════════════════════════════════════════════
		//  NOTIFY_SOCKET_V1 — a matriz de mundo de um socket, UMA vez so
		//
		//  ── POR QUE PUBLICA E ESTATICA ────────────────────────────────────
		//
		//  Tres lugares precisam desta conta: o anexo de socket
		//  (UpdateSocketAttachments), o disparo de notify no jogo, e o preview
		//  do Animation Editor. Os dois primeiros ja a faziam; o terceiro nao,
		//  e por isso a particula aparecia no cano no editor e nos pes no jogo.
		//
		//  A licao ja custou caro nesta engine: quando a MESMA pergunta e
		//  respondida em dois lugares, os dois divergem, e o sintoma e sempre
		//  "funciona no editor e nao no jogo" — o pior tipo de bug que existe,
		//  porque so aparece depois que alguem ja posicionou tudo certinho.
		//
		//  ── O QUE ELA ACEITA ──────────────────────────────────────────────
		//
		//  `socketName` pode ser o nome de um SOCKET do `.axeskel` (leva o
		//  transform local do socket junto) ou o nome cru de um OSSO. Aceitar
		//  os dois e o que faz o campo Socket do AnimNotify funcionar tanto
		//  para "sai do GunSocket" quanto para "sai da mao direita".
		//
		//  Devolve false quando nao ha o que resolver — nome vazio, personagem
		//  sem esqueleto, ou pose ainda nao calculada neste frame. Quem chama
		//  cai na convencao antiga (origem do personagem), que e visivelmente
		//  errada mas visivel: sumir seria pior.
		// ═══════════════════════════════════════════════════════════════════
		static bool ResolveSocketWorld(Scene& scene, entt::entity character,
			const std::string& socketName, glm::mat4& out);
		// inPlay = false: o tempo NÃO avança, mas a palette continua sendo
		// calculada. É o que permite o editor mostrar o personagem na pose
		// certa (bind pose, ou o frame onde você parou o scrub) em vez de
		// uma mesh colapsada na origem.
		void OnUpdate(Scene& scene, float deltaTime, bool inPlay);

		// ═══════════════════════════════════════════════════════════════════
		//  SOCKET_LAG_V1 — recompoe o mundo dos anexos de socket
		//
		//  ── POR QUE ISTO E PUBLICO ────────────────────────────────────────
		//
		//  A matriz de um anexo e
		//
		//      mundo do personagem  x  osso na pose  x  local do socket
		//
		//  O OnUpdate ja a calcula no fim, e para os previews do editor isso
		//  basta: la ninguem move o personagem depois.
		//
		//  No JOGO nao basta. A ordem do frame e animacao -> scripts -> fisica
		//  -> sequence, e os tres ultimos MOVEM o personagem. O anexo ficava
		//  com a posicao de ANTES do movimento — um frame inteiro atras. Parado
		//  isso e invisivel; correndo, a arma escorrega da mao exatamente
		//  `velocidade x dt`, e quanto mais rapido, mais longe.
		//
		//  A pose (BoneGlobals) nao muda depois da animacao; so a transform de
		//  mundo do personagem muda. Entao recompor no fim do frame e barato e
		//  e a unica forma de a arma chegar onde a mao chegou.
		//
		//  Chamar duas vezes por frame e de proposito: a chamada de dentro do
		//  OnUpdate fica para nao quebrar os cinco previews do editor que criam
		//  um AnimationWorld proprio e nunca chamam mais nada.
		// ═══════════════════════════════════════════════════════════════════
		void UpdateSocketAttachments(Scene& scene);

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


		// Buffer de pose reusado entre personagens.
		//
		// Um vector<BoneTransform> alocado por personagem, por frame, seria
		// morte por mil cortes — e o profiler apontaria pro malloc, não pra
		// animação. Como a avaliação é sequencial, um buffer só basta.
		Pose m_ScratchPose;
	};

} // namespace axe