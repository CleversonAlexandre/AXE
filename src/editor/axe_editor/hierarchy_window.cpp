#include "hierarchy_window.hpp"
#include "axe/scene/scene_objects.hpp"
#include "axe/scene/components.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include <imgui.h>
#include <cstring>
#include "editor_icon_library.hpp"



namespace axe
{

    HierarchyWindow::HierarchyWindow() = default;

    void HierarchyWindow::SetContext(EditorContext* context)
    {
        m_Context = context;
    }

    void HierarchyWindow::Draw()
    {
        ImGui::Begin("Hierarchy");

        if (!m_Context || !m_Context->ActiveScene)
        {
            ImGui::TextDisabled("Sem cena ativa.");
            ImGui::End();
            return;
        }

        if (ImGui::IsWindowFocused())
            HandleKeyboardShortcuts();

        // Itera só as raízes — filhos são desenhados recursivamente
        auto roots = m_Context->ActiveScene->GetRootEntities();
        for (auto entity : roots)
            DrawNode(entity);

        // Clique no vazio → deseleciona e cancela rename
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
        {
            m_Context->ClearSelection();
            m_Renaming = false;
        }

        // Menu de contexto no vazio
        if (ImGui::BeginPopupContextWindow("##hierarchy_empty",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            DrawContextMenuEmpty();
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void HierarchyWindow::DrawNode(entt::entity entity)
    {
        auto& registry = m_Context->ActiveScene->GetRegistry();
        if (!registry.valid(entity)) return;

        auto* nameComp = registry.try_get<NameComponent>(entity);
        if (!nameComp) return;

        bool isFolder = registry.any_of<FolderComponent>(entity);
        bool isLight = registry.any_of<LightComponent>(entity);
        bool isSelected = (m_Context->SelectedEntity == entity);
        bool isRenaming = (m_Renaming && m_RenamingEntity == entity);

        auto* rel = registry.try_get<RelationshipComponent>(entity);
        bool hasChildren = rel && !rel->Children.empty();

        // Estado de abertura por entity
        static std::unordered_map<uint32_t, bool> s_OpenState;
        bool& isOpen = s_OpenState[(uint32_t)entity];

        // Cor para pastas
        if (isFolder)
        {
            auto& folder = registry.get<FolderComponent>(entity);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(folder.Color.x, folder.Color.y, folder.Color.z, folder.Color.w));
        }

        // Arrow manual para nós com filhos
        if (hasChildren)
        {
            std::string arrowID = "##arrow_" + std::to_string((uint32_t)entity);
            if (ImGui::ArrowButton(arrowID.c_str(), isOpen ? ImGuiDir_Down : ImGuiDir_Right))
                isOpen = !isOpen;
            ImGui::SameLine();
        }
        else
        {
            // Alinha com nós que têm arrow (22px)
            ImGui::Dummy(ImVec2(22.0f, 0.0f));
            ImGui::SameLine();
        }

        if (isFolder) ImGui::PopStyleColor();

        // Ícone
        auto& icons = EditorIconLibrary::Get();
        std::shared_ptr<Texture2D> icon;
        if (isFolder)     icon = icons.GetFolder();
        else if (isLight) icon = icons.GetDirectionalLight();
        else              icon = icons.GetMesh();

        if (icon && icon->IsLoaded())
        {
            ImGui::Image(
                (ImTextureID)(uintptr_t)icon->GetRendererID(),
                ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0)
            );
            ImGui::SameLine();
        }

        // Rename inline ou Selectable
        if (isRenaming)
        {
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SetKeyboardFocusHere();

            if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                nameComp->Name = m_RenameBuffer;
                m_Renaming = false;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                m_Renaming = false;
        }
        else
        {
            ImGui::PushID((uint32_t)entity);
            bool clicked = ImGui::Selectable(
                nameComp->Name.c_str(),
                isSelected,
                ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_DontClosePopups
            );
            ImGui::PopID();

            if (clicked)
            {
                m_Context->Select(entity);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    StartRename(entity);
            }

            // Drag source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                uint32_t id = (uint32_t)entity;
                ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &id, sizeof(uint32_t));
                ImGui::Text("Mover: %s", nameComp->Name.c_str());
                ImGui::EndDragDropSource();
            }

            // Drop target
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
                {
                    uint32_t childID = *(const uint32_t*)payload->Data;
                    entt::entity child = (entt::entity)childID;
                    if (child != entity)
                        m_Context->ActiveScene->SetParent(child, entity);
                }
                ImGui::EndDragDropTarget();
            }

