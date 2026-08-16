#pragma once
#include "hierarchy_window.hpp"
#include "viewport_window.hpp"
#include "inspector_window.hpp"
#include "asset_browser.hpp"
#include "editor_context.hpp"
#include "editor/axe_editor/material/material_editor_window.hpp"
#include "editor/axe_editor/particles/particle_editor_window.hpp"
#include "editor/axe_editor/audio/sound_cue_editor_window.hpp"
#include "animation/anim_graph_window.hpp"
#include "rig/control_rig_window.hpp"
#include "animation/anim_clip_window.hpp"
#include "editor/axe_editor/input/input_settings_window.hpp"
#include "editor/axe_editor/asset/asset_report_window.hpp"   // B3.2
#include "editor/axe_editor/script/script_graph_window.hpp"
#include <imgui.h>
#include <functional>
#include <filesystem>   // m_LoadedInputProjectRoot

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
        SoundCueEditorWindow m_SoundCueEditorWindow;

        // Mixer de buses. Janela pequena demais para arquivo proprio, e o
        // conteudo e cinco sliders — separar so criaria indireção.
        bool m_ShowAudioMixer = false;
        void DrawAudioMixer();
        ScriptGraphWindow    m_ScriptGraphWindow;
        AnimGraphWindow      m_AnimGraphWindow;
        ControlRigWindow     m_ControlRigWindow;
        AnimClipWindow       m_AnimClipWindow;
        InputSettingsWindow  m_InputSettingsWindow;
        AssetReportWindow    m_AssetReportWindow;   // B3.2

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

        // Qual projeto a janela de Input Settings ja carregou.
        //
        // MEMBRO, e nao `static` local: o EditorApp::RequestReopenProject
        // DESTROI o EditorLayer — e com ele este EditorUI e a janela de Input —
        // e cria outro. Um static sobrevive a essa troca; entao, ao reabrir o
        // MESMO projeto, a comparacao dava "igual", o SetProjectPath nunca era
        // chamado na janela NOVA, e ela abria com o caminho vazio: "Nenhum
        // projeto carregado", sem poder criar nem salvar mapeamentos. Como
        // membro, o estado morre junto com a janela a que pertence.
        std::filesystem::path m_LoadedInputProjectRoot;

        // Mesma armadilha, mesmo motivo: o layout padrao era guardado num
        // static, e o EditorUI recriado no reopen herdava "ja construi" de um
        // objeto que nao existe mais.
        bool m_DefaultLayoutBuilt = false;

        bool m_ShowHierarchy = true;
        bool m_ShowViewport = true;
        bool m_ShowInspector = true;
        bool m_ShowAssetBrowser = true;
        bool m_ShowEnvironment = false;
        bool m_ShowGameMode = false;

        ViewportRenderer* m_ViewportRenderer = nullptr;
    };
}