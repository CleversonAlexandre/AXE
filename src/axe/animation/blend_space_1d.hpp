#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/pose.hpp"
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/skeleton.hpp"

#include <memory>
#include <string>
#include <vector>

namespace axe
{
	// Mistura N clipes ao longo de UM eixo contínuo.
	//
	// O caso clássico é locomoção: em vez de três estados discretos
	// (idle / walk / run) com transições entre eles, você tem UM parâmetro —
	// velocidade — e a pose é a mistura dos dois clipes vizinhos:
	//
	//     idle(0)          walk(200)              run(600)
	//       |-----------------|---------------------|
	//                    ^ speed = 320
	//              → 78% walk + 22% run
	//
	// Isso elimina o "pop" de trocar de animação no meio de uma aceleração,
	// que nenhum crossfade resolve bem (o crossfade sabe QUANDO trocar; o
	// blend space não precisa trocar nunca).
	//
	// É a "Blend Space" da Unreal. Duas dimensões (velocidade × direção)
	// ficam pro Milestone 4 — o problema lá é triangulação, não blending.
	class AXE_API BlendSpace1D
	{
	public:
		struct Sample
		{
			std::shared_ptr<AnimationClip> Clip;
			float Value = 0.0f;   // posição no eixo (ex: velocidade)
		};

		void AddSample(const std::shared_ptr<AnimationClip>& clip, float value);
		void Clear() { m_Samples.clear(); }

		const std::vector<Sample>& GetSamples() const { return m_Samples; }
		bool IsEmpty() const { return m_Samples.empty(); }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& n) { m_Name = n; }

		// Avalia em `value`, no instante `time`, e produz a pose.
		//
		// SINCRONIZAÇÃO DE FASE: os clipes NÃO são amostrados no mesmo tempo
		// absoluto — são amostrados na mesma FASE (0..1 do ciclo). Sem isso,
		// walk (1.2s) e run (0.7s) andam fora de compasso, e o blend mistura
		// o pé esquerdo de um com o direito do outro: o personagem "patina"
		// e as pernas se atravessam. Esse é O bug de blend space, e a
		// solução é normalizar o tempo pela duração de cada clipe.
		void Evaluate(const Skeleton& skeleton,
			float value,
			float time,
			Pose& outPose) const;

		// Duração efetiva em `value` — a média ponderada das durações dos
		// clipes ativos.
		//
		// É isso que o chamador usa pra avançar o tempo: conforme o
		// personagem acelera, o ciclo naturalmente encurta (a passada fica
		// mais rápida), sem nenhum ajuste manual de PlayRate.
		float GetDurationAt(float value) const;

	private:
		// Devolve os dois samples vizinhos de `value` e o fator entre eles.
		void FindNeighbors(float value, int& outA, int& outB, float& outT) const;

		std::string         m_Name;
		std::vector<Sample> m_Samples;   // mantidos ORDENADOS por Value
	};

} // namespace axe