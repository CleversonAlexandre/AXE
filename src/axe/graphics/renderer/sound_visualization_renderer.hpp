#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/graphics/renderer/line_renderer.hpp"

#include <vector>

namespace axe
{
	struct SoundEvent;

	// ── SoundVisualizationRenderer ───────────────────────────────────────────
	//
	// Desenha um pulso na posicao de cada som ativo. Recurso de
	// acessibilidade: um jogador surdo nao ouve o inimigo atras da parede, mas
	// ve o anel na direcao dele.
	//
	// NAO le a Scene, ao contrario do ColliderDebugRenderer. Recebe a lista
	// pronta, e isso e deliberado: os sons mais informativos — passo de
	// AnimNotify, tiro de script — NAO TEM ENTIDADE. Um renderer que varresse
	// o ECS deixaria de fora justamente o que importa.
	//
	// Tambem nao ha shader novo: o pulso e um circulo de segmentos desenhado
	// pelo LineRenderer que ja existe. Menos codigo, zero risco de backend, e
	// o resultado le bem em cima de qualquer cena.
	class AXE_API SoundVisualizationRenderer
	{
	public:
		void Render(const std::vector<SoundEvent>& sounds,
			const glm::mat4& view,
			const glm::mat4& projection,
			const glm::vec3& cameraPosition);

	private:
		// Circulo virado para a camera (billboard). Um anel no plano do chao
		// desapareceria visto de lado — que e exatamente a situacao em que o
		// jogador mais precisa dele.
		void PushRing(const glm::vec3& center, float radius,
			const glm::vec3& right, const glm::vec3& up,
			const glm::vec4& color);

		LineRenderer m_Lines;
	};

} // namespace axe