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

        // ── VIEWPORT_RESIZE_V1 ───────────────────────────────────────────────
        //
        // Filtro dos COLOR attachments. O padrao continua NEAREST, que e o que
        // todo passe interno precisa: G-Buffer, SSAO, SSR, TAA e picking leem
        // texel a texel, e interpolar ali produz dado errado (uma normal
        // interpolada entre dois objetos nao e a normal de ninguem).
        //
        // LINEAR so faz sentido no alvo que e APRESENTADO como imagem — o
        // framebuffer do Viewport e o do Material Preview. Enquanto se arrasta
        // a borda da janela, o ImGui desenha a textura do frame anterior no
        // tamanho NOVO: com NEAREST isso aparece como escadinha grossa; com
        // LINEAR, como um leve borrao que some no frame seguinte.
        //
        // Default false de proposito: nenhum framebuffer existente muda de
        // comportamento por causa deste campo.
        bool LinearFilter = false;
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