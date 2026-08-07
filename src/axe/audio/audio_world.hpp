#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

namespace axe
{
	class Scene;

	// ── AudioWorld ───────────────────────────────────────────────────────────
	//
	// Le o ECS e traduz fontes em voices. Mesma posicao que o ParticleWorld
	// ocupa para particulas — e, no frame, mesmo contrato: OnUpdate por
	// frame, OnScenePlay/OnSceneStop nas transicoes.
	//
	// ORDEM NO FRAME (por que fica no FIM do OnUpdate):
	//
	//   ParticleWorld -> AnimationWorld -> [Play] Script -> [Play] Physics
	//     -> AudioWorld -> (OnRender: SceneCollector -> SceneRenderer)
	//
	//   Uma fonte 3D presa a um personagem le a posicao do transform. Se o
	//   audio rodasse antes da fisica, a posicao seria a do frame anterior —
	//   audivel como atraso de panning em movimento rapido. O render nao
	//   depende do audio, entao o audio pode ser o ultimo.
	//
	//   Isso vale so para as voices PERSISTENTES. One-shot (AnimNotify,
	//   PlaySound) nao passa por aqui: dispara no instante da chamada, com a
	//   posicao daquele momento. Deferir one-shot para este tick atrasaria
	//   todo passo e todo tiro em um frame.
	//
	// EDIT vs PLAY: fontes da cena so tocam em Play. Som disparando sozinho
	// enquanto o usuario edita e hostil — a regra e mais rigida que a das
	// particulas, que tickam em Edit como preview visual. O preview de audio
	// em Edit existe, mas so por pedido explicito (botao do Inspector), via
	// _PlayRequested.
	class AXE_API AudioWorld
	{
	public:
		// listenerPos/Forward/Up: pose de FALLBACK, usada quando a cena nao
		// tem AudioListenerComponent — na pratica, a camera ativa. O editor
		// passa a GameCamera em Play e a camera do viewport em Edit.
		void OnUpdate(Scene& scene, float deltaTime,
			bool inPlay,
			bool paused,
			const glm::vec3& listenerPos,
			const glm::vec3& listenerForward,
			const glm::vec3& listenerUp);

		// Dispara as fontes com PlayOnStart. Chamado DEPOIS do
		// SceneSnapshot::Capture, para que o snapshot guarde a cena sem
		// nenhuma voice viva — assim o Restore devolve handles limpos.
		void OnScenePlay(Scene& scene);

		// Mata toda voice da cena e zera os handles. Sem isso, um loop
		// disparado em Play sobreviveria ao Stop e tocaria por cima do
		// editor ate o programa fechar.
		void OnSceneStop(Scene& scene);

	private:
		bool m_Paused = false;

		// Pose do listener no frame anterior, para derivar a velocidade dele.
		glm::vec3 m_LastListenerPos{ 0.0f };
		bool      m_HasLastListenerPos = false;

		bool m_WarnedMultipleListeners = false;
	};

} // namespace axe