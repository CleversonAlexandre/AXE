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

        static std::shared_ptr<CascadedShadowPass> Create();
    };

} // namespace axe