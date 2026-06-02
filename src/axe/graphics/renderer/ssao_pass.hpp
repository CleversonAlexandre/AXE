#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "gbuffer.hpp"
#include <memory>
#include <vector>

namespace axe
{
    struct SSAOSettings
    {
        int   KernelSize = 64;
        float Radius = 0.5f;
        float Bias = 0.025f;
        float Power = 2.0f;
        bool  Enabled = false;
        bool  Debug = false;  // mostra textura de oclusão pura
    };

    class AXE_API SSAOPass
    {
    public:
        virtual ~SSAOPass() = default;

        virtual void Initialize(uint32_t width, uint32_t height) = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // Recebe o G-Buffer, escreve textura de oclusão
        virtual void Execute(const GBuffer& gbuffer,
            const glm::mat4& projection,
            const glm::mat4& view,
            const SSAOSettings& settings) = 0;

        virtual uint32_t GetOcclusionTextureID() const = 0;
        virtual bool     IsInitialized()         const = 0;

        static std::shared_ptr<SSAOPass> Create();
    };
}