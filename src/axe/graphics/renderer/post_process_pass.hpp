#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    struct PostProcessSettings
    {
        // Tone Mapping
        float Exposure = 1.0f;

        // Bloom
        bool  BloomEnabled = false;
        float BloomThreshold = 1.0f;   // brilho mínimo para extrair
        float BloomIntensity = 0.5f;
        int   BloomBlurPasses = 5;
    };

    class AXE_API PostProcessPass
    {
    public:
        virtual ~PostProcessPass() = default;

        virtual void Initialize(uint32_t width, uint32_t height) = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // Recebe o color attachment HDR, escreve no framebuffer atual (0 = tela)
        virtual void Execute(uint32_t hdrColorID,
            const PostProcessSettings& settings) = 0;

        virtual bool IsInitialized() const = 0;

        static std::shared_ptr<PostProcessPass> Create();
    };
}