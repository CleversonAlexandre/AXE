#include "asset_browser.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/anim_graph_asset.hpp"
#include "axe/audio/sound_cue.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "axe/audio/audio_clip.hpp"
#include "axe/audio/audio_engine.hpp"
#include "axe/animation/rig/control_rig_asset.hpp"
#include "axe/log/log.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/script/script_paths.hpp"
#include "axe/material/material_asset.hpp"
#include "editor/axe_editor/material/material_function.hpp"   // MATFUNC_V1
#include "axe/particles/particle_system_asset.hpp"
#include "editor/axe_editor/script/script_asset.hpp"

// BATCH_SHADING_MODEL_V1 — para o menu usar o ENUM em vez de indices
// digitados. Um "12" solto aqui e a mesma familia de bug das contagens
// hardcoded: no dia em que alguem reordenar MaterialShadingModel, o menu passa
// a gravar outro modelo em lote, em dezenas de materiais, sem erro nenhum.
#include "editor/axe_editor/node_graph/node_types.hpp"
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
        ".gltf", ".glb", ".obj", ".fbx", ".dae",   // .fbx: formato da Mixamo
        ".png", ".jpg", ".jpeg",
        ".axemat", ".axescene", ".axeskel", ".axeanim",  // .axeskel: personagem | .axeanim: state machine
        ".axerig",                                       // .axerig: control rig
        ".wav", ".mp3", ".flac",                         // audio
        ".axecue",                                       // sound cue
        ".axematfunc"                                    // MATFUNC_V1 — Material Function
    };

    // ═════════════════════════════════════════════════════════════════════════
    //  SC41 — FAIXA DE TIPO NA MINIATURA
    //
    //  ── O PROBLEMA ────────────────────────────────────────────────────────
    //
    //  Um personagem gera arquivos que sao a MESMA silhueta branca em fundo
    //  cinza: o .axeskel, o .axeanim, o FBX de origem e cada FBX de animacao.
    //  O rotulo com extensao (build M5) resolveu metade do problema — mas ler
    //  texto e um ato deliberado, e escolher um asset numa grade e um ato de
    //  RECONHECIMENTO. Cor se reconhece com o olho parado; texto, nao.
    //
    //  ── POR QUE UMA FAIXA, E NAO A BORDA OU O FUNDO ───────────────────────
    //
    //  A borda ja carrega SELECAO e HOVER. Somar tipo nela significaria que
    //  um item selecionado perde a cor de tipo, ou que a selecao deixa de ser
    //  obvia — e a selecao e a informacao mais urgente das duas. A faixa
    //  inferior e um canal proprio, que nao disputa com nada.
    //
    //  ── POR QUE SO ALGUNS TIPOS TEM COR ───────────────────────────────────
    //
    //  Colorir tudo e o mesmo que nao colorir nada. A faixa marca a familia
    //  ANIMACAO/LOGICA, que e onde a confusao acontece de verdade. Malha,
    //  textura, material e som ja se distinguem pela propria miniatura.
    //
    //  ── ONDE ISTO MORA ────────────────────────────────────────────────────
    //
    //  Local a este arquivo, de proposito. A regra do editor_widgets.hpp e
    //  promover o que tem DOIS usos reais; hoje o unico desenho de grade de
    //  assets e este. Quando o AssetPicker adotar a mesma faixa, a tabela
    //  sobe para axe::ui sem mudar de forma.
    // ═════════════════════════════════════════════════════════════════════════
    namespace
    {
        // Cores de FAMILIA, nao de arquivo. Os nomes dizem o papel do asset
        // no fluxo de animacao — e o que o autor tem na cabeca quando procura.
        constexpr ImU32 kAccentAnimation = IM_COL32(92, 201, 76, 255);   // clipe (FBX de animacao)
        constexpr ImU32 kAccentAnimLogic = IM_COL32(196, 104, 22, 255);  // .axeanim — state machine
        constexpr ImU32 kAccentControlRig = IM_COL32(224, 198, 44, 255);  // .axerig
        constexpr ImU32 kAccentScript = IM_COL32(62, 132, 224, 255);  // .axescript
        constexpr ImU32 kAccentSkeleton = IM_COL32(72, 178, 192, 255);  // .axeskel — o personagem

        // Reservadas: os conceitos existem na engine, mas ainda nao como ASSET
        // proprio. O BlendSpace1D e um NO dentro do AnimGraph; Montage/Slot e
        // um no planejado (ver anim_node.hpp). Ficam aqui escritas para que,
        // no dia em que virarem arquivo, a cor ja esteja decidida e nao seja
        // sorteada de novo.
        // constexpr ImU32 kAccentBlendSpace = IM_COL32(232, 142, 32,  255);
        // constexpr ImU32 kAccentMontage    = IM_COL32(152,  92, 204, 255);

        // 0 = SEM faixa. Nao e "transparente": o chamador pula o desenho.
        ImU32 AssetAccentColor(const AssetRecord& record, bool isAnimationSource)
        {
            // A animacao vem PRIMEIRO porque um FBX de animacao e, para o
            // AssetDatabase, um Mesh como qualquer outro — a classificacao
            // por extensao nunca chegaria nela.
            if (isAnimationSource)
                return kAccentAnimation;

            const std::string ext = record.FilePath.extension().string();

            if (ext == ".axeanim") return kAccentAnimLogic;
            if (ext == ".axerig")  return kAccentControlRig;
            if (ext == ".axeskel") return kAccentSkeleton;

            if (record.Type == AssetType::Script) return kAccentScript;

            return 0;
        }
    }

    // NOTA sobre esta lista: ela e um SEGUNDO portao de extensoes, paralelo
    // ao AssetTypeFromExtension do asset.hpp. O AssetDatabase::Scan ja
    // reconhecia audio ha muito tempo — o que barrava era esta lista aqui,
    // que decide o que o drag-and-drop e o "Import Asset..." aceitam.
    //
    // Ter duas listas e a causa raiz: acrescentar um tipo em asset.hpp nao
    // o torna importavel, e o sintoma e "o arquivo simplesmente nao entra",
    // sem log e sem erro. Unificar as duas e trabalho para um patch proprio
    // (mexe no contrato de quem decide o que e asset); registrado aqui para
    // nao virar folclore.



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
        // SC41: o conjunto de animacoes pode ter mudado.
        InvalidateAnimationSources();

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

    void AssetBrowser::RevealAsset(const std::string& uuid)
    {
        const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);

        if (!rec)
            return;

        m_SelectedFolder = rec->VirtualFolder;
        m_SelectedUUID = uuid;
        m_SearchBuffer[0] = '\0';   // busca ativa esconderia o asset revelado
    }

    void AssetBrowser::OnFileDrop(const std::string& filepath)
    {
        // SC41: o conjunto de animacoes pode ter mudado.
        InvalidateAnimationSources();

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
        // SC41: o conjunto de animacoes pode ter mudado.
        InvalidateAnimationSources();

        // Copia os dados necessários ANTES de remover do AssetDatabase —
        // 'record' é uma referência para dentro do map; depois do Unregister()
        // ela fica pendurada (dangling) e não pode mais ser usada.
        std::string uuid = record.UUID;
        std::string name = record.Name;
        auto filePath = record.FilePath;
        const bool isScript = (record.Type == AssetType::Script);

        try
        {
            // SC4 — script excluido leva junto o .cpp e a .dll gerados. Antes
            // eles ficavam para tras em bin/temp_scripts para sempre: nada no
            // editor sabia que existiam, e no projeto seguinte apareciam
            // misturados com os de outro projeto.
            //
            // Antes de apagar o arquivo do asset, porque se a DLL estiver
            // travada (carregada em Play) o remove falha e o aviso precisa
            // sair — apagar o .axescript primeiro deixaria o artefato orfao
            // sem ninguem para reclamar.
            if (isScript)
            {
                const std::string stem = filePath.stem().string();
                int removedArtifacts = ScriptPaths::RemoveArtifactsFor(stem, uuid);
                if (removedArtifacts == 0)
                    AXE_CORE_INFO("AssetBrowser: nenhum artefato compilado de '{}' para remover.", name);
            }

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
        // SC41: o conjunto de animacoes pode ter mudado.
        InvalidateAnimationSources();

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
            const std::filesystem::path oldPath = record.FilePath;

            AssetDatabase::Get().UpdatePath(record.UUID, newPath, newName);

            // Editores abertos com este arquivo seguram o caminho ANTIGO —
            // avisa antes que algum deles salve e recrie o arquivo velho.
            if (m_AssetRenamedCallback)
                m_AssetRenamedCallback(oldPath, newPath, newName);

            // Assets que guardam o PRÓPRIO nome dentro do JSON (separado do
            // nome do arquivo) precisam do campo atualizado no rename — senão
            // o editor continua exibindo o nome antigo e as duas verdades
            // divergem. Mesmo caso do .axemat logo abaixo.
            //
            // .axescript: o nome vira o nome da CLASSE gerada na compilação,
            // então deixar velho não é só cosmético — bagunça de verdade.
            {
                const std::string ext = newPath.extension().string();

                if (ext == ".axescript" || ext == ".axeanim" ||
                    ext == ".axeskel" || ext == ".axepart" ||
                    ext == ".axerig")
                {
                    try
                    {
                        std::ifstream in(newPath);

                        if (in.is_open())
                        {
                            nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
                            in.close();

                            if (!j.is_discarded() && j.contains("name"))
                            {
                                j["name"] = newName;

                                std::ofstream out(newPath);

                                if (out.is_open())
                                    out << j.dump(4);
                            }
                        }
                    }
                    catch (const std::exception& e)
                    {
                        AXE_CORE_WARN("Rename: nao consegui atualizar o nome dentro de '{}': {}",
                            newPath.string(), e.what());
                    }
                }
            }

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
        // SC41: o conjunto de animacoes pode ter mudado.
        InvalidateAnimationSources();

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

    // Carimbo de versao. Aparece UMA vez, no primeiro Draw.
    //
    // Serve pra responder, sem adivinhacao, a pergunta que custou varios
    // ciclos de compilacao: "este build tem o codigo novo ou nao?"
    static bool s_VersionLogged = false;

    void AssetBrowser::Draw()
    {
        if (!s_VersionLogged)
        {
            s_VersionLogged = true;
            AXE_EDITOR_INFO("AssetBrowser [build M5]: 'Criar AnimGraph' e rotulos com extensao ATIVOS.");
        }

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

        // SCENE_ASSET_V1 — confirmação de ABRIR CENA (ver a nota no duplo
        // clique). Mesma forma do modal de exclusão logo acima: o membro
        // guarda o pendente, e o modal existe enquanto ele não está vazio.
        //
        // Estas duas variaveis carregam a decisão até DEPOIS do `ImGui::End()`
        // — a troca de cena não pode acontecer no meio da pilha da janela.
        AssetRecord openScene;
        bool        openSceneRequested = false;

        if (!m_OpenSceneConfirmUUID.empty())
        {
            // O record pode ter sumido entre o clique e este frame (exclusão
            // pelo menu de contexto). Testado ANTES do OpenPopup: perguntar
            // "abrir a cena '?'" é pior que não perguntar nada.
            auto* rec = AssetDatabase::Get().GetByUUID(m_OpenSceneConfirmUUID);
            if (!rec)
                m_OpenSceneConfirmUUID.clear();
            else
                ImGui::OpenPopup("##confirm_open_scene");

            if (rec && ImGui::BeginPopupModal("##confirm_open_scene", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Abrir a cena '%s'?", rec->Name.c_str());
                ImGui::TextDisabled("A cena atual sera fechada. Alteracoes nao");
                ImGui::TextDisabled("salvas serao perdidas.");
                ImGui::Separator();

                if (ImGui::Button("Abrir", ImVec2(100, 0)))
                {
                    // ADIADO de proposito, e por uma razao concreta: este
                    // ponto esta ANTES do `ImGui::End()` da janela (linha do
                    // End mais abaixo). Chamar o callback aqui dentro e sair
                    // por um `return` deixaria a pilha do ImGui aberta —
                    // assert no frame seguinte. A copia tambem e necessaria:
                    // o callback recarrega a cena e pode mexer no banco, e
                    // `rec` aponta para dentro dele.
                    openScene = *rec;
                    openSceneRequested = true;

                    m_OpenSceneConfirmUUID.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancelar", ImVec2(100, 0)))
                {
                    m_OpenSceneConfirmUUID.clear();
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

        // BATCH_SHADING_MODEL_V1 — no MESMO nivel dos outros modais, e nao
        // dentro do popup de contexto: um BeginPopupModal aberto de dentro de
        // outro popup fecha junto com ele no frame seguinte, e o modal nunca
        // chega a aparecer.
        DrawShadingModelModals();

        ImGui::End();

        // SCENE_ASSET_V1 — DEPOIS do End, de proposito: e o unico ponto do
        // Draw fora da pilha do ImGui, e trocar a cena aberta reconstroi
        // meio editor. Ver a nota no botao "Abrir" do modal.
        if (openSceneRequested && m_AssetOpenCallback)
            m_AssetOpenCallback(openScene);
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
        // BATCH_SHADING_MODEL_V1 — a pasta inteira (e subpastas). E este o
        // caminho que resolve "o cenario todo em Toon".
        if (ImGui::BeginMenu("Shading Model dos materiais"))
        {
            DrawShadingModelMenu(CollectMaterialsInFolder(folderPath));
            ImGui::EndMenu();
        }
        ImGui::Separator();

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

    // ═════════════════════════════════════════════════════════════════════════
    //  BATCH_SHADING_MODEL_V1 — trocar o Shading Model de muitos materiais
    //
    //  Existe porque um jogo inteiro num estilo (toon, por exemplo) nao e o
    //  personagem: e o CENARIO. Marcar Toon abrindo cada `.axemat`, trocando o
    //  combo e clicando Compile e uma tarde de trabalho num projeto pequeno —
    //  e cada Compile e uma recompilacao de shader.
    // ═════════════════════════════════════════════════════════════════════════

    std::vector<std::string> AssetBrowser::CollectMaterialsInSelection() const
    {
        std::vector<std::string> out;

        auto add = [&](const std::string& uuid)
            {
                if (uuid.empty()) return;
                const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);
                if (!rec) return;

                // Pela EXTENSAO, e nao pelo record.Type. Type e estado derivado e
                // persistido: assets registrados antes de um tipo existir ficam com
                // o valor velho no indice. E a armadilha do ASSETTYPE_FIX_V1, e o
                // disco e a unica fonte que nao envelhece.
                if (rec->FilePath.extension() != ".axemat") return;

                if (std::find(out.begin(), out.end(), uuid) == out.end())
                    out.push_back(uuid);
            };

        add(m_SelectedUUID);
        for (const auto& u : m_SelectedUUIDs) add(u);
        return out;
    }

    std::vector<std::string> AssetBrowser::CollectMaterialsInFolder(const std::string& folderPath) const
    {
        std::vector<std::string> out;

        for (const auto& [uuid, rec] : AssetDatabase::Get().GetAll())
        {
            if (rec.FilePath.extension() != ".axemat") continue;

            // Subarvore, e nao so a pasta exata: quem organiza o cenario em
            // Assets/Cenario/Predios e Assets/Cenario/Rua espera que "aplicar
            // em Cenario" alcance os dois. O prefixo com "/" evita que
            // "Cenario" pegue tambem "CenarioAntigo".
            const bool inSubtree =
                rec.VirtualFolder == folderPath ||
                (rec.VirtualFolder.rfind(folderPath + "/", 0) == 0);

            if (inSubtree) out.push_back(uuid);
        }
        return out;
    }

    void AssetBrowser::DrawShadingModelMenu(const std::vector<std::string>& targets)
    {
        // So os REAIS. O enum da UI tem doze entradas, dez delas placeholders
        // da Unreal que o motor nao implementa — oferece-las aqui, em lote,
        // seria oferecer estragar dezenas de materiais de uma vez.
        struct Opt { const char* label; int value; };
        static const Opt kOpts[] = {
            { "Default Lit", (int)MaterialShadingModel::DefaultLit },
            { "Unlit",       (int)MaterialShadingModel::Unlit },
            { "Toon",        (int)MaterialShadingModel::Toon },
        };

        ImGui::BeginDisabled(targets.empty());

        for (const auto& opt : kOpts)
        {
            std::string label = std::string(opt.label);
            if (!targets.empty())
                label += "  (" + std::to_string((int)targets.size()) + ")";

            if (ImGui::MenuItem(label.c_str()))
            {
                m_PendingShadingUUIDs = targets;
                m_PendingShadingModel = opt.value;
                m_ShadingConfirmOpen = true;
            }
        }

        ImGui::EndDisabled();

        if (targets.empty())
            ImGui::TextDisabled("nenhum material aqui");
    }

    void AssetBrowser::DrawShadingModelModals()
    {
        if (m_ShadingConfirmOpen)
        {
            ImGui::OpenPopup("##shading_confirm");
            if (ImGui::BeginPopupModal("##shading_confirm", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                const char* modelName =
                    m_PendingShadingModel == (int)MaterialShadingModel::Unlit ? "Unlit" :
                    m_PendingShadingModel == (int)MaterialShadingModel::Toon ? "Toon" :
                    "Default Lit";

                ImGui::Text("Definir Shading Model = %s em %d material(is).",
                    modelName, (int)m_PendingShadingUUIDs.size());
                ImGui::Spacing();
                ImGui::TextDisabled("Cada material sera RECOMPILADO e o .axeshader");
                ImGui::TextDisabled("recozido. Com muitos materiais a janela trava");
                ImGui::TextDisabled("por um instante — e esperado.");
                ImGui::Spacing();
                ImGui::TextDisabled("Materiais que nao sao de dominio Surface sao");
                ImGui::TextDisabled("ignorados: Shading Model so existe la.");

                ImGui::Separator();
                ImGui::Spacing();

                if (ui::AccentButton("Aplicar", ui::Accent::Primary, nullptr, ImVec2(120, 0)))
                {
                    m_ShadingResultCount = m_BatchShadingModelCallback
                        ? m_BatchShadingModelCallback(m_PendingShadingUUIDs, m_PendingShadingModel)
                        : -1;

                    m_PendingShadingUUIDs.clear();
                    m_ShadingConfirmOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancelar", ImVec2(100, 0)))
                {
                    m_PendingShadingUUIDs.clear();
                    m_ShadingConfirmOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        if (m_ShadingResultCount >= 0)
        {
            ImGui::OpenPopup("##shading_result");
            if (ImGui::BeginPopupModal("##shading_result", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text(ICON_CHECK "  %d material(is) atualizado(s).", m_ShadingResultCount);
                ImGui::Spacing();
                ImGui::TextDisabled("Materiais ja no modelo pedido, ou de outro");
                ImGui::TextDisabled("dominio, nao entram na conta.");

                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(100, 0)))
                {
                    m_ShadingResultCount = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
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

        // BATCH_SHADING_MODEL_V1 — opera sobre a SELECAO, nao so sobre o item
        // clicado: com varios materiais selecionados o menu ja mostra a
        // contagem, e e esse o caso que a acao existe para resolver.
        if (record.FilePath.extension() == ".axemat")
        {
            if (ImGui::BeginMenu("Shading Model"))
            {
                DrawShadingModelMenu(CollectMaterialsInSelection());
                ImGui::EndMenu();
            }
        }

        // ── Importar personagem animado ──────────────────────────────────
        //
        // So aparece em arquivos de MALHA (fbx/gltf/dae/obj). O .fbx e uma
        // FONTE, nao um asset: este item gera o .axeskel ao lado dele, que e
        // o asset de verdade — e e o .axeskel que voce arrasta pra cena.
        // ── Criar Sound Cue a partir de um .wav ──────────────────────────
        //
        // Nasce a partir da wave, como o AnimGraph nasce do .axeskel: um cue
        // vazio nao teria nada pra tocar, e a primeira coisa que o usuario
        // faria seria arrastar uma wave pra dentro.
        //
        // Chaveado pela EXTENSAO, nao por record.Type — mesma razao ja
        // documentada abaixo: Type e estado derivado e pode estar velho.
        {
            // Pergunta ao AssetTypeFromExtension em vez de comparar literais.
            //
            // A versao anterior tinha `ext == ".wav" || ...` — que ignorava
            // "Step4_1.WAV" (o Windows preserva a caixa do nome) e criava uma
            // TERCEIRA lista de extensoes de audio no projeto, depois da do
            // asset.hpp e da s_SupportedExtensions logo acima. Uma funcao ja
            // sabe responder isso, e ja normaliza a caixa.
            if (AssetTypeFromExtension(record.FilePath.extension().string())
                == AssetType::Audio)
            {
                ImGui::Separator();

                if (ImGui::MenuItem("Criar Sound Cue"))
                {
                    auto cue = SoundCueAsset::Create(record.Name, record.UUID);

                    std::filesystem::path out = record.FilePath;
                    out.replace_extension(".axecue");

                    if (cue->Save(out))
                    {
                        const std::string newUuid = AssetDatabase::Get().Register(out);

                        if (auto* newRec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(newUuid)))
                            newRec->VirtualFolder = record.VirtualFolder;

                        if (ProjectManager::Get().HasProject())
                            AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

                        AXE_EDITOR_INFO("Sound Cue '{}' criado ja com variacao de pitch. "
                            "Aponte um Audio Source para ele.", record.Name);
                    }
                }

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cria um .axecue com Random + Modulator sobre este som.\n"
                        "Serve em qualquer lugar que aceite um audio.");
            }
        }

        // ── Criar AnimGraph a partir de um personagem ────────────────────
        //
        // Nasce a partir do .axeskel, e nao do nada, porque um AnimGraph SEM
        // esqueleto e inutil: o editor nao teria a lista de clipes pra
        // oferecer nos estados. O vinculo e gravado no .axeanim.
        // Chaveado pela EXTENSAO, e nao por record.Type.
        //
        // record.Type e estado DERIVADO, guardado no AssetDatabase e
        // persistido em disco. Se o banco tiver sido salvo por um build
        // antigo, o Type pode estar velho — e o item simplesmente nao
        // aparece, sem erro nenhum. A extensao do arquivo e a verdade.
        if (record.FilePath.extension() == ".axeskel")
        {
            ImGui::Separator();

            if (ImGui::MenuItem("Criar AnimGraph"))
            {
                auto graph = AnimGraphAsset::Create(record.Name, record.UUID);

                std::filesystem::path out = record.FilePath;
                out.replace_extension(".axeanim");

                if (graph->Save(out))
                {
                    const std::string newUuid = AssetDatabase::Get().Register(out);

                    if (auto* newRec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(newUuid)))
                        newRec->VirtualFolder = record.VirtualFolder;

                    if (ProjectManager::Get().HasProject())
                        AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

                    AXE_EDITOR_INFO("AnimGraph '{}' criado. Duplo-clique para abrir o editor.", record.Name);
                }
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cria uma state machine (.axeanim) ligada a este personagem.");

            // ── Criar Control Rig a partir do personagem ──────────────────
            //
            // O rig nasce POVOADO com os ossos do esqueleto. Um rig vazio nao
            // teria o que os nos referenciassem, entao precisamos CARREGAR o
            // .axeskel aqui — nao basta guardar o UUID.
            if (ImGui::MenuItem("Criar Control Rig"))
            {
                auto skelAsset = SkeletalMeshAsset::LoadFromFile(record.FilePath);

                const Skeleton* sk = (skelAsset && skelAsset->GetSkeleton())
                    ? skelAsset->GetSkeleton().get()
                    : nullptr;

                if (!sk)
                {
                    AXE_EDITOR_ERROR("Control Rig: '{}' nao tem esqueleto carregavel. "
                        "Reimporte o personagem antes.", record.Name);
                }
                else
                {
                    auto rig = ControlRigAsset::Create(record.Name, record.UUID, sk);

                    std::filesystem::path out = record.FilePath;
                    out.replace_extension(".axerig");

                    if (rig->Save(out))
                    {
                        const std::string newUuid = AssetDatabase::Get().Register(out);

                        if (auto* newRec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(newUuid)))
                            newRec->VirtualFolder = record.VirtualFolder;

                        if (ProjectManager::Get().HasProject())
                            AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

                        AXE_EDITOR_INFO("Control Rig '{}' criado com {} elementos. "
                            "Duplo-clique para abrir o editor.", record.Name, rig->GetHierarchy().Size());
                    }
                }
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cria um Control Rig (.axerig) com os ossos deste personagem.");
        }

        if (record.Type == AssetType::Mesh)
        {
            ImGui::Separator();

            // ── SC33: importar um clipe DIRETO no esqueleto ──────────────────
            //
            //  Um FBX so de animacao (Mixamo "Without Skin") nao pode virar
            //  asset sozinho: clipe nao existe sem esqueleto — os indices de
            //  bone de cada arquivo nao batem entre si, e o casamento e sempre
            //  por NOME contra um esqueleto alvo. Por isso o MeshLoader recusa
            //  o arquivo, e a recusa esta certa.
            //
            //  O que estava errado era o CAMINHO: a unica forma de importar era
            //  botar o personagem na cena, seleciona-lo e usar o Inspector. O
            //  Asset Browser, que e onde o arquivo esta, so sabia dizer nao.
            //
            //  Agora ele pergunta o que falta — QUAL esqueleto — e faz o
            //  import. A operacao ja existia inteira (AddAnimation + Save no
            //  .axeskel); faltava um lugar para chama-la.
            if (ImGui::BeginMenu("Importar animacao em..."))
            {
                bool anySkel = false;

                for (const auto& [uuid, rec] : AssetDatabase::Get().GetAll())
                {
                    if (rec.FilePath.extension() != ".axeskel") continue;
                    anySkel = true;

                    if (ImGui::MenuItem(rec.Name.c_str()))
                    {
                        auto target = SkeletalMeshAsset::LoadFromFile(rec.FilePath);

                        if (!target || !target->Resolve())
                        {
                            AXE_EDITOR_ERROR("Nao foi possivel abrir o esqueleto '{}'.", rec.Name);
                        }
                        else
                        {
                            // AddAnimation grava a REFERENCIA ao arquivo no
                            // .axeskel, e nao uma copia das curvas. Por isso o
                            // Save() logo em seguida: sem ele o clipe existe so
                            // nesta sessao e some ao fechar o editor.
                            const int added = target->AddAnimation(record.FilePath.string());

                            if (added <= 0)
                            {
                                AXE_EDITOR_ERROR("'{}' nao trouxe nenhum clipe para '{}'. "
                                    "Os nomes dos bones do arquivo precisam bater com os do "
                                    "esqueleto — canais sem bone correspondente sao descartados.",
                                    record.Name, rec.Name);
                            }
                            else if (target->Save(rec.FilePath))
                            {
                                AXE_EDITOR_INFO("{} clipe(s) de '{}' importado(s) em '{}'.",
                                    added, record.Name, rec.Name);

                                // SC41 — o FBX acabou de VIRAR animacao sem que
                                // nenhum arquivo tenha nascido ou morrido: o
                                // carimbo por tamanho do banco nao veria isso.
                                // A faixa verde precisa aparecer no mesmo gesto
                                // que a importou.
                                InvalidateAnimationSources();
                            }
                            else
                            {
                                AXE_EDITOR_ERROR("Clipes lidos, mas nao foi possivel gravar '{}'.",
                                    rec.FilePath.string());
                            }
                        }
                    }
                }

                if (!anySkel)
                    ImGui::TextDisabled("Nenhum .axeskel no projeto.");

                ImGui::EndMenu();
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("For a FBX with only curves (Mixamo 'Without Skin').\n"
                    "The clip is stored in the chosen skeleton's .axeskel.");

            if (ImGui::MenuItem("Importar como Skeletal Mesh"))
            {
                auto asset = SkeletalMeshAsset::Create(record.Name, record.FilePath);

                // Resolve JA: e aqui que descobrimos se o arquivo tem bones.
                // Falhar agora, com uma mensagem clara, e muito melhor do que
                // criar um .axeskel quebrado que so vai dar errado quando o
                // usuario arrastar pra cena.
                if (!asset->Resolve())
                {
                    AXE_EDITOR_ERROR("'{}' nao tem esqueleto — nao e um personagem animado. "
                        "Use como mesh estatica.", record.Name);
                }
                else
                {
                    std::filesystem::path out = record.FilePath;
                    out.replace_extension(".axeskel");

                    if (asset->Save(out))
                    {
                        const std::string newUuid = AssetDatabase::Get().Register(out);

                        // ── A PASTA VIRTUAL ──────────────────────────────
                        //
                        // A grade do browser filtra por `record.VirtualFolder`,
                        // NAO pelo caminho fisico do arquivo. E o Register()
                        // nao preenche esse campo — quem preenche e o browser,
                        // no fluxo normal de import.
                        //
                        // Sem isto, o .axeskel existe no disco, tem UUID, esta
                        // no banco... e some da grade, porque cai na pasta
                        // virtual RAIZ enquanto o usuario olha a pasta do
                        // personagem. O asset esta la — so num lugar onde
                        // ninguem vai procurar.
                        //
                        // Herda a pasta do FBX de origem: o .axeskel nasce ao
                        // lado dele no disco, entao tem que nascer ao lado dele
                        // na grade tambem.
                        if (auto* newRec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(newUuid)))
                            newRec->VirtualFolder = record.VirtualFolder;

                        if (ProjectManager::Get().HasProject())
                            AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

                        AXE_EDITOR_INFO("Skeletal Mesh '{}' criado: {} bones, {} clipe(s) embutido(s). "
                            "Arraste o .axeskel (nao o .fbx) para a cena.",
                            record.Name,
                            asset->GetSkeleton()->GetBoneCount(),
                            asset->GetClips().size());
                    }
                }
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Gera um asset .axeskel a partir deste arquivo.\n"
                    "Depois, arraste o .axeskel para a cena e importe\n"
                    "as animacoes pelo Inspector.");
            }
        }

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

            // MATFUNC_V1 — mesmos cinco passos do Material logo acima: caminho
            // unico, Create, Save, Register, VirtualFolder + Save do banco.
            //
            // Diferenca de um so ponto: o Material nasce SEM `.axegraph` (ele
            // so aparece no primeiro save do grafo), enquanto a funcao nasce
            // com o grafo dentro do proprio arquivo, ja com um Function Input
            // ligado num Function Output. Uma funcao vazia abriria numa tela em
            // branco sem nenhuma pista do que a torna uma funcao.
            if (ImGui::MenuItem("Material Function"))
            {
                auto fnPath = ProjectManager::Get().GetCurrent().AssetsPath
                    / "Materials" / "NewMaterialFunction.axematfunc";
                int k = 1;
                while (std::filesystem::exists(fnPath))
                    fnPath = ProjectManager::Get().GetCurrent().AssetsPath
                    / "Materials" / ("NewMaterialFunction_" + std::to_string(k++) + ".axematfunc");

                auto fnAsset = MaterialFunction::Create(fnPath.stem().string());
                if (fnAsset && fnAsset->GetGraph())
                    fnAsset->Save(fnPath, *fnAsset->GetGraph());

                auto fnUuid = AssetDatabase::Get().Register(fnPath.string());

                auto* fnRec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(fnUuid));
                if (fnRec) fnRec->VirtualFolder = m_SelectedFolder;

                if (ProjectManager::Get().HasProject())
                    AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);
            }

            if (ImGui::MenuItem("Particle System"))
            {
                auto partPath = ProjectManager::Get().GetCurrent().AssetsPath
                    / "Particles" / "NewParticleSystem.axepart";
                int i = 1;
                while (std::filesystem::exists(partPath))
                    partPath = ProjectManager::Get().GetCurrent().AssetsPath
                    / "Particles" / ("NewParticleSystem_" + std::to_string(i++) + ".axepart");

                auto partAsset = ParticleSystemAsset::Create(partPath.stem().string());
                partAsset->Save(partPath);
                auto uuid = AssetDatabase::Get().Register(partPath.string());

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
                "Assets\0*.png;*.jpg;*.jpeg;*.gltf;*.glb;*.obj;*.fbx;*.dae;*.axemat;*.wav;*.mp3;*.flac\0"
                "Audio\0*.wav;*.mp3;*.flac\0"
                "All Files\0*.*\0",
                "Import Asset");
            if (!path.empty())
                OnFileDrop(path.string());
        }
    }

    // ==================== Asset Grid ====================

    // ─────────────────────────────────────────────────────────────────────────
    //  SC41 — cruzamento .axeskel -> FBX de animacao
    //
    //  Um FBX so e "animacao" porque ALGUM personagem o lista como tal. Nao ha
    //  como saber isso pelo arquivo: um FBX de animacao da Mixamo e um FBX
    //  normal, sem malha. A verdade mora no .axeskel — entao e ele que se le.
    //
    //  ── SC42: por que NAO se compara o caminho na mao ─────────────────────
    //
    //  A primeira versao casava `entry.SourceFile` contra `record.FilePath` por
    //  string. Nunca casava nada: o .axeskel grava o caminho RELATIVO ao
    //  projeto (ver RelativizePath no Save), e o AssetRecord guarda o ABSOLUTO.
    //  Todas as animacoes ficavam sem faixa, e o sintoma — "so o verde nao
    //  aparece" — nao apontava para lugar nenhum, porque os outros tipos casam
    //  por extensao e nunca tocam em caminho.
    //
    //  Agora quem responde e o proprio asset, via FindAnimationEntryBySource:
    //  ele ja resolve relativo->absoluto, ja normaliza caixa e separador, e
    //  ainda casa por NOME DE ARQUIVO quando o FBX mudou de pasta. Reescrever
    //  essas tres regras aqui seria manter duas verdades sobre o mesmo assunto
    //  — e esta seria a que ninguem lembra de atualizar.
    //
    //  O FBX de origem do PROPRIO personagem (GetSourceFile) fica de fora de
    //  proposito: ele e a malha com skin, nao um clipe. Marcar os dois de
    //  verde reintroduziria exatamente a confusao que a faixa veio resolver —
    //  e ele fica de fora sozinho, porque nao esta em m_Animations.
    // ─────────────────────────────────────────────────────────────────────────
    // Ver a nota no ponto de uso. nullptr = o icone do tipo ja e unico.
    const char* AssetBrowser::AssetTypeTag(AssetType type)
    {
        switch (type)
        {
            // GameMode, ParticleSystem e Sequence SAIRAM: ganharam icone proprio, e
            // uma etiqueta sobre um desenho que ja e inconfundivel e so ruido.
            // A etiqueta e remendo enquanto falta arte — nao um enfeite permanente.
        case AssetType::SoundCue:       return "CUE";
        case AssetType::AnimationClip:  return "CLIP";
        case AssetType::SkeletalMesh:   return "SKEL";
        case AssetType::AnimGraph:      return "ANIM";
        case AssetType::ControlRig:     return "RIG";
        default:                        return nullptr;
        }
    }

    void AssetBrowser::EnsureAnimationSources()
    {
        auto& db = AssetDatabase::Get();
        const std::size_t stamp = db.GetAll().size();

        if (stamp == m_AnimSourcesStamp)
            return;

        m_AnimSourcesStamp = stamp;
        m_AnimationSourceUUIDs.clear();

        // Os esqueletos, uma vez. LoadFromFile so le JSON — o FBX so entra no
        // Resolve(), que nao acontece aqui e nao e necessario: as entradas de
        // animacao ja vem do arquivo.
        std::vector<std::shared_ptr<SkeletalMeshAsset>> skeletons;

        for (const auto& kv : db.GetAll())
        {
            if (kv.second.FilePath.extension() != ".axeskel")
                continue;

            if (auto skel = SkeletalMeshAsset::LoadFromFile(kv.second.FilePath))
                skeletons.push_back(skel);
            // .axeskel quebrado nao aborta a varredura: o personagem seguinte
            // pode estar inteiro.
        }

        if (skeletons.empty())
            return;

        for (const auto& kv : db.GetAll())
        {
            const AssetRecord& rec = kv.second;

            // So arquivo de modelo e candidato. Poupa uma varredura de
            // animacoes por textura, material e som do projeto.
            std::string ext = rec.FilePath.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char ch) { return (char)std::tolower(ch); });

            // `.axeclipbin` assado tambem: ele e uma animacao registrada num
            // `.axeskel` como qualquer outra, e merece a mesma faixa.
            if (ext != ".fbx" && ext != ".dae" && ext != ".gltf" && ext != ".glb" &&
                ext != ".axeclipbin")
                continue;

            for (const auto& skel : skeletons)
            {
                if (skel->FindAnimationEntryBySource(rec.FilePath) >= 0)
                {
                    m_AnimationSourceUUIDs.insert(rec.UUID);
                    break;   // um dono basta: a faixa e a mesma
                }
            }
        }

        AXE_EDITOR_INFO("AssetBrowser [SC42]: {} arquivo(s) marcado(s) como animacao "
            "({} esqueleto(s) consultado(s)).",
            (int)m_AnimationSourceUUIDs.size(), (int)skeletons.size());
    }

    void AssetBrowser::DrawAssetGrid()
    {
        // Antes de qualquer tile: a faixa de tipo precisa saber quais FBX sao
        // animacao. Sai por um `if` na maioria esmagadora dos frames.
        EnsureAnimationSources();

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

        // MATFUNC_V2 — a funcao entra pela MESMA porta do material. O
        // ThumbnailRenderer e quem sabe que uma delas precisa de um envelope
        // para virar shader; daqui as duas sao "um asset que rende esfera".
        //
        // O icone de fallback continua sendo o de material: enquanto a esfera
        // nao ficou pronta (o render acontece no OnRender, nao aqui), e ele que
        // aparece — e um `.axematfunc` com icone generico de arquivo era
        // exatamente a queixa.
        if (m_ThumbnailRenderer &&
            ((record.Type == AssetType::Material &&
                record.FilePath.extension() == ".axemat") ||
                (record.Type == AssetType::MaterialFunction &&
                    record.FilePath.extension() == ".axematfunc")))
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
            case AssetType::SoundCue: icon = icons.GetAudio(); /* TODO: icone dedicado */      break;
                // Os tres tem arte propria agora. O `? :` nao e zelo excessivo: se o
                // PNG nao estiver ao lado do executavel, o load devolve nullptr e
                // sem o fallback o asset ficaria com um quadrado cinza em vez do
                // icone antigo.
            case AssetType::GameMode:
                icon = icons.GetGameMode() ? icons.GetGameMode() : icons.GetScene();  break;
            case AssetType::ParticleSystem:
                icon = icons.GetParticle() ? icons.GetParticle() : icons.GetMesh();   break;
            case AssetType::Sequence:
                icon = icons.GetSequence() ? icons.GetSequence() : icons.GetScene();  break;
            case AssetType::AnimationClip: icon = icons.GetMesh();                              break;
            default:                  icon = icons.GetMesh();                                   break;
            }

            // ── SC23: miniatura do CONTEUDO, quando existir ──────────────────
            //
            // Depois do switch, e nao no lugar dele: o icone generico continua
            // sendo o que aparece enquanto a miniatura nao ficou pronta, e
            // continua sendo o fallback definitivo para um asset sem malha
            // (um script sem corpo, um clipe sem esqueleto ao lado). Zero
            // vindo do GetThumbnail significa "use o icone", nunca "espere".
            //
            // Malha, esqueleto, clipe de animacao e script entram; textura e
            // material ficam de fora porque ja tem miniatura propria, e audio
            // e cena nao tem forma para desenhar.
            const bool wantsMeshThumb =
                record.Type == AssetType::Mesh ||
                record.Type == AssetType::Script ||
                record.Type == AssetType::ControlRig ||   // SC32: personagem do rig
                record.Type == AssetType::AnimationClip ||
                record.FilePath.extension() == ".axeskel" ||
                record.FilePath.extension() == ".axeanim";

            if (wantsMeshThumb && m_MeshThumbnails)
            {
                m_MeshThumbnails->Register(record.UUID, record.FilePath, (int)record.Type);
                if (uint32_t id = m_MeshThumbnails->GetThumbnail(record.UUID))
                    overrideTextureID = id;
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

        bool selected = IsSelected(record.UUID);

        ImGui::InvisibleButton("##item", ImVec2(totalW, totalH));

        bool hovered = ImGui::IsItemHovered();
        bool dclicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        // Clique sobre o botao de tocar nao seleciona nem abre: e um gesto
        // proprio. Testado ANTES da selecao porque quem clica no play quer
        // ouvir, e nao navegar.
        const bool overPlay = (record.Type == AssetType::Audio)
            && (hovered || selected)
            && [&]()
            {
                const ImVec2 c(itemPos.x + padding + m_IconSize * 0.5f,
                    itemPos.y + padding + m_IconSize * 0.5f);
                const float r = std::max(12.0f, m_IconSize * 0.28f);
                const ImVec2 m = ImGui::GetIO().MousePos;
                const float dx = m.x - c.x, dy = m.y - c.y;

                return (dx * dx + dy * dy) <= r * r;
            }();

        if (overPlay && ImGui::IsItemClicked())
        {
            TogglePreview(record);
        }
        else if (ImGui::IsItemClicked())
        {
            // Ctrl+clique acumula; clique simples recomeca a selecao. E o
            // gesto que todo gerenciador de arquivos usa, entao ninguem
            // precisa aprender.
            if (ImGui::GetIO().KeyCtrl)
            {
                auto it = std::find(m_SelectedUUIDs.begin(), m_SelectedUUIDs.end(), record.UUID);

                if (it != m_SelectedUUIDs.end())
                    m_SelectedUUIDs.erase(it);
                else
                    m_SelectedUUIDs.push_back(record.UUID);
            }
            else
            {
                m_SelectedUUIDs.clear();
            }

            m_SelectedUUID = record.UUID;
        }

        // Drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            // Monta a lista arrastada: os multi-selecionados MAIS o item sob o
            // cursor, que pode nao estar na lista (arrastar um item sem
            // clicar nele antes e comum).
            std::vector<std::string> dragged = m_SelectedUUIDs;

            if (std::find(dragged.begin(), dragged.end(), record.UUID) == dragged.end())
                dragged.push_back(record.UUID);

            if (dragged.size() > 1)
            {
                // Payload de LISTA: UUIDs separados por '\n'.
                //
                // Tipo distinto de proposito. O ImGui so permite UM payload
                // por arrasto, e todos os alvos existentes (viewport, pastas,
                // slots de material) esperam ASSET_UUID e recebem UM asset.
                // Se a lista viesse com o mesmo nome, eles leriam a string
                // inteira como se fosse um UUID so. Com nome proprio, quem
                // nao entende simplesmente ignora — arrastar varios pra um
                // alvo de um nao faz nada, em vez de fazer errado.
                std::string joined;

                for (const auto& u : dragged)
                {
                    if (!joined.empty()) joined += '\n';
                    joined += u;
                }

                ImGui::SetDragDropPayload("ASSET_UUID_LIST", joined.c_str(), joined.size() + 1);
                ImGui::Text("%d assets", (int)dragged.size());
            }
            else
            {
                ImGui::SetDragDropPayload("ASSET_UUID", record.UUID.c_str(), record.UUID.size() + 1);

                if (icon && icon->IsLoaded())
                    ImGui::Image((ImTextureID)(uintptr_t)icon->GetRendererID(),
                        ImVec2(32, 32), ImVec2(0, 1), ImVec2(1, 0));

                ImGui::Text("%s", record.Name.c_str());
            }

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
            // ── SC34: personagem ABRE, nao instancia ─────────────────────
            //
            // O ramo generico abaixo chama os DOIS callbacks. Para um
            // .axeskel isso jogava o personagem na cena E abria a janela no
            // mesmo gesto — e o que se via era a instancia, porque ela e
            // visivel e a janela nasce atras. Editar o esqueleto exigia
            // apagar o que o duplo clique acabara de criar.
            //
            // Para pôr na cena continua havendo o arrastar, que e o gesto
            // explicito de "quero isto aqui".
            else if (record.FilePath.extension() == ".axeskel")
            {
                if (m_AssetOpenCallback) m_AssetOpenCallback(record);
            }
            // ── Sequence ABRE, nao instancia ─────────────────────────────
            //
            // Mesma razao do .axeskel logo acima: o ramo generico chama os
            // DOIS callbacks, e instanciar uma cutscene na cena nao quer
            // dizer nada — nao ha o que instanciar. Uma sequence so tem um
            // gesto util no duplo clique, que e abrir o Sequencer nela.
            else if (record.FilePath.extension() == ".axeseq")
            {
                if (m_AssetOpenCallback) m_AssetOpenCallback(record);
            }
            // Clipe assado: ABRE o Animation Editor do personagem dono, no
            // clipe certo. Instanciar uma curva na cena nao quer dizer nada —
            // mesma razao do .axeskel e do .axeseq.
            else if (record.Type == AssetType::AnimationClip)
            {
                if (m_AssetOpenCallback) m_AssetOpenCallback(record);
            }
            // MATFUNC_V1 — mesma razao do .axeskel, do .axeseq e do clipe
            // assado: o ramo generico chama os DOIS callbacks, e instanciar uma
            // Material Function na cena nao quer dizer nada. Ela nao e um
            // material — nao tem shader, nao tem dominio e nao se aplica a
            // malha nenhuma. O unico gesto util e abrir o editor nela.
            else if (record.Type == AssetType::MaterialFunction)
            {
                if (m_AssetOpenCallback) m_AssetOpenCallback(record);
            }
            // ── SCENE_ASSET_V1 — cena ABRE, e PERGUNTA antes ─────────────
            //
            // Duas coisas aqui, e a segunda e a que importa.
            //
            // 1) O ramo generico abaixo chama `m_InstantiateCallback` com o
            //    UUID. Instanciar uma CENA na cena nao quer dizer nada — e a
            //    mesma familia do .axeskel e do .axeseq. Enquanto o
            //    `.axescene` nao aparecia na grade (o bug que este V1
            //    conserta), ninguem tropecou nisso; agora tropecaria.
            //
            // 2) Abrir uma cena TROCA a cena aberta, e o editor nao tem hoje
            //    marca de "modificado" — nao ha como saber se ha trabalho nao
            //    salvo. Sem a pergunta, um duplo clique acidental na grade
            //    apagaria uma sessao inteira de edicao sem uma tela sequer.
            //
            //    Inventar aqui um rastreio de "cena suja" seria um sistema
            //    novo escondido dentro de um conserto de icone. A pergunta
            //    resolve o risco AGORA e continua correta no dia em que a
            //    marca existir — ela so passa a poder se calar quando a cena
            //    estiver limpa.
            else if (record.Type == AssetType::Scene)
            {
                m_OpenSceneConfirmUUID = record.UUID;
            }
            else
            {
                AXE_EDITOR_INFO("AssetBrowser: duplo-clique em '{}' (tipo {}, arquivo {}).",
                    record.Name, AssetTypeToString(record.Type),
                    record.FilePath.filename().string());

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

        // ── SC41: faixa de tipo ──────────────────────────────────────────
        //
        // DEPOIS da imagem, para ficar por cima dela; DENTRO do retangulo do
        // icone, para nao invadir o espaco do nome. Cantos arredondados so
        // embaixo, acompanhando a moldura.
        if (const ImU32 accent = AssetAccentColor(record,
            m_AnimationSourceUUIDs.count(record.UUID) > 0))
        {
            const float barH = std::max(3.0f, m_IconSize * 0.045f);

            draw->AddRectFilled(
                ImVec2(iconMin.x, iconMax.y - barH),
                ImVec2(iconMax.x, iconMax.y),
                accent, 4.0f, ImDrawFlags_RoundCornersBottom);
        }

        // ── ETIQUETA DE TIPO ─────────────────────────────────────────────
        //
        // ── POR QUE ISTO EXISTE ──────────────────────────────────────────
        //
        // Os icones acabaram. Ha mais TIPOS de asset do que PNGs no
        // editor_icon_library, e o que aconteceu foi o previsivel: Sound Cue
        // usa o icone de audio, GameMode e Sequence usam o de cena,
        // ParticleSystem e clipe assado usam o de malha. Cinco tipos, dois
        // desenhos — e a faixa colorida sozinha exige decorar um codigo de
        // cores.
        //
        // Tres letras resolvem sem nenhum asset novo, e resolvem melhor: um
        // icone dedicado para cada tipo novo seria um PNG a desenhar toda vez
        // que o engine ganhasse um formato, e a duvida ("qual e este?")
        // continuaria existindo ate o dia em que ele fosse desenhado.
        //
        // So aparece para quem COMPARTILHA icone. Malha, textura, som,
        // script, material e cena tem desenho proprio e reconhecivel — poluir
        // os seis para desambiguar os cinco seria trocar um problema por
        // outro maior.
        if (const char* tag = AssetTypeTag(record.Type))
        {
            const float pad = std::max(2.0f, m_IconSize * 0.04f);
            const ImVec2 ts = ImGui::CalcTextSize(tag);

            const ImVec2 tagMin(iconMin.x + pad, iconMin.y + pad);
            const ImVec2 tagMax(tagMin.x + ts.x + 6.0f, tagMin.y + ts.y + 2.0f);

            // Fundo escuro semi-opaco: a etiqueta cai por cima de miniaturas
            // claras e escuras, e texto puro some numa das duas.
            draw->AddRectFilled(tagMin, tagMax, IM_COL32(0, 0, 0, 170), 3.0f);
            draw->AddText(ImVec2(tagMin.x + 3.0f, tagMin.y + 1.0f),
                IM_COL32(235, 235, 235, 255), tag);
        }

        // Botao de tocar por cima do icone, so para audio.
        if (record.Type == AssetType::Audio)
            DrawAudioPlayOverlay(record, iconMin, iconMax, hovered, selected);

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
            // O nome exibido inclui a EXTENSAO para assets do engine.
            //
            // Um personagem gera tres arquivos com o MESMO nome:
            //   Y Bot (1).fbx      (fonte)
            //   Y Bot (1).axeskel  (o personagem)
            //   Y Bot (1).axeanim  (a state machine)
            //
            // Com o mesmo icone e o mesmo rotulo, e impossivel saber qual e
            // qual — e voce arrasta/clica no errado sem perceber.
            std::string displayName = record.Name;

            const std::string recExt = record.FilePath.extension().string();

            if (recExt == ".axeskel" || recExt == ".axeanim")
                displayName += recExt;

            auto lines = WrapNameToLines(displayName, totalW - 2.0f, kMaxNameLines);
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



    // ─────────────────────────────────────────────────────────────────────────
    //  Preview de audio no icone
    // ─────────────────────────────────────────────────────────────────────────
    void AssetBrowser::TogglePreview(const AssetRecord& record)
    {
        // Ja tocando ESTE som: para.
        if (m_PreviewVoice != 0 && m_PreviewUUID == record.UUID
            && AudioEngine::IsPlaying(m_PreviewVoice))
        {
            AudioEngine::Stop(m_PreviewVoice);
            m_PreviewVoice = 0;
            return;
        }

        // Tocando outro: para o anterior. Dois sons ao mesmo tempo aqui nao
        // ajudam a comparar nada — atrapalham.
        if (m_PreviewVoice != 0)
        {
            AudioEngine::Stop(m_PreviewVoice);
            m_PreviewVoice = 0;
        }

        // Guarda de tipo antes de decodificar: GetClip manda o arquivo direto
        // pro decoder do miniaudio, e um .axecue (que e JSON) faria ele falhar
        // com dois erros vermelhos que nao dizem nada sobre a causa.
        if (AssetTypeFromExtension(record.FilePath.extension().string()) != AssetType::Audio)
            return;

        auto clip = AudioEngine::GetClip(record.UUID);

        if (!clip || !clip->IsValid())
            return;

        // 2D e no bus Master: preview e escuta, nao mixagem. Passando pelo bus
        // de SFX, o volume mudaria conforme o slider do Mixer — e voce ouviria
        // "baixo" um arquivo que esta correto.
        VoiceParams p;
        p.Spatialized = false;
        p.Bus = AudioBus::Master;

        m_PreviewVoice = AudioEngine::Play(clip, p, false);
        m_PreviewUUID = record.UUID;
    }

    bool AssetBrowser::DrawAudioPlayOverlay(const AssetRecord& record,
        const ImVec2& iconMin, const ImVec2& iconMax,
        bool hovered, bool selected)
    {
        const bool playing = m_PreviewVoice != 0 && m_PreviewUUID == record.UUID
            && AudioEngine::IsPlaying(m_PreviewVoice);

        // Aparece no hover, na selecao, ou enquanto toca. O ultimo caso
        // importa: sem ele o botao de parar sumiria assim que o mouse saisse,
        // e o som continuaria sem controle visivel.
        if (!hovered && !selected && !playing)
            return false;

        ImDrawList* draw = ImGui::GetWindowDrawList();

        const ImVec2 c((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
        const float  r = std::max(12.0f, m_IconSize * 0.28f);

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float  dx = mouse.x - c.x, dy = mouse.y - c.y;
        const bool   over = (dx * dx + dy * dy) <= r * r;

        // Escurece o icone atras do botao: o alto-falante branco por baixo
        // engoliria o simbolo.
        draw->AddCircleFilled(c, r, IM_COL32(10, 12, 16, over ? 235 : 195), 32);
        draw->AddCircle(c, r, over ? IM_COL32(120, 190, 255, 255)
            : IM_COL32(200, 210, 225, 180), 32, 1.5f);

        const ImU32 fg = over ? IM_COL32(150, 210, 255, 255) : IM_COL32(235, 240, 250, 230);

        if (playing)
        {
            // Stop: quadrado. Formas diferentes em vez de so cores, pra
            // funcionar tambem pra quem nao distingue bem cor.
            const float h = r * 0.42f;
            draw->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), fg, 1.5f);
        }
        else
        {
            // Play: triangulo, deslocado meio pixel a direita porque um
            // triangulo centrado no centroide parece torto pra esquerda.
            const float h = r * 0.46f;
            draw->AddTriangleFilled(
                ImVec2(c.x - h * 0.6f + 1.0f, c.y - h),
                ImVec2(c.x - h * 0.6f + 1.0f, c.y + h),
                ImVec2(c.x + h + 1.0f, c.y), fg);
        }

        return over;
    }

} // namespace axe