#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include <entt/entt.hpp>
#include <imgui.h>
#include <string>

namespace axe
{

    class AXE_API HierarchyWindow
    {
    public:
        HierarchyWindow();
        void SetContext(EditorContext* context);
        void Draw();

    private:
        // Renderiza um nó e seus filhos recursivamente
        void DrawNode(entt::entity entity);
        void DrawContextMenuEmpty();
        void DrawContextMenuObject(entt::entity entity);
        void HandleKeyboardShortcuts();

        // Criação
        void CreateObject(const std::string& name, const std::string& primitiveUUID);
        void CreateFolder();
        void CreateLight();

        // Ações
        void DeleteSelected();
        void DuplicateSelected();
        void StartRename(entt::entity entity);

        // Rename inline
        bool          m_Renaming = false;
        entt::entity  m_RenamingEntity = entt::null;
        char          m_RenameBuffer[256] = {};

        EditorContext* m_Context = nullptr;
    };

} // namespace axe