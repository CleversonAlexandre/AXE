#pragma once
#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/framebuffer.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    // G-Buffer contém 3 color attachments + depth como textura:
    // Attachment 0 — Posição   (RGB16F) — world space
    // Attachment 1 — Normal    (RGB16F) — world space
    // Attachment 2 — Albedo    (RGBA8)
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
        uint32_t GetAlbedoID()   const; // attachment 2
        uint32_t GetDepthID()    const; // depth texture

        uint32_t GetWidth()  const;
        uint32_t GetHeight() const;

    private:
        std::shared_ptr<Framebuffer> m_Framebuffer;
        bool m_Initialized = false;
    };
}