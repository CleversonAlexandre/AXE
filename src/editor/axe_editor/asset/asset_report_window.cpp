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