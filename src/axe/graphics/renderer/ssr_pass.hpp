#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    class GBuffer;

    struct SSRSettings
    {
        bool  Enabled = false;
        float MaxDistance = 20.0f;  // distância máxima do raio (view space)
        int   MaxSteps = 40;     // passos do ray march linear
        int   BinaryRefine = 5;      // refinamento binário no ponto de hit
        float Thickness = 0.5f;   // espessura do teste de profundidade
        float MaxRoughness = 0.6f;   // superfícies mais ásperas que isso não refletem
        float Intensity = 1.0f;   // intensidade da reflexão
        float EdgeFade = 0.1f;   // fade nas bordas da tela (0-0.5)
    };

    // Interface abstrata — sem GL. Impl em opengl/opengl_ssr_pass.
    class AXE_API SSRPass
    {
    public:
        virtual ~SSRPass() = default;

        virtual void Initialize(uint32_t width, uint32_t height) = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;
        virtual bool IsInitialized() const = 0;

        // Lê GBuffer + sceneColor, retorna a textura com reflexões aplicadas.
        virtual uint32_t Execute(
            const GBuffer& gbuffer,
            uint32_t           sceneColorID,
            const glm::mat4& projection,
            const glm::mat4& view,
            const SSRSettings& settings,
            uint32_t           width,
            uint32_t           height) = 0;

        static std::shared_ptr<SSRPass> Create();
    };

} // namespace axe