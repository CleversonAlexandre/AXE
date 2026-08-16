// script_graph_window.cpp
// Entrada principal — inicialização, abertura/fechamento, Draw (host window + dockspace),
// CompileScript, SaveNodePositions, Undo/Redo.
// O restante está dividido em:
//   script_preview.cpp      — preview 3D (InitPreviewScene, Sync*, RenderPreview, etc.)
//   script_node_graph.cpp   — DrawGraphWindow / DrawNodeGraph (context menu, links, drops)
//   script_node_draw.cpp    — DrawNode (renderização individual de cada node)
//   script_members.cpp      — DrawMyBlueprintWindow (Variables, Override Events, Dispatchers)
//   script_details.cpp      — DrawDetailsWindow / DrawScriptDetails / DrawSceneGraphWindow

#include "script_graph_window.hpp"
#include "editor/axe_editor/script/script_asset.hpp"
#include <nlohmann/json.hpp>
#include "editor/axe_editor/script/script_graph.hpp"
#include "editor/axe_editor/script/script_graph_compiler.hpp"
#include "axe/script/script_compiler.hpp"
#include "axe/script/script_paths.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/log/log.hpp"
#include "editor/axe_editor/editor_icon_library.hpp"
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>   // SC16: gizmo do preview conta como gesto em andamento
#include <imgui_node_editor.h>
#include <sstream>
#include <cstdio>   // snprintf nos rotulos do console
#include <fstream>
#include <filesystem>
#include <cmath>   // SC10: isfinite na guarda de posicao
#include <windows.h>

namespace ed = ax::NodeEditor;

namespace axe
{


    // ─────────────────────────────────────────────────────────────────────────
    ScriptGraphWindow::ScriptGraphWindow() = default;
    ScriptGraphWindow::~ScriptGraphWindow() { Shutdown(); }

    void ScriptGraphWindow::Initialize()
    {
        AXE_EDITOR_INFO("Script Editor — SCRIPT_SKELETAL_V2 (include fix + root transform + anim path)");

        ed::Config cfg;
        cfg.SettingsFile = nullptr;
        m_EdCtx = ed::CreateEditor(&cfg);
    }

