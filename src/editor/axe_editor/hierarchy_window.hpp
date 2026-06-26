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

        // Criação
        void CreateObject(const std::string& name, const std::string& primitiveUUID);
    private:
        void DrawNode(entt::entity entity);
        void DrawContextMenuEmpty();
        void DrawContextMenuObject(entt::entity entity);
        void HandleKeyboardShortcuts();

        void CreateFolder();
        void CreateLight();

        void DeleteSelected();
        void DuplicateSelected();
        void StartRename(entt::entity entity);

        bool          m_Renaming = false;
        entt::entity  m_RenamingEntity = entt::null;
        char          m_RenameBuffer[256] = {};

        EditorContext* m_Context = nullptr;
        CommandHistory* m_History = nullptr;
    };

} // namespace axe