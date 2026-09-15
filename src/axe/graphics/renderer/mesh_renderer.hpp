#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <array>
#include <glm/glm.hpp>
#include "axe/lighting/directional_light.hpp"

#include "axe/scene/scene_environment.hpp"
#include "cascaded_shadow_pass.hpp"   // FORWARD_SHADOW_V1 — AXE_SHADOW_CASCADES

namespace axe
{
	class Mesh;
	class Shader;
	class Pipeline;
	class Material;

	class AXE_API MeshRenderer
	{
	public:
		MeshRenderer();

		void Begin(const glm::mat4& viewProjection, const glm::vec3& cameraPosition);
		void DrawMesh(const Mesh& mesh, const glm::mat4& model, const Material* material = nullptr,
			const DirectionalLight* light = nullptr,
			bool transparent = false);
		void End();
		std::shared_ptr<Shader> GetDefaultShader() const { return m_Shader; }
		void SetEnvironment(const SceneEnvironment* env) { m_Environment = env; }
		void SetShadowMap(uint32_t depthMapID, const glm::mat4& lightSpaceMatrix);

		// ── SCENE_DEPTH_SURFACE_V1 — o que esta ATRAS da superficie ─────────
		//
		// O attachment de POSICAO do G-Buffer (mundo, RGB16F), para o passe
		// FORWARD de materiais translucidos poder ler a cena OPACA que ja foi
		// desenhada. E o que sustenta agua estilizada: a cor pela profundidade
		// da lamina e a espuma na linha da praia sao, as duas, a distancia
		// entre a superficie da agua e o fundo atras dela.
		//
		// So faz sentido no passe transparente. No caminho OPACO a geometria
		// ESCREVE nesse mesmo attachment, e ler de uma textura anexada ao FBO
		// corrente e comportamento indefinido em GL — por isso o shader do
		// G-Buffer recebe stubs neutros em vez desta textura (ver o
		// MaterialCompiler, mesmo carimbo).
		//
		// ID zero desliga: o material compila igual e le profundidade 0.
		void SetSceneDepthSource(uint32_t scenePositionTexID,
			uint32_t screenWidth, uint32_t screenHeight);

		// ── SCENE_HEIGHT_V1 ──────────────────────────────────────────────
		//
		//  As duas texturas do mapa de topo, mais a matriz que projeta uma
		//  posicao de mundo nelas.
		//
		//  Diferenca essencial para o SetSceneDepthSource logo acima: aquele
		//  entrega o G-Buffer, que so conhece o que a CAMERA VE, e por isso
		//  toda medida feita com ele muda quando a camera gira. Este entrega
		//  uma render de cima, que e a mesma de qualquer angulo.
		//
		//  Vai para os slots 10 e 11 — os primeiros livres depois dos mapas do
		//  material (0-4), do IBL (5-7), da shadow map (8) e do G-Buffer (9).
		void SetSceneHeightSource(uint32_t heightMapID, uint32_t seedMapID,
			const glm::mat4& topDownMatrix);

		// ═══════════════════════════════════════════════════════════════════
		//  FORWARD_SHADOW_V1 — as cascatas, para o passe TRANSLUCIDO
		//
		//  ── O DEFEITO QUE ISTO CONSERTA ────────────────────────────────────
		//
		//  Material translucido nao recebia sombra NENHUMA, e por dois motivos
		//  empilhados:
		//
		//    1. O shader gerado pelo MaterialCompiler nunca teve termo de
		//       sombra — nem uma linha. O shader FIXO daqui tem
		//       ShadowCalculation com PCF; o gerado a partir do grafo, nao.
		//
		//    2. E mesmo o fixo ficava sem dado: `SetShadowMap` so e chamado
		//       atras de `if (m_ShadowPass)`, e com `m_UseCSM = true` (o
		//       padrao) o RenderShadowPass entra no ramo das cascatas e o
		//       m_ShadowPass legado NUNCA e criado. u_HasShadowMap ficava 0.
		//
		//  Nao aparecia no opaco porque la a sombra vem do lighting pass
		//  deferred. So o forward — ou seja, exatamente a agua — ficava sem.
		//
		//  ── POR QUE COPIA, E NAO PONTEIRO GUARDADO ────────────────────────
		//
		//  Mesmo precedente do SetShadowMap e do SetSkyLight: guardar o
		//  ponteiro do passe entre frames e guardar algo que pode morrer. O
		//  que este renderer precisa sao seis valores pequenos; copia-los na
		//  hora custa nada e nao cria tempo de vida novo.
		//
		//  `csm` nulo (ou nao inicializado) desliga: o shader compila igual e
		//  le sombra zero, que e o comportamento anterior a esta rodada.
		void SetCascadedShadow(const CascadedShadowPass* csm, const glm::mat4& view);

	private:
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<Pipeline> m_Pipeline;
		std::shared_ptr<Pipeline> m_TransparentPipeline; // blend on, depth-write off
		// TWO_SIDED_V1 — identica, sem descarte de face. Escolhida por
		// Material::TwoSided; e o que mantem um plano de agua visivel quando a
		// camera passa por baixo dele.
		std::shared_ptr<Pipeline> m_TransparentTwoSidedPipeline;
		std::shared_ptr<Material> m_DefaultMaterial;

		glm::mat4 m_ViewProjection{ 1.0f };
		glm::vec3 m_CameraPosition{ 0.0f };

		const SceneEnvironment* m_Environment = nullptr;

		uint32_t  m_ShadowMapID = 0;
		glm::mat4 m_LightSpaceMatrix{ 1.0f };

		// SCENE_DEPTH_SURFACE_V1
		uint32_t  m_ScenePositionID = 0;
		uint32_t  m_SceneHeightID = 0;   // SCENE_HEIGHT_V1
		uint32_t  m_SceneSeedID = 0;   // SCENE_HEIGHT_V1
		glm::mat4 m_SceneHeightMatrix{ 1.0f };
		glm::vec2 m_ScreenSize{ 1.0f, 1.0f };

		// FORWARD_SHADOW_V1 — copia do que o CascadedShadowPass produziu.
		// Count = 0 significa "sem cascatas neste frame".
		int       m_CascadeCount = 0;
		uint32_t  m_CascadeArrayID = 0;
		glm::mat4 m_CascadeView{ 1.0f };
		std::array<glm::mat4, AXE_SHADOW_CASCADES> m_CascadeMatrices{};
		std::array<float, AXE_SHADOW_CASCADES>     m_CascadeSplits{};
		std::array<float, AXE_SHADOW_CASCADES>     m_CascadeTexelWorld{};
	};
}