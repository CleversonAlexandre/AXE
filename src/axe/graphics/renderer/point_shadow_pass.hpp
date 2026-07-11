#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
	struct RenderQueue;

	// ── Point Shadow Pass ────────────────────────────────────────────────
	// Sombras omnidirecionais pra Point/Spot Lights, num CUBE MAP ARRAY
	// único (R32F): cada layer é a sombra de uma luz, guardando a
	// DISTÂNCIA LINEAR até a luz normalizada pelo Radius (0..1). O shader
	// do lighting compara length(frag - luz) contra o valor armazenado —
	// o clássico "omnidirectional shadow mapping".
	//
	// Por que distância linear em R32F em vez de depth buffer: a
	// comparação vira uma subtração simples e uniforme em todas as
	// direções, sem lidar com a não-linearidade do depth perspectivo por
	// face — menos artefato de borda entre faces do cubo.
	//
	// Até kMaxShadowLights luzes por frame (o SceneRenderer escolhe as
	// mais próximas da câmera entre as marcadas com CastShadows). Spot
	// Lights usam o mesmo caminho: o cone já mascara o resto da esfera —
	// meia dúzia de faces desperdiçadas, mas UM código só.
	//
	// Custo honesto: cada luz sombreada re-renderiza a cena 6x por frame
	// (na resolução do shadow map). 4 luzes = 24 mini-passes. OK pra
	// cenas de porte médio; o upgrade futuro é cache pra luzes estáticas.
	class AXE_API PointShadowPass
	{
	public:
		static constexpr int kMaxShadowLights = 4;

		virtual ~PointShadowPass() = default;

		virtual void Initialize(uint32_t resolution = 512) = 0;
		virtual bool IsInitialized() const = 0;

		// Renderiza as 6 faces da sombra da luz no layer indicado.
		// farPlane deve ser o Radius da luz (a distância é normalizada
		// por ele). Salva e restaura FBO/viewport — NÃO vaza estado GL
		// (lição aprendida com o CSM e os previews).
		virtual void RenderLightShadow(const RenderQueue& queue, int layer,
			const glm::vec3& lightPos, float farPlane) = 0;

		// ID opaco do cube map array (mesmo contrato do shadowMapID do
		// lighting pass) — consumido pelo OpenGLLightingPass no unit 28.
		virtual uint32_t GetTextureID() const = 0;

		static std::shared_ptr<PointShadowPass> Create();
	};
}