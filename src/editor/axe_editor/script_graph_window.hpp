#pragma once
#include "axe/core/types.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_component.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include "inspector_window.hpp"
#include <imgui_node_editor.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <entt/entt.hpp>

namespace ed = ax::NodeEditor;

namespace axe
{
    class AXE_API ScriptGraphWindow
    {
    public:
        ScriptGraphWindow();
        ~ScriptGraphWindow();

        void Initialize();
        void Draw();
        void RenderPreview();
        void Shutdown();

        void OpenForEntity(entt::entity entity, ScriptComponent* comp,
            entt::registry* registry);
        void SetInspectorWindow(InspectorWindow* insp) { m_InspectorWindow = insp; }
        void OpenForAsset(std::shared_ptr<ScriptAsset> asset);
        void Close();
        bool IsOpen() const { return m_IsOpen; }

    private:
        void DrawPreviewWindow();
        void DrawGraphWindow();
        void DrawNodeGraph();        // lógica do node editor — só chamado quando janela visível
        void DrawSceneGraphWindow();
        void DrawDetailsWindow();
        void DrawConsoleWindow();

        void DrawNode(ScriptNode* node);
        void HandlePreviewInput();
        void DrawPreviewGizmo();       // gizmo sobreposto na preview
        void DrawScriptDetails();      // conteúdo do painel Details quando objeto selecionado
        void CompileScript();
        void InitPreviewScene();
        void SyncMeshFromSource();
        void SyncMeshFromAsset();
        void SyncComponentsToPreview();  // espelha ScriptComponentDef → componentes reais no preview
        void SaveNodePositions();           // salva posições do editor de volta nos ScriptNodes

        // Node editor
        ed::EditorContext* m_EdCtx = nullptr;
        ScriptGraph* m_Graph = nullptr;
        ScriptComponent* m_Component = nullptr;
        entt::entity       m_Entity = entt::null;
        entt::registry* m_SourceRegistry = nullptr;
        InspectorWindow* m_InspectorWindow = nullptr;  // para DrawMaterialGraphParams
        std::shared_ptr<ScriptAsset> m_ScriptAsset;

        // Preview
        std::unique_ptr<ViewportRenderer>  m_PreviewRenderer;
        std::shared_ptr<Framebuffer>       m_PreviewFramebuffer;
        std::unique_ptr<Scene>             m_PreviewScene;
        std::unique_ptr<SceneEnvironment>  m_PreviewEnvironment;
        entt::entity                       m_PreviewEntity = entt::null;
        ImVec2                             m_PreviewSize = { 0, 0 };
        bool                               m_PreviewHovered = false;
        ImVec2                             m_PreviewMouseDelta = {};
        bool                               m_PreviewEntitySelected = true;  // objeto sempre selecionado no preview
        ImVec2                             m_PreviewBoundsMin = {};
        ImVec2                             m_PreviewBoundsMax = {};
        ImGuizmo::OPERATION                m_GizmoOp = ImGuizmo::TRANSLATE;

        // Context menu
        ImVec2 m_CtxCanvasPos = {};   // posição no canvas onde o clique aconteceu
        char   m_CtxBuf[128] = {};
        bool   m_CtxOpen[5] = { true, true, true, true, true };

        char   m_CompSearchBuf[128] = {};
        int    m_SelectedCompIndex = -1; // componente selecionado no Scene Graph

        bool m_IsOpen = false;
        bool m_FirstFrame = true;
        bool m_LayoutBuilt = false;

        std::string m_Msg;
        bool        m_MsgOk = false;
        float       m_MsgTimer = 0.0f;

        std::vector<std::string> m_ConsoleLines;
    };

} // namespace axe