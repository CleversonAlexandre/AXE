#pragma once
#include "axe/core/types.hpp"
#include "axe/renderer/volumetric_fog_pass.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    struct PostProcessSettings
    {
        // Tone Mapping
        float Exposure = 1.0f;
        int   ToneMapMode = 1;   // 0 = Reinhard, 1 = ACES

        // Bloom
        bool  BloomEnabled = false;
        float BloomThreshold = 0.7f;
        float BloomIntensity = 0.5f;
        int   BloomBlurPasses = 5;

        // Volumetric Fog
        VolumetricFogSettings Fog;
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