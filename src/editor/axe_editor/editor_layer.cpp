#include "editor_layer.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/anim_graph_asset.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/particles/particle_system_asset.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/scene/components.hpp"
#include "editor/axe_editor/editor_app.hpp"
#include "axe/script/script_base.hpp"
#include "axe/physics/physics_system.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>

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

                // A ultima cena aberta passa a ser a cena de abertura do
                // projeto — no proximo boot ela sobe sozinha, sem file dialog.
                ProjectManager::Get().SetStartScene(path);

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

                // Salvou = e nela que voce esta trabalhando. Vira a cena de
                // abertura do projeto.
                ProjectManager::Get().SetStartScene(savePath);
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
                }
                else if (record.Type == AssetType::ParticleSystem)
                {
                    auto particleAsset = ParticleSystemAsset::LoadFromFile(record.FilePath);
                    if (particleAsset)
                        m_EditorUI->m_ParticleEditorWindow.OpenAsset(particleAsset);
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

                // Arquivo de ANIMACAO? Abre o Animation Editor no clipe.
                // (mesma checagem nos outros caminhos de abrir/instanciar —
                // ver TryOpenAnimationFile.)
                if (TryOpenAnimationFile(*record))
                    return;

                if (record->FilePath.extension() == ".axeskel")
                {
                    // Duplo-clique no personagem SPAWNA (comportamento
                    // classico). O Animation Editor abre pelo duplo-clique
                    // no ARQUIVO DE ANIMACAO — decisao do Clever: o gatilho
                    // e a animacao, nao o personagem.
                    SpawnSkeletalMesh(*record, uuid);
                    return;
                }

                // AnimGraph nao vira entidade — ABRE O EDITOR.
                //
                // E um asset de comportamento, nao de cena.
                if (record->FilePath.extension() == ".axeanim")
                {
                    AXE_EDITOR_INFO("AnimGraph: abrindo '{}'...", record->Name);

                    auto graphAsset = AnimGraphAsset::LoadFromFile(record->FilePath);

                    if (!graphAsset)
                    {
                        AXE_EDITOR_ERROR("AnimGraph: falha ao ler '{}'. Arquivo corrompido?",
                            record->FilePath.string());
                        return;
                    }

                    // Resolve contra o esqueleto que o .axeanim referencia —
                    // e o que da ao editor a lista de clipes.
                    std::shared_ptr<SkeletalMeshAsset> skel;

                    if (const AssetRecord* skelRec =
                        AssetDatabase::Get().GetByUUID(graphAsset->GetSkeletonUUID()))
                    {
                        skel = SkeletalMeshAsset::LoadFromFile(skelRec->FilePath);
                        if (skel && skel->Resolve())
                            graphAsset->Resolve(*skel);
                    }
                    else
                    {
                        AXE_EDITOR_ERROR("AnimGraph '{}': o esqueleto (.axeskel) referenciado nao foi "
                            "encontrado. O editor abre, mas sem lista de clipes.", record->Name);
                    }

                    m_EditorUI->m_AnimGraphWindow.OpenForAsset(graphAsset, skel);

                    AXE_EDITOR_INFO("AnimGraph: janela aberta (IsOpen={}, esqueleto={}).",
                        m_EditorUI->m_AnimGraphWindow.IsOpen(),
                        skel ? "ok" : "AUSENTE");
                    return;
                }

                if (record->Type == AssetType::Script)
                {
                    InstantiateScriptAsset(record->FilePath, uuid);
                    return;
                }

                if (record->Type == AssetType::ParticleSystem)
                {
                    auto particleAsset = ParticleSystemAsset::LoadFromFile(record->FilePath);
                    if (!particleAsset) return;
                    auto entity = m_Scene->CreateEntity(record->Name);
                    auto& ps = registry.emplace<ParticleSystemComponent>(entity);
                    ps.Data = particleAsset;
                    ps.ParticleAssetUUID = uuid;
                    ps.EmitterRuntimes.resize(particleAsset->Emitters.size());
                    m_Context.Select(entity);
                    return;
                }

                // Arquivo SO DE ANIMACAO nao vira mesh estatica — abre o
                // Animation Editor. Sem esta linha o MeshLoader reclamava
                // "nao contem malha" e o clipe ficava inalcancavel por este
                // caminho.
                if (TryOpenAnimationFile(*record))
                    return;

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

                if (record->Type == AssetType::ParticleSystem)
                {
                    auto particleAsset = ParticleSystemAsset::LoadFromFile(record->FilePath);
                    if (!particleAsset) return;
                    auto entity = m_Scene->CreateEntity(record->Name);
                    auto& ps = registry.emplace<ParticleSystemComponent>(entity);
                    ps.Data = particleAsset;
                    ps.ParticleAssetUUID = uuid;
                    m_Context.Select(entity);
                    ps.EmitterRuntimes.resize(particleAsset->Emitters.size());
                    return;
                }

                // Personagem animado.
                //
                // TEM que vir antes do fallback abaixo: sem este branch, o
                // .axeskel (que e JSON) caía no MeshLoader, que tentava
                // parsea-lo como FBX e morria com
                // "FBX-Tokenize ... unexpected colon" — os dois-pontos do
                // proprio JSON.
                if (record->FilePath.extension() == ".axeskel")
                {
                    SpawnSkeletalMesh(*record, uuid);
                    return;
                }

                // ── .axeanim ARRASTADO PRA VIEWPORT ───────────────────────
                //
                // Um AnimGraph nao vira entidade — ele nao tem geometria. Voce
                // o ATRIBUI a um personagem, pelo Inspector. Entao arrastar um
                // .axeanim aqui deve ABRIR O EDITOR, igual ao duplo-clique.
                //
                // Sem este branch, o .axeanim caía no fallback abaixo e ia pro
                // MeshLoader, que tentava le-lo como modelo:
                //   "No suitable reader found for the file format".
                //
                // Chaveado por EXTENSAO, e nao por record->Type: um .axeanim
                // registrado por build antigo tem Type velho no .axemeta, e o
                // branch por Type falharia silenciosamente — a mesma armadilha
                // que ja mordeu o slot do Inspector.
                if (record->FilePath.extension() == ".axeanim")
                {
                    auto graphAsset = AnimGraphAsset::LoadFromFile(record->FilePath);
                    if (!graphAsset) return;

                    std::shared_ptr<SkeletalMeshAsset> skel;

                    if (const AssetRecord* skelRec =
                        AssetDatabase::Get().GetByUUID(graphAsset->GetSkeletonUUID()))
                    {
                        skel = SkeletalMeshAsset::LoadFromFile(skelRec->FilePath);
                        if (skel && skel->Resolve())
                            graphAsset->Resolve(*skel);
                    }

                    m_EditorUI->m_AnimGraphWindow.OpenForAsset(graphAsset, skel);
                    return;
                }

                // Fallback: qualquer outra coisa vira mesh estatica.
                // Arquivo SO DE ANIMACAO nao vira mesh estatica — abre o
                // Animation Editor. Sem esta linha o MeshLoader reclamava
                // "nao contem malha" e o clipe ficava inalcancavel por este
                // caminho.
                if (TryOpenAnimationFile(*record))
                    return;

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
                    else if (record->Type == AssetType::SkeletalMesh)
                    {
                        // Sem isto, arrastar um .axeskel nao mostrava fantasma
                        // nenhum — e a falta de feedback visual e exatamente o
                        // que faz o usuario achar que "nao da pra arrastar".
                        ghostMesh = MeshFactory::CreateByUUID(axe::PrimitiveUUID::Cylinder);
                    }

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
                    material->AlbedoMap = result.AlbedoTexture;
                    material->NormalMap = result.NormalTexture;
                    material->IsTransparent = result.IsTransparent;
                    material->BakedEmissive = MaterialCompiler::ComputeBakedEmissive(&graph);
                }
                catch (...) {}
            });

        // ── Light Material recompile callback (SceneSerializer) ───────────────
        // Re-resolve o shader/samplers do Light Material a partir do UUID
        // quando a cena é desserializada (snapshot do Stop ou load do disco).
        // O compilador de light function vive no editor, por isso é callback.
        SceneSerializer::SetLightMaterialRecompileCallback(
            [](const std::string& assetUUID,
                std::shared_ptr<Shader>& outShader,
                std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers) -> bool
            {
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
                if (!record) return false;
                return MaterialCompiler::CompileLightFunctionFromFile(
                    record->FilePath, outShader, outSamplers);
            });

        SceneSerializer::SetParticleMaterialRecompileCallback(
            [](const std::string& assetUUID,
                std::shared_ptr<Shader>& outShader,
                std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers) -> bool
            {
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
                if (!record) return false;
                return MaterialCompiler::CompileParticleFunctionFromFile(
                    record->FilePath, outShader, outSamplers);
            });

        m_Environment.LoadHDRI("resources/quarry_04_puresky_2k.hdr");
        EditorIconLibrary::Get().Load("resources");

        m_EditorUI->m_MaterialEditorWindow.Initialize();
        m_EditorUI->m_ParticleEditorWindow.Initialize();
        m_EditorUI->m_ScriptGraphWindow.Initialize();
        m_EditorUI->m_AnimGraphWindow.Initialize();
        m_EditorUI->m_AnimClipWindow.Initialize();
        m_EditorUI->m_AnimClipWindow.SetAssetBrowser(&m_EditorUI->m_AssetBowserWindow);

        AXE_EDITOR_INFO("EditorLayer — BPSYNC_SAFE_V3 (sync nao derruba script + capsula no preview + reparent por menu)");
        m_EditorUI->m_ScriptGraphWindow.SetInspectorWindow(&m_EditorUI->m_InspectorWindow);

        m_EditorUI->m_ScriptGraphWindow.SetScriptSavedCallback(
            [this](const std::filesystem::path& p) { SyncScriptInstances(p); });

        // ── Script editor callbacks ───────────────────────────────────────────
        // Rename com editor aberto: o asset carregado segura o caminho antigo
        // e regravaria o arquivo velho no proximo Save.
        m_EditorUI->GetAssetBrowser()->SetAssetRenamedCallback(
            [this](const std::filesystem::path& oldPath,
                const std::filesystem::path& newPath,
                const std::string& newName)
            {
                m_EditorUI->m_ScriptGraphWindow.HandleAssetRenamed(oldPath, newPath, newName);
            });

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

        m_EditorUI->m_InspectorWindow.m_OnOpenParticleSystem = [this](std::shared_ptr<ParticleSystemAsset> asset)
            {
                m_EditorUI->m_ParticleEditorWindow.OpenAsset(asset);
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
                // &m_Environment estava FALTANDO aqui (o caminho da cena
                // default logo abaixo sempre passou). Sem ele, abrir o projeto
                // pela cena de abertura perdia o environment silenciosamente —
                // e so por esse caminho, o que tornava o bug confuso.
                SceneSerializer::Deserialize(
                    ProjectManager::Get().GetStartScenePath().string(), *m_Scene, &m_Environment);

                m_CurrentScenePath = ProjectManager::Get().GetStartScenePath().string();
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

        // Partículas tickam em Edit (preview ao vivo) E em Play; congelam no Pause.
        // AutoDestroy só é permitido em Play — em Edit nunca destrói entities.
        if (m_Scene && m_EditorState != EditorState::Pause)
        {
            bool inPlay = (m_EditorState == EditorState::Play);
            glm::vec3 camPos(0.f);
            if (m_ViewportRenderer && m_ViewportRenderer->m_Camera)
                camPos = m_ViewportRenderer->m_Camera->GetPosition();
            m_ParticleWorld.OnUpdate(*m_Scene, deltaTime, inPlay, camPos);

            // Animação tem que rodar ANTES do render: o SceneCollector lê a
            // BonePalette que este update acabou de escrever. Se rodasse
            // depois, o personagem ficaria sempre um frame atrasado — o que
            // aparece como "tremida" sutil em movimento rápido.
            //
            // Em Edit (inPlay == false) o tempo não avança, mas a palette
            // continua sendo calculada: é o que mostra o personagem na
            // bind pose em vez de colapsado na origem.
            m_AnimationWorld.OnUpdate(*m_Scene, deltaTime, inPlay);
        }

        // Preview do Particle Editor tem sua própria ParticleWorld/cena —
        // tickado independente do estado de Play/Pause da cena principal.
        if (m_EditorUI && m_EditorUI->m_ParticleEditorWindow.IsOpen())
            m_EditorUI->m_ParticleEditorWindow.UpdatePreview(deltaTime);

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

            if (m_EditorUI->m_ParticleEditorWindow.IsOpen())
                m_EditorUI->m_ParticleEditorWindow.RenderPreview();

            if (m_EditorUI->m_ScriptGraphWindow.IsOpen())
                m_EditorUI->m_ScriptGraphWindow.RenderPreview();

            if (m_EditorUI->m_AnimGraphWindow.IsOpen())
                m_EditorUI->m_AnimGraphWindow.RenderPreview();

            if (m_EditorUI->m_AnimClipWindow.IsOpen())
                m_EditorUI->m_AnimClipWindow.RenderPreview();

            m_EditorUI->Draw();

            if (m_EditorUI->m_ScriptGraphWindow.IsOpen())
            {
                m_EditorUI->m_ScriptGraphWindow.SetActiveScene(m_Scene.get());
                m_EditorUI->m_ScriptGraphWindow.Draw();
            }

            // FORA do if acima — de proposito.
            //
            // Estava dentro, e a janela do AnimGraph so aparecia quando o
            // Script Editor tambem estivesse aberto. O duplo-clique
            // funcionava, o asset carregava, m_Open virava true... e ninguem
            // desenhava. O proprio Draw() ja checa IsOpen().
            m_EditorUI->m_AnimGraphWindow.Draw();
            m_EditorUI->m_AnimClipWindow.Draw();

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
                if (m_EditorState == EditorState::Play && event.GetKeyCode() == static_cast<int>(axe::Key::Escape))
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
    // ═══════════════════════════════════════════════════════════════════
    //  Arquivo de animacao -> Animation Editor
    //
    //  Um FBX/DAE/GLTF de animacao nao sabe de quem e: quem sabe e o
    //  .axeskel que o registrou como entrada. Varremos os .axeskel do
    //  projeto perguntando "este arquivo e teu?" e abrimos o editor no
    //  clipe certo.
    //
    //  ESTE METODO EXISTE PORQUE HA TRES CAMINHOS de abrir/instanciar asset
    //  no editor (AssetOpen, Instantiate e o drop da viewport). O tratamento
    //  vivia inline em UM deles — nos outros dois, o FBX de animacao caia no
    //  fallback de mesh estatica e o MeshLoader reclamava "nao contem malha".
    //  Mesma armadilha das lambdas duplicadas que ja mordeu no .axeskel.
    //
    //  Devolve true quando ABRIU (o chamador deve retornar).
    // ═══════════════════════════════════════════════════════════════════
    bool EditorLayer::TryOpenAnimationFile(const AssetRecord& record)
    {
        std::string ext = record.FilePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char ch) { return (char)std::tolower(ch); });

        const bool isModelFile =
            (ext == ".fbx" || ext == ".dae" || ext == ".gltf" || ext == ".glb");

        if (!isModelFile)
            return false;

        std::string scanned;
        int scannedCount = 0;

        for (const auto& [ruuid, rec] : AssetDatabase::Get().GetAll())
        {
            if (rec.FilePath.extension() != ".axeskel")
                continue;

            auto skel = SkeletalMeshAsset::LoadFromFile(rec.FilePath);

            if (!skel)
                continue;

            ++scannedCount;

            const int entryIdx = skel->FindAnimationEntryBySource(record.FilePath);

            if (entryIdx < 0)
            {
                scanned += "\n  - '" + skel->GetName() + "' referencia: ";

                const auto& entries = skel->GetAnimations();

                if (entries.empty())
                    scanned += "(nenhuma animacao)";

                for (std::size_t e = 0; e < entries.size(); ++e)
                {
                    if (e) scanned += ", ";
                    scanned += entries[e].SourceFile.generic_string();
                }

                continue;
            }

            // CONTINUE, nao break: um .axeskel quebrado (ex.: apontando pra
            // um FBX que perdeu a malha) nao pode abortar a varredura e
            // esconder o personagem certo que vem depois.
            if (!skel->Resolve())
            {
                AXE_EDITOR_WARN("Animation Editor: '{}' referencia este arquivo, mas nao resolve — pulando.",
                    skel->GetName());
                continue;
            }

            // Auto-cura: se o casamento veio pelo NOME (arquivo movido de
            // pasta), grava o caminho atual no .axeskel — da proxima vez
            // casa direto, sem fallback.
            skel->UpdateAnimationSource((std::size_t)entryIdx, record.FilePath);

            const std::string clipName = skel->GetAnimations()[entryIdx].Name;

            m_EditorUI->m_AnimClipWindow.OpenForAsset(skel);
            m_EditorUI->m_AnimClipWindow.SelectClipByName(clipName);

            AXE_EDITOR_INFO("Animation Editor: '{}' pertence a '{}' — aberto no clipe '{}'.",
                record.Name, skel->GetName(), clipName);

            return true;
        }

        if (scannedCount > 0)
        {
            AXE_EDITOR_WARN("'{}' nao esta registrado como animacao de nenhum .axeskel ({} verificado(s)):{}"
                "\n  Se e um clipe, importe no personagem (Inspector -> Importar animacao) ou arraste pro AnimGraph.",
                record.Name, scannedCount, scanned);
        }

        return false;
    }

    void EditorLayer::SpawnSkeletalMesh(const AssetRecord& record, const std::string& uuid)
    {
        auto asset = SkeletalMeshAsset::LoadFromFile(record.FilePath);

        if (!asset || !asset->Resolve())
        {
            AXE_EDITOR_ERROR("Falha ao abrir o Skeletal Mesh '{}'. Veja o console.", record.Name);
            return;
        }

        auto& registry = m_Scene->GetRegistry();
        auto entity = m_Scene->CreateEntity(record.Name);

        auto& sk = registry.emplace<SkeletalMeshComponent>(entity);

        // O Asset e a fonte de verdade — e o que faz o personagem sobreviver
        // ao salvar/reabrir a cena. Sem ele, a entidade e orfa.
        sk.Asset = asset;
        sk.AssetUUID = uuid;

        sk.Data = asset->GetMesh();
        sk.Clips = asset->GetClips();
        sk.CurrentClip = asset->GetClips().empty() ? -1 : 0;

        // Esqueleto visivel no primeiro spawn: e a forma mais rapida de ver,
        // de cara, se o rig entrou certo.
        sk.ShowSkeleton = true;

        m_Context.Select(entity);

        AXE_EDITOR_INFO("Personagem '{}' na cena: {} bones, {} clipe(s).",
            record.Name,
            asset->GetSkeleton()->GetBoneCount(),
            asset->GetClips().size());
    }

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

        // Clone de memoria. Nada passa por JSON, nada e perdido — nem o
        // ProbeGrid, nem a pose de um personagem, nem o runtime de um emissor.
        m_SceneSnapshot.Capture(*m_Scene);

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
        m_ScriptWorld.SetActiveCamera(&m_GameCamera); // injeta câmera pra scripts
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
        m_ParticleWorld.OnSceneStop(*m_Scene);
        m_GameCamera.CameraMode = GameCamera::Mode::FreeFly;
        m_GameCamera.ClearTarget();
        m_PlayerEntity = entt::null;
        m_PhysicsWorld.OnSceneStop(*m_Scene);
        axe::ScriptBase::ClearScreenMessages();

        // Libera o cursor ao parar o Play — via abstração Window, sem GLFW cru.
        EditorApp::Get().GetWindow().CaptureCursor(false);

        if (!m_SceneSnapshot.IsEmpty())
        {
            // A Scene NAO e recriada — so o registry dela e reposto. Os
            // ponteiros do viewport e do contexto continuam validos, e nao ha
            // nada pra reconectar.
            //
            // Sumiu daqui o hack que preservava ProbeGrid e ReflectionCapture
            // atraves do Play/Stop: eram shared_ptr que o JSON nao conseguia
            // carregar, e sem o hack o rebake custava ~20s a CADA Stop. O
            // clone de memoria copia shared_ptr como shared_ptr — o problema
            // deixou de existir, em vez de ser contornado.
            const entt::entity previouslySelected = m_Context.SelectedEntity;

            m_SceneSnapshot.Restore(*m_Scene);
            m_SceneSnapshot.Clear();

            // Os IDs sao preservados pelo restore, entao a selecao sobrevive.
            m_Context.SelectedEntity =
                m_Scene->GetRegistry().valid(previouslySelected)
                ? previouslySelected
                : entt::null;

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
    // ═══════════════════════════════════════════════════════════════════
    //  Propagacao BP -> instancias na cena
    //
    //  Editar o Blueprint e ver a cena continuar velha e o pior dos mundos:
    //  o autor tem que apagar e recolocar cada instancia pra ver a mudanca.
    //  A Unreal resolve isso no "Compile"; aqui, no Salvar/Compilar.
    //
    //  Regras:
    //   - Atualiza CAMPOS DE CONFIGURACAO dos componentes que ja existem, em
    //     vez de recriar: o CharacterController carrega estado de runtime
    //     (CharacterID do Jolt, IsCreated, contatos ativos) que nao pode ser
    //     jogado fora sem derrubar o personagem no chao.
    //   - Componente que o BP passou a ter e a instancia nao tem: cria.
    //   - NAO toca no Transform da entidade: posicao/rotacao/escala na cena
    //     sao colocacao POR INSTANCIA — reescrever isso empilharia todos os
    //     inimigos na origem a cada save.
    // ═══════════════════════════════════════════════════════════════════
    int EditorLayer::SyncScriptInstances(const std::filesystem::path& scriptPath)
    {
        if (!m_Scene || scriptPath.empty())
            return 0;

        // Durante o PLAY, nao. Reescrever componentes de um personagem em
        // movimento (capsula, controller, material) no meio da partida e
        // pedir para o jogo se comportar de forma estranha; o lugar do sync
        // e o modo edicao.
        if (m_EditorState == EditorState::Play)
        {
            AXE_EDITOR_WARN("BP sync ignorado: pare o Play para propagar mudancas do Blueprint.");
            return 0;
        }

        auto scriptAsset = ScriptAsset::LoadFromFile(scriptPath);

        if (!scriptAsset)
            return 0;

        auto& registry = m_Scene->GetRegistry();

        // ── Casamento de caminho no Windows ──────────────────────────────
        //
        // Comparar as strings direto NAO funciona: "C:/..." vs "c:\...",
        // separador trocado, ou o caminho relativo que veio da cena salva —
        // qualquer um desses faz a instancia nao ser reconhecida, e o autor
        // fica achando que o sync nao existe (foi o que aconteceu).
        //
        // equivalent() e a resposta certa quando os DOIS arquivos existem:
        // pergunta ao filesystem se e o mesmo arquivo, resolvendo relativo,
        // caixa e link de uma vez. Se algum nao existir, cai na comparacao
        // textual normalizada em caixa baixa.
        auto samePath = [](const std::filesystem::path& a,
            const std::filesystem::path& b)
            {
                std::error_code ec;

                if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec))
                {
                    const bool eq = std::filesystem::equivalent(a, b, ec);

                    if (!ec)
                        return eq;
                }

                auto norm = [](const std::filesystem::path& p)
                    {
                        std::error_code e2;
                        std::string s = std::filesystem::absolute(p, e2).lexically_normal().generic_string();

                        std::transform(s.begin(), s.end(), s.begin(),
                            [](unsigned char ch) { return (char)std::tolower(ch); });

                        return s;
                    };

                return norm(a) == norm(b);
            };

        int updated = 0;
        int seen = 0;
        std::string misses;

        registry.view<ScriptComponent>().each(
            [&](entt::entity entity, ScriptComponent& sc)
            {
                if (sc.ScriptAssetPath.empty())
                    return;

                ++seen;

                if (!samePath(sc.ScriptAssetPath, scriptPath))
                {
                    misses += "\n  - '" + sc.ScriptName + "' aponta para: " + sc.ScriptAssetPath;
                    return;
                }

                for (const auto& def : scriptAsset->GetComponents())
                {
                    if (def.Type == "CharacterController")
                    {
                        auto& cc = registry.get_or_emplace<CharacterControllerComponent>(entity);

                        cc.Height = def.CCHeight;
                        cc.Radius = def.CCRadius;
                        cc.MaxSlopeAngle = def.CCMaxSlope;
                        cc.StepHeight = def.CCStepHeight;
                        cc.MaxSpeed = def.CCMaxSpeed;
                        cc.JumpForce = def.CCJumpForce;
                        cc.OrientRotationToMovement = def.CCOrientToMovement;
                        cc.RotationRate = def.CCRotationRate;
                        cc.ShowDebug = def.CCShowDebug;
                        cc.CapsuleOffset = { def.CCOffsetX, def.CCOffsetY, def.CCOffsetZ };
                        // Velocity/CharacterID/IsCreated/contatos: intocados.
                    }
                    else if (def.Type == "SkeletalMesh")
                    {
                        auto& sk = registry.get_or_emplace<SkeletalMeshComponent>(entity);

                        sk.ShowSkeleton = def.ShowSkeleton;

                        if (sk.AssetUUID != def.AssetUUID)
                        {
                            sk.AssetUUID = def.AssetUUID;
                            sk.Asset.reset();
                            sk.Data.reset();
                            sk.Clips.clear();
                            sk.CurrentClip = -1;
                            sk._AppliedClip = -2;

                            if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(def.AssetUUID))
                            {
                                if (auto asset = SkeletalMeshAsset::LoadFromFile(rec->FilePath);
                                    asset && asset->Resolve())
                                {
                                    sk.Asset = asset;
                                    sk.Data = asset->GetMesh();
                                    sk.Clips = asset->GetClips();
                                    sk.CurrentClip = asset->GetClips().empty() ? -1 : 0;
                                }
                            }
                        }

                        if (sk.GraphAssetUUID != def.AnimGraphUUID)
                        {
                            sk.GraphAssetUUID = def.AnimGraphUUID;
                            sk.GraphAsset.reset();
                            sk.GraphInstance.Reset();

                            if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(def.AnimGraphUUID))
                            {
                                if (auto graph = AnimGraphAsset::LoadFromFile(rec->FilePath))
                                {
                                    if (sk.Asset)
                                        graph->Resolve(*sk.Asset);

                                    sk.GraphAsset = graph;
                                }
                            }
                        }
                    }
                    else if (def.Type == "Material" && !def.AssetUUID.empty())
                    {
                        auto& mc = registry.get_or_emplace<MaterialComponent>(entity);

                        if (mc.MaterialAssetUUID != def.AssetUUID)
                        {
                            if (const AssetRecord* r = AssetDatabase::Get().GetByUUID(def.AssetUUID))
                            {
                                if (auto ma = MaterialAsset::LoadFromFile(r->FilePath))
                                {
                                    mc.Data = ma->GetMaterial();
                                    mc.MaterialAssetUUID = def.AssetUUID;
                                }
                            }
                        }
                    }
                    else if (def.Type == "SpringArm")
                    {
                        if (auto* sa = registry.try_get<SpringArmComponent>(entity))
                        {
                            sa->Length = def.SALength / 100.0f;
                            sa->HeightOffset = def.SAHeightOffset;
                        }
                    }
                }

                // SO o nome. DllPath e IsCompiled sao resultado da COMPILACAO,
                // nao configuracao do BP: propagar aqui derrubava o script que
                // ja estava rodando — salvar sem compilar marcava a instancia
                // como "nao compilada", o script parava e o personagem ficava
                // ANIMANDO SEM SAIR DO LUGAR ("patinando"). Quem atualiza DLL
                // e o Compilar, no seu proprio fluxo.
                sc.ScriptName = scriptAsset->GetName();

                ++updated;
            });

        if (updated > 0)
        {
            AXE_EDITOR_INFO("[BP_SYNC_V2] BP '{}': {} instancia(s) na cena atualizada(s).",
                scriptAsset->GetName(), updated);
        }
        else if (seen > 0)
        {
            // Nenhuma casou, mas existem scripts na cena: o motivo quase
            // sempre e caminho divergente — mostra os dois lados em vez de
            // deixar o autor no escuro.
            AXE_EDITOR_WARN("[BP_SYNC_V2] BP '{}': nenhuma instancia casou.\n  Salvo em: {}{}",
                scriptAsset->GetName(), scriptPath.string(), misses);
        }

        return updated;
    }

    void EditorLayer::InstantiateScriptAsset(const std::filesystem::path& scriptPath,
        const std::string& assetUUID)
    {
        auto scriptAsset = ScriptAsset::LoadFromFile(scriptPath);
        if (!scriptAsset) return;

        auto& registry = m_Scene->GetRegistry();
        auto entity = m_Scene->CreateEntity(scriptAsset->GetName());

        // Transform RAIZ autorado no Script Editor (escala do personagem,
        // offset do pivo). Sem isto o Y Bot em cm nascia gigante na cena.
        if (auto* rtc = registry.try_get<TransformComponent>(entity))
        {
            rtc->Data.Position += glm::vec3(
                scriptAsset->RootPosX, scriptAsset->RootPosY, scriptAsset->RootPosZ);

            rtc->Data.Rotation = glm::radians(glm::vec3(
                scriptAsset->RootRotX, scriptAsset->RootRotY, scriptAsset->RootRotZ));

            rtc->Data.Scale = glm::vec3(
                scriptAsset->RootScaleX, scriptAsset->RootScaleY, scriptAsset->RootScaleZ);
        }

        for (const auto& def : scriptAsset->GetComponents())
        {
            if (def.Type == "Mesh")
            {
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = MeshFactory::CreateByUUID(
                    def.AssetUUID.empty() ? axe::PrimitiveUUID::Cube : def.AssetUUID);
                mc.AssetUUID = def.AssetUUID;
            }
            // ── SkeletalMesh: personagem animado do script ─────────────────
            //
            // Mesma receita do SpawnSkeletalMesh (Asset e a fonte de verdade,
            // Data/Clips derivam dele), mais o AnimGraph que o script escolheu.
            else if (def.Type == "SkeletalMesh" && !def.AssetUUID.empty())
            {
                const AssetRecord* srec = AssetDatabase::Get().GetByUUID(def.AssetUUID);

                if (srec)
                {
                    auto asset = SkeletalMeshAsset::LoadFromFile(srec->FilePath);

                    if (asset && asset->Resolve())
                    {
                        auto& sk = registry.emplace<SkeletalMeshComponent>(entity);
                        sk.Asset = asset;
                        sk.AssetUUID = def.AssetUUID;
                        sk.Data = asset->GetMesh();
                        sk.Clips = asset->GetClips();
                        sk.CurrentClip = asset->GetClips().empty() ? -1 : 0;
                        sk.ShowSkeleton = def.ShowSkeleton;

                        if (!def.AnimGraphUUID.empty())
                        {
                            if (const AssetRecord* grec = AssetDatabase::Get().GetByUUID(def.AnimGraphUUID))
                            {
                                if (auto graph = AnimGraphAsset::LoadFromFile(grec->FilePath))
                                {
                                    graph->Resolve(*asset);
                                    sk.GraphAsset = graph;
                                    sk.GraphAssetUUID = def.AnimGraphUUID;
                                }
                            }
                        }
                    }
                    else
                    {
                        AXE_EDITOR_ERROR("Script '{}': nao consegui resolver o Skeletal Mesh '{}'.",
                            scriptAsset->GetName(), srec->Name);
                    }
                }
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
                cc.OrientRotationToMovement = def.CCOrientToMovement;
                cc.RotationRate = def.CCRotationRate;
                cc.ShowDebug = def.CCShowDebug;
                cc.CapsuleOffset = { def.CCOffsetX, def.CCOffsetY, def.CCOffsetZ };
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
            else if (def.Type == "Material")
            {
                // Diagnostico: material que nao chega na cena e sempre um
                // destes tres — UUID vazio (nunca escolhido no BP), record
                // sumido do banco, ou o .axemat que nao carrega.
                if (def.AssetUUID.empty())
                {
                    AXE_EDITOR_WARN("Script '{}': componente Material sem asset escolhido — a instancia nasce sem material.",
                        scriptAsset->GetName());
                }
                else if (const AssetRecord* r = AssetDatabase::Get().GetByUUID(def.AssetUUID))
                {
                    if (auto ma = MaterialAsset::LoadFromFile(r->FilePath))
                    {
                        MaterialComponent mc{ ma->GetMaterial() };
                        mc.MaterialAssetUUID = def.AssetUUID;
                        registry.emplace<MaterialComponent>(entity, mc);
                    }
                    else
                    {
                        AXE_EDITOR_WARN("Script '{}': falha ao carregar o material '{}'.",
                            scriptAsset->GetName(), r->Name);
                    }
                }
                else
                {
                    AXE_EDITOR_WARN("Script '{}': material UUID '{}' nao esta no AssetDatabase.",
                        scriptAsset->GetName(), def.AssetUUID);
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