#include "axe_editor/asset/asset_viewer_window.hpp"   // ASSET_VIEWER_V1
#include <functional>   // FRAME_SELECTED_V1
#include "axe_editor/ui/view_gizmo.hpp"   // VIEW_GIZMO_V1
#include "editor_layer.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/anim_graph_asset.hpp"
#include "axe/animation/rig/control_rig_asset.hpp"
#include "axe/material/material_asset.hpp"
#include "axe_editor/asset/asset_spawn_defaults.hpp"   // ASSET_DEFAULTS_V1
#include "axe/material/material_shader_cache.hpp"   // BATCH_SHADING_MODEL_V1
#include "axe/particles/particle_system_asset.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/scene/components.hpp"
#include "editor/axe_editor/editor_app.hpp"
#include "axe/script/script_base.hpp"
#include "axe/script/script_paths.hpp"
#include "axe/physics/physics_system.hpp"
#include "axe/mesh/mesh_cooked.hpp"        // B2.1 — cook no import
#include "axe/asset/asset_import_hooks.hpp"   // B2.4
#include "editor/axe_editor/import/skeletal_mesh_loader.hpp"   // B2.4
#include "axe/audio/audio_engine.hpp"
#include "axe/audio/sound_cue.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>



namespace axe
{

    // ═══════════════════════════════════════════════════════════════════════
    //  EDITOR_CAM_PERSIST_V1 — a camera do editor viaja com a cena
    //
    //  Ele pedia: "guardar a ultima posicao da camera do editor para que nao
    //  precise, a todo load, ajustar novamente".
    //
    //  A camera do editor nao e entidade — vive no ViewportRenderer —, entao
    //  o serializer nao a alcanca. A ponte sao as duas caixas de correio
    //  estaticas do SceneSerializer, e estas duas funcoes sao os UNICOS
    //  lugares que as tocam: ha 3 chamadas de Serialize e 4 de Deserialize
    //  neste arquivo, e escrever o mesmo bloco 7 vezes e exatamente como o
    //  EnvironmentComponent ficou anos sem ser salvo.
    //
    //  Guarda a ORBITA (foco/distancia/pitch/yaw), nao a posicao: e o estado
    //  completo desta camera, e a posicao e derivada dele.
    // ═══════════════════════════════════════════════════════════════════════
    // ═══════════════════════════════════════════════════════════════════════
    //  FRAME_SELECTED_V1 — a tecla F, como na Unreal e no Blender
    //
    //  Enquadra a entidade selecionada: poe o FOCO da camera no centro dela e
    //  a distancia proporcional ao tamanho. Como a EditorCamera e orbital, isso
    //  tambem conserta o pivo — a partir dai orbitar gira em volta do objeto, e
    //  nao de um ponto qualquer que ficou para tras. Numa cena com muitos itens
    //  e a diferenca entre navegar e procurar.
    //
    //  A conta e a MESMA do FramePreviewCamera do Script Editor, de proposito:
    //  dois enquadramentos diferentes no mesmo editor parecem defeito. A altura
    //  manda porque personagem e alto e estreito; usar a maior das tres
    //  dimensoes afastaria demais num objeto largo e baixo.
    //
    //  Percorre os FILHOS tambem: um "BP_Player" costuma ser uma entidade vazia
    //  com a malha pendurada abaixo. Enquadrar so o pai daria uma caixa de
    //  tamanho zero e a camera iria para cima do nada.
    // ═══════════════════════════════════════════════════════════════════════
    void EditorLayer::FrameSelectedEntity()
    {
        if (!m_Scene || !m_Context.HasSelection()) return;
        if (!m_ViewportRenderer || !m_ViewportRenderer->m_Camera) return;

        auto& reg = m_Scene->GetRegistry();

        bool haveBounds = false;
        glm::vec3 worldMin(0.0f), worldMax(0.0f);

        // Acumula a caixa da entidade e de toda a subarvore dela.
        std::function<void(entt::entity)> accumulate =
            [&](entt::entity e)
            {
                if (!reg.valid(e)) return;

                glm::mat4 world(1.0f);
                if (auto* tc = reg.try_get<TransformComponent>(e))
                    world = tc->Data.GetMatrix();

                auto measure = [&](const auto& meshPtr)
                    {
                        if (!meshPtr) return;
                        const auto& verts = meshPtr->GetVertices();
                        if (verts.empty()) return;

                        for (const auto& v : verts)
                        {
                            const glm::vec3 p = glm::vec3(world * glm::vec4(v.Position, 1.0f));
                            if (!haveBounds) { worldMin = worldMax = p; haveBounds = true; }
                            else { worldMin = glm::min(worldMin, p); worldMax = glm::max(worldMax, p); }
                        }
                    };

                if (auto* sk = reg.try_get<SkeletalMeshComponent>(e)) measure(sk->Data);
                if (auto* mc = reg.try_get<MeshComponent>(e))         measure(mc->Data);

                if (auto* rel = reg.try_get<RelationshipComponent>(e))
                    for (auto child : rel->Children) accumulate(child);
            };

        accumulate(m_Context.SelectedEntity);

        // Sem malha nenhuma (luz, volume, entidade vazia): ainda vale enquadrar
        // a POSICAO dela, com uma distancia padrao. Nao fazer nada seria pior —
        // o usuario apertou F e nada aconteceu, sem saber por que.
        if (!haveBounds)
        {
            glm::vec3 p(0.0f);
            if (auto* tc = reg.try_get<TransformComponent>(m_Context.SelectedEntity))
                p = glm::vec3(tc->Data.GetMatrix()[3]);
            m_ViewportRenderer->m_Camera->SetView(p, 5.0f);
            return;
        }

        const glm::vec3 size = worldMax - worldMin;
        const float height = std::max(size.y, 0.0001f);
        const float width = std::max(size.x, size.z);
        const float extent = std::max(height, width);

        const glm::vec3 focal = (worldMin + worldMax) * 0.5f;

        // 1.9x e o fator ja calibrado nos previews com FOV 45.
        m_ViewportRenderer->m_Camera->SetView(focal, extent * 1.9f);
    }

    namespace
    {
        void CaptureEditorCamera(ViewportRenderer* vr)
        {
            SceneSerializer::PendingEditorCamera = {};
            if (!vr || !vr->m_Camera) return;

            const auto& c = *vr->m_Camera;
            SceneSerializer::PendingEditorCamera.FocalPoint = c.GetFocalPoint();
            SceneSerializer::PendingEditorCamera.Distance = c.GetDistance();
            SceneSerializer::PendingEditorCamera.Pitch = c.GetPitch();
            SceneSerializer::PendingEditorCamera.Yaw = c.GetYaw();
            SceneSerializer::PendingEditorCamera.Valid = true;
        }

        void RestoreEditorCamera(ViewportRenderer* vr)
        {
            const auto& st = SceneSerializer::LoadedEditorCamera;
            // Valid=false = cena salva antes desta versao. Nao mexer na camera
            // e melhor que joga-la para uma orbita zerada.
            if (!st.Valid || !vr || !vr->m_Camera) return;
            // ATENCAO A ORDEM: a assinatura existente e (foco, distancia,
            // YAW, PITCH) — inverter aqui deitaria a camera de lado sem
            // erro nenhum de compilacao, porque os dois sao float.
            vr->m_Camera->SetOrbit(st.FocalPoint, st.Distance, st.Yaw, st.Pitch);
        }
    }

