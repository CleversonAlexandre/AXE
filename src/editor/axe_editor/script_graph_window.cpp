#include "script_graph_window.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_graph_compiler.hpp"
#include "axe/script/script_compiler.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/scene/components.hpp"
#include "axe/material/material_asset.hpp"
#include "asset/asset_picker.hpp"
#include "node_graph/material_graph.hpp"
#include "axe/material/material_compiler.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/physics/physics_components.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>
#include "axe/graphics/editor_camera.hpp"
#include "axe/log/log.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/utils/glm_config.hpp"
#include <imgui_internal.h>
#include <imgui_node_editor.h>
#include <utilities/widgets.h>
#include <utilities/drawing.h>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ed = ax::NodeEditor;
using IconType = ax::Drawing::IconType;

namespace axe
{
    static const float ICON_SZ = 20.0f;
    static const float PIN_H = 22.0f;

    static IconType PinIcon(ScriptPinType t)
    {
        return t == ScriptPinType::Flow ? IconType::Flow : IconType::Circle;
    }
    static ImVec4 PinCol(ScriptPinType t)
    {
        ImColor c = GetPinColor(t); return { c.Value.x,c.Value.y,c.Value.z,c.Value.w };
    }
    static ImVec4 HdrCol(ScriptNodeCategory c)
    {
        ImColor x = GetNodeHeaderColor(c); return { x.Value.x,x.Value.y,x.Value.z,x.Value.w };
    }

    struct NE { const char* label; const char* type; };
    static const NE sEv[] = { {"On Start","OnStart"},{"On Update","OnUpdate"},
        {"On End","OnEnd"},{"On Collision","OnCollision"},{"On Event","OnEvent"} };
    static const NE sAc[] = { {"Move","Move"},{"Rotate","Rotate"},{"Apply Force","ApplyForce"},
        {"Send Event","SendEvent"},{"Print String","PrintString"} };
    static const NE sLo[] = { {"Branch","Branch"},{"Compare","Compare"},
        {"Get Variable","GetVariable"},{"Set Variable","SetVariable"} };
    static const NE sMa[] = { {"Add","Add"},{"Multiply","Multiply"},{"Make Vec3","MakeVec3"} };
    static const NE sIn[] = { {"Get Key","GetKey"},{"Get Axis","GetAxis"} };

    struct CatDef { const char* name; const NE* e; int n; ImVec4 col; };
    static const CatDef s_Cats[] = {
        {"Eventos",    sEv, 5, {0.85f,0.3f,0.2f,1}},
        {"Acoes",      sAc, 5, {0.2f,0.7f,0.45f,1}},
        {"Logica",     sLo, 4, {0.8f,0.6f,0.1f,1}},
        {"Matematica", sMa, 3, {0.3f,0.5f,0.9f,1}},
        {"Input",      sIn, 2, {0.7f,0.2f,0.6f,1}},
    };
    static const ImVec4 s_CtxCols[] = {
        {1.f,0.45f,0.35f,1},{0.3f,0.85f,0.55f,1},
        {1.f,0.78f,0.2f,1},{0.4f,0.65f,1.f,1},{0.85f,0.3f,0.75f,1} };

    // Nodes gerados por componente — mapeados do tipo do ScriptComponentDef
    struct CompNodeEntry { const char* label; const char* type; };
    static const CompNodeEntry s_TransformNodes[] = {
        {"Get Transform", "GetTransform"}, {"Set Transform", "SetTransform"},
        {"Get Position",  "GetPosition"},  {"Set Position",  "SetPosition"},
    };
    static const CompNodeEntry s_RigidbodyNodes[] = {
        {"Get Rigidbody",   "GetRigidbody"},
        {"Set Velocity",    "SetRigidbodyVelocity"},
        {"Apply Force",     "ApplyForce"},
    };
    static const CompNodeEntry s_ColliderNodes[] = {
        {"Get Collider",    "GetCollider"},
        {"On Collision",    "OnCollision"},
    };
    static const CompNodeEntry s_CCNodes[] = {
        {"Get Character Ctrl", "GetCharacterController"},
        {"Character Move",     "CharacterMove"},
        {"Character Jump",     "CharacterJump"},
    };

    // ─────────────────────────────────────────────────────────────────────────
    ScriptGraphWindow::ScriptGraphWindow() = default;
    ScriptGraphWindow::~ScriptGraphWindow() { Shutdown(); }

    void ScriptGraphWindow::Initialize()
    {
        ed::Config cfg; cfg.SettingsFile = nullptr;
        m_EdCtx = ed::CreateEditor(&cfg);
    }

    void ScriptGraphWindow::Shutdown()
    {
        if (m_EdCtx) { ed::DestroyEditor(m_EdCtx); m_EdCtx = nullptr; }
        m_PreviewRenderer.reset();
        m_PreviewFramebuffer.reset();
        m_PreviewScene.reset();
        m_PreviewEnvironment.reset();
    }

