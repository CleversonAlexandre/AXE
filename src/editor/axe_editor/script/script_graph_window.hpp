#include "axe/core/command_history.hpp"
#pragma once
#include "axe/core/types.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_component.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include "editor/axe_editor/inspector_window.hpp"
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

        // ── Constantes compartilhadas ─────────────────────────────────────────────
        const float ICON_SZ = 20.0f;
        const float PIN_H = 22.0f;

        void Initialize();
        void Draw();
        void SetActiveScene(Scene* scene) { m_ActiveScene = scene; }

        // Undo/Redo — snapshot based
        void PushUndo(const std::string& actionName);  // call BEFORE making changes
        void CommitUndo(const std::string& actionName);   // call AFTER making changes
        void Undo();
        void Redo();
        bool CanUndo() const { return m_History.CanUndo(); }
        bool CanRedo() const { return m_History.CanRedo(); }
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
        void DrawMyBlueprintWindow();  // painel Variables / Events / Dispatchers
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
        ImVec2      m_CtxCanvasPos = {};
        ed::PinId   m_CtxPinId;
        ed::NodeId  m_CtxNodeId;
        char        m_CtxBuf[128] = {};
        std::string m_PendingNodeType;         // node a criar no próximo frame (dentro do Begin/End)
        ImVec2      m_PendingNodePos = {};
        std::string m_PendingNodeStrValue;
        // Variable drop Get/Set popup
        std::string m_VarDropName;
        ImVec2      m_VarDropPos = {};
        bool        m_VarDropPending = false;
        bool        m_InsideNodeEditorFrame = false;
        bool        m_SpringArmDragging = false;
        int         m_PendingVarType = 0;
        bool        m_VarDropIsCanvas = false;
        // Promote to Variable pending state
        ed::PinId          m_PendingPromotePinId = {};
        bool               m_PendingPromoteIsInput = false;
        int                m_PendingPromoteVarType = 0;
        ScriptPinType      m_PendingPromotePinType = ScriptPinType::Float;
        bool   m_CtxOpen[6] = { true, true, true, true, true, false };

        char   m_CompSearchBuf[128] = {};
        int    m_SelectedCompIndex = -1; // componente selecionado no Scene Graph

        bool m_IsOpen = false;
        bool m_FirstFrame = true;
        bool m_LayoutBuilt = false;

        std::string m_Msg;
        bool        m_MsgOk = false;
        float       m_MsgTimer = 0.0f;

        std::vector<std::string> m_ConsoleLines;

        // My Blueprint panel state
        char  m_NewVarName[64] = "NewVar";
        int   m_NewVarType = 0;  // index into ScriptVarType
        char  m_NewEvtName[64] = "OnMyEvent";
        int   m_SelectedVar = -1;
        int   m_RenamingVar = -1;
        char  m_RenameBuf[64] = {};
        bool  m_RenameJustStarted = false;
        int   m_DeleteVarIndex = -1;
        std::string m_DeleteVarName;
        bool  m_DeleteVarAlsoNodes = true;
        std::vector<ed::NodeId> m_PendingDeleteNodes;
        bool  m_CompCollapsed[32] = {};
        ImVec2      m_GraphWindowCenter = {};
        entt::entity m_CameraPreviewEntity = entt::null;  // mesh de câmera no preview 3D
        Scene* m_ActiveScene = nullptr;
        CommandHistory m_History;
        std::string    m_SnapshotBeforeAction;
        std::string    m_PendingUndoSnapshot;
        std::string    m_PendingRedoSnapshot;  // cena ativa do editor (para propagar mudanças em Play)
    };

} // namespace axe