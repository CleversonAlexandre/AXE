#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    struct TAASettings
    {
        bool  Enabled = false;
        float BlendFactor = 0.1f;   // base: 0.05=suave, 0.15=responsivo

        // Blend adaptativo — emissivos e animações
        float EmissiveLumMin = 0.2f;  // lum abaixo disso = superfície escura (sem boost)
        float EmissiveLumMax = 1.2f;  // lum acima disso = emissivo cheio (blend máximo)
        float EmissiveBlendMax = 0.85f; // blend máximo em regiões emissivas
        float TemporalSensitivity = 4.0f; // multiplicador da diferença temporal

        bool  Sharpen = false;
        float SharpenAmount = 0.3f;
    };

    // Interface abstrata — sem GL.
    class AXE_API TAAPass
    {
    public:
        virtual ~TAAPass() = default;

        virtual void Initialize(uint32_t width, uint32_t height) = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;
        virtual bool IsInitialized() const = 0;

        // Resolve: lê hdrColorID + depth, escreve resultado no framebuffer ativo.
        // Retorna a textura TAA resultante (usada pelo post-process).
        virtual uint32_t Execute(
            uint32_t         hdrColorID,
            uint32_t         depthID,
            const glm::mat4& invViewProj,
            const glm::mat4& prevViewProj,
            const glm::vec2& jitter,
            const TAASettings& settings,
            uint32_t         width,
            uint32_t         height) = 0;

        // Chama todo frame antes do render — gera jitter e atualiza prevVP.
        virtual void BeginFrame(const glm::mat4& viewProj) = 0;

        // Retorna o offset de jitter pra aplicar na projection matrix.
        virtual glm::vec2 GetCurrentJitter() const = 0;
        virtual glm::mat4 GetPrevViewProj()  const = 0;

        static std::shared_ptr<TAAPass> Create();
    };

} // namespace axe