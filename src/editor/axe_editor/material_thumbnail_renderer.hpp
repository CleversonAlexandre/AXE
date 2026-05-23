#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/material/material.hpp"
#include "axe/material/material_asset.hpp"
#include <unordered_map>
#include <memory>
#include <string>
#include <filesystem>

namespace axe
{
    class MaterialThumbnailRenderer
    {
    public:
        MaterialThumbnailRenderer() = default;
        ~MaterialThumbnailRenderer() = default;

        void Initialize();

        // Registra um material — só carrega do disco se não estiver no cache
        void Register(const std::string& uuid,
            const std::filesystem::path& filePath);

        // Retorna o TextureID do thumbnail — 0 se ainda não renderizado
        uint32_t GetThumbnail(const std::string& uuid);

        // Invalida o cache — força re-render (chamar após compilar shader)
        void Invalidate(const std::string& uuid);

        // Renderiza thumbnails pendentes — chamar no OnRender() antes do ImGui
        void RenderPending();

    private:
        void RenderThumbnail(const std::string& uuid);

        struct ThumbnailEntry
        {
            std::shared_ptr<Framebuffer> Framebuffer;
            std::shared_ptr<Material>    Material;
            bool                         Dirty = true;
            bool                         Rendered = false;
        };

        std::unique_ptr<ViewportRenderer>  m_Renderer;
        std::unique_ptr<Scene>             m_Scene;
        std::unique_ptr<SceneEnvironment>  m_Environment;
        std::shared_ptr<Material>          m_DefaultMaterial;
        entt::entity                       m_SphereEntity = entt::null;

        std::unordered_map<std::string, ThumbnailEntry> m_Cache;

        static constexpr uint32_t k_ThumbnailSize = 128;
    };

} // namespace axe