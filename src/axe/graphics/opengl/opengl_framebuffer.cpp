#include "opengl_framebuffer.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>

namespace axe
{
    static GLenum ToGLInternalFormat(FramebufferTextureFormat fmt)
    {
        switch (fmt)
        {
        case FramebufferTextureFormat::RGBA8:            return GL_RGBA8;
        case FramebufferTextureFormat::RGBA16F:          return GL_RGBA16F;
        case FramebufferTextureFormat::RGB16F:           return GL_RGB16F;
        case FramebufferTextureFormat::R8:               return GL_R8;
        case FramebufferTextureFormat::DEPTH32F:         return GL_DEPTH_COMPONENT32F;
        case FramebufferTextureFormat::DEPTH24STENCIL8:  return GL_DEPTH24_STENCIL8;
        default: return GL_RGBA8;
        }
    }

    static bool IsDepthFormat(FramebufferTextureFormat fmt)
    {
        return fmt == FramebufferTextureFormat::DEPTH24STENCIL8 ||
            fmt == FramebufferTextureFormat::DEPTH32F;
    }

    OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec)
    {
        AXE_CORE_INFO("OpenGLFramebuffer CONSTRUCTOR: {}x{} HDR={}",
            m_Specification.Width, m_Specification.Height,
            m_Specification.HDR);

        // Compatibilidade — se não tem attachments definidos, usa o comportamento antigo
        if (m_Specification.Attachments.empty())
        {
            if (spec.HDR)
                m_Specification.Attachments = {
                    FramebufferTextureFormat::RGBA16F,
                    FramebufferTextureFormat::DEPTH24STENCIL8
            };
            else
                m_Specification.Attachments = {
                    FramebufferTextureFormat::RGBA8,
                    FramebufferTextureFormat::DEPTH24STENCIL8
            };
        }
        Invalidate();
    }

    OpenGLFramebuffer::~OpenGLFramebuffer()
    {
        glDeleteFramebuffers(1, &m_RendererID);
        if (!m_ColorAttachments.empty())
            glDeleteTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());
        if (m_DepthIsTexture)
            glDeleteTextures(1, &m_DepthAttachment);
        else
            glDeleteRenderbuffers(1, &m_DepthAttachment);
    }

    void OpenGLFramebuffer::Invalidate()
    {
        AXE_CORE_INFO("Invalidate called: FBO={} size={}x{}",
            m_RendererID, m_Specification.Width, m_Specification.Height);

        if (m_RendererID)
        {
            glDeleteFramebuffers(1, &m_RendererID);
            if (!m_ColorAttachments.empty())
                glDeleteTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());
            if (m_DepthIsTexture)
                glDeleteTextures(1, &m_DepthAttachment);
            else if (m_DepthAttachment)
                glDeleteRenderbuffers(1, &m_DepthAttachment);
            m_ColorAttachments.clear();
            m_DepthAttachment = 0;
        }

        glCreateFramebuffers(1, &m_RendererID);

        // Separa color de depth
        std::vector<FramebufferTextureSpec> colorSpecs;
        FramebufferTextureSpec depthSpec;

        for (auto& spec : m_Specification.Attachments)
        {
            if (IsDepthFormat(spec.Format)) depthSpec = spec;
            else colorSpecs.push_back(spec);
        }

        // Color attachments
        m_ColorAttachments.resize(colorSpecs.size());
        glCreateTextures(GL_TEXTURE_2D, (GLsizei)colorSpecs.size(), m_ColorAttachments.data());

        for (uint32_t i = 0; i < colorSpecs.size(); i++)
        {
            GLenum internalFmt = ToGLInternalFormat(colorSpecs[i].Format);
            glTextureStorage2D(m_ColorAttachments[i], 1, internalFmt,
                m_Specification.Width, m_Specification.Height);
            glTextureParameteri(m_ColorAttachments[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(m_ColorAttachments[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(m_ColorAttachments[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(m_ColorAttachments[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glNamedFramebufferTexture(m_RendererID, GL_COLOR_ATTACHMENT0 + i,
                m_ColorAttachments[i], 0);
        }

        // Draw buffers
        if (!m_ColorAttachments.empty())
        {
            std::vector<GLenum> drawBuffers(m_ColorAttachments.size());
            for (uint32_t i = 0; i < drawBuffers.size(); i++)
                drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
            glNamedFramebufferDrawBuffers(m_RendererID,
                (GLsizei)drawBuffers.size(), drawBuffers.data());
        }

        // Depth
        if (depthSpec.Format != FramebufferTextureFormat::None)
        {
            if (depthSpec.Format == FramebufferTextureFormat::DEPTH32F)
            {
                // Depth como textura — necessário para SSAO
                m_DepthIsTexture = true;
                glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthAttachment);
                glTextureStorage2D(m_DepthAttachment, 1, GL_DEPTH_COMPONENT32F,
                    m_Specification.Width, m_Specification.Height);
                glTextureParameteri(m_DepthAttachment, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTextureParameteri(m_DepthAttachment, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTextureParameteri(m_DepthAttachment, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                glTextureParameteri(m_DepthAttachment, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                glNamedFramebufferTexture(m_RendererID, GL_DEPTH_ATTACHMENT,
                    m_DepthAttachment, 0);
            }
            else
            {
                // Depth como renderbuffer — mais rápido quando não precisa sampler
                m_DepthIsTexture = false;
                glCreateRenderbuffers(1, &m_DepthAttachment);
                glNamedRenderbufferStorage(m_DepthAttachment, GL_DEPTH24_STENCIL8,
                    m_Specification.Width, m_Specification.Height);
                glNamedFramebufferRenderbuffer(m_RendererID, GL_DEPTH_STENCIL_ATTACHMENT,
                    GL_RENDERBUFFER, m_DepthAttachment);
            }
        }

        GLenum status = glCheckNamedFramebufferStatus(m_RendererID, GL_FRAMEBUFFER);
        AXE_CORE_INFO("Framebuffer FBO={} status={}", m_RendererID,
            status == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE");
    }

    void OpenGLFramebuffer::Bind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glViewport(0, 0, m_Specification.Width, m_Specification.Height);
    }

    void OpenGLFramebuffer::Unbind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0 || width > 8192 || height > 8192) return;

        // ✅ Só recria se o tamanho realmente mudou
        if (width == m_Specification.Width && height == m_Specification.Height) return;

        m_Specification.Width = width;
        m_Specification.Height = height;
        Invalidate();
    }

    std::uint32_t OpenGLFramebuffer::GetColorAttachmentRendererID() const
    {
        AXE_CORE_ASSERT(!m_ColorAttachments.empty(), "Sem color attachments!");
        return m_ColorAttachments[0];
    }

    std::uint32_t OpenGLFramebuffer::GetColorAttachmentRendererID(uint32_t index) const
    {
        AXE_CORE_ASSERT(index < m_ColorAttachments.size(), "Index fora do range!");
        return m_ColorAttachments[index];
    }

    std::uint32_t OpenGLFramebuffer::ReadPixel(std::uint32_t x, std::uint32_t y) const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        std::uint8_t pixel[4] = {};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return (uint32_t)pixel[0] | (uint32_t)pixel[1] << 8 |
            (uint32_t)pixel[2] << 16 | (uint32_t)pixel[3] << 24;
    }
}