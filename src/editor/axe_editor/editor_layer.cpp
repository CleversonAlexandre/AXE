#include "editor_layer.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/scene/components.hpp"
#include "editor/axe_editor/editor_app.hpp"
#include "axe/script/script_base.hpp"
#include "axe/physics/physics_system.hpp"
#include <iostream>

namespace axe
{
    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_EditorUI(std::make_unique<EditorUI>())
    {}

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::OnAttach()
    {
        AXE_EDITOR_INFO("EditorLayer attached");

        axe::Input::Init(&EditorApp::Get().GetWindow());

        m_Scene = std::make_unique<Scene>();
        m_Context.ActiveScene = m_Scene.get();
        m_Context.SelectedEntity = entt::null;
        m_EditorUI->SetContext(&m_Context);

        // ── Callbacks do menu File ────────────────────────────────────────────
        m_EditorUI->OnNewScene = [this]()
            {
                m_Scene = std::make_unique<Scene>();
                m_Context.ActiveScene = m_Scene.get();
                m_Context.SelectedEntity = entt::null;
                m_ViewportRenderer->SetScene(m_Scene.get());
                m_ViewportRenderer->SetSelectedEntity(&m_Context.SelectedEntity);
                m_CurrentScenePath.clear();
                EnsureEnvironmentComponent();
                AXE_EDITOR_INFO("Nova cena criada.");
            };

        m_EditorUI->OnOpenScene = [this](const std::string& path)
            {
                if (m_EditorState != EditorState::Edit)
                {
                    AXE_EDITOR_WARN("Não é possível carregar cena durante o Play. Pressione Stop primeiro.");
                    return;
                }
                m_Scene = std::make_unique<Scene>();
                m_Context.ActiveScene = m_Scene.get();
                m_Context.SelectedEntity = entt::null;
                m_ViewportRenderer->SetScene(m_Scene.get());
                m_ViewportRenderer->SetSelectedEntity(&m_Context.SelectedEntity);
                SceneSerializer::Deserialize(path, *m_Scene, &m_Environment);
                m_CurrentScenePath = path;
                EnsureEnvironmentComponent();
                AXE_EDITOR_INFO("Cena aberta: {}", path);
            };

        m_EditorUI->OnSaveScene = [this](const std::string& path)
            {
                if (m_EditorState != EditorState::Edit)
                {
                    AXE_EDITOR_WARN("Não é possível salvar cena durante o Play. Pressione Stop primeiro.");
                    return;
                }
                std::string savePath = path.empty() ? m_CurrentScenePath : path;
                if (savePath.empty())
                {
                    // BUGFIX: isso chamava m_EditorUI->OnSaveScene("") de novo,
                    // recursivamente — se o usuário CANCELASSE o diálogo,
                    // FileDialog::Save devolve path vazio, e a chamada
                    // recursiva caía direto nesse MESMO "if (savePath.empty())"
                    // de novo, abrindo o diálogo outra vez, pra sempre (só
                    // saía digitando um nome de verdade ou fechando o motor).
                    // Agora trata o resultado do diálogo aqui mesmo, sem
                    // recursão: cancelar só sai da função, sem salvar nada.
                    auto chosen = FileDialog::Save("AXE Scene\0*.axescene\0", "Salvar Cena", "axescene");
                    if (chosen.empty())
                    {
                        AXE_EDITOR_INFO("Salvar cena cancelado.");
                        return;
                    }
                    SceneSerializer::Serialize(*m_Scene, chosen.string(), &m_Environment);
                    m_CurrentScenePath = chosen.string();
                    AXE_EDITOR_INFO("Cena salva em: {}", m_CurrentScenePath);
                    return;
                }
                SceneSerializer::Serialize(*m_Scene, savePath, &m_Environment);
                m_CurrentScenePath = savePath;
                AXE_EDITOR_INFO("Cena salva em: {}", savePath);
            };

        m_EditorUI->OnUndo = [this]() { m_CommandHistory.Undo(); };
        m_EditorUI->OnRedo = [this]() { m_CommandHistory.Redo(); };
        m_EditorUI->OnCanUndo = [this]() { return m_CommandHistory.CanUndo(); };
        m_EditorUI->OnCanRedo = [this]() { return m_CommandHistory.CanRedo(); };
        m_EditorUI->IsPlaying = [this]() { return m_EditorState != EditorState::Edit; };

        m_EditorUI->OnSaveProject = [this]()
            {
                if (m_EditorState != EditorState::Edit)
                {
                    AXE_EDITOR_WARN("Não é possível salvar o projeto durante o Play. Pressione Stop primeiro.");
                    return;
                }
                if (!ProjectManager::Get().HasProject())
                {
                    AXE_EDITOR_WARN("Nenhum projeto aberto pra salvar.");
                    return;
                }
                ProjectManager::Get().SaveProject();
                AXE_EDITOR_INFO("Projeto salvo: {}", ProjectManager::Get().GetCurrent().Name);
            };

        m_EditorUI->OnOpenProject = [this](const std::string& path)
            {
                if (m_EditorState != EditorState::Edit)
                {
                    AXE_EDITOR_WARN("Não é possível abrir outro projeto durante o Play. Pressione Stop primeiro.");
                    return;
                }
                // Troca o EditorLayer inteiro — ver comentário em
                // EditorApp::RequestReopenProject pra entender por quê (e
                // por que isso roda agendado, não direto aqui).
                if (!EditorApp::Get().RequestReopenProject(path))
                    AXE_EDITOR_WARN("Falha ao abrir o projeto: {}", path);
            };

        m_EditorUI->OnDrawEnvironment = [this]()
            {
                std::string shortPath = m_Environment.SkyboxPath.empty() ? "Nenhum" :
                    std::filesystem::path(m_Environment.SkyboxPath).filename().string();
                ImGui::TextDisabled("HDRI:");
                ImGui::SameLine();
                ImGui::Text("%s", shortPath.c_str());
                if (ImGui::Button("Carregar HDRI..."))
                {
                    auto p = FileDialog::Open("HDR Image\0*.hdr;*.exr\0All Files\0*.*\0",
                        "Selecionar HDRI", "hdr");
                    if (!p.empty()) m_Environment.LoadHDRI(p.string());
                }
                ImGui::Separator();
                ImGui::DragFloat("Rotação Skybox", &m_Environment.SkyboxRotation, 1.0f, -360.0f, 360.0f);
            };

        // ── Callback do AssetBrowser — drop de arquivo ────────────────────────
        m_EditorUI->GetAssetBrowser()->SetFileDropCallback(
            [this](const std::string& filepath)
            {
                std::string uuid = AssetDatabase::Get().Register(filepath);
                LoadedAsset asset = MeshLoader::Load(filepath);
                if (!asset.MeshData) { AXE_CORE_ERROR("EditorLayer: falha ao carregar '{}'", filepath); return; }

                std::string name = std::filesystem::path(filepath).stem().string();
                auto& registry = m_Scene->GetRegistry();
                entt::entity entity = m_Scene->CreateEntity(name);

                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = asset.MeshData;
                mc.AssetUUID = uuid;

                if (asset.MaterialData)
                    registry.emplace<MaterialComponent>(entity, asset.MaterialData);

                m_Context.Select(entity);

                if (ProjectManager::Get().HasProject())
                {
                    auto& root = ProjectManager::Get().GetCurrent().RootPath;
                    AssetDatabase::Get().Save(root);
                    m_EditorUI->GetAssetBrowser()->SaveFolders(root);
                }
                AXE_CORE_INFO("EditorLayer: '{}' importado. UUID: {}", name, uuid);
            });

        m_EditorUI->GetAssetBrowser()->SetAssetOpenCallback([this](const AssetRecord& record)
            {
                if (record.Type == AssetType::Material)
                {
                    auto matAsset = MaterialAsset::LoadFromFile(record.FilePath);
                    if (matAsset)
                        m_EditorUI->m_MaterialEditorWindow.OpenMaterial(matAsset);
                    m_EditorUI->m_MaterialEditorWindow.OpenMaterial(matAsset);
                }
            });

        // ── Callback de instanciação (drag para viewport / asset browser) ─────
        auto instantiate = [this](const std::string& uuid)
            {
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                if (!record) return;
                auto& registry = m_Scene->GetRegistry();

                if (record->Type == AssetType::Material)
                {
                    if (!m_Context.HasSelection()) return;
                    entt::entity selected = m_Context.SelectedEntity;
                    if (!registry.valid(selected)) return;
                    if (!registry.all_of<MeshComponent>(selected)) return;

                    auto matAsset = MaterialAsset::LoadFromFile(record->FilePath);
                    if (!matAsset) return;
                    auto material = matAsset->GetMaterial();

                    if (SceneSerializer::GetMaterialRecompileCallback())
                        SceneSerializer::GetMaterialRecompileCallback()(uuid, material.get());

                    auto graphPath = record->FilePath;
                    graphPath.replace_extension(".axegraph");
                    if (std::filesystem::exists(graphPath))
                    {
                        std::ifstream file(graphPath);
                        try
                        {
                            nlohmann::json j = nlohmann::json::parse(file);
                            MaterialGraph graph;
                            graph.Deserialize(j);
                            int slot = 0;
                            for (auto& node : graph.GetNodes())
                            {
                                if (node->Name != "Texture Sample") continue;
                                if (!node->Value.TextureVal) { ++slot; continue; }
                                bool isConnected = false;
                                for (auto& output : node->Outputs)
                                    for (auto& link : graph.GetLinks())
                                        if (link.StartPin == output.ID) isConnected = true;
                                if (isConnected && slot == 0)
                                {
                                    material->AlbedoMap = node->Value.TextureVal;
                                    material->AlbedoUUID = node->Value.TextureUUID;
                                }
                                ++slot;
                            }
                        }
                        catch (...) {}
                    }

                    auto matComp = MaterialComponent{ material };
                    matComp.MaterialAssetUUID = uuid;
                    if (registry.all_of<MaterialComponent>(selected))
                        registry.get<MaterialComponent>(selected) = matComp;
                    else
                        registry.emplace<MaterialComponent>(selected, matComp);

                    AXE_EDITOR_INFO("Material '{}' aplicado.", record->Name);
                    return;
                }

                if (MeshFactory::IsPrimitive(uuid))
                {
                    auto mesh = MeshFactory::CreateByUUID(uuid);
                    if (!mesh) return;
                    auto entity = m_Scene->CreateEntity(record->Name);
                    auto& mc = registry.emplace<MeshComponent>(entity);
                    mc.Data = mesh; mc.AssetUUID = uuid;
                    m_Context.Select(entity);
                    return;
                }

                if (record->Type == AssetType::Script)
                {
                    InstantiateScriptAsset(record->FilePath, uuid);
                    return;
                }

                LoadedAsset asset = MeshLoader::Load(record->FilePath.string());
                if (!asset.MeshData) return;
                auto entity = m_Scene->CreateEntity(record->Name);
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = asset.MeshData; mc.AssetUUID = uuid;
                if (asset.MaterialData) registry.emplace<MaterialComponent>(entity, asset.MaterialData);
                m_Context.Select(entity);
            };

        m_EditorUI->GetAssetBrowser()->SetInstantiateCallback(instantiate);

        m_EditorUI->GetViewport()->SetAssetDropCallback(
            [this](const std::string& uuid, float mouseX, float mouseY)
            {
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                if (!record) return;
                auto& registry = m_Scene->GetRegistry();

                if (record->Type == AssetType::Material)
                {
                    uint32_t pickID = m_ViewportRenderer->PickObject(mouseX, mouseY);
                    if (pickID != 0)
                    {
                        entt::entity picked = (entt::entity)pickID;
                        if (registry.valid(picked)) m_Context.Select(picked);
                    }
                }

                if (MeshFactory::IsPrimitive(uuid))
                {
                    auto mesh = MeshFactory::CreateByUUID(uuid);
                    if (!mesh) return;
                    auto entity = m_Scene->CreateEntity(record->Name);
                    auto& mc = registry.emplace<MeshComponent>(entity);
                    mc.Data = mesh; mc.AssetUUID = uuid;
                    m_Context.Select(entity);
                    return;
                }

                if (record->Type == AssetType::Material)
                {
                    if (!m_Context.HasSelection()) return;
                    entt::entity selected = m_Context.SelectedEntity;
                    if (!registry.valid(selected) || !registry.all_of<MeshComponent>(selected)) return;
                    auto matAsset = MaterialAsset::LoadFromFile(record->FilePath);
                    if (!matAsset) return;
                    auto material = matAsset->GetMaterial();
                    if (SceneSerializer::GetMaterialRecompileCallback())
                        SceneSerializer::GetMaterialRecompileCallback()(uuid, material.get());
                    auto matComp = MaterialComponent{ material };
                    matComp.MaterialAssetUUID = uuid;
                    if (registry.all_of<MaterialComponent>(selected))
                        registry.get<MaterialComponent>(selected) = matComp;
                    else
                        registry.emplace<MaterialComponent>(selected, matComp);
                    return;
                }

                if (record->Type == AssetType::Script)
                {
                    InstantiateScriptAsset(record->FilePath, uuid);
                    return;
                }

                LoadedAsset asset = MeshLoader::Load(record->FilePath.string());
                if (!asset.MeshData) return;
                auto entity = m_Scene->CreateEntity(record->Name);
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = asset.MeshData; mc.AssetUUID = uuid;
                if (asset.MaterialData) registry.emplace<MaterialComponent>(entity, asset.MaterialData);
                m_Context.Select(entity);
            });

        // ── Inicializa o viewport ─────────────────────────────────────────────
        ViewportWindow* viewport = m_EditorUI->GetViewport();
        if (viewport)
        {
            viewport->Initialize();
            viewport->SetGuizmoCallback([this](const glm::vec2& min, const glm::vec2& max)
                {
                    m_ViewportRenderer->DrawGuizmo(min, max);
                });

            viewport->SetDragPreviewCallback([this](const std::string& uuid) -> std::string
                {
                    const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                    if (!record) { m_ViewportRenderer->ClearDragGhost(); return ""; }

                    std::shared_ptr<Mesh> ghostMesh;
                    if (MeshFactory::IsPrimitive(uuid))
                        ghostMesh = MeshFactory::CreateByUUID(uuid);
                    else if (record->Type == AssetType::Script)
                    {
                        const std::string& ct = record->ScriptClassType;
                        if (ct == "Trigger")               ghostMesh = MeshFactory::CreateByUUID(axe::PrimitiveUUID::Sphere);
                        else if (ct == "Character" || ct == "Agent") ghostMesh = MeshFactory::CreateByUUID(axe::PrimitiveUUID::Cylinder);
                        else                               ghostMesh = MeshFactory::CreateByUUID(axe::PrimitiveUUID::Cube);
                    }
                    else if (record->Type == AssetType::Mesh)
                        ghostMesh = MeshFactory::CreateByUUID(axe::PrimitiveUUID::Cube);

                    if (ghostMesh && m_ViewportRenderer->m_Camera)
                    {
                        glm::vec3 pos = m_ViewportRenderer->m_Camera->GetPosition()
                            + m_ViewportRenderer->m_Camera->GetForwardDirection() * 5.0f;
                        m_ViewportRenderer->SetDragGhost(ghostMesh, glm::translate(glm::mat4(1.0f), pos));
                    }

                    std::string info = record->Name;
                    if (record->Type == AssetType::Script && !record->ScriptClassType.empty())
                        info += " [" + record->ScriptClassType + "]";
                    return info;
                });

            viewport->SetDragEndCallback([this]() { m_ViewportRenderer->ClearDragGhost(); });

            viewport->SetPlayStateCallback([this]() -> int
                {
                    if (m_EditorState == EditorState::Play)  return 1;
                    if (m_EditorState == EditorState::Pause) return 2;
                    return 0;
                });

            viewport->SetPlayActionCallback([this](int action)
                {
                    if (action == 0)
                    {
                        if (m_EditorState == EditorState::Edit) EnterPlay();
                        else if (m_EditorState == EditorState::Pause)
                        {
                            m_Context.ClearSelection();
                            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
                            EditorApp::Get().GetWindow().CaptureCursor(true);
                            m_GameCamera.MouseCaptured = true;
                            m_GameCamera.m_FirstMouse = true;
                            m_ViewportRenderer->SetGameCamera(&m_GameCamera);
                            m_EditorState = EditorState::Play;
                            AXE_EDITOR_INFO("Modo Play retomado.");
                        }
                    }
                    else if (action == 1) EnterPause();
                    else if (action == 2) EnterEdit();
                });
        }
        else
        {
            AXE_EDITOR_ERROR("ViewportWindow is null during OnAttach!");
        }

        // ── Inicializa o renderer ─────────────────────────────────────────────
        m_ViewportRenderer = std::make_unique<axe::ViewportRenderer>();
        m_ViewportRenderer->Initialize();
        m_ViewportRenderer->SetCommandHistory(&m_CommandHistory);
        m_EditorUI->GetHierarchy()->SetCommandHistory(&m_CommandHistory);
        viewport->SetViewportRenderer(m_ViewportRenderer.get());
        m_ViewportRenderer->SetScene(m_Scene.get());
        m_ViewportRenderer->SetSelectedEntity(&m_Context.SelectedEntity);
        m_ViewportRenderer->SetEnvironment(&m_Environment);
        m_EditorUI->SetViewportRenderer(m_ViewportRenderer.get());

        m_ThumbnailRenderer.Initialize();
        m_EditorUI->GetAssetBrowser()->SetThumbnailRenderer(&m_ThumbnailRenderer);
        m_EditorUI->m_MaterialEditorWindow.SetThumbnailRenderer(&m_ThumbnailRenderer);

        // ── Material recompile callback (SceneSerializer) ─────────────────────
        SceneSerializer::SetMaterialRecompileCallback(
            [](const std::string& assetUUID, Material* material)
            {
                if (!material) return;
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
                if (!record) return;
                auto graphPath = record->FilePath;
                graphPath.replace_extension(".axegraph");
                if (!std::filesystem::exists(graphPath)) return;
                std::ifstream file(graphPath);
                try
                {
                    nlohmann::json j = nlohmann::json::parse(file);
                    MaterialGraph graph;
                    graph.Deserialize(j);
                    auto result = MaterialCompiler::Compile(&graph);
                    if (!result.Success) return;
                    auto forwardShader = Shader::Create(result.VertexShader, result.FragmentShader);
                    if (forwardShader) material->SetShader(forwardShader);
                    if (!result.GeometryFragShader.empty())
                    {
                        auto geometryShader = Shader::Create(result.VertexShader, result.GeometryFragShader);
                        if (geometryShader) material->SetGeometryShader(geometryShader);
                    }
                    material->SamplerTextures = result.SamplerTextures;
                    if (result.AlbedoTexture) material->AlbedoMap = result.AlbedoTexture;
                    if (result.NormalTexture) material->NormalMap = result.NormalTexture;
                }
                catch (...) {}
            });

        m_Environment.LoadHDRI("resources/quarry_04_puresky_2k.hdr");
        EditorIconLibrary::Get().Load("resources");

        m_EditorUI->m_MaterialEditorWindow.Initialize();
        m_EditorUI->m_ScriptGraphWindow.Initialize();
        m_EditorUI->m_ScriptGraphWindow.SetInspectorWindow(&m_EditorUI->m_InspectorWindow);

        // ── Script editor callbacks ───────────────────────────────────────────
        m_EditorUI->GetAssetBrowser()->SetScriptOpenCallback([this](const std::string& uuid)
            {
                AXE_CORE_INFO("ScriptOpenCallback chamado, uuid={}", uuid);
                const auto* rec = AssetDatabase::Get().GetByUUID(uuid);
                if (!rec) { AXE_CORE_ERROR("ScriptOpenCallback: UUID nao encontrado no AssetDatabase"); return; }
                AXE_CORE_INFO("ScriptOpenCallback: abrindo {}", rec->FilePath.string());
                auto asset = ScriptAsset::LoadFromFile(rec->FilePath);
                if (!asset) { AXE_CORE_ERROR("ScriptOpenCallback: falha ao carregar ScriptAsset"); return; }
                m_EditorUI->m_ScriptGraphWindow.OpenForAsset(asset);
                AXE_CORE_INFO("ScriptOpenCallback: OpenForAsset chamado, IsOpen={}", m_EditorUI->m_ScriptGraphWindow.IsOpen());
            });

        m_EditorUI->m_InspectorWindow.m_OnOpenScript = [this](entt::entity e, ScriptComponent* sc, entt::registry* registry)
            {
                if (!sc->ScriptAssetPath.empty())
                {
                    auto asset = ScriptAsset::LoadFromFile(sc->ScriptAssetPath);
                    if (asset) { m_EditorUI->m_ScriptGraphWindow.OpenForAsset(asset); return; }
                }
                m_EditorUI->m_ScriptGraphWindow.OpenForEntity(e, sc, registry);
            };

        if (ProjectManager::Get().HasProject())
            m_EditorUI->GetAssetBrowser()->LoadFolders(ProjectManager::Get().GetCurrent().RootPath);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::OnDetach()
    {
        m_EditorUI.reset();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::OnUpdate(float deltaTime)
    {
        // Carrega a cena no primeiro frame (após OpenGL estar pronto)
        if (!m_SceneLoaded)
        {
            m_SceneLoaded = true;
            if (ProjectManager::Get().HasStartScene())
            {
                SceneSerializer::Deserialize(
                    ProjectManager::Get().GetStartScenePath().string(), *m_Scene);
            }
            else
            {
                std::string defaultScene = "resources/default_scene/main.axescene";
                if (std::filesystem::exists(defaultScene))
                    SceneSerializer::Deserialize(defaultScene, *m_Scene, &m_Environment);
                else
                {
                    m_Scene->CreateLight("Directional Light");
                    auto ppEntity = m_Scene->CreateEntity("Post Process Volume");
                    m_Scene->GetRegistry().emplace<PostProcessComponent>(ppEntity);
                    m_Scene->CreateEntity("Enviroment");
                }
                EnsureEnvironmentComponent();
            }
        }

        m_DeltaTime = deltaTime;
        m_FPSAccumulator += 1.0f / deltaTime;
        m_FPSSamples++;
        if (m_FPSSamples >= 60)
        {
            m_FPS = m_FPSAccumulator / m_FPSSamples;
            m_FPSAccumulator = 0.0f;
            m_FPSSamples = 0;
        }

        if (m_EditorUI && m_EditorUI->GetAssetBrowser())
            m_EditorUI->GetAssetBrowser()->Update();
        else
            AXE_CORE_ERROR("AssetBrowser é nullptr!");

        if (m_EditorState == EditorState::Play)
        {
            ImGui::GetIO().WantCaptureKeyboard = false;
            ImGui::SetNextFrameWantCaptureKeyboard(false);

            if (m_PlayerEntity != entt::null && m_Scene)
            {
                auto* tc = m_Scene->GetRegistry().try_get<TransformComponent>(m_PlayerEntity);
                if (tc) m_GameCamera.SetTarget(&tc->Data.Position);
            }

            m_GameCamera.OnUpdate(deltaTime, &EditorApp::Get().GetWindow());
            m_ScriptWorld.OnSceneUpdate(*m_Scene, deltaTime);
            m_PhysicsWorld.OnUpdate(*m_Scene, deltaTime);
            axe::ScriptBase::TickScreenMessages(deltaTime);

            bool escNow = EditorApp::Get().GetWindow().IsKeyDown((int)Key::Escape);
            if (escNow && !m_EscWasPressed) EnterPause();
            m_EscWasPressed = escNow;
        }
        else
        {
            m_EscWasPressed = false;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::OnRender()
    {
        m_ThumbnailRenderer.RenderPending();

        if (m_EditorState == EditorState::Play)
            m_ViewportRenderer->SetGameCamera(&m_GameCamera);
        else
            m_ViewportRenderer->SetGameCamera(nullptr);

        if (m_EditorUI)
        {
            ViewportWindow* viewport = m_EditorUI->GetViewport();
            if (viewport && viewport->IsInitialized())
            {
                auto framebuffer = viewport->GetFramebuffer();
                if (framebuffer && viewport->GetWidth() > 0 && viewport->GetHeight() > 0)
                {
                    m_ViewportRenderer->RenderToFramebuffer(
                        *framebuffer, viewport->GetWidth(), viewport->GetHeight(),
                        EditorApp::Get().GetWindow().GetTime());
                }
            }
        }

        if (m_EditorUI)
        {
            if (m_EditorUI->m_MaterialEditorWindow.IsOpen())
                m_EditorUI->m_MaterialEditorWindow.RenderPreview();

            if (m_EditorUI->m_ScriptGraphWindow.IsOpen())
                m_EditorUI->m_ScriptGraphWindow.RenderPreview();

            m_EditorUI->Draw();

            if (m_EditorUI->m_ScriptGraphWindow.IsOpen())
            {
                m_EditorUI->m_ScriptGraphWindow.SetActiveScene(m_Scene.get());
                m_EditorUI->m_ScriptGraphWindow.Draw();
            }

            // ── On-screen messages (Print String) ────────────────────────────
            if (m_EditorState != EditorState::Edit)
            {
                auto* dl = ImGui::GetForegroundDrawList();


                const auto& msgs = axe::ScriptBase::GetScreenMessages();
                if (!msgs.empty())
                {
                    ViewportWindow* vp = m_EditorUI->GetViewport();
                    if (vp && vp->IsInitialized())
                    {
                        float x = vp->GetBoundsMin().x + 16.f;
                        float y = vp->GetBoundsMin().y + 50.f;

                        for (const auto& msg : msgs)
                        {
                            if (msg.Text.empty()) continue;
                            float alpha = std::min(1.f, msg.TimeLeft / 0.5f);
                            float textW = (float)msg.Text.size() * 8.f;

                            dl->AddRectFilled(
                                ImVec2(x - 8, y - 4), ImVec2(x + textW + 8, y + 20),
                                IM_COL32(0, 0, 0, (int)(200 * alpha)), 4.f);
                            dl->AddRect(
                                ImVec2(x - 8, y - 4), ImVec2(x + textW + 8, y + 20),
                                IM_COL32(255, 200, 0, (int)(180 * alpha)), 4.f);
                            dl->AddText(ImVec2(x, y),
                                IM_COL32(255, 255, 0, (int)(255 * alpha)),
                                msg.Text.c_str());
                            y += 28.f;
                        }
                    }
                }
            }

            if (m_EditorState == EditorState::Edit || m_EditorState == EditorState::Pause)
                HandleViewportCameraInput();

            HandleSceneInput();

            std::string title = "AXE Engine — " + std::to_string((int)m_FPS) + " FPS";
            EditorApp::Get().GetWindow().SetTitle(title);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);

        dispatcher.Dispatch<FileDropEvent>([this](FileDropEvent& event)
            {
                m_EditorUI->GetAssetBrowser()->OnFileDrop(event.GetPath());
                return true;
            });

        dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event)
            {
                if (m_EditorState == EditorState::Play && event.GetKeyCode() == GLFW_KEY_ESCAPE)
                {
                    EnterPause();
                    return true;
                }
                return false;
            });
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::DrawPlayToolbar()
    {
        ImGui::SetNextWindowPos(
            ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 30.0f),
            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(160, 40), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("##toolbar", nullptr, flags);

        bool isEdit = m_EditorState == EditorState::Edit;
        bool isPlay = m_EditorState == EditorState::Play;
        bool isPause = m_EditorState == EditorState::Pause;

        if (isPlay) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button("▶ Play", ImVec2(50, 26)))
        {
            if (isEdit)        EnterPlay();
            else if (isPause) { m_GameCamera.MouseCaptured = true; m_EditorState = EditorState::Play; }
        }
        if (isPlay) ImGui::PopStyleColor();

        ImGui::SameLine();

        if (isPause) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.6f, 0.0f, 1.0f));
        if (ImGui::Button("⏸", ImVec2(26, 26)) && isPlay) EnterPause();
        if (isPause) ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("⏹", ImVec2(26, 26)) && !isEdit) EnterEdit();