            // Menu de contexto
            std::string popupID = "##ctx_" + std::to_string((uint32_t)entity);
            if (ImGui::BeginPopupContextItem(popupID.c_str()))
            {
                m_Context->Select(entity);
                DrawContextMenuObject(entity);
                ImGui::EndPopup();
            }
        }

        // Filhos
        if (isOpen && rel)
        {
            ImGui::Indent(20.0f);
            for (auto child : rel->Children)
                DrawNode(child);
            ImGui::Unindent(20.0f);
        }
    }

    void HierarchyWindow::DrawContextMenuEmpty()
    {
        if (ImGui::BeginMenu("Criar"))
        {
            if (ImGui::MenuItem("Objeto Vazio"))
                CreateObject("Objeto Vazio", "");

            if (ImGui::MenuItem("Pasta"))
                CreateFolder();

            ImGui::Separator();

            if (ImGui::BeginMenu("Primitivas"))
            {
                if (ImGui::MenuItem("Cubo"))     CreateObject("Cube", PrimitiveUUID::Cube);
                if (ImGui::MenuItem("Esfera"))   CreateObject("Sphere", PrimitiveUUID::Sphere);
                if (ImGui::MenuItem("Plano"))    CreateObject("Plane", PrimitiveUUID::Plane);
                if (ImGui::MenuItem("Cilindro")) CreateObject("Cylinder", PrimitiveUUID::Cylinder);
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Luz Direcional"))
                CreateLight();

            if (ImGui::MenuItem("Post Process Volume"))
                CreatePostProcess();

            ImGui::EndMenu();
        }
    }

    void HierarchyWindow::DrawContextMenuObject(entt::entity entity)
    {
        if (ImGui::MenuItem("Renomear", "F2"))
            StartRename(entity);

        if (ImGui::MenuItem("Duplicar", "Ctrl+D"))
            DuplicateSelected();

        // Opção de remover do pai
        auto& registry = m_Context->ActiveScene->GetRegistry();
        auto* rel = registry.try_get<RelationshipComponent>(entity);
        if (rel && rel->Parent != entt::null)
        {
            if (ImGui::MenuItem("Remover do grupo"))
                m_Context->ActiveScene->RemoveParent(entity);
        }

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        if (ImGui::MenuItem("Deletar", "Delete"))
            DeleteSelected();
        ImGui::PopStyleColor();
    }

    void HierarchyWindow::HandleKeyboardShortcuts()
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            DeleteSelected();

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
            DuplicateSelected();

        if (ImGui::IsKeyPressed(ImGuiKey_F2) && m_Context->HasSelection())
            StartRename(m_Context->SelectedEntity);
    }

    void HierarchyWindow::StartRename(entt::entity entity)
    {
        auto& registry = m_Context->ActiveScene->GetRegistry();
        auto* name = registry.try_get<NameComponent>(entity);
        if (!name) return;

        m_Renaming = true;
        m_RenamingEntity = entity;
        std::strncpy(m_RenameBuffer, name->Name.c_str(), sizeof(m_RenameBuffer) - 1);
        m_Context->Select(entity);
    }

    void HierarchyWindow::CreateObject(const std::string& name, const std::string& primitiveUUID)
    {
        auto entity = m_Context->ActiveScene->CreateEntity(name);
        auto& registry = m_Context->ActiveScene->GetRegistry();

        if (!primitiveUUID.empty())
        {
            auto mesh = MeshFactory::CreateByUUID(primitiveUUID);
            if (mesh)
            {
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = mesh;
                mc.AssetUUID = primitiveUUID;
            }
        }

        // Se há uma pasta selecionada, coloca o objeto dentro dela
        if (m_Context->HasSelection())
        {
            auto selected = m_Context->SelectedEntity;
            if (registry.any_of<FolderComponent>(selected))
                m_Context->ActiveScene->SetParent(entity, selected);
        }

        m_Context->Select(entity);
    }

    void HierarchyWindow::CreateFolder()
    {
        auto entity = m_Context->ActiveScene->CreateFolder("Folder");

        // Se há uma pasta selecionada, cria dentro dela
        auto& registry = m_Context->ActiveScene->GetRegistry();
        if (m_Context->HasSelection())
        {
            auto selected = m_Context->SelectedEntity;
            if (registry.any_of<FolderComponent>(selected))
                m_Context->ActiveScene->SetParent(entity, selected);
        }

        m_Context->Select(entity);
        StartRename(entity); // começa renomeando imediatamente
    }

    void HierarchyWindow::CreateLight()
    {
        auto entity = m_Context->ActiveScene->CreateLight();
        m_Context->Select(entity);
    }

    void HierarchyWindow::DeleteSelected()
    {
        if (!m_Context->HasSelection()) return;

        entt::entity entity = m_Context->SelectedEntity;
        m_Context->ClearSelection();
        m_Renaming = false;
        m_Context->ActiveScene->DestroyEntity(entity);
    }

    void HierarchyWindow::DuplicateSelected()
    {
        if (!m_Context->HasSelection()) return;

        entt::entity copy = m_Context->ActiveScene->DuplicateEntity(m_Context->SelectedEntity);
        if (copy != entt::null)
            m_Context->Select(copy);
    }

    void HierarchyWindow::CreatePostProcess()
    {
        auto entity = m_Context->ActiveScene->CreateEntity("Post Process Volume");
        auto& registry = m_Context->ActiveScene->GetRegistry();
        registry.emplace<PostProcessComponent>(entity);
        m_Context->Select(entity);
    }

} // namespace axe