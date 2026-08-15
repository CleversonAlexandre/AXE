#include "editor/axe_editor/asset/asset_report_window.hpp"

#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/log/log.hpp"

#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>    // std::snprintf — rotulo do cabecalho com a contagem

namespace axe
{
    namespace
    {
        // Nome legivel de um UUID. O Result guarda UUIDs porque e o que o grafo
        // manipula; nenhum ser humano le "a3f9-...".
        std::string Label(const std::string& uuid)
        {
            if (const AssetRecord* r = AssetDatabase::Get().GetByUUID(uuid))
                return r->Name + "   (" + AssetTypeToString(r->Type) + ")";

            return uuid + "   (not in database)";
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void AssetReportWindow::Open()
    {
        m_Open = true;

        // Roda na abertura: abrir e ver "clique para analisar" seria um passo a
        // mais para a unica coisa que a janela faz.
        Run();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void AssetReportWindow::Run()
    {
        std::vector<std::string> roots;

        m_StartSceneUnregistered = false;

        // ── Raizes ───────────────────────────────────────────────────────────
        //
        // O GameMode ativo entra SEMPRE. Ele e a raiz de verdade do jogo: puxa
        // o DefaultPawn, que puxa o script, o personagem, o AnimGraph e o rig.
        if (ProjectManager::Get().HasProject())
        {
            const std::string& gm = ProjectManager::Get().GetCurrent().ActiveGameModeUUID;

            if (!gm.empty())
                roots.push_back(gm);
        }

        // ── A cena inicial do projeto ────────────────────────────────────────
        //
        // `Project::StartScene` e a raiz de VERDADE: e literalmente a cena que
        // o jogo empacotado vai abrir. Melhor do que "todas as cenas
        // registradas" por dois motivos.
        //
        // O primeiro e semantico. O segundo apareceu na pratica: o
        // AssetDatabase::Load so faz Scan quando o `axe_assets.json` ainda NAO
        // existe. Uma cena criada depois disso nunca entra no indice — e a
        // primeira versao desta janela, que so olhava GetAllOfType(Scene),
        // simplesmente nao via cena nenhuma. O relatorio saia com metade do
        // projeto marcada como descartavel.
        //
        // Resolver pelo CAMINHO contorna isso: a cena entra como raiz mesmo
        // sem estar registrada.
        if (ProjectManager::Get().HasProject())
        {
            const Project& proj = ProjectManager::Get().GetCurrent();

            if (!proj.StartScene.empty())
            {
                const std::filesystem::path abs = proj.RootPath / proj.StartScene;

                if (const AssetRecord* rec = AssetDatabase::Get().GetByPath(abs))
                {
                    roots.push_back(rec->UUID);
                }
                else
                {
                    std::error_code ec;

                    // A cena existe no disco e nao esta no banco. Nao e caso de
                    // silencio: o relatorio inteiro sai errado, e a causa (um
                    // indice desatualizado) nao tem como ser adivinhada.
                    if (std::filesystem::exists(abs, ec))
                    {
                        AXE_CORE_WARN("AssetReport: start scene '{}' exists on disk but is "
                            "not in the AssetDatabase. The index is stale - reopen the "
                            "project to rescan.", proj.StartScene);

                        m_StartSceneUnregistered = true;
                    }
                }
            }
        }

        if (m_RootsAllScenes)
        {
            for (const AssetRecord* s : AssetDatabase::Get().GetAllOfType(AssetType::Scene))
                if (s) roots.push_back(s->UUID);
        }

        m_Result = AssetDependencyGraph::Collect(roots);

        // Ordena por nome para leitura.
        m_ReachableSorted.assign(m_Result.ReachableUUIDs.begin(),
            m_Result.ReachableUUIDs.end());

        auto byName = [](const std::string& a, const std::string& b)
            {
                return Lower(Label(a)) < Lower(Label(b));
            };

        std::sort(m_ReachableSorted.begin(), m_ReachableSorted.end(), byName);
        std::sort(m_Result.UnreachableUUIDs.begin(), m_Result.UnreachableUUIDs.end(), byName);

        m_HasResult = true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    void AssetReportWindow::DrawList(const char* label,
        const std::vector<std::string>& uuids,
        const ImVec4& accent,
        const char* emptyText)
    {
        const std::string filter = Lower(m_Filter);

        // Conta os que passam no filtro ANTES do cabecalho: um "Unused (12)"
        // com a lista mostrando 2 itens filtrados confunde mais do que ajuda.
        int shown = 0;

        for (const auto& u : uuids)
            if (filter.empty() || Lower(Label(u)).find(filter) != std::string::npos)
                ++shown;

        ImGui::PushStyleColor(ImGuiCol_Header, accent);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
            ImVec4(accent.x * 1.3f, accent.y * 1.3f, accent.z * 1.3f, 1.0f));

        char header[128];
        std::snprintf(header, sizeof(header), "%s  (%d)", label, (int)uuids.size());

        const bool open = ImGui::CollapsingHeader(header);

        ImGui::PopStyleColor(2);

        if (!open)
            return;

        ImGui::Indent(10.0f);

        if (uuids.empty())
        {
            ImGui::TextDisabled("%s", emptyText);
        }
        else if (!shown)
        {
            ImGui::TextDisabled("Nothing matches the filter.");
        }
        else
        {
            for (const auto& u : uuids)
            {
                const std::string text = Label(u);

                if (!filter.empty() && Lower(text).find(filter) == std::string::npos)
                    continue;

                ImGui::BulletText("%s", text.c_str());

                // O UUID cru no tooltip: e o que serve para procurar no JSON
                // quando algo nao bate.
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", u.c_str());
            }
        }

        ImGui::Unindent(10.0f);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void AssetReportWindow::DrawPackageSection()
    {
        // Fechada por padrao. O uso diario desta janela e ler o relatorio;
        // empacotar e ocasional, e um botao que escreve em disco nao deve estar
        // no caminho do olhar.
        if (!ImGui::CollapsingHeader("Package game"))
            return;

        ImGui::Indent(10.0f);

        // Sugestao de destino: IRMA da pasta do projeto, nunca dentro.
        //
        // O packager recusa destino dentro do projeto — com CleanOutput isso
        // apagaria o trabalho do usuario. Sugerir um caminho ja valido evita
        // que a primeira tentativa seja a recusada.
        if (m_PackageOutDir[0] == 0 && ProjectManager::Get().HasProject())
        {
            const Project& proj = ProjectManager::Get().GetCurrent();

            const std::string suggested =
                (proj.RootPath.parent_path() / (proj.Name + "_Build")).string();

            std::snprintf(m_PackageOutDir, sizeof(m_PackageOutDir), "%s", suggested.c_str());
        }

        // ── Assets fora do projeto (PKG2) ────────────────────────────────────
        //
        // ANTES dos campos de destino, e nao no meio dos resultados: se houver
        // asset externo, o pacote SAI INCOMPLETO, e o usuario precisa resolver
        // isto antes de clicar em empacotar — nao depois de ler 17 avisos.
        {
            const auto external = ProjectManager::Get().HasProject()
                ? AssetDatabase::Get().ExternalAssets(
                    ProjectManager::Get().GetCurrent().RootPath)
                : std::vector<const AssetRecord*>{};

            if (!external.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                    ICON_TRIANGLE_EXCLAMATION "  %d asset(s) live outside the project folder.",
                    (int)external.size());

                ImGui::TextWrapped(
                    "Dragging a file from outside registers its ORIGINAL path - nothing is "
                    "copied. Those assets cannot be packaged, and the project is not "
                    "portable as it is: moving the folder to another machine loses them.");

                ImGui::Spacing();

                if (ui::IconButton(ICON_FOLDER_OPEN,
                    "Copy them into Assets/Imported and update the index",
                    ui::Accent::Warning))
                {
                    const auto r = AssetDatabase::Get().ImportExternalAssets(
                        ProjectManager::Get().GetCurrent().RootPath);

                    AXE_EDITOR_INFO("Asset Report: {} external asset(s) imported, {} failure(s).",
                        r.Imported, r.Failures.size());

                    for (const auto& f : r.Failures)
                        AXE_EDITOR_ERROR("Asset Report: {}", f);

                    Run();
                }

                ImGui::SameLine();
                ImGui::TextDisabled("UUIDs are preserved - nothing needs to be reopened.");

                if (ImGui::TreeNode("##externallist", "Show the %d file(s)", (int)external.size()))
                {
                    for (const AssetRecord* rec : external)
                        ImGui::BulletText("%s", rec->FilePath.string().c_str());

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            }
        }

        ImGui::TextDisabled("Output folder (must be outside the project)");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##pkgout", m_PackageOutDir, sizeof(m_PackageOutDir));

        ImGui::TextDisabled("Binaries folder (game.exe + axe.dll) - leave empty to copy assets only");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##pkgbin", m_PackageBinDir, sizeof(m_PackageBinDir));

        ImGui::Checkbox("Clean the output folder first", &m_PackageClean);

        if (m_PackageClean)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                ICON_TRIANGLE_EXCLAMATION "  deletes everything in that folder");
        }

        ImGui::Spacing();

        if (ui::IconButton(ICON_SAVE, "Build the package", ui::Accent::Primary))
        {
            GamePackager::Options opt;
            opt.OutputDir = m_PackageOutDir;
            opt.BinariesDir = m_PackageBinDir;
            opt.CleanOutput = m_PackageClean;

            m_PackageResult = GamePackager::Package(opt);
            m_HasPackageResult = true;

            // Reflete no relatorio o que o pacote realmente levou: as raizes do
            // packager sao SO a cena inicial e o GameMode, e ver o relatorio de
            // "todas as cenas" ao lado de um pacote menor confundiria.
            Run();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Assets are copied as-is; the cooked files come from import.");

        if (m_HasPackageResult)
        {
            ImGui::Spacing();
            ImGui::Separator();

            const auto& r = m_PackageResult;

            if (r.Success)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f),
                    ICON_CHECK "  %d file(s), %.1f MB",
                    (int)r.FilesCopied, (double)r.BytesCopied / (1024.0 * 1024.0));
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                    ICON_TRIANGLE_EXCLAMATION "  failed - %d error(s)",
                    (int)r.Errors.size());
            }

            for (const auto& e : r.Errors)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "  %s", e.c_str());

            // Aviso nao impede o pacote, mas o usuario tem que ver: uma
            // referencia quebrada vira um buraco no jogo.
            for (const auto& w : r.Warnings)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "  %s", w.c_str());
        }

        ImGui::Unindent(10.0f);
        ImGui::Spacing();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void AssetReportWindow::Draw()
    {
        if (!m_Open)
            return;

        ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Asset Report###asset_report", &m_Open))
        {
            ImGui::End();
            return;
        }

        if (ui::IconButton(ICON_ROTATE_LEFT, "Re-analyze", ui::Accent::Primary))
            Run();

        ImGui::SameLine();

        // ── Rescan do indice ─────────────────────────────────────────────────
        //
        // O AssetDatabase::Load so varre a pasta quando o `axe_assets.json`
        // ainda nao existe. Dali em diante o indice e lido do arquivo, e um
        // asset criado fora do editor — ou uma cena salva antes de alguma
        // versao que registrava — fica invisivel para sempre.
        //
        // O Scan e aditivo (Register nao duplica UUID existente), entao repetir
        // e seguro: assets ja conhecidos mantem o UUID que tinham, e so os
        // ausentes entram.
        //
        // Este botao mora AQUI porque foi aqui que o problema apareceu, mas ele
        // e util muito alem desta janela.
        if (ui::IconButton(ICON_MAGNIFYING_GLASS, "Rescan the assets folder and reindex"))
        {
            if (ProjectManager::Get().HasProject())
            {
                const Project& proj = ProjectManager::Get().GetCurrent();

                AssetDatabase::Get().Scan(proj.RootPath / "Assets");
                AssetDatabase::Get().Save(proj.RootPath);

                Run();
            }
        }

        ImGui::SameLine();
        ui::ToolbarSeparator();
        ImGui::SameLine();

        if (ImGui::Checkbox("Include all scenes as roots", &m_RootsAllScenes))
            Run();

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "On: starts from the active GameMode AND every scene.\n"
                "Off: active GameMode only - closer to what the packaged\n"
                "game would load, but marks as unused every scene that is\n"
                "not wired to it yet.");
        }

        ImGui::Separator();

        if (!m_HasResult)
        {
            ImGui::TextDisabled("No analysis yet.");
            ImGui::End();
            return;
        }

        // ── Resumo ───────────────────────────────────────────────────────────
        ImGui::Text("%d used   |   %d unused   |   %d file(s) to copy",
            (int)m_Result.ReachableUUIDs.size(),
            (int)m_Result.UnreachableUUIDs.size(),
            (int)m_Result.Files.size());

        if (m_StartSceneUnregistered)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                ICON_CIRCLE_INFO "  The start scene is not in the asset index - "
                "reopen the project to rescan. This report is incomplete.");
        }

        // Referencia quebrada e ERRO, e por isso ganha destaque em vez de virar
        // mais uma linha do resumo: ela ja esta quebrando alguma coisa hoje, no
        // editor, independente de empacotamento.
        if (!m_Result.MissingUUIDs.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                ICON_CIRCLE_INFO "  %d reference(s) point to a missing asset.",
                (int)m_Result.MissingUUIDs.size());
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##assetreportfilter", "Filter by name...",
            m_Filter, sizeof(m_Filter));

        ImGui::Separator();

        DrawPackageSection();

        ImGui::BeginChild("##assetreportlists");

        // ── Raizes ───────────────────────────────────────────────────────────
        //
        // Primeiro da lista de proposito. Uma raiz que parseou e nao referencia
        // nada e a explicacao mais provavel para um relatorio que "esqueceu"
        // metade do projeto — e sem esta secao o sintoma e mudo.
        {
            int silent = 0;

            for (const auto& r : m_Result.Roots)
                if (r.Parsed && r.DirectRefs == 0)
                    ++silent;

            const ImVec4 accent = silent
                ? ImVec4(0.55f, 0.35f, 0.12f, 1.0f)
                : ImVec4(0.20f, 0.30f, 0.45f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Header, accent);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                ImVec4(accent.x * 1.3f, accent.y * 1.3f, accent.z * 1.3f, 1.0f));

            char header[128];
            std::snprintf(header, sizeof(header), "Roots  (%d)",
                (int)m_Result.Roots.size());

            // Abre sozinha quando ha raiz silenciosa: e o caso em que o usuario
            // precisa ver isto sem procurar.
            const bool open = ImGui::CollapsingHeader(header,
                silent ? ImGuiTreeNodeFlags_DefaultOpen : 0);

            ImGui::PopStyleColor(2);

            if (open)
            {
                ImGui::Indent(10.0f);

                if (silent)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                        "%d root(s) reference nothing. If a scene is open with "
                        "unsaved changes, save it (Ctrl+S) and re-analyze - this "
                        "report reads the files on disk.", silent);
                    ImGui::Spacing();
                }

                for (const auto& r : m_Result.Roots)
                {
                    const char* label = r.Name.empty() ? r.UUID.c_str() : r.Name.c_str();

                    if (!r.Parsed)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                            "  %s  -  could not be read", label);
                    }
                    else if (r.DirectRefs == 0)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                            "  %s  -  0 references", label);
                    }
                    else
                    {
                        ImGui::BulletText("%s  -  %d direct reference(s)",
                            label, (int)r.DirectRefs);
                    }
                }

                ImGui::Unindent(10.0f);
            }
        }

        DrawList("Broken references", m_Result.MissingUUIDs,
            ImVec4(0.55f, 0.20f, 0.18f, 1.0f),
            "None - the project is intact.");

        DrawList("Unused", m_Result.UnreachableUUIDs,
            ImVec4(0.50f, 0.42f, 0.16f, 1.0f),
            "None - everything in the project is reachable.");

        DrawList("Used", m_ReachableSorted,
            ImVec4(0.18f, 0.42f, 0.28f, 1.0f),
            "None - check that the project has an active GameMode and scenes.");

        ImGui::EndChild();

        ImGui::End();
    }

} // namespace axe