#pragma once
#include "axe/core/types.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_component.hpp"
#include <imgui_node_editor.h>
#include <imgui.h>
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
        void Shutdown();

        void OpenForEntity(entt::entity entity, ScriptComponent* comp);
        void Close();
        bool IsOpen() const { return m_IsOpen; }

        // Textura do viewport principal para o 3D preview
        void SetViewportTexture(ImTextureID tex) { m_ViewportTexture = tex; }

    private:
        void DrawGraph();
        void DrawNode(ScriptNode* node);
        void DrawNodeList();
        void DrawComponentsPanel();
        void DrawDetailsPanel();
        void DrawContextMenu();
        void DrawConsole();
        void SpawnAt(const char* type, ImVec2 pos);
        void CompileScript();

        ed::EditorContext* m_EdCtx = nullptr;
        ScriptGraph* m_Graph = nullptr;
        ScriptComponent* m_Component = nullptr;
        entt::entity       m_Entity = entt::null;

        // Textura do viewport para o 3D preview
        ImTextureID m_ViewportTexture = nullptr;

        bool m_IsOpen = false;
        bool m_FirstFrame = true;

        bool   m_ShowCtx = false;
        ImVec2 m_CtxPos = {};
        char   m_CtxBuf[128] = {};
        char   m_SearchBuf[128] = {};
        char   m_CompSearchBuf[128] = {};

        bool m_CatOpen[5] = { true, true, true, true, true };
        bool m_CtxOpen[5] = { true, true, true, true, true };

        std::string m_Msg;
        bool        m_MsgOk = false;
        float       m_MsgTimer = 0.0f;

        std::vector<std::string> m_ConsoleLines;
    };

} // namespace axe