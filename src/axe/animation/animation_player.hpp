#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/pose.hpp"
#include "axe/animation/bone_mask.hpp"
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/skeleton.hpp"

#include <memory>
#include <vector>

namespace axe
{
	// Toca clipes e faz a transição entre eles.
	//
	// É o runtime de animação de UM personagem. No Milestone 4 o AnimGraph
	// (state machine) vai dirigir este player — mas o player não sabe nada
	// sobre grafos, e essa separação é de propósito: o gameplay pode chamar
	// Play("attack") direto, sem AnimGraph nenhum, e funciona.
	//
	// O que ele resolve:
	//   - crossfade: sair de um clipe pro outro sem "pulo"
	//   - interromper uma transição no meio com outra (acontece o tempo todo:
	//     idle→run é interrompido por run→jump antes de terminar)
	//   - uma camada opcional por cima (mascarada e/ou aditiva)
	class AXE_API AnimationPlayer
	{
	public:
		// ── Camada base ──────────────────────────────────────────────────

		// Transiciona pro clipe dado ao longo de `blendTime` segundos.
		// blendTime = 0 → troca imediata (corte seco).
		//
		// Chamar com o clipe que já está tocando é NO-OP: sem isso, um
		// gameplay que chama Play("idle") todo frame reiniciaria a animação
		// eternamente e o personagem ficaria congelado no frame 0.
		void Play(const std::shared_ptr<AnimationClip>& clip, float blendTime = 0.2f);

		// Reinicia do zero mesmo que seja o mesmo clipe (ex: atacar duas
		// vezes seguidas — você QUER que o segundo golpe recomece).
		void PlayRestart(const std::shared_ptr<AnimationClip>& clip, float blendTime = 0.1f);

		// ── Camada de cima (opcional) ────────────────────────────────────
		//
		// Dois modos, e a diferença importa:
		//
		//   Mascarada  — a camada SUBSTITUI os ossos marcados.
		//                Ex: correr (base) + atirar (camada, do peito pra
		//                cima). Os braços passam a ser os do tiro.
		//
		//   Aditiva    — a camada SOMA em cima da base, sem substituí-la.
		//                Ex: correr + respirar ofegante, ou um tranco de
		//                dano. A corrida continua visível por baixo.
		void SetLayer(const std::shared_ptr<AnimationClip>& clip,
			const BoneMask* mask,
			bool additive,
			float weight = 1.0f);

		void ClearLayer() { m_Layer.Clip.reset(); }

		// Clipe de referência do aditivo (normalmente a bind pose ou o frame
		// 0 do idle). O aditivo é a DIFERENÇA em relação a isto — sem
		// referência correta, o "tranco" aplica a pose inteira em vez do
		// delta, e o personagem se contorce.
		void SetAdditiveReference(const std::shared_ptr<AnimationClip>& clip, float atTime = 0.0f);

		// ── Tick ─────────────────────────────────────────────────────────

		// Avança o tempo e produz a pose final.
		// `advanceTime = false` congela o tempo mas continua avaliando —
		// é o que faz o preview do editor mostrar a pose certa.
		void Update(const Skeleton& skeleton, float deltaTime, bool advanceTime = true);

		const Pose& GetPose() const { return m_Result; }

		bool IsBlending() const { return m_BlendDuration > 0.0f && m_BlendElapsed < m_BlendDuration; }
		float GetBlendAlpha() const;

		const AnimationClip* GetCurrentClip() const { return m_Current.Clip.get(); }

		// NAO renomeie para GetCurrentTime/SetCurrentTime.
		//
		// O <windows.h> define GetCurrentTime como MACRO:
		//     #define GetCurrentTime() GetTickCount()
		//
		// Um metodo com esse nome e reescrito pelo pre-processador e o
		// compilador reclama que "GetTickCount nao e membro de
		// AnimationPlayer" — um erro que nao faz sentido nenhum ate voce
		// descobrir a macro. E invisivel fora do Windows.
		float GetTime() const { return m_Current.Time; }
		void  SetTime(float t) { m_Current.Time = t; }

		float PlayRate = 1.0f;   // 0 = pausado, negativo = ré
		bool  Playing = true;

	private:
		struct Track
		{
			std::shared_ptr<AnimationClip> Clip;
			float Time = 0.0f;
		};

		struct LayerTrack
		{
			std::shared_ptr<AnimationClip> Clip;
			const BoneMask* Mask = nullptr;
			bool  Additive = false;
			float Weight = 1.0f;
			float Time = 0.0f;
		};

		Track m_Current;   // clipe pro qual estamos indo (ou já estamos)
		Track m_Previous;  // clipe do qual estamos saindo (durante o blend)

		LayerTrack m_Layer;

		std::shared_ptr<AnimationClip> m_AdditiveRefClip;
		float m_AdditiveRefTime = 0.0f;

		float m_BlendElapsed = 0.0f;
		float m_BlendDuration = 0.0f;

		// Buffers reaproveitados entre frames — sem isto seriam ~5 vectors
		// alocados e destruídos por personagem, por frame.
		Pose m_PoseCurrent;
		Pose m_PosePrevious;
		Pose m_PoseLayer;
		Pose m_PoseAdditiveRef;
		Pose m_PoseAdditive;
		Pose m_Result;
	};

} // namespace axe
