#include "asset_browser.hpp"
#include "axe/log/log.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/scene/game_mode_asset.hpp"
#include <imgui.h>
#include <algorithm>
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

    // Quebra um nome em até `maxLines` linhas que cabem em `maxWidth` pixels,
    // ao invés de truncar com "..." — usado nos itens do grid (assets e pastas)
    // para que o nome inteiro fique visível sempre que possível.
    static std::vector<std::string> WrapNameToLines(const std::string& text, float maxWidth, int maxLines)
    {
        std::vector<std::string> lines;
        std::string current;

        auto pushWord = [&](const std::string& word)
            {
                if (word.empty()) return;
                // Sem espaço artificial aqui: o separador original (' ' ou '_')
                // já fica anexado ao FINAL do token anterior pelo tokenizador
                // abaixo. Inserir " " de novo duplicava o espaço visualmente
                // (ex: "Computador_" + " " + "Base" => "Computador_ Base",
                // com um espaço fantasma que não existe no nome real).
                std::string candidate = current + word;
                if (current.empty() || ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
                    current = candidate;
                else
                {
                    lines.push_back(current);
                    current = word;
                }
            };

        // Tokeniza por espaço/underscore, mantendo o separador junto da palavra
        // para evitar "comer" caracteres do nome original.
        std::string token;
        for (size_t i = 0; i <= text.size(); ++i)
        {
            bool isBreak = (i == text.size()) || text[i] == ' ' || text[i] == '_';
            if (!isBreak) { token += text[i]; continue; }
            if (i < text.size()) token += text[i];

            if (!token.empty())
            {
                // Palavra sozinha não cabe na largura — quebra por caractere
                if (ImGui::CalcTextSize(token.c_str()).x > maxWidth)
                {
                    std::string piece;
                    for (char c : token)
                    {
                        std::string test = piece + c;
                        if (!piece.empty() && ImGui::CalcTextSize(test.c_str()).x > maxWidth)
                        {
                            pushWord(piece);
                            piece = std::string(1, c);
                        }
                        else piece += c;
                    }
                    if (!piece.empty()) pushWord(piece);
                }
                else pushWord(token);

                token.clear();
            }

            if ((int)lines.size() >= maxLines) break;
        }
        if (!current.empty() && (int)lines.size() < maxLines)
            lines.push_back(current);

        // Se ainda sobrou conteúdo não exibido, adiciona "..." só na última linha
        if ((int)lines.size() >= maxLines)
        {
            lines.resize(maxLines);
            size_t shownLen = 0;
            for (auto& l : lines) shownLen += l.size();
            if (shownLen < text.size())
            {
                std::string& last = lines.back();
                while (!last.empty() && ImGui::CalcTextSize((last + "...").c_str()).x > maxWidth)
                    last.pop_back();
                last += "...";
            }
        }
        if (lines.empty()) lines.push_back("");
        return lines;
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

    // Verifica se 'path' está dentro de 'dir' (ambos resolvidos para absoluto)
    static bool IsPathInsideDir(const std::filesystem::path& path, const std::filesystem::path& dir)
    {
        std::error_code ec;
        auto rel = std::filesystem::relative(path, dir, ec);
        if (ec || rel.empty()) return false;
        // relative() começa com ".." quando 'path' está FORA de 'dir'
        return rel.begin()->string() != "..";
    }

    // Copia para 'targetDir' qualquer arquivo da mesma pasta de 'srcPath' que
    // compartilhe o nome-base (stem) — cobre o caso clássico de .obj + .mtl
    // (e texturas que sigam a mesma convenção de nome).
    static void CopySiblingFiles(const std::filesystem::path& srcPath, const std::filesystem::path& targetDir)
    {
        std::error_code dirEc;
        if (!std::filesystem::exists(srcPath.parent_path())) return;

        for (const auto& entry : std::filesystem::directory_iterator(srcPath.parent_path(), dirEc))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path() == srcPath) continue;
            if (entry.path().stem() != srcPath.stem()) continue;
            if (entry.path().extension() == ".axemeta") continue;

            auto siblingDest = targetDir / entry.path().filename();
            if (!std::filesystem::exists(siblingDest))
            {
                std::error_code sCopyEc;
                std::filesystem::copy_file(entry.path(), siblingDest,
                    std::filesystem::copy_options::none, sCopyEc);
            }
        }
    }

    void AssetBrowser::ScanExternalAssets(std::vector<std::string>& outUUIDs) const
    {
        outUUIDs.clear();
        if (!ProjectManager::Get().HasProject()) return;

        auto assetsDir = std::filesystem::absolute(ProjectManager::Get().GetCurrent().AssetsPath);

        for (const auto& [uuid, record] : AssetDatabase::Get().GetAll())
        {
            if (record.FilePath.empty()) continue;             // primitivas (Cube, Sphere...)
            if (!std::filesystem::exists(record.FilePath)) continue; // já quebrado — nada a mover

            auto absPath = std::filesystem::absolute(record.FilePath);
            if (!IsPathInsideDir(absPath, assetsDir))
                outUUIDs.push_back(uuid);
        }
    }

    void AssetBrowser::RelocateAssets(const std::vector<std::string>& uuids)
    {
        m_RelocateErrorMessages.clear();
        m_RelocateSuccessCount = 0;

        if (!ProjectManager::Get().HasProject()) return;
        auto assetsRoot = ProjectManager::Get().GetCurrent().AssetsPath;

        for (const auto& uuid : uuids)
        {
            const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);
            if (!rec) continue;

            std::filesystem::path srcPath = rec->FilePath;
            std::string virtualFolder = rec->VirtualFolder; // copia — rec fica inválido após UpdatePath
            std::string recordName = rec->Name;

            auto targetDir = assetsRoot;
            if (!virtualFolder.empty())
                targetDir /= virtualFolder;

            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            auto destPath = targetDir / srcPath.filename();
            int i = 1;
            while (std::filesystem::exists(destPath))
                destPath = targetDir / (srcPath.stem().string() + "_" + std::to_string(i++) + srcPath.extension().string());

            std::error_code copyEc;
            std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::none, copyEc);

            if (copyEc)
            {
                m_RelocateErrorMessages.push_back(recordName + ": " + copyEc.message());
                continue;
            }

            CopySiblingFiles(srcPath, targetDir);

            // Atualiza o registro para o novo caminho — move o .axemeta e
            // corrige o índice de caminhos (ver comentário em UpdatePath).
            // O arquivo ORIGINAL (em Downloads, etc.) não é apagado.
            AssetDatabase::Get().UpdatePath(uuid, destPath);
            m_RelocateSuccessCount++;
        }

        SaveIfProject();
    }

    void AssetBrowser::OnFileDrop(const std::string& filepath)
    {
        if (!IsSupported(filepath)) return;

        std::filesystem::path srcPath = std::filesystem::absolute(filepath);
        std::string finalPath = srcPath.string();

        // CRÍTICO: copia o arquivo importado para dentro da pasta Assets do
        // projeto (respeitando a pasta virtual selecionada), em vez de só
        // registrar o caminho ORIGINAL de onde foi arrastado. Sem isso, o
        // asset "morava" para sempre fora do projeto (ex: na pasta Downloads)
        // — o .axemeta era criado lá, e a pasta correspondente dentro do
        // projeto nunca recebia o arquivo de fato.
        if (ProjectManager::Get().HasProject())
        {
            auto targetDir = ProjectManager::Get().GetCurrent().AssetsPath;
            if (!m_SelectedFolder.empty())
                targetDir /= m_SelectedFolder;

            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            // Já está dentro da própria pasta Assets do projeto? não duplica.
            std::error_code eqEc;
            bool alreadyInProject = std::filesystem::exists(targetDir) &&
                std::filesystem::equivalent(srcPath.parent_path(), targetDir, eqEc) && !eqEc;

            if (!alreadyInProject)
            {
                auto destPath = targetDir / srcPath.filename();

                // Evita sobrescrever um arquivo existente com o mesmo nome
                int i = 1;
                while (std::filesystem::exists(destPath))
                    destPath = targetDir / (srcPath.stem().string() + "_" + std::to_string(i++) + srcPath.extension().string());

                std::error_code copyEc;
                std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::none, copyEc);

                if (!copyEc)
                {
                    finalPath = destPath.string();
                    AXE_CORE_INFO("AssetBrowser: '{}' importado para '{}'.",
                        srcPath.filename().string(), targetDir.string());

                    // Malhas .obj costumam vir com um .mtl (e às vezes texturas)
                    // na mesma pasta, referenciados por caminho relativo. Copia
                    // junto qualquer arquivo com o mesmo nome-base (stem), senão
                    // o material da malha quebra ao carregar do novo local.
                    CopySiblingFiles(srcPath, targetDir);
                }
                else
                {
                    AXE_CORE_ERROR("AssetBrowser: falha ao copiar asset importado '{}': {}",
                        srcPath.string(), copyEc.message());
                }
            }
        }

        std::string uuid = AssetDatabase::Get().Register(finalPath);

        // Coloca na pasta selecionada
        auto* record = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
        if (record && !m_SelectedFolder.empty())
            record->VirtualFolder = m_SelectedFolder;

        if (ProjectManager::Get().HasProject())
            AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

        std::string ext = std::filesystem::path(finalPath).extension().string();
        for (char& c : ext) c = (char)std::tolower(c);
        if (AssetTypeFromExtension(ext) == AssetType::Mesh && m_FileDropCallback)
            m_FileDropCallback(finalPath);
    }

    void AssetBrowser::DeleteAsset(const AssetRecord& record)
    {
        // Copia os dados necessários ANTES de remover do AssetDatabase —
        // 'record' é uma referência para dentro do map; depois do Unregister()
        // ela fica pendurada (dangling) e não pode mais ser usada.
        std::string uuid = record.UUID;
        std::string name = record.Name;
        auto filePath = record.FilePath;

        try
        {
            if (std::filesystem::exists(filePath))
                std::filesystem::remove(filePath);

            // Remove meta se existir
            auto meta = filePath;
            meta += ".axemeta";
            if (std::filesystem::exists(meta))
                std::filesystem::remove(meta);

            // Remove do cache de texturas
            m_TextureCache.erase(uuid);
            m_TexturesPendingLoad.erase(uuid);
            m_TexturesFailedLoad.erase(uuid);

            // CRÍTICO: remove o registro do AssetDatabase em memória.
            // Sem isso, o asset só desaparecia do browser depois de reiniciar
            // o editor — o arquivo já tinha sido apagado do disco, mas o
            // registro continuava vivo em m_Records até o próximo Load().
            AssetDatabase::Get().Unregister(uuid);

            if (m_SelectedUUID == uuid)
                m_SelectedUUID.clear();

            if (ProjectManager::Get().HasProject())
                AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

            AXE_CORE_INFO("AssetBrowser: '{}' excluído.", name);
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("AssetBrowser: falha ao excluir '{}': {}", name, e.what());
        }
    }

    void AssetBrowser::RenameAsset(const AssetRecord& record, const std::string& newName)
    {
        if (newName.empty() || newName == record.Name) return;

        auto newPath = record.FilePath.parent_path() / (newName + record.FilePath.extension().string());
        try
        {
            // Não permite renomear para um nome que colida com outro arquivo já existente
            if (std::filesystem::exists(newPath) && newPath != record.FilePath)
            {
                AXE_CORE_ERROR("AssetBrowser: já existe um arquivo '{}'. Renomeação cancelada.", newPath.string());
                return;
            }

            std::filesystem::rename(record.FilePath, newPath);

            // Atualiza o record E corrige o índice de caminhos do AssetDatabase
            // (ver comentário em AssetDatabase::UpdatePath — sem isso o caminho
            // antigo fica "fantasma" registrado e pode ser reaproveitado por um
            // asset novo, corrompendo este registro).
            AssetDatabase::Get().UpdatePath(record.UUID, newPath, newName);

            // Materiais guardam o próprio nome dentro do JSON do .axemat
            // (separado do nome do arquivo). Sem isso, o Material Editor
            // continuaria mostrando "NewMaterial" mesmo após renomear no browser.
            if (newPath.extension() == ".axemat")
            {
                try
                {
                    std::ifstream in(newPath);
                    if (in.is_open())
                    {
                        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
                        in.close();
                        if (!j.is_discarded())
                        {
                            j["name"] = newName;
                            std::ofstream out(newPath);
                            if (out.is_open())
                                out << j.dump(4);
                        }
                    }
                }
                catch (...) {}
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
                    // Corrige o índice de caminhos — ver UpdatePath para detalhes
                    // de por que isso é crítico (caminho antigo fantasma).
                    AssetDatabase::Get().UpdatePath(uuid, newPath);
                    rec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
                }
            }
        }

        if (rec) rec->VirtualFolder = folder;
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

        float totalAvailWidth = ImGui::GetContentRegionAvail().x;

        ImGui::BeginChild("##folders", ImVec2(m_FolderPanelWidth, 0), true);

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
        ImGui::SameLine(0.0f, 0.0f);

        // Splitter arrastável — permite redimensionar o painel de pastas com o mouse
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.85f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.55f, 0.85f, 0.8f));
        ImGui::Button("##folder_splitter", ImVec2(6.0f, ImGui::GetContentRegionAvail().y));
        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        if (ImGui::IsItemActive())
            m_FolderPanelWidth += ImGui::GetIO().MouseDelta.x;

        // Limites razoáveis — não deixa colapsar nem tomar a janela inteira
        // (usa a largura TOTAL capturada antes dos painéis, não a sobra atual,
        // que já reflete o painel de pastas no tamanho antigo)
        float minAssetAreaWidth = 150.0f;
        float splitterWidth = 6.0f;
        float maxFolderWidth = std::max(120.0f, totalAvailWidth - splitterWidth - minAssetAreaWidth);
        m_FolderPanelWidth = std::clamp(m_FolderPanelWidth, 120.0f, maxFolderWidth);

        ImGui::SameLine(0.0f, 0.0f);

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
                ImGui::Text("Delete '%s'?", rec ? rec->Name.c_str() : "?");
                ImGui::TextDisabled("This action cannot be undone.");
                ImGui::Separator();
                if (ImGui::Button("Delete", ImVec2(100, 0)))
                {
                    if (rec) DeleteAsset(*rec);
                    m_DeleteConfirmUUID.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0)))
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
                ImGui::Text("WARNING — This action is irreversible!");
                ImGui::PopStyleColor();

                ImGui::Separator();
                ImGui::Text("Folder '%s' will be deleted:", m_DeleteConfirmFolder.c_str());
                ImGui::Spacing();

                ImGui::BulletText("From the editor (virtual organization)");
                ImGui::BulletText("From disk, permanently:");
                ImGui::Indent(20.0f);
                ImGui::TextDisabled("%s", m_DeleteConfirmFolderDiskPath.c_str());
                ImGui::Unindent(20.0f);

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                ImGui::Text("All files inside this folder will be lost!");
                ImGui::PopStyleColor();

                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Delete permanently", ImVec2(200, 0)))
                {
                    DeleteFolder(m_DeleteConfirmFolder);
                    m_DeleteConfirmFolder.clear();
                    m_DeleteConfirmFolderDiskPath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(2);

                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0)))
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
                ImGui::Text("Folder Color");
                ImGui::ColorPicker4("##color", m_PickerColor, ImGuiColorEditFlags_NoAlpha);
                if (ImGui::Button("Apply", ImVec2(100, 0)))
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
                if (ImGui::Button("Cancel", ImVec2(100, 0)))
                {
                    m_ColorPickerFolder.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        DrawRelocateAssetsModals();

        ImGui::End();
    }

    void AssetBrowser::DrawToolbar()
    {
        ImGui::SliderFloat("##iconsize", &m_IconSize, 32.0f, 128.0f, "%.0f");
        ImGui::SameLine();
        ImGui::TextDisabled("Size");
        ImGui::SameLine(0, 20);

        // Breadcrumb da pasta atual
        if (m_SelectedFolder.empty())
            ImGui::TextDisabled("/ All");
        else
            ImGui::TextDisabled("/ %s", m_SelectedFolder.c_str());

        ImGui::SameLine();

        // Botão nova pasta
        if (ImGui::SmallButton("+ Folder"))
        {
            char buf[64] = "New Folder";
            CreateFolder(buf, m_SelectedFolder);
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Relocate Assets"))
        {
            ScanExternalAssets(m_PendingRelocateUUIDs);
            m_RelocateConfirmOpen = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies assets that live outside the project's Assets folder\n"
                "(e.g. imported before this fix, or referenced from Downloads)\n"
                "into the project. Originals are not deleted.");
    }

    void AssetBrowser::DrawRelocateAssetsModals()
    {
        if (m_RelocateConfirmOpen)
        {
            ImGui::OpenPopup("##relocate_confirm");
            if (ImGui::BeginPopupModal("##relocate_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (m_PendingRelocateUUIDs.empty())
                {
                    ImGui::Text("Every asset is already inside the project.");
                    ImGui::TextDisabled("Nothing to relocate.");
                }
                else
                {
                    ImGui::Text("%d asset(s) found outside the project's Assets folder.",
                        (int)m_PendingRelocateUUIDs.size());
                    ImGui::Spacing();
                    ImGui::TextDisabled("They will be COPIED into the project, organized by their");
                    ImGui::TextDisabled("current virtual folder. Original files are kept untouched.");
                }

                ImGui::Separator();
                ImGui::Spacing();

                if (!m_PendingRelocateUUIDs.empty())
                {
                    if (ImGui::Button("Relocate", ImVec2(120, 0)))
                    {
                        RelocateAssets(m_PendingRelocateUUIDs);
                        m_PendingRelocateUUIDs.clear();
                        m_RelocateConfirmOpen = false;
                        m_RelocateResultOpen = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                }
                if (ImGui::Button("Cancel", ImVec2(100, 0)))
                {
                    m_PendingRelocateUUIDs.clear();
                    m_RelocateConfirmOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        if (m_RelocateResultOpen)
        {
            ImGui::OpenPopup("##relocate_result");
            if (ImGui::BeginPopupModal("##relocate_result", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("%d asset(s) relocated successfully.", m_RelocateSuccessCount);

                if (!m_RelocateErrorMessages.empty())
                {
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
                    ImGui::Text("%d failed:", (int)m_RelocateErrorMessages.size());
                    ImGui::PopStyleColor();
                    for (auto& msg : m_RelocateErrorMessages)
                        ImGui::BulletText("%s", msg.c_str());
                }

                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(100, 0)))
                {
                    m_RelocateResultOpen = false;
                    m_RelocateErrorMessages.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
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
        if (ImGui::Selectable("/ All", rootSelected))
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
            DrawFolderNode(folder.Name); // path == name na raiz (parent vazio)
        }

        ImGui::Separator();

        // Botão para criar nova pasta raiz
        if (ImGui::SmallButton("+ New Folder"))
        {
            CreateFolder("New Folder", "");
            // Inicia rename
            m_RenamingFolder = "New Folder";
            strncpy(m_RenameBuffer, "New Folder", sizeof(m_RenameBuffer));
        }
    }

    void AssetBrowser::DrawFolderNode(const std::string& folderPath)
    {
        // Identifica a pasta pelo CAMINHO COMPLETO, não só pelo nome.
        // Antes, a busca era "f.Name == folderName" aceitando qualquer parent
        // para profundidade > 0 — então duas pastas com o mesmo nome em
        // lugares diferentes da árvore colidiam: renomear/editar uma acabava
        // afetando a primeira pasta com aquele nome encontrada no vetor.
        VirtualFolderDef* folderDef = nullptr;
        for (auto& f : m_Folders)
        {
            if (GetFullFolderPath(f.Name, f.Parent) == folderPath)
            {
                folderDef = &f;
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

        // Subpastas — passa o CAMINHO COMPLETO do filho, não só o nome
        if (hasChildren && folderDef->Expanded)
        {
            ImGui::Indent(12.0f);
            for (auto* sub : subfolders)
                DrawFolderNode(GetFullFolderPath(sub->Name, sub->Parent));
            ImGui::Unindent(12.0f);
        }
    }

    // ==================== Context Menus ====================

    void AssetBrowser::DrawFolderContextMenu(const std::string& folderPath)
    {
        if (ImGui::MenuItem("New Subfolder"))
        {
            CreateFolder("New Folder", folderPath);
            m_RenamingFolder = GetFullFolderPath("New Folder", folderPath);
            strncpy(m_RenameBuffer, "New Folder", sizeof(m_RenameBuffer));
            m_SelectedFolder = folderPath;
        }

        if (ImGui::MenuItem("Rename", "F2"))
        {
            m_RenamingFolder = folderPath;
            // Pega só o nome sem o path do pai
            auto pos = folderPath.rfind('/');
            std::string name = pos == std::string::npos ? folderPath : folderPath.substr(pos + 1);
            strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer));
        }

        if (ImGui::MenuItem("Folder Color..."))
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
        if (ImGui::MenuItem("Delete Folder", "Del"))
        {
            m_DeleteConfirmFolder = folderPath;
            auto diskPath = GetDiskPath(folderPath);
            m_DeleteConfirmFolderDiskPath = diskPath.string();
        }
        ImGui::PopStyleColor();
    }

    void AssetBrowser::DrawAssetContextMenu(const AssetRecord& record)
    {
        if (ImGui::MenuItem("Open"))
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

        if (ImGui::MenuItem("Open in Explorer"))
            OpenInExplorer(record.FilePath);

        ImGui::Separator();

        if (ImGui::MenuItem("Rename", "F2"))
        {
            m_RenamingUUID = record.UUID;
            m_RenameFocusNeeded = true;
            strncpy(m_RenameBuffer, record.Name.c_str(), sizeof(m_RenameBuffer));
        }

        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            DuplicateAsset(record);

        if (ImGui::MenuItem("Copy Path", "Ctrl+C"))
            ImGui::SetClipboardText(record.FilePath.string().c_str());

        ImGui::Separator();

        if (ImGui::BeginMenu("Move to"))
        {
            if (ImGui::MenuItem("/ Root"))
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
        if (ImGui::MenuItem("Delete", "Del"))
            m_DeleteConfirmUUID = record.UUID;
        ImGui::PopStyleColor();
    }

    void AssetBrowser::DrawEmptyAreaContextMenu()
    {
        if (ImGui::BeginMenu("New"))
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
                    { "Entity",       "Entity",       "Generic object with a Transform" },
                    { "Agent",        "Agent",        "Controllable by the player or AI" },
                    { "Character",    "Character",    "Agent + CharacterController" },
                    { "StaticObject", "StaticObject", "Visual only, no physics" },
                    { "Trigger",      "Trigger",      "Invisible collision, fires events" },
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

        if (ImGui::MenuItem("New Folder"))
        {
            CreateFolder("New Folder", m_SelectedFolder);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Import Asset..."))
        {
            auto path = FileDialog::Open(
                "Assets\0*.png;*.jpg;*.jpeg;*.gltf;*.glb;*.obj;*.axemat\0All Files\0*.*\0",
                "Import Asset");
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
        ImGui::InputTextWithHint("##search", "Search assets...", m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::Separator();

        auto& db = AssetDatabase::Get();
        auto& records = db.GetAll();

        std::string searchLower = m_SearchBuffer;
        for (char& c : searchLower) c = (char)std::tolower(c);

        std::vector<const AssetRecord*> filtered;
        for (const auto& [uuid, record] : records)
        {
            // Filtro de pasta — uma pasta mostra SOMENTE o que está diretamente
            // dentro dela (igual ao Content Browser da Unreal). Antes, pastas
            // predefinidas como "Materials" ou "Audio" filtravam por TIPO,
            // mostrando todos os materiais/áudios do projeto inteiro mesmo que
            // estivessem organizados em outras subpastas — por isso o filtro
            // parecia "não funcionar".
            bool inFolder = m_SelectedFolder.empty()
                ? true                                          // "/ Todos" — mostra tudo
                : (record.VirtualFolder == m_SelectedFolder);    // só o que está nesta pasta

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

        // Subpastas diretas da pasta atual — exibidas como itens navegáveis
        // no próprio grid, igual ao Content Browser da Unreal. Antes, pastas
        // só apareciam na árvore à esquerda; ao selecionar uma pasta-pai
        // (ex: "Content") o grid ficava vazio mesmo havendo subpastas dentro.
        std::vector<VirtualFolderDef*> subfolders = GetSubfolders(m_SelectedFolder);

        // Filtro de pesquisa também se aplica às subpastas
        if (!searchLower.empty())
        {
            subfolders.erase(std::remove_if(subfolders.begin(), subfolders.end(),
                [&](VirtualFolderDef* f) {
                    std::string nameLower = f->Name;
                    for (char& c : nameLower) c = (char)std::tolower(c);
                    return nameLower.find(searchLower) == std::string::npos;
                }), subfolders.end());
        }

        if (filtered.empty() && subfolders.empty())
        {
            if (strlen(m_SearchBuffer) > 0)
                ImGui::TextDisabled("No assets found for \"%s\".", m_SearchBuffer);
            else
            {
                ImGui::TextDisabled("This folder is empty.");
                ImGui::TextDisabled("Drag files here to import.");
            }
            return;
        }

        float panelWidth = ImGui::GetContentRegionAvail().x;
        float cellSize = m_IconSize + 20.0f;
        int   columns = std::max(1, (int)(panelWidth / cellSize));

        ImGui::Columns(columns, nullptr, false);

        for (auto* folder : subfolders)
            DrawFolderItem(*folder);

        for (const auto* record : filtered)
            DrawAssetItem(*record);

        ImGui::Columns(1);
    }

    void AssetBrowser::DrawFolderItem(const VirtualFolderDef& folder)
    {
        std::string folderPath = GetFullFolderPath(folder.Name, folder.Parent);

        auto& icons = EditorIconLibrary::Get();
        auto  icon = icons.GetFolder();

        ImGui::PushID(folderPath.c_str());

        ImVec2 itemPos = ImGui::GetCursorScreenPos();
        float  padding = 6.0f;
        float  totalW = m_IconSize + padding * 2.0f;
        float  iconBlockH = totalW;
        float  lineHeight = ImGui::GetTextLineHeight();
        const int kMaxNameLines = 3;
        float  textBlockH = lineHeight * kMaxNameLines + 4.0f;
        float  totalH = iconBlockH + textBlockH;

        bool selected = (m_SelectedFolder == folderPath);

        ImGui::InvisibleButton("##folderitem", ImVec2(totalW, totalH));

        bool hovered = ImGui::IsItemHovered();
        bool dclicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        if (ImGui::IsItemClicked()) m_SelectedFolder = folderPath;
        if (dclicked) m_SelectedFolder = folderPath; // navega para dentro

        // Drag & drop target — soltar um asset aqui o move para esta pasta
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
            {
                std::string uuid = (const char*)payload->Data;
                MoveAssetToFolder(uuid, folderPath);
            }
            ImGui::EndDragDropTarget();
        }

        if (hovered && !dclicked)
            ImGui::SetTooltip("%s\nDouble-click to open", folder.Name.c_str());

        if (ImGui::BeginPopupContextItem("##folderitem_ctx"))
        {
            DrawFolderContextMenu(folderPath);
            ImGui::EndPopup();
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImVec2 rectMin = itemPos;
        ImVec2 rectMax = ImVec2(itemPos.x + totalW, itemPos.y + totalH);

        if (selected)
            draw->AddRectFilled(rectMin, rectMax, IM_COL32(60, 100, 200, 80), 6.0f);
        else if (hovered)
            draw->AddRectFilled(rectMin, rectMax, IM_COL32(60, 120, 200, 50), 6.0f);

        ImVec2 iconMin = ImVec2(itemPos.x + padding, itemPos.y + padding);
        ImVec2 iconMax = ImVec2(iconMin.x + m_IconSize, iconMin.y + m_IconSize);

        ImVec4 folderColorF = ImGui::ColorConvertU32ToFloat4(folder.Color);
        if (icon && icon->IsLoaded())
            draw->AddImage((ImTextureID)(uintptr_t)icon->GetRendererID(),
                iconMin, iconMax, ImVec2(0, 1), ImVec2(1, 0),
                ImGui::ColorConvertFloat4ToU32(folderColorF));
        else
            draw->AddRectFilled(iconMin, iconMax, folder.Color, 4.0f);

        float textBlockY = itemPos.y + iconBlockH + 2.0f;
        auto lines = WrapNameToLines(folder.Name, totalW - 2.0f, kMaxNameLines);
        ImU32 textColor = selected ? IM_COL32(140, 190, 255, 255) :
            hovered ? IM_COL32(140, 180, 255, 255) :
            IM_COL32(220, 220, 220, 255);

        for (size_t li = 0; li < lines.size(); ++li)
        {
            float lineTextW = ImGui::CalcTextSize(lines[li].c_str()).x;
            float lineTextX = itemPos.x + (totalW - lineTextW) * 0.5f;
            draw->AddText(ImVec2(lineTextX, textBlockY + li * lineHeight), textColor, lines[li].c_str());
        }

        ImGui::NextColumn();
        ImGui::PopID();
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
        float  iconBlockH = totalW; // área do ícone (quadrada) + padding
        float  lineHeight = ImGui::GetTextLineHeight();
        const int kMaxNameLines = 3;
        float  textBlockH = lineHeight * kMaxNameLines + 4.0f;
        float  totalH = iconBlockH + textBlockH;

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

        // Nome — rename inline ou texto (com quebra de linha, sem truncar)
        float textBlockY = itemPos.y + iconBlockH + 2.0f;

        if (m_RenamingUUID == record.UUID)
        {
            ImGui::SetCursorScreenPos(ImVec2(itemPos.x, textBlockY));
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
            auto lines = WrapNameToLines(record.Name, totalW - 2.0f, kMaxNameLines);
            ImU32 textColor = selected ? IM_COL32(140, 190, 255, 255) :
                hovered ? IM_COL32(140, 180, 255, 255) :
                IM_COL32(200, 200, 200, 255);

            for (size_t li = 0; li < lines.size(); ++li)
            {
                float lineTextW = ImGui::CalcTextSize(lines[li].c_str()).x;
                float lineTextX = itemPos.x + (totalW - lineTextW) * 0.5f;
                draw->AddText(ImVec2(lineTextX, textBlockY + li * lineHeight), textColor, lines[li].c_str());
            }
        }

        ImGui::NextColumn();
        ImGui::PopID();
    }

    void AssetBrowser::DrawContextMenuEmpty()
    {
        DrawEmptyAreaContextMenu();
    }



} // namespace axe