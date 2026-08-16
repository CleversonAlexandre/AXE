// script_preview.cpp
// Preview 3D do Script Editor: inicialização da cena de preview, sincronização
// de componentes, renderização, gizmo sobreposto e input orbital.

#include "script_graph_window.hpp"
#include "editor/axe_editor/import/mesh_loader.hpp"
#include "editor/axe_editor/script/script_asset.hpp"
#include "axe/scene/components.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/material/material_asset.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/asset/asset_database.hpp"
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <fstream>
#include <filesystem>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/anim_graph_asset.hpp"
#include "axe/asset/asset_database.hpp"

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::InitPreviewScene()
    {
        FramebufferSpecification spec;
        spec.Width = 512; spec.Height = 512;
        spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
        m_PreviewFramebuffer = Framebuffer::Create(spec);

        m_PreviewRenderer = std::make_unique<ViewportRenderer>();
        m_PreviewRenderer->Initialize();
        m_PreviewRenderer->SetPickingEnabled(false);
        m_PreviewRenderer->SetPreviewMode(true);
        if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
        {
            sr->SetDeferredEnabled(false);
            sr->SetDeferredSupported(false);
        }

        m_PreviewRenderer->m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 1000.0f);
        m_PreviewRenderer->ShowGrid = true;
        m_PreviewRenderer->ShowColliders = true;

        m_PreviewScene = std::make_unique<Scene>();
        m_PreviewEntity = m_PreviewScene->CreateEntity("ScriptPreview");
        SeedPreviewMesh();

        auto& reg = m_PreviewScene->GetRegistry();
        auto light = m_PreviewScene->CreateEntity("PreviewLight");
        auto& lc = reg.emplace<LightComponent>(light);
        lc.Data = std::make_shared<DirectionalLight>();
        lc.Data->Direction = glm::vec3(0.0f, -1.0f, -1.0f);
        lc.Data->Color = glm::vec3(1.0f);
        lc.Data->Intensity = 3.0f;
        lc.Data->AmbientStrength = 0.3f;
        lc.Data->IBLIntensity = 0.15f;

        m_PreviewRenderer->SetScene(m_PreviewScene.get());
        m_PreviewEnvironment = std::make_unique<SceneEnvironment>();
        m_PreviewEnvironment->LoadHDRI("resources/quarry_04_puresky_2k.hdr");
        m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());
        if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
            sr->SetEnvironment(m_PreviewEnvironment.get());
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────
    //  S0a — SyncMeshFromSource virou SeedPreviewMesh.
    //
    //  A versao antiga copiava a malha e o material da ENTIDADE de origem,
    //  quando o editor tinha sido aberto por OpenForEntity. Esse caminho saiu
    //  junto com o ScriptComponent::Graph — a janela so abre a partir de um
    //  asset, e o SyncComponentsToPreview logo em seguida define a malha de
    //  verdade a partir dos componentes do script.
    //
    //  O que sobra e o unico trabalho que ainda importava: garantir que a
    //  entidade de preview tenha ALGUMA malha entre o InitPreviewScene e o
    //  primeiro Sync. Sem isso o preview abre com a entidade vazia e o
    //  enquadramento de camera mede bounds de nada.
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::SeedPreviewMesh()
    {
        if (!m_PreviewScene) return;

        auto& pr = m_PreviewScene->GetRegistry();
        auto& mc = pr.get_or_emplace<MeshComponent>(m_PreviewEntity);

        if (!mc.Data)
            mc.Data = MeshFactory::CreateSphere(32);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::SyncMeshFromAsset()
    {
        if (!m_PreviewScene || !m_ScriptAsset) return;
        for (auto& def : m_ScriptAsset->GetComponents())
        {
            if (def.Type == "Mesh" && !def.AssetUUID.empty())
            {
                // SC22 — aqui havia um TODO com "(void)rec; // placeholder".
                //
                // Consequencia pratica: o componente Mesh do Script Editor so
                // sabia primitivas. Arrastar uma malha importada gravava o
                // UUID no asset e o preview continuava mostrando a esfera —
                // parecia que o drop nao tinha funcionado. Foi o que voce
                // encontrou tentando dar corpo ao BP_Weapon: a pistola nao tem
                // esqueleto (logo, nao serve como SkeletalMesh) e o unico
                // outro caminho estava sem implementacao.
                //
                // Mesma resolucao que o SceneSerializer ja faz: primitiva pela
                // fabrica, asset pelo MeshLoader.
                auto& pr0 = m_PreviewScene->GetRegistry();
                auto& mc0 = pr0.get_or_emplace<MeshComponent>(m_PreviewEntity);
                mc0.AssetUUID = def.AssetUUID;

                mc0.Data = MeshFactory::ResolveByUUID(def.AssetUUID);
                if (!mc0.Data)
                    AXE_EDITOR_WARN("ScriptPreview: mesh '{}' nao resolvida.", def.AssetUUID);
            }
        }
        auto& pr = m_PreviewScene->GetRegistry();
        auto& mc = pr.get_or_emplace<MeshComponent>(m_PreviewEntity);
        if (!mc.Data) mc.Data = MeshFactory::CreateSphere(32);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::FramePreviewCamera()
    {
        if (!m_PreviewScene || !m_PreviewRenderer || !m_PreviewRenderer->m_Camera) return;
        if (m_PreviewEntity == entt::null) return;

        auto& reg = m_PreviewScene->GetRegistry();

        // Escala e posicao da ENTIDADE. Diferente do preview do AnimGraph, aqui
        // elas nao podem ser sobrescritas: vieram do Transform raiz do asset e
        // sao decisao do autor. Quem se adapta e a camera.
        glm::vec3 entPos(0.0f);
        glm::vec3 entScale(1.0f);
        if (auto* tc = reg.try_get<TransformComponent>(m_PreviewEntity))
        {
            entPos = tc->Data.Position;
            entScale = tc->Data.Scale;
        }

        // Bounds da malha em espaco LOCAL. Skeletal primeiro: e o caso comum de
        // um script de personagem, e a estatica so existe como fallback.
        bool haveBounds = false;
        glm::vec3 mn(0.0f), mx(0.0f);

        auto measure = [&](const auto& meshPtr)
            {
                if (!meshPtr) return;
                const auto& verts = meshPtr->GetVertices();
                if (verts.empty()) return;
                mn = mx = verts[0].Position;
                for (const auto& v : verts)
                {
                    mn = glm::min(mn, v.Position);
                    mx = glm::max(mx, v.Position);
                }
                haveBounds = true;
            };

        if (auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity)) measure(sk->Data);
        if (!haveBounds)
            if (auto* mc = reg.try_get<MeshComponent>(m_PreviewEntity))     measure(mc->Data);

        if (!haveBounds) return;

        // Para mundo. A altura manda no enquadramento porque personagem e alto
        // e estreito; usar a maior das tres dimensoes afastaria a camera demais
        // num objeto largo e baixo, entao pega-se o maior entre altura e a
        // maior horizontal, com a altura tendo preferencia natural.
        const glm::vec3 worldMin = entPos + mn * entScale;
        const glm::vec3 worldMax = entPos + mx * entScale;
        const glm::vec3 size = worldMax - worldMin;

        const float height = std::max(size.y, 0.0001f);
        const float width = std::max(size.x, size.z);
        const float extent = std::max(height, width);

        // 1.9x foi o fator ja calibrado no preview do AnimGraph com FOV 45 —
        // enquadra o corpo inteiro com folga em cima e embaixo. Reaproveitado
        // aqui de proposito: dois previews do mesmo editor com enquadramentos
        // diferentes parecem defeito.
        const glm::vec3 focal(
            (worldMin.x + worldMax.x) * 0.5f,
            (worldMin.y + worldMax.y) * 0.5f,
            (worldMin.z + worldMax.z) * 0.5f);

        m_PreviewRenderer->m_Camera->SetView(focal, extent * 1.9f);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::SyncComponentsToPreview()
    {
        if (!m_PreviewScene || !m_ScriptAsset || m_PreviewEntity == entt::null) return;
        auto& reg = m_PreviewScene->GetRegistry();

        // Transform RAIZ do asset -> preview. Aplicado UMA vez por asset
        // aberto: depois disso o painel Object edita o preview e escreve de
        // volta no asset (senao um sync no meio da edicao pisaria no valor
        // que o usuario esta arrastando).
        if (m_RootTransformAppliedFor != m_ScriptAsset.get())
        {
            if (auto* rtc = reg.try_get<TransformComponent>(m_PreviewEntity))
            {
                rtc->Data.Position = { m_ScriptAsset->RootPosX, m_ScriptAsset->RootPosY, m_ScriptAsset->RootPosZ };
                rtc->Data.Rotation = glm::radians(glm::vec3(
                    m_ScriptAsset->RootRotX, m_ScriptAsset->RootRotY, m_ScriptAsset->RootRotZ));
                rtc->Data.Scale = { m_ScriptAsset->RootScaleX, m_ScriptAsset->RootScaleY, m_ScriptAsset->RootScaleZ };
                rtc->Data.UseWorldMatrix = false;
                rtc->Data.WorldMatrix = rtc->Data.GetMatrix();
            }

            m_RootTransformAppliedFor = m_ScriptAsset.get();
        }

        if (reg.all_of<ColliderComponent>(m_PreviewEntity))            reg.remove<ColliderComponent>(m_PreviewEntity);
        if (reg.all_of<RigidbodyComponent>(m_PreviewEntity))           reg.remove<RigidbodyComponent>(m_PreviewEntity);
        if (reg.all_of<CharacterControllerComponent>(m_PreviewEntity)) reg.remove<CharacterControllerComponent>(m_PreviewEntity);

        bool hasMesh = false;
        bool hasSkeletal = false;

        // SC44 — um componente ANEXADO a socket nao mora na entidade raiz: ele
        // tem entidade propria (ver SyncSocketAttachmentsToPreview). Conta-lo
        // aqui faria a malha da arma virar a malha do personagem.
        for (auto& def : m_ScriptAsset->GetComponents())
        {
            if (!def.ParentSocket.empty()) continue;

            if (def.Type == "Mesh")         hasMesh = true;
            if (def.Type == "SkeletalMesh") hasSkeletal = true;
        }

        if (!hasMesh && reg.all_of<MeshComponent>(m_PreviewEntity))
            reg.remove<MeshComponent>(m_PreviewEntity);

        if (!hasSkeletal && reg.all_of<SkeletalMeshComponent>(m_PreviewEntity))
            reg.remove<SkeletalMeshComponent>(m_PreviewEntity);

        for (auto& def : m_ScriptAsset->GetComponents())
        {
            // SC44 — anexados sao tratados no passo proprio, no fim.
            if (!def.ParentSocket.empty())
                continue;

            // ── SpringArm ─────────────────────────────────────────────────────
            if (def.Type == "SpringArm" && !m_SpringArmDragging)
            {
                auto& sa = reg.get_or_emplace<SpringArmComponent>(m_PreviewEntity);
                sa.Length = def.SALength / 100.0f;
                sa.HeightOffset = def.SAHeightOffset;
                sa.SocketOffset = { def.SASocketOffX, def.SASocketOffY, def.SASocketOffZ };
                sa.LagSpeed = def.SALagSpeed;
                sa.EnableCameraLag = def.SAEnableLag;
                sa.MouseRotates = def.SAMouseRotates;
            }

            // ── SkeletalMesh: personagem animado (.axeskel + .axeanim) ───────
            if (def.Type == "SkeletalMesh")
            {
                auto& sk = reg.get_or_emplace<SkeletalMeshComponent>(m_PreviewEntity);

                // Esqueleto. Recarrega so quando o UUID muda — o cache de
                // identidade garante que e sempre o MESMO objeto do resto do
                // editor (importar um clipe no Animation Editor aparece aqui).
                if (sk.AssetUUID != def.AssetUUID)
                {
                    sk.AssetUUID = def.AssetUUID;
                    sk.Asset.reset();
                    sk.Data.reset();
                    sk.Clips.clear();
                    sk.CurrentClip = -1;
                    sk._AppliedClip = -2;

                    if (!def.AssetUUID.empty())
                    {
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
                }

                // AnimGraph. Com grafo, ele manda; sem grafo, o CurrentClip
                // acima segura a pose (o personagem nao fica em T-pose).
                if (sk.GraphAssetUUID != def.AnimGraphUUID)
                {
                    sk.GraphAssetUUID = def.AnimGraphUUID;
                    sk.GraphAsset.reset();
                    sk.GraphInstance.Reset();

                    if (!def.AnimGraphUUID.empty())
                    {
                        if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(def.AnimGraphUUID))
                        {
                            if (auto graph = AnimGraphAsset::LoadFromFile(rec->FilePath))
                            {
                                if (sk.Asset && sk.Asset->GetSkeleton())
                                    graph->Resolve(*sk.Asset);

                                sk.GraphAsset = graph;
                            }
                        }
                    }
                }

                sk.PreviewInEditor = true;
                sk.ShowSkeleton = def.ShowSkeleton;
            }
            // ── Mesh ──────────────────────────────────────────────────────────
            else if (def.Type == "Mesh")
            {
                auto& mc = reg.get_or_emplace<MeshComponent>(m_PreviewEntity);

                // SC24 — AQUI estava o motivo de a pistola nao aparecer.
                //
                // Esta linha era um CreateByUUID incondicional. Para uma
                // primitiva funciona; para um asset importado a fabrica nao
                // conhece o UUID e devolve nada, apagando a malha que o
                // SyncMeshFromAsset tinha acabado de carregar. E como este
                // Sync roda a cada mudanca no painel, a pistola era carregada
                // e destruida no mesmo frame — nenhuma escala ia adiantar,
                // porque nao havia malha nenhuma para escalar.
                const bool changed = (mc.AssetUUID != def.AssetUUID) || !mc.Data;
                mc.AssetUUID = def.AssetUUID;

                if (changed)
                {
                    mc.Data = def.AssetUUID.empty()
                        ? MeshFactory::CreateByUUID(PrimitiveUUID::Sphere)
                        : MeshFactory::ResolveByUUID(def.AssetUUID);

                    // Reenquadra: a pistola e o personagem diferem em ordens de
                    // grandeza, e manter a camera da malha anterior deixaria a
                    // nova fora de vista — que e exatamente o sintoma que
                    // parecia "escala errada".
                    m_CameraFramedFor = nullptr;
                }
            }
            // ── Material ──────────────────────────────────────────────────────
            else if (def.Type == "Material" && !def.AssetUUID.empty())
            {
                auto* existing = reg.try_get<MaterialComponent>(m_PreviewEntity);
                bool alreadyLoaded = existing && existing->MaterialAssetUUID == def.AssetUUID
                    && existing->Data && existing->Data->GetShader();
                if (!alreadyLoaded)
                {
                    const auto* rec = AssetDatabase::Get().GetByUUID(def.AssetUUID);
                    if (rec)
                    {
                        auto matAsset = MaterialAsset::LoadFromFile(rec->FilePath);
                        if (matAsset)
                        {
                            auto graphPath = rec->FilePath;
                            graphPath.replace_extension(".axegraph");
                            if (std::filesystem::exists(graphPath))
                            {
                                try
                                {
                                    std::ifstream gf(graphPath);
                                    auto gj = nlohmann::json::parse(gf);
                                    auto matGraph = std::make_unique<MaterialGraph>();
                                    matGraph->Deserialize(gj);
                                    auto result = MaterialCompiler::Compile(matGraph.get());
                                    if (result.Success)
                                    {
                                        auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
                                        if (shader) matAsset->GetMaterial()->SetShader(shader);
                                        if (!result.GeometryFragShader.empty())
                                        {
                                            auto geoShader = Shader::Create(result.VertexShader, result.GeometryFragShader);
                                            if (geoShader) matAsset->GetMaterial()->SetGeometryShader(geoShader);
                                        }
                                    }
                                }
                                catch (...) {}
                            }
                            auto& mc = reg.get_or_emplace<MaterialComponent>(m_PreviewEntity);
                            mc.Data = matAsset->GetMaterial();
                            mc.MaterialAssetUUID = def.AssetUUID;
                        }
                    }
                }
            }
            // ── Rigidbody ─────────────────────────────────────────────────────
            else if (def.Type == "Rigidbody")
            {
                auto& rb = reg.emplace_or_replace<RigidbodyComponent>(m_PreviewEntity);
                rb.Type = def.BodyType == "Static" ? BodyType::Static :
                    def.BodyType == "Kinematic" ? BodyType::Kinematic : BodyType::Dynamic;
                rb.Mass = def.Mass; rb.Friction = def.Friction; rb.Restitution = def.Restitution;
                rb.LinearDamping = def.LinearDamping; rb.AngularDamping = def.AngularDamping;
                rb.UseGravity = def.UseGravity;
                rb.LockRotX = def.LockRotX; rb.LockRotY = def.LockRotY; rb.LockRotZ = def.LockRotZ;
            }
            // ── Collider ──────────────────────────────────────────────────────
            else if (def.Type.find("Collider") != std::string::npos)
            {
                auto& col = reg.emplace_or_replace<ColliderComponent>(m_PreviewEntity);
                col.IsTrigger = def.IsTrigger;
                col.ShowDebug = def.ShowDebug;
                col.Offset = { def.ColliderOffsetX, def.ColliderOffsetY, def.ColliderOffsetZ };

                if (def.ColliderShape == "Sphere")     col.Shape = ColliderShape::Sphere;
                else if (def.ColliderShape == "Capsule")    col.Shape = ColliderShape::Capsule;
                else if (def.ColliderShape == "Mesh")       col.Shape = ColliderShape::Mesh;
                else if (def.ColliderShape == "ConvexHull") col.Shape = ColliderShape::ConvexHull;
                else                                        col.Shape = ColliderShape::Box;

                col.HalfExtent = { def.ColliderSizeX, def.ColliderSizeY, def.ColliderSizeZ };
                col.Radius = def.ColliderRadius;
                col.Height = def.ColliderHeight;
                col.CapsuleRadius = def.ColliderCapsuleRadius;
            }
            // ── CharacterController ───────────────────────────────────────────
            else if (def.Type == "CharacterController")
            {
                auto& cc = reg.emplace_or_replace<CharacterControllerComponent>(m_PreviewEntity);
                cc.Height = def.CCHeight; cc.Radius = def.CCRadius;
                cc.MaxSlopeAngle = def.CCMaxSlope; cc.StepHeight = def.CCStepHeight;
                cc.MaxSpeed = def.CCMaxSpeed; cc.JumpForce = def.CCJumpForce;

                // Faltavam estes dois: por isso mexer no Capsule Offset (ou
                // desligar o wireframe) no Script Editor nao mudava NADA no
                // preview — o componente era recriado sem eles a cada sync.
                cc.CapsuleOffset = { def.CCOffsetX, def.CCOffsetY, def.CCOffsetZ };
                cc.ShowDebug = def.CCShowDebug;

                // O collider implicito que existia aqui so para "ver a
                // capsula" saiu: o CharacterController agora tem desenho
                // proprio (e correto, sem a escala do transform). Manter os
                // dois desenhava uma segunda capsula, minuscula, por cima.
                if (reg.all_of<ColliderComponent>(m_PreviewEntity))
                    reg.remove<ColliderComponent>(m_PreviewEntity);
            }
        }

        // SC15 — enquadra a camera UMA vez por asset. Aqui no fim, e nao no
        // OpenForAsset: naquele momento a malha ainda nao foi resolvida (o
        // SkeletalMeshAsset e carregado neste proprio laco), e medir bounds
        // vazios daria uma distancia sem sentido.
        if (m_CameraFramedFor != m_ScriptAsset.get())
        {
            FramePreviewCamera();
            m_CameraFramedFor = m_ScriptAsset.get();
        }

        SyncSocketAttachmentsToPreview();   // SC44
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  SC44 — anexos a socket no PREVIEW
    //
    //  A mesma coisa que o InstantiateScriptAsset faz na cena, com a mesma
    //  forma: entidade filha + SocketAttachmentComponent, e o AnimationWorld
    //  (que ja roda neste preview) resolve o transform.
    //
    //  Escrever a matriz na mao aqui teria sido mais curto e teria criado uma
    //  SEGUNDA implementacao de "onde fica o socket". Duas contas para a mesma
    //  pergunta divergem — e a divergencia so aparece no jogo, depois de o
    //  autor ter jurado que estava certo no editor.
    //
    //  As entidades sao RECRIADAS quando a lista muda, e nao reaproveitadas
    //  por indice: um componente removido no meio deslocaria todos os indices
    //  seguintes, e o anexo do 'GunSocket' passaria a apontar para a malha do
    //  vizinho sem que nada avisasse.
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::SyncSocketAttachmentsToPreview()
    {
        if (!m_PreviewScene || !m_ScriptAsset) return;

        auto& reg = m_PreviewScene->GetRegistry();
        const auto& comps = m_ScriptAsset->GetComponents();

        // Assinatura do estado atual. Barata de calcular e suficiente: se ela
        // nao mudou, nenhuma entidade precisa nascer ou morrer.
        std::string sig;

        for (const auto& def : comps)
        {
            if (def.ParentSocket.empty()) continue;

            sig += def.Type + "|" + def.ParentSocket + "|" + def.AssetUUID + ";";
        }

        if (sig != m_SocketAttachSig)
        {
            m_SocketAttachSig = sig;

            for (auto e : m_SocketAttachEntities)
                if (e != entt::null && reg.valid(e))
                    m_PreviewScene->DestroyEntity(e);

            m_SocketAttachEntities.clear();

            for (const auto& def : comps)
            {
                if (def.ParentSocket.empty()) continue;

                // Mesma reconferencia da instanciacao: sem pai esqueleto, sem
                // anexo. O componente simplesmente nao aparece no preview —
                // coerente com o que aconteceria na cena.
                if (def.ParentIndex < 0 || def.ParentIndex >= (int)comps.size())
                    continue;

                if (comps[def.ParentIndex].Type != "SkeletalMesh")
                    continue;

                if (def.Type != "Mesh")
                    continue;   // so malha tem o que mostrar num preview

                auto child = m_PreviewScene->CreateEntity("SocketAttach_" + def.ParentSocket);

                auto& mc = reg.emplace<MeshComponent>(child);
                mc.AssetUUID = def.AssetUUID;
                mc.Data = MeshFactory::ResolveByUUID(def.AssetUUID);

                auto& att = reg.emplace<SocketAttachmentComponent>(child);
                att.Target = m_PreviewEntity;
                att.SocketName = def.ParentSocket;

                m_SocketAttachEntities.push_back(child);
            }
        }

        // Transform local: fora do bloco acima porque arrastar Location no
        // painel muda o valor sem mudar a assinatura — e e justamente o ajuste
        // que precisa aparecer ao vivo.
        std::size_t k = 0;

        for (const auto& def : comps)
        {
            if (def.ParentSocket.empty()) continue;
            if (k >= m_SocketAttachEntities.size()) break;

            const entt::entity e = m_SocketAttachEntities[k];

            if (e != entt::null && reg.valid(e))
            {
                if (auto* tc = reg.try_get<TransformComponent>(e))
                {
                    tc->Data.Position = glm::vec3(def.PosX, def.PosY, def.PosZ);
                    tc->Data.Rotation = glm::radians(glm::vec3(def.RotX, def.RotY, def.RotZ));
                    tc->Data.Scale = glm::vec3(def.ScaleX, def.ScaleY, def.ScaleZ);
                }
            }

            ++k;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::RenderPreview()
    {
        if (!m_IsOpen || !m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene) return;

        if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
        {
            sr->SetDeferredEnabled(false);
            sr->SetDeferredSupported(false);
        }

        m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());
        if (m_PreviewEnvironment && m_PreviewRenderer->GetSceneRenderer())
            m_PreviewRenderer->GetSceneRenderer()->SetEnvironment(m_PreviewEnvironment.get());

        uint32_t w = (m_PreviewSize.x > 4) ? (uint32_t)m_PreviewSize.x : 512u;
        uint32_t h = (m_PreviewSize.y > 4) ? (uint32_t)m_PreviewSize.y : 512u;
        auto& spec = m_PreviewFramebuffer->GetSpecification();
        if ((uint32_t)spec.Width != w || (uint32_t)spec.Height != h)
        {
            m_PreviewFramebuffer->Resize(w, h);
            if (m_PreviewRenderer->m_Camera)
                m_PreviewRenderer->m_Camera->SetAspectRatio((float)w / (float)h);
        }

        // Anima o personagem do preview (SkeletalMesh + AnimGraph). Sem Play:
        // PreviewInEditor ja autoriza o tempo a correr no editor.
        {
            if (!m_PreviewAnim)
                m_PreviewAnim = std::make_unique<AnimationWorld>();

            m_PreviewAnim->OnUpdate(*m_PreviewScene, ImGui::GetIO().DeltaTime, false);
        }

        m_PreviewRenderer->SetScene(m_PreviewScene.get());
        m_PreviewRenderer->RenderToFramebuffer(*m_PreviewFramebuffer, w, h, 0.0f);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawPreviewWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("Script Preview"))
        {
            // ── Toolbar de gizmo ─────────────────────────────────────────────
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
            ImGui::SetCursorPos(ImVec2(6, 6));

            auto gizmoBtn = [&](const char* lbl, ImGuizmo::OPERATION op)
                {
                    bool active = (m_GizmoOp == op);
                    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.9f, 0.9f));
                    if (ImGui::SmallButton(lbl)) m_GizmoOp = op;
                    if (active) ImGui::PopStyleColor();
                    ImGui::SameLine(0, 2);
                };
            gizmoBtn("T", ImGuizmo::TRANSLATE);
            gizmoBtn("R", ImGuizmo::ROTATE);
            gizmoBtn("S", ImGuizmo::SCALE);
            ImGui::PopStyleVar();

            // O helper acima chama SameLine DEPOIS de cada botao — inclusive
            // do ultimo. Sem quebrar a linha aqui, a imagem do preview era
            // submetida AO LADO dos botoes: comecava deslocada e o
            // GetContentRegionAvail devolvia so a sobra da linha, deixando a
            // faixa vazia a esquerda e o preview menor que o painel.
            ImGui::NewLine();

            // ── Imagem do preview ─────────────────────────────────────────────
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 4 && avail.y > 4) m_PreviewSize = avail;
            m_PreviewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

            ImVec2 imgPos = ImGui::GetCursorScreenPos();
            m_PreviewBoundsMin = { imgPos.x, imgPos.y };
            m_PreviewBoundsMax = { imgPos.x + avail.x, imgPos.y + avail.y };

            if (m_PreviewFramebuffer)
            {
                ImTextureID tid = (ImTextureID)(uintptr_t)
                    m_PreviewFramebuffer->GetColorAttachmentRendererID();
                if (tid) ImGui::Image(tid, avail, ImVec2(0, 1), ImVec2(1, 0));
                else     ImGui::Dummy(avail);
            }
            else ImGui::Dummy(avail);

            DrawPreviewGizmo();
            HandlePreviewInput();

            // ── Overlays de texto ─────────────────────────────────────────────
            auto* dl = ImGui::GetForegroundDrawList();
            ImVec2 wp = ImGui::GetWindowPos();
            dl->AddText(ImVec2(wp.x + 6, m_PreviewBoundsMax.y - 18),
                ImColor(180, 180, 180, 120), "Alt+LMB orbitar | Scroll zoom | T/R/S gizmo");

            const char* lbl = m_ScriptAsset ? m_ScriptAsset->GetName().c_str() : nullptr;
            if (lbl)
            {
                float tw = ImGui::CalcTextSize(lbl).x;
                dl->AddRectFilled(ImVec2(wp.x + 4, imgPos.y + 4),
                    ImVec2(wp.x + tw + 12, imgPos.y + 20), ImColor(0, 0, 0, 160), 2.f);
                dl->AddText(ImVec2(wp.x + 6, imgPos.y + 5), ImColor(220, 220, 100, 255), lbl);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawPreviewGizmo()
    {
        if (!m_PreviewRenderer || !m_PreviewScene || m_PreviewEntity == entt::null) return;
        if (!m_PreviewRenderer->m_Camera) return;
        auto& reg = m_PreviewScene->GetRegistry();
        if (!reg.valid(m_PreviewEntity)) return;
        auto* tc = reg.try_get<TransformComponent>(m_PreviewEntity);
        if (!tc) return;

        float w = m_PreviewBoundsMax.x - m_PreviewBoundsMin.x;
        float h = m_PreviewBoundsMax.y - m_PreviewBoundsMin.y;
        if (w <= 0.f || h <= 0.f) return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(m_PreviewBoundsMin.x, m_PreviewBoundsMin.y, w, h);

        glm::mat4 view = m_PreviewRenderer->m_Camera->GetViewMatrix();
        glm::mat4 proj = m_PreviewRenderer->m_Camera->GetProjectionMatrix();

        bool cameraSelected = false;
        if (m_ScriptAsset && m_SelectedCompIndex >= 0 &&
            m_SelectedCompIndex < (int)m_ScriptAsset->GetComponents().size())
        {
            auto& selDef = m_ScriptAsset->GetComponents()[m_SelectedCompIndex];
            cameraSelected = (selDef.Type == "SpringArm" || selDef.Type == "Camera");
        }

        auto* sa = reg.try_get<SpringArmComponent>(m_PreviewEntity);
        if (sa && (m_CameraPreviewEntity == entt::null || !reg.valid(m_CameraPreviewEntity)))
        {
            m_CameraPreviewEntity = m_PreviewScene->CreateEntity("CameraPreviewMesh");
            auto& mc = reg.get_or_emplace<MeshComponent>(m_CameraPreviewEntity);
            mc.Data = MeshFactory::CreateCamera();
        }

        TransformComponent* camTc = (m_CameraPreviewEntity != entt::null && reg.valid(m_CameraPreviewEntity))
            ? reg.try_get<TransformComponent>(m_CameraPreviewEntity) : nullptr;

        auto* gizmoTc = (cameraSelected && camTc) ? camTc : tc;

        if (sa && camTc && !m_SpringArmDragging)
        {
            camTc->Data.Position = tc->Data.Position + glm::vec3(
                sa->SocketOffset.x,
                sa->HeightOffset + sa->SocketOffset.y,
                sa->Length + sa->SocketOffset.z);
            camTc->Data.Scale = glm::vec3(0.35f);
            glm::vec3 dir = camTc->Data.Position - tc->Data.Position;
            if (glm::length(dir) > 0.001f)
            {
                dir = glm::normalize(dir);
                camTc->Data.Rotation.y = glm::atan(dir.x, dir.z);
                camTc->Data.Rotation.x = glm::asin(-dir.y);
            }
        }

        glm::mat4 model = gizmoTc->Data.GetMatrix();

        if (!cameraSelected)
        {
            bool used = ImGuizmo::Manipulate(
                glm::value_ptr(view), glm::value_ptr(proj),
                m_GizmoOp, ImGuizmo::LOCAL, glm::value_ptr(model));

            if (used)
            {
                float t[3], r[3], s[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), t, r, s);
                gizmoTc->Data.Position = { t[0], t[1], t[2] };
                gizmoTc->Data.Rotation = { glm::radians(r[0]), glm::radians(r[1]), glm::radians(r[2]) };
                gizmoTc->Data.Scale = { std::max(s[0],0.001f), std::max(s[1],0.001f), std::max(s[2],0.001f) };
                gizmoTc->Data.UseWorldMatrix = false;
                gizmoTc->Data.WorldMatrix = gizmoTc->Data.GetMatrix();
            }
        }

        // ── SpringArm Gizmo (linha + ponto arrastável) ────────────────────────
        auto* sa2D = reg.try_get<SpringArmComponent>(m_PreviewEntity);
        if (sa2D && m_ScriptAsset)
        {
            glm::vec3 pawnPos = tc->Data.Position;
            glm::vec3 camOffset = {
                sa2D->SocketOffset.x,
                sa2D->HeightOffset + sa2D->SocketOffset.y,
                sa2D->Length + sa2D->SocketOffset.z
            };
            glm::vec3 camPos = pawnPos + camOffset;

            auto worldToScreen = [&](const glm::vec3& world) -> ImVec2
                {
                    glm::vec4 clip = proj * view * glm::vec4(world, 1.0f);
                    if (std::abs(clip.w) < 0.001f) return ImVec2(-9999, -9999);
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    float sx = m_PreviewBoundsMin.x + (ndc.x * 0.5f + 0.5f) * w;
                    float sy = m_PreviewBoundsMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * h;
                    return ImVec2(sx, sy);
                };

            ImVec2 pawnScreen = worldToScreen(pawnPos);
            ImVec2 camScreen = worldToScreen(camPos);
            ImDrawList* dl = ImGui::GetWindowDrawList();

            dl->AddLine(pawnScreen, camScreen, IM_COL32(180, 130, 255, 200), 1.5f);
            dl->AddCircleFilled(pawnScreen, 4.f, IM_COL32(200, 150, 255, 230));

            bool hovered = ImGui::IsMouseHoveringRect(
                ImVec2(camScreen.x - 8, camScreen.y - 8),
                ImVec2(camScreen.x + 8, camScreen.y + 8));
            dl->AddCircleFilled(camScreen, hovered ? 8.f : 6.f,
                hovered ? IM_COL32(255, 200, 100, 255) : IM_COL32(255, 180, 80, 200));
            dl->AddCircle(camScreen, hovered ? 8.f : 6.f, IM_COL32(255, 255, 255, 180), 12, 1.f);
            dl->AddText(ImVec2(camScreen.x + 10, camScreen.y - 8), IM_COL32(220, 200, 255, 220), "Camera");

            // Frustum
            float camFov = 60.f;
            auto* camComp = reg.try_get<CameraComponent>(m_PreviewEntity);
            if (camComp) camFov = camComp->Fov;
            float halfFovRad = glm::radians(camFov * 0.5f);
            float frustumLen = 0.8f;
            float frustumHalf = std::tan(halfFovRad) * frustumLen;

            glm::vec3 camDir = glm::normalize(pawnPos - camPos);
            glm::vec3 camRight = glm::normalize(glm::cross(camDir, glm::vec3(0, 1, 0)));
            glm::vec3 camUp = glm::normalize(glm::cross(camRight, camDir));

            glm::vec3 ftl = camPos + camDir * frustumLen + camUp * frustumHalf - camRight * frustumHalf;
            glm::vec3 ftr = camPos + camDir * frustumLen + camUp * frustumHalf + camRight * frustumHalf;
            glm::vec3 fbl = camPos + camDir * frustumLen - camUp * frustumHalf - camRight * frustumHalf;
            glm::vec3 fbr = camPos + camDir * frustumLen - camUp * frustumHalf + camRight * frustumHalf;

            ImVec2 sFtl = worldToScreen(ftl), sFtr = worldToScreen(ftr);
            ImVec2 sFbl = worldToScreen(fbl), sFbr = worldToScreen(fbr);
            ImU32 fc = IM_COL32(140, 200, 255, 220), fc2 = IM_COL32(180, 220, 255, 180);
            dl->AddLine(camScreen, sFtl, fc, 2.f); dl->AddLine(camScreen, sFtr, fc, 2.f);
            dl->AddLine(camScreen, sFbl, fc, 2.f); dl->AddLine(camScreen, sFbr, fc, 2.f);
            dl->AddLine(sFtl, sFtr, fc2, 2.f); dl->AddLine(sFtr, sFbr, fc2, 2.f);
            dl->AddLine(sFbr, sFbl, fc2, 2.f); dl->AddLine(sFbl, sFtl, fc2, 2.f);

            // Drag do endpoint
            if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                float ppu = h / (2.0f * std::tan(glm::radians(45.0f / 2.0f)) * 5.0f);
                if (ppu > 0.001f)
                {
                    sa2D->Length -= delta.y / ppu;
                    sa2D->HeightOffset += delta.y / ppu * 0.3f;
                    sa2D->Length = std::max(sa2D->Length, 1.0f);
                    if (m_ScriptAsset)
                        for (auto& d : m_ScriptAsset->GetComponents())
                            if (d.Type == "SpringArm")
                            {
                                d.SALength = sa2D->Length * 100.0f;
                                d.SAHeightOffset = sa2D->HeightOffset;
                                break;
                            }
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::HandlePreviewInput()
    {
        if (!m_PreviewHovered || !m_PreviewRenderer) return;
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mousePos = ImGui::GetMousePos();
        static ImVec2 lastMouse = mousePos;
        m_PreviewMouseDelta = ImVec2(mousePos.x - lastMouse.x, mousePos.y - lastMouse.y);
        lastMouse = mousePos;

        const bool alt = io.KeyAlt;
        glm::vec2 delta(m_PreviewMouseDelta.x, m_PreviewMouseDelta.y);
        delta *= 0.003f;

        if (alt)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))        m_PreviewRenderer->OnMouseRotate(delta);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_PreviewRenderer->OnMousePan(delta);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))  m_PreviewRenderer->OnMouseZoom(delta.y * 10.f);
        }
        if (io.MouseWheel != 0.0f) m_PreviewRenderer->OnMouseZoom(io.MouseWheel);

        // ── Picking por ray cast (sem Alt) ────────────────────────────────────
        if (!alt && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_PreviewScene)
        {
            float w = m_PreviewBoundsMax.x - m_PreviewBoundsMin.x;
            float h = m_PreviewBoundsMax.y - m_PreviewBoundsMin.y;
            if (w > 0.f && h > 0.f)
            {
                float nx = ((mousePos.x - m_PreviewBoundsMin.x) / w) * 2.f - 1.f;
                float ny = 1.f - ((mousePos.y - m_PreviewBoundsMin.y) / h) * 2.f;

                glm::mat4 view = m_PreviewRenderer->m_Camera->GetViewMatrix();
                glm::mat4 proj = m_PreviewRenderer->m_Camera->GetProjectionMatrix();
                glm::mat4 invVP = glm::inverse(proj * view);

                glm::vec4 rNear = invVP * glm::vec4(nx, ny, -1.f, 1.f);
                glm::vec4 rFar = invVP * glm::vec4(nx, ny, 1.f, 1.f);
                rNear /= rNear.w; rFar /= rFar.w;

                glm::vec3 rayOrigin = glm::vec3(rNear);
                glm::vec3 rayDir = glm::normalize(glm::vec3(rFar) - rayOrigin);

                auto& reg = m_PreviewScene->GetRegistry();
                entt::entity bestEntity = entt::null;
                float bestDist = 1e9f;

                auto testSphere = [&](entt::entity e, float radius)
                    {
                        auto* ptc = reg.try_get<TransformComponent>(e);
                        if (!ptc) return;
                        glm::vec3 oc = rayOrigin - ptc->Data.Position;
                        float b = glm::dot(oc, rayDir);
                        float c = glm::dot(oc, oc) - radius * radius;
                        float disc = b * b - c;
                        if (disc >= 0.f)
                        {
                            float t = -b - glm::sqrt(disc);
                            if (t > 0.001f && t < bestDist) { bestDist = t; bestEntity = e; }
                        }
                    };

                testSphere(m_PreviewEntity, 0.5f);
                if (m_CameraPreviewEntity != entt::null && reg.valid(m_CameraPreviewEntity))
                    testSphere(m_CameraPreviewEntity, 0.25f);

                if (bestEntity != entt::null && m_ScriptAsset)
                {
                    if (bestEntity == m_CameraPreviewEntity)
                    {
                        auto& comps = m_ScriptAsset->GetComponents();
                        for (int i = 0; i < (int)comps.size(); i++)
                            if (comps[i].Type == "SpringArm" || comps[i].Type == "Camera")
                            {
                                m_SelectedCompIndex = i; break;
                            }
                    }
                    else
                        m_SelectedCompIndex = 0;
                }
                else
                    m_SelectedCompIndex = -1;
            }
        }
    }

} // namespace axe