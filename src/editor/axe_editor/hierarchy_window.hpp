#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include "axe/core/command_history.hpp"
#include <entt/entt.hpp>
#include <imgui.h>
#include <string>

namespace axe
{

    class HierarchyWindow
    {
    public:
        HierarchyWindow();
        void SetContext(EditorContext* context);
        void SetCommandHistory(CommandHistory* history) { m_History = history; }
        void Draw();
        void CreatePostProcess();
        void CreatePointLight();
        void CreateCamera();
        void CreateSpline();

        // Criação
        void CreateObject(const std::string& name, const std::string& primitiveUUID);

        // ── VIEWPORT_DELETE_V1 ───────────────────────────────────────────────
        //
        //  Publico para que a tecla Delete no VIEWPORT chame exatamente este
        //  metodo, e nao uma segunda implementacao de "deletar".
        //
        //  Nao e detalhe: este caminho carrega o snapshot para o undo, a
        //  recursao nos filhos e, desde o ENTITY_DETACH_V1, a religacao do pai
        //  ao desfazer. Uma copia no viewport comecaria correta e divergiria
        //  no primeiro conserto — que e exatamente como os dois caminhos de
        //  criar o Post Process Volume divergiram (ver PPVOLUME_ONE_PATH_V1).
        void DeleteSelected();

    private:
        void DrawNode(entt::entity entity);
        void DrawContextMenuEmpty();
        void DrawContextMenuObject(entt::entity entity);
        void HandleKeyboardShortcuts();

        void CreateFolder();
        void CreateLight();
        void CreateSkyLight();   // SKY_LIGHT_V1

        void DuplicateSelected();
        void StartRename(entt::entity entity);

        bool          m_Renaming = false;
        entt::entity  m_RenamingEntity = entt::null;
        char          m_RenameBuffer[256] = {};

        EditorContext* m_Context = nullptr;
        CommandHistory* m_History = nullptr;
    };

} // namespace axe