    namespace
    {
        // ── B2.1: carrega e cozinha se preciso ───────────────────────────────
        //
        // Existe para que os TRES pontos de import do editor (drop de arquivo
        // externo, arrastar do Asset Browser para a hierarquia, e para o
        // viewport) cozinhem pelo mesmo caminho. Espalhar a chamada de Write
        // pelos tres garantiria que um deles ficasse para tras — e o sintoma
        // seria "esta malha carrega rapido e aquela nao", sem causa aparente.
        //
        // Reimporta e regrava quando o cozido esta velho, entao substituir o
        // FBX e usar a malha de novo ja atualiza o .axemesh.
        LoadedAsset LoadMeshCooking(const std::filesystem::path& path)
        {
            LoadedAsset asset = MeshLoader::Load(path.string());

            if (asset.MeshData && !MeshCooked::IsUpToDate(path))
                MeshCooked::Write(MeshCooked::PathFor(path), *asset.MeshData);

            return asset;
        }
    }

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_EditorUI(std::make_unique<EditorUI>())
    {}

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::OnAttach()
    {
        AXE_EDITOR_INFO("EditorLayer attached");

        // SR1 — Input::Init e AudioEngine::Init eram duas chamadas soltas
        // aqui. Viraram uma so, do lado do runtime.
        //
        // Nao e cosmetica: o KNOWN_LIMITATIONS registrava que a
        // inicializacao do AudioEngine estava acoplada ao EditorLayer e
        // precisava de um ponto standalone. Este e o ponto — o `main` do
        // jogo chamara a MESMA funcao, e nao precisara saber que existe um
        // AudioEngine para inicializar.
        SceneRuntime::InitializeServices(&EditorApp::Get().GetWindow());

        // ── B2.4: registra os importadores ────────────────────────────────
        //
        // Este e o ponto que liga o assimp ao runtime — e ele so existe no
        // EDITOR. O `game.exe` nao registra nada, e por isso consegue rodar
        // com um `axe.dll` que nao linka assimp.
        //
        // Cada hook importa E COZINHA: a partir do proximo load, o asset entra
        // pelo formato proprio e o importador nao e mais tocado. E o que faz o
        // projeto migrar sozinho, um asset por vez.
        AssetImportHooks::SetMeshImporter(
            [](const std::filesystem::path& p) -> std::shared_ptr<Mesh>
            {
                auto asset = MeshLoader::Load(p.string());

                if (asset.MeshData && !MeshCooked::IsUpToDate(p))
                    MeshCooked::Write(MeshCooked::PathFor(p), *asset.MeshData);

                return asset.MeshData;
            });

        AssetImportHooks::SetSkeletalImporter(
            [](const std::filesystem::path& p) -> AssetImportHooks::SkeletalResult
            {
                SkeletalAsset imported = SkeletalMeshLoader::Load(p.string());

                AssetImportHooks::SkeletalResult r;
                r.Mesh = imported.MeshData;
                r.Skeleton = imported.SkeletonData;
                r.Clips = imported.Clips;

                // O cozido NAO e gravado aqui: o .axeskelbin e derivado do
                // .axeskel, nao do FBX (um mesmo FBX pode originar varios), e
                // quem conhece esse caminho e o SkeletalMeshAsset::Resolve.
                return r;
            });

        AssetImportHooks::SetClipImporter(
            [](const std::filesystem::path& p, const Skeleton& target)
            {
                return SkeletalMeshLoader::LoadClips(p.string(), target);
            });

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
        m_EditorUI->m_SequencerWindow.SetContext(&m_Context);
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
                RestoreEditorCamera(m_ViewportRenderer.get());   // EDITOR_CAM_PERSIST_V1
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
                    CaptureEditorCamera(m_ViewportRenderer.get());   // EDITOR_CAM_PERSIST_V1
                    SceneSerializer::Serialize(*m_Scene, chosen.string(), &m_Environment);
                    m_CurrentScenePath = chosen.string();
                    AXE_EDITOR_INFO("Cena salva em: {}", m_CurrentScenePath);
                    return;
                }
                CaptureEditorCamera(m_ViewportRenderer.get());   // EDITOR_CAM_PERSIST_V1
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
        m_EditorUI->OnDrawSequencer = [this]()
            {
                m_SequencerWindow.IsOpen();

            };
        // ── Callback do AssetBrowser — drop de arquivo ────────────────────────
        m_EditorUI->GetAssetBrowser()->SetFileDropCallback(
            [this](const std::string& filepath)
            {
                std::string uuid = AssetDatabase::Get().Register(filepath);
                LoadedAsset asset = LoadMeshCooking(filepath);
                if (!asset.MeshData) { AXE_CORE_ERROR("EditorLayer: falha ao carregar '{}'", filepath); return; }

                std::string name = std::filesystem::path(filepath).stem().string();
                auto& registry = m_Scene->GetRegistry();
                entt::entity entity = m_Scene->CreateEntity(name);

                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = asset.MeshData;
                mc.AssetUUID = uuid;

                if (asset.MaterialData)
                    registry.emplace<MaterialComponent>(entity, asset.MaterialData);

                // ASSET_DEFAULTS_V1 — material padrao e collider do `.axemeta`.
                // Uma linha em cada ponto de spawn, com a regra vivendo num lugar so.
                AssetSpawnDefaults::Apply(registry, entity, uuid);

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
                // ── ASSET_VIEWER_V1 ─────────────────────────────────────
                //
                // Textura e malha nao tinham editor: duplo clique nelas nao
                // fazia nada. Entram AQUI, no mesmo despacho dos outros, e
                // nao num caminho novo — caminho de abertura duplicado e
                // como o Post Process Volume divergiu.
                //
                // O teste e Handles(), e nao um `if` de tipo escrito de novo:
                // a lista de tipos que a janela sabe mostrar vive DENTRO dela,
                // e quem adiciona um painel novo la nao precisa lembrar de
                // vir mexer aqui.
                if (AssetViewerWindow::Handles(record.Type))
                {
                    m_EditorUI->m_AssetViewerWindow.Open(
                        record.FilePath, record.Type, record.Name);
                    return;
                }

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
                else if (record.Type == AssetType::SoundCue)
                {
                    if (auto cue = SoundCueAsset::LoadFromFile(record.FilePath))
                        m_EditorUI->m_SoundCueEditorWindow.OpenAsset(cue, record.FilePath);
                    else
                        AXE_EDITOR_ERROR("Sound Cue '{}': arquivo invalido.", record.Name);
                }
                // ── SC34: .axeskel abre a janela do personagem ────────────────
                //
                // A janela ja existia e ja recebe um SkeletalMeshAsset
                // (AnimClipWindow::OpenForAsset) — inclusive com a aba
                // Skeleton, que e onde os sockets vao morar. O que faltava era
                // o duplo clique CHAMAR: ele caia no ramo generico, que
                // instancia na cena.
                //
                // Material, Particle e Sound Cue ja abriam editor no duplo
                // clique; o personagem era o unico asset editavel que ia
                // parar na cena em vez do editor dele.
                else if (record.FilePath.extension() == ".axeskel")
                {
                    if (auto skel = SkeletalMeshAsset::LoadFromFile(record.FilePath))
                    {
                        // Resolve antes de abrir: a janela precisa da malha e
                        // do esqueleto, e falhar AQUI da uma mensagem util em
                        // vez de uma janela vazia sem explicacao.
                        if (skel->Resolve())
                            m_EditorUI->m_AnimClipWindow.OpenForAsset(skel);
                        else
                            AXE_EDITOR_ERROR("'{}': nao foi possivel resolver o esqueleto "
                                "(o FBX de origem ainda existe?).", record.Name);
                    }
                    else
                    {
                        AXE_EDITOR_ERROR("'{}': .axeskel invalido.", record.Name);
                    }
                }
                // ── .axeseq abre o Sequencer NAQUELA sequence ─────────────────
                //
                // Ate aqui o Sequencer so sabia carregar um caminho fixo. Com o
                // tipo registrado, o duplo clique passa a ser o caminho normal
                // de abrir uma cutscene — e a janela guarda de qual arquivo ela
                // veio, que e o que faz o Ctrl+S regravar no lugar certo em vez
                // de perguntar de novo.
                else if (record.Type == AssetType::Sequence)
                {
                    m_EditorUI->m_SequencerWindow.OpenFile(record.FilePath);
                }
                // ── CLIPE ASSADO ──────────────────────────────────────────────
                //
                // O `TryOpenAnimationFile` ja resolve "de quem e este arquivo?"
                // varrendo os `.axeskel` do projeto com
                // FindAnimationEntryBySource — e a entrada de um clipe assado
                // aponta para o proprio `.axeclipbin`, entao ele e encontrado
                // pelo mesmo caminho que um FBX de animacao.
                else if (record.Type == AssetType::AnimationClip)
                {
                    if (!TryOpenAnimationFile(record))
                        AXE_EDITOR_WARN("'{}': nenhum .axeskel do projeto referencia "
                            "este clipe. Ele foi assado para outro personagem?",
                            record.Name);
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

                    // ASSET_DEFAULTS_V1 — as ~35 linhas que moravam aqui (ler
                    // o `.axemat`, disparar a recompilacao do shader, pescar a
                    // textura do primeiro Texture Sample no `.axegraph` irmao)
                    // foram para AssetSpawnDefaults::ResolveMaterial.
                    //
                    // Nao foi arrumacao: o padrao de material do asset precisa
                    // do MESMO carregamento, e duas copias dele significariam
                    // um material que carrega diferente conforme o caminho que
                    // o carregou — que e a forma exata do bug que estamos
                    // caçando na build empacotada. Uma verdade so.
                    auto material = AssetSpawnDefaults::ResolveMaterial(uuid);
                    if (!material) return;

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

                    // ASSET_DEFAULTS_V1 — material padrao e collider do `.axemeta`.
                    // Uma linha em cada ponto de spawn, com a regra vivendo num lugar so.
                    AssetSpawnDefaults::Apply(registry, entity, uuid);
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

                // Control Rig nao vira entidade — ABRE O EDITOR.
                //
                // Mesmo raciocinio do .axeanim: e asset de comportamento, nao
                // de cena. O esqueleto vem junto porque a janela precisa dele
                // pra sincronizar ossos novos e pro preview.
                if (record->FilePath.extension() == ".axerig")
                {
                    AXE_EDITOR_INFO("Control Rig: abrindo '{}'...", record->Name);

                    auto rigAsset = ControlRigAsset::LoadFromFile(record->FilePath);
                    if (!rigAsset)
                    {
                        AXE_EDITOR_ERROR("Control Rig: falha ao ler '{}'. Arquivo corrompido?",
                            record->FilePath.string());
                        return;
                    }

                    std::shared_ptr<SkeletalMeshAsset> skel;

                    if (const AssetRecord* skelRec =
                        AssetDatabase::Get().GetByUUID(rigAsset->GetSkeletonUUID()))
                    {
                        skel = SkeletalMeshAsset::LoadFromFile(skelRec->FilePath);
                        if (skel) skel->Resolve();
                    }
                    else
                    {
                        AXE_EDITOR_ERROR("Control Rig '{}': o esqueleto (.axeskel) referenciado nao "
                            "foi encontrado. A janela abre, mas sem preview.", record->Name);
                    }

                    m_EditorUI->m_ControlRigWindow.OpenForAsset(rigAsset, skel);
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

                // Audio e Sound Cue tambem nao viram mesh.
                //
                // Sem esta guarda, o caminho terminava em MeshLoader::Load
                // sobre um .axecue e o assimp cuspia "DXF: no data blocks
                // loaded" — erro sem relacao nenhuma com o problema real, que
                // e o pior tipo de mensagem de erro que existe.
                if (record->Type == AssetType::SoundCue ||
                    record->Type == AssetType::Audio)
                {
                    AXE_EDITOR_INFO("'{}' e um asset de audio: use-o no Audio Source, "
                        "no notify de som ou num no de script.", record->Name);
                    return;
                }

                LoadedAsset asset = LoadMeshCooking(record->FilePath);
                if (!asset.MeshData) return;
                auto entity = m_Scene->CreateEntity(record->Name);
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = asset.MeshData; mc.AssetUUID = uuid;
                if (asset.MaterialData) registry.emplace<MaterialComponent>(entity, asset.MaterialData);

                // ASSET_DEFAULTS_V1 — material padrao e collider do `.axemeta`.
                // Uma linha em cada ponto de spawn, com a regra vivendo num lugar so.
                AssetSpawnDefaults::Apply(registry, entity, uuid);
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

                    // ASSET_DEFAULTS_V1 — material padrao e collider do `.axemeta`.
                    // Uma linha em cada ponto de spawn, com a regra vivendo num lugar so.
                    AssetSpawnDefaults::Apply(registry, entity, uuid);
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

                // ── .axerig ARRASTADO PRA VIEWPORT ────────────────────────
                //
                // Este branch existia SO na lambda `instantiate` (duplo-clique).
                // No drop, o .axerig atravessava a cadeia inteira sem casar com
                // nada e caía no LoadMeshCooking la embaixo, onde o assimp
                // respondia "No suitable reader found" — e o mesh_loader ainda
                // acusava "alguma entidade da cena aponta pra ele como se fosse
                // mesh", que e um diagnostico falso: nao havia entidade nenhuma,
                // era o proprio drop entrando pelo caminho errado.
                //
                // A mesma armadilha ja tinha sido corrigida para o .axeskel e
                // para o .axeanim, e esquecida para o .axerig. Sao duas lambdas
                // gemeas que precisam concordar.
                //
                // E um Control Rig NAO VIRA ENTIDADE — nao tem geometria e nao
                // existe ControlRigComponent. Ele e aplicado a um personagem
                // dentro de um AnimGraph, pelo no `AnimNode_ControlRig`. Entao
                // "colocar em cena" um .axerig quer dizer ABRIR O EDITOR DELE,
                // exatamente como o duplo-clique ja fazia.
                if (record->FilePath.extension() == ".axerig")
                {
                    AXE_EDITOR_INFO("Control Rig: abrindo '{}'...", record->Name);

                    auto rigAsset = ControlRigAsset::LoadFromFile(record->FilePath);
                    if (!rigAsset)
                    {
                        AXE_EDITOR_ERROR("Control Rig: falha ao ler '{}'. Arquivo corrompido?",
                            record->FilePath.string());
                        return;
                    }

                    std::shared_ptr<SkeletalMeshAsset> skel;

                    if (const AssetRecord* skelRec =
                        AssetDatabase::Get().GetByUUID(rigAsset->GetSkeletonUUID()))
                    {
                        skel = SkeletalMeshAsset::LoadFromFile(skelRec->FilePath);
                        if (skel) skel->Resolve();
                    }
                    else
                    {
                        AXE_EDITOR_ERROR("Control Rig '{}': o esqueleto (.axeskel) referenciado "
                            "nao foi encontrado. A janela abre, mas sem preview.", record->Name);
                    }

                    m_EditorUI->m_ControlRigWindow.OpenForAsset(rigAsset, skel);
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

                LoadedAsset asset = LoadMeshCooking(record->FilePath);
                if (!asset.MeshData) return;
                auto entity = m_Scene->CreateEntity(record->Name);
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.Data = asset.MeshData; mc.AssetUUID = uuid;
                if (asset.MaterialData) registry.emplace<MaterialComponent>(entity, asset.MaterialData);

                // ASSET_DEFAULTS_V1 — material padrao e collider do `.axemeta`.
                // Uma linha em cada ponto de spawn, com a regra vivendo num lugar so.
                AssetSpawnDefaults::Apply(registry, entity, uuid);
                m_Context.Select(entity);
            });



        // ── Inicializa o viewport ─────────────────────────────────────────────
        ViewportWindow* viewport = m_EditorUI->GetViewport();
        if (viewport)
        {
            viewport->Initialize();
            viewport->SetGuizmoCallback([this](const glm::vec2& min, const glm::vec2& max)
                {
                    // Em Play o viewport e o jogo — nada de gizmo nem de formas
                    // de Control Rig por cima dele. Ver SuppressEditorGizmos.
                    m_ViewportRenderer->SuppressEditorGizmos =
                        (m_EditorState == EditorState::Play);

                    // VIEW_GIZMO_V1 — o widget de navegacao do canto. Desenhado
                    // aqui porque este callback ja roda logo apos o ImGui::Image
                    // e ja recebe os cantos REAIS da imagem (e nao os da
                    // janela, que diferem quando ha barra de ferramentas).
                    //
                    // Fora do Play: em Play o viewport e o jogo, e um widget de
                    // camera de editor por cima nao faz sentido.
                    if (m_EditorState != EditorState::Play &&
                        m_ViewportRenderer && m_ViewportRenderer->m_Camera)
                    {
                        ui::DrawViewGizmo(*m_ViewportRenderer->m_Camera,
                            ImVec2(min.x, min.y), ImVec2(max.x, max.y));
                    }

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

                    // .axerig e .axeanim nao viram entidade: nao ha fantasma
                    // para mostrar, e a ausencia de feedback le como "este asset
                    // nao pode ser arrastado". Dizer o que vai acontecer custa
                    // uma string e mata a duvida antes do drop.
                    const std::string ext = record->FilePath.extension().string();
                    if (ext == ".axerig")       info += "  (abre o Control Rig)";
                    else if (ext == ".axeanim") info += "  (abre o AnimGraph)";

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
                            auto& gameCam = m_Runtime.GetGameCamera();
                            gameCam.MouseCaptured = true;
                            gameCam.m_FirstMouse = true;
                            m_ViewportRenderer->SetGameCamera(&gameCam);
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

        // O contexto passa a saber onde fica o viewport. Quem precisa disso sao
        // as ferramentas que manipulam algo que nao e entidade — o Sequencer
        // move osso, socket e controle de rig, e nenhum dos tres tem
        // TransformComponent para o gizmo normal agarrar.
        m_Context.Viewport = m_ViewportRenderer.get();

        m_ThumbnailRenderer.Initialize();
        m_EditorUI->GetAssetBrowser()->SetThumbnailRenderer(&m_ThumbnailRenderer);

        // SC23 — mesma sequencia do de material: inicializa e entrega ao
        // browser. Sao dois renderers e nao um porque as cenas sao diferentes
        // (esfera fixa com material variavel x malha variavel com material
        // fixo) e a camera de um e estatica enquanto a do outro reenquadra a
        // cada asset.
        m_MeshThumbnails.Initialize();
        m_EditorUI->GetAssetBrowser()->SetMeshThumbnailRenderer(&m_MeshThumbnails);
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

                    // B4 — aproveita a compilacao que acabou de acontecer e
                    // deixa o `.axeshader` em dia. Efeito colateral valioso:
                    // ABRIR uma cena no editor ja cozinha todos os materiais
                    // dela — projetos antigos entram no formato novo sem passo
                    // manual, so abrindo o projeto uma vez antes de empacotar.
                    //
                    // PKG10 — SO se o grafo for de superficie. Este callback
                    // roda para todo MaterialComponent da cena, e o `result`
                    // acima e sempre do compilador de SUPERFICIE. Um
                    // MaterialComponent apontando para um `.axemat` de
                    // Particle/LightFunction — possivel, porque o AssetPicker
                    // filtra por TIPO de asset e nao por dominio — faria este
                    // ponto regravar o `.axeshader` daquele material como
                    // superficie A CADA LOAD DE CENA, e a luz/emitter que o usa
                    // cairia no shader padrao no jogo.
                    //
                    // Mesma guarda que ja existe nos outros tres pontos de
                    // cozimento (CompileAndApply e os dois FromFile).
                    if (graph.Domain == MaterialDomain::Surface)
                        MaterialCompiler::BakeToDisk(result, record->FilePath,
                            material->BakedEmissive);
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

        // POSTPROCESS_DOMAIN_V1 — sem este registro, o MaterialShaderCache nao
        // tinha callback para o dominio e caia no cozido. Funcionava, mas so
        // ate o grafo mudar: no editor a fonte da verdade e o GRAFO, e o cozido
        // e a foto da ultima compilacao.
        SceneSerializer::SetPostProcessMaterialRecompileCallback(
            [](const std::string& assetUUID,
                std::shared_ptr<Shader>& outShader,
                std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers) -> bool
            {
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
                if (!record) return false;
                return MaterialCompiler::CompilePostProcessFromFile(
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

        AXE_EDITOR_INFO("EditorLayer — NODECOMBO_V3 (popup ancorado no botao, com cara de combo)");
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

        // ── BATCH_SHADING_MODEL_V1 ───────────────────────────────────────────
        //
        // Troca o Shading Model de varios materiais e recompila cada um.
        // Implementado AQUI porque precisa do MaterialCompiler e do
        // MaterialShaderCache; o Asset Browser so junta os alvos e confirma.
        //
        // ── A ARMADILHA QUE DITOU O DESENHO ──────────────────────────────────
        //
        // O caminho obvio seria Deserialize -> mexer no campo -> Serialize.
        // Isso DESTRUIRIA o layout de todos os grafos: MaterialGraph::Serialize
        // le as posicoes de `m_NodePositions`, que so e preenchido pelo Draw da
        // janela do editor; o Deserialize escreve em `m_PendingPositions`. Fora
        // do editor aberto, `m_NodePositions` esta VAZIO — e todo node voltaria
        // para (0,0), em silencio, em cada material tocado.
        //
        // Entao a edicao e feita NO JSON, num campo so. O resto do arquivo
        // atravessa intacto, byte por byte. O MaterialGraph so e construido
        // depois, em memoria, para COMPILAR — e ali posicao nao importa.
        m_EditorUI->GetAssetBrowser()->SetBatchShadingModelCallback(
            [](const std::vector<std::string>& uuids, int shadingModel) -> int
            {
                int changed = 0;

                for (const auto& uuid : uuids)
                {
                    const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                    if (!record) continue;

                    auto graphPath = record->FilePath;
                    graphPath.replace_extension(".axegraph");
                    if (!std::filesystem::exists(graphPath)) continue;

                    try
                    {
                        nlohmann::json j;
                        {
                            std::ifstream in(graphPath);
                            if (!in.is_open()) continue;
                            j = nlohmann::json::parse(in);
                        }

                        // Shading Model so existe no dominio Surface. Um
                        // material de Particle ou Post Process com o campo
                        // gravado nao quebraria nada — mas contaria como
                        // "alterado" e mentiria no resultado.
                        const int domain = j.value("domain", 0);
                        if (domain != (int)MaterialDomain::Surface) continue;

                        if (j.value("shading_model", 0) == shadingModel) continue;

                        j["shading_model"] = shadingModel;

                        // Toon precisa das bandas. Se o material nunca foi Toon,
                        // o campo nao existe — e o default do grafo (3) so
                        // valeria ao reabrir no editor, nao nesta compilacao.
                        if (shadingModel == (int)MaterialShadingModel::Toon
                            && !j.contains("toon_steps"))
                            j["toon_steps"] = 3;

                        {
                            std::ofstream out(graphPath);
                            if (!out.is_open()) continue;
                            out << j.dump(4);
                        }

                        // Recompila e recozinha. Sem isto o `.axegraph` estaria
                        // certo e a CENA continuaria com o shader antigo ate
                        // alguem abrir cada material e clicar Compile — que e
                        // exatamente o trabalho que esta acao existe para evitar.
                        MaterialGraph graph;
                        graph.Deserialize(j);

                        auto result = MaterialCompiler::Compile(&graph);
                        if (result.Success)
                            MaterialCompiler::BakeToDisk(result, record->FilePath,
                                MaterialCompiler::ComputeBakedEmissive(&graph));

                        MaterialShaderCache::Invalidate(uuid);
                        ++changed;
                    }
                    catch (const std::exception& e)
                    {
                        AXE_EDITOR_WARN("Shading Model em lote: '{}' falhou: {}",
                            record->Name, e.what());
                    }
                }

                if (changed > 0)
                {
                    // O material aberto no Material Editor ficou velho em
                    // disco. Sem isto, salvar por la depois regravaria o
                    // shading model ANTIGO por cima do que o lote acabou de
                    // aplicar.
                    MaterialEditorWindow::MarkNeedsReload();

                    AXE_EDITOR_INFO("BATCH_SHADING_MODEL_V1: {} material(is) atualizado(s).",
                        changed);
                }

                return changed;
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
                // S0a — o fallback para OpenForEntity saiu. Ele existia para
                // entidades cujo grafo morava DENTRO do componente, fluxo
                // anterior ao .axescript. Como esse grafo nunca foi gravado
                // pelo SceneSerializer, uma entidade nesse estado ja abria
                // vazia — o fallback so escondia isso atras de um canvas em
                // branco.
                //
                // Agora a entidade sem asset recebe uma frase que diz o que
                // fazer, em vez de um editor que nao edita nada.
                if (sc->ScriptAssetPath.empty())
                {
                    AXE_EDITOR_WARN("Esta entidade tem um Script Component sem asset. "
                        "Escolha um .axescript no Inspector para abrir o editor.");
                    return;
                }

                if (auto asset = ScriptAsset::LoadFromFile(sc->ScriptAssetPath))
                {
                    m_EditorUI->m_ScriptGraphWindow.OpenForAsset(asset);
                    return;
                }

                AXE_EDITOR_WARN("Nao foi possivel abrir o script '{}': o arquivo "
                    "nao carregou.", sc->ScriptAssetPath);
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
        // ── SR1b: parada defensiva ────────────────────────────────────────
        //
        // Morrer em Play e caminho legitimo: fechar a janela nao passa pelo
        // botao Stop. Sem isto, o SceneRuntime seria destruido com a
        // simulacao ainda de pe.
        //
        // O que isso deixa para tras, em ordem de gravidade:
        //
        //   1. Os callbacks do Jolt continuam registrados no PhysicsSystem,
        //      que e SINGLETON — sobrevive a esta layer. Eles capturam
        //      `this` do SceneRuntime e a `Scene`, os dois prestes a virar
        //      memoria morta. Qualquer contato despachado depois disso e
        //      use-after-free.
        //   2. As DLLs de script ficam carregadas, com OnEnd nunca chamado.
        //   3. Voices vivas seguram PCM de clips no instante em que o
        //      ShutdownServices logo abaixo fecha o device.
        //
        // Hoje o (1) e inofensivo por acidente: o unico caminho que destroi
        // esta layer com o jogo rodando e o fechamento do programa, e ali
        // nada mais ticka. Trocar de projeto ja exige Stop (ver
        // OnOpenProject). Mas "inofensivo por acidente" e uma garantia que
        // depende de nenhum caminho novo aparecer — e o HUD vai adicionar
        // caminhos. Melhor nao depender.
        //
        // Chamamos OnStop direto, e nao EnterEdit: EnterEdit restauraria o
        // snapshot, mexeria na selecao e no CommandHistory, e escreveria em
        // m_EditorUI — que pode ja ter sido destruido dependendo da ordem.
        // Aqui nao se quer voltar ao estado de edicao; quer-se desligar.
        if (m_EditorState != EditorState::Edit && m_Scene)
        {
            m_Runtime.OnStop(*m_Scene);
            m_EditorState = EditorState::Edit;
        }

        m_EditorUI.reset();

        // SR1 — o AudioEngine::Shutdown mudou de lugar, nao de momento: a
        // razao continua sendo que uma voice viva ainda segura o PCM de um
        // clip, e o backend fecharia com dado em uso.
        //
        // Depende do OnStop acima ter rodado primeiro quando havia jogo de
        // pe: e ele quem mata as voices da cena.
        SceneRuntime::ShutdownServices();
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
                RestoreEditorCamera(m_ViewportRenderer.get());   // EDITOR_CAM_PERSIST_V1

                m_CurrentScenePath = ProjectManager::Get().GetStartScenePath().string();
            }
            else
            {
                std::string defaultScene = "resources/default_scene/main.axescene";
                // EDITOR_CAM_PERSIST_V1 — as chaves entraram junto: este `if`
                // nao tinha nenhuma, entao a segunda linha caia FORA dele e
                // quebrava o `else` logo abaixo.
                if (std::filesystem::exists(defaultScene))
                {
                    SceneSerializer::Deserialize(defaultScene, *m_Scene, &m_Environment);
                    RestoreEditorCamera(m_ViewportRenderer.get());
                }
                else
                {
                    m_Scene->CreateLight("Directional Light");
                    // PPVOLUME_ONE_PATH_V1 — era so o PostProcessComponent
                    // aqui, enquanto o menu da Hierarchy criava mais tres.
                    m_Scene->CreatePostProcessVolume();
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

        // Preview do Particle Editor tem sua própria ParticleWorld/cena —
        // tickado independente do estado de Play/Pause da cena principal.
        if (m_EditorUI && m_EditorUI->m_ParticleEditorWindow.IsOpen())
            m_EditorUI->m_ParticleEditorWindow.UpdatePreview(deltaTime);

        // ── SR1: a orquestracao do frame virou UMA chamada ────────────────
        //
        // Aqui havia ~110 linhas chamando ParticleWorld, AnimationWorld,
        // ScriptWorld, PhysicsWorld e AudioWorld na ordem certa, com as
        // justificativas dessa ordem em comentario. Tudo isso mudou para
        // SceneRuntime::OnUpdate — inclusive os comentarios, que valem tanto
        // para o editor quanto para o jogo.
        //
        // O que sobrou deste lado e o que so o editor sabe:
        //
        //   * qual TickMode o estado atual do editor significa;
        //   * onde esta a camera do viewport, que serve de ouvido em Edit e
        //     de referencia de LOD fora do Play;
        //   * o preview do Particle Editor, que tem cena e mundo proprios.
        if (m_Scene)
        {
            SceneRuntime::TickContext tick;
            tick.InputWindow = &EditorApp::Get().GetWindow();

            switch (m_EditorState)
            {
            case EditorState::Play:  tick.Mode = SceneRuntime::TickMode::Play;        break;
            case EditorState::Pause: tick.Mode = SceneRuntime::TickMode::Paused;      break;
            default:                 tick.Mode = SceneRuntime::TickMode::EditPreview; break;
            }

            // Camera do viewport: existe so no editor.
            //
            // LISTENER — fora do Play, o ouvido do usuario esta onde ele
            // esta olhando, nao onde a camera de jogo parou. Em Play nao
            // passamos override, e o runtime usa a GameCamera; e o mesmo
            // caminho que o jogo empacotado vai percorrer.
            //
            // LOD DE PARTICULA — mudanca de comportamento consciente, a
            // unica deste patch. Antes o LOD usava a camera do viewport
            // SEMPRE, inclusive em Play: com o viewport parado atras do
            // personagem, emissores perto do jogador eram avaliados como
            // distantes. Agora, em Play, o LOD usa a GameCamera. Fora do
            // Play o comportamento e identico ao anterior.
            SceneRuntime::ListenerPose viewportListener;
            glm::vec3 viewportCamPos(0.0f);
            const bool haveViewportCam =
                (m_ViewportRenderer && m_ViewportRenderer->m_Camera);

            if (haveViewportCam)
            {
                viewportCamPos = m_ViewportRenderer->m_Camera->GetPosition();
                viewportListener = SceneRuntime::PoseFromView(
                    viewportCamPos, m_ViewportRenderer->m_Camera->GetViewMatrix());
            }

            if (m_EditorState != EditorState::Play && haveViewportCam)
            {
                tick.ListenerOverride = &viewportListener;
                tick.LodCameraPosition = &viewportCamPos;
            }

            m_Runtime.OnUpdate(*m_Scene, deltaTime, tick);
        }

        // Esc sai do Play. Fica aqui, e nao no runtime: "Esc volta pro
        // editor" e comportamento de editor. Num jogo, Esc e o que o jogo
        // decidir que ele e.
        if (m_EditorState == EditorState::Play)
        {
            ImGui::GetIO().WantCaptureKeyboard = false;
            ImGui::SetNextFrameWantCaptureKeyboard(false);

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
        m_MeshThumbnails.RenderPending();

        if (m_EditorState == EditorState::Play)
            m_ViewportRenderer->SetGameCamera(&m_Runtime.GetGameCamera());
        else
            m_ViewportRenderer->SetGameCamera(nullptr);

        if (m_EditorUI)
        {
            ViewportWindow* viewport = m_EditorUI->GetViewport();
            if (viewport && viewport->IsInitialized())
            {
                // VIEWPORT_RESIZE_V1 — o tamanho que o ImGui pediu no frame
                // ANTERIOR entra em vigor AQUI, antes do render. Aplicar isso
                // dentro do Draw() era o que fazia o ImGui exibir uma textura
                // recem-alocada onde ninguem tinha desenhado ainda — o
                // "chuvisco" ao arrastar a borda do viewport.
                viewport->ApplyPendingResize();

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

            // ASSET_VIEWER_V2b — o preview 3D do Asset Viewer entra na MESMA
            // lista. Renderizar aqui, antes do ImGui, e o que garante que o
            // framebuffer esteja pronto quando o Draw for apresenta-lo.
            if (m_EditorUI->m_AssetViewerWindow.IsOpen())
                m_EditorUI->m_AssetViewerWindow.RenderPreview();

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
            m_EditorUI->m_ControlRigWindow.Draw();
            m_EditorUI->m_AnimClipWindow.Draw();
            // Em Play e Pause quem dirige a cena e o SceneRuntime (o
            // SequenceWorld dentro dele). Ver SequencerWindow::SetScenePlaying.
            m_EditorUI->m_SequencerWindow.SetScenePlaying(
                m_EditorState != EditorState::Edit);

            m_EditorUI->m_SequencerWindow.Draw();

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
            else if (isPause) { m_Runtime.GetGameCamera().MouseCaptured = true; m_EditorState = EditorState::Play; }
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

        // ── QUEM RESPONDE AO ATALHO ──────────────────────────────────────────
        //
        // Ctrl+Z, Ctrl+Y e Ctrl+S eram tratados aqui SEM guarda de foco. Isso
        // funcionou enquanto o unico historico era o da cena; com o Sequencer
        // tendo o seu, um Ctrl+Z com aquela janela em foco disparava os DOIS —
        // desfazia a curva e, junto, um movimento de entidade no viewport que
        // ninguem tinha pedido.
        //
        // O Material Editor ja tinha esta precedencia para o Ctrl+S; isto so
        // estende a mesma regra, e agora tambem para o historico.
        const bool sequencerHasFocus =
            m_EditorUI->m_SequencerWindow.IsOpen() &&
            m_EditorUI->m_SequencerWindow.IsFocused();

        if (!sequencerHasFocus)
        {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
                m_CommandHistory.Undo();
            if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) ||
                (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))))
                m_CommandHistory.Redo();
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !sequencerHasFocus)
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

        // ── PILOTANDO: A VISTA NAO E DO USUARIO ──────────────────────────────
        //
        // Reposiciona a EditorCamera a partir da entidade-camera, TODO FRAME.
        // Com o Sequencer animando o transform dela, arrastar o playhead vira
        // assistir a cutscene.
        //
        // E devolve `true` para a navegacao ser pulada: orbitar enquanto outra
        // coisa escreve na mesma camera daria um cabo de guerra em que o
        // arrasto do usuario e desfeito no frame seguinte — o editor pareceria
        // travado sem dizer por que. A faixa no viewport explica.
        if (m_ViewportRenderer->UpdatePilotCamera())
            return;

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

        // `OverlayConsumedClick`: um desenho de ferramenta externa (as formas
        // dos Controls do rig, hoje) pegou este clique. Sem esta guarda,
        // clicar num controle tambem trocaria a entidade selecionada — e o
        // personagem sairia da selecao no instante em que o animador tentasse
        // pegar um controle DELE.
        // VIEW_GIZMO_V1 — o gizmo de navegacao entra na MESMA guarda do
        // ImGuizmo e das formas do rig, e pelo mesmo motivo: sem ela, clicar
        // num eixo do gizmo tambem trocaria a selecao para o objeto que
        // estiver atras dele.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt &&
            !ImGuizmo::IsOver() && !ui::ViewGizmoCapturesMouse() &&
            !m_ViewportRenderer->OverlayConsumedClick())
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

        // FRAME_SELECTED_V1 — F enquadra a selecao.
        //
        // Antes do return do Alt de proposito: F e navegacao SEM Alt, e ficaria
        // inalcancavel se viesse depois. E o WantTextInput impede que digitar
        // "f" num campo de nome jogue a camera para o outro lado da cena.
        if (!io.WantTextInput && !io.KeyCtrl && !io.KeyAlt &&
            ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            FrameSelectedEntity();
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

        // `.axeclipbin` entra junto: um clipe ASSADO pelo Sequencer e registrado
        // no `.axeskel` exatamente como um FBX de animacao (a AnimEntry aponta
        // para ele), entao a mesma pergunta — "de quem e este arquivo?" — tem a
        // mesma resposta pelo mesmo caminho.
        const bool isAnimationFile =
            (ext == ".fbx" || ext == ".dae" || ext == ".gltf" || ext == ".glb" ||
                ext == ".axeclipbin");

        if (!isAnimationFile)
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
        CaptureEditorCamera(m_ViewportRenderer.get());   // EDITOR_CAM_PERSIST_V1
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
        RestoreEditorCamera(m_ViewportRenderer.get());   // EDITOR_CAM_PERSIST_V1
    }

    // ─────────────────────────────────────────────────────────────────────────
    void EditorLayer::EnterPlay()
    {
        if (m_EditorState != EditorState::Edit) return;

        // Clone de memoria. Nada passa por JSON, nada e perdido — nem o
        // ProbeGrid, nem a pose de um personagem, nem o runtime de um emissor.
        m_SceneSnapshot.Capture(*m_Scene);

        // ── SR1: start do jogo, uma chamada ───────────────────────────────
        //
        // Daqui saiu ~110 linhas: fiacao dos callbacks do Jolt, OnSceneStart
        // dos mundos, resolucao do GameMode -> DefaultPawn e configuracao da
        // GameCamera a partir de SpringArm/CameraComponent.
        //
        // Nada disso era editor. Todas as quatro coisas o jogo empacotado
        // precisa fazer identicamente — por isso foram para o SceneRuntime.
        //
        // O ProjectManager NAO atravessou junto: o runtime nao sabe o que e
        // um `.axeproject`. Quem le o GameMode ativo e o editor, e injeta.
        // Amanha quem injeta e o manifesto de empacotamento, e o
        // SceneRuntime nao muda uma linha.
        SceneRuntime::StartConfig startCfg;
        startCfg.InputWindow = &EditorApp::Get().GetWindow();
        if (ProjectManager::Get().HasProject())
            startCfg.GameModeUUID = ProjectManager::Get().GetCurrent().ActiveGameModeUUID;

        // O Capture ja aconteceu logo acima — e precisa mesmo ter acontecido
        // antes, porque o OnStart dispara as fontes com PlayOnStart e o
        // snapshot tem que guardar a cena sem nenhuma voice viva.
        const SceneRuntime::StartResult started = m_Runtime.OnStart(*m_Scene, startCfg);

        auto& gameCam = m_Runtime.GetGameCamera();

        // Fallback de posicionamento da camera: cena sem NENHUM
        // CameraComponent comeca o Play de onde o usuario estava olhando.
        //
        // Fica do lado do editor de proposito — depende da camera do
        // viewport, que so existe aqui. Um jogo com cena sem camera usa o
        // default da GameCamera, e isso e problema do nivel, nao do runtime.
        auto& editorCam = m_ViewportRenderer->m_Camera;
        if (!started.HasSceneCamera && editorCam)
        {
            gameCam.Reset(editorCam->GetPosition(),
                glm::degrees(editorCam->GetYaw()),
                glm::degrees(editorCam->GetPitch()));
        }

        gameCam.MouseCaptured = true;
        gameCam.m_FirstMouse = true;
        m_ViewportRenderer->SetGameCamera(&gameCam);
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
        m_Runtime.GetGameCamera().MouseCaptured = false;
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

        // ── SR1: stop do jogo, uma chamada ────────────────────────────────
        //
        // A ordem (script -> particulas -> audio -> StopAll -> camera ->
        // fisica -> mensagens de tela) e as razoes dela foram inteiras para
        // SceneRuntime::OnStop. O runtime ainda ganhou algo que nao existia
        // aqui: a desconexao dos callbacks do Jolt.
        m_Runtime.OnStop(*m_Scene);

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

        m_Runtime.GetGameCamera().MouseCaptured = false;
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
    // ═══════════════════════════════════════════════════════════════════
    //  SC46 — RECONCILIA OS ANEXOS DE UMA INSTANCIA JA NA CENA
    //
    //  ── O BURACO QUE ISTO FECHA ────────────────────────────────────────
    //
    //  O SC44 ensinou o InstantiateScriptAsset a criar entidades-filhas para
    //  componentes com Parent Socket. O SyncScriptInstances — que roda quando
    //  o Blueprint e salvo — nao sabia que elas existiam.
    //
    //  Resultado: trocar o socket, trocar a malha da arma ou remover o anexo
    //  nao mudava NADA no personagem que ja estava na cena. A unica forma de
    //  ver o efeito era apagar e reinstanciar — que e o que a gente acabava
    //  fazendo por instinto, sem perceber que era um sintoma.
    //
    //  ── RECONCILIAR, E NAO RECRIAR ─────────────────────────────────────
    //
    //  Seria mais curto destruir todos os filhos e criar de novo. Seria
    //  errado: a entidade-filha e um objeto de cena como qualquer outro — ela
    //  pode ter sido selecionada, renomeada, ou ter ganhado componentes a
    //  mao. Recriar joga isso fora a cada Ctrl+S do Blueprint.
    //
    //  Entao o casamento e por NOME DE SOCKET, que e a identidade estavel do
    //  anexo: um socket e um lugar no corpo, e a arma que esta na mao
    //  continua sendo a arma que esta na mao mesmo que a malha mude.
    //
    //  ── O QUE NAO SE TOCA ──────────────────────────────────────────────
    //
    //  O Transform do filho SO e escrito quando a entidade acabou de nascer.
    //  Mesma regra que o sync ja aplica a entidade raiz e pela mesma razao:
    //  offset e colocacao por instancia. Se o autor ajustou 2cm na arma de um
    //  inimigo especifico, o save do Blueprint nao pode desfazer isso.
    // ═══════════════════════════════════════════════════════════════════
    void EditorLayer::SyncSocketAttachments(entt::entity root, ScriptAsset& scriptAsset)
    {
        auto& registry = m_Scene->GetRegistry();
        const auto& comps = scriptAsset.GetComponents();

        // ── 1) O que o Blueprint pede agora ─────────────────────────────
        std::unordered_map<std::string, const ScriptComponentDef*> wanted;

        for (const auto& def : comps)
        {
            if (def.ParentSocket.empty())
                continue;

            // Mesma reconferencia da instanciacao: Parent Socket sem pai
            // esqueleto nao vira anexo. Ver InstantiateScriptAsset.
            if (def.ParentIndex < 0 || def.ParentIndex >= (int)comps.size())
                continue;

            if (comps[def.ParentIndex].Type != "SkeletalMesh")
                continue;

            if (def.Type != "Mesh" && def.Type != "Camera")
                continue;

            wanted[def.ParentSocket] = &def;
        }

        // ── 2) O que a cena tem hoje ────────────────────────────────────
        std::unordered_map<std::string, entt::entity> existing;
        std::vector<entt::entity> orphans;

        auto view = registry.view<SocketAttachmentComponent>();

        for (auto e : view)
        {
            const auto& att = view.get<SocketAttachmentComponent>(e);

            if (att.Target != root)
                continue;   // anexo de OUTRO personagem

            if (wanted.count(att.SocketName) != 0)
                existing[att.SocketName] = e;
            else
                orphans.push_back(e);   // socket saiu do Blueprint
        }

        // ── 3) Some o que nao e mais pedido ─────────────────────────────
        for (auto e : orphans)
        {
            AXE_EDITOR_INFO("BP sync: anexo removido (socket nao esta mais no Blueprint).");
            m_Scene->DestroyEntity(e);
        }

        // ── 4) Cria o que falta, atualiza o que ja esta la ──────────────
        for (const auto& [socketName, def] : wanted)
        {
            entt::entity child = entt::null;
            bool isNew = false;

            const auto it = existing.find(socketName);

            if (it != existing.end())
            {
                child = it->second;
            }
            else
            {
                child = m_Scene->CreateEntity(scriptAsset.GetName() + "_" + socketName);
                isNew = true;

                auto& att = registry.emplace<SocketAttachmentComponent>(child);
                att.Target = root;
                att.SocketName = socketName;

                m_Scene->SetParent(child, root, false);
            }

            if (def->Type == "Mesh")
            {
                auto& mc = registry.get_or_emplace<MeshComponent>(child);

                // So recarrega quando a malha MUDOU: ResolveByUUID reabre o
                // arquivo, e o sync roda a cada save do Blueprint.
                if (mc.AssetUUID != def->AssetUUID || !mc.Data)
                {
                    mc.AssetUUID = def->AssetUUID;
                    mc.Data = MeshFactory::ResolveByUUID(def->AssetUUID);

                    if (!mc.Data)
                        AXE_EDITOR_WARN("BP sync: malha do anexo '{}' nao resolveu.",
                            socketName);
                }
            }
            else if (def->Type == "Camera")
            {
                auto& cam = registry.get_or_emplace<CameraComponent>(child);
                cam.Fov = def->CamFov;
                cam.NearClip = def->CamNearClip;
                cam.FarClip = def->CamFarClip;
                cam.Sensitivity = def->CamSensitivity;
                cam.IsPrimary = def->CamIsPrimary;
            }

            // Ver a nota do cabecalho: transform so no nascimento.
            if (isNew)
            {
                if (auto* tc = registry.try_get<TransformComponent>(child))
                {
                    tc->Data.Position = glm::vec3(def->PosX, def->PosY, def->PosZ);
                    tc->Data.Rotation = glm::radians(glm::vec3(def->RotX, def->RotY, def->RotZ));
                    tc->Data.Scale = glm::vec3(def->ScaleX, def->ScaleY, def->ScaleZ);
                }

                AXE_EDITOR_INFO("BP sync: anexo criado no socket '{}'.", socketName);
            }
        }
    }

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

                SyncSocketAttachments(entity, *scriptAsset);   // SC46

                for (const auto& def : scriptAsset->GetComponents())
                {
                    // SC46 — componente anexado nao mora na entidade raiz.
                    // Sem este skip, o Material de uma arma anexada seria
                    // aplicado no PERSONAGEM no proximo save do Blueprint.
                    if (!def.ParentSocket.empty())
                        continue;

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
                            // ── BP_MATERIAL_V1 — faltava a TEXTURA ────────
                            //
                            // Este bloco fazia dois dos TRES passos: lia o
                            // `.axemat` e recompilava o shader. Faltava pescar
                            // a textura do primeiro Texture Sample conectado no
                            // `.axegraph` irmao — o `.axemat` sozinho nao a
                            // carrega.
                            //
                            // Por isso a arma no BP nascia cinza com o material
                            // "aplicado": ela tinha o shader certo e nenhum
                            // albedo. Aplicar o mesmo material direto no asset
                            // funcionava porque AQUELE caminho fazia os tres.
                            //
                            // Agora chama a funcao unica. Era o que o comentario
                            // logo abaixo, no outro caminho, ja avisava: "seria
                            // uma terceira copia da mesma rotina".
                            if (auto mat = AssetSpawnDefaults::ResolveMaterial(def.AssetUUID))
                            {
                                mc.Data = mat;
                                mc.MaterialAssetUUID = def.AssetUUID;
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

        // ═══════════════════════════════════════════════════════════════════
        //  SC44 — quem vai virar ENTIDADE PROPRIA
        //
        //  Historicamente TODO componente do script colapsa na entidade raiz:
        //  Mesh, Rigidbody, Collider, Camera, tudo junto. Isso funciona
        //  enquanto os componentes compartilham o mesmo transform.
        //
        //  Um anexo a socket nao compartilha: a arma precisa do transform da
        //  MAO, e a entidade raiz ja e a do personagem. Duas posicoes, uma
        //  entidade — nao cabe.
        //
        //  Entao a excecao e ESTREITA de proposito: so componentes com
        //  ParentSocket preenchido saem para entidades proprias. Todo script
        //  ja existente continua nascendo exatamente como antes, sem migracao
        //  de asset e sem mudanca de comportamento.
        // ═══════════════════════════════════════════════════════════════════
        const auto& comps = scriptAsset->GetComponents();
        std::vector<bool> isAttached(comps.size(), false);

        for (std::size_t ci = 0; ci < comps.size(); ++ci)
        {
            const auto& d = comps[ci];

            if (d.ParentSocket.empty())
                continue;

            // Reconfere a hierarquia em vez de confiar no arquivo: um
            // .axescript editado a mao pode ter ParentSocket num componente
            // sem pai, ou com pai que nao e esqueleto. Ignorar em silencio
            // seria pior — o componente sumiria da cena; aqui ele so volta a
            // colapsar na raiz, como antes de existir socket.
            if (d.ParentIndex < 0 || d.ParentIndex >= (int)comps.size())
            {
                AXE_EDITOR_WARN("Script '{}': componente '{}' tem Parent Socket "
                    "mas nao tem pai — anexo ignorado.",
                    scriptAsset->GetName(), d.Type);
                continue;
            }

            if (comps[d.ParentIndex].Type != "SkeletalMesh")
            {
                AXE_EDITOR_WARN("Script '{}': componente '{}' tem Parent Socket "
                    "mas o pai e '{}', nao um Skeletal Mesh — anexo ignorado.",
                    scriptAsset->GetName(), d.Type, comps[d.ParentIndex].Type);
                continue;
            }

            isAttached[ci] = true;
        }

        std::size_t _defIndex = (std::size_t)-1;

        for (const auto& def : scriptAsset->GetComponents())
        {
            ++_defIndex;

            // Anexado: nao entra na raiz. Sai para entidade propria no passo
            // de baixo, DEPOIS que o SkeletalMeshComponent do personagem ja
            // existir — senao o alvo do anexo estaria vazio no mesmo frame.
            if (_defIndex < isAttached.size() && isAttached[_defIndex])
                continue;

            if (def.Type == "Mesh")
            {
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.AssetUUID = def.AssetUUID;

                // SC25 — era CreateByUUID, que so conhece primitiva. Uma malha
                // importada vinculada no Script Editor nascia SEM geometria ao
                // ser posta na cena: o script funcionava, a entidade existia, e
                // nao havia nada na tela. ResolveByUUID cobre os dois casos.
                mc.Data = MeshFactory::ResolveByUUID(def.AssetUUID);
                if (!mc.Data)
                    mc.Data = MeshFactory::CreateByUUID(axe::PrimitiveUUID::Cube);

                // ASSET_DEFAULTS_V1 — material padrao e collider do `.axemeta`.
                // Uma linha em cada ponto de spawn, com a regra vivendo num lugar so.
                AssetSpawnDefaults::Apply(registry, entity, def.AssetUUID);
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
                    // ── BP_MATERIAL_V1 ──────────────────────────────────
                    //
                    // Os TRES passos, num lugar so: ler o `.axemat`, compilar
                    // o shader do `.axegraph`, e trazer a textura do primeiro
                    // Texture Sample conectado. Faltava o terceiro aqui, e o
                    // sintoma era uma malha com material e sem cor.
                    //
                    // O comentario que morava neste bloco dizia que
                    // recompilar a mao "seria uma terceira copia da mesma
                    // rotina — ja ha duas". Agora ha uma.
                    if (auto mat = AssetSpawnDefaults::ResolveMaterial(def.AssetUUID))
                    {
                        MaterialComponent mc{ mat };
                        mc.MaterialAssetUUID = def.AssetUUID;
                        registry.emplace<MaterialComponent>(entity, mc);
                    }
                    else
                    {
                        // Diagnostico preservado: o ResolveMaterial devolve
                        // nulo pelo mesmo motivo de antes (o `.axemat` nao
                        // carrega), e a mensagem continua nomeando o asset.
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

        // ═══════════════════════════════════════════════════════════════════
        //  SC44 — os anexos, agora que o personagem existe
        //
        //  O Target e sempre a entidade RAIZ, e nao "a entidade do componente
        //  pai": como todo componente colapsa na raiz, e nela que o
        //  SkeletalMeshComponent mora. No dia em que um script puder ter dois
        //  esqueletos, esta linha e a que muda — e o resto continua valendo.
        //
        //  Filho tambem no Relationship, e nao so no anexo: e o que faz a arma
        //  aparecer aninhada na Hierarchy e morrer junto com o personagem. O
        //  transform vem do socket (Scene::GetWorldTransform prioriza o
        //  anexo), entao os dois nao brigam.
        // ═══════════════════════════════════════════════════════════════════
        for (std::size_t ci = 0; ci < comps.size(); ++ci)
        {
            if (!isAttached[ci])
                continue;

            const auto& def = comps[ci];

            const std::string childName =
                scriptAsset->GetName() + "_" + def.ParentSocket;

            auto child = m_Scene->CreateEntity(childName);

            // Transform LOCAL relativo ao socket. E o mesmo campo que o painel
            // do Script Editor edita: um ajuste fino por cima do socket, sem
            // ter de reabrir o Animation Editor para corrigir 2cm.
            if (auto* ctc = registry.try_get<TransformComponent>(child))
            {
                ctc->Data.Position = glm::vec3(def.PosX, def.PosY, def.PosZ);
                ctc->Data.Rotation = glm::radians(glm::vec3(def.RotX, def.RotY, def.RotZ));
                ctc->Data.Scale = glm::vec3(def.ScaleX, def.ScaleY, def.ScaleZ);
            }

            if (def.Type == "Mesh")
            {
                auto& mc = registry.emplace<MeshComponent>(child);
                mc.AssetUUID = def.AssetUUID;
                mc.Data = MeshFactory::ResolveByUUID(def.AssetUUID);

                // ASSET_DEFAULTS_V1 — material padrao e collider do `.axemeta`.
                // Uma linha em cada ponto de spawn, com a regra vivendo num lugar so.
                AssetSpawnDefaults::Apply(registry, child, def.AssetUUID);

                if (!mc.Data)
                    AXE_EDITOR_WARN("Script '{}': malha do anexo '{}' nao resolveu.",
                        scriptAsset->GetName(), def.ParentSocket);
            }
            else if (def.Type == "Camera")
            {
                auto& cam = registry.emplace<CameraComponent>(child);
                cam.Fov = def.CamFov;
                cam.NearClip = def.CamNearClip;
                cam.FarClip = def.CamFarClip;
                cam.Sensitivity = def.CamSensitivity;
                cam.IsPrimary = def.CamIsPrimary;
            }
            // Outros tipos entram aqui conforme fizerem sentido anexados.
            // Rigidbody num socket, por exemplo, precisa de uma decisao que
            // ainda nao foi tomada: o corpo obedece a fisica ou ao osso? As
            // duas respostas sao defensaveis e nenhuma e obvia, entao o tipo
            // fica de fora ate a pergunta ser respondida.

            auto& att = registry.emplace<SocketAttachmentComponent>(child);
            att.Target = entity;
            att.SocketName = def.ParentSocket;

            m_Scene->SetParent(child, entity, false);   // transform ja e local

            AXE_EDITOR_INFO("Script '{}': '{}' anexado ao socket '{}'.",
                scriptAsset->GetName(), def.Type, def.ParentSocket);
        }

        ScriptComponent sc;
        sc.ScriptAssetPath = scriptPath.string();
        sc.ScriptName = scriptAsset->GetName();
        // SC4 — resolvido do disco, nao copiado do campo do asset: o DllPath
        // gravado no .axescript pode ser de outra maquina ou apontar para o
        // temp_scripts antigo. ScriptWorld reconfirma no Play de qualquer
        // forma; isto aqui so deixa o IsCompiled coerente ja na instanciacao.
        {
            std::string uuid;
            if (auto* rec = AssetDatabase::Get().GetByPath(scriptPath))
                uuid = rec->UUID;
            auto dll = ScriptPaths::ResolveDll(sc.ScriptName, uuid);
            sc.DllPath = dll.string();
            sc.IsCompiled = !dll.empty();
        }
        registry.emplace<ScriptComponent>(entity, sc);

        m_Context.Select(entity);
        AXE_EDITOR_INFO("InstantiateScriptAsset: '{}' instanciado.", scriptAsset->GetName());
    }



} // namespace axe