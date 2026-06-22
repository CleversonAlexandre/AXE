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
#include "axe/script/script_asset.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_graph_compiler.hpp"
#include "axe/script/script_compiler.hpp"
#include "axe/log/log.hpp"
#include "editor/axe_editor/editor_icon_library.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <windows.h>

namespace ed = ax::NodeEditor;

namespace axe
{


    // ─────────────────────────────────────────────────────────────────────────
    ScriptGraphWindow::ScriptGraphWindow() = default;
    ScriptGraphWindow::~ScriptGraphWindow() { Shutdown(); }

    void ScriptGraphWindow::Initialize()
    {
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
    void ScriptGraphWindow::OpenForEntity(entt::entity entity, ScriptComponent* comp,
        entt::registry* registry)
    {
        m_Entity = entity;
        m_Component = comp;
        m_Graph = comp ? comp->Graph.get() : nullptr;
        m_EditingFunctionIndex = -1; // abrir um novo alvo sempre começa no grafo principal
        m_SourceRegistry = registry;
        m_IsOpen = true;
        m_FirstFrame = true;
        m_CtxBuf[0] = m_CompSearchBuf[0] = '\0';
        m_ConsoleLines.clear();
        m_ConsoleLines.push_back("[Script Editor] Pronto.");

        if (!m_PreviewRenderer)
            InitPreviewScene();
        else
            SyncMeshFromSource();
    }

    void ScriptGraphWindow::OpenForAsset(std::shared_ptr<ScriptAsset> asset)
    {
        if (!asset) return;
        m_ScriptAsset = asset;
        m_Graph = asset->GetGraph().get();
        m_EditingFunctionIndex = -1; // abrir um novo asset sempre começa no grafo principal
        m_Entity = entt::null;
        m_Component = nullptr;
        m_SourceRegistry = nullptr;
        m_IsOpen = true;
        m_FirstFrame = true;
        m_CtxBuf[0] = m_CompSearchBuf[0] = '\0';
        m_ConsoleLines.clear();
        m_ConsoleLines.push_back("[Script Editor] " + asset->GetName() + " — " +
            ScriptClassTypeToString(asset->GetClassType()));

        if (!m_PreviewRenderer)
            InitPreviewScene();
        SyncMeshFromAsset();
        SyncComponentsToPreview();
    }

    // ─────────────────────────────────────────────────────────────────────────
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
        m_Component = nullptr;
        m_Entity = entt::null;
        m_SourceRegistry = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Draw — janela host com DockSpace interno
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::Draw()
    {
        if (!m_IsOpen || !m_Graph) return;

        ImGui::SetNextWindowSize(ImVec2(1280, 820), ImGuiCond_FirstUseEver);
        std::string title = "Script Editor \xe2\x80\x94 " +
            (m_ScriptAsset ? m_ScriptAsset->GetName() :
                m_Component ? m_Component->ScriptName : "?") + "###ScriptEditorHost";

        // ── Undo / Redo shortcuts ─────────────────────────────────────────────
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
        {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 3.f));
        bool vis = ImGui::Begin(title.c_str(), &m_IsOpen,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();
        if (!vis) { ImGui::End(); return; }

        // ── Menu bar ─────────────────────────────────────────────────────────
        if (ImGui::BeginMenuBar())
        {
            auto& icons = EditorIconLibrary::Get();

            // Helper local: botão com ícone (16x16) + label, mesmo visual em toda a toolbar.
            // Cai para texto puro se o ícone não estiver carregado (robustez contra
            // recursos faltando, sem quebrar o layout da barra).
            auto iconButton = [](std::shared_ptr<Texture2D> icon, const char* label, ImVec2 size = ImVec2(0, 0)) -> bool
                {
                    // ID único por chamada: o label entra na chave de ID do ImGui mesmo
                    // estando "atrás" do "##" (que só oculta o texto visível do botão,
                    // não participa do ID). Sem isso, todo botão desta toolbar colidiria
                    // no mesmo ID interno ("##btn"), e cliques/hover ficariam cruzados
                    // entre Compilar/Save/Undo/Redo/Fit.
                    std::string id = std::string("##") + label;
                    if (icon && icon->IsLoaded())
                    {
                        ImGui::BeginGroup();
                        bool pressed = ImGui::Button(id.c_str(), size.x > 0 ? size : ImVec2(ImGui::CalcTextSize(label).x + 38.f, 0));
                        ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        float iconSz = 15.f;
                        float cy = (r0.y + r1.y) * 0.5f;
                        dl->AddImage((ImTextureID)(uintptr_t)icon->GetRendererID(),
                            ImVec2(r0.x + 8.f, cy - iconSz * 0.5f), ImVec2(r0.x + 8.f + iconSz, cy + iconSz * 0.5f),
                            ImVec2(0, 1), ImVec2(1, 0));
                        dl->AddText(ImVec2(r0.x + 8.f + iconSz + 6.f, cy - ImGui::GetFontSize() * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_Text), label);
                        ImGui::EndGroup();
                        return pressed;
                    }
                    return ImGui::Button(label, size); // fallback sem ícone
                };

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.45f, 0.13f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1));
            if (iconButton(icons.GetCompile(), "Compilar")) CompileScript();
            ImGui::SameLine(0, 6);

            // Save — movido para junto do Compilar, a pedido (antes ficava isolado
            // no painel Scene Graph, em script_details.cpp).
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.35f, 0.55f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.75f, 1));
            bool canSave = m_ScriptAsset && !m_ScriptAsset->GetFilePath().empty();
            ImGui::BeginDisabled(!canSave);
            if (iconButton(icons.GetSave(), "Save"))
            {
                SaveNodePositions();
                m_ScriptAsset->Save(m_ScriptAsset->GetFilePath());
                m_ConsoleLines.push_back("[Info] Script salvo: " + m_ScriptAsset->GetFilePath().string());
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0, 10);

            ImGui::BeginDisabled(!CanUndo());
            bool undoClicked = iconButton(icons.GetUndo(), "Undo");
            // Tooltip mostra o nome da ação que será desfeita — antes isso era um
            // texto solto na toolbar, que podia parecer um botão por engano
            // (ex.: "Add Event" ficando ao lado do Redo).
            if (CanUndo() && ImGui::IsItemHovered())
                ImGui::SetTooltip("Desfazer: %s", m_History.GetUndoName().c_str());
            if (undoClicked) Undo();
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);

            ImGui::BeginDisabled(!CanRedo());
            bool redoClicked = iconButton(icons.GetRedo(), "Redo");
            if (CanRedo() && ImGui::IsItemHovered())
                ImGui::SetTooltip("Refazer: %s", m_History.GetRedoName().c_str());
            if (redoClicked) Redo();
            ImGui::EndDisabled();
            ImGui::PopStyleColor(2);

            ImGui::SameLine(0, 10);
            if (iconButton(icons.GetFit(), "Fit"))
            {
                ed::SetCurrentEditor(m_EdCtx);
                ed::NavigateToContent();
                ed::SetCurrentEditor(nullptr);
            }

            ImGui::SameLine(0, 8);
            ImGui::TextDisabled("%s", m_Component ? m_Component->ScriptName.c_str() : "—");

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
                    ImGui::Text("\xe2\x9a\x99 Function: %s", funcs[m_EditingFunctionIndex].Name.c_str());
                    ImGui::PopStyleColor();

                    ImGui::SameLine(0, 8);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.28f, 0.3f, 1));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.38f, 0.4f, 1));
                    if (ImGui::SmallButton("< Voltar ao grafo principal"))
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
            ImGui::EndMenuBar();
        }

        // Respiro visual entre a toolbar e o conteúdo do dock abaixo.
        const float kToolbarGap = 8.0f;
        ImGui::Dummy(ImVec2(0, kToolbarGap));

        // ── DockSpace ────────────────────────────────────────────────────────
        ImGuiID dsId = ImGui::GetID("ScriptEditorDockSpace");
        ImGuiDockNode* existingNode = ImGui::DockBuilderGetNode(dsId);

        if (!m_LayoutBuilt && (!existingNode || existingNode->IsEmpty()))
        {
            m_LayoutBuilt = true;
            ImVec2 sz = ImGui::GetWindowSize();
            sz.y -= ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f + kToolbarGap;
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

        ImGui::End();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawConsoleWindow()
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.07f, 1));
        if (ImGui::Begin("Script Console"))
        {
            ImGui::TextDisabled("Console");
            ImGui::SameLine();
            if (ImGui::SmallButton("Limpar")) m_ConsoleLines.clear();
            ImGui::Separator();

            for (auto& line : m_ConsoleLines)
            {
                bool err = line.find("[ERROR]") != std::string::npos;
                bool warn = line.find("[WARN]") != std::string::npos;
                ImGui::PushStyleColor(ImGuiCol_Text,
                    err ? ImVec4(1, .3f, .3f, 1) :
                    warn ? ImVec4(1, .8f, .2f, 1) : ImVec4(.8f, .8f, .8f, 1));
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
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

        Command cmd;
        cmd.Name = actionName;
        cmd.Execute = nullptr;
        cmd.Undo = [this, snapBefore, snapAfter]()
            {
                m_PendingUndoSnapshot = snapBefore;
                m_PendingRedoSnapshot = snapAfter;
            };
        m_History.Push(std::move(cmd));
    }

    void ScriptGraphWindow::Undo()
    {
        m_PendingUndoSnapshot.clear();
        m_PendingRedoSnapshot.clear();
        m_History.Undo();
        if (!m_PendingUndoSnapshot.empty() && m_ScriptAsset)
        {
            m_ScriptAsset->LoadFromString(m_PendingUndoSnapshot);
            m_Graph = m_ScriptAsset->GetGraph().get();
            m_EditingFunctionIndex = -1; // undo sempre volta pro grafo principal
            m_PendingUndoSnapshot.clear();
            SyncComponentsToPreview();
        }
    }

    void ScriptGraphWindow::Redo()
    {
        if (!m_PendingRedoSnapshot.empty() && m_ScriptAsset)
        {
            std::string snap = m_PendingRedoSnapshot;
            m_PendingRedoSnapshot.clear();
            m_ScriptAsset->LoadFromString(snap);
            m_Graph = m_ScriptAsset->GetGraph().get();
            m_EditingFunctionIndex = -1; // redo sempre volta pro grafo principal
            SyncComponentsToPreview();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::SaveNodePositions()
    {
        if (!m_Graph || !m_EdCtx) return;
        if (!m_InsideNodeEditorFrame)
            ed::SetCurrentEditor(m_EdCtx);
        for (auto& node : m_Graph->GetNodes())
            node->Position = ed::GetNodePosition(node->ID);
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
            m_ConsoleLines.push_back("[Info] Script salvo automaticamente.");
        }

        if (!m_Graph) return;
        if (!m_Component && !m_ScriptAsset) return;

        std::string scriptName = m_ScriptAsset ? m_ScriptAsset->GetName() :
            m_Component ? m_Component->ScriptName : "Script";

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
            vendor / "nlohmann",           // json.hpp
        };

        std::filesystem::path axeLib = (exeDir / ".." / "axe" / "axe.lib").lexically_normal();
        std::filesystem::path dir = exeDir / "temp_scripts";
        std::filesystem::create_directories(dir);

        auto cpp = (dir / (scriptName + ".cpp")).string();
        auto dll = (dir / (scriptName + ".dll")).string();

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
        std::string code = ScriptGraphCompiler::Generate(*mainGraph, scriptName, assetVars, functions);

        std::ofstream f(cpp); f << code; f.close();
        m_ConsoleLines.push_back("[Script Editor] .cpp salvo: " + cpp);

        std::string includeStr;
        for (auto& inc : includes)
        {
            if (!includeStr.empty()) includeStr += ";";
            includeStr += inc.string();
        }

        m_Msg = "Compilando..."; m_MsgOk = true; m_MsgTimer = 2.0f;
        bool ok = ScriptCompiler::Compile(cpp, dll, includeStr, axeLib.string(),
            [this](const std::string& msg, bool success)
            {
                m_Msg = success ? "Compilado!" : "Erro";
                m_MsgOk = success; m_MsgTimer = 5.0f;
                std::istringstream ss(msg); std::string ln;
                while (std::getline(ss, ln))
                    m_ConsoleLines.push_back(success ? "[OK] " + ln : "[ERROR] " + ln);
            });

        if (ok)
        {
            if (m_Component) { m_Component->DllPath = dll; m_Component->IsCompiled = true; }
            if (m_ScriptAsset) { m_ScriptAsset->DllPath = dll; m_ScriptAsset->IsCompiled = true; }
        }
    }

} // namespace axe