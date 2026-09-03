#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <vector>
#include <array>

namespace axe
{
    class Mesh;

    // Número de cascades — 4 é o padrão da indústria (perto→longe).
    static constexpr int AXE_SHADOW_CASCADES = 4;

    struct CascadeData
    {
        glm::mat4 LightSpaceMatrix{ 1.0f };
        float     SplitDepth = 0.0f; // profundidade do fim desta cascade (view space)

        // ── SHADOW_ACNE_V1 ───────────────────────────────────────────────────
        //
        // Quanto MUNDO cabe em um texel desta cascade (metros/texel).
        //
        // E o numero que faltava para corrigir acne direito. O bias constante
        // que existia e um chute em unidades de profundidade — o mesmo valor e
        // pouco na cascade 0 e demais na 3, porque um texel da cascade 3 cobre
        // dezenas de vezes mais chao. Com esta medida, o deslocamento passa a
        // ser expresso em TEXEIS, que e a unidade em que o problema acontece.
        float     TexelWorldSize = 0.0f;

        // PCSS_V1 — quantos METROS o intervalo [0,1] de profundidade desta
        // cascade cobre. Converte a diferenca de profundidade lida no mapa
        // (adimensional) para a distancia real entre bloqueador e receptor,
        // que e o que determina o tamanho da penumbra.
        float     DepthRange = 0.0f;
    };

    // Cascaded Shadow Maps — divide o frustum da câmera em N faixas de
    // profundidade, cada uma com seu próprio shadow map. Resolução alta
    // perto, baixa longe — sombras nítidas em toda a distância.
    class AXE_API CascadedShadowPass
    {
    public:
        virtual ~CascadedShadowPass() = default;

        virtual void Initialize(uint32_t resolution = 2048) = 0;
        virtual bool IsInitialized() const = 0;

        // Calcula as N matrizes de cascade a partir do frustum da câmera.
        virtual void ComputeCascades(
            const glm::vec3& lightDir,
            const glm::mat4& cameraView,
            const glm::mat4& cameraProj,
            float cameraNear, float cameraFar) = 0;

        // Renderiza a geometria em cada cascade.
        virtual void Begin(int cascadeIndex) = 0;
        virtual void DrawMesh(const Mesh& mesh, const glm::mat4& model) = 0;
        virtual void End() = 0;

        virtual int      GetCascadeCount() const = 0;
        virtual uint32_t GetDepthArrayID() const = 0; // texture array com todas as cascades
        virtual const std::array<CascadeData, AXE_SHADOW_CASCADES>& GetCascades() const = 0;

        // ── SHADOW_HWPCF_V1 ─────────────────────────────────────────────────
        //
        // O MESMO texture array precisa ser lido de DUAS maneiras no mesmo
        // frame, e o modo de comparacao e parametro da TEXTURA, nao do shader:
        //
        //   Compare — a unidade de textura compara a profundidade gravada com
        //             a de referencia E JA FILTRA BILINEARMENTE o resultado
        //             0/1 dos quatro texels vizinhos. E o PCF de hardware:
        //             cada tap sai como 0..1 em vez de 0 ou 1, sem custo.
        //
        //   Raw     — profundidade crua, sem comparacao e sem filtro. A busca
        //             de bloqueador do PCSS PRECISA disto: ela quer saber a
        //             QUE DISTANCIA esta quem bloqueia, e a media de dois
        //             valores de profundidade nao corresponde a superficie
        //             nenhuma.
        //
        // A saida sao dois SAMPLER OBJECTS (glGenSamplers) apontando para a
        // mesma textura em duas unidades. Sem eles seria preciso trocar o
        // parametro da textura no meio do frame, ou duplicar o shadow map.
        //
        // 0 = backend nao fornece; nesse caso o chamador so binda a textura.
        virtual uint32_t GetCompareSamplerID() const { return 0; }
        virtual uint32_t GetRawSamplerID()     const { return 0; }

        static std::shared_ptr<CascadedShadowPass> Create();
    };

} // namespace axe