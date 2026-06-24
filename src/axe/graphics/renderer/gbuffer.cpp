#include "gbuffer.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    void GBuffer::Initialize(uint32_t width, uint32_t height)
    {
        FramebufferSpecification spec;
        spec.Width = width;
        spec.Height = height;
        spec.Attachments = {
            FramebufferTextureFormat::RGB16F,   // 0 — Posição  (view space)
            FramebufferTextureFormat::RGB16F,   // 1 — Normal   (view space)
            FramebufferTextureFormat::RGBA8,    // 2 — Albedo + Metallic
            FramebufferTextureFormat::RGBA8,    // 3 — PBR (roughness, ao)
            FramebufferTextureFormat::RGB16F,   // 4 — Emissive (HDR)
            FramebufferTextureFormat::DEPTH32F, // Depth
        };
        m_Framebuffer = Framebuffer::Create(spec);
        m_Initialized = true;
        //AXE_CORE_INFO("GBuffer initialized ({}x{})", width, height);
    }

    uint32_t GBuffer::GetFramebufferID() const
    {
        return m_Framebuffer ? m_Framebuffer->GetRendererID() : 0;
    }

    void GBuffer::Resize(uint32_t width, uint32_t height)
    {
        if (!m_Framebuffer) return;
        auto& spec = m_Framebuffer->GetSpecification();
        if (spec.Width == width && spec.Height == height) return;
        m_Framebuffer->Resize(width, height);
    }

    void GBuffer::Bind() { if (m_Framebuffer) m_Framebuffer->Bind(); }
    void GBuffer::Unbind() { if (m_Framebuffer) m_Framebuffer->Unbind(); }

    uint32_t GBuffer::GetPositionID() const { return m_Framebuffer->GetColorAttachmentRendererID(0); }
    uint32_t GBuffer::GetNormalID()   const { return m_Framebuffer->GetColorAttachmentRendererID(1); }
    uint32_t GBuffer::GetAlbedoID()   const { return m_Framebuffer->GetColorAttachmentRendererID(2); }
    uint32_t GBuffer::GetDepthID()    const { return m_Framebuffer->GetDepthAttachmentRendererID(); }
    uint32_t GBuffer::GetPBRID() const { return m_Framebuffer->GetColorAttachmentRendererID(3); }
    uint32_t GBuffer::GetEmissiveID() const { return m_Framebuffer->GetColorAttachmentRendererID(4); }

    uint32_t GBuffer::GetWidth()  const { return m_Framebuffer ? m_Framebuffer->GetSpecification().Width : 0; }
    uint32_t GBuffer::GetHeight() const { return m_Framebuffer ? m_Framebuffer->GetSpecification().Height : 0; }
}