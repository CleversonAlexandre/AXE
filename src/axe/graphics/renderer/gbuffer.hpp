#pragma once
#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/framebuffer.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    // G-Buffer contém 4 color attachments + depth como textura:
    // Attachment 0 — Posição   (RGB16F) — world space
    // Attachment 1 — Normal    (RGB16F) — world space
    // Attachment 2 — Albedo    (RGBA8)
    // Attachment 3 — PBR       (RGBA8)  — roughness, ao
    // Attachment 4 — Emissive  (RGB16F) — permite valores >1.0 (HDR/bloom)
    // Depth        — DEPTH32F  — acessível como textura para SSAO

    class AXE_API GBuffer
    {
    public:
        void Initialize(uint32_t width, uint32_t height);
        void Resize(uint32_t width, uint32_t height);

        void Bind();
        void Unbind();

        bool IsInitialized() const { return m_Initialized; }

        uint32_t GetPositionID() const; // attachment 0
        uint32_t GetNormalID()   const; // attachment 1
        uint32_t GetAlbedoID()   const; // attachment 2 (rgb=albedo, a=metallic)
        uint32_t GetPBRID()      const; // attachment 3 (r=roughness, g=ao)
        uint32_t GetDepthID()    const; // depth texture

        uint32_t GetWidth()  const;
        uint32_t GetHeight() const;        
        uint32_t GetEmissiveID() const; // attachment 4

        uint32_t GetFramebufferID() const;

    private:
        std::shared_ptr<Framebuffer> m_Framebuffer;
        bool m_Initialized = false;
    };
}