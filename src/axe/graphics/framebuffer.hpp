#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <cstdint>
#include <vector>

namespace axe
{
    enum class FramebufferTextureFormat
    {
        None = 0,
        // Color
        RGBA8,
        RGBA16F,
        RGB16F,
        R8,
        // Depth
        DEPTH24STENCIL8,
        DEPTH32F,
    };

    struct FramebufferTextureSpec
    {
        FramebufferTextureFormat Format = FramebufferTextureFormat::None;
        FramebufferTextureSpec() = default;
        FramebufferTextureSpec(FramebufferTextureFormat fmt) : Format(fmt) {}
    };

    struct AXE_API FramebufferSpecification
    {
        std::uint32_t Width = 1;
        std::uint32_t Height = 1;
        bool          HDR = false; // ← mantido para compatibilidade

        std::vector<FramebufferTextureSpec> Attachments; // ← novo
    };

    class AXE_API Framebuffer
    {
    public:
        virtual ~Framebuffer() = default;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;
        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

        // Compatibilidade — retorna o primeiro color attachment
        virtual std::uint32_t GetColorAttachmentRendererID() const = 0;

        // Múltiplos attachments
        virtual std::uint32_t GetColorAttachmentRendererID(uint32_t index) const = 0;
        virtual std::uint32_t GetDepthAttachmentRendererID()               const = 0;

        virtual const FramebufferSpecification& GetSpecification() const = 0;

        static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& spec);

        virtual std::uint32_t ReadPixel(std::uint32_t x, std::uint32_t y) const = 0;

        virtual uint32_t GetRendererID() const = 0;
    };
}