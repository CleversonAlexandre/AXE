// material_editor_window.cpp
// Núcleo do MaterialEditorWindow: construtor/destrutor, abertura de material,
// dockspace/layout das sub-janelas (Draw) e input de teclado de alto nível.
// O resto da implementação está dividida nos demais arquivos desta pasta.

#include "material_editor_window.hpp"
#include "axe/asset/asset_database.hpp"
#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>
#include <imgui/imgui_internal.h>
#include <fstream>
#include <filesystem>

namespace ed = ax::NodeEditor;

namespace axe
{

    bool MaterialEditorWindow::s_NeedsReload = false;


    // -------------------------------------------------------------------------
    // Construtor / Destrutor
    // -------------------------------------------------------------------------

    MaterialEditorWindow::MaterialEditorWindow()
    {
        ed::Config config;
        config.SettingsFile = "material_editor.ini";
        m_NodeEditorContext = ed::CreateEditor(&config);
        m_Graph = std::make_unique<MaterialGraph>();
    }

    MaterialEditorWindow::~MaterialEditorWindow()
    {
        if (m_NodeEditorContext)
            ed::DestroyEditor(m_NodeEditorContext);
    }

    // -------------------------------------------------------------------------
    // Inicialização
    // -------------------------------------------------------------------------


    void MaterialEditorWindow::Initialize()
    {
        InitializePreview();
    }


    void MaterialEditorWindow::OpenMaterial(std::shared_ptr<MaterialAsset> asset)
    {
        if (m_NodeEditorContext)
        {
            ed::DestroyEditor(m_NodeEditorContext);
            m_NodeEditorContext = nullptr;
        }

        ed::Config config;
        config.SettingsFile = nullptr;
        m_NodeEditorContext = ed::CreateEditor(&config);

        m_Asset = asset;
        m_Material = asset->GetMaterial();
        m_Open = true;
        m_FrameCount = 0;

        // O nome do material vive duplicado: dentro do .axemat (m_Asset->GetName())
        // e no AssetRecord do AssetBrowser. O AssetRecord é a fonte da verdade
        // (é o que aparece no browser e é atualizado ao renomear), então
        // sincronizamos aqui para o título do editor nunca ficar desatualizado
        // mostrando "NewMaterial" em materiais já renomeados.
        {
            const AssetRecord* record = AssetDatabase::Get().GetByPath(asset->GetFilePath());
            if (record && !record->Name.empty() && record->Name != asset->GetName())
                asset->SetName(record->Name);
        }

        if (m_Material)
            m_Material->UsePBR = true;

        m_Graph = std::make_unique<MaterialGraph>();
        LoadGraph(); // ← recompila shader no m_Material

        // Aplica o material na esfera APÓS LoadGraph — shader já está compilado
        if (m_PreviewScene && m_Material)
        {
            auto& registry = m_PreviewScene->GetRegistry();
            if (registry.valid(m_PreviewEntity))
            {
                if (registry.all_of<MaterialComponent>(m_PreviewEntity))
                    registry.get<MaterialComponent>(m_PreviewEntity).Data = m_Material;
                else
                    registry.emplace<MaterialComponent>(m_PreviewEntity, m_Material);
            }
        }

        // Reseta câmera do preview
        if (m_PreviewRenderer)
        {
            m_PreviewRenderer->m_Camera = std::make_unique<EditorCamera>(
                45.0f, 1.0f, 0.1f, 1000.0f
            );
            m_PreviewRenderer->SetScene(m_PreviewScene.get());
            m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());
        }

        m_History.Clear();
    }

    // -------------------------------------------------------------------------
    // Draw principal
    // -------------------------------------------------------------------------


    void MaterialEditorWindow::Draw()
    {
        if (!m_Open || !m_Asset || !m_Material) return;
        

        if (s_NeedsReload && m_Open)
        {
            s_NeedsReload = false;
            ReloadGraph();
        }

        // Sempre chama Begin/End para o ImGui salvar a posição no imgui.ini
        ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);

        std::string title = m_Asset
            ? "Material Editor — " + m_Asset->GetName() + "###MaterialEditor"
            : "Material Editor###MaterialEditor";

        if (!ImGui::Begin(title.c_str(), &m_Open))
        {
            ImGui::End();
            return;
        }


        ImVec2 availSize = ImGui::GetContentRegionAvail();

        ImGuiID dockspace_id = ImGui::GetID("MaterialEditorDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        //ImGuiID dock_preview, dock_log;
        //ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.6f, &dock_preview, &dock_log);


        // Layout padrão — só cria se o dockspace estiver vazio (primeira vez)        

        static bool s_LayoutConfigured = false;
        if (!s_LayoutConfigured)
        {
            s_LayoutConfigured = true;

            // Verifica se o layout já foi configurado com esta versão
            const char* layoutFlagFile = "material_editor_layout_v2.flag";
            bool needsRebuild = !std::filesystem::exists(layoutFlagFile);

            if (needsRebuild)
            {
                // Cria o arquivo de flag
                std::ofstream flag(layoutFlagFile);
                flag << "v2";

                ImGui::DockBuilderRemoveNode(dockspace_id);
                ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

                ImGui::DockBuilderSetNodeSize(dockspace_id,
                    (availSize.x > 0 && availSize.y > 0) ? availSize : ImVec2(1200, 700));

                ImGuiID dock_left, dock_right;
                ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, &dock_left, &dock_right);

                ImGuiID dock_graph, dock_bottom;
                ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Up, 0.6f, &dock_graph, &dock_bottom);

                ImGuiID dock_preview, dock_log;
                ImGui::DockBuilderSplitNode(dock_bottom, ImGuiDir_Left, 0.6f, &dock_preview, &dock_log);

                ImGui::DockBuilderDockWindow("Material Params", dock_left);
                ImGui::DockBuilderDockWindow("Material Graph", dock_graph);
                ImGui::DockBuilderDockWindow("Material Preview", dock_preview);
                ImGui::DockBuilderDockWindow("Shader Log", dock_log);

                ImGui::DockBuilderFinish(dockspace_id);
            }
        }


        DrawMaterialParamsWindow();
        DrawNodeGraphWindow();
        DrawPreviewWindow();
        DrawShaderLog();
        m_IsAnyWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        HandleInput();

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Sub-janelas
    // -------------------------------------------------------------------------


    void MaterialEditorWindow::HandleInput()
    {
        if (!m_Open)
        {
            m_History.Clear();
            m_OriginalPinLinks.clear();

            return;
        }

        ImGuiIO& io = ImGui::GetIO();



        // Só processa se alguma janela do material editor estiver focada
        if (!m_IsAnyWindowFocused) return;

        // Ctrl+Z — Undo
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
            m_History.Undo();

        // Ctrl+Shift+Z — Redo  
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
            m_History.Redo();
    }


    // -------------------------------------------------------------------------
    // Câmera de preview
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::UpdatePreviewCamera()
    {
        // Reservado para sincronização futura de câmera
    }


} // namespace axe