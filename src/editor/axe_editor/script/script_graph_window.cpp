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

    void ScriptGraphWindow::Close()
    {
        m_IsOpen = false;
        m_Graph = nullptr;
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

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool vis = ImGui::Begin(title.c_str(), &m_IsOpen,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();
        if (!vis) { ImGui::End(); return; }

        // ── Menu bar ─────────────────────────────────────────────────────────
        if (ImGui::BeginMenuBar())
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.45f, 0.13f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1));
            if (ImGui::Button("  Compilar  ")) CompileScript();
            ImGui::SameLine(0, 8);

            ImGui::BeginDisabled(!CanUndo());
            if (ImGui::Button("  Undo  ")) Undo();
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(!CanRedo());
            if (ImGui::Button("  Redo  ")) Redo();
            ImGui::EndDisabled();

            if (CanUndo())
            {
                ImGui::SameLine(0, 8);
                ImGui::TextDisabled("%s", m_History.GetUndoName().c_str());
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine(0, 8);
            if (ImGui::Button("Fit"))
            {
                ed::SetCurrentEditor(m_EdCtx);
                ed::NavigateToContent();
                ed::SetCurrentEditor(nullptr);
            }

            ImGui::SameLine(0, 8);
            ImGui::TextDisabled("%s", m_Component ? m_Component->ScriptName.c_str() : "—");

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

        // ── DockSpace ────────────────────────────────────────────────────────
        ImGuiID dsId = ImGui::GetID("ScriptEditorDockSpace");
        ImGuiDockNode* existingNode = ImGui::DockBuilderGetNode(dsId);

        if (!m_LayoutBuilt && (!existingNode || existingNode->IsEmpty()))
        {
            m_LayoutBuilt = true;
            ImVec2 sz = ImGui::GetWindowSize();
            sz.y -= ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
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
        std::string code = ScriptGraphCompiler::Generate(*m_Graph, scriptName, assetVars);

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