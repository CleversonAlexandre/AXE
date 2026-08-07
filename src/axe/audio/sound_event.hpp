#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include <cstdint>
#include <cstring>

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════════
	//  VISUALIZACAO DE SOM
	//
	//  Mostra na tela DE ONDE cada som esta vindo. E recurso de
	//  acessibilidade: um jogador surdo nao ouve o inimigo atras da parede,
	//  mas pode ver o pulso na direcao dele.
	//
	//  NAO precisa de ray tracing acustico. A versao util so precisa de duas
	//  coisas que o sistema ja produz desde o A2: a POSICAO da fonte e a
	//  INTENSIDADE com que ela tocou. Um pulso na fonte entrega a maior parte
	//  do valor de acessibilidade; propagacao geometrica (o pulso aparecer na
	//  parede onde o som refletiu) e refinamento de uma camada futura, nao
	//  pre-requisito.
	// ═════════════════════════════════════════════════════════════════════════

	enum class SoundCategory : int
	{
		Generic = 0,
		Footstep,
		Weapon,
		Impact,
		Voice,
		Ambient,

		Count
	};

	inline const char* SoundCategoryToString(SoundCategory c)
	{
		switch (c)
		{
		case SoundCategory::Generic:  return "Generic";
		case SoundCategory::Footstep: return "Footstep";
		case SoundCategory::Weapon:   return "Weapon";
		case SoundCategory::Impact:   return "Impact";
		case SoundCategory::Voice:    return "Voice";
		case SoundCategory::Ambient:  return "Ambient";
		default:                      return "Generic";
		}
	}

	inline SoundCategory SoundCategoryFromString(const char* s)
	{
		for (int i = 0; i < (int)SoundCategory::Count; ++i)
		{
			const auto c = (SoundCategory)i;

			if (std::strcmp(SoundCategoryToString(c), s) == 0)
				return c;
		}

		return SoundCategory::Generic;
	}

	// Cor por categoria.
	//
	// A escolha nao e decorativa: quem depende disto precisa distinguir
	// "passo" de "tiro" em meio segundo. Por isso os tons sao bem separados em
	// MATIZ, e a intensidade e codificada no TAMANHO do pulso, nao na cor —
	// um jogador com daltonismo perde a categoria mas continua lendo direcao
	// e distancia, que e a informacao que salva.
	inline glm::vec3 SoundCategoryColor(SoundCategory c)
	{
		switch (c)
		{
		case SoundCategory::Footstep: return { 0.35f, 0.90f, 0.45f };  // verde
		case SoundCategory::Weapon:   return { 1.00f, 0.30f, 0.25f };  // vermelho
		case SoundCategory::Impact:   return { 1.00f, 0.70f, 0.20f };  // ambar
		case SoundCategory::Voice:    return { 0.40f, 0.70f, 1.00f };  // azul
		case SoundCategory::Ambient:  return { 0.65f, 0.55f, 0.85f };  // lilas
		default:                      return { 0.85f, 0.85f, 0.85f };  // branco
		}
	}

	// Um som ativo, do ponto de vista de quem vai desenha-lo.
	//
	// Deliberadamente SEM referencia a entidade: one-shot de AnimNotify e de
	// script nao tem dono, e amarrar isto a uma entity deixaria de fora
	// justamente os sons mais informativos (passo, tiro).
	struct AXE_API SoundEvent
	{
		glm::vec3     Position{ 0.0f };
		float         Volume = 1.0f;      // ja com os multiplicadores do cue
		float         MaxDistance = 100.0f;
		SoundCategory Category = SoundCategory::Generic;
		bool          Spatialized = true;

		float         Age = 0.0f;
		float         Lifetime = 0.6f;

		// Voice associada, quando ha uma. Zero para one-shot: ele dispara e
		// e esquecido, e o pulso vive sozinho ate expirar.
		std::uint32_t Voice = 0;

		float Alpha() const
		{
			if (Lifetime <= 0.0f)
				return 0.0f;

			const float t = Age / Lifetime;
			return (t >= 1.0f) ? 0.0f : (1.0f - t);
		}
	};

} // namespace axe