        ImGui::End();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::HandleSceneInput()
    {
        ImGuiIO& io = ImGui::GetIO();

        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
            m_CommandHistory.Undo();
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) ||
            (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))))
            m_CommandHistory.Redo();

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            if (m_EditorUI->m_MaterialEditorWindow.IsOpen() &&
                m_EditorUI->m_MaterialEditorWindow.IsFocused())
                m_EditorUI->m_MaterialEditorWindow.SaveGraph();
            else if (m_EditorUI->OnSaveScene)
                m_EditorUI->OnSaveScene(m_CurrentScenePath);
        }

        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            auto path = FileDialog::Save("AXE Scene\0*.axescene\0", "Salvar Cena Como", "axescene");
            if (!path.empty() && m_EditorUI->OnSaveScene)
                m_EditorUI->OnSaveScene(path.string());
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        {
            auto path = FileDialog::Open("AXE Scene\0*.axescene\0All Files\0*.*\0", "Abrir Cena", "axescene");
            if (!path.empty() && m_EditorUI->OnOpenScene)
                m_EditorUI->OnOpenScene(path.string());
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N))
            if (m_EditorUI->OnNewScene) m_EditorUI->OnNewScene();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::HandleViewportCameraInput()
    {
        if (!m_EditorUI) return;
        ViewportWindow* viewport = m_EditorUI->GetViewport();
        if (!viewport || !viewport->IsHovered()) return;

        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsKeyPressed(ImGuiKey_P))
        {
            m_ViewportRenderer->m_Camera->isPerspective = !m_ViewportRenderer->m_Camera->isPerspective;
            if (!m_ViewportRenderer->m_Camera->isPerspective)
                m_ViewportRenderer->m_Camera->viewHeight =
                m_ViewportRenderer->m_Camera->viewWidth * io.DisplaySize.y / io.DisplaySize.x;
            AXE_EDITOR_INFO("Perspective mode: {}", m_ViewportRenderer->m_Camera->isPerspective);
        }

        if (viewport->IsFocused())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::SCALE;
            if (ImGui::IsKeyPressed(ImGuiKey_T)) m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::TRANSLATE;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !ImGuizmo::IsOver())
        {
            ImVec2 boundsMin = { viewport->GetBoundsMin().x, viewport->GetBoundsMin().y };
            ImVec2 mousePos = ImGui::GetMousePos();
            float localX = mousePos.x - boundsMin.x;
            float localY = mousePos.y - boundsMin.y;

            if (localX >= 0 && localY >= 0 &&
                localX < viewport->GetWidth() && localY < viewport->GetHeight())
            {
                std::uint32_t pickID = m_ViewportRenderer->PickObject(localX, localY);
                if (pickID == 0)
                    m_Context.ClearSelection();
                else
                {
                    entt::entity picked = (entt::entity)pickID;
                    if (m_Scene->GetRegistry().valid(picked))
                        m_Context.Select(picked);
                    else
                        m_Context.ClearSelection();
                }
            }
        }

        const bool alt = io.KeyAlt;
        if (!alt) return;

        glm::vec2 delta = viewport->GetMouseDelta();
        delta *= 0.003f;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))        m_ViewportRenderer->OnMouseRotate(delta);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_ViewportRenderer->OnMousePan(delta);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))  m_ViewportRenderer->OnMouseZoom(delta.y * 10.0f);

        if (io.MouseWheel != 0.0f) m_ViewportRenderer->OnMouseZoom(io.MouseWheel);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::EnsureEnvironmentComponent()
    {
        if (!m_Scene) return;
        auto& registry = m_Scene->GetRegistry();

        for (auto entity : registry.view<EnvironmentComponent>())
        {
            if (registry.any_of<FolderComponent>(entity))
                registry.remove<FolderComponent>(entity);
            return;
        }

        for (auto entity : registry.view<NameComponent>())
        {
            auto& name = registry.get<NameComponent>(entity).Name;
            if (name == "Enviroment" && !registry.any_of<FolderComponent>(entity))
            {
                if (!registry.any_of<EnvironmentComponent>(entity))
                {
                    auto& ec = registry.emplace<EnvironmentComponent>(entity);
                    ec.HDRIPath = m_Environment.SkyboxPath.empty() ? "resources/quarry_04_puresky_2k.hdr" : m_Environment.SkyboxPath;
                    ec.SkyboxRotation = m_Environment.SkyboxRotation;
                }
                return;
            }
        }

        auto envEntity = m_Scene->CreateEntity("Enviroment");
        auto& ec = registry.emplace<EnvironmentComponent>(envEntity);
        ec.HDRIPath = m_Environment.SkyboxPath.empty() ? "resources/quarry_04_puresky_2k.hdr" : m_Environment.SkyboxPath;
        ec.SkyboxRotation = 0.0f;
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::SaveScene()
    {
        if (m_EditorState != EditorState::Edit)
        {
            AXE_EDITOR_WARN("Pressione Stop antes de salvar a cena."); return;
        }
        if (!ProjectManager::Get().HasProject()) return;

        auto scenePath = ProjectManager::Get().GetCurrent().AssetsPath / "Scenes" / "main.axescene";
        SceneSerializer::Serialize(*m_Scene, scenePath, &m_Environment);

        auto& project = ProjectManager::Get().GetCurrent();
        project.StartScene = "Assets/Scenes/main.axescene";
        ProjectManager::Get().SaveProject();
        AXE_EDITOR_INFO("Cena salva e definida como padrão.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::LoadScene()
    {
        if (m_EditorState != EditorState::Edit)
        {
            AXE_EDITOR_WARN("Pressione Stop antes de carregar uma cena."); return;
        }
        if (!ProjectManager::Get().HasProject()) return;

        auto scenePath = ProjectManager::Get().GetCurrent().AssetsPath / "Scenes" / "main.axescene";
        if (!std::filesystem::exists(scenePath))
        {
            AXE_CORE_WARN("EditorLayer: cena não encontrada em '{}'", scenePath.string()); return;
        }

        m_Scene = std::make_unique<Scene>();
        m_Context.ActiveScene = m_Scene.get();
        m_Context.SelectedEntity = entt::null;
        m_ViewportRenderer->SetScene(m_Scene.get());
        m_ViewportRenderer->SetSelectedEntity(&m_Context.SelectedEntity);
        SceneSerializer::Deserialize(scenePath, *m_Scene, &m_Environment);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::EnterPlay()
    {
        if (m_EditorState != EditorState::Edit) return;

        m_SceneSnapshot = SceneSerializer::SerializeToString(*m_Scene);

        // ── Conecta callbacks do Jolt ao ScriptWorld ──────────────────────────
        // Feito ANTES de OnSceneStart para que os bodies já criados sejam cobertos.
        // Os callbacks recebem entt::entity como uint32_t (via UserData do body).
        PhysicsSystem::Get().SetCollisionCallback(
            [this](uint32_t a, uint32_t b)
            {
                if (!m_Scene) return;
                m_ScriptWorld.DispatchCollision(*m_Scene,
                    (entt::entity)a, (entt::entity)b);
            });

        PhysicsSystem::Get().SetTriggerCallbacks(
            [this](uint32_t a, uint32_t b)
            {
                if (!m_Scene) return;
                // Para triggers: chama OnCollision em ambas as partes
                // (comportamento padrão — personagem encosta no trigger)
                m_ScriptWorld.DispatchCollision(*m_Scene,
                    (entt::entity)a, (entt::entity)b);
                m_ScriptWorld.DispatchTriggerEnter(*m_Scene,
                    (entt::entity)a, (entt::entity)b);
            },
            [this](uint32_t a, uint32_t b)
            {
                if (!m_Scene) return;
                m_ScriptWorld.DispatchTriggerExit(*m_Scene,
                    (entt::entity)a, (entt::entity)b);
            });

        m_PhysicsWorld.OnSceneStart(*m_Scene);
        m_ScriptWorld.OnSceneStart(*m_Scene);

        m_PlayerEntity = entt::null;
        if (ProjectManager::Get().HasProject())
        {
            const std::string& gmUUID = ProjectManager::Get().GetCurrent().ActiveGameModeUUID;
            if (!gmUUID.empty())
            {
                const AssetRecord* gmRec = AssetDatabase::Get().GetByUUID(gmUUID);
                if (gmRec)
                {
                    auto gmAsset = GameModeAsset::LoadFromFile(gmRec->FilePath);
                    if (gmAsset && gmAsset->HasDefaultPawn())
                    {
                        auto& registry = m_Scene->GetRegistry();
                        registry.view<ScriptComponent>().each([&](entt::entity e, ScriptComponent& sc)
                            {
                                if (m_PlayerEntity != entt::null) return;
                                const AssetRecord* rec = AssetDatabase::Get().GetByPath(sc.ScriptAssetPath);
                                if (rec && rec->UUID == gmAsset->DefaultPawnScriptUUID)
                                    m_PlayerEntity = e;
                            });

                        if (m_PlayerEntity != entt::null)
                        {
                            auto* sa = registry.try_get<SpringArmComponent>(m_PlayerEntity);
                            auto* tc = registry.try_get<TransformComponent>(m_PlayerEntity);

                            if (sa)
                            {
                                m_GameCamera.CameraMode = GameCamera::Mode::ThirdPerson;
                                m_GameCamera.TPDistance = sa->Length;
                                m_GameCamera.TPHeight = sa->HeightOffset;
                                m_GameCamera.TPLagSpeed = sa->LagSpeed;
                                m_GameCamera.TPMouseRotates = sa->MouseRotates;
                            }
                            if (auto* cam = registry.try_get<CameraComponent>(m_PlayerEntity))
                            {
                                m_GameCamera.Fov = cam->Fov;
                                m_GameCamera.NearClip = cam->NearClip;
                                m_GameCamera.FarClip = cam->FarClip;
                                m_GameCamera.Sensitivity = cam->Sensitivity;
                            }
                            if (tc)
                            {
                                glm::vec3 startPos = tc->Data.Position +
                                    glm::vec3(0, m_GameCamera.TPHeight, m_GameCamera.TPDistance);
                                m_GameCamera.Reset(startPos, -90.0f, -10.0f);
                                m_GameCamera.SetTarget(&tc->Data.Position);
                            }
                            AXE_EDITOR_INFO("GameMode: pawn encontrado (entity {}), câmera third person.", (uint32_t)m_PlayerEntity);
                        }
                    }
                }
            }
        }

        if (m_Scene)
        {
            auto& registry = m_Scene->GetRegistry();
            for (auto entity : registry.view<CameraComponent>())
            {
                auto& cam = registry.get<CameraComponent>(entity);
                if (cam.IsPrimary)
                {
                    m_GameCamera.Fov = cam.Fov;
                    m_GameCamera.NearClip = cam.NearClip;
                    m_GameCamera.FarClip = cam.FarClip;
                    m_GameCamera.MoveSpeed = cam.MoveSpeed;
                    m_GameCamera.Sensitivity = cam.Sensitivity;
                    if (auto* tc = registry.try_get<TransformComponent>(entity))
                        m_GameCamera.Reset(tc->Data.Position, tc->Data.Rotation.y, tc->Data.Rotation.x);
                    break;
                }
            }
        }

        auto& editorCam = m_ViewportRenderer->m_Camera;
        if (!m_Scene || m_Scene->GetRegistry().view<CameraComponent>().empty())
        {
            m_GameCamera.Reset(editorCam->GetPosition(),
                glm::degrees(editorCam->GetYaw()),
                glm::degrees(editorCam->GetPitch()));
        }

        m_GameCamera.MouseCaptured = true;
        m_GameCamera.m_FirstMouse = true;
        m_ViewportRenderer->SetGameCamera(&m_GameCamera);
        m_Context.ClearSelection();
        m_EditorState = EditorState::Play;

        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        EditorApp::Get().GetWindow().CaptureCursor(true);
        AXE_EDITOR_INFO("Modo Play iniciado.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::EnterPause()
    {
        if (m_EditorState != EditorState::Play) return;
        m_GameCamera.MouseCaptured = false;
        m_EditorState = EditorState::Pause;
        m_ViewportRenderer->SetGameCamera(nullptr);
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        EditorApp::Get().GetWindow().CaptureCursor(false);
        AXE_EDITOR_INFO("Modo Pause — editor ativo.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::EnterEdit()
    {
        if (m_EditorState == EditorState::Edit) return;

        m_ScriptWorld.OnSceneStop(*m_Scene);
        m_GameCamera.CameraMode = GameCamera::Mode::FreeFly;
        m_GameCamera.ClearTarget();
        m_PlayerEntity = entt::null;
        m_PhysicsWorld.OnSceneStop(*m_Scene);
        axe::ScriptBase::ClearScreenMessages();

        GLFWwindow* window = (GLFWwindow*)EditorApp::Get().GetWindow().GetNativeWindow();
        if (window) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        if (!m_SceneSnapshot.empty())
        {
            m_Scene = std::make_unique<Scene>();
            m_Context.ActiveScene = m_Scene.get();
            m_Context.SelectedEntity = entt::null;
            m_ViewportRenderer->SetScene(m_Scene.get());
            m_ViewportRenderer->SetSelectedEntity(&m_Context.SelectedEntity);
            SceneSerializer::DeserializeFromString(m_SceneSnapshot, *m_Scene);
            m_SceneSnapshot.clear();
            EnsureEnvironmentComponent();
            m_CommandHistory.Clear();
            if (m_EditorUI) m_EditorUI->GetHierarchy()->SetContext(&m_Context);
        }

        m_GameCamera.MouseCaptured = false;
        m_ViewportRenderer->SetGameCamera(nullptr);
        m_EditorState = EditorState::Edit;
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        AXE_EDITOR_INFO("Modo Edit restaurado.");
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::InstantiateScriptAsset(const std::filesystem::path& scriptPath,
        const std::string& assetUUID)
    {
        auto scriptAsset = ScriptAsset::LoadFromFile(scriptPath);
        if (!scriptAsset) return;

        auto& registry = m_Scene->GetRegistry();
        auto entity = m_Scene->CreateEntity(scriptAsset->GetName());

        for (const auto& def : scriptAsset->GetComponents())
        {
            if (def.Type == "Mesh")
            {
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = MeshFactory::CreateByUUID(
                    def.AssetUUID.empty() ? axe::PrimitiveUUID::Cube : def.AssetUUID);
                mc.AssetUUID = def.AssetUUID;
            }
            else if (def.Type == "Rigidbody")
            {
                RigidbodyComponent rb;
                rb.Type = def.BodyType == "Static" ? BodyType::Static :
                    def.BodyType == "Kinematic" ? BodyType::Kinematic : BodyType::Dynamic;
                rb.Mass = def.Mass; rb.Friction = def.Friction; rb.Restitution = def.Restitution;
                rb.LinearDamping = def.LinearDamping; rb.AngularDamping = def.AngularDamping;
                rb.UseGravity = def.UseGravity;
                rb.LockRotX = def.LockRotX; rb.LockRotY = def.LockRotY; rb.LockRotZ = def.LockRotZ;
                registry.emplace<RigidbodyComponent>(entity, rb);
            }
            else if (def.Type == "Collider" || def.Type.find("Collider") != std::string::npos)
            {
                ColliderComponent col;
                if (def.ColliderShape == "Sphere")     col.Shape = ColliderShape::Sphere;
                else if (def.ColliderShape == "Capsule")    col.Shape = ColliderShape::Capsule;
                else if (def.ColliderShape == "Mesh")       col.Shape = ColliderShape::Mesh;
                else                                        col.Shape = ColliderShape::Box;
                col.HalfExtent = { def.ColliderSizeX, def.ColliderSizeY, def.ColliderSizeZ };
                col.Radius = def.ColliderRadius;
                col.Height = def.ColliderHeight;
                col.CapsuleRadius = def.ColliderCapsuleRadius;
                col.Offset = { def.ColliderOffsetX, def.ColliderOffsetY, def.ColliderOffsetZ };
                col.IsTrigger = def.IsTrigger;
                col.ShowDebug = def.ShowDebug;
                registry.emplace<ColliderComponent>(entity, col);
            }
            else if (def.Type == "CharacterController")
            {
                CharacterControllerComponent cc;
                cc.Height = def.CCHeight; cc.Radius = def.CCRadius;
                cc.MaxSlopeAngle = def.CCMaxSlope; cc.StepHeight = def.CCStepHeight;
                cc.MaxSpeed = def.CCMaxSpeed; cc.JumpForce = def.CCJumpForce;
                registry.emplace<CharacterControllerComponent>(entity, cc);
            }
            else if (def.Type == "SpringArm")
            {
                SpringArmComponent sa;
                sa.Length = def.SALength / 100.0f;
                sa.HeightOffset = def.SAHeightOffset;
                sa.SocketOffset = { def.SASocketOffX, def.SASocketOffY, def.SASocketOffZ };
                sa.LagSpeed = def.SALagSpeed;
                sa.EnableCameraLag = def.SAEnableLag;
                sa.MouseRotates = def.SAMouseRotates;
                registry.emplace<SpringArmComponent>(entity, sa);
            }
            else if (def.Type == "Camera")
            {
                CameraComponent cam;
                cam.Fov = def.CamFov; cam.NearClip = def.CamNearClip;
                cam.FarClip = def.CamFarClip; cam.Sensitivity = def.CamSensitivity;
                cam.IsPrimary = def.CamIsPrimary;
                registry.emplace<CameraComponent>(entity, cam);
            }
            else if (def.Type == "Material" && !def.AssetUUID.empty())
            {
                const AssetRecord* r = AssetDatabase::Get().GetByUUID(def.AssetUUID);
                if (r)
                {
                    auto ma = MaterialAsset::LoadFromFile(r->FilePath);
                    if (ma)
                    {
                        MaterialComponent mc{ ma->GetMaterial() };
                        mc.MaterialAssetUUID = def.AssetUUID;
                        registry.emplace<MaterialComponent>(entity, mc);
                    }
                }
            }
        }

        ScriptComponent sc;
        sc.ScriptAssetPath = scriptPath.string();
        sc.ScriptName = scriptAsset->GetName();
        sc.DllPath = scriptAsset->DllPath;
        sc.IsCompiled = scriptAsset->IsCompiled;
        registry.emplace<ScriptComponent>(entity, sc);

        m_Context.Select(entity);
        AXE_EDITOR_INFO("InstantiateScriptAsset: '{}' instanciado.", scriptAsset->GetName());
    }

} // namespace axe