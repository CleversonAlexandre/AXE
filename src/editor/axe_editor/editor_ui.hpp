#pragma once
#include "hierarchy_window.hpp"
#include "viewport_window.hpp"
#include "inspector_window.hpp"
#include "asset_browser.hpp"
#include "editor_context.hpp"
#include "editor/axe_editor/material/material_editor_window.hpp"
#include "editor/axe_editor/particles/particle_editor_window.hpp"
#include "animation/anim_graph_window.hpp"
#include "animation/anim_clip_window.hpp"
#include "editor/axe_editor/input/input_settings_window.hpp"
#include "editor/axe_editor/script/script_graph_window.hpp"
#include <imgui.h>
#include <functional>

namespace axe
{
    class ViewportRenderer;

    class EditorUI
    {
    public:
        void SetViewportRenderer(ViewportRenderer* renderer);
        void SetContext(EditorContext* context);
        void Draw();

        ViewportWindow* GetViewport() { return &m_ViewportWindow; }
        AssetBrowser* GetAssetBrowser() { return &m_AssetBowserWindow; }
        MaterialEditorWindow m_MaterialEditorWindow;
        ParticleEditorWindow m_ParticleEditorWindow;
        ScriptGraphWindow    m_ScriptGraphWindow;
        AnimGraphWindow      m_AnimGraphWindow;
        AnimClipWindow       m_AnimClipWindow;
        InputSettingsWindow  m_InputSettingsWindow;

        // Callbacks conectados pelo EditorLayer
        std::function<void()> OnNewScene;
        std::function<void(const std::string&)> OnOpenScene;
        std::function<void(const std::string&)> OnSaveScene;
        std::function<void()> OnSaveProject;
        std::function<void(const std::string&)> OnOpenProject;
        std::function<void()> OnDrawEnvironment;
        std::function<void()> OnUndo;
        std::function<void()> OnRedo;
        std::function<bool()> OnCanUndo;
        std::function<bool()> OnCanRedo;
        std::function<bool()> IsPlaying; // retorna true se estiver em Play ou Pause

        HierarchyWindow* GetHierarchy() { return &m_HierarchyWindow; }
        InspectorWindow m_InspectorWindow;
        AssetBrowser    m_AssetBowserWindow;
    private:
        void BeginDockspace();
        void EndDockspace();
        void DrawMenuBar();
        void BuildDefaultLayout(ImGuiID dockspaceId);

        HierarchyWindow m_HierarchyWindow;

        ViewportWindow  m_ViewportWindow;

        

        bool m_ShowHierarchy = true;
        bool m_ShowViewport = true;
        bool m_ShowInspector = true;
        bool m_ShowAssetBrowser = true;
        bool m_ShowEnvironment = false;
        bool m_ShowGameMode = false;

        ViewportRenderer* m_ViewportRenderer = nullptr;
    };
}