    void ScriptGraphWindow::OpenForEntity(entt::entity entity, ScriptComponent* comp,
        entt::registry* registry)
    {
        m_Entity = entity;
        m_Component = comp;
        m_Graph = comp ? comp->Graph.get() : nullptr;
        m_SourceRegistry = registry;
        m_IsOpen = true;
        m_FirstFrame = true;
        // Não reseta m_LayoutBuilt — preserva o layout entre aberturas
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
        // Não reseta m_LayoutBuilt — preserva o layout entre aberturas
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
        m_IsOpen = false; m_Graph = nullptr;
        m_Component = nullptr; m_Entity = entt::null;
        m_SourceRegistry = nullptr;
    }

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
            sr->SetDeferredEnabled(false); sr->SetDeferredSupported(false);
        }

        m_PreviewRenderer->m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 1000.0f);
        m_PreviewRenderer->ShowGrid = true;
        m_PreviewRenderer->ShowColliders = true;

        m_PreviewScene = std::make_unique<Scene>();
        m_PreviewEntity = m_PreviewScene->CreateEntity("ScriptPreview");
        SyncMeshFromSource();

        auto& reg = m_PreviewScene->GetRegistry();
        auto light = m_PreviewScene->CreateEntity("PreviewLight");
        auto& lc = reg.emplace<LightComponent>(light);
        lc.Data = std::make_shared<DirectionalLight>();
        lc.Data->Direction = glm::vec3(0.0f, -1.0f, -1.0f);
        lc.Data->Color = glm::vec3(1.0f, 1.0f, 1.0f);
        lc.Data->Intensity = 3.0f;
        lc.Data->AmbientStrength = 0.3f;
        lc.Data->IBLIntensity = 0.15f; // reduz IBL — forward shader aplica gamma+tonemap interno

        m_PreviewRenderer->SetScene(m_PreviewScene.get());
        m_PreviewEnvironment = std::make_unique<SceneEnvironment>();
        m_PreviewEnvironment->LoadHDRI("resources/quarry_04_puresky_2k.hdr");
        m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());
        if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
            sr->SetEnvironment(m_PreviewEnvironment.get());

        // Sem PostProcessComponent — usa os defaults do renderer
        // igual ao Material Editor, para não alterar a aparência do material
    }

    void ScriptGraphWindow::SyncMeshFromSource()
    {
        if (!m_PreviewScene) return;
        auto& pr = m_PreviewScene->GetRegistry();
        if (m_SourceRegistry && m_Entity != entt::null &&
            m_SourceRegistry->valid(m_Entity) &&
            m_SourceRegistry->all_of<MeshComponent>(m_Entity))
        {
            auto& sm = m_SourceRegistry->get<MeshComponent>(m_Entity);
            auto& dm = pr.get_or_emplace<MeshComponent>(m_PreviewEntity);
            dm.Data = sm.Data;
            if (m_SourceRegistry->all_of<MaterialComponent>(m_Entity))
            {
                auto& smat = m_SourceRegistry->get<MaterialComponent>(m_Entity);
                auto& dmat = pr.get_or_emplace<MaterialComponent>(m_PreviewEntity);
                dmat.Data = smat.Data;
            }
        }
        else
        {
            auto& mc = pr.get_or_emplace<MeshComponent>(m_PreviewEntity);
            if (!mc.Data) mc.Data = MeshFactory::CreateSphere(32);
        }
    }

    void ScriptGraphWindow::SyncMeshFromAsset()
    {
        if (!m_PreviewScene || !m_ScriptAsset) return;
        // Usa a primeira MeshComponent definida no asset
        for (auto& def : m_ScriptAsset->GetComponents())
        {
            if (def.Type == "Mesh" && !def.AssetUUID.empty())
            {
                // Tenta carregar pelo UUID
                const auto* rec = AssetDatabase::Get().GetByUUID(def.AssetUUID);
                if (rec)
                {
                    // TODO: carregar mesh pelo AssetDatabase quando houver MeshAsset
                    // Por ora usa esfera de placeholder
                }
            }
        }
        // Fallback: esfera
        auto& pr = m_PreviewScene->GetRegistry();
        auto& mc = pr.get_or_emplace<MeshComponent>(m_PreviewEntity);
        if (!mc.Data) mc.Data = MeshFactory::CreateSphere(32);
    }

    void ScriptGraphWindow::SyncComponentsToPreview()
    {
        if (!m_PreviewScene || !m_ScriptAsset || m_PreviewEntity == entt::null) return;
        auto& reg = m_PreviewScene->GetRegistry();

        // Remove todos os componentes visuais e físicos antes de recriar
        if (reg.all_of<ColliderComponent>(m_PreviewEntity))               reg.remove<ColliderComponent>(m_PreviewEntity);
        if (reg.all_of<RigidbodyComponent>(m_PreviewEntity))              reg.remove<RigidbodyComponent>(m_PreviewEntity);
        if (reg.all_of<CharacterControllerComponent>(m_PreviewEntity))    reg.remove<CharacterControllerComponent>(m_PreviewEntity);

        // Verifica se há componente Mesh no asset — se não houver, remove a mesh do preview
        bool hasMesh = false;
        for (auto& def : m_ScriptAsset->GetComponents())
            if (def.Type == "Mesh") { hasMesh = true; break; }

        if (!hasMesh && reg.all_of<MeshComponent>(m_PreviewEntity))
            reg.remove<MeshComponent>(m_PreviewEntity);

        for (auto& def : m_ScriptAsset->GetComponents())
        {
            // ── Mesh ──────────────────────────────────────────────────────────
            if (def.Type == "Mesh")
            {
                auto& mc = reg.get_or_emplace<MeshComponent>(m_PreviewEntity);
                mc.Data = MeshFactory::CreateByUUID(
                    def.AssetUUID.empty() ? PrimitiveUUID::Sphere : def.AssetUUID);
                mc.AssetUUID = def.AssetUUID;
            }
            // ── Material ──────────────────────────────────────────────────────
            else if (def.Type == "Material" && !def.AssetUUID.empty())
            {
                // Só recarrega se o UUID mudou — evita recompilar shader todo frame
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
                    def.BodyType == "Kinematic" ? BodyType::Kinematic :
                    BodyType::Dynamic;
                rb.Mass = def.Mass;
                rb.Friction = def.Friction;
                rb.Restitution = def.Restitution;
                rb.LinearDamping = def.LinearDamping;
                rb.AngularDamping = def.AngularDamping;
                rb.UseGravity = def.UseGravity;
                rb.LockRotX = def.LockRotX;
                rb.LockRotY = def.LockRotY;
                rb.LockRotZ = def.LockRotZ;
            }
            // ── Collider ──────────────────────────────────────────────────────
            else if (def.Type.find("Collider") != std::string::npos)
            {
                auto& col = reg.emplace_or_replace<ColliderComponent>(m_PreviewEntity);
                col.IsTrigger = def.IsTrigger;
                col.ShowDebug = def.ShowDebug;   // controla o wireframe
                col.Offset = { def.ColliderOffsetX, def.ColliderOffsetY, def.ColliderOffsetZ };

                if (def.ColliderShape == "Sphere")    col.Shape = ColliderShape::Sphere;
                else if (def.ColliderShape == "Capsule")   col.Shape = ColliderShape::Capsule;
                else if (def.ColliderShape == "Mesh")      col.Shape = ColliderShape::Mesh;
                else if (def.ColliderShape == "ConvexHull")col.Shape = ColliderShape::ConvexHull;
                else                                       col.Shape = ColliderShape::Box;

                col.HalfExtent = { def.ColliderSizeX, def.ColliderSizeY, def.ColliderSizeZ };
                col.Radius = def.ColliderRadius;
                col.Height = def.ColliderHeight;
                col.CapsuleRadius = def.ColliderCapsuleRadius;
            }
            // ── CharacterController ───────────────────────────────────────────
            else if (def.Type == "CharacterController")
            {
                auto& cc = reg.emplace_or_replace<CharacterControllerComponent>(m_PreviewEntity);
                cc.Height = def.CCHeight;
                cc.Radius = def.CCRadius;
                cc.MaxSlopeAngle = def.CCMaxSlope;
                cc.StepHeight = def.CCStepHeight;
                cc.MaxSpeed = def.CCMaxSpeed;
                cc.JumpForce = def.CCJumpForce;

                // CharacterController implica collider capsule para debug
                auto& col = reg.emplace_or_replace<ColliderComponent>(m_PreviewEntity);
                col.Shape = ColliderShape::Capsule;
                col.Height = def.CCHeight;
                col.CapsuleRadius = def.CCRadius;
                col.ShowDebug = true;
            }
        }
    }

    void ScriptGraphWindow::RenderPreview()
    {
        if (!m_IsOpen || !m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene) return;

        // Garante forward toda frame — igual ao MaterialEditorWindow
        if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
        {
            sr->SetDeferredEnabled(false);
            sr->SetDeferredSupported(false);
        }

        // Garante ambiente toda frame
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

        m_PreviewRenderer->SetScene(m_PreviewScene.get());
        m_PreviewRenderer->RenderToFramebuffer(*m_PreviewFramebuffer, w, h, 0.0f);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Draw — janela host com DockSpace interno
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::Draw()
    {
        if (!m_IsOpen || !m_Graph) return;

        ImGui::SetNextWindowSize(ImVec2(1280, 820), ImGuiCond_FirstUseEver);
        std::string title = "Script Editor \xe2\x80\x94 " +
            (m_Component ? m_Component->ScriptName : "?") + "###ScriptEditorHost";

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool vis = ImGui::Begin(title.c_str(), &m_IsOpen,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();
        if (!vis) { ImGui::End(); return; }

        if (ImGui::BeginMenuBar())
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.45f, 0.13f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1));
            if (ImGui::Button("  Compilar  ")) CompileScript();
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

        ImGuiID dsId = ImGui::GetID("ScriptEditorDockSpace");

        // Só constrói o layout padrão se o DockSpace não tiver dados salvos no ini
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

            ImGui::DockBuilderDockWindow("Script Preview", dLeft);
            ImGui::DockBuilderDockWindow("Scene Graph", dLeft);
            ImGui::DockBuilderDockWindow("Script Graph", dGraphTop);
            ImGui::DockBuilderDockWindow("Script Console", dConsole);
            ImGui::DockBuilderDockWindow("Script Details", dRight);
            ImGui::DockBuilderFinish(dsId);
        }
        else if (!m_LayoutBuilt)
        {
            m_LayoutBuilt = true; // ini já tem o layout salvo, só marca como construído
        }

        ImGui::DockSpace(dsId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

        DrawPreviewWindow();
        DrawSceneGraphWindow();
        DrawGraphWindow();
        DrawConsoleWindow();
        DrawDetailsWindow();

        ImGui::End();
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

            auto gizmoBtn = [&](const char* lbl, ImGuizmo::OPERATION op) {
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

            // ── Gizmo sobreposto ──────────────────────────────────────────────
            DrawPreviewGizmo();
            HandlePreviewInput();

            // ── Overlays de texto ─────────────────────────────────────────────
            auto* dl = ImGui::GetForegroundDrawList();
            ImVec2 wp = ImGui::GetWindowPos();
            const char* hint = "Alt+LMB orbitar | Scroll zoom | T/R/S gizmo";
            dl->AddText(ImVec2(wp.x + 6, m_PreviewBoundsMax.y - 18),
                ImColor(180, 180, 180, 120), hint);

            const char* lbl = m_ScriptAsset ? m_ScriptAsset->GetName().c_str() :
                m_Component ? m_Component->ScriptName.c_str() : nullptr;
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

        // Usa a drawlist da janela atual (Script Preview) — igual ao viewport_window
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(m_PreviewBoundsMin.x, m_PreviewBoundsMin.y, w, h);

        glm::mat4 view = m_PreviewRenderer->m_Camera->GetViewMatrix();
        glm::mat4 proj = m_PreviewRenderer->m_Camera->GetProjectionMatrix();
        glm::mat4 model = tc->Data.GetMatrix();

        bool used = ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(proj),
            m_GizmoOp, ImGuizmo::LOCAL,
            glm::value_ptr(model));

        if (used)
        {
            float t[3], r[3], s[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), t, r, s);
            tc->Data.Position = { t[0], t[1], t[2] };
            tc->Data.Rotation = { glm::radians(r[0]), glm::radians(r[1]), glm::radians(r[2]) };
            tc->Data.Scale = { std::max(s[0],0.001f), std::max(s[1],0.001f), std::max(s[2],0.001f) };
            tc->Data.UseWorldMatrix = false;
            tc->Data.WorldMatrix = tc->Data.GetMatrix();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawSceneGraphWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (ImGui::Begin("Scene Graph"))
        {
            // ── Cabeçalho ─────────────────────────────────────────────────────
            if (m_ScriptAsset)
            {
                // Tipo de script como badge colorido
                static const struct { ScriptClassType t; ImVec4 col; const char* icon; } badges[] = {
                    { ScriptClassType::Entity,       {0.4f,0.7f,1.f,1},  "Entity"       },
                    { ScriptClassType::Agent,        {0.3f,1.f,0.5f,1},  "Agent"        },
                    { ScriptClassType::Character,    {1.f,0.7f,0.2f,1},  "Character"    },
                    { ScriptClassType::StaticObject, {0.7f,0.7f,0.7f,1}, "StaticObject" },
                    { ScriptClassType::Trigger,      {1.f,0.4f,0.8f,1},  "Trigger"      },
                };
                for (auto& b : badges)
                    if (b.t == m_ScriptAsset->GetClassType())
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, b.col);
                        ImGui::TextUnformatted(b.icon);
                        ImGui::PopStyleColor();
                        break;
                    }
                ImGui::SameLine(0, 6);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.6f, 1));
                ImGui::TextUnformatted(m_ScriptAsset->GetName().c_str());
                ImGui::PopStyleColor();
            }
            else if (m_Component)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.6f, 1));
                ImGui::TextUnformatted(m_Component->ScriptName.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextDisabled("Nenhum script aberto.");
                ImGui::End(); ImGui::PopStyleVar(); return;
            }

            ImGui::Spacing();

            // ── Botão + Adicionar Componente ──────────────────────────────────
            float bw = ImGui::GetContentRegionAvail().x;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.15f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.2f, 1));
            if (ImGui::Button("+ Adicionar Componente", ImVec2(bw, 0)))
                ImGui::OpenPopup("##addcomp");
            ImGui::PopStyleColor(2);

            ImGui::SetNextWindowSize(ImVec2(215, 295), ImGuiCond_Always);
            if (ImGui::BeginPopup("##addcomp"))
            {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::InputTextWithHint("##cs2", "Pesquisar...", m_CompSearchBuf, sizeof(m_CompSearchBuf));
                ImGui::Separator();
                std::string s = m_CompSearchBuf;
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);

                struct CE { const char* name; const char* type; const char* desc; ImVec4 col; };
                static const CE comps[] = {
                    {"Mesh",             "Mesh",                "Malha 3D do objeto",      {0.6f,0.9f,1.f,1}},
                    {"Material",         "Material",            "Material PBR",             {1.f,0.7f,0.4f,1}},
                    {"Rigidbody",        "Rigidbody",           "Fisica dinamica",          {0.3f,0.7f,1.f,1}},
                    {"Collider",         "Collider",            "Colisao (Box/Sphere/Capsule/Mesh)", {0.3f,1.f,0.5f,1}},
                    {"Character Ctrl",   "CharacterController", "Controlador personagem",   {1.f,0.7f,0.2f,1}},
                    {"Point Light",      "PointLight",          "Luz pontual",              {1.f,0.9f,0.3f,1}},
                    {"Camera",           "Camera",              "Camera de jogo",           {0.7f,0.5f,1.f,1}},
                };
                for (auto& c : comps)
                {
                    std::string low = c.name;
                    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                    if (!s.empty() && low.find(s) == std::string::npos) continue;
                    ImGui::PushStyleColor(ImGuiCol_Text, c.col);
                    if (ImGui::MenuItem(c.name))
                    {
                        if (m_ScriptAsset)
                        {
                            ScriptComponentDef def;
                            def.Type = c.type;
                            m_ScriptAsset->AddComponent(def);
                            SyncComponentsToPreview();
                            m_ConsoleLines.push_back(std::string("[Info] Componente adicionado: ") + c.name);
                        }
                        m_CompSearchBuf[0] = '\0'; ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.desc);
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // ── Lista de componentes do asset ─────────────────────────────────
            if (m_ScriptAsset)
            {
                auto& comps = m_ScriptAsset->GetComponents();
                int removeIdx = -1;

                // Transform — sempre presente, sempre arrastável
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1));
                ImGui::Selectable("  Transform", false, ImGuiSelectableFlags_None,
                    ImVec2(ImGui::GetContentRegionAvail().x - 28.f, 0));
                ImGui::PopStyleColor();
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    const char* nodeTypes[] = { "GetTransform","SetTransform","GetPosition","SetPosition" };
                    // Payload: "COMP_NODE:<nodeType>"
                    std::string payload = "GetTransform";
                    ImGui::SetDragDropPayload("COMP_NODE", payload.c_str(), payload.size() + 1);
                    ImGui::TextUnformatted("Transform → Graph");
                    ImGui::EndDragDropSource();
                }

                for (int i = 0; i < (int)comps.size(); i++)
                {
                    auto& def = comps[i];
                    ImVec4 col =
                        def.Type == "Mesh" ? ImVec4(0.6f, 0.9f, 1.f, 1) :
                        def.Type == "Material" ? ImVec4(1.f, 0.7f, 0.4f, 1) :
                        def.Type == "Rigidbody" ? ImVec4(0.3f, 0.7f, 1.f, 1) :
                        def.Type.find("Collider") != std::string::npos ? ImVec4(0.3f, 1.f, 0.5f, 1) :
                        def.Type == "CharacterController" ? ImVec4(1.f, 0.7f, 0.2f, 1) :
                        ImVec4(0.85f, 0.85f, 0.85f, 1);

                    ImGui::PushID(i);

                    // Botão X
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                    if (ImGui::SmallButton("x")) removeIdx = i;
                    ImGui::PopStyleColor(3);

                    ImGui::SameLine(0, 4);

                    // Ícone de drag
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));
                    ImGui::TextUnformatted("=");
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 4);

                    // Nome arrastável
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::Selectable(def.Type.c_str(), false,
                        ImGuiSelectableFlags_None,
                        ImVec2(ImGui::GetContentRegionAvail().x, 0));
                    ImGui::PopStyleColor();

                    // Drag source — arrasta para o graph canvas
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        // Apenas componentes com nodes mapeados são arrastáveis
                        std::string defaultNode;
                        if (def.Type == "Rigidbody")                              defaultNode = "GetRigidbody";
                        else if (def.Type.find("Collider") != std::string::npos)       defaultNode = "GetCollider";
                        else if (def.Type == "CharacterController")                    defaultNode = "GetCharacterController";
                        // Mesh e Material não têm nodes — não iniciam drag
                        if (!defaultNode.empty())
                        {
                            ImGui::SetDragDropPayload("COMP_NODE", defaultNode.c_str(), defaultNode.size() + 1);
                            ImGui::PushStyleColor(ImGuiCol_Text, col);
                            ImGui::Text("Arrastar %s → Graph", def.Type.c_str());
                            ImGui::PopStyleColor();
                            ImGui::TextDisabled("Solte no canvas do graph");
                        }
                        else
                        {
                            ImGui::TextDisabled("%s sem nodes disponíveis", def.Type.c_str());
                        }
                        ImGui::EndDragDropSource();
                    }

                    ImGui::PopID();
                }
                if (removeIdx >= 0)
                {
                    m_ScriptAsset->RemoveComponent(removeIdx);
                    SyncComponentsToPreview();
                    m_ConsoleLines.push_back("[Info] Componente removido.");
                }
            }
            else if (m_SourceRegistry && m_Entity != entt::null && m_SourceRegistry->valid(m_Entity))
            {
                auto& reg = *m_SourceRegistry;
                auto dc = [&](const char* n, ImVec4 col) {
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::BulletText("%s", n); ImGui::PopStyleColor(); };
                if (reg.all_of<TransformComponent>(m_Entity))           dc("Transform", { 0.8f,0.8f,0.8f,1 });
                if (reg.all_of<MeshComponent>(m_Entity))                dc("Mesh", { 0.6f,0.9f,1.f,1 });
                if (reg.all_of<MaterialComponent>(m_Entity))            dc("Material", { 1.f,0.7f,0.4f,1 });
                if (reg.all_of<RigidbodyComponent>(m_Entity))           dc("Rigidbody", { 0.4f,0.8f,1.f,1 });
                if (reg.all_of<ColliderComponent>(m_Entity))            dc("Collider", { 0.4f,1.f,0.6f,1 });
                if (reg.all_of<CharacterControllerComponent>(m_Entity)) dc("CharacterController", { 1.f,0.8f,0.2f,1 });
                if (reg.all_of<LightComponent>(m_Entity))               dc("Light", { 1.f,0.95f,0.4f,1 });
                if (reg.all_of<CameraComponent>(m_Entity))              dc("Camera", { 0.7f,0.5f,1.f,1 });
                if (reg.all_of<ScriptComponent>(m_Entity))              dc("Script", { 1.f,0.5f,0.7f,1 });
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextDisabled("Nodes: %d", m_Graph ? (int)m_Graph->GetNodes().size() : 0);
            ImGui::TextDisabled("Links:  %d", m_Graph ? (int)m_Graph->GetLinks().size() : 0);

            // ── Botão Salvar ──────────────────────────────────────────────────
            if (m_ScriptAsset)
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.35f, 0.55f, 1));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.75f, 1));
                if (ImGui::Button("Salvar", ImVec2(bw, 0)))
                {
                    if (!m_ScriptAsset->GetFilePath().empty())
                    {
                        SaveNodePositions();
                        m_ScriptAsset->Save(m_ScriptAsset->GetFilePath());
                        m_ConsoleLines.push_back("[Info] Script salvo: " +
                            m_ScriptAsset->GetFilePath().string());
                    }
                }
                ImGui::PopStyleColor(2);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Graph window — wrapper dockável
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawGraphWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("Script Graph", nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            DrawNodeGraph();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Node graph — lógica do node editor (separado igual ao Material Editor)
    // Context menu: O POPUP É ABERTO E DESENHADO NO MESMO SUSPEND/RESUME.
    // Isso evita que ed::Resume() seja chamado com s_Editor==nullptr entre frames.
    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawNodeGraph()
    {
        if (!m_Graph) return;

        ed::SetCurrentEditor(m_EdCtx);
        ed::Begin("##SG", ImVec2(0, 0));

        // Primeira frame: restaura posições salvas no JSON
        if (m_FirstFrame && m_Graph)
            for (auto& node : m_Graph->GetNodes())
                if (node->Position.x != 0.f || node->Position.y != 0.f)
                    ed::SetNodePosition(node->ID, node->Position);

        for (auto& n : m_Graph->GetNodes()) DrawNode(n.get());

        for (auto& lk : m_Graph->GetLinks())
        {
            auto* p = m_Graph->FindPin(lk.StartPin);
            ImColor c = p ? GetPinColor(p->Type) : ImColor(255, 255, 255);
            ed::Link(lk.ID, lk.StartPin, lk.EndPin, c, 2.0f);
        }

        if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f))
        {
            ed::PinId sId, eId;
            if (ed::QueryNewLink(&sId, &eId))
            {
                auto* pA = m_Graph->FindPin(sId);
                auto* pB = m_Graph->FindPin(eId);
                if (pA && pB && pA != pB)
                {
                    auto* o = pA->Kind == ed::PinKind::Output ? pA : pB;
                    auto* i = pA->Kind == ed::PinKind::Input ? pA : pB;
                    if (o->Kind != ed::PinKind::Output || i->Kind != ed::PinKind::Input)
                        ed::RejectNewItem(ImColor(255, 0, 0), 2);
                    else if (o->Type != i->Type)
                        ed::RejectNewItem(ImColor(255, 128, 0), 2);
                    else if (ed::AcceptNewItem(GetPinColor(o->Type), 2.5f))
                        m_Graph->AddLink(o->ID, i->ID);
                }
            }
        }
        ed::EndCreate();

        if (ed::BeginDelete())
        {
            ed::LinkId lid;
            while (ed::QueryDeletedLink(&lid))
                if (ed::AcceptDeletedItem()) m_Graph->RemoveLink(lid);
            ed::NodeId nid;
            while (ed::QueryDeletedNode(&nid))
                if (ed::AcceptDeletedItem()) m_Graph->RemoveNode(nid);
        }
        ed::EndDelete();

        // ── Context menu ──────────────────────────────────────────────────────
        // REGRA: OpenPopup e BeginPopup/EndPopup no MESMO bloco Suspend/Resume.
        // O bloco fica "suspenso" entre frames enquanto o popup estiver aberto.
        ImVec2 mousePos = ImGui::GetMousePos();

        ed::Suspend();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

        if (ed::ShowBackgroundContextMenu())
        {
            // Salva posição em canvas para posicionar o node criado
            m_CtxCanvasPos = ed::ScreenToCanvas(mousePos);
            m_CtxBuf[0] = '\0';
            ImGui::OpenPopup("##SGCtx");
        }

        // Popup desenhado aqui — mesmo bloco suspenso, persiste entre frames
        ImGui::SetNextWindowSize(ImVec2(220, 400), ImGuiCond_Always);
        if (ImGui::BeginPopup("##SGCtx"))
        {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##cs", "Pesquisar node...", m_CtxBuf, sizeof(m_CtxBuf));
            ImGui::Separator();

            std::string s = m_CtxBuf;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            bool filtering = !s.empty();

            auto spawnNode = [&](const char* type)
                {
                    if (!m_Graph) return;
                    auto* node = m_Graph->AddNode(type);
                    if (node) ed::SetNodePosition(node->ID, m_CtxCanvasPos);
                    m_CtxBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                };

            // ── Categorias estáticas ──────────────────────────────────────────
            for (int ci = 0; ci < 5; ci++)
            {
                auto& cat = s_Cats[ci];
                ImVec4 col = s_CtxCols[ci];

                bool anyMatch = false;
                if (filtering)
                    for (int i = 0; i < cat.n; i++) {
                        std::string l = cat.e[i].label;
                        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                        if (l.find(s) != std::string::npos) { anyMatch = true; break; }
                    }
                if (filtering && !anyMatch) continue;

                ImGui::PushStyleColor(ImGuiCol_Header,
                    ImVec4(col.x * .32f, col.y * .32f, col.z * .32f, 1));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                    ImVec4(col.x * .52f, col.y * .52f, col.z * .52f, 1));
                bool show = filtering ||
                    ImGui::CollapsingHeader(cat.name,
                        m_CtxOpen[ci] ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                if (!filtering) m_CtxOpen[ci] = show;
                ImGui::PopStyleColor(2);
                if (!filtering && !show) continue;

                ImGui::Indent(6);
                for (int i = 0; i < cat.n; i++)
                {
                    std::string l = cat.e[i].label, low = l;
                    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                    if (filtering && low.find(s) == std::string::npos) continue;
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    if (ImGui::MenuItem(l.c_str())) spawnNode(cat.e[i].type);
                    ImGui::PopStyleColor();
                }
                ImGui::Unindent(6);
            }

            // ── Categoria Componentes — só aparece se o script tiver componentes ──
            if (m_ScriptAsset && !m_ScriptAsset->GetComponents().empty())
            {
                ImVec4 compCol = { 0.6f, 0.85f, 1.0f, 1.f };

                // Verifica se tem match na busca
                bool anyCompMatch = !filtering;
                if (filtering)
                {
                    // Transform sempre existe
                    std::string tl = "transform"; if (tl.find(s) != std::string::npos) anyCompMatch = true;
                    for (auto& def : m_ScriptAsset->GetComponents()) {
                        std::string low = def.Type;
                        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                        if (low.find(s) != std::string::npos) { anyCompMatch = true; break; }
                    }
                }
                if (anyCompMatch)
                {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.1f, 0.25f, 0.4f, 1));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.15f, 0.35f, 0.55f, 1));
                    bool showComp = filtering ||
                        ImGui::CollapsingHeader("Componentes", ImGuiTreeNodeFlags_DefaultOpen);
                    ImGui::PopStyleColor(2);

                    if (showComp)
                    {
                        ImGui::Indent(6);

                        // Helper para desenhar os nodes de um grupo
                        auto drawGroup = [&](const char* groupName, const CompNodeEntry* entries,
                            int count, ImVec4 col)
                            {
                                for (int i = 0; i < count; i++)
                                {
                                    std::string low = entries[i].label;
                                    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                                    if (filtering && low.find(s) == std::string::npos) continue;
                                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                                    if (ImGui::MenuItem(entries[i].label)) spawnNode(entries[i].type);
                                    ImGui::PopStyleColor();
                                }
                            };

                        // Transform sempre disponível
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 0.6f));
                        ImGui::TextUnformatted("-- Transform --");
                        ImGui::PopStyleColor();
                        drawGroup("Transform", s_TransformNodes, 4, { 0.75f,0.75f,0.75f,1 });

                        // Componentes do asset
                        for (auto& def : m_ScriptAsset->GetComponents())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));
                            ImGui::Text("-- %s --", def.Type.c_str());
                            ImGui::PopStyleColor();

                            if (def.Type == "Rigidbody")
                                drawGroup("Rigidbody", s_RigidbodyNodes, 3, { 0.3f,0.8f,1.f,1 });
                            else if (def.Type.find("Collider") != std::string::npos)
                                drawGroup("Collider", s_ColliderNodes, 2, { 0.3f,1.f,0.5f,1 });
                            else if (def.Type == "CharacterController")
                                drawGroup("CharacterController", s_CCNodes, 3, { 1.f,0.7f,0.2f,1 });
                        }
                        ImGui::Unindent(6);
                    }
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar();
        ed::Resume();
        // ── Fim context menu ──────────────────────────────────────────────────

        if (m_FirstFrame) { ed::NavigateToContent(); m_FirstFrame = false; }
        ed::End();
        ed::SetCurrentEditor(nullptr);

        // ── Drop target no canvas do graph ────────────────────────────────────
        // O InvisibleButton cobre toda a área e aceita o drop do componente
        ImVec2 canvasMin = ImGui::GetWindowPos();
        ImVec2 canvasSize = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(canvasMin);
        ImGui::InvisibleButton("##graph_drop_target", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft);

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMP_NODE"))
            {
                std::string nodeType = (const char*)payload->Data;
                if (m_Graph)
                {
                    // Posição central do canvas como posição do node
                    ed::SetCurrentEditor(m_EdCtx);
                    ImVec2 dropPos = ImGui::GetMousePos();
                    ImVec2 canvasPos = ed::ScreenToCanvas(dropPos);
                    auto* node = m_Graph->AddNode(nodeType.c_str());
                    if (node)
                    {
                        ed::SetNodePosition(node->ID, canvasPos);
                        m_ConsoleLines.push_back("[Info] Node criado: " + nodeType);
                    }
                    ed::SetCurrentEditor(nullptr);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawNode(ScriptNode* node)
    {
        if (!node) return;

        float inW = 0, outW = 0;
        for (auto& p : node->Inputs)  inW = std::max(inW, ImGui::CalcTextSize(p.Name.c_str()).x);
        for (auto& p : node->Outputs) outW = std::max(outW, ImGui::CalcTextSize(p.Name.c_str()).x);
        float titleW = ImGui::CalcTextSize(node->Name.c_str()).x + 16.0f;
        float nodeW = std::max(titleW, inW + outW + ICON_SZ * 2 + 24.0f);
        nodeW = std::max(nodeW, 160.0f);

        ImVec4 hcol = HdrCol(node->Category);
        ImColor hc(hcol.x, hcol.y, hcol.z, 1.f);
        ImColor hcDark(hcol.x * .55f, hcol.y * .55f, hcol.z * .55f, 1.f);

        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(32, 32, 32, 240));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(55, 55, 55, 200));
        ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
        ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 4));
        ed::BeginNode(node->ID);

        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float  hh = ImGui::GetTextLineHeight() + 10.0f;
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + nodeW, p.y + hh), hc, 6.f, ImDrawFlags_RoundCornersTop);
            dl->AddRectFilled(ImVec2(p.x, p.y + hh - 3), ImVec2(p.x + nodeW, p.y + hh), hcDark, 0);
            float tw = ImGui::CalcTextSize(node->Name.c_str()).x;
            ImGui::SetCursorScreenPos(ImVec2(p.x + (nodeW - tw) * .5f, p.y + 5));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            ImGui::TextUnformatted(node->Name.c_str());
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + hh));
            ImGui::Dummy(ImVec2(nodeW, 4));
        }

        int maxP = (int)std::max(node->Inputs.size(), node->Outputs.size());
        for (int i = 0; i < maxP; i++)
        {
            bool hasIn = i < (int)node->Inputs.size();
            bool hasOut = i < (int)node->Outputs.size();
            float rowY = ImGui::GetCursorPosY();

            if (hasIn)
            {
                auto& pin = node->Inputs[i];
                ImGui::SetCursorPosY(rowY);
                ed::BeginPin(pin.ID, ed::PinKind::Input);
                ax::Widgets::Icon(ImVec2(ICON_SZ, ICON_SZ), PinIcon(pin.Type),
                    m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), { 0.1f,0.1f,0.1f,0.8f });
                ed::EndPin();
                ImGui::SameLine(0, 3);
                ImGui::SetCursorPosY(rowY + (ICON_SZ - ImGui::GetTextLineHeight()) * .5f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.88f, 1));
                ImGui::TextUnformatted(pin.Name.c_str());
                ImGui::PopStyleColor();
            }
            if (hasOut)
            {
                auto& pin = node->Outputs[i];
                float textW = ImGui::CalcTextSize(pin.Name.c_str()).x;
                float totW = textW + 3.f + ICON_SZ;
                ImGui::SameLine(nodeW - totW - 2.f);
                ImGui::SetCursorPosY(rowY + (ICON_SZ - ImGui::GetTextLineHeight()) * .5f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.88f, 1));
                ImGui::TextUnformatted(pin.Name.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 3);
                ImGui::SetCursorPosY(rowY);
                ed::BeginPin(pin.ID, ed::PinKind::Output);
                ax::Widgets::Icon(ImVec2(ICON_SZ, ICON_SZ), PinIcon(pin.Type),
                    m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), { 0.1f,0.1f,0.1f,0.8f });
                ed::EndPin();
            }
            ImGui::SetCursorPosY(rowY + PIN_H);
            ImGui::Dummy(ImVec2(nodeW, 0));
        }
        ImGui::Dummy(ImVec2(nodeW, 2));
        ed::EndNode();
        ed::PopStyleVar(3);
        ed::PopStyleColor(2);
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
    void ScriptGraphWindow::DrawDetailsWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (ImGui::Begin("Script Details"))
        {
            DrawScriptDetails();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ScriptGraphWindow::DrawScriptDetails()
    {
        if (ImGui::BeginTabBar("##detailstabs"))
        {
            // ── Tab: OBJETO ───────────────────────────────────────────────────
            if (ImGui::BeginTabItem("Objeto"))
            {
                auto& reg = m_PreviewScene->GetRegistry();

                // ── Transform (igual ao Inspector) ────────────────────────────
                ImGui::Text("Transform");
                auto* tc = reg.try_get<TransformComponent>(m_PreviewEntity);
                if (tc)
                {
                    bool changed = false;
                    if (ImGui::DragFloat3("Position", &tc->Data.Position.x, 0.1f)) changed = true;
                    glm::vec3 rotDeg = glm::degrees(tc->Data.Rotation);
                    if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotDeg), 0.5f))
                    {
                        tc->Data.Rotation = glm::radians(rotDeg); changed = true;
                    }
                    glm::vec3 scaleCopy = tc->Data.Scale;
                    if (ImGui::DragFloat3("Scale", &scaleCopy.x, 0.05f))
                    {
                        tc->Data.Scale.x = std::max(scaleCopy.x, 0.001f);
                        tc->Data.Scale.y = std::max(scaleCopy.y, 0.001f);
                        tc->Data.Scale.z = std::max(scaleCopy.z, 0.001f);
                        changed = true;
                    }
                    if (changed) { tc->Data.UseWorldMatrix = false; tc->Data.WorldMatrix = tc->Data.GetMatrix(); }
                }

                // ── Componentes do ScriptAsset ────────────────────────────────
                if (m_ScriptAsset)
                {
                    auto& comps = m_ScriptAsset->GetComponents();
                    for (int i = 0; i < (int)comps.size(); i++)
                    {
                        auto& def = comps[i];
                        ImGui::PushID(i);
                        ImGui::Separator();
                        ImGui::Spacing();

                        float available = ImGui::GetContentRegionAvail().x;

                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.25f, 1));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.32f, 1));
                        bool open = ImGui::CollapsingHeader(
                            (def.Type + "##hdr_" + std::to_string(i)).c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
                        ImGui::PopStyleColor(2);

                        // Botão X (igual ao Inspector)
                        ImGui::SameLine(available - 18.f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1));
                        bool doRemove = ImGui::SmallButton(("x##x_" + std::to_string(i)).c_str());
                        ImGui::PopStyleColor(3);

                        if (doRemove)
                        {
                            m_ScriptAsset->RemoveComponent(i);
                            SyncComponentsToPreview();
                            m_ConsoleLines.push_back("[Info] Componente removido.");
                            ImGui::PopID();
                            break; // evita invalidar o iterador
                        }

                        if (open)
                        {
                            // ── MESH ──────────────────────────────────────────
                            if (def.Type == "Mesh")
                            {
                                static const char* primitives[] = { "Sphere","Cube","Cylinder","Plane" };
                                static const char* primUUIDs[] = {
                                    PrimitiveUUID::Sphere, PrimitiveUUID::Cube,
                                    PrimitiveUUID::Cylinder, PrimitiveUUID::Plane };
                                int curIdx = 0;
                                for (int p = 0; p < 4; p++)
                                    if (def.AssetUUID == primUUIDs[p]) { curIdx = p; break; }
                                if (ImGui::Combo("Primitiva", &curIdx, primitives, 4))
                                {
                                    def.AssetUUID = primUUIDs[curIdx];
                                    auto& mc = reg.get_or_emplace<MeshComponent>(m_PreviewEntity);
                                    mc.Data = MeshFactory::CreateByUUID(def.AssetUUID);
                                }
                                // Drop de mesh do Asset Browser
                                if (ImGui::BeginDragDropTarget())
                                {
                                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_UUID"))
                                    {
                                        std::string uuid = (const char*)p->Data;
                                        const auto* rec = AssetDatabase::Get().GetByUUID(uuid);
                                        if (rec && rec->Type == AssetType::Mesh)
                                        {
                                            def.AssetUUID = uuid;
                                            auto& mc = reg.get_or_emplace<MeshComponent>(m_PreviewEntity);
                                            // Carrega mesh pelo asset database
                                            mc.AssetUUID = uuid;
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                            }
                            // ── MATERIAL ──────────────────────────────────────
                            else if (def.Type == "Material")
                            {
                                // Usa AssetPicker igual ao Inspector
                                AssetPicker::Draw("Material", def.AssetUUID,
                                    { AssetType::Material },
                                    [&](const AssetRecord& record)
                                    {
                                        def.AssetUUID = record.UUID;
                                        auto matAsset = MaterialAsset::LoadFromFile(record.FilePath);
                                        if (matAsset)
                                        {
                                            // Compila shader igual ao Material Editor
                                            auto graphPath = record.FilePath;
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
                                                    }
                                                }
                                                catch (...) {}
                                            }
                                            auto& mc = reg.get_or_emplace<MaterialComponent>(m_PreviewEntity);
                                            mc.Data = matAsset->GetMaterial();
                                            mc.MaterialAssetUUID = record.UUID;
                                        }
                                    });

                                // Usa DrawMaterialGraphParams do Inspector — edita e salva
                                // diretamente no .axegraph, igual ao Inspector
                                if (m_InspectorWindow && !def.AssetUUID.empty())
                                {
                                    auto& previewReg = m_PreviewScene->GetRegistry();
                                    m_InspectorWindow->DrawMaterialGraphParams(
                                        def.AssetUUID, previewReg, m_PreviewEntity);
                                }
                                else if (!def.AssetUUID.empty())
                                {
                                    // Fallback sem inspector: mostra os valores em memória
                                    auto* mc = reg.try_get<MaterialComponent>(m_PreviewEntity);
                                    if (mc && mc->Data)
                                    {
                                        auto& mat = *mc->Data;
                                        ImGui::DragFloat("Metallic", &mat.Metallic, 0.01f, 0.f, 1.f);
                                        ImGui::DragFloat("Roughness", &mat.Roughness, 0.01f, 0.f, 1.f);
                                        ImGui::ColorEdit3("Cor Base", glm::value_ptr(mat.Color));
                                    }
                                }
                            }
                            // ── RIGIDBODY ─────────────────────────────────────
                            else if (def.Type == "Rigidbody")
                            {
                                static const char* types[] = { "Static","Dynamic","Kinematic" };
                                int typeIdx = (def.BodyType == "Dynamic") ? 1 :
                                    (def.BodyType == "Kinematic") ? 2 : 0;
                                if (ImGui::Combo("Tipo", &typeIdx, types, 3))
                                    def.BodyType = types[typeIdx];

                                if (def.BodyType != "Static")
                                {
                                    ImGui::DragFloat("Massa", &def.Mass, 0.1f, 0.01f, 1000.f);
                                    ImGui::DragFloat("Friccao", &def.Friction, 0.01f, 0.f, 1.f);
                                    ImGui::DragFloat("Restitution", &def.Restitution, 0.01f, 0.f, 1.f);
                                    ImGui::DragFloat("Linear Damp", &def.LinearDamping, 0.01f, 0.f, 1.f);
                                    ImGui::DragFloat("Angular Damp", &def.AngularDamping, 0.01f, 0.f, 1.f);
                                    ImGui::Checkbox("Gravidade", &def.UseGravity);
                                    ImGui::TextDisabled("Travar Rotacao:");
                                    ImGui::SameLine(); ImGui::Checkbox("X##lrx", &def.LockRotX);
                                    ImGui::SameLine(); ImGui::Checkbox("Y##lry", &def.LockRotY);
                                    ImGui::SameLine(); ImGui::Checkbox("Z##lrz", &def.LockRotZ);
                                }
                            }
                            // ── COLLIDER ──────────────────────────────────────
                            else if (def.Type.find("Collider") != std::string::npos ||
                                def.Type == "ColliderSphere" ||
                                def.Type == "ColliderCapsule" ||
                                def.Type == "ColliderMesh")
                            {
                                static const char* shapes[] =
                                { "Box","Sphere","Capsule","Mesh (Static)","Convex Hull" };
                                static const char* shapeKeys[] =
                                { "Box","Sphere","Capsule","Mesh","ConvexHull" };
                                int cur = 0;
                                for (int s = 0; s < 5; s++)
                                    if (def.ColliderShape == shapeKeys[s]) { cur = s; break; }
                                if (ImGui::Combo("Forma", &cur, shapes, 5))
                                    def.ColliderShape = shapeKeys[cur];

                                ImGui::Checkbox("Is Trigger", &def.IsTrigger);
                                ImGui::Checkbox("Debug Wireframe", &def.ShowDebug);
                                ImGui::DragFloat3("Offset", &def.ColliderOffsetX, 0.01f);

                                if (def.ColliderShape == "Box")
                                    ImGui::DragFloat3("Half Extent", &def.ColliderSizeX, 0.01f, 0.01f, 100.f);
                                else if (def.ColliderShape == "Sphere")
                                    ImGui::DragFloat("Raio", &def.ColliderRadius, 0.01f, 0.01f, 100.f);
                                else if (def.ColliderShape == "Capsule")
                                {
                                    ImGui::DragFloat("Altura", &def.ColliderHeight, 0.01f, 0.1f, 10.f);
                                    ImGui::DragFloat("Raio Capsule", &def.ColliderCapsuleRadius, 0.01f, 0.01f, 5.f);
                                }
                                else if (def.ColliderShape == "Mesh")
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.1f, 1));
                                    ImGui::TextWrapped("Mesh exato. Use apenas com Rigidbody Static.");
                                    ImGui::PopStyleColor();
                                }
                            }
                            // ── CHARACTER CONTROLLER ──────────────────────────
                            else if (def.Type == "CharacterController")
                            {
                                ImGui::DragFloat("Altura", &def.CCHeight, 0.01f, 0.5f, 5.f);
                                ImGui::DragFloat("Raio", &def.CCRadius, 0.01f, 0.1f, 2.f);
                                ImGui::DragFloat("Max Slope", &def.CCMaxSlope, 0.5f, 0.f, 89.f);
                                ImGui::DragFloat("Step Height", &def.CCStepHeight, 0.01f, 0.f, 1.f);
                                ImGui::DragFloat("Max Speed", &def.CCMaxSpeed, 0.1f, 0.f, 50.f);
                                ImGui::DragFloat("Jump Force", &def.CCJumpForce, 0.1f, 0.f, 50.f);
                            }
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::EndTabItem();
            }

            // ── Tab: NODE ─────────────────────────────────────────────────────
            if (ImGui::BeginTabItem("Node"))
            {
                if (m_EdCtx)
                {
                    ed::SetCurrentEditor(m_EdCtx);
                    int sel = ed::GetSelectedObjectCount();
                    ed::NodeId selNode; int got = 0;
                    if (sel > 0) got = ed::GetSelectedNodes(&selNode, 1);
                    ed::SetCurrentEditor(nullptr);

                    if (sel == 0 || got == 0)
                    {
                        ImGui::TextDisabled("Selecione um node no graph.");
                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
                        ImGui::TextWrapped("Clique direito no graph para adicionar nodes");
                        ImGui::TextWrapped("Arraste pins para conectar");
                        ImGui::TextWrapped("Delete para remover selecionados");
                        ImGui::PopStyleColor();
                    }
                    else if (m_Graph)
                    {
                        auto* node = m_Graph->FindNode(selNode);
                        if (node)
                        {
                            ImColor hc = GetNodeHeaderColor(node->Category);
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                ImVec4(hc.Value.x, hc.Value.y, hc.Value.z, 1));
                            ImGui::TextUnformatted(node->Name.c_str());
                            ImGui::PopStyleColor();
                            ImGui::Separator(); ImGui::Spacing();

                            if (node->Name == "Get Key" || node->Name == "Get Axis" ||
                                node->Name == "Get Variable" || node->Name == "Set Variable" ||
                                node->Name == "Print String")
                            {
                                const char* lbl =
                                    node->Name == "Get Key" ? "Tecla (ex: W):" :
                                    node->Name == "Get Axis" ? "Eixo (ex: Horizontal):" :
                                    node->Name == "Print String" ? "Mensagem:" : "Variavel:";
                                ImGui::TextDisabled("%s", lbl);
                                ImGui::SetNextItemWidth(-1);
                                char buf[128];
                                std::strncpy(buf, node->StringValue.c_str(), sizeof(buf));
                                buf[sizeof(buf) - 1] = '\0';
                                if (ImGui::InputText("##sv", buf, sizeof(buf)))
                                    node->StringValue = buf;
                                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                            }
                            for (auto& p : node->Inputs)
                            {
                                ImColor pc = GetPinColor(p.Type);
                                ImGui::PushStyleColor(ImGuiCol_Text,
                                    ImVec4(pc.Value.x, pc.Value.y, pc.Value.z, 1));
                                ImGui::BulletText("In: %s", p.Name.c_str());
                                ImGui::PopStyleColor();
                            }
                            for (auto& p : node->Outputs)
                            {
                                ImColor pc = GetPinColor(p.Type);
                                ImGui::PushStyleColor(ImGuiCol_Text,
                                    ImVec4(pc.Value.x, pc.Value.y, pc.Value.z, 1));
                                ImGui::BulletText("Out: %s", p.Name.c_str());
                                ImGui::PopStyleColor();
                            }
                        }
                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void ScriptGraphWindow::HandlePreviewInput()
    {
        if (!m_PreviewHovered || !m_PreviewRenderer) return;
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mousePos = ImGui::GetMousePos();
        static ImVec2 lastMouse = mousePos;
        m_PreviewMouseDelta = ImVec2(mousePos.x - lastMouse.x, mousePos.y - lastMouse.y);
        lastMouse = mousePos;
        const bool alt = io.KeyAlt;
        if (!alt && io.MouseWheel == 0.0f) return;
        glm::vec2 delta(m_PreviewMouseDelta.x, m_PreviewMouseDelta.y);
        delta *= 0.003f;
        if (alt)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))   m_PreviewRenderer->OnMouseRotate(delta);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_PreviewRenderer->OnMousePan(delta);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))  m_PreviewRenderer->OnMouseZoom(delta.y * 10.f);
        }
        if (io.MouseWheel != 0.0f) m_PreviewRenderer->OnMouseZoom(io.MouseWheel);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::SaveNodePositions()
    {
        // Lê posições atuais do node editor e salva no ScriptNode antes de serializar
        if (!m_Graph || !m_EdCtx) return;
        ed::SetCurrentEditor(m_EdCtx);
        for (auto& node : m_Graph->GetNodes())
            node->Position = ed::GetNodePosition(node->ID);
        ed::SetCurrentEditor(nullptr);
    }

    void ScriptGraphWindow::CompileScript()
    {
        // Salva posições e arquivo antes de compilar
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

        // ── Resolve paths absolutos a partir do executável ────────────────────
        // editor.exe fica em:  bin/<config>/editor/editor.exe
        // Subindo 3 níveis chegamos na raiz do projeto onde está /src
        char exeBuf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
        std::filesystem::path root = (exeDir / ".." / ".." / "..").lexically_normal();
        std::filesystem::path vendor = root / "src" / "vendor";

        // Todos os include paths que o script compilado precisa
        // (mirrors dos includedirs do premake5.lua para o projeto axe)
        std::vector<std::filesystem::path> includes = {
            root / "src",                       // axe/script/script_base.hpp, etc.
            vendor / "glm",                     // glm/glm.hpp
            vendor / "entt" / "src",            // entt/entt.hpp
            vendor / "spdlog" / "include",      // spdlog/spdlog.h
            vendor / "spdlog",                  // fallback caso inclua sem /include
        };

        // axe.lib — gerado na mesma config/plataforma que o editor
        // bin/<config>/axe/axe.lib  (targetdir do projeto axe no premake)
        std::filesystem::path axeLib = exeDir / ".." / "axe" / "axe.lib";
        axeLib = axeLib.lexically_normal();

        // temp_scripts ao lado do executável
        std::filesystem::path dir = exeDir / "temp_scripts";
        std::filesystem::create_directories(dir);

        auto cpp = (dir / (scriptName + ".cpp")).string();
        auto dll = (dir / (scriptName + ".dll")).string();

        m_ConsoleLines.push_back("[Script Editor] Gerando C++...");
        std::string code = ScriptGraphCompiler::Generate(*m_Graph, scriptName);
        std::ofstream f(cpp); f << code; f.close();
        m_ConsoleLines.push_back("[Script Editor] .cpp salvo: " + cpp);

        // Monta string de includes para passar ao ScriptCompiler
        // Formato: path1;path2;path3  (separados por ;)
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