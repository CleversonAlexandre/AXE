#include "asset_browser.hpp"
#include "axe/log/log.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/scene/game_mode_asset.hpp"
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <shellapi.h>  // Windows — ShellExecuteA para Open in Explorer
#include <windows.h>

namespace axe
{

    static const std::vector<std::string> s_SupportedExtensions = {
        ".gltf", ".glb", ".obj",
        ".png", ".jpg", ".jpeg",
        ".axemat", ".axescene"
    };

   

    // ==================== Utilitários ====================

    bool AssetBrowser::IsSupported(const std::string& filepath) const
    {
        std::string ext = std::filesystem::path(filepath).extension().string();
        for (char& c : ext) c = (char)std::tolower(c);
        for (const auto& s : s_SupportedExtensions)
            if (ext == s) return true;
        return false;
    }

    std::string AssetBrowser::GetFullFolderPath(const std::string& name, const std::string& parent)
    {
        return parent.empty() ? name : parent + "/" + name;
    }

    std::vector<VirtualFolderDef*> AssetBrowser::GetSubfolders(const std::string& parent)
    {
        std::vector<VirtualFolderDef*> result;
        for (auto& f : m_Folders)
            if (f.Parent == parent)
                result.push_back(&f);
        return result;
    }

    std::string AssetBrowser::GetCurrentFolderFilter() const
    {
        return m_SelectedFolder;
    }

    // ==================== Operações de arquivo ====================

    void AssetBrowser::OnFileDrop(const std::string& filepath)
    {
        if (!IsSupported(filepath)) return;

        std::string uuid = AssetDatabase::Get().Register(filepath);

        // Coloca na pasta selecionada
        auto* record = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
        if (record && !m_SelectedFolder.empty())
            record->VirtualFolder = m_SelectedFolder;

        if (ProjectManager::Get().HasProject())
            AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

        std::string ext = std::filesystem::path(filepath).extension().string();
        for (char& c : ext) c = (char)std::tolower(c);
        if (AssetTypeFromExtension(ext) == AssetType::Mesh && m_FileDropCallback)
            m_FileDropCallback(filepath);
    }

    void AssetBrowser::DeleteAsset(const AssetRecord& record)
    {
        try
        {
            if (std::filesystem::exists(record.FilePath))
                std::filesystem::remove(record.FilePath);

            // Remove meta se existir
            auto meta = record.FilePath;
            meta += ".axemeta";
            if (std::filesystem::exists(meta))
                std::filesystem::remove(meta);

            // Remove do cache de texturas
            m_TextureCache.erase(record.UUID);
            m_TexturesPendingLoad.erase(record.UUID);
            m_TexturesFailedLoad.erase(record.UUID);

            AXE_CORE_INFO("AssetBrowser: '{}' excluído.", record.Name);
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("AssetBrowser: falha ao excluir '{}': {}", record.Name, e.what());
        }
    }

    void AssetBrowser::RenameAsset(const AssetRecord& record, const std::string& newName)
    {
        if (newName.empty() || newName == record.Name) return;

        auto newPath = record.FilePath.parent_path() / (newName + record.FilePath.extension().string());
        try
        {
            std::filesystem::rename(record.FilePath, newPath);

            // Atualiza o record
            auto* rec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(record.UUID));
            if (rec)
            {
                rec->FilePath = newPath;
                rec->Name = newName;
            }

            if (ProjectManager::Get().HasProject())
                AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

            AXE_CORE_INFO("AssetBrowser: '{}' renomeado para '{}'.", record.Name, newName);
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("AssetBrowser: falha ao renomear: {}", e.what());
        }
    }

    void AssetBrowser::DuplicateAsset(const AssetRecord& record)
    {
        auto newPath = record.FilePath.parent_path() /
            (record.Name + "_copy" + record.FilePath.extension().string());

        int i = 1;
        while (std::filesystem::exists(newPath))
        {
            newPath = record.FilePath.parent_path() /
                (record.Name + "_copy" + std::to_string(i++) + record.FilePath.extension().string());
        }

        try
        {
            std::filesystem::copy_file(record.FilePath, newPath);
            auto uuid = AssetDatabase::Get().Register(newPath.string());

            auto* rec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
            if (rec) rec->VirtualFolder = record.VirtualFolder;

            if (ProjectManager::Get().HasProject())
                AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

            AXE_CORE_INFO("AssetBrowser: '{}' duplicado.", record.Name);
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("AssetBrowser: falha ao duplicar: {}", e.what());
        }
    }

    void AssetBrowser::OpenInExplorer(const std::filesystem::path& path)
    {
#ifdef _WIN32
        std::string folder = path.parent_path().string();
        ShellExecuteA(nullptr, "explore", folder.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
#endif
    }

    void AssetBrowser::MoveAssetToFolder(const std::string& uuid, const std::string& folder)
    {
        auto* rec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
        if (!rec) return;

        // Move arquivo fisicamente para a pasta de destino
        if (ProjectManager::Get().HasProject() && !folder.empty())
        {
            auto targetDir = ProjectManager::Get().GetCurrent().AssetsPath / folder;
            std::filesystem::create_directories(targetDir);

            auto newPath = targetDir / rec->FilePath.filename();
            if (rec->FilePath != newPath && std::filesystem::exists(rec->FilePath))
            {
                std::error_code ec;
                std::filesystem::rename(rec->FilePath, newPath, ec);
                if (!ec)
                {
                    AXE_CORE_INFO("AssetBrowser: '{}' movido para '{}'",
                        rec->Name, targetDir.string());
                    rec->FilePath = newPath;
                }
            }
        }

        rec->VirtualFolder = folder;
        SaveIfProject();
    }

    // ==================== Pastas virtuais ====================

    // Retorna o path físico no disco para uma pasta virtual
    static std::filesystem::path GetDiskPath(const std::string& virtualPath)
    {
        if (!ProjectManager::Get().HasProject()) return {};
        return ProjectManager::Get().GetCurrent().AssetsPath / virtualPath;
    }

    void AssetBrowser::CreateFolder(const std::string& name, const std::string& parent)
    {
        std::string fullPath = GetFullFolderPath(name, parent);
        for (auto& f : m_Folders)
            if (GetFullFolderPath(f.Name, f.Parent) == fullPath) return;

        m_Folders.push_back({ name, parent, 0xFF4A9EFF });

        // Cria pasta física no disco
        auto diskPath = GetDiskPath(fullPath);
        if (!diskPath.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(diskPath, ec);
            if (!ec)
                AXE_CORE_INFO("AssetBrowser: pasta criada no disco: {}", diskPath.string());
        }

        SaveIfProject();
    }

    void AssetBrowser::DeleteFolder(const std::string& folderPath)
    {
        // Move assets da pasta para a raiz no database
        for (auto& [uuid, record] : const_cast<std::unordered_map<std::string, AssetRecord>&>(
            const_cast<const AssetDatabase&>(AssetDatabase::Get()).GetAll()))
        {
            if (record.VirtualFolder == folderPath)
                record.VirtualFolder = "";
        }

        m_Folders.erase(std::remove_if(m_Folders.begin(), m_Folders.end(),
            [&](const VirtualFolderDef& f) {
                std::string fp = GetFullFolderPath(f.Name, f.Parent);
                return fp == folderPath || fp.starts_with(folderPath + "/");
            }), m_Folders.end());

        if (m_SelectedFolder == folderPath)
            m_SelectedFolder = "";

        // Deleta pasta física do disco recursivamente
        auto diskPath = GetDiskPath(folderPath);
        if (!diskPath.empty() && std::filesystem::exists(diskPath))
        {
            std::error_code ec;
            std::filesystem::remove_all(diskPath, ec);
            if (!ec)
                AXE_CORE_INFO("AssetBrowser: pasta '{}' deletada do disco.", diskPath.string());
            else
                AXE_CORE_ERROR("AssetBrowser: falha ao deletar '{}': {}", diskPath.string(), ec.message());
        }

        SaveIfProject();
    }

    void AssetBrowser::RenameFolder(const std::string& oldPath, const std::string& newName)
    {
        for (auto& f : m_Folders)
        {
            if (GetFullFolderPath(f.Name, f.Parent) == oldPath)
            {
                std::string newPath = GetFullFolderPath(newName, f.Parent);

                // Renomeia pasta física no disco
                auto oldDisk = GetDiskPath(oldPath);
                auto newDisk = GetDiskPath(newPath);
                if (!oldDisk.empty() && std::filesystem::exists(oldDisk))
                {
                    std::error_code ec;
                    std::filesystem::rename(oldDisk, newDisk, ec);
                    if (!ec)
                    {
                        // Atualiza paths dos assets que estão na pasta
                        for (auto& [uuid, record] : const_cast<std::unordered_map<std::string, AssetRecord>&>(
                            const_cast<const AssetDatabase&>(AssetDatabase::Get()).GetAll()))
                        {
                            // Atualiza VirtualFolder
                            if (record.VirtualFolder == oldPath)
                                record.VirtualFolder = newPath;

                            // Atualiza FilePath se o arquivo estava na pasta antiga
                            auto relPath = record.FilePath.lexically_relative(oldDisk);
                            if (!relPath.empty() && relPath.native()[0] != '.')
                                record.FilePath = newDisk / relPath;
                        }
                    }
                }
                else
                {
                    // Pasta só virtual — só atualiza VirtualFolder
                    for (auto& [uuid, record] : const_cast<std::unordered_map<std::string, AssetRecord>&>(
                        const_cast<const AssetDatabase&>(AssetDatabase::Get()).GetAll()))
                    {
                        if (record.VirtualFolder == oldPath)
                            record.VirtualFolder = newPath;
                    }
                }

                f.Name = newName;
                if (m_SelectedFolder == oldPath) m_SelectedFolder = newPath;
                break;
            }
        }
        SaveIfProject();
    }

    // ==================== Persistência de pastas ====================

    void AssetBrowser::SaveFolders(const std::filesystem::path& projectRoot)
    {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& f : m_Folders)
        {
            nlohmann::json entry;
            entry["name"] = f.Name;
            entry["parent"] = f.Parent;
            entry["color"] = f.Color;
            entry["expanded"] = f.Expanded;
            j.push_back(entry);
        }

        std::ofstream file(projectRoot / "axe_browser.json");
        if (file.is_open())
            file << j.dump(4);
    }

    void AssetBrowser::LoadFolders(const std::filesystem::path& projectRoot)
    {
        auto path = projectRoot / "axe_browser.json";
        if (!std::filesystem::exists(path))
        {
            // Primeira vez — salva as pastas padrão e cria no disco
            SaveFolders(projectRoot);
            if (ProjectManager::Get().HasProject())
            {
                for (auto& f : m_Folders)
                {
                    auto diskPath = ProjectManager::Get().GetCurrent().AssetsPath /
                        GetFullFolderPath(f.Name, f.Parent);
                    std::error_code ec;
                    std::filesystem::create_directories(diskPath, ec);
                }
            }
            return;
        }

        std::ifstream file(path);
        if (!file.is_open()) return;

        try
        {
            auto j = nlohmann::json::parse(file);
            if (j.empty()) return;

            m_Folders.clear();
            for (const auto& entry : j)
            {
                VirtualFolderDef f;
                f.Name = entry.value("name", "");
                f.Parent = entry.value("parent", "");
                f.Color = entry.value("color", (uint32_t)0xFFAAAAAA);
                f.Expanded = entry.value("expanded", true);
                if (!f.Name.empty())
                    m_Folders.push_back(f);
            }

            // Garante que todas as pastas existem no disco
            if (ProjectManager::Get().HasProject())
            {
                for (auto& f : m_Folders)
                {
                    auto diskPath = ProjectManager::Get().GetCurrent().AssetsPath /
                        GetFullFolderPath(f.Name, f.Parent);
                    std::error_code ec;
                    std::filesystem::create_directories(diskPath, ec);
                }
            }
        }
        catch (...) {}
    }

    void AssetBrowser::SaveIfProject()
    {
        if (!ProjectManager::Get().HasProject()) return;
        auto& root = ProjectManager::Get().GetCurrent().RootPath;
        AssetDatabase::Get().Save(root);
        SaveFolders(root);
    }

    void AssetBrowser::Update()
    {
        m_FramesSinceStart++;
        if (m_FramesSinceStart < 3) return;

        if (!m_TexturesPendingLoad.empty())
        {
            auto uuid = *m_TexturesPendingLoad.begin();
            m_TexturesPendingLoad.erase(m_TexturesPendingLoad.begin());

            auto& db = AssetDatabase::Get();
            const AssetRecord* record = db.GetByUUID(uuid);

            if (record && std::filesystem::exists(record->FilePath))
            {
                try
                {
                    auto tex = Texture2D::Create(record->FilePath.string());
                    if (tex && tex->IsLoaded())
                        m_TextureCache[uuid] = tex;
                    else
                        m_TexturesFailedLoad.insert(uuid);
                }
                catch (...) { m_TexturesFailedLoad.insert(uuid); }
            }
            else m_TexturesFailedLoad.insert(uuid);
        }
    }

    // ==================== Draw ====================

    void AssetBrowser::Draw()
    {
        if (!ImGui::Begin("Asset Browser")) { ImGui::End(); return; }

        DrawToolbar();
        ImGui::Separator();

        float leftWidth = 180.0f;

        ImGui::BeginChild("##folders", ImVec2(leftWidth, 0), true);

        // Atalhos para pasta — só quando o painel de pastas tem foco
        if (ImGui::IsWindowFocused() && !m_SelectedFolder.empty())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                m_DeleteConfirmFolder = m_SelectedFolder;
                m_DeleteConfirmFolderDiskPath = GetDiskPath(m_SelectedFolder).string();
            }

            if (ImGui::IsKeyPressed(ImGuiKey_F2))
            {
                m_RenamingFolder = m_SelectedFolder;
                auto pos = m_SelectedFolder.rfind('/');
                std::string name = pos == std::string::npos
                    ? m_SelectedFolder : m_SelectedFolder.substr(pos + 1);
                strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer));
                m_RenameFocusNeeded = true;
            }
        }

        DrawFolderTree();
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("##assets", ImVec2(0, 0), true);
        DrawAssetGrid();

        if (ImGui::BeginPopupContextWindow("##empty_ctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            DrawEmptyAreaContextMenu();
            ImGui::EndPopup();
        }
        ImGui::EndChild();

        // Drag & drop de asset para pasta no painel esquerdo é tratado em DrawFolderNode

        // Popup de confirmação de exclusão
        if (!m_DeleteConfirmUUID.empty())
        {
            ImGui::OpenPopup("##confirm_delete_asset");
            auto* rec = AssetDatabase::Get().GetByUUID(m_DeleteConfirmUUID);
            if (ImGui::BeginPopupModal("##confirm_delete_asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Excluir '%s'?", rec ? rec->Name.c_str() : "?");
                ImGui::TextDisabled("Esta ação não pode ser desfeita.");
                ImGui::Separator();
                if (ImGui::Button("Excluir", ImVec2(100, 0)))
                {
                    if (rec) DeleteAsset(*rec);
                    m_DeleteConfirmUUID.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancelar", ImVec2(100, 0)))
                {
                    m_DeleteConfirmUUID.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        // Modal de confirmação de exclusão de PASTA
        if (!m_DeleteConfirmFolder.empty())
        {
            ImGui::OpenPopup("##confirm_delete_folder");
            if (ImGui::BeginPopupModal("##confirm_delete_folder", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::Text("ATENÇÃO — Esta ação é irreversível!");
                ImGui::PopStyleColor();

                ImGui::Separator();
                ImGui::Text("A pasta '%s' será excluída:", m_DeleteConfirmFolder.c_str());
                ImGui::Spacing();

                ImGui::BulletText("Do editor (organização virtual)");
                ImGui::BulletText("Do disco permanentemente:");
                ImGui::Indent(20.0f);
                ImGui::TextDisabled("%s", m_DeleteConfirmFolderDiskPath.c_str());
                ImGui::Unindent(20.0f);

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                ImGui::Text("Todos os arquivos dentro da pasta serao perdidos!");
                ImGui::PopStyleColor();

                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Excluir permanentemente", ImVec2(200, 0)))
                {
                    DeleteFolder(m_DeleteConfirmFolder);
                    m_DeleteConfirmFolder.clear();
                    m_DeleteConfirmFolderDiskPath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(2);

                ImGui::SameLine();
                if (ImGui::Button("Cancelar", ImVec2(100, 0)))
                {
                    m_DeleteConfirmFolder.clear();
                    m_DeleteConfirmFolderDiskPath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        if (!m_ColorPickerFolder.empty())
        {
            ImGui::OpenPopup("##folder_color");
            if (ImGui::BeginPopupModal("##folder_color", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Cor da pasta");
                ImGui::ColorPicker4("##color", m_PickerColor, ImGuiColorEditFlags_NoAlpha);
                if (ImGui::Button("Aplicar", ImVec2(100, 0)))
                {
                    uint32_t r = (uint32_t)(m_PickerColor[0] * 255);
                    uint32_t g = (uint32_t)(m_PickerColor[1] * 255);
                    uint32_t b = (uint32_t)(m_PickerColor[2] * 255);
                    uint32_t col = IM_COL32(r, g, b, 255);
                    for (auto& f : m_Folders)
                        if (GetFullFolderPath(f.Name, f.Parent) == m_ColorPickerFolder)
                            f.Color = col;
                    SaveIfProject();
                    m_ColorPickerFolder.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancelar", ImVec2(100, 0)))
                {
                    m_ColorPickerFolder.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        ImGui::End();
    }

    void AssetBrowser::DrawToolbar()
    {
        ImGui::SliderFloat("##iconsize", &m_IconSize, 32.0f, 128.0f, "%.0f");
        ImGui::SameLine();
        ImGui::TextDisabled("Tamanho");
        ImGui::SameLine(0, 20);

        // Breadcrumb da pasta atual
        if (m_SelectedFolder.empty())
            ImGui::TextDisabled("/ Todos");
        else
            ImGui::TextDisabled("/ %s", m_SelectedFolder.c_str());

        ImGui::SameLine();

        // Botão nova pasta
        if (ImGui::SmallButton("+ Pasta"))
        {
            char buf[64] = "Nova Pasta";
            CreateFolder(buf, m_SelectedFolder);
        }
    }

    // ==================== Folder Tree ====================

    void AssetBrowser::DrawFolderTree()
    {
        ImGui::Text("Assets");
        ImGui::Separator();

        // Raiz
        bool rootSelected = m_SelectedFolder.empty();
        if (rootSelected)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        if (ImGui::Selectable("/ Todos", rootSelected))
            m_SelectedFolder = "";
        if (rootSelected)
            ImGui::PopStyleColor();

        // Drag & drop target para raiz
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
            {
                std::string uuid = (const char*)payload->Data;
                MoveAssetToFolder(uuid, "");
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Separator();

        // Pastas raiz (parent == "")
        for (auto& folder : m_Folders)
        {
            if (!folder.Parent.empty()) continue;
            DrawFolderNode(folder.Name, 0);
        }

        ImGui::Separator();

        // Botão para criar nova pasta raiz
        if (ImGui::SmallButton("+ Nova Pasta"))
        {
            CreateFolder("Nova Pasta", "");
            // Inicia rename
            m_RenamingFolder = "Nova Pasta";
            strncpy(m_RenameBuffer, "Nova Pasta", sizeof(m_RenameBuffer));
        }
    }

    void AssetBrowser::DrawFolderNode(const std::string& folderName, int depth)
    {
        // Encontra a definição
        VirtualFolderDef* folderDef = nullptr;
        std::string folderPath;
        for (auto& f : m_Folders)
        {
            // Para depth 0, parent deve ser ""
            // Para outros, montar o path correto
            if (f.Name == folderName &&
                (depth == 0 ? f.Parent.empty() : true))
            {
                folderDef = &f;
                folderPath = GetFullFolderPath(f.Name, f.Parent);
                break;
            }
        }
        if (!folderDef) return;

        bool selected = (m_SelectedFolder == folderPath);
        auto subfolders = GetSubfolders(folderPath);
        bool hasChildren = !subfolders.empty();

        // Cor da pasta
        ImVec4 folderColor = ImGui::ColorConvertU32ToFloat4(folderDef->Color);
        ImGui::PushStyleColor(ImGuiCol_Text, folderColor);

        // Ícone de pasta
        auto& icons = EditorIconLibrary::Get();
        if (icons.GetFolder() && icons.GetFolder()->IsLoaded())
        {
            ImGui::Image(
                (ImTextureID)(uintptr_t)icons.GetFolder()->GetRendererID(),
                ImVec2(14, 14), ImVec2(0, 1), ImVec2(1, 0),
                folderColor);
            ImGui::SameLine();
        }

        ImGui::PopStyleColor();

        // Rename inline
        if (m_RenamingFolder == folderPath)
        {
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##rename_folder", m_RenameBuffer, sizeof(m_RenameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                RenameFolder(folderPath, m_RenameBuffer);
                m_RenamingFolder.clear();
            }
            if (!ImGui::IsItemActive() && !ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
                m_RenamingFolder.clear();
        }
        else
        {
            std::string label = (hasChildren ? (folderDef->Expanded ? "v " : "> ") : "  ") + folderDef->Name;
            if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0, 0)))
            {
                m_SelectedFolder = folderPath;
                if (hasChildren) folderDef->Expanded = !folderDef->Expanded;
            }
        }

        // Context menu da pasta
        if (ImGui::BeginPopupContextItem(("##folder_ctx_" + folderPath).c_str()))
        {
            DrawFolderContextMenu(folderPath);
            ImGui::EndPopup();
        }

        // Drag & drop target
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
            {
                std::string uuid = (const char*)payload->Data;
                MoveAssetToFolder(uuid, folderPath);
                m_SelectedFolder = folderPath;
            }
            ImGui::EndDragDropTarget();
        }

        // Subpastas
        if (hasChildren && folderDef->Expanded)
        {
            ImGui::Indent(12.0f);
            for (auto* sub : subfolders)
                DrawFolderNode(sub->Name, depth + 1);
            ImGui::Unindent(12.0f);
        }
    }

    // ==================== Context Menus ====================

    void AssetBrowser::DrawFolderContextMenu(const std::string& folderPath)
    {
        if (ImGui::MenuItem("Nova Subpasta"))
        {
            CreateFolder("Nova Pasta", folderPath);
            m_RenamingFolder = GetFullFolderPath("Nova Pasta", folderPath);
            strncpy(m_RenameBuffer, "Nova Pasta", sizeof(m_RenameBuffer));
            m_SelectedFolder = folderPath;
        }

        if (ImGui::MenuItem("Renomear", "F2"))
        {
            m_RenamingFolder = folderPath;
            // Pega só o nome sem o path do pai
            auto pos = folderPath.rfind('/');
            std::string name = pos == std::string::npos ? folderPath : folderPath.substr(pos + 1);
            strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer));
        }

        if (ImGui::MenuItem("Cor da Pasta..."))
        {
            m_ColorPickerFolder = folderPath;
            // Carrega a cor atual
            for (auto& f : m_Folders)
            {
                if (GetFullFolderPath(f.Name, f.Parent) == folderPath)
                {
                    ImVec4 c = ImGui::ColorConvertU32ToFloat4(f.Color);
                    m_PickerColor[0] = c.x;
                    m_PickerColor[1] = c.y;
                    m_PickerColor[2] = c.z;
                    m_PickerColor[3] = c.w;
                }
            }
        }

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.3f, 0.3f, 1));
        if (ImGui::MenuItem("Excluir Pasta", "Del"))
        {
            m_DeleteConfirmFolder = folderPath;
            auto diskPath = GetDiskPath(folderPath);
            m_DeleteConfirmFolderDiskPath = diskPath.string();
        }
        ImGui::PopStyleColor();
    }

    void AssetBrowser::DrawAssetContextMenu(const AssetRecord& record)
    {
        if (ImGui::MenuItem("Abrir"))
        {
            if (record.FilePath.extension() == ".axescript")
            {
                if (m_OnOpenScript) m_OnOpenScript(record.UUID);
            }
            else if (record.FilePath.extension() == ".axegamemode")
            {
                // Define como GameMode ativo do projeto
                if (ProjectManager::Get().HasProject())
                {
                    ProjectManager::Get().GetCurrent().ActiveGameModeUUID = record.UUID;
                    ProjectManager::Get().SaveProject();
                    AXE_EDITOR_INFO("GameMode '{}' definido como ativo.", record.Name);
                }
            }
            else
            {
                if (m_AssetOpenCallback) m_AssetOpenCallback(record);
            }
        }

        if (ImGui::MenuItem("Abrir no Explorer"))
            OpenInExplorer(record.FilePath);

        ImGui::Separator();

        if (ImGui::MenuItem("Renomear", "F2"))
        {
            m_RenamingUUID = record.UUID;
            m_RenameFocusNeeded = true;
            strncpy(m_RenameBuffer, record.Name.c_str(), sizeof(m_RenameBuffer));
        }

        if (ImGui::MenuItem("Duplicar", "Ctrl+D"))
            DuplicateAsset(record);

        if (ImGui::MenuItem("Copiar Path", "Ctrl+C"))
            ImGui::SetClipboardText(record.FilePath.string().c_str());

        ImGui::Separator();

        if (ImGui::BeginMenu("Mover para"))
        {
            if (ImGui::MenuItem("/ Raiz"))
                MoveAssetToFolder(record.UUID, "");
            for (auto& f : m_Folders)
            {
                std::string fp = GetFullFolderPath(f.Name, f.Parent);
                if (ImGui::MenuItem(fp.c_str()))
                    MoveAssetToFolder(record.UUID, fp);
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.3f, 0.3f, 1));
        if (ImGui::MenuItem("Excluir", "Del"))
            m_DeleteConfirmUUID = record.UUID;
        ImGui::PopStyleColor();
    }

    void AssetBrowser::DrawEmptyAreaContextMenu()
    {
        if (ImGui::BeginMenu("Novo"))
        {
            if (ImGui::MenuItem("Material"))
            {
                auto matPath = ProjectManager::Get().GetCurrent().AssetsPath
                    / "Materials" / "NewMaterial.axemat";
                int i = 1;
                while (std::filesystem::exists(matPath))
                    matPath = ProjectManager::Get().GetCurrent().AssetsPath
                    / "Materials" / ("NewMaterial_" + std::to_string(i++) + ".axemat");

                auto matAsset = MaterialAsset::Create(matPath.stem().string());
                matAsset->Save(matPath);
                auto uuid = AssetDatabase::Get().Register(matPath.string());

                auto* rec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
                if (rec) rec->VirtualFolder = m_SelectedFolder;

                if (ProjectManager::Get().HasProject())
                    AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);
            }

            if (ImGui::MenuItem("Game Mode"))
            {
                if (ProjectManager::Get().HasProject())
                {
                    auto dir = ProjectManager::Get().GetCurrent().AssetsPath;
                    std::filesystem::create_directories(dir);
                    auto path = dir / "NewGameMode.axegamemode";
                    int i = 1;
                    while (std::filesystem::exists(path))
                        path = dir / ("NewGameMode_" + std::to_string(i++) + ".axegamemode");

                    auto gm = GameModeAsset::Create(path.stem().string());
                    gm->Save(path);
                    auto uuid = AssetDatabase::Get().Register(path.string());
                    auto* rec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
                    if (rec) rec->VirtualFolder = m_SelectedFolder;
                    AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);
                }
            }

            ImGui::Separator();

            // ── Novo Script ────────────────────────────────────────────────
            if (ImGui::BeginMenu("Script"))
            {
                struct ScriptTypeEntry { const char* label; const char* type; const char* desc; };
                static const ScriptTypeEntry entries[] = {
                    { "Entity",       "Entity",       "Objeto generico com Transform" },
                    { "Agent",        "Agent",        "Controlavel pelo player ou AI" },
                    { "Character",    "Character",    "Agent + CharacterController" },
                    { "StaticObject", "StaticObject", "So visual, sem fisica" },
                    { "Trigger",      "Trigger",      "Colisao invisivel, dispara eventos" },
                };
                for (auto& e : entries)
                {
                    if (ImGui::MenuItem(e.label))
                    {
                        if (!ProjectManager::Get().HasProject()) break;
                        auto dir = ProjectManager::Get().GetCurrent().AssetsPath / "Scripts";
                        std::filesystem::create_directories(dir);
                        auto path = dir / (std::string("New") + e.type + ".axescript");
                        int idx = 1;
                        while (std::filesystem::exists(path))
                            path = dir / (std::string("New") + e.type + "_" + std::to_string(idx++) + ".axescript");

                        auto scriptAsset = ScriptAsset::Create(path.stem().string(),
                            ScriptClassTypeFromString(e.type));
                        scriptAsset->Save(path);
                        auto uuid = AssetDatabase::Get().Register(path.string());
                        auto* rec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
                        if (rec) rec->VirtualFolder = m_SelectedFolder;
                        AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

                        // Abre o editor imediatamente
                        if (m_OnOpenScript) m_OnOpenScript(uuid);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.desc);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Nova Pasta"))
        {
            CreateFolder("Nova Pasta", m_SelectedFolder);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Importar Asset..."))
        {
            auto path = FileDialog::Open(
                "Assets\0*.png;*.jpg;*.jpeg;*.gltf;*.glb;*.obj;*.axemat\0All Files\0*.*\0",
                "Importar Asset");
            if (!path.empty())
                OnFileDrop(path.string());
        }
    }

    // ==================== Asset Grid ====================

    void AssetBrowser::DrawAssetGrid()
    {
        // Atalhos de teclado — só quando o painel está com foco e há seleção
        if (ImGui::IsWindowFocused() && !m_SelectedUUID.empty())
        {
            auto* selectedRecord = AssetDatabase::Get().GetByUUID(m_SelectedUUID);

            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedRecord)
                m_DeleteConfirmUUID = m_SelectedUUID;

            if (ImGui::IsKeyPressed(ImGuiKey_F2) && selectedRecord)
            {
                m_RenamingUUID = m_SelectedUUID;
                m_RenameFocusNeeded = true;
                strncpy(m_RenameBuffer, selectedRecord->Name.c_str(), sizeof(m_RenameBuffer));
            }

            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selectedRecord)
                DuplicateAsset(*selectedRecord);
        }

        // Barra de pesquisa
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.0f);
        ImGui::InputTextWithHint("##search", "Pesquisar asset...", m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::Separator();

        auto& db = AssetDatabase::Get();
        auto& records = db.GetAll();

        std::string searchLower = m_SearchBuffer;
        for (char& c : searchLower) c = (char)std::tolower(c);

        // Mapeamento de pasta predefinida → tipo de asset
        static const std::unordered_map<std::string, AssetType> s_FolderTypeMap = {
            { "Meshes",    AssetType::Mesh     },
            { "Textures",  AssetType::Texture  },
            { "Scenes",    AssetType::Scene    },
            { "Scripts",   AssetType::Script   },
            { "Audio",     AssetType::Audio    },
            { "Materials", AssetType::Material },
        };

        // Verifica se a pasta selecionada é uma pasta predefinida de tipo
        AssetType folderTypeFilter = AssetType::Unknown;
        if (!m_SelectedFolder.empty())
        {
            auto it = s_FolderTypeMap.find(m_SelectedFolder);
            if (it != s_FolderTypeMap.end())
                folderTypeFilter = it->second;
        }

        std::vector<const AssetRecord*> filtered;
        for (const auto& [uuid, record] : records)
        {
            // Filtro de pasta
            bool inFolder = false;
            if (m_SelectedFolder.empty())
            {
                // "/ Todos" — mostra tudo
                inFolder = true;
            }
            else if (folderTypeFilter != AssetType::Unknown)
            {
                // Pasta predefinida de tipo — mostra por tipo OU por VirtualFolder
                inFolder = (record.Type == folderTypeFilter) ||
                    (record.VirtualFolder == m_SelectedFolder);
            }
            else
            {
                // Pasta customizada — filtra por VirtualFolder
                inFolder = (record.VirtualFolder == m_SelectedFolder);
            }

            if (!inFolder) continue;

            // Filtro de pesquisa
            if (!searchLower.empty())
            {
                std::string nameLower = record.Name;
                for (char& c : nameLower) c = (char)std::tolower(c);
                if (nameLower.find(searchLower) == std::string::npos)
                    continue;
            }

            filtered.push_back(&record);
        }

        if (filtered.empty())
        {
            if (strlen(m_SearchBuffer) > 0)
                ImGui::TextDisabled("Nenhum asset encontrado para \"%s\".", m_SearchBuffer);
            else
            {
                ImGui::TextDisabled("Pasta vazia.");
                ImGui::TextDisabled("Arraste arquivos para importar.");
            }
            return;
        }

        float panelWidth = ImGui::GetContentRegionAvail().x;
        float cellSize = m_IconSize + 20.0f;
        int   columns = std::max(1, (int)(panelWidth / cellSize));

        ImGui::Columns(columns, nullptr, false);

        for (const auto* record : filtered)
            DrawAssetItem(*record);

        ImGui::Columns(1);
    }

    void AssetBrowser::DrawAssetItem(const AssetRecord& record)
    {
        auto& icons = EditorIconLibrary::Get();
        std::shared_ptr<Texture2D> icon;
        uint32_t overrideTextureID = 0;

        if (record.Type == AssetType::Material && m_ThumbnailRenderer &&
            record.FilePath.extension() == ".axemat")
        {
            m_ThumbnailRenderer->Register(record.UUID, record.FilePath);
            overrideTextureID = m_ThumbnailRenderer->GetThumbnail(record.UUID);
            icon = icons.GetMaterial();
        }
        else if (record.Type == AssetType::Texture)
        {
            if (m_TexturesFailedLoad.count(record.UUID))
                icon = icons.GetForType("Texture");
            else if (m_TextureCache.count(record.UUID))
                icon = m_TextureCache[record.UUID];
            else if (!m_TexturesPendingLoad.count(record.UUID))
            {
                if (std::filesystem::exists(record.FilePath))
                    m_TexturesPendingLoad.insert(record.UUID);
                icon = icons.GetForType("Texture");
            }
            else
                icon = icons.GetForType("Texture");
        }
        else
        {
            switch (record.Type)
            {
            case AssetType::Scene:    icon = icons.GetScene();                                   break;
            case AssetType::Script:   icon = icons.GetScriptForClass(record.ScriptClassType);  break;
            case AssetType::Audio:    icon = icons.GetAudio();                                  break;
            case AssetType::GameMode: icon = icons.GetScene();                                  break;
            default:                  icon = icons.GetMesh();                                   break;
            }
        }

        ImGui::PushID(record.UUID.c_str());

        ImVec2 itemPos = ImGui::GetCursorScreenPos();
        float  padding = 6.0f;
        float  totalW = m_IconSize + padding * 2.0f;
        float  totalH = totalW + 22.0f;

        bool selected = (m_SelectedUUID == record.UUID);

        ImGui::InvisibleButton("##item", ImVec2(totalW, totalH));

        bool hovered = ImGui::IsItemHovered();
        bool dclicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        if (ImGui::IsItemClicked()) m_SelectedUUID = record.UUID;

        // Drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("ASSET_UUID", record.UUID.c_str(), record.UUID.size() + 1);
            if (icon && icon->IsLoaded())
                ImGui::Image((ImTextureID)(uintptr_t)icon->GetRendererID(),
                    ImVec2(32, 32), ImVec2(0, 1), ImVec2(1, 0));
            ImGui::Text("%s", record.Name.c_str());
            ImGui::EndDragDropSource();
        }

        if (dclicked)
        {
            // Duplo clique em .axescript abre o Script Editor
            if (record.FilePath.extension() == ".axescript")
            {
                if (m_OnOpenScript) m_OnOpenScript(record.UUID);
            }
            else if (record.FilePath.extension() == ".axegamemode")
            {
                // Define como GameMode ativo do projeto
                if (ProjectManager::Get().HasProject())
                {
                    ProjectManager::Get().GetCurrent().ActiveGameModeUUID = record.UUID;
                    ProjectManager::Get().SaveProject();
                    AXE_EDITOR_INFO("GameMode '{}' definido como ativo.", record.Name);
                }
            }
            else
            {
                if (m_InstantiateCallback) m_InstantiateCallback(record.UUID);
                if (m_AssetOpenCallback)   m_AssetOpenCallback(record);
            }
        }

        if (hovered && !dclicked)
            ImGui::SetTooltip("%s\n%s", record.Name.c_str(), record.FilePath.string().c_str());

        // Context menu
        if (ImGui::BeginPopupContextItem("##item_ctx"))
        {
            DrawAssetContextMenu(record);
            ImGui::EndPopup();
        }

        // Desenha
        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImVec2 rectMin = itemPos;
        ImVec2 rectMax = ImVec2(itemPos.x + totalW, itemPos.y + totalH);

        if (selected)
            draw->AddRectFilled(rectMin, rectMax, IM_COL32(60, 100, 200, 80), 6.0f);
        else if (hovered)
            draw->AddRectFilled(rectMin, rectMax, IM_COL32(60, 120, 200, 50), 6.0f);

        ImVec2 iconMin = ImVec2(itemPos.x + padding, itemPos.y + padding);
        ImVec2 iconMax = ImVec2(iconMin.x + m_IconSize, iconMin.y + m_IconSize);

        draw->AddRect(iconMin, iconMax,
            selected ? IM_COL32(100, 150, 255, 255) :
            hovered ? IM_COL32(80, 150, 255, 200) :
            IM_COL32(80, 80, 80, 120),
            4.0f, 0, 1.5f);

        if (overrideTextureID != 0)
            draw->AddImage((ImTextureID)(uintptr_t)overrideTextureID,
                iconMin, iconMax, ImVec2(0, 1), ImVec2(1, 0));
        else if (icon && icon->IsLoaded())
            draw->AddImage((ImTextureID)(uintptr_t)icon->GetRendererID(),
                iconMin, iconMax, ImVec2(0, 1), ImVec2(1, 0));
        else
            draw->AddRectFilled(iconMin, iconMax, IM_COL32(60, 60, 60, 255), 4.0f);

        // Nome — rename inline ou texto
        float textY = itemPos.y + totalH - 20.0f;

        if (m_RenamingUUID == record.UUID)
        {
            ImGui::SetCursorScreenPos(ImVec2(itemPos.x, textY));
            ImGui::SetNextItemWidth(totalW);

            // Foco automático no primeiro frame do rename
            if (m_RenameFocusNeeded)
            {
                ImGui::SetKeyboardFocusHere();
                m_RenameFocusNeeded = false;
            }

            bool confirmed = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

            if (confirmed)
            {
                RenameAsset(record, m_RenameBuffer);
                m_RenamingUUID.clear();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                m_RenamingUUID.clear();
            }
            // Cancela se clicar FORA do input — mas só se o item não estiver ativo
            else if (!ImGui::IsItemActive() && !ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                // Aplica o rename ao perder foco
                RenameAsset(record, m_RenameBuffer);
                m_RenamingUUID.clear();
            }
        }
        else
        {
            std::string displayName = record.Name;
            if (displayName.size() > 10)
                displayName = displayName.substr(0, 9) + "...";

            float textW = ImGui::CalcTextSize(displayName.c_str()).x;
            float textX = itemPos.x + (totalW - textW) * 0.5f;

            draw->AddText(ImVec2(textX, textY),
                selected ? IM_COL32(140, 190, 255, 255) :
                hovered ? IM_COL32(140, 180, 255, 255) :
                IM_COL32(200, 200, 200, 255),
                displayName.c_str());
        }

        ImGui::NextColumn();
        ImGui::PopID();
    }

    void AssetBrowser::DrawContextMenuEmpty()
    {
        DrawEmptyAreaContextMenu();
    }

   

} // namespace axe