    void ScriptGraphWindow::Shutdown()
    {
        if (m_EdCtx) { ed::DestroyEditor(m_EdCtx); m_EdCtx = nullptr; }
        m_PreviewRenderer.reset();
        m_PreviewFramebuffer.reset();
        m_CameraPreviewEntity = entt::null;
        m_PreviewScene.reset();
        m_PreviewEnvironment.reset();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::OpenForAsset(std::shared_ptr<ScriptAsset> asset)
    {
        if (!asset) return;
        m_ScriptAsset = asset;
        m_Graph = asset->GetGraph().get();
        m_EditingFunctionIndex = -1; // abrir um novo asset sempre começa no grafo principal
        m_IsOpen = true;
        m_FirstFrame = true;
        m_CtxBuf[0] = m_CompSearchBuf[0] = '\0';
        m_ConsoleLines.clear();
        m_ConsoleLines.push_back("[Script Editor] " + asset->GetName() + " - " +
            ScriptClassTypeToString(asset->GetClassType()));

        if (!m_PreviewRenderer)
            InitPreviewScene();
        SyncMeshFromAsset();
        SyncComponentsToPreview();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::HandleAssetRenamed(const std::filesystem::path& oldPath,
        const std::filesystem::path& newPath, const std::string& newName)
    {
        if (!m_ScriptAsset)
            return;

        // Comparacao textual: o arquivo antigo ja NAO EXISTE neste ponto
        // (o rename ja acabou), entao equivalent() nao serve.
        if (m_ScriptAsset->GetFilePath() != oldPath)
            return;

        m_ScriptAsset->SetFilePath(newPath);
        m_ScriptAsset->SetName(newName);

        m_ConsoleLines.push_back("[Info] Script renomeado para: " + newName);
    }

    void ScriptGraphWindow::SwitchToMainGraph()
    {
        if (!m_ScriptAsset || m_EditingFunctionIndex < 0) return; // já é o grafo principal
        SaveNodePositions(); // salva posições do grafo da função antes de saída
        m_Graph = m_ScriptAsset->GetGraph().get();
        m_EditingFunctionIndex = -1;
        m_SelectedVar = -1;
        m_LastCanvasSelectedNode = {};
        m_FirstFrame = true; // recentra a câmera no grafo recém-aberto
    }

    void ScriptGraphWindow::SwitchToFunctionGraph(ScriptFunction* func)
    {
        if (!func || !func->Graph || !m_ScriptAsset) return;
        // Converte o ponteiro (transiente — válido só neste frame, antes de
        // qualquer Add/RemoveFunction) pro índice estável que de fato fica
        // guardado entre frames (ver comentário de m_EditingFunctionIndex
        // no header — um ponteiro guardado sobreviveria a uma realocação do
        // vector<ScriptFunction> e apontaria pra lixo).
        auto& funcs = m_ScriptAsset->GetFunctions();
        int idx = -1;
        for (int i = 0; i < (int)funcs.size(); i++) if (&funcs[i] == func) { idx = i; break; }
        if (idx < 0 || m_EditingFunctionIndex == idx) return;

        SaveNodePositions(); // salva posições do grafo anterior antes de saída
        m_Graph = func->Graph.get();
        m_EditingFunctionIndex = idx;
        m_SelectedVar = -1;
        m_LastCanvasSelectedNode = {};
        m_FirstFrame = true;
    }

    void ScriptGraphWindow::RebuildFunctionCallSites(ScriptFunction& func)
    {
        if (!func.Graph) return;

        // 1) Function Entry / Return Node — vivem no PRÓPRIO grafo da função
        for (auto& n : func.Graph->GetNodes())
            if (n->Name == "Function Entry" || n->Name == "Return Node")
                func.Graph->RebuildFunctionNodePins(n.get(), func);

        if (!m_ScriptAsset) return;

        // 2) Qualquer node "Call <func.Name>" no grafo PRINCIPAL
        auto mainGraph = m_ScriptAsset->GetGraph();
        if (mainGraph)
            for (auto& n : mainGraph->GetNodes())
                if (n->Category == ScriptNodeCategory::Function && n->StringValue == func.Name &&
                    n->Name != "Function Entry" && n->Name != "Return Node")
                    mainGraph->RebuildFunctionNodePins(n.get(), func);

        // 3) Idem dentro de QUALQUER OUTRA função (cobre recursão e funções
        //    chamando outras funções)
        for (auto& other : m_ScriptAsset->GetFunctions())
        {
            if (!other.Graph) continue;
            for (auto& n : other.Graph->GetNodes())
                if (n->Category == ScriptNodeCategory::Function && n->StringValue == func.Name &&
                    n->Name != "Function Entry" && n->Name != "Return Node")
                    other.Graph->RebuildFunctionNodePins(n.get(), func);
        }
    }

    void ScriptGraphWindow::Close()
    {
        m_IsOpen = false;
        m_Graph = nullptr;
        m_EditingFunctionIndex = -1;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Draw — janela host com DockSpace interno
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::Draw()
    {
        if (!m_IsOpen || !m_Graph) return;

        ImGui::SetNextWindowSize(ImVec2(1280, 820), ImGuiCond_FirstUseEver);
        // SC5 — "-" no lugar do travessao U+2014. A fonte de texto do editor e
        // a ProggyClean embutida do ImGui (io.Fonts->AddFontDefault), que so
        // cobre ate U+00FF; qualquer coisa acima disso o ImGui desenha como
        // "?" (o FallbackChar dele). Era exatamente o "Script Editor ?
        // BP_Player" da aba. Nao da pra consertar acrescentando range: a
        // ProggyClean e bitmap e simplesmente nao tem os glifos. A saida certa
        // de longo prazo e carregar uma TTF de texto de verdade em
        // resources/fonts e trocar o AddFontDefault por ela — enquanto isso,
        // ASCII.
        // S0a — o fallback para m_Component saiu junto com OpenForEntity: sem
        // asset nao ha grafo, e sem grafo o Draw ja retornou la em cima.
        std::string title = "Script Editor - " +
            (m_ScriptAsset ? m_ScriptAsset->GetName() : "?") + "###ScriptEditorHost";

        // SC9 — a toolbar saiu da MENU BAR e passou para o CORPO da janela.
        //
        // Tentei duas vezes dar respiro aos icones mexendo no FramePadding
        // antes do Begin, e as duas erraram: a menu bar do ImGui calcula a
        // propria altura a partir do estilo no instante do Begin, mas o
        // conteudo dela e posicionado por outra conta, e o resultado foi o
        // icone cortado em cima em vez de centrado. Nao ha, dentro da menu
        // bar, um lugar onde eu controle os dois.
        //
        // O Control Rig — que e a referencia que voce pediu desde o inicio —
        // nunca usou menu bar: ele chama DrawToolbar() no corpo e depois o
        // DockSpace. No corpo, o espaco acima e abaixo e um Dummy meu, e
        // portanto exato. Menos mecanismo, mais controle.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 3.f));
        bool vis = ImGui::Begin(title.c_str(), &m_IsOpen,
            ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        if (!vis) { ImGui::End(); return; }


        // ── Toolbar (corpo da janela, nao menu bar) ──────────────────────────
        {
            // Respiro ACIMA dos icones. Numero explicito: e o unico jeito de
            // isto ser previsivel.
            ImGui::Dummy(ImVec2(0, 4.0f));

            // ── Toolbar ──────────────────────────────────────────────────────
            //
            // SC5 — antes daqui saía uma lambda `iconButton` de ~25 linhas que
            // desenhava um botão vazio, colava a textura do ícone por cima com
            // AddImage e escrevia o rótulo ao lado, tudo à mão. Era o
            // ui::IconButton reescrito, pior e só nesta janela — o Control Rig
            // já fazia a mesma barra com uma linha por botão.
            //
            // A troca também muda a FONTE do ícone: de PNG carregado pela
            // EditorIconLibrary para o glifo Font Awesome mesclado na fonte de
            // texto. Um glifo escala com a fonte, herda a cor do tema e não
            // custa uma textura por botão. (O Material Graph continua na
            // EditorIconLibrary; migrar aquele é outro patch, e as texturas
            // seguem em uso até lá.)
            //
            // Rótulo virou tooltip: ler cinco palavras em fila é mais lento do
            // que reconhecer cinco formas — mesmo argumento registrado na
            // toolbar do rig.

            // ── Compilar: o botao CONTA em que estado o script esta ──────────
            //
            //   verde   raio       compilado e em dia
            //   ambar   triangulo  o grafo mudou desde a ultima compilacao
            //   vermelho bug       a ultima compilacao falhou
            //
            // "Sujo" vence "falhou" de proposito. Quando a compilacao quebra, o
            // passo seguinte e editar para consertar — e nesse instante o erro
            // vermelho passa a descrever uma versao do grafo que nao existe
            // mais. Manter o vermelho ali seria informacao vencida ocupando o
            // lugar da atual, que e "isto ainda nao foi compilado".
            {
                const char* compileIcon = ICON_BOLT;
                ui::Accent  compileAccent = ui::Accent::Add;
                const char* compileTip = "Compiled and up to date  (click to rebuild)";

                if (m_GraphDirty)
                {
                    compileIcon = ICON_TRIANGLE_EXCLAMATION;
                    compileAccent = ui::Accent::Warning;
                    compileTip = "The graph changed since the last build.\nCompile to bring the C++ up to date.";
                }
                else if (m_LastCompileFailed)
                {
                    compileIcon = ICON_BUG;
                    compileAccent = ui::Accent::Danger;
                    compileTip = "The last build failed.\nSee the Script Console for the error.";
                }

                if (ui::IconButton(compileIcon, compileTip, compileAccent))
                    CompileScript();
            }

            ImGui::SameLine();

            // O DISCO manda no nome. Renomear no Asset Browser troca o
            // arquivo (e o campo "name" dentro dele), mas o asset ja carregado
            // aqui continuaria com o nome velho — e o proximo Save
            // regravaria o antigo por cima. Adotar o stem do arquivo resolve
            // pelos dois lados e nao custa nada por frame.
            if (m_ScriptAsset && !m_ScriptAsset->GetFilePath().empty())
            {
                const std::string stem = m_ScriptAsset->GetFilePath().stem().string();

                if (!stem.empty() && stem != m_ScriptAsset->GetName())
                    m_ScriptAsset->SetName(stem);
            }

            const bool canSave = m_ScriptAsset && !m_ScriptAsset->GetFilePath().empty();
            ImGui::BeginDisabled(!canSave);
            if (ui::IconButton(ICON_SAVE, "Save the script  (Ctrl+S)", ui::Accent::Primary))
            {
                SaveNodePositions();
                m_ScriptAsset->Save(m_ScriptAsset->GetFilePath());
                m_ConsoleLines.push_back("[Info] Script saved: " + m_ScriptAsset->GetFilePath().string());

                // Propaga pras instancias na cena (o "Compile" da Unreal):
                // sem isto, mudar o BP so vale pros proximos spawns e o autor
                // tem que apagar e recolocar tudo que ja esta na cena.
                if (m_ScriptSavedCallback)
                    m_ScriptSavedCallback(m_ScriptAsset->GetFilePath());
            }
            ImGui::EndDisabled();

            ui::ToolbarSeparator();

            // Undo / Redo. Tooltip com o NOME da ação — "desfazer o quê?" é a
            // dúvida real, e antes isso era um texto solto na barra que podia
            // passar por botão.
            {
                const bool canUndo = CanUndo();
                ImGui::BeginDisabled(!canUndo);
                if (ui::IconButton(ICON_UNDO, nullptr)) Undo();
                ImGui::EndDisabled();
                if (canUndo && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Undo: %s  (Ctrl+Z)", m_History.GetUndoName().c_str());

                ImGui::SameLine();

                const bool canRedo = CanRedo();
                ImGui::BeginDisabled(!canRedo);
                if (ui::IconButton(ICON_REDO, nullptr)) Redo();
                ImGui::EndDisabled();
                if (canRedo && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Redo: %s  (Ctrl+Shift+Z / Ctrl+Y)", m_History.GetRedoName().c_str());
            }

            ui::ToolbarSeparator();

            if (ui::IconButton(ICON_EXPAND, "Frame the whole graph"))
            {
                ed::SetCurrentEditor(m_EdCtx);
                ed::NavigateToContent();
                ed::SetCurrentEditor(nullptr);
            }

            ImGui::SameLine(0, 8);
            // "-" e nao "—": a fonte de texto do editor e a ProggyClean padrao
            // do ImGui, que so tem ate U+00FF. Todo travessao U+2014 do editor
            // sai como "?" na tela (era o "Script Editor ? BP_Player" do
            // titulo e o "BP_Player ? Character" do console). Ver o comentario
            // no titulo, mais acima.
            ImGui::TextDisabled("%s", m_ScriptAsset ? m_ScriptAsset->GetName().c_str() : "-");

            // ── Breadcrumb de Function ───────────────────────────────────────────
            // Sem isso, trocar de grafo ao clicar numa Function no Script Members
            // acontecia silenciosamente — nada na tela avisava que o canvas agora
            // mostra o grafo DELA, não o grafo principal do script. Visível só
            // quando m_EditingFunctionIndex >= 0 (ver SwitchToFunctionGraph).
            if (m_EditingFunctionIndex >= 0 && m_ScriptAsset)
            {
                auto& funcs = m_ScriptAsset->GetFunctions();
                if (m_EditingFunctionIndex < (int)funcs.size())
                {
                    ImGui::SameLine(0, 14);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.85f, 0.78f, 1));
                    ImGui::Text(ICON_FUNCTION "  Function: %s", funcs[m_EditingFunctionIndex].Name.c_str());
                    ImGui::PopStyleColor();

                    ImGui::SameLine(0, 8);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.28f, 0.3f, 1));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.38f, 0.4f, 1));
                    if (ImGui::SmallButton("< Back to main graph"))
                        SwitchToMainGraph();
                    ImGui::PopStyleColor(2);
                }
            }

            if (m_MsgTimer > 0)
            {
                m_MsgTimer -= ImGui::GetIO().DeltaTime;
                ImGui::SameLine(0, 16);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    m_MsgOk ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.3f, 0.3f, 1));
                ImGui::TextUnformatted(m_Msg.c_str());
                ImGui::PopStyleColor();
            }
            // Respiro ABAIXO dos icones, antes do separador.
            ImGui::Dummy(ImVec2(0, 4.0f));
            ImGui::Separator();
        }

        // Respiro visual entre a toolbar e o conteúdo do dock abaixo.
        ImGui::Dummy(ImVec2(0, 4.0f));

        // ── DockSpace ────────────────────────────────────────────────────────
        ImGuiID dsId = ImGui::GetID("ScriptEditorDockSpace");
        ImGuiDockNode* existingNode = ImGui::DockBuilderGetNode(dsId);

        if (!m_LayoutBuilt && (!existingNode || existingNode->IsEmpty()))
        {
            m_LayoutBuilt = true;

            // Altura util do dockspace = janela menos o que a toolbar ja
            // consumiu. Antes isto era "GetFrameHeight + padding + kToolbarGap",
            // uma reconstrucao a mao da altura da menu bar — e kToolbarGap
            // deixou de existir quando a toolbar saiu dela (SC9).
            //
            // GetCursorPosY() ja e exatamente onde o conteudo comeca depois da
            // toolbar, seja ela qual for. Uma conta a menos para errar quando o
            // respiro dos Dummy mudar.
            ImVec2 sz = ImGui::GetWindowSize();
            sz.y -= ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
            if (sz.x < 10.f) sz.x = 1280.f;
            if (sz.y < 10.f) sz.y = 780.f;

            ImGui::DockBuilderRemoveNode(dsId);
            ImGui::DockBuilderAddNode(dsId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dsId, sz);

            ImGuiID dLeft, dCenter;
            ImGui::DockBuilderSplitNode(dsId, ImGuiDir_Left, 0.18f, &dLeft, &dCenter);
            ImGuiID dGraph, dRight;
            ImGui::DockBuilderSplitNode(dCenter, ImGuiDir_Right, 0.20f, &dRight, &dGraph);
            ImGuiID dGraphTop, dConsole;
            ImGui::DockBuilderSplitNode(dGraph, ImGuiDir_Down, 0.22f, &dConsole, &dGraphTop);
            ImGuiID dDetails, dMyBP;
            ImGui::DockBuilderSplitNode(dRight, ImGuiDir_Down, 0.50f, &dMyBP, &dDetails);

            ImGui::DockBuilderDockWindow("Script Preview", dLeft);
            ImGui::DockBuilderDockWindow("Scene Graph", dLeft);
            ImGui::DockBuilderDockWindow("Script Graph", dGraphTop);
            ImGui::DockBuilderDockWindow("Script Console", dConsole);
            ImGui::DockBuilderDockWindow("Script Details", dDetails);
            ImGui::DockBuilderDockWindow("Script Members", dMyBP);
            ImGui::DockBuilderFinish(dsId);
        }
        else if (!m_LayoutBuilt)
        {
            m_LayoutBuilt = true;
        }

        ImGui::DockSpace(dsId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

        DrawPreviewWindow();
        DrawSceneGraphWindow();
        DrawGraphWindow();
        DrawConsoleWindow();
        DrawDetailsWindow();
        DrawMyBlueprintWindow();

        // ── Atalhos de teclado ───────────────────────────────────────────────
        //
        // SC13 — ficavam LOGO DEPOIS do Begin, antes de qualquer painel. Foi o
        // que faltava comparar com o Control Rig: la eles rodam no FIM do
        // Draw, depois de toda a UI submetida.
        //
        // A diferenca importa para uma coisa so, mas e justo a que nao
        // funcionava. Rodando no inicio, um Ctrl+Z restaurava o modelo e o
        // canvas do MESMO frame ja tentava aplicar as posicoes — durante o
        // frame em que o node-editor ainda estava resolvendo o proprio estado
        // do node. Undo de variavel sobrevivia a isso (nao depende do canvas);
        // undo de POSICAO nao, porque o valor era escrito e reescrito no mesmo
        // frame. Rodando no fim, a restauracao cai num frame limpo, no topo do
        // proximo ed::Begin — exatamente onde o rig aplica o
        // m_NodePositionsLoaded dele.
        //
        // SC8 — este bloco ficava ANTES do Begin(), e era por isso que o Ctrl+Z
        // "nao funcionava": IsWindowFocused() responde sobre a janela CORRENTE,
        // e antes do Begin a janela corrente e outra qualquer — a resposta nao
        // tinha nada a ver com o Script Editor.
        //
        // E o flag tambem estava errado. ChildWindows cobre BeginChild; os
        // paineis daqui (Script Graph, Details, Members) sao janelas
        // ANCORADAS, que para o ImGui sao janelas de topo dentro do dockspace.
        // Quem cobre isso e DockHierarchy. Sem ele, so o foco no host contava —
        // e o host quase nunca esta em foco, porque o clique cai sempre num
        // painel.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows |
            ImGuiFocusedFlags_DockHierarchy))
        {
            ImGuiIO& io = ImGui::GetIO();

            // WantTextInput: com o cursor dentro de um campo de texto, Ctrl+Z
            // pertence ao campo, nao ao grafo.
            if (io.KeyCtrl && !io.WantTextInput)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    // Ctrl+Shift+Z e o redo que faltava: a barra mostra
                    // "(Ctrl+Shift+Z)" no tooltip do Redo desde o SC5, mas so
                    // Ctrl+Y estava ligado.
                    if (io.KeyShift) Redo();
                    else             Undo();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();

                if (ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveScript();
                if (ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelectedNodes(false);
                if (ImGui::IsKeyPressed(ImGuiKey_X, false)) CopySelectedNodes(true);
                if (ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteNodes();
                if (ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelectedNodes();
            }
        }

        // SC7 — depois de TODOS os painéis, uma vez por frame. Precisa vir por
        // último: uma edição feita no Details ou no Members ainda não teria
        // acontecido se isto rodasse antes deles, e o passo sairia com o
        // estado errado do lado "depois".
        CommitPendingUndo();

        ImGui::End();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────
    //  SC15 — Console com filtro por severidade e texto selecionavel
    //
    //  A severidade e derivada do PREFIXO da linha ("[ERROR]", "[WARN]"), e nao
    //  de um campo guardado junto. Foi a escolha barata: as mensagens ja nascem
    //  com esse prefixo em todo o editor, e mudar m_ConsoleLines para uma
    //  estrutura obrigaria a tocar as ~40 chamadas de push_back espalhadas em
    //  cinco arquivos. Se um dia a severidade precisar de mais que cor e
    //  filtro — hora, origem, link para o node — aí a estrutura se paga; hoje
    //  nao se pagava.
    // ─────────────────────────────────────────────────────────────────────────

    static ScriptGraphWindow::ConsoleSeverity ClassifyConsoleLine(const std::string& s)
    {
        if (s.find("[ERROR]") != std::string::npos) return ScriptGraphWindow::ConsoleSeverity::Error;
        if (s.find("[WARN]") != std::string::npos ||
            s.find("[Aviso]") != std::string::npos) return ScriptGraphWindow::ConsoleSeverity::Warning;
        return ScriptGraphWindow::ConsoleSeverity::Info;
    }

    void ScriptGraphWindow::DrawConsoleWindow()
    {
        using Sev = ConsoleSeverity;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.07f, 1));
        if (ImGui::Begin("Script Console"))
        {
            // Contagens por severidade, para os rotulos das abas. Uma passada
            // por frame sobre algumas centenas de strings e irrelevante perto
            // de desenha-las.
            int nInfo = 0, nWarn = 0, nErr = 0;
            for (const auto& l : m_ConsoleLines)
                switch (ClassifyConsoleLine(l))
                {
                case Sev::Error:   nErr++;  break;
                case Sev::Warning: nWarn++; break;
                default:           nInfo++; break;
                }

            // ── Filtros ──────────────────────────────────────────────────────
            //
            // ToggleButton e nao TabBar: uma aba obriga a escolher UMA
            // categoria, e o caso mais util do console e "erros e avisos, sem
            // o resto". Com botoes que ligam e desligam, isso e um clique.
            const bool allOn = m_ConsoleShowInfo && m_ConsoleShowWarn && m_ConsoleShowError;
            if (ui::ToggleButton("All", allOn, "Show every message"))
                m_ConsoleShowInfo = m_ConsoleShowWarn = m_ConsoleShowError = true;
            ImGui::SameLine();

            char buf[48];
            snprintf(buf, sizeof(buf), ICON_CIRCLE_INFO "  Info (%d)", nInfo);
            if (ui::ToggleButton(buf, m_ConsoleShowInfo, "Show informational messages"))
                m_ConsoleShowInfo = !m_ConsoleShowInfo;
            ImGui::SameLine();

            snprintf(buf, sizeof(buf), ICON_TRIANGLE_EXCLAMATION "  Warn (%d)", nWarn);
            if (ui::ToggleButton(buf, m_ConsoleShowWarn, "Show warnings", ui::Accent::Warning))
                m_ConsoleShowWarn = !m_ConsoleShowWarn;
            ImGui::SameLine();

            snprintf(buf, sizeof(buf), ICON_BUG "  Error (%d)", nErr);
            if (ui::ToggleButton(buf, m_ConsoleShowError, "Show errors", ui::Accent::Danger))
                m_ConsoleShowError = !m_ConsoleShowError;

            ImGui::SameLine(0, 16);
            if (ui::IconButton(ICON_COPY, "Copy visible lines to the clipboard"))
            {
                std::string all;
                for (const auto& l : m_ConsoleLines)
                {
                    const Sev sv = ClassifyConsoleLine(l);
                    if (sv == Sev::Error && !m_ConsoleShowError) continue;
                    if (sv == Sev::Warning && !m_ConsoleShowWarn)  continue;
                    if (sv == Sev::Info && !m_ConsoleShowInfo)   continue;
                    all += l;
                    all += '\n';
                }
                ImGui::SetClipboardText(all.c_str());
            }
            ImGui::SameLine();
            if (ui::IconButton(ICON_TRASH, "Clear the console", ui::Accent::Danger))
                m_ConsoleLines.clear();

            ImGui::Separator();

            // ── Linhas ───────────────────────────────────────────────────────
            //
            // Selecionaveis, e nao TextUnformatted: o ImGui nao tem selecao de
            // texto em rotulo estatico. Selectable da o realce e o clique; para
            // COPIAR de verdade existe o botao acima (linhas visiveis) e o
            // clique duplo (uma linha).
            ImGui::BeginChild("##consolelines", ImVec2(0, 0), false,
                ImGuiWindowFlags_HorizontalScrollbar);

            for (int i = 0; i < (int)m_ConsoleLines.size(); i++)
            {
                const std::string& line = m_ConsoleLines[i];
                const Sev sv = ClassifyConsoleLine(line);

                if (sv == Sev::Error && !m_ConsoleShowError) continue;
                if (sv == Sev::Warning && !m_ConsoleShowWarn)  continue;
                if (sv == Sev::Info && !m_ConsoleShowInfo)   continue;

                ImGui::PushStyleColor(ImGuiCol_Text,
                    sv == Sev::Error ? ImVec4(1.f, .35f, .35f, 1.f) :
                    sv == Sev::Warning ? ImVec4(1.f, .8f, .25f, 1.f) :
                    ImVec4(.82f, .82f, .82f, 1.f));

                ImGui::PushID(i);
                ImGui::Selectable(line.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    ImGui::SetClipboardText(line.c_str());
                    m_Msg = "Line copied"; m_MsgOk = true; m_MsgTimer = 2.0f;
                }
                ImGui::PopID();
                ImGui::PopStyleColor();
            }

            // So rola sozinho se ja estiver no fim: rolar para ler uma
            // mensagem antiga e ser puxado de volta a cada linha nova e o
            // motivo pelo qual consoles com autoscroll incondicional sao
            // inutilizaveis durante uma compilacao longa.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Undo / Redo — snapshot based
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::PushUndo(const std::string& actionName)
    {
        if (!m_ScriptAsset) return;
        SaveNodePositions();
        m_SnapshotBeforeAction = m_ScriptAsset->SaveToString();
    }

    void ScriptGraphWindow::CommitUndo(const std::string& actionName)
    {
        if (!m_ScriptAsset || m_SnapshotBeforeAction.empty()) return;
        SaveNodePositions();
        std::string snapBefore = m_SnapshotBeforeAction;
        std::string snapAfter = m_ScriptAsset->SaveToString();
        m_SnapshotBeforeAction.clear();

        // SC6 — o grafo mudou, logo o C++ compilado ficou velho. Aqui e nao nos
        // chamadores: este e o unico ponto por onde toda mutacao passa.
        m_GraphDirty = true;

        Command cmd;
        cmd.Name = actionName;
        cmd.Execute = nullptr;
        cmd.Undo = [this, snapBefore, snapAfter]()
            {
                m_PendingUndoSnapshot = snapBefore;
                m_PendingRedoSnapshot = snapAfter;
            };
        m_History.Push(std::move(cmd));

        // SC7 — o estado confirmado avancou. Um baseline velho aqui faria o
        // proximo passo aberto por MarkEdited voltar demais, engolindo esta
        // acao que ja tem passo proprio.
        m_Baseline = snapAfter;

        // Um gesto pendente perdeu a razao de existir: a mudanca que ele ia
        // registrar acabou de entrar no historico por outro caminho.
        m_PendingUndo = false;
        m_PendingUndoName.clear();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  SC7 — undo por gesto
    // ─────────────────────────────────────────────────────────────────────────

    void ScriptGraphWindow::RefreshBaseline()
    {
        if (!m_ScriptAsset) { m_Baseline.clear(); return; }
        SaveNodePositions();
        m_Baseline = m_ScriptAsset->SaveToString();
        m_PendingUndo = false;
        m_PendingUndoName.clear();
    }

    void ScriptGraphWindow::MarkEdited(const char* actionName)
    {
        m_GraphDirty = true;

        // Um PushUndo em andamento ja esta cuidando deste trecho: abrir um
        // segundo passo por cima produziria dois registros para uma acao so.
        if (!m_SnapshotBeforeAction.empty()) return;

        // So a PRIMEIRA chamada nomeia o passo. As seguintes, dentro do mesmo
        // gesto, sao no-op de proposito.
        if (!m_PendingUndo)
        {
            m_PendingUndo = true;
            m_PendingUndoName = actionName ? actionName : "Edit";
        }
    }

    void ScriptGraphWindow::CommitNodeDrag()
    {
        if (!m_ScriptAsset || !m_Graph || m_DragStartPositions.empty()) return;

        // O "antes" nao e uma foto tirada por acaso: e reconstruido de
        // proposito. As posicoes do modelo sao devolvidas ao que eram no
        // clique, o snapshot e tirado desse estado, e so entao as posicoes
        // atuais do canvas entram. Assim before/after diferem EXATAMENTE no
        // arrasto, e em mais nada.
        std::vector<ImVec2> current;
        current.reserve(m_Graph->GetNodes().size());

        for (auto& n : m_Graph->GetNodes())
        {
            current.push_back(n->Position);
            for (const auto& pr : m_DragStartPositions)
                if (pr.first == (int)n->ID.Get()) { n->Position = pr.second; break; }
        }

        // PushUndo nao serve aqui: ele chama SaveNodePositions() e traria as
        // posicoes NOVAS do canvas de volta, apagando o "antes" que acabamos de
        // montar. O snapshot e tirado direto.
        m_SnapshotBeforeAction = m_ScriptAsset->SaveToString();

        // Restaura o que o modelo tinha e sincroniza com o canvas.
        {
            size_t i = 0;
            for (auto& n : m_Graph->GetNodes())
                if (i < current.size()) n->Position = current[i++];
        }
        SaveNodePositions();

        CommitUndo("Move Node");

    }

    void ScriptGraphWindow::CommitPendingUndo()
    {
        if (!m_ScriptAsset) return;

        // ── ESPERA O GESTO TERMINAR ──────────────────────────────────────────
        //
        // IsAnyItemActive cobre campo de texto e drag de widget. O botao do
        // mouse ainda pressionado cobre o ARRASTO DE NODE, que e o caso que
        // motivou tudo isto: o node-editor trata o proprio input, entao
        // IsAnyItemActive fica FALSO enquanto voce arrasta um node — sem esta
        // segunda condicao, atravessar o canvas com um node viraria um passo
        // de undo por frame.
        //
        // O fio na mao (Ctrl+arraste) tambem e gesto em andamento, e o passo
        // dele ja tem PushUndo proprio.
        const bool anyActive = ImGui::IsAnyItemActive();

        // SC16 — enquanto um widget esta em uso, guarda de QUAL painel ele e.
        // ActiveIdWindow e do imgui_internal, ja incluido aqui. Serve so para
        // o passo de undo ter nome ("Edit in Script Details" em vez de
        // "Edit"), que e o que aparece no tooltip do botao Undo.
        if (anyActive)
        {
            if (ImGuiContext* g = ImGui::GetCurrentContext())
                if (g->ActiveIdWindow && g->ActiveIdWindow->Name)
                    m_ActiveWidgetWindow = g->ActiveIdWindow->Name;
        }

        // O gizmo do preview nao e um item do ImGui: arrastar a seta do
        // ImGuizmo deixa IsAnyItemActive() em falso o tempo todo. Sem
        // contabiliza-lo, mover o objeto no preview geraria um passo de undo
        // por frame — e o mesmo cuidado esta escrito no Control Rig.
        const bool gizmoActive = ImGuizmo::IsUsing();
        const bool interacting = anyActive || gizmoActive;

        // Borda de desativacao: algo estava em uso no frame passado e nao esta
        // mais. E o instante exato em que uma edicao de painel terminou —
        // qualquer painel, qualquer widget, inclusive os que nunca chamaram
        // MarkEdited.
        const bool justDeactivated = m_AnyItemActiveLastFrame && !interacting;
        m_AnyItemActiveLastFrame = interacting;

        const bool gestureActive =
            interacting ||
            ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
            m_CarryingWire;

        // Primeira passada do asset: semeia o baseline em vez de esperar uma
        // edicao. Se ficasse para depois, o PRIMEIRO gesto do usuario acharia
        // o baseline vazio e nao viraria passo de undo nenhum — e "o primeiro
        // Ctrl+Z nao faz nada" e o tipo de defeito que so aparece quando ja
        // esta atrapalhando. Seguro aqui porque as posicoes dos nodes ja
        // foram aplicadas ao canvas neste mesmo frame.
        if (m_Baseline.empty())
        {
            if (!gestureActive) RefreshBaseline();
            return;
        }

        // Duas portas de entrada:
        //   • m_PendingUndo  — alguem chamou MarkEdited explicitamente
        //   • justDeactivated — um widget qualquer acabou de ser solto
        //
        // A segunda e a que da cobertura automatica. Ela dispara tambem quando
        // NADA mudou (soltar um botao, fechar um combo sem escolher); nesses
        // casos a comparacao abaixo nao encontra diferenca e nenhum passo e
        // criado. O custo de um SaveToString por interacao encerrada e
        // aceitavel — nao ha um por frame.
        if (!(m_PendingUndo || justDeactivated) || gestureActive) return;

        SaveNodePositions();
        const std::string after = m_ScriptAsset->SaveToString();

        if (m_Baseline != after)
        {
            const std::string before = m_Baseline;

            // Nome: o explicito manda; sem ele, o painel onde a edicao
            // aconteceu. "Edit in Script Details" nao diz o campo, mas diz
            // muito mais do que "Edit" — e e derivado, entao nao envelhece
            // quando um campo novo aparece.
            std::string name = m_PendingUndoName;
            if (name.empty())
                name = m_ActiveWidgetWindow.empty()
                ? "Edit"
                : ("Edit in " + m_ActiveWidgetWindow);

            Command cmd;
            cmd.Name = name;
            cmd.Execute = nullptr;
            cmd.Undo = [this, before, after]()
                {
                    m_PendingUndoSnapshot = before;
                    m_PendingRedoSnapshot = after;
                };
            m_History.Push(std::move(cmd));
        }

        m_Baseline = after;
        m_PendingUndo = false;
        m_PendingUndoName.clear();

        // Nome do painel e valido so para o passo que acabou de fechar; manter
        // faria o proximo passo generico herdar um painel onde nada aconteceu.
        m_ActiveWidgetWindow.clear();
    }

    void ScriptGraphWindow::Undo()
    {
        // SC10 — eco no console quando nao ha o que desfazer.
        //
        // Serve de diagnostico: se o atalho NAO estiver chegando aqui, o
        // console fica mudo, e isso distingue "o Ctrl+Z nao dispara" de "o
        // Ctrl+Z dispara mas o historico esta vazio" — duas causas com
        // consertos completamente diferentes, e ate agora nao havia como
        // saber qual das duas era.
        if (!CanUndo())
        {
            m_ConsoleLines.push_back("[Info] Nothing to undo.");
            return;
        }

        // Desfazer tambem deixa o grafo diferente do que foi compilado.
        m_GraphDirty = true;
        m_PendingUndoSnapshot.clear();
        m_PendingRedoSnapshot.clear();
        m_History.Undo();
        if (!m_PendingUndoSnapshot.empty() && m_ScriptAsset)
        {
            const std::string snap = m_PendingUndoSnapshot;
            m_PendingUndoSnapshot.clear();
            RestoreFromSnapshot(snap);
        }
    }

    void ScriptGraphWindow::Redo()
    {
        if (!CanRedo())
        {
            m_ConsoleLines.push_back("[Info] Nothing to redo.");
            return;
        }
        m_GraphDirty = true;
        if (!m_PendingRedoSnapshot.empty() && m_ScriptAsset)
        {
            const std::string snap = m_PendingRedoSnapshot;
            m_PendingRedoSnapshot.clear();
            RestoreFromSnapshot(snap);
        }
    }

    void ScriptGraphWindow::RestoreFromSnapshot(const std::string& snapshot)
    {
        if (!m_ScriptAsset) return;

        // ── SC9: fica no grafo em que o autor estava ─────────────────────────
        //
        // Undo e Redo jogavam m_EditingFunctionIndex para -1 e devolviam o
        // autor ao grafo principal. Editando uma Function, cada Ctrl+Z
        // significava sair dela e entrar de novo — e a edicao seguinte
        // recomecava a conta.
        //
        // O indice e reconferido depois da restauracao porque o proprio undo
        // pode ter sido a criacao daquela Function: nesse caso ela nao existe
        // mais no snapshot restaurado, e insistir nela apontaria para lixo.
        const int wantedFunc = m_EditingFunctionIndex;

        m_ScriptAsset->LoadFromString(snapshot);

        auto& funcs = m_ScriptAsset->GetFunctions();
        if (wantedFunc >= 0 && wantedFunc < (int)funcs.size() && funcs[wantedFunc].Graph)
        {
            m_EditingFunctionIndex = wantedFunc;
            m_Graph = funcs[wantedFunc].Graph.get();
        }
        else
        {
            m_EditingFunctionIndex = -1;
            m_Graph = m_ScriptAsset->GetGraph().get();
        }

        SyncComponentsToPreview();

        // O canvas ainda tem as posicoes de ANTES da restauracao; sem isto o
        // proprio detector de arrasto desfaz o undo no frame seguinte.
        m_PendingPositionSync = true;

        // O estado atual voltou no tempo, entao o "antes" tem de voltar junto.
        //
        // SC14 — direto, e NUNCA via RefreshBaseline() aqui. RefreshBaseline
        // chama SaveNodePositions(), que le o CANVAS — e neste exato ponto o
        // canvas ainda esta com as posicoes de antes do undo (o sync so roda
        // no proximo frame, e o flag acima). Ler o canvas agora gravava as
        // posicoes novas por cima das que o snapshot acabou de restaurar: o
        // proprio undo se desfazia, uma linha depois de ter funcionado. Era
        // exatamente o que o log mostrava — "re-applied to N node(s)" com os
        // valores que nunca mudaram.
        //
        // O baseline correto e o proprio snapshot: e literalmente o estado
        // atual do modelo, sem passar pelo canvas.
        m_Baseline = snapshot;
        m_PendingUndo = false;
        m_PendingUndoName.clear();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  SC8 — copiar / colar / duplicar nodes
    // ─────────────────────────────────────────────────────────────────────────

    void ScriptGraphWindow::SaveScript()
    {
        if (!m_ScriptAsset || m_ScriptAsset->GetFilePath().empty()) return;
        SaveNodePositions();
        m_ScriptAsset->Save(m_ScriptAsset->GetFilePath());
        m_ConsoleLines.push_back("[Info] Script saved: " + m_ScriptAsset->GetFilePath().string());
        if (m_ScriptSavedCallback)
            m_ScriptSavedCallback(m_ScriptAsset->GetFilePath());
    }

    void ScriptGraphWindow::CopySelectedNodes(bool cut)
    {
        if (!m_Graph || !m_EdCtx) return;

        ed::SetCurrentEditor(m_EdCtx);
        const int count = ed::GetSelectedObjectCount();
        std::vector<ed::NodeId> sel(count > 0 ? count : 1);
        const int got = count > 0 ? ed::GetSelectedNodes(sel.data(), count) : 0;
        ed::SetCurrentEditor(nullptr);

        if (got <= 0)
        {
            m_ConsoleLines.push_back("[Info] Nothing selected to copy.");
            return;
        }
        sel.resize(got);

        // Comment é deixado de fora do recorte porque ele SELECIONA junto tudo
        // o que está por baixo dele no node-editor; um Ctrl+X sobre um
        // comentário levaria metade do grafo sem que o autor tivesse marcado
        // aqueles nodes. Copiar, tudo bem.
        SaveNodePositions();
        m_NodeClipboard = m_Graph->SerializeSubset(sel).dump();

        if (!cut) { m_ConsoleLines.push_back("[Info] " + std::to_string(got) + " node(s) copied."); return; }

        PushUndo("Cut Nodes");
        int removed = 0;
        for (const auto& id : sel)
        {
            if (auto* n = m_Graph->FindNode(id))
            {
                if (n->Name == "Function Entry" || n->Name == "Return Node") continue;
                m_Graph->RemoveNode(id);
                removed++;
            }
        }
        CommitUndo("Cut Nodes");
        m_ConsoleLines.push_back("[Info] " + std::to_string(removed) + " node(s) cut.");
    }

    void ScriptGraphWindow::PasteNodes()
    {
        if (!m_Graph || m_NodeClipboard.empty()) return;

        nlohmann::json j;
        try { j = nlohmann::json::parse(m_NodeClipboard); }
        catch (const std::exception& e)
        {
            m_ConsoleLines.push_back(std::string("[ERROR] Clipboard is not a valid node set: ") + e.what());
            return;
        }

        // Deslocamento fixo, e não "na posição do mouse": o atalho pode ser
        // acionado com o cursor em qualquer lugar, inclusive fora do canvas,
        // e nesse caso a cópia apareceria longe do original. Um degrau
        // constante deixa claro que houve cópia e mantém tudo por perto.
        PushUndo("Paste Nodes");
        auto created = m_Graph->PasteSubset(j, ImVec2(40.f, 40.f));
        CommitUndo("Paste Nodes");

        // SC10 — posicionar e selecionar acontece no PROXIMO frame do canvas,
        // nao aqui. Este codigo roda fora do par ed::Begin/End: mexer no
        // node-editor dali e trabalhar com um contexto que ainda nao viu os
        // nodes novos, que foi a origem do estrago. m_PendingPositionSync ja
        // existe desde o SC9 e faz exatamente isso, no lugar certo.
        m_PendingPositionSync = true;
        m_PendingSelectNodes = created;

        m_ConsoleLines.push_back("[Info] " + std::to_string(created.size()) + " node(s) pasted.");
    }

    void ScriptGraphWindow::DuplicateSelectedNodes()
    {
        // Ctrl+D é copiar e colar num gesto só. Preserva o clipboard anterior:
        // duplicar não deveria custar o que estava guardado para colar.
        const std::string saved = m_NodeClipboard;
        CopySelectedNodes(false);
        if (!m_NodeClipboard.empty()) PasteNodes();
        m_NodeClipboard = saved.empty() ? m_NodeClipboard : saved;
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::SaveNodePositions()
    {
        if (!m_Graph || !m_EdCtx) return;
        if (!m_InsideNodeEditorFrame)
            ed::SetCurrentEditor(m_EdCtx);

        for (auto& node : m_Graph->GetNodes())
        {
            const ImVec2 p = ed::GetNodePosition(node->ID);

            // ── SC10: node que o CANVAS ainda nao conhece ────────────────────
            //
            // ed::GetNodePosition devolve (FLT_MAX, FLT_MAX) quando o node
            // nunca foi desenhado — e nao um zero, nem um erro. Um node
            // recem-criado por codigo (colar, por exemplo) esta exatamente
            // nesse estado ate o proximo frame do canvas.
            //
            // Sem esta guarda, o SaveNodePositions chamado logo em seguida
            // (dentro do CommitUndo) gravava FLT_MAX na posicao do node. A
            // partir dai: o snapshot de undo carrega infinito, o Fit calcula os
            // limites do conteudo com infinito e some com a visao, e um Save
            // leva o infinito para o .axescript. Foi o "travou tudo" depois de
            // colar.
            //
            // A posicao do MODELO e a boa nesse caso — ela veio de quem criou o
            // node. O canvas se alinha a ela no proximo frame
            // (m_PendingPositionSync).
            const bool valid =
                std::isfinite(p.x) && std::isfinite(p.y) &&
                std::fabs(p.x) < 1.0e6f && std::fabs(p.y) < 1.0e6f;

            if (valid) node->Position = p;
        }

        if (!m_InsideNodeEditorFrame)
            ed::SetCurrentEditor(nullptr);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::CompileScript()
    {
        SaveNodePositions();
        if (m_ScriptAsset && !m_ScriptAsset->GetFilePath().empty())
        {
            m_ScriptAsset->Save(m_ScriptAsset->GetFilePath());
            m_ConsoleLines.push_back("[Info] Script auto-saved.");

            // Compilar tambem propaga: e o gesto que o autor faz esperando
            // "agora vale pra tudo", igual ao Compile da Unreal.
            if (m_ScriptSavedCallback)
                m_ScriptSavedCallback(m_ScriptAsset->GetFilePath());
        }

        if (!m_Graph) return;
        if (!m_ScriptAsset) return;   // S0a: compilar sempre parte de um asset

        std::string scriptName = m_ScriptAsset->GetName();

        char exeBuf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
        std::filesystem::path root = (exeDir / ".." / ".." / "..").lexically_normal();
        std::filesystem::path vendor = root / "src" / "vendor";

        std::vector<std::filesystem::path> includes = {
            root / "src",
            vendor / "glm",
            vendor / "entt" / "src",
            vendor / "spdlog" / "include",
            vendor / "spdlog",
            vendor / "imgui",              // components.hpp inclui <imgui.h>
            vendor / "imgui-node-editor",  // script_graph.hpp inclui imgui_node_editor.h
            vendor / "nlohmann",           // "json.hpp" (include SEM pasta)

            // O PAI de nlohmann/ — headers do engine usam a forma
            // <nlohmann/json.hpp> (com a pasta no caminho), e essa so
            // resolve a partir de src/vendor. Faltava, e qualquer header
            // que o script puxasse com esse include quebrava a compilacao
            // com C1083 (foi o caso do anim_node.hpp, que entra na cadeia
            // via components.hpp -> anim_graph_instance.hpp).
            vendor,
        };

        std::filesystem::path axeLib = (exeDir / ".." / "axe" / "axe.lib").lexically_normal();

        // SC4 — os artefatos gerados passam a morar no PROJETO, não na pasta
        // bin do editor. O nome leva o UUID do asset: sem ele, dois projetos
        // com um "BP_Player" cada gravavam por cima um do outro, e nada
        // conseguia dizer de quem era um arquivo solto na pasta.
        std::string scriptUuid;
        if (m_ScriptAsset && !m_ScriptAsset->GetFilePath().empty())
            if (auto* rec = AssetDatabase::Get().GetByPath(m_ScriptAsset->GetFilePath()))
                scriptUuid = rec->UUID;

        auto cppPath = ScriptPaths::CppPathFor(scriptName, scriptUuid);
        auto dllPath = ScriptPaths::DllPathFor(scriptName, scriptUuid);

        if (cppPath.empty() || dllPath.empty())
        {
            // Sem projeto aberto não há onde gravar. Falhar aqui com a razao
            // dita e melhor do que voltar a escrever em temp_scripts, que e
            // justamente o comportamento que este patch remove.
            m_ConsoleLines.push_back("[ERROR] No project open - nowhere to write the script files.");
            m_Msg = "No project"; m_MsgOk = false; m_MsgTimer = 5.0f;
            return;
        }

        auto cpp = cppPath.string();
        auto dll = dllPath.string();

        m_ConsoleLines.push_back("[Script Editor] Generating C++...");
        const std::vector<ScriptVariable>* assetVars =
            (m_ScriptAsset && !m_ScriptAsset->GetVariables().empty())
            ? &m_ScriptAsset->GetVariables() : nullptr;
        const std::vector<ScriptFunction>* functions =
            (m_ScriptAsset && !m_ScriptAsset->GetFunctions().empty())
            ? &m_ScriptAsset->GetFunctions() : nullptr;
        // BUGFIX: usa SEMPRE o grafo principal do asset pra compilar, nunca
        // m_Graph diretamente — se o usuário estiver no meio da edição de uma
        // Function (m_Graph apontando pro grafo DELA) e clicar "Compilar",
        // compilar m_Graph geraria a função como se fosse o script inteiro,
        // ignorando OnStart/OnUpdate/etc. do grafo principal.
        const ScriptGraph* mainGraph = m_ScriptAsset ? m_ScriptAsset->GetGraph().get() : m_Graph;
        // S3 — a identidade da classe e o UUID do asset, o MESMO que ja nomeia
        // os artefatos (SC4). Um script sem asset registrado compila sem
        // identidade: roda, mas nenhum Cast casa com ele — falhar o cast e
        // melhor do que casar com o script errado.
        std::string code = ScriptGraphCompiler::Generate(*mainGraph, scriptName,
            assetVars, functions, scriptUuid);

        std::ofstream f(cpp); f << code; f.close();
        m_ConsoleLines.push_back("[Script Editor] .cpp saved: " + cpp);

        std::string includeStr;
        for (auto& inc : includes)
        {
            if (!includeStr.empty()) includeStr += ";";
            includeStr += inc.string();
        }

        m_Msg = "Compiling..."; m_MsgOk = true; m_MsgTimer = 2.0f;
        bool ok = ScriptCompiler::Compile(cpp, dll, includeStr, axeLib.string(),
            [this](const std::string& msg, bool success)
            {
                m_Msg = success ? "Compiled!" : "Failed";
                m_MsgOk = success; m_MsgTimer = 5.0f;
                std::istringstream ss(msg); std::string ln;
                while (std::getline(ss, ln))
                    m_ConsoleLines.push_back(success ? "[OK] " + ln : "[ERROR] " + ln);
            });

        // SC6 — o resultado vira estado do botao.
        //
        // m_GraphDirty sai como FALSE mesmo quando a compilacao falha, e isso e
        // deliberado: "sujo" significa "o grafo mudou desde a ultima
        // tentativa", nao "esta funcionando". Acabamos de tentar exatamente
        // este grafo. Deixa-lo sujo aqui esconderia o vermelho para sempre —
        // sujo tem prioridade no desenho do botao — e o erro nunca apareceria.
        // O ambar volta assim que o autor editar qualquer coisa.
        m_GraphDirty = false;
        m_LastCompileFailed = !ok;

        if (ok)
        {
            if (m_ScriptAsset) { m_ScriptAsset->DllPath = dll; m_ScriptAsset->IsCompiled = true; }
        }
    }

} // namespace axe