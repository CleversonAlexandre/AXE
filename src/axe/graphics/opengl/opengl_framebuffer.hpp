#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/framebuffer.hpp"
#include <vector>

namespace axe
{
    class AXE_API OpenGLFramebuffer final : public Framebuffer
    {
    public:
        OpenGLFramebuffer(const FramebufferSpecification& spec);
        ~OpenGLFramebuffer() override;

        void Bind()   override;
        void Unbind() override;
        void Resize(std::uint32_t width, std::uint32_t height) override;

        std::uint32_t GetColorAttachmentRendererID()              const override;
        std::uint32_t GetColorAttachmentRendererID(uint32_t index) const override;
        std::uint32_t GetDepthAttachmentRendererID()               const override { return m_DepthAttachment; }

        std::uint32_t ReadPixel(std::uint32_t x, std::uint32_t y) const override;
        const FramebufferSpecification& GetSpecification()         const override { return m_Specification; }
        uint32_t GetRendererID() const override { return m_RendererID; }
    private:
        void Invalidate();

        FramebufferSpecification      m_Specification;
        unsigned int                  m_RendererID = 0;
        std::vector<unsigned int>     m_ColorAttachments;
        unsigned int                  m_DepthAttachment = 0;
        bool                          m_DepthIsTexture = false;
    };
}