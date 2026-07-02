#pragma once
#include "axe/core/types.hpp"
#include "axe/particles/particle_system_asset.hpp"
#include "axe/particles/particle_world.hpp"
#include "axe/scene/scene.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "editor/axe_editor/editor_context.hpp"

#include <imgui.h>
#include <entt/entt.hpp>
#include <memory>
#include <string>

namespace axe
{
    class ParticleEditorWindow
    {
    public:
        void Initialize();
        void SetContext(EditorContext* context) { m_Context = context; }

        void OpenAsset(std::shared_ptr<ParticleSystemAsset> asset);
        bool IsOpen()    const { return m_Open; }
        bool IsFocused() const { return m_IsAnyWindowFocused; }

        void UpdatePreview(float deltaTime);
        void RenderPreview();
        void Draw();
        void Save();
        void Restart();

    private:
        void InitializePreview();
        void SyncPreviewComponent();
        void HandlePreviewInput();
        void DrawEmitterListWindow();
        void DrawEmitterDetailsWindow();
        void DrawPreviewWindow();
        void DrawMaterialSlot(ParticleEmitterDef& emitter);

        EditorContext* m_Context = nullptr;
        std::shared_ptr<ParticleSystemAsset> m_Asset;
        bool m_Open = false;
        bool m_IsAnyWindowFocused = false;

        // Índice do emitter selecionado no painel esquerdo
        int m_SelectedEmitter = 0;

        // Preview
        std::unique_ptr<Scene>           m_PreviewScene;
        std::unique_ptr<ViewportRenderer> m_PreviewRenderer;
        std::shared_ptr<Framebuffer>     m_PreviewFramebuffer;
        entt::entity                     m_PreviewEntity = entt::null;
        ParticleWorld                    m_PreviewParticleWorld;

        ImVec2 m_PreviewSize{ 512, 512 };
        bool   m_PreviewHovered = false;
        bool   m_PreviewFocused = false;
    };

} // namespace axe