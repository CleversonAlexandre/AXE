#include "material_editor_window.hpp"
#include "editor_icon_library.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>
#include <utilities/widgets.h>
#include <imgui/imgui_internal.h>

#include "axe/material/material_compiler.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/graphics/render_command.hpp"
#include <glm/gtc/type_ptr.hpp>
#include "axe/graphics/framebuffer.hpp"
#include <sstream>

#include "material_thumbnail_renderer.hpp"

#include "inspector_window.hpp"

#include "axe_editor/asset/asset_picker.hpp"

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

    void MaterialEditorWindow::InitializePreview()
    {
        // Framebuffer do preview
        FramebufferSpecification spec;
        spec.Width = 512;
        spec.Height = 512;
        spec.Attachments = {
            FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::DEPTH32F,
        };
        m_PreviewFramebuffer = Framebuffer::Create(spec);

        // Renderer dedicado ao preview — desacoplado do renderer do editor
        m_PreviewRenderer = std::make_unique<ViewportRenderer>();
        m_PreviewRenderer->Initialize();
        m_PreviewRenderer->SetPickingEnabled(false);
        m_PreviewRenderer->SetPreviewMode(true);
        //if (m_PreviewRenderer->GetSceneRenderer())
        //    m_PreviewRenderer->GetSceneRenderer()->SetDeferredSupported(false);


       

        // Cena isolada com a esfera de preview
        m_PreviewScene = std::make_unique<Scene>();
        m_PreviewEntity = m_PreviewScene->CreateEntity("PreviewSphere");

        auto& registry = m_PreviewScene->GetRegistry();

        // Escala padrão
        if (registry.all_of<TransformComponent>(m_PreviewEntity))
        {
            auto& tc = registry.get<TransformComponent>(m_PreviewEntity);
            tc.Data.Scale = glm::vec3(1.0f);
        }

        auto lightEntity = m_PreviewScene->CreateEntity("PreviewLight");
        auto& lc = registry.emplace<LightComponent>(lightEntity);
        lc.Data = std::make_shared<DirectionalLight>();
        lc.Data->Direction = glm::vec3(0.0f, -1.0f, -1.0f);
        lc.Data->Color = glm::vec3(1.0f, 1.0f, 1.0f);
        lc.Data->Intensity = 3.0f;
        lc.Data->AmbientStrength = 0.3f;

        // Mesh de preview — usa CreateSphere() diretamente para evitar
        // dependência do AssetDatabase (o UUID é apenas para referência)
        auto mesh = MeshFactory::CreateSphere();
        auto& mc = registry.emplace<MeshComponent>(m_PreviewEntity);
        mc.Data = mesh;
        mc.AssetUUID = PrimitiveUUID::Sphere;

        // O MaterialComponent é adicionado em OpenMaterial(),
        // quando m_Material estiver disponível


        m_PreviewRenderer->SetScene(m_PreviewScene.get());

        // Ambiente HDRI
        m_PreviewEnvironment = std::make_unique<SceneEnvironment>();
        m_PreviewEnvironment->LoadHDRI("resources/quarry_04_puresky_2k.hdr");
        m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());
        
        if (m_PreviewRenderer->GetSceneRenderer())
            m_PreviewRenderer->GetSceneRenderer()->SetEnvironment(m_PreviewEnvironment.get());        
    }

    // -------------------------------------------------------------------------
    // Render (chamado ANTES do ImGui no EditorLayer::OnRender)
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::RenderPreview()
    {
        if (!m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene) return;

        // ✅ Garante forward ANTES de renderizar
        if (m_PreviewRenderer->GetSceneRenderer())
            m_PreviewRenderer->GetSceneRenderer()->SetDeferredSupported(false);

        m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());
        if (m_PreviewEnvironment && m_PreviewRenderer->GetSceneRenderer())
            m_PreviewRenderer->GetSceneRenderer()->SetEnvironment(m_PreviewEnvironment.get());

        uint32_t width = (uint32_t)m_PreviewSize.x;
        uint32_t height = (uint32_t)m_PreviewSize.y;
        if (width == 0 || height == 0) { width = 512; height = 512; }

        m_PreviewRenderer->SetScene(m_PreviewScene.get());
        m_PreviewRenderer->RenderToFramebuffer(*m_PreviewFramebuffer, width, height, 0.0f);
    }

    //void MaterialEditorWindow::RenderPreview()
    //{
    //    if (!m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene) return;

    //    // Garante que o environment está no ViewportRenderer
    //    m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());

    //    if (m_PreviewEnvironment && m_PreviewRenderer->GetSceneRenderer())
    //        m_PreviewRenderer->GetSceneRenderer()->SetEnvironment(m_PreviewEnvironment.get());

    //    //if (m_PreviewEnvironment && m_PreviewRenderer->GetSceneRenderer())
    //    //{
    //    //    m_PreviewRenderer->GetSceneRenderer()->SetEnvironment(m_PreviewEnvironment.get());
    //    //    // ✅ Garante forward com IBL
    //    //    m_PreviewRenderer->GetSceneRenderer()->SetDeferredSupported(false);
    //    //}

    //    uint32_t width = (uint32_t)m_PreviewSize.x;
    //    uint32_t height = (uint32_t)m_PreviewSize.y;
    //    if (width == 0 || height == 0) { width = 512; height = 512; }

    //    m_PreviewRenderer->SetScene(m_PreviewScene.get());
    //    m_PreviewRenderer->RenderToFramebuffer(*m_PreviewFramebuffer, width, height, 0.0f);
    //}

    // -------------------------------------------------------------------------
    // Abertura de material
    // -------------------------------------------------------------------------

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

    void MaterialEditorWindow::DrawMaterialParamsWindow()
    {
        ed::SetCurrentEditor(m_NodeEditorContext);

        if (ImGui::Begin("Material Params"))
            DrawMaterialParams(*m_Material);
        ImGui::End();

        ed::SetCurrentEditor(nullptr);
    }

    void MaterialEditorWindow::DrawNodeGraphWindow()
    {
        if (ImGui::Begin("Material Graph"))
        {
            // Toolbar
            auto& icons = EditorIconLibrary::Get();
            float btnSize = 24.0f;

            // Undo
            bool canUndo = m_History.CanUndo();
            if (!canUndo) ImGui::BeginDisabled();
            if (icons.GetUndo())
            {
                if (ImGui::ImageButton("##undo",
                    (ImTextureID)(uintptr_t)icons.GetUndo()->GetRendererID(),
                    ImVec2(btnSize, btnSize),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                ))
                    m_History.Undo();
            }
            if (!canUndo) ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Undo: %s", m_History.GetUndoName().c_str());

            ImGui::SameLine();

            // Redo
            bool canRedo = m_History.CanRedo();
            if (!canRedo) ImGui::BeginDisabled();
            if (icons.GetRedo())
            {
                if (ImGui::ImageButton("##redo",
                    (ImTextureID)(uintptr_t)icons.GetRedo()->GetRendererID(),
                    ImVec2(btnSize, btnSize),
                    ImVec2(0, 1),
                    ImVec2(1, 0)))
                    m_History.Redo();
            }
            if (!canRedo) ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Redo: %s", m_History.GetRedoName().c_str());

            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Salvar
            if (icons.GetSave())
            {
                if (ImGui::ImageButton("##save",
                    (ImTextureID)(uintptr_t)icons.GetSave()->GetRendererID(),
                    ImVec2(btnSize, btnSize),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                ))
                    SaveGraph();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Salvar grafo");

            ImGui::SameLine();

            // Compilar
            if (icons.GetCompile())
            {
                if (ImGui::ImageButton("##compile",
                    (ImTextureID)(uintptr_t)icons.GetCompile()->GetRendererID(),
                    ImVec2(btnSize, btnSize),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                ))
                {
                    // Chama o mesmo código do "Compile and Apply"
                    if (m_Material)
                        CompileAndApply();


                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Compilar e aplicar");

            ImGui::Separator();
            DrawNodeGraph();
        }
        ImGui::End();
    }

    void MaterialEditorWindow::DrawPreviewWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        if (ImGui::Begin("Material Preview", nullptr,
            ImGuiWindowFlags_NoScrollbar)) // | ImGuiWindowFlags_NoMove))
        {
            m_PreviewHovered = ImGui::IsWindowHovered();
            m_PreviewFocused = ImGui::IsWindowFocused();

            ImVec2 viewportSize = ImGui::GetContentRegionAvail();
            uint32_t width = static_cast<uint32_t>(viewportSize.x);
            uint32_t height = static_cast<uint32_t>(viewportSize.y);

            // Redimensiona framebuffer se o tamanho mudou
            if (width > 0 && height > 0 &&
                (std::abs((float)width - m_PreviewSize.x) > 1.0f ||
                    std::abs((float)height - m_PreviewSize.y) > 1.0f))
            {
                m_PreviewSize = ImVec2((float)width, (float)height);
                m_PreviewFramebuffer->Resize(width, height);

                if (m_PreviewRenderer && m_PreviewRenderer->m_Camera)
                    m_PreviewRenderer->m_Camera->SetAspectRatio((float)width / (float)height);
            }

            m_PreviewBoundsMin = ImGui::GetCursorScreenPos();
            m_PreviewBoundsMax = ImVec2(
                m_PreviewBoundsMin.x + viewportSize.x,
                m_PreviewBoundsMin.y + viewportSize.y);

            // Exibe a textura renderizada
            ImTextureID textureID = (ImTextureID)(uintptr_t)
                m_PreviewFramebuffer->GetColorAttachmentRendererID();
            if (textureID != (ImTextureID)0)
                ImGui::Image(textureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));

            HandlePreviewInput();
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

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
    // Input da câmera do preview
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::HandlePreviewInput()
    {
        if (!m_PreviewHovered) return;

        ImGuiIO& io = ImGui::GetIO();

        ImVec2 mousePos = ImGui::GetMousePos();
        static ImVec2 lastMousePos = mousePos;
        m_PreviewMouseDelta = ImVec2(
            mousePos.x - lastMousePos.x,
            mousePos.y - lastMousePos.y);
        lastMousePos = mousePos;

        // Controles da câmera apenas com Alt pressionado
        const bool alt = io.KeyAlt;
        if (!alt) return;

        glm::vec2 delta(m_PreviewMouseDelta.x, m_PreviewMouseDelta.y);
        delta *= 0.003f;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_PreviewRenderer->OnMouseRotate(delta);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            m_PreviewRenderer->OnMousePan(delta);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            m_PreviewRenderer->OnMouseZoom(delta.y * 10.0f);

        if (io.MouseWheel != 0.0f)
            m_PreviewRenderer->OnMouseZoom(io.MouseWheel);
    }

    // -------------------------------------------------------------------------
    // Parâmetros do material
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::CompileAndApply()
    {
        ClearLog();
        LogInfo("Compilando shader...");

        if (!m_Material || !m_Graph) return;

        auto result = MaterialCompiler::Compile(m_Graph.get());
        if (!result.Success) { LogError("Compilação falhou: " + result.ErrorMessage); return; }

        std::shared_ptr<Shader> compiledShader;
        try { compiledShader = Shader::Create(result.VertexShader, result.FragmentShader); }
        catch (const std::exception& e) { LogError(std::string("Shader creation failed: ") + e.what()); return; }
        if (!compiledShader) { LogError("Shader::Create retornou null"); return; }

        m_Material->SetShader(compiledShader);
        m_Material->UsePBR = true;

        // ✅ Geometry shader para deferred
        if (!result.GeometryFragShader.empty())
        {
            try
            {
                auto geometryShader = Shader::Create(result.VertexShader, result.GeometryFragShader);
                if (geometryShader)
                    m_Material->SetGeometryShader(geometryShader);
            }
            catch (const std::exception& e)
            {
                AXE_CORE_WARN("GeometryShader creation failed: {}", e.what());
            }
        }
        m_Material->SamplerTextures = result.SamplerTextures;
        // ✅ Função para encontrar Texture Sample conectado a um pin
        std::function<Node* (ed::PinId)> findTextureSample =
            [&](ed::PinId startPin) -> Node*
            {
                for (auto& n : m_Graph->GetNodes())
                    for (auto& outPin : n->Outputs)
                    {
                        if (outPin.ID != startPin) continue;
                        if (n->Name == "Texture Sample") return n.get();
                        for (auto& inPin : n->Inputs)
                            for (auto& lnk : m_Graph->GetLinks())
                            {
                                if (lnk.EndPin != inPin.ID) continue;
                                Node* found = findTextureSample(lnk.StartPin);
                                if (found) return found;
                            }
                    }
                return nullptr;
            };

        // ✅ Encontra output node
        Node* outputNode = nullptr;
        for (auto& n : m_Graph->GetNodes())
            if (n->Name == "Material Output") { outputNode = n.get(); break; }

        // ✅ Limpa texturas — serão preenchidas apenas se houver conexão
        m_Material->AlbedoMap = nullptr; m_Material->AlbedoUUID = "";
        m_Material->NormalMap = nullptr; m_Material->NormalUUID = "";

        bool hasAlbedoConnection = false;
        if (outputNode)
        {
            // Base Color (pin 0)
            for (auto& lnk : m_Graph->GetLinks())
            {
                if (lnk.EndPin != outputNode->Inputs[0].ID) continue;
                Node* texNode = findTextureSample(lnk.StartPin);
                if (texNode && texNode->Value.TextureVal)
                {
                    m_Material->AlbedoMap = texNode->Value.TextureVal;
                    m_Material->AlbedoUUID = texNode->Value.TextureUUID;
                }
                break;
            }

            // Normal (pin 3)
            for (auto& lnk : m_Graph->GetLinks())
            {
                if (lnk.EndPin != outputNode->Inputs[3].ID) continue;
                Node* texNode = findTextureSample(lnk.StartPin);
                if (texNode && texNode->Value.TextureVal)
                {
                    m_Material->NormalMap = texNode->Value.TextureVal;
                    m_Material->NormalUUID = texNode->Value.TextureUUID;
                }
                break;
            }

            if (!hasAlbedoConnection)
                m_Material->Color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
        }

        if (m_Asset) m_Asset->SetMaterial(m_Material);
        if (!m_Asset->GetFilePath().empty()) m_Asset->Save(m_Asset->GetFilePath());
        SaveGraph();

        // Atualiza preview
        if (m_PreviewScene)
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

        // Aplica na cena
        if (m_Context && m_Context->ActiveScene && m_Asset)
        {
            auto& registry = m_Context->ActiveScene->GetRegistry();
            const AssetRecord* record = AssetDatabase::Get().GetByPath(m_Asset->GetFilePath());
            if (!record) { LogWarning("Asset não encontrado."); return; }

            std::string assetUUID = record->UUID;
            int count = 0;
            for (auto entity : registry.view<MaterialComponent>())
            {
                auto& mc = registry.get<MaterialComponent>(entity);
                if (mc.MaterialAssetUUID == assetUUID)
                {
                    mc.Data = m_Material;
                    ++count;
                }
            }
            if (count > 0) LogInfo("Material aplicado em " + std::to_string(count) + " objeto(s).");
            else LogWarning("Nenhum objeto usa este material.");
        }
        else LogWarning("Nenhuma cena ativa.");

        LogInfo("Shader compilado com sucesso.");

        if (m_ThumbnailRenderer && m_Asset)
        {
            const AssetRecord* record = AssetDatabase::Get().GetByPath(m_Asset->GetFilePath());
            if (record) m_ThumbnailRenderer->Invalidate(record->UUID);
        }

        InspectorWindow::MarkGraphCacheDirty();
    }

    void MaterialEditorWindow::DrawMaterialParams(Material& mat)
    {
        // Verifica se há um node selecionado no graph
        int selectedCount = ed::GetSelectedObjectCount();
        ed::NodeId selectedNodeId = 0;

        if (selectedCount > 0)
        {
            std::vector<ed::NodeId> selectedNodes(selectedCount);
            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), selectedCount);
            if (nodeCount > 0)
                selectedNodeId = selectedNodes[0];
        }

        if (selectedNodeId)
        {
            // Encontra o node selecionado
            auto nodePtr = m_Graph->FindNode(selectedNodeId);
            if (nodePtr)
            {
                Node* node = nodePtr->get();
                ImGui::Text("Node: %s", node->Name.c_str());
                ImGui::Separator();

                if (node->Name == "Float" && node->IsConstant)
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##val", &node->Value.FloatVal, 0.01f, 0.0f, 10.0f);
                }
                else if (node->Name == "Color" && node->IsConstant)
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::ColorEdit4("##col", &node->Value.Vec4Val.x);
                }
                else if (node->Name == "Texture Sample")
                {
                    ImGui::Text("Textura:");
                    ImGui::Spacing();

                    std::string uuid = node->Value.TextureUUID;
                    AssetPicker::Draw("##tex",
                        node->Value.TextureUUID,
                        { AssetType::Texture },
                        [&](const AssetRecord& record)
                        {
                            node->Value.TextureVal = Texture2D::Create(record.FilePath.string());
                            node->Value.TextureUUID = record.UUID;
                        });
                }
                else
                {
                    // Mostra os pins de input do node
                    ImGui::Text("Inputs:");
                    ImGui::Spacing();
                    for (auto& pin : node->Inputs)
                        ImGui::TextDisabled("• %s (%s)", pin.Name.c_str(),
                            pin.Type == PinType::Float ? "Float" :
                            pin.Type == PinType::Vec3 ? "Vec3" :
                            pin.Type == PinType::Vec4 ? "Vec4" : "?");

                    ImGui::Spacing();
                    ImGui::Text("Outputs:");
                    ImGui::Spacing();
                    for (auto& pin : node->Outputs)
                        ImGui::TextDisabled("• %s", pin.Name.c_str());
                }
                return;
            }
        }

        // Sem node selecionado — mostra parâmetros globais do material
        ImGui::TextDisabled("Selecione um node para editar.");
        ImGui::Separator();

        bool usePBR = mat.UsePBR;
        if (ImGui::Checkbox("PBR", &usePBR))
            mat.UsePBR = usePBR;

        ImGui::Separator();

        if (!mat.UsePBR)
        {
            ImGui::ColorEdit4("Cor", glm::value_ptr(mat.Color));
            ImGui::DragFloat("Specular", &mat.SpecularStrength, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Shininess", &mat.Shininess, 1.0f, 1.0f, 256.0f);
        }
        else
        {
            ImGui::DragFloat("Metallic", &mat.Metallic, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Roughness", &mat.Roughness, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("AO", &mat.AO, 0.01f, 0.0f, 1.0f);
        }
    }

    // -------------------------------------------------------------------------
    // Texture slot
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::DrawTextureSlot(const char* label,
        std::shared_ptr<Texture2D>& tex, std::string& uuid)
    {
        ImGui::PushID(label);

        ImVec2 size(48, 48);
        if (tex && tex->IsLoaded())
            ImGui::Image((ImTextureID)(uintptr_t)tex->GetRendererID(),
                size, ImVec2(0, 1), ImVec2(1, 0));
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::Button("##empty", size);
            ImGui::PopStyleColor();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
            {
                std::string dropped = (const char*)payload->Data;
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(dropped);
                if (record && record->Type == AssetType::Texture)
                {
                    tex = Texture2D::Create(record->FilePath.string());
                    uuid = dropped;
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", label);
        if (tex && tex->IsLoaded())
        {
            ImGui::TextDisabled("%dx%d", tex->GetWidth(), tex->GetHeight());
            if (ImGui::SmallButton("X")) { tex = nullptr; uuid = ""; }
        }
        else
            ImGui::TextDisabled("Nenhuma");
        ImGui::EndGroup();

        ImGui::PopID();
        ImGui::Spacing();
    }

    // -------------------------------------------------------------------------
    // Node graph
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::DrawNodeGraph()
    {
        ed::SetCurrentEditor(m_NodeEditorContext);
        ed::Begin("MaterialGraph", ImVec2(0.0f, 0.0f));

        ed::NodeId doubleClickedNode = ed::GetDoubleClickedNode();

        for (auto& [id, pos] : m_Graph->GetPendingPositions())
            ed::SetNodePosition(ed::NodeId(id), pos);
        m_Graph->ClearPendingPositions();

        for (auto& node : m_Graph->GetNodes())
            m_Graph->UpdateNodePosition(node->ID.Get(), ed::GetNodePosition(node->ID));

        for (auto& node : m_Graph->GetNodes())
        {
            if (doubleClickedNode && node->ID == doubleClickedNode &&
                node->Type == NodeType::Comment)
            {
                m_EditingCommentNode = node.get();
                m_CommentEditBuffer = node->Name;
                ImVec2 nodePos = ed::GetNodePosition(node->ID);
                m_CommentEditPopupPos = ed::CanvasToScreen(nodePos);
            }
            DrawNode(*node);
        }

        for (auto& link : m_Graph->GetLinks())
            ed::Link(link.ID, link.StartPin, link.EndPin);

        if (m_FrameCount > 2)
        {
            // --- Criação de links ---
            if (ed::BeginCreate())
            {
                Pin* newLinkPin = nullptr;

                auto showLabel = [](const char* label, ImColor color)
                    {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
                        auto size = ImGui::CalcTextSize(label);
                        auto padding = ImGui::GetStyle().FramePadding;
                        auto spacing = ImGui::GetStyle().ItemSpacing;
                        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));
                        auto rectMin = ImGui::GetCursorScreenPos() - padding;
                        auto rectMax = ImGui::GetCursorScreenPos() + size + padding;
                        ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                        ImGui::TextUnformatted(label);
                    };

                ed::PinId startPinId = 0, endPinId = 0;
                if (ed::QueryNewLink(&startPinId, &endPinId))
                {
                    auto startPin = m_Graph->FindPin(startPinId);
                    auto endPin = m_Graph->FindPin(endPinId);
                    newLinkPin = startPin ? startPin : endPin;

                    if (startPin && startPin->Kind == ed::PinKind::Input)
                    {
                        std::swap(startPin, endPin);
                        std::swap(startPinId, endPinId);
                    }

                    if (startPin && endPin)
                    {
                        if (endPin == startPin)
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        else if (endPin->Kind == startPin->Kind)
                        {
                            showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else if (endPin->Type != startPin->Type &&
                            endPin->Type != PinType::Any &&
                            startPin->Type != PinType::Any &&
                            endPin->Type != PinType::Vec3 && // Material Output aceita qualquer vec
                            startPin->Type != PinType::Vec3)
                        {
                            showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 128, 128), 2.0f);
                        }
                        else
                        {
                            showLabel("+ Create Link", ImColor(32, 45, 32, 100));
                            if (ed::AcceptNewItem(ImColor(125, 255, 128), 1.0f))
                            {
                                // Procura se o input já tem uma conexão
                                Link oldLink(0, 0, 0);
                                bool hasOldLink = false;
                                for (auto& link : m_Graph->GetLinks())
                                {
                                    if (link.EndPin == endPinId)
                                    {
                                        oldLink = link;
                                        hasOldLink = true;
                                        break;
                                    }

                                    AXE_CORE_INFO("Links Criados: '{}'", link.ID.Get());
                                }

                                if (hasOldLink)
                                {
                                    // Substitui sem registrar no histórico
                                    m_Graph->RemoveLink(oldLink.ID);
                                    m_Graph->AddLink(startPinId, endPinId);
                                }
                                else
                                {
                                    // Nova conexão — registra no histórico normalmente
                                    m_History.Push({
                                        "Add Link",
                                        [this, startPinId, endPinId]() { m_Graph->AddLink(startPinId, endPinId); },
                                        [this]() {
                                            if (!m_Graph->GetLinks().empty())
                                                m_Graph->RemoveLink(m_Graph->GetLinks().back().ID);
                                        }
                                        });
                                }
                            }
                        }
                    }
                }

                ed::EndCreate();
            }

            // --- Deleção de links ---
            if (ed::BeginDelete())
            {
                ed::LinkId deletedLink;
                //while (ed::QueryDeletedLink(&deletedLink))
                //    if (ed::AcceptDeletedItem())
                //        m_Graph->RemoveLink(deletedLink);

                while (ed::QueryDeletedLink(&deletedLink))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        auto linkId = deletedLink;
                        // Salva o link antes de deletar para poder restaurar
                        Link savedLink;
                        for (auto& l : m_Graph->GetLinks())
                            if (l.ID == linkId) { savedLink = l; break; }

                        m_History.Push({
                          "Remove Link",
                          [this, savedLink]() {
                                // Remove pelo EndPin — funciona mesmo que o ID tenha mudado
                                for (auto& l : m_Graph->GetLinks())
                                {
                                    if (l.EndPin == savedLink.EndPin && l.StartPin == savedLink.StartPin)
                                    {
                                        m_Graph->RemoveLink(l.ID);
                                        break;
                                    }
                                }
                            },
                            [this, savedLink]() {
                                m_Graph->AddLink(savedLink.StartPin, savedLink.EndPin);
                            }
                            });
                    }
                }

                ed::EndDelete();
            }

            // --- Context menus ---
            auto openPopupPosition = ImGui::GetMousePos();

            ed::Suspend();
            if (ed::ShowNodeContextMenu(&m_Graph->contextNodeId))
                ImGui::OpenPopup("Node Context Menu");
            else if (ed::ShowLinkContextMenu(&m_Graph->contextLinkId))
                ImGui::OpenPopup("Link Context Menu");
            else if (ed::ShowBackgroundContextMenu())
            {
                ImGui::OpenPopup("Create New Node");
                newNodeLinkPin = nullptr;
            }
            ed::Resume();

            ed::Suspend();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

            if (ImGui::BeginPopup("Node Context Menu"))
            {
                auto nodePtr = m_Graph->FindNode(m_Graph->contextNodeId);
                auto node = nodePtr ? nodePtr->get() : nullptr;

                ImGui::TextUnformatted("Node Context Menu");
                ImGui::Separator();
                if (node)
                {
                    ImGui::Text("ID: %p", node->ID.AsPointer());
                    ImGui::Text("Inputs: %d", (int)node->Inputs.size());
                    ImGui::Text("Outputs: %d", (int)node->Outputs.size());

                    if (ImGui::MenuItem("Delete"))
                    {
                        auto nodeId = m_Graph->contextNodeId;
                        DeleteNodeWithHistory(nodeId);
                        // ed::DeleteNode(nodeId);
                    }
                }
                else
                {
                    if (ImGui::MenuItem("Delete"))
                        ed::DeleteNode(m_Graph->contextNodeId);
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("Create New Node"))
            {
                Node* node = nullptr;

                if (ImGui::BeginMenu("Input"))
                {
                    if (ImGui::MenuItem("Texture Sample")) node = m_Graph->AddTextureSampleNode();
                    if (ImGui::MenuItem("Color"))          node = m_Graph->AddColorNode();
                    if (ImGui::MenuItem("Float"))          node = m_Graph->AddFloatNode();
                    if (ImGui::MenuItem("UV Coordinate"))  node = m_Graph->AddUVNode();
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Math"))
                {
                    if (ImGui::MenuItem("Add"))      node = m_Graph->AddAddNode();
                    if (ImGui::MenuItem("Multiply")) node = m_Graph->AddMultiplyNode();
                    if (ImGui::MenuItem("Lerp"))     node = m_Graph->AddLerpNode();
                    if (ImGui::MenuItem("Subtract")) node = m_Graph->AddSubtractNode();
                    if (ImGui::MenuItem("Divide"))   node = m_Graph->AddDivideNode();
                    if (ImGui::MenuItem("Power"))    node = m_Graph->AddPowerNode();
                    if (ImGui::MenuItem("Clamp"))      node = m_Graph->AddClampNode();
                    if (ImGui::MenuItem("Abs"))        node = m_Graph->AddAbsNode();
                    if (ImGui::MenuItem("OneMinus"))   node = m_Graph->AddOneMinusNode();

                    ImGui::EndMenu();
                }

                ImGui::Separator();
                if (ImGui::BeginMenu("Utility"))
                {
                    if (ImGui::MenuItem("World Position")) node = m_Graph->AddWorldPositionNode();
                    if (ImGui::MenuItem("Fresnel"))        node = m_Graph->AddFresnelNode();
                    if (ImGui::MenuItem("Normal Map")) node = m_Graph->AddNormalMapNode();
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Material Output")) node = m_Graph->AddMaterialOutputNode();
                ImGui::Separator();
                if (ImGui::MenuItem("Comment"))         node = m_Graph->AddComment();

                if (node)
                {
                    m_Graph->BuildNodes();
                    ed::SetNodePosition(node->ID, openPopupPosition);

                    if (auto startPin = newNodeLinkPin)
                    {
                        auto& pins = startPin->Kind == ed::PinKind::Input
                            ? node->Outputs : node->Inputs;
                        for (auto& pin : pins)
                        {
                            if (CanCreateLink(startPin, &pin))
                            {
                                auto endPin = &pin;
                                if (startPin->Kind == ed::PinKind::Input)
                                    std::swap(startPin, endPin);
                                m_Graph->m_Links.emplace_back(
                                    m_Graph->GetNextID(), startPin->ID, endPin->ID);
                                m_Graph->m_Links.back().Color = GetIconColor(startPin->Type);
                            }
                        }
                    }
                }
                ImGui::EndPopup();
            }

            // Tecla C — cria comment agrupando seleção
            if (ImGui::IsKeyPressed(ImGuiKey_C) && !ImGui::GetIO().WantTextInput)
            {
                int selectedCount = ed::GetSelectedObjectCount();
                if (selectedCount > 0)
                {
                    std::vector<ed::NodeId> selectedNodes(selectedCount);
                    ed::GetSelectedNodes(selectedNodes.data(), selectedCount);

                    if (!selectedNodes.empty())
                    {
                        ImVec2 minPos(FLT_MAX, FLT_MAX), maxPos(-FLT_MAX, -FLT_MAX);
                        for (auto nodeId : selectedNodes)
                        {
                            ImVec2 pos = ed::GetNodePosition(nodeId);
                            //ImVec2 pos = m_Graph->GetNodePosition(nodeId.Get()); // ← nodeId.Get()
                            ImVec2 size = ed::GetNodeSize(nodeId);
                            minPos.x = std::min(minPos.x, pos.x);
                            minPos.y = std::min(minPos.y, pos.y);
                            maxPos.x = std::max(maxPos.x, pos.x + size.x);
                            maxPos.y = std::max(maxPos.y, pos.y + size.y);
                        }
                        const float margin = 32.0f;
                        minPos.x -= margin; minPos.y -= margin;
                        maxPos.x += margin; maxPos.y += margin;

                        Node* comment = m_Graph->AddComment();
                        comment->Size = ImVec2(maxPos.x - minPos.x, maxPos.y - minPos.y);
                        ed::SetNodePosition(comment->ID, minPos);
                        UpdateCommentChildren(comment);
                        ed::ClearSelection();
                        ed::SelectNode(comment->ID, false);
                    }
                }
            }

            ImGui::PopStyleVar();
            ed::Resume();
        }

        // --- Edição de comentário ---
        ed::Suspend();
        if (m_EditingCommentNode)
        {
            ImGui::SetNextWindowPos(m_CommentEditPopupPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

            if (ImGui::Begin("##EditComment", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
            {
                static char buffer[2048];
                static bool focusSet = false;

                if (!focusSet)
                {
                    strncpy(buffer, m_CommentEditBuffer.c_str(), sizeof(buffer) - 1);
                    buffer[sizeof(buffer) - 1] = '\0';
                    ImGui::SetKeyboardFocusHere();
                    focusSet = true;
                }

                ImGui::InputTextMultiline("##edit", buffer, sizeof(buffer),
                    ImVec2(384, 184), ImGuiInputTextFlags_AllowTabInput);

                if (!ImGui::IsWindowFocused() || ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
                        m_EditingCommentNode->Name = buffer;
                    m_EditingCommentNode = nullptr;
                    focusSet = false;
                }
                ImGui::End();
            }
            ImGui::PopStyleVar();
        }

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput)
        {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_History.Undo();

            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_History.Redo();

            // Tecla Delete — deleta nodes selecionados
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !io.WantTextInput)
            {
                int selectedCount = ed::GetSelectedObjectCount();
                if (selectedCount > 0)
                {
                    std::vector<ed::NodeId> selectedNodes(selectedCount);
                    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), selectedCount);

                    for (int i = 0; i < nodeCount; i++)
                    {
                        DeleteNodeWithHistory(selectedNodes[i]);
                    }
                }
            }
        }


        ed::Resume();

        m_FrameCount++;
        ed::End();
        ed::SetCurrentEditor(nullptr);
    }

    // -------------------------------------------------------------------------
    // Nodes
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::DrawNode(Node& node)
    {
        if (node.Type == NodeType::Comment)
        {
            DrawCommentNode(&node);
            return;
        }

        const float NODE_WIDTH = 180.0f;

        auto drawPin = [&](Pin& pin, bool isInput)
            {
                auto col = GetPinColor(pin.Type);
                auto imcol = ImVec4(col.Value.x, col.Value.y, col.Value.z, 1.0f);
                float iconSize = (float)m_PinIconSize;
                float textHeight = ImGui::GetTextLineHeight();
                float verticalOffset = (iconSize - textHeight) * 0.5f;

                ed::BeginPin(pin.ID, isInput ? ed::PinKind::Input : ed::PinKind::Output);
                ed::PinPivotAlignment(isInput ? ImVec2(0.0f, 0.5f) : ImVec2(1.0f, 0.5f));
                ed::PinPivotSize(ImVec2(0, 0));
                ImGui::BeginGroup();

                if (isInput)
                {
                    bool connected = m_Graph->IsPinLinked(pin.ID);
                    DrawPinIcon(pin, connected, 255);
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + verticalOffset);
                    ImGui::TextColored(imcol, "%s", pin.Name.c_str());
                }
                else
                {
                    float contentWidth = ImGui::CalcTextSize(pin.Name.c_str()).x + iconSize + 8;
                    float availWidth = NODE_WIDTH * 0.5f - 8;
                    float offset = availWidth - contentWidth;
                    if (offset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

                    float baseY = ImGui::GetCursorPosY();
                    ImGui::SetCursorPosY(baseY + verticalOffset);
                    ImGui::TextColored(imcol, "%s", pin.Name.c_str());
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(baseY);
                    bool connected = m_Graph->IsPinLinked(pin.ID);
                    DrawPinIcon(pin, connected, 255);
                }

                ImGui::EndGroup();
                ed::EndPin();
            };

        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 4));
        ed::PushStyleVar(ed::StyleVar_NodeRounding, 8.0f);
        ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.5f);
        ed::BeginNode(node.ID);

        // Barra de título
        {
            ImVec2 titlePos = ImGui::GetCursorScreenPos();
            float  titleH = ImGui::GetTextLineHeightWithSpacing() + 8.0f;
            ImVec2 titleEnd = ImVec2(titlePos.x + NODE_WIDTH, titlePos.y + titleH);

            ImGui::GetWindowDrawList()->AddRectFilled(
                titlePos, titleEnd,
                ImColor(node.Color.x, node.Color.y, node.Color.z, 1.0f),
                8.0f, ImDrawFlags_RoundCornersTop);

            ImVec2 textSize = ImGui::CalcTextSize(node.Name.c_str());
            ImGui::SetCursorScreenPos(ImVec2(
                titlePos.x + (NODE_WIDTH - textSize.x) * 0.5f,
                titlePos.y + 4.0f));
            ImGui::TextUnformatted(node.Name.c_str());

            ImGui::SetCursorScreenPos(ImVec2(titlePos.x, titlePos.y + titleH));
            ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
        }

        // Conteúdo do node
        if (node.IsConstant)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
            ImGui::PushID(node.ID.AsPointer());

            if (node.Value.Type == PinType::Float && node.Outputs.size() == 1)
            {
                ImGui::SetNextItemWidth(NODE_WIDTH - 16);
                ImGui::DragFloat("##val", &node.Value.FloatVal, 0.01f, 0.0f, 1.0f);
            }
            else if (node.Value.Type == PinType::Vec4 && !node.Outputs.empty())
            {
                ImGui::SetNextItemWidth(NODE_WIDTH - 16);
                ImGui::ColorEdit4("##col", &node.Value.Vec4Val.x,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            }
            ImGui::PopID();
        }
        else if (node.Name == "Texture Sample")
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
            ImGui::PushID(node.ID.AsPointer());
            float imgSize = NODE_WIDTH - 16;

            if (node.Value.TextureVal && node.Value.TextureVal->IsLoaded())
                ImGui::Image(
                    (ImTextureID)(uintptr_t)node.Value.TextureVal->GetRendererID(),
                    ImVec2(imgSize, imgSize), ImVec2(0, 1), ImVec2(1, 0));
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
                ImGui::Button("Arraste\numa textura", ImVec2(imgSize, imgSize * 0.6f));
                ImGui::PopStyleColor();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
                {
                    std::string uuid = (const char*)payload->Data;
                    const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                    if (record && record->Type == AssetType::Texture)
                    {
                        node.Value.TextureVal = Texture2D::Create(record->FilePath.string());
                        node.Value.TextureUUID = uuid;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
        }

        // Pins
        int maxPins = (int)std::max(node.Inputs.size(), node.Outputs.size());
        const float inputColumnWidth = NODE_WIDTH * 0.5f - 8;
        const float outputColumnWidth = NODE_WIDTH * 0.5f - 8;

        for (int i = 0; i < maxPins; i++)
        {
            bool hasInput = i < (int)node.Inputs.size();
            bool hasOutput = i < (int)node.Outputs.size();

            if (hasInput)  drawPin(node.Inputs[i], true);
            else           ImGui::Dummy(ImVec2(inputColumnWidth, ImGui::GetTextLineHeight()));

            ImGui::SameLine(inputColumnWidth + 8);

            if (hasOutput) { ImGui::BeginGroup(); drawPin(node.Outputs[i], false); ImGui::EndGroup(); }
            else           ImGui::Dummy(ImVec2(outputColumnWidth, ImGui::GetTextLineHeight()));
        }

        ed::EndNode();
        ed::PopStyleVar(3);
    }

    void MaterialEditorWindow::DrawCommentNode(Node* node)
    {
        constexpr float padding = 16.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.75f);
        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 64));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 64));

        ed::BeginNode(node->ID);
        ImGui::PushID(node->ID.AsPointer());

        float textWrapWidth = std::max(100.0f, node->Size.x - padding);
        ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(
            ImGui::GetFontSize(), FLT_MAX, textWrapWidth, node->Name.c_str());

        float newTitleHeight = textSize.y + 16.0f;
        float deltaHeight = newTitleHeight - node->TitleHeight;

        if (std::abs(deltaHeight) > 0.1f)
        {
            for (int childID : node->ChildNodeIDs)
            {
                Node* child = m_Graph->FindNodeByID(childID);
                if (child)
                {
                    ImVec2 childPos = ed::GetNodePosition(child->ID);
                    ed::SetNodePosition(child->ID,
                        ImVec2(childPos.x, childPos.y + deltaHeight));
                }
            }
            node->TitleHeight = newTitleHeight;
        }

        ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
        ImGui::PushTextWrapPos(cursorScreenPos.x + textWrapWidth);
        ImGui::TextWrapped("%s", node->Name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));

        ed::Group(node->Size);
        ImGui::PopID();
        ed::EndNode();

        ed::PopStyleColor(2);
        ImGui::PopStyleVar();

        ImVec2 newSize = ed::GetNodeSize(node->ID);
        if (newSize.x > 50 && newSize.y > 50)
            node->Size = newSize;

        UpdateCommentChildren(node);

        if (ed::BeginGroupHint(node->ID))
        {
            auto bgAlpha = static_cast<int>(ImGui::GetStyle().Alpha * 255);
            auto min = ed::GetGroupMin();
            ImGui::SetCursorScreenPos(min + ImVec2(8, 8));

            std::string preview = node->Name;
            size_t newlinePos = preview.find('\n');
            if (newlinePos != std::string::npos)
                preview = preview.substr(0, newlinePos);
            if (preview.length() > 20)
                preview = preview.substr(0, 20) + "...";

            ImGui::TextUnformatted(preview.c_str());

            auto drawList = ed::GetHintBackgroundDrawList();
            auto hintBounds = ImGui_GetItemRect();
            auto hintFrameBounds = ImRect_Expanded(hintBounds, 8, 4);

            drawList->AddRectFilled(hintFrameBounds.GetTL(), hintFrameBounds.GetBR(),
                IM_COL32(255, 255, 255, 64 * bgAlpha / 255), 4.0f);
            drawList->AddRect(hintFrameBounds.GetTL(), hintFrameBounds.GetBR(),
                IM_COL32(255, 255, 255, 128 * bgAlpha / 255), 4.0f);

            ed::EndGroupHint();
        }
    }

    void MaterialEditorWindow::UpdateCommentChildren(Node* commentNode)
    {
        commentNode->ChildNodeIDs.clear();

        ImVec2 commentPos = ed::GetNodePosition(commentNode->ID);
        ImVec2 commentMin = commentPos;
        ImVec2 commentMax = ImVec2(
            commentPos.x + commentNode->Size.x,
            commentPos.y + commentNode->Size.y);

        for (auto& node : m_Graph->GetNodes())
        {
            if (node->ID == commentNode->ID) continue;

            ImVec2 nodePos = ed::GetNodePosition(node->ID);
            ImVec2 nodeSize = ed::GetNodeSize(node->ID);
            ImVec2 nodeCenter = ImVec2(
                nodePos.x + nodeSize.x * 0.5f,
                nodePos.y + nodeSize.y * 0.5f);

            if (nodeCenter.x >= commentMin.x && nodeCenter.x <= commentMax.x &&
                nodeCenter.y >= commentMin.y && nodeCenter.y <= commentMax.y)
                commentNode->ChildNodeIDs.push_back(node->ID.Get());
        }
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    ImColor MaterialEditorWindow::GetPinColor(PinType type) const
    {
        switch (type)
        {
        case PinType::Float:     return ImColor(147, 226, 74);
        case PinType::Vec2:      return ImColor(147, 226, 74);
        case PinType::Vec3:      return ImColor(0, 200, 100);
        case PinType::Vec4:      return ImColor(100, 150, 255);
        case PinType::Texture2D: return ImColor(200, 100, 200);
        default:                 return ImColor(200, 200, 200);
        }
    }

    ImColor MaterialEditorWindow::GetIconColor(PinType type)
    {
        switch (type)
        {
        case PinType::Float:     return ImColor(147, 226, 74);
        case PinType::Vec2:      return ImColor(0, 200, 100);
        case PinType::Vec3:      return ImColor(0, 200, 100);
        case PinType::Vec4:      return ImColor(100, 150, 255);
        case PinType::Texture2D: return ImColor(200, 100, 200);
        case PinType::Any:       return ImColor(200, 200, 200);
        default:                 return ImColor(200, 200, 200);
        }
    }

    void MaterialEditorWindow::DrawPinIcon(const Pin& pin, bool connected, int alpha)
    {
        ax::Drawing::IconType iconType;
        ImColor color = GetIconColor(pin.Type);
        color.Value.w = alpha / 255.0f;

        switch (pin.Type)
        {
        case PinType::Float:     iconType = ax::Drawing::IconType::Circle;      break;
        case PinType::Vec2:      iconType = ax::Drawing::IconType::Diamond;     break;
        case PinType::Vec3:      iconType = ax::Drawing::IconType::Circle;      break;
        case PinType::Vec4:      iconType = ax::Drawing::IconType::Circle;      break;
        case PinType::Texture2D: iconType = ax::Drawing::IconType::RoundSquare; break;
        case PinType::Any:       iconType = ax::Drawing::IconType::Circle;      break;
        default: return;
        }

        ax::Widgets::Icon(
            ImVec2((float)m_PinIconSize, (float)m_PinIconSize),
            iconType, connected, color, ImColor(32, 32, 32, alpha));
    }

    bool MaterialEditorWindow::CanCreateLink(Pin* a, Pin* b)
    {
        if (!a || !b || a == b) return false;
        if (a->Kind == b->Kind) return false;
        if (a->Type != b->Type) return false;
        if (a->ParentNode == b->ParentNode) return false;
        return true;
    }

    ImRect MaterialEditorWindow::ImGui_GetItemRect()
    {
        return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }

    ImRect MaterialEditorWindow::ImRect_Expanded(const ImRect& rect, float x, float y)
    {
        ImRect result = rect;
        result.Min.x -= x; result.Min.y -= y;
        result.Max.x += x; result.Max.y += y;
        return result;
    }

    void MaterialEditorWindow::UpdatePreviewCamera()
    {
        // Reservado para sincronização futura de câmera
    }

    void MaterialEditorWindow::SaveGraph()
    {
        if (!m_Asset || !m_Graph) return;

        auto graphPath = m_Asset->GetFilePath();
        graphPath.replace_extension(".axegraph");

        nlohmann::json j = m_Graph->Serialize();

        std::ofstream file(graphPath);
        if (!file.is_open())
        {
            AXE_EDITOR_INFO("MaterialEditorWindow: falha ao salvar grafo em '{}'", graphPath.string());
            return;
        }
        file << j.dump(4);
        AXE_EDITOR_INFO("MaterialEditorWindow: grafo salvo em '{}'", graphPath.string());
    }

    void MaterialEditorWindow::LoadGraph()
    {
        if (!m_Asset || !m_Graph) return;

        auto graphPath = m_Asset->GetFilePath();
        graphPath.replace_extension(".axegraph");

        if (!std::filesystem::exists(graphPath))
            return;

        std::ifstream file(graphPath);
        nlohmann::json j;
        try { j = nlohmann::json::parse(file); }
        catch (const nlohmann::json::exception& e)
        {
            AXE_CORE_ERROR("MaterialEditorWindow: erro ao carregar grafo: {}", e.what());
            return;
        }

        m_Graph->Deserialize(j);

        // ✅ Compila mas não tenta aplicar na cena — cena pode não estar pronta
        auto result = MaterialCompiler::Compile(m_Graph.get());
        if (!result.Success) return;

        try
        {
            auto compiledShader = Shader::Create(result.VertexShader, result.FragmentShader);
            if (compiledShader && m_Material)
                m_Material->SetShader(compiledShader);

            if (!result.GeometryFragShader.empty())
            {
                auto geometryShader = Shader::Create(result.VertexShader, result.GeometryFragShader);
                if (geometryShader && m_Material)
                    m_Material->SetGeometryShader(geometryShader);
            }
        }
        catch (...) {}

        // ✅ Preenche NormalMap
        Node* outputNode = nullptr;
        for (auto& n : m_Graph->GetNodes())
            if (n->Name == "Material Output") { outputNode = n.get(); break; }

        if (outputNode && outputNode->Inputs.size() > 3)
        {
            std::function<Node* (ed::PinId)> findTex = [&](ed::PinId startPin) -> Node*
                {
                    for (auto& n : m_Graph->GetNodes())
                        for (auto& outPin : n->Outputs)
                        {
                            if (outPin.ID != startPin) continue;
                            if (n->Name == "Texture Sample") return n.get();
                            for (auto& inPin : n->Inputs)
                                for (auto& lnk : m_Graph->GetLinks())
                                {
                                    if (lnk.EndPin != inPin.ID) continue;
                                    Node* found = findTex(lnk.StartPin);
                                    if (found) return found;
                                }
                        }
                    return nullptr;
                };

            for (auto& lnk : m_Graph->GetLinks())
            {
                if (lnk.EndPin != outputNode->Inputs[3].ID) continue;
                Node* texNode = findTex(lnk.StartPin);
                if (texNode && texNode->Value.TextureVal)
                {
                    m_Material->NormalMap = texNode->Value.TextureVal;
                    m_Material->NormalUUID = texNode->Value.TextureUUID;
                }
                break;
            }

            bool hasNormalConnection = false;
            for (auto& lnk : m_Graph->GetLinks())
                if (lnk.EndPin == outputNode->Inputs[3].ID)
                {
                    hasNormalConnection = true; break;
                }
            if (!hasNormalConnection)
            {
                m_Material->NormalMap = nullptr;
                m_Material->NormalUUID = "";
            }
        }

        AXE_CORE_INFO("MaterialEditorWindow: grafo carregado.");
    }

    void MaterialEditorWindow::DeleteNodeWithHistory(ed::NodeId nodeId)
    {
        auto nodePtr = m_Graph->FindNode(nodeId);
        if (!nodePtr) return;
        auto* node = nodePtr->get();

        // Salva o estado completo do node antes de deletar
        std::string nodeName = node->Name;
        ImVec2      nodePos = m_Graph->GetNodePosition(nodeId.Get());

        // Salva os valores do node
        float       floatVal = node->Value.FloatVal;
        glm::vec4   vec4Val = { node->Value.Vec4Val.x, node->Value.Vec4Val.y,
                                  node->Value.Vec4Val.z, node->Value.Vec4Val.w };
        std::string textureUUID = node->Value.TextureUUID;
        auto        textureVal = node->Value.TextureVal;

        // Salva os links conectados a este node
        std::vector<Link> connectedLinks;
        for (auto& link : m_Graph->GetLinks())
        {
            for (auto& pin : node->Inputs)
                if (link.EndPin == pin.ID || link.StartPin == pin.ID)
                {
                    connectedLinks.push_back(link); break;
                }
            for (auto& pin : node->Outputs)
                if (link.EndPin == pin.ID || link.StartPin == pin.ID)
                {
                    connectedLinks.push_back(link); break;
                }
        }

        m_History.Push({
            "Delete Node: " + nodeName,

            // Execute — deleta o node
            [this, nodeId]()
            {
                m_Graph->DeleteNode(nodeId);
                ed::DeleteNode(nodeId);
            },

            // Undo — recria o node com todos os dados
            [this, nodeName, nodePos, floatVal, vec4Val, textureUUID, textureVal, connectedLinks]()
            {
                // Recria o node pelo nome
                Node* newNode = nullptr;
                if (nodeName == "Float")           newNode = m_Graph->AddFloatNode();
                else if (nodeName == "Color")           newNode = m_Graph->AddColorNode();
                else if (nodeName == "Texture Sample")  newNode = m_Graph->AddTextureSampleNode();
                else if (nodeName == "UV Coordinate")   newNode = m_Graph->AddUVNode();
                else if (nodeName == "Multiply")        newNode = m_Graph->AddMultiplyNode();
                else if (nodeName == "Add")             newNode = m_Graph->AddAddNode();
                else if (nodeName == "Subtract")        newNode = m_Graph->AddSubtractNode();
                else if (nodeName == "Divide")          newNode = m_Graph->AddDivideNode();
                else if (nodeName == "Power")           newNode = m_Graph->AddPowerNode();
                else if (nodeName == "Lerp")            newNode = m_Graph->AddLerpNode();
                else if (nodeName == "Comment")         newNode = m_Graph->AddComment();
                else if (nodeName == "Material Output") newNode = m_Graph->AddMaterialOutputNode();
                else if (nodeName == "Clamp")           newNode = m_Graph->AddClampNode();
                else if (nodeName == "Abs")             newNode = m_Graph->AddAbsNode();
                else if (nodeName == "OneMinus")        newNode = m_Graph->AddOneMinusNode();
                else if (nodeName == "World Position")  newNode = m_Graph->AddWorldPositionNode();
                else if (nodeName == "Fresnel")         newNode = m_Graph->AddFresnelNode();
                else if (nodeName == "Normal Map") newNode = m_Graph->AddNormalMapNode();

                if (!newNode) return;

                // Restaura posição
                m_Graph->m_PendingPositions[newNode->ID.Get()] = nodePos;

                // Restaura valores
                newNode->Value.FloatVal = floatVal;
                newNode->Value.Vec4Val = { vec4Val.x, vec4Val.y, vec4Val.z, vec4Val.w };
                newNode->Value.TextureUUID = textureUUID;
                newNode->Value.TextureVal = textureVal;

                // Nota: os links não podem ser restaurados porque os IDs dos pins
                // mudam ao recriar o node. O usuário precisará reconectar.
                // (Uma solução completa exigiria salvar o mapeamento de pins)

                m_Graph->BuildNodes();
            }
            });
    }

    void MaterialEditorWindow::LogInfo(const std::string& msg)
    {
        std::istringstream stream(msg);
        std::string line;
        while (std::getline(stream, line))
            if (!line.empty())
                m_ShaderLog.push_back({ ShaderLogEntry::Level::Info, line });
    }
    void MaterialEditorWindow::LogWarning(const std::string& msg)
    {
        m_ShaderLog.push_back({ ShaderLogEntry::Level::Warnning, msg });
    }
    void MaterialEditorWindow::LogError(const std::string& msg)
    {
        // Divide mensagens multi-linha em entradas separadas
        std::istringstream stream(msg);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty())
                m_ShaderLog.push_back({ ShaderLogEntry::Level::Error, line });
        }
    }
    void MaterialEditorWindow::ClearLog()
    {
        m_ShaderLog.clear();
    }

    void MaterialEditorWindow::DrawShaderLog()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

        if (ImGui::Begin("Shader Log", nullptr, ImGuiWindowFlags_NoScrollbar))
        {
            // Botão limpar
            if (ImGui::SmallButton("Limpar"))
                ClearLog();

            ImGui::SameLine();
            ImGui::TextDisabled("%d messagens", (int)m_ShaderLog.size());

            ImGui::Separator();
            //Lista de mensagens com scroll
            ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false,
                ImGuiWindowFlags_HorizontalScrollbar);

            for (auto& entry : m_ShaderLog)
            {
                ImVec4 color;
                const char* prefix;

                switch (entry.level)
                {
                case ShaderLogEntry::Level::Info:
                    color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    prefix = "[INFO]";
                    break;
                case ShaderLogEntry::Level::Warnning:
                    color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
                    prefix = "[WARN]";
                    break;
                case ShaderLogEntry::Level::Error:
                    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                    prefix = "[ERR]";
                    break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted((prefix + entry.message).c_str());
                ImGui::PopStyleColor();
            }

            //Auto-Scroll para o final
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();

    }

} // namespace axe