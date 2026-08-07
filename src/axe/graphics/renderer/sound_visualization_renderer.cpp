#include "axe/graphics/renderer/sound_visualization_renderer.hpp"
#include "axe/audio/sound_event.hpp"

#include <algorithm>
#include <cmath>

namespace axe
{
	namespace
	{
		constexpr int   kSegments = 28;
		constexpr float kTwoPi = 6.28318530718f;

		// Raio do pulso em unidades de mundo.
		//
		// Escala com o VOLUME e com o alcance da fonte, nao com a distancia
		// ate a camera: um som alto tem que parecer alto de longe. Como o anel
		// vive no espaco do mundo, a perspectiva ja cuida de encolhe-lo com a
		// distancia — que e a leitura certa de "isso esta longe".
		float RadiusOf(const SoundEvent& e)
		{
			const float loud = std::clamp(e.Volume, 0.0f, 2.0f);
			const float reach = std::clamp(e.MaxDistance, 1.0f, 200.0f);

			// A raiz comprime a faixa: sem ela, uma fonte com MaxDistance 100
			// desenharia um anel dez vezes maior que uma de 10, e o de perto
			// sumiria.
			return 0.25f + 0.9f * loud + 0.10f * std::sqrt(reach);
		}
	}

	void SoundVisualizationRenderer::PushRing(const glm::vec3& center, float radius,
		const glm::vec3& right, const glm::vec3& up,
		const glm::vec4& color)
	{
		glm::vec3 prev = center + right * radius;

		for (int i = 1; i <= kSegments; ++i)
		{
			const float a = (float)i / (float)kSegments * kTwoPi;
			const glm::vec3 p = center
				+ right * (std::cos(a) * radius)
				+ up * (std::sin(a) * radius);

			m_Lines.DrawLine(prev, p, color);
			prev = p;
		}
	}

	void SoundVisualizationRenderer::Render(const std::vector<SoundEvent>& sounds,
		const glm::mat4& view,
		const glm::mat4& projection,
		const glm::vec3& cameraPosition)
	{
		if (sounds.empty())
			return;

		// Eixos da camera extraidos da view matrix. Ela e a inversa da pose,
		// entao as LINHAS da parte rotacional sao os eixos do mundo — por isso
		// lemos por linha e nao por coluna.
		const glm::vec3 right = glm::normalize(glm::vec3(view[0][0], view[1][0], view[2][0]));
		const glm::vec3 up = glm::normalize(glm::vec3(view[0][1], view[1][1], view[2][1]));

		m_Lines.Begin(projection * view);

		for (const SoundEvent& e : sounds)
		{
			// Som 2D nao tem lugar no mundo — musica, narracao, UI. Desenhar
			// um anel na origem para eles seria ruido puro.
			if (!e.Spatialized)
				continue;

			const float alpha = e.Alpha();

			if (alpha <= 0.001f)
				continue;

			const glm::vec3 rgb = SoundCategoryColor(e.Category);
			const float base = RadiusOf(e);

			// DOIS aneis: um fixo, marcando a fonte, e um que se expande e
			// desbota, marcando o INSTANTE.
			//
			// O segundo e o que carrega a informacao temporal. Sem ele, dois
			// tiros seguidos no mesmo lugar seriam indistinguiveis de um so —
			// e saber que o inimigo esta disparando em rajada e exatamente o
			// tipo de coisa que o jogador surdo perde.
			PushRing(e.Position, base, right, up, glm::vec4(rgb, alpha * 0.85f));

			const float t = 1.0f - alpha;                 // 0 no disparo, 1 no fim
			PushRing(e.Position, base * (1.0f + t * 1.6f), right, up,
				glm::vec4(rgb, alpha * alpha * 0.55f));

			// Cruzeta central: um anel sozinho vira um "o" ambiguo quando
			// varios se sobrepoem. A cruz ancora o olho no ponto exato.
			const float tick = base * 0.28f;
			const glm::vec4 c(rgb, alpha);

			m_Lines.DrawLine(e.Position - right * tick, e.Position + right * tick, c);
			m_Lines.DrawLine(e.Position - up * tick, e.Position + up * tick, c);
		}

		m_Lines.End();

		(void)cameraPosition;   // reservado: ordenar por distancia se necessario
	}

} // namespace axe