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
    static const NE sCast[] = {
        {"To Float",     "ToFloat"    },
        {"To Int",       "ToInt"      },
        {"To Bool",      "ToBool"     },
        {"To String",    "ToString"   },
        {"To Vec3",      "ToVec3"     },
        {"Break Vec3",   "BreakVec3"  },
        {"Float to Vec3","FloatToVec3"},
        {nullptr, nullptr}
    };

    struct CatDef { const char* name; const NE* e; int n; ImVec4 col; };
    static const CatDef s_Cats[] = {
        {"Events",  sEv,   5, {0.85f,0.3f,0.2f, 1}},
        {"Actions", sAc,   5, {0.2f,0.7f,0.45f, 1}},
        {"Logic",   sLo,   4, {0.8f,0.6f,0.1f,  1}},
        {"Math",    sMa,   3, {0.3f,0.5f,0.9f,  1}},
        {"Input",   sIn,   2, {0.7f,0.2f,0.6f,  1}},
        {"Cast",    sCast, 7, {0.5f,0.8f,0.8f,  1}},
    };
    static const ImVec4 s_CtxCols[] = {
        {1.f,0.45f,0.35f,1},{0.3f,0.85f,0.55f,1},
        {1.f,0.78f,0.2f,1},{0.4f,0.65f,1.f,1},{0.85f,0.3f,0.75f,1},
        {0.5f,0.9f,0.9f,1} };

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
    static const CompNodeEntry s_SpringArmNodes[] = {
        {"Get Spring Arm", "GetSpringArm"},
        {"Set Spring Arm", "SetSpringArm"},
    };
    static const CompNodeEntry s_CameraNodes[] = {
        {"Get Camera",     "GetCamera"},
        {"Set Camera FOV", "SetCameraFOV"},
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
        m_CameraPreviewEntity = entt::null;
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
            // ── SpringArm — sincroniza exceto durante drag do gizmo ──────
            if (def.Type == "SpringArm" && !m_SpringArmDragging)
            {
                auto& saComp = reg.get_or_emplace<SpringArmComponent>(m_PreviewEntity);
                saComp.Length = def.SALength / 100.0f;
                saComp.HeightOffset = def.SAHeightOffset;
                saComp.SocketOffset = { def.SASocketOffX, def.SASocketOffY, def.SASocketOffZ };
                saComp.LagSpeed = def.SALagSpeed;
                saComp.EnableCameraLag = def.SAEnableLag;
                saComp.MouseRotates = def.SAMouseRotates;
            }

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

        if (ImGui::BeginMenuBar())
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.45f, 0.13f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1));
            if (ImGui::Button("  Compilar  ")) CompileScript();
            ImGui::SameLine(0, 8);
            ImGui::BeginDisabled(!CanUndo());
            if (ImGui::Button("  Undo  ") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
                Undo();
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(!CanRedo());
            if (ImGui::Button("  Redo  "))
                Redo();
            ImGui::EndDisabled();
            if (CanUndo()) { ImGui::SameLine(0, 8); ImGui::TextDisabled("%s", m_History.GetUndoName().c_str()); }
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
            // Script Members ocupa a metade inferior do painel direito
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
            m_LayoutBuilt = true; // ini já tem o layout salvo, só marca como construído
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

        // Gizmo target: se SpringArm ou Camera selecionado → mesh da câmera
        // caso contrário → pawn
        bool cameraSelected = false;
        if (m_ScriptAsset && m_SelectedCompIndex >= 0 &&
            m_SelectedCompIndex < (int)m_ScriptAsset->GetComponents().size())
        {
            auto& selDef = m_ScriptAsset->GetComponents()[m_SelectedCompIndex];
            cameraSelected = (selDef.Type == "SpringArm" || selDef.Type == "Camera");
        }

        // Garante que a câmera entity existe se há SpringArm
        auto* sa = reg.try_get<SpringArmComponent>(m_PreviewEntity);
        if (sa && (m_CameraPreviewEntity == entt::null || !reg.valid(m_CameraPreviewEntity)))
        {
            m_CameraPreviewEntity = m_PreviewScene->CreateEntity("CameraPreviewMesh");
            auto& mc = reg.get_or_emplace<MeshComponent>(m_CameraPreviewEntity);
            mc.Data = MeshFactory::CreateCamera();
        }

        TransformComponent* camTc = (m_CameraPreviewEntity != entt::null && reg.valid(m_CameraPreviewEntity))
            ? reg.try_get<TransformComponent>(m_CameraPreviewEntity) : nullptr;

        // Gizmo target
        auto* gizmoTc = (cameraSelected && camTc) ? camTc : tc;

        // Atualiza posição do mesh câmera baseado no SpringArm (quando não está em drag)
        if (sa && camTc && !m_SpringArmDragging)
        {
            camTc->Data.Position = tc->Data.Position + glm::vec3(
                sa->SocketOffset.x,
                sa->HeightOffset + sa->SocketOffset.y,
                sa->Length + sa->SocketOffset.z);
            camTc->Data.Scale = glm::vec3(0.35f);
            // Câmera aponta para o pawn
            glm::vec3 dir = camTc->Data.Position - tc->Data.Position;
            if (glm::length(dir) > 0.001f)
            {
                dir = glm::normalize(dir);
                camTc->Data.Rotation.y = glm::atan(dir.x, dir.z);
                camTc->Data.Rotation.x = glm::asin(-dir.y);
            }
        }

        glm::mat4 model = gizmoTc->Data.GetMatrix();

        // Gizmo só ativo no pawn — SpringArm/Camera editados via Script Details
        if (!cameraSelected)
        {
            bool used = ImGuizmo::Manipulate(
                glm::value_ptr(view), glm::value_ptr(proj),
                m_GizmoOp, ImGuizmo::LOCAL,
                glm::value_ptr(model));

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

        // ── Spring Arm Gizmo ──────────────────────────────────────────────────
        // Desenha uma linha do pawn até a posição da câmera e um manipulador
        // que permite arrastar o endpoint da câmera para reposicionar o SpringArm
        auto* sa2D = reg.try_get<SpringArmComponent>(m_PreviewEntity);
        if (sa2D && m_ScriptAsset)
        {
            // Posição do pawn
            glm::vec3 pawnPos = tc->Data.Position;

            // Posição da câmera = pawn + offset vertical + trás no eixo Z local
            // (simplificado — assume câmera atrás no eixo Z)
            glm::vec3 camOffset = {
                sa2D->SocketOffset.x,
                sa2D->HeightOffset + sa2D->SocketOffset.y,
                sa2D->Length + sa2D->SocketOffset.z
            };
            glm::vec3 camPos = pawnPos + camOffset;

            // Projeta as duas posições para screen space para desenhar a linha
            auto worldToScreen = [&](const glm::vec3& world) -> ImVec2 {
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

            // Linha do braço — roxo/lilás
            dl->AddLine(pawnScreen, camScreen, IM_COL32(180, 130, 255, 200), 1.5f);

            // Ponto do pawn (origem)
            dl->AddCircleFilled(pawnScreen, 4.f, IM_COL32(200, 150, 255, 230));

            // Ponto da câmera (endpoint arrastável)
            bool hovered = ImGui::IsMouseHoveringRect(
                ImVec2(camScreen.x - 8, camScreen.y - 8),
                ImVec2(camScreen.x + 8, camScreen.y + 8));
            dl->AddCircleFilled(camScreen, hovered ? 8.f : 6.f,
                hovered ? IM_COL32(255, 200, 100, 255) : IM_COL32(255, 180, 80, 200));
            dl->AddCircle(camScreen, hovered ? 8.f : 6.f, IM_COL32(255, 255, 255, 180), 12, 1.f);

            // Label
            dl->AddText(ImVec2(camScreen.x + 10, camScreen.y - 8),
                IM_COL32(220, 200, 255, 220), "Camera");

            // Frustum da câmera — triângulo simples representando o FOV
            // Busca o FOV do CameraComponent no preview
            float camFov = 60.f;
            auto* camComp = reg.try_get<CameraComponent>(m_PreviewEntity);
            if (camComp) camFov = camComp->Fov;
            float halfFovRad = glm::radians(camFov * 0.5f);
            float frustumLen = 0.8f; // comprimento visual do frustum em unidades de mundo
            float frustumHalf = std::tan(halfFovRad) * frustumLen;

            // Direção da câmera: apontando para o pawn (inverso do arm)
            glm::vec3 camDir = glm::normalize(pawnPos - camPos);
            glm::vec3 camRight = glm::normalize(glm::cross(camDir, glm::vec3(0, 1, 0)));
            glm::vec3 camUp = glm::normalize(glm::cross(camRight, camDir));

            glm::vec3 ftl = camPos + camDir * frustumLen + camUp * frustumHalf - camRight * frustumHalf;
            glm::vec3 ftr = camPos + camDir * frustumLen + camUp * frustumHalf + camRight * frustumHalf;
            glm::vec3 fbl = camPos + camDir * frustumLen - camUp * frustumHalf - camRight * frustumHalf;
            glm::vec3 fbr = camPos + camDir * frustumLen - camUp * frustumHalf + camRight * frustumHalf;

            ImVec2 sFtl = worldToScreen(ftl), sFtr = worldToScreen(ftr);
            ImVec2 sFbl = worldToScreen(fbl), sFbr = worldToScreen(fbr);
            ImU32 frustumCol = IM_COL32(140, 200, 255, 220);
            ImU32 frustumCol2 = IM_COL32(180, 220, 255, 180);
            // 4 linhas do apex da câmera até os cantos
            dl->AddLine(camScreen, sFtl, frustumCol, 2.f);
            dl->AddLine(camScreen, sFtr, frustumCol, 2.f);
            dl->AddLine(camScreen, sFbl, frustumCol, 2.f);
            dl->AddLine(camScreen, sFbr, frustumCol, 2.f);
            // Retângulo do near plane
            dl->AddLine(sFtl, sFtr, frustumCol2, 2.f);
            dl->AddLine(sFtr, sFbr, frustumCol2, 2.f);
            dl->AddLine(sFbr, sFbl, frustumCol2, 2.f);
            dl->AddLine(sFbl, sFtl, frustumCol2, 2.f);

            // Arraste do endpoint da câmera
            if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

                // Converte delta de pixels para unidades de mundo (aproximado)
                float pixelsPerUnit = h / (2.0f * std::tan(glm::radians(45.0f / 2.0f)) * 5.0f);
                if (pixelsPerUnit > 0.001f)
                {
                    sa2D->Length -= delta.y / pixelsPerUnit;  // Y do mouse → comprimento
                    sa2D->HeightOffset += delta.y / pixelsPerUnit * 0.3f;

                    sa2D->Length = std::max(sa2D->Length, 1.0f);

                    // Sincroniza de volta para o def
                    if (m_ScriptAsset)
                    {
                        for (auto& d : m_ScriptAsset->GetComponents())
                        {
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
                ImGui::TextDisabled("No script open.");
                ImGui::End(); ImGui::PopStyleVar(); return;
            }

            ImGui::Spacing();

            // ── Botão + Adicionar Componente ──────────────────────────────────
            float bw = ImGui::GetContentRegionAvail().x;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.15f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.2f, 1));
            if (ImGui::Button("+ Add Component", ImVec2(bw, 0)))
                ImGui::OpenPopup("##addcomp");
            ImGui::PopStyleColor(2);

            ImGui::SetNextWindowSize(ImVec2(215, 295), ImGuiCond_Always);
            if (ImGui::BeginPopup("##addcomp"))
            {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::InputTextWithHint("##cs2", "Search...", m_CompSearchBuf, sizeof(m_CompSearchBuf));
                ImGui::Separator();
                std::string s = m_CompSearchBuf;
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);

                struct CE { const char* name; const char* type; const char* desc; ImVec4 col; };
                static const CE comps[] = {
                    {"Mesh",             "Mesh",                "Malha 3D do objeto",         {0.6f,0.9f,1.f,1}},
                    {"Material",         "Material",            "Material PBR",                {1.f,0.7f,0.4f,1}},
                    {"Rigidbody",        "Rigidbody",           "Fisica dinamica",             {0.3f,0.7f,1.f,1}},
                    {"Collider",         "Collider",            "Colisao (Box/Sphere/Capsule)", {0.3f,1.f,0.5f,1}},
                    {"Character Ctrl",   "CharacterController", "Character controller",      {1.f,0.7f,0.2f,1}},
                    {"Point Light",      "PointLight",          "Luz pontual",                 {1.f,0.9f,0.3f,1}},
                    {"Spring Arm",       "SpringArm",           "Camera boom arm",      {0.9f,0.6f,1.f,1}},
                    {"Camera",           "Camera",              "Camera de jogo",              {0.7f,0.5f,1.f,1}},
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
                            PushUndo("Add Component");
                            m_ScriptAsset->AddComponent(def);
                            CommitUndo("Add Component");
                            SyncComponentsToPreview();
                            m_ConsoleLines.push_back(std::string("[Info] Component added: ") + c.name);
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

                // ── Árvore hierárquica de componentes ────────────────────────
                // Primeiro renderiza os que NÃO têm pai (raiz)
                // Depois renderiza filhos com indentação
                auto getColor = [](const std::string& t) -> ImVec4 {
                    if (t == "Mesh")              return { 0.6f,0.9f,1.f,1 };
                    if (t == "Material")          return { 1.f,0.7f,0.4f,1 };
                    if (t == "Rigidbody")         return { 0.3f,0.7f,1.f,1 };
                    if (t.find("Collider") != std::string::npos) return { 0.3f,1.f,0.5f,1 };
                    if (t == "CharacterController") return { 1.f,0.7f,0.2f,1 };
                    if (t == "SpringArm")         return { 0.9f,0.6f,1.f,1 };
                    if (t == "Camera")            return { 0.7f,0.5f,1.f,1 };
                    return { 0.85f,0.85f,0.85f,1 };
                    };

                // Icon per component type (Unicode symbols as ASCII-safe fallbacks)
                auto getIcon = [](const std::string& t) -> const char* {
                    if (t == "Mesh")               return "[M]";
                    if (t == "Material")           return "[~]";
                    if (t == "Rigidbody")          return "[R]";
                    if (t.find("Collider") != std::string::npos) return "[C]";
                    if (t == "CharacterController")return "[P]";
                    if (t == "SpringArm")          return "[A]";
                    if (t == "Camera")             return "[CAM]";
                    if (t == "Light")              return "[L]";
                    return "[?]";
                    };

                auto drawComp = [&](int i, float indent) {
                    auto& def = comps[i];
                    ImVec4 col = getColor(def.Type);
                    bool isSelected = (m_SelectedCompIndex == i);

                    ImGui::PushID(i);
                    if (indent > 0) ImGui::Indent(indent);

                    // X button
                    // Collapse button
                    bool& collapsed = m_CompCollapsed[i < 32 ? i : 0];
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
                    if (ImGui::SmallButton(collapsed ? ">" : "v")) collapsed = !collapsed;
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine(0, 2);
                    // X button
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                    if (ImGui::SmallButton("x")) removeIdx = i;
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine(0, 4);

                    // Icon
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::TextUnformatted(getIcon(def.Type));
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 4);

                    // Selectable com highlight
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    if (ImGui::Selectable(def.Type.c_str(), isSelected,
                        ImGuiSelectableFlags_None,
                        ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                        m_SelectedCompIndex = i;
                    ImGui::PopStyleColor();

                    // Drag para graph
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        std::string node;
                        if (def.Type == "Rigidbody")                             node = "GetRigidbody";
                        else if (def.Type.find("Collider") != std::string::npos) node = "GetCollider";
                        else if (def.Type == "CharacterController")              node = "GetCharacterController";
                        else if (def.Type == "SpringArm")                           node = "GetSpringArm";
                        else if (def.Type == "Camera")                              node = "GetCamera";
                        if (!node.empty())
                        {
                            ImGui::SetDragDropPayload("COMP_NODE", node.c_str(), node.size() + 1);
                            ImGui::PushStyleColor(ImGuiCol_Text, col);
                            ImGui::Text("Drag %s → Graph", def.Type.c_str());
                            ImGui::PopStyleColor();
                        }
                        else ImGui::TextDisabled("%s", def.Type.c_str());
                        ImGui::EndDragDropSource();
                    }

                    // Drop target — permite reparentar arrastando Camera sobre SpringArm
                    if (def.Type == "SpringArm" && ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("COMP_REPARENT"))
                        {
                            int childIdx = *(const int*)p->Data;
                            if (childIdx != i && childIdx < (int)comps.size())
                                comps[childIdx].ParentIndex = i;
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Camera: Ctrl+Drag → reparent para SpringArm
                    if (def.Type == "Camera" && ImGui::GetIO().KeyCtrl
                        && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        ImGui::SetDragDropPayload("COMP_REPARENT", &i, sizeof(int));
                        ImGui::TextUnformatted("Ctrl+Drag Camera → SpringArm");
                        ImGui::EndDragDropSource();
                    }

                    if (indent > 0) ImGui::Unindent(indent);
                    ImGui::PopID();
                    };

                // Renderiza raiz primeiro, depois filhos com indent
                for (int i = 0; i < (int)comps.size(); i++)
                    if (comps[i].ParentIndex == -1)
                        drawComp(i, 0.f);
                for (int i = 0; i < (int)comps.size(); i++)
                    if (comps[i].ParentIndex >= 0)
                        drawComp(i, 16.f);
                if (removeIdx >= 0)
                {
                    PushUndo("Remove Component");
                    m_ScriptAsset->RemoveComponent(removeIdx);
                    CommitUndo("Remove Component");
                    SyncComponentsToPreview();
                    m_ConsoleLines.push_back("[Info] Component removed.");
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
                if (ImGui::Button("Save", ImVec2(bw, 0)))
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
            m_GraphWindowCenter = ImVec2(
                ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f,
                ImGui::GetWindowPos().y + ImGui::GetWindowSize().y * 0.5f);
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
        m_InsideNodeEditorFrame = true;

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
                    {
                        ed::RejectNewItem(ImColor(255, 0, 0), 2);
                    }
                    else if (!ArePinsCompatible(o->Type, i->Type))
                    {
                        // Tipos incompatíveis — rejeita com vermelho e hint
                        ed::RejectNewItem(ImColor(220, 40, 40), 2);
                        ed::Suspend();
                        ImGui::SetTooltip("Tipos incompativeis — use um node Cast");
                        ed::Resume();
                    }
                    else if (o->Type != i->Type)
                    {
                        // Cast implicito — aceita com laranja (warning visual)
                        if (ed::AcceptNewItem(ImColor(220, 140, 40), 2.5f))
                        {
                            PushUndo("Add Link (cast)");
                            m_Graph->AddLink(o->ID, i->ID);
                            CommitUndo("Add Link (cast)");
                        }
                    }
                    else if (ed::AcceptNewItem(GetPinColor(o->Type), 2.5f))
                    {
                        PushUndo("Add Link");
                        m_Graph->AddLink(o->ID, i->ID);
                        CommitUndo("Add Link");
                    }
                }
            }
        }
        ed::EndCreate();

        // Process pending node deletions — remove directly from graph + editor
        for (auto& pendNid : m_PendingDeleteNodes)
        {
            // Remove all links connected to this node first
            auto* node = m_Graph->FindNode(pendNid);
            if (node)
            {
                std::vector<ed::LinkId> linkIds;
                for (auto& link : m_Graph->GetLinks())
                {
                    for (auto& p : node->Inputs)
                        if (link.StartPin == p.ID || link.EndPin == p.ID)
                        {
                            linkIds.push_back(link.ID); break;
                        }
                    for (auto& p : node->Outputs)
                        if (link.StartPin == p.ID || link.EndPin == p.ID)
                        {
                            linkIds.push_back(link.ID); break;
                        }
                }
                for (auto& lid : linkIds) m_Graph->RemoveLink(lid);
            }
            m_Graph->RemoveNode(pendNid);
        }
        if (!m_PendingDeleteNodes.empty()) { PushUndo("Delete Nodes"); }
        m_PendingDeleteNodes.clear();

        if (ed::BeginDelete())
        {
            ed::LinkId lid;
            bool anyDeleted = false;
            while (ed::QueryDeletedLink(&lid))
                if (ed::AcceptDeletedItem())
                {
                    if (!anyDeleted) { PushUndo("Delete"); anyDeleted = true; } m_Graph->RemoveLink(lid);
                }
            ed::NodeId nid;
            while (ed::QueryDeletedNode(&nid))
                if (ed::AcceptDeletedItem())
                {
                    if (!anyDeleted) { PushUndo("Delete"); anyDeleted = true; } m_Graph->RemoveNode(nid);
                }
            if (anyDeleted) CommitUndo("Delete");
        }
        ed::EndDelete();

        // ── Pending node (criado via drag de Script Members ou Override Events) ──
        // SetNodePosition só funciona aqui, dentro do Begin/End do editor (não no Suspend).
        if (!m_PendingNodeType.empty() && m_Graph)
        {
            PushUndo("Add Node: " + m_PendingNodeType);
            auto* node = m_Graph->AddNode(m_PendingNodeType.c_str());
            if (node)
            {
                // Position: if promoting, place relative to source node
                ImVec2 pos = m_PendingNodePos;
                if (m_PendingPromotePinId != ed::PinId{})
                {
                    // Find source node position via the saved pin
                    ScriptPin* srcPin = m_Graph->FindPin(m_PendingPromotePinId);
                    for (auto& n : m_Graph->GetNodes())
                    {
                        bool found = false;
                        for (auto& p : n->Inputs)  if (&p == srcPin) { found = true; break; }
                        for (auto& p : n->Outputs) if (&p == srcPin) { found = true; break; }
                        if (found)
                        {
                            ImVec2 srcPos = ed::GetNodePosition(n->ID);
                            pos = m_PendingPromoteIsInput
                                ? ImVec2(srcPos.x - 220.f, srcPos.y)
                                : ImVec2(srcPos.x + 220.f, srcPos.y);
                            break;
                        }
                    }
                }

                // Variable nodes from Get/Set popup: pos is screen coords, convert to canvas
                ImVec2 finalPos = pos;
                if (m_PendingNodeType == "GetVariable" || m_PendingNodeType == "SetVariable")
                    finalPos = ed::ScreenToCanvas(pos);
                ed::SetNodePosition(node->ID, finalPos);

                if (!m_PendingNodeStrValue.empty())
                    node->StringValue = m_PendingNodeStrValue;

                // Variable node: set type and pin types
                if ((m_PendingNodeType == "GetVariable" || m_PendingNodeType == "SetVariable")
                    && m_PendingVarType >= 0)
                {
                    node->IntValue = m_PendingVarType;
                    ScriptPinType pt = ScriptPinType::Float;
                    switch ((ScriptVarType)m_PendingVarType) {
                    case ScriptVarType::Bool:   pt = ScriptPinType::Bool;   break;
                    case ScriptVarType::Int:    pt = ScriptPinType::Int;    break;
                    case ScriptVarType::Vec3:   pt = ScriptPinType::Vec3;   break;
                    case ScriptVarType::String: pt = ScriptPinType::String; break;
                    default: break;
                    }
                    for (auto& p : node->Inputs)
                        if (p.Name == "Value") p.Type = pt;
                    for (auto& p : node->Outputs)
                        if (p.Name == "Value") p.Type = pt;
                    m_PendingVarType = 0;
                }

                // Promote: set var type and pin types, then auto-connect
                if (m_PendingPromotePinId != ed::PinId{})
                {
                    node->IntValue = m_PendingPromoteVarType;
                    for (auto& p : node->Inputs)
                        if (p.Name == "Value") p.Type = m_PendingPromotePinType;
                    for (auto& p : node->Outputs)
                        if (p.Name == "Value") p.Type = m_PendingPromotePinType;

                    // Auto-connect
                    ScriptPin* srcPin = m_Graph->FindPin(m_PendingPromotePinId);
                    if (srcPin)
                    {
                        if (!m_PendingPromoteIsInput)
                            for (auto& p : node->Inputs)
                                if (p.Name == "Value") { m_Graph->AddLink(srcPin->ID, p.ID); break; }
                                else
                                    for (auto& p : node->Outputs)
                                        if (p.Name == "Value") { m_Graph->AddLink(p.ID, srcPin->ID); break; }
                    }
                    m_PendingPromotePinId = {};
                    m_ConsoleLines.push_back("[Info] Promoted to variable: " + m_PendingNodeStrValue);
                }
                else
                {
                    m_ConsoleLines.push_back("[Info] Node: " + m_PendingNodeType +
                        (m_PendingNodeStrValue.empty() ? "" : " (" + m_PendingNodeStrValue + ")"));
                }
            }
            CommitUndo("Add Node");
            m_PendingNodeType.clear();
            m_PendingNodeStrValue.clear();
        }

        // ── Context menu ──────────────────────────────────────────────────────
        ImVec2 openPopupPosition = ImGui::GetMousePos();
        openPopupPosition.y -= 20.0f;

        // ShowPinContextMenu / ShowNodeContextMenu chamados APÓS o DrawNodeGraph
        // Os pins precisam estar registrados (BeginPin/EndPin) antes de ShowPinContextMenu
        ed::PinId ctxPinId;
        bool openPinCtx = ed::ShowPinContextMenu(&ctxPinId);
        if (openPinCtx) m_CtxPinId = ctxPinId;

        ed::NodeId ctxMenuNodeId;
        bool openNodeCtx = ed::ShowNodeContextMenu(&ctxMenuNodeId);
        if (openNodeCtx) m_CtxNodeId = ctxMenuNodeId;

        ed::Suspend();
        if (openPinCtx)  ImGui::OpenPopup("##PinCtx");
        if (openNodeCtx) ImGui::OpenPopup("##NodeCtx");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

        if (openPinCtx) ImGui::OpenPopup("##PinCtx");
        if (openNodeCtx) ImGui::OpenPopup("##NodeCtx");

        if (ImGui::BeginPopup("##PinCtx"))
        {
            // Find pin via graph API, then find its owner node
            ScriptPin* ctxPin = m_Graph->FindPin(m_CtxPinId);
            ScriptNode* ctxNode = nullptr;
            if (ctxPin)
            {
                for (auto& n : m_Graph->GetNodes())
                {
                    for (auto& p : n->Inputs)
                        if (&p == ctxPin) { ctxNode = n.get(); break; }
                    if (ctxNode) break;
                    for (auto& p : n->Outputs)
                        if (&p == ctxPin) { ctxNode = n.get(); break; }
                    if (ctxNode) break;
                }
            }

            bool isVarNode = ctxNode &&
                (ctxNode->Name == "Get Variable" || ctxNode->Name == "Set Variable");
            bool isSplit = ctxNode && (ctxNode->IntValue & 0x100);

            // Check if variable is Vec3 type (via asset or pin type or IntValue)
            bool isVec3Var = false;
            if (isVarNode && m_ScriptAsset)
            {
                int vt = ctxNode->IntValue & 0xFF; // low byte = ScriptVarType
                isVec3Var = (vt == (int)ScriptVarType::Vec3);
                if (!isVec3Var) // fallback: check asset
                    for (auto& v : m_ScriptAsset->GetVariables())
                        if (v.Name == ctxNode->StringValue)
                        {
                            isVec3Var = (v.Type == ScriptVarType::Vec3); break;
                        }
            }

            if (isVarNode && isVec3Var)
            {
                // Determine which side based on which pin was right-clicked
                bool clickedOutput = ctxPin && ctxPin->Kind == ed::PinKind::Output;
                bool clickedInput = ctxPin && ctxPin->Kind == ed::PinKind::Input;

                // Check if THIS side is already split
                bool outputSplit = false, inputSplit = false;
                for (auto& p : ctxNode->Outputs)
                    if (p.Name == "X" || p.Name == "Y" || p.Name == "Z") { outputSplit = true; break; }
                for (auto& p : ctxNode->Inputs)
                    if (p.Name == "X" || p.Name == "Y" || p.Name == "Z") { inputSplit = true; break; }

                bool thisSideSplit = clickedOutput ? outputSplit : inputSplit;

                if (!thisSideSplit && ImGui::MenuItem("Split Struct Pin"))
                {
                    if (clickedOutput)
                    {
                        // Remove Value output, add X Y Z outputs
                        ctxNode->Outputs.erase(
                            std::remove_if(ctxNode->Outputs.begin(), ctxNode->Outputs.end(),
                                [](const ScriptPin& p) { return p.Name == "Value"; }),
                            ctxNode->Outputs.end());
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), "X", ScriptPinType::Float, ed::PinKind::Output);
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), "Y", ScriptPinType::Float, ed::PinKind::Output);
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), "Z", ScriptPinType::Float, ed::PinKind::Output);
                    }
                    else
                    {
                        // Remove Value input (keep Flow In), add X Y Z inputs
                        ctxNode->Inputs.erase(
                            std::remove_if(ctxNode->Inputs.begin(), ctxNode->Inputs.end(),
                                [](const ScriptPin& p) { return p.Name == "Value"; }),
                            ctxNode->Inputs.end());
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), "X", ScriptPinType::Float, ed::PinKind::Input);
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), "Y", ScriptPinType::Float, ed::PinKind::Input);
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), "Z", ScriptPinType::Float, ed::PinKind::Input);
                    }
                    ctxNode->IntValue |= 0x100;
                    m_ConsoleLines.push_back("[Info] Pin split: " + ctxNode->StringValue);
                }
                else if (thisSideSplit && ImGui::MenuItem("Recombine Pin"))
                {
                    if (clickedOutput)
                    {
                        ctxNode->Outputs.erase(
                            std::remove_if(ctxNode->Outputs.begin(), ctxNode->Outputs.end(),
                                [](const ScriptPin& p) { return p.Name == "X" || p.Name == "Y" || p.Name == "Z"; }),
                            ctxNode->Outputs.end());
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), "Value", ScriptPinType::Vec3, ed::PinKind::Output);
                    }
                    else
                    {
                        ctxNode->Inputs.erase(
                            std::remove_if(ctxNode->Inputs.begin(), ctxNode->Inputs.end(),
                                [](const ScriptPin& p) { return p.Name == "X" || p.Name == "Y" || p.Name == "Z"; }),
                            ctxNode->Inputs.end());
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), "Value", ScriptPinType::Vec3, ed::PinKind::Input);
                    }
                    // Only clear split flag if neither side is split anymore
                    outputSplit = false; inputSplit = false;
                    for (auto& p : ctxNode->Outputs)
                        if (p.Name == "X" || p.Name == "Y" || p.Name == "Z") { outputSplit = true; break; }
                    for (auto& p : ctxNode->Inputs)
                        if (p.Name == "X" || p.Name == "Y" || p.Name == "Z") { inputSplit = true; break; }
                    if (!outputSplit && !inputSplit) ctxNode->IntValue &= ~0x100;
                    m_ConsoleLines.push_back("[Info] Pin recombined: " + ctxNode->StringValue);
                }
            }
            else if (ctxNode && ctxPin)
            {
                // Promote to Variable — available for any data pin
                bool isDataPin = ctxPin && ctxPin->Type != ScriptPinType::Flow;
                if (isDataPin && ImGui::MenuItem("Promote to Variable"))
                {
                    // Tudo feito via pending — NÃO usar ed::Set/GetNodePosition dentro do Suspend
                    ScriptVariable newVar;
                    newVar.Name = ctxPin->Name.empty() ? "NewVar" : ctxPin->Name;
                    switch (ctxPin->Type)
                    {
                    case ScriptPinType::Bool:   newVar.Type = ScriptVarType::Bool;   break;
                    case ScriptPinType::Int:    newVar.Type = ScriptVarType::Int;    break;
                    case ScriptPinType::Vec3:   newVar.Type = ScriptVarType::Vec3;   break;
                    case ScriptPinType::String: newVar.Type = ScriptVarType::String; break;
                    default:                    newVar.Type = ScriptVarType::Float;  break;
                    }
                    if (m_ScriptAsset) m_ScriptAsset->AddVariable(newVar);

                    bool isInput = ctxPin->Kind == ed::PinKind::Input;
                    // Store info for next-frame processing
                    m_PendingNodeType = isInput ? "SetVariable" : "GetVariable";
                    m_PendingNodeStrValue = newVar.Name;
                    m_PendingNodePos = m_GraphWindowCenter; // fallback pos
                    m_PendingPromotePinId = ctxPin->ID;
                    m_PendingPromoteIsInput = isInput;
                    m_PendingPromoteVarType = (int)newVar.Type;
                    m_PendingPromotePinType = ctxPin->Type;
                }
            }
            else
            {
                ImGui::TextDisabled("No actions available");
            }
            ImGui::EndPopup();
        }

        // ── Node context menu ─────────────────────────────────────────────────
        if (ImGui::BeginPopup("##NodeCtx"))
        {
            auto* node = m_Graph->FindNode(m_CtxNodeId);
            if (node)
            {
                ImGui::TextUnformatted(node->Name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Node"))
                {
                    ed::DeleteNode(ctxMenuNodeId);
                    m_ConsoleLines.push_back("[Info] Node deleted.");
                }
            }
            ImGui::EndPopup();
        }

        if (ed::ShowBackgroundContextMenu())
        {
            m_CtxBuf[0] = '\0';
            ImGui::OpenPopup("##SGCtx");
        }

        // ── Get / Set Variable popup ──────────────────────────────────────────
        if (m_VarDropPending) { ImGui::OpenPopup("##VarGetSet"); m_VarDropPending = false; }
        ImGui::SetNextWindowSize(ImVec2(130, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(m_VarDropPos, ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##VarGetSet"))
        {
            // Find var type for color
            ScriptVarType vt = ScriptVarType::Float;
            if (m_ScriptAsset)
                for (auto& v : m_ScriptAsset->GetVariables())
                    if (v.Name == m_VarDropName) { vt = v.Type; break; }

            ImColor vc = axe::GetVariableNodeColor((int)vt);
            ImVec4  vcv = { vc.Value.x, vc.Value.y, vc.Value.z, 1.f };

            ImGui::PushStyleColor(ImGuiCol_Text, vcv);
            ImGui::TextUnformatted(m_VarDropName.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();

            // Usa pending — ed::SetCurrentEditor dentro do Suspend corrompe o Resume
            if (ImGui::MenuItem("Get"))
            {
                m_PendingNodeType = "GetVariable";
                m_PendingNodePos = m_VarDropPos;  // screen coords — mesmo padrão do context menu
                m_PendingNodeStrValue = m_VarDropName;
                m_PendingVarType = (int)vt;
                m_VarDropIsCanvas = false;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Set"))
            {
                m_PendingNodeType = "SetVariable";
                m_PendingNodePos = m_VarDropPos;
                m_PendingNodeStrValue = m_VarDropName;
                m_PendingVarType = (int)vt;
                m_VarDropIsCanvas = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // Popup desenhado aqui — mesmo bloco suspenso, persiste entre frames
        ImGui::SetNextWindowSize(ImVec2(220, 400), ImGuiCond_Always);
        if (ImGui::BeginPopup("##SGCtx"))
        {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##cs", "Search node...", m_CtxBuf, sizeof(m_CtxBuf));
            ImGui::Separator();

            std::string s = m_CtxBuf;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            bool filtering = !s.empty();

            auto spawnNode = [&](const char* type)
                {
                    if (!m_Graph) return;
                    auto* node = m_Graph->AddNode(type);
                    if (node)
                    {
                        ed::SetNodePosition(node->ID, openPopupPosition);
                        m_ConsoleLines.push_back(std::string("[Info] Node criado: ") + type);
                    }
                    m_CtxBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                };

            // ── Categorias estáticas ──────────────────────────────────────────
            for (int ci = 0; ci < 6; ci++)
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
                        ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen);
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
                            else if (def.Type == "SpringArm")
                                drawGroup("SpringArm", s_SpringArmNodes, 2, { 0.9f,0.6f,1.f,1 });
                            else if (def.Type == "Camera")
                                drawGroup("Camera", s_CameraNodes, 2, { 0.7f,0.5f,1.f,1 });
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
        m_InsideNodeEditorFrame = false;
        ed::End();
        ed::SetCurrentEditor(nullptr);

        // ── Drop target no canvas do graph ────────────────────────────────────
        // Usa SetNextItemAllowOverlap + flag AllowOverlap para aceitar drags de outras janelas
        ImVec2 canvasMin = ImGui::GetWindowPos();
        ImVec2 canvasSize = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(canvasMin);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##graph_drop_target", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMP_NODE"))
            {
                std::string nodeType = (const char*)payload->Data;
                if (m_Graph && m_EdCtx)
                {
                    ed::SetCurrentEditor(m_EdCtx);
                    ImVec2 dropPos = ImGui::GetMousePos();
                    ImVec2 canvasPos = ed::ScreenToCanvas(dropPos);
                    auto* node = m_Graph->AddNode(nodeType.c_str());
                    if (node)
                    {
                        ed::SetNodePosition(node->ID, canvasPos);
                        m_ConsoleLines.push_back("[Info] Node created: " + nodeType);
                    }
                    ed::SetCurrentEditor(nullptr);
                }
            }

            // Variable drag → popup Get/Set choice (like Unreal)
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VAR_NODE"))
            {
                std::string data = (const char*)payload->Data;
                m_VarDropName = data.substr(data.find(':') + 1);
                m_VarDropPos = ImGui::GetMousePos();
                m_VarDropPending = true;  // flag — abre popup no Suspend
            }

            // Event Dispatcher drag → pending (same pattern as VAR_NODE)
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EVT_NODE"))
            {
                std::string data = (const char*)payload->Data;
                m_PendingNodeType = "SendEvent";
                m_PendingNodePos = ImGui::GetMousePos();
                m_PendingNodeStrValue = data.substr(data.find(':') + 1);
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
        // Vec3 variable nodes need more width to fit X/Y/Z fields
        if (node->Category == ScriptNodeCategory::Variable && !node->StringValue.empty() && m_ScriptAsset)
        {
            for (auto& v : m_ScriptAsset->GetVariables())
                if (v.Name == node->StringValue && v.Type == ScriptVarType::Vec3)
                {
                    nodeW = std::max(nodeW, 185.0f); break;
                }
        }

        // Variable nodes: sync name, type and pin colors from asset
        ImVec4 hcol;
        if (node->Category == ScriptNodeCategory::Variable && m_ScriptAsset)
        {
            int varTypeIdx = node->IntValue & 0xFF;
            ScriptPinType pinType = ScriptPinType::Float;

            // Sync from asset: update name reference and type
            for (auto& v : m_ScriptAsset->GetVariables())
            {
                // Match by StringValue (name) — if found, keep in sync
                if (v.Name == node->StringValue)
                {
                    varTypeIdx = (int)v.Type;
                    node->IntValue = (node->IntValue & 0x100) | varTypeIdx;
                    break;
                }
            }

            // Map var type to pin type
            switch ((ScriptVarType)varTypeIdx) {
            case ScriptVarType::Bool:   pinType = ScriptPinType::Bool;   break;
            case ScriptVarType::Int:    pinType = ScriptPinType::Int;    break;
            case ScriptVarType::Vec3:   pinType = ScriptPinType::Vec3;   break;
            case ScriptVarType::String: pinType = ScriptPinType::String; break;
            default:                    pinType = ScriptPinType::Float;  break;
            }

            // Update all Value pins to correct type
            bool isSplitNode = (node->IntValue & 0x100);
            for (auto& p : node->Inputs)
                if (p.Name == "Value") p.Type = pinType;
            for (auto& p : node->Outputs)
                if (p.Name == "Value") p.Type = pinType;
            // Split pins are always Float — don't change X/Y/Z

            ImColor c = axe::GetVariableNodeColor(varTypeIdx);
            hcol = { c.Value.x, c.Value.y, c.Value.z, c.Value.w };
        }
        else
        {
            hcol = HdrCol(node->Category);
        }
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
            // Variable nodes show "Get VarName" / "Set VarName"
            std::string dispName = node->Name;
            if (!node->StringValue.empty() &&
                (node->Name == "Get Variable" || node->Name == "Set Variable"))
                dispName = (node->Name == "Get Variable" ? "Get " : "Set ") + node->StringValue;
            float tw = ImGui::CalcTextSize(dispName.c_str()).x;
            ImGui::SetCursorScreenPos(ImVec2(p.x + (nodeW - tw) * .5f, p.y + 5));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            ImGui::TextUnformatted(dispName.c_str());
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + hh));
            ImGui::Dummy(ImVec2(nodeW, 4));
        }

        // ── Inline value editor for Set Variable nodes only ─────────────────────
        bool isGetVar = (node->Name == "Get Variable" && !node->StringValue.empty());
        bool isSetVar = (node->Name == "Set Variable" && !node->StringValue.empty());
        if (isSetVar && m_ScriptAsset)
        {
            ScriptVariable* var = nullptr;
            for (auto& v : m_ScriptAsset->GetVariables())
                if (v.Name == node->StringValue) { var = &v; break; }

            if (var)
            {
                // Set Variable: hide inline editor when Value input is connected
                bool hasConnection = false;
                if (isSetVar)
                    for (auto& link : m_Graph->GetLinks())
                        for (auto& p : node->Inputs)
                            if (p.Name == "Value" &&
                                (link.StartPin == p.ID || link.EndPin == p.ID))
                            {
                                hasConnection = true; break;
                            }

                if (!hasConnection)
                {
                    switch (var->Type)
                    {
                    case ScriptVarType::Float:
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
                        ImGui::SetNextItemWidth(nodeW - 16.f);
                        ImGui::DragFloat("##nv", &var->DefaultFloat, 0.01f);
                        break;
                    case ScriptVarType::Bool:
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
                        ImGui::Checkbox("##nv", &var->DefaultBool);
                        break;
                    case ScriptVarType::Int:
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
                        ImGui::SetNextItemWidth(nodeW - 16.f);
                        ImGui::DragInt("##nv", &var->DefaultInt);
                        break;
                    case ScriptVarType::Vec3:
                    {
                        float pad = 4.f;
                        float gap = 4.f;
                        float lbl = ImGui::CalcTextSize("X").x + 2.f;
                        float edPad = ed::GetStyle().NodePadding.z;
                        float fieldW = (nodeW - pad - (pad + edPad) - lbl * 3.f - gap * 2.f) / 3.f;
                        fieldW = std::max(fieldW, 38.f);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("X"); ImGui::SameLine(0, 2);
                        ImGui::SetNextItemWidth(fieldW);
                        ImGui::DragFloat("##nvx", &var->DefaultVec3[0], 0.01f, 0, 0, "%.3f");
                        ImGui::SameLine(0, gap);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Y"); ImGui::SameLine(0, 2);
                        ImGui::SetNextItemWidth(fieldW);
                        ImGui::DragFloat("##nvy", &var->DefaultVec3[1], 0.01f, 0, 0, "%.3f");
                        ImGui::SameLine(0, gap);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Z"); ImGui::SameLine(0, 2);
                        ImGui::SetNextItemWidth(fieldW);
                        ImGui::DragFloat("##nvz", &var->DefaultVec3[2], 0.01f, 0, 0, "%.3f");
                    }
                    break;
                    case ScriptVarType::String:
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
                        ImGui::SetNextItemWidth(nodeW - 16.f);
                        {
                            char buf[128] = {}; strncpy(buf, var->DefaultString.c_str(), 127);
                            if (ImGui::InputText("##nv", buf, 128)) var->DefaultString = buf;
                        }
                        break;
                    }
                }
            }
            ImGui::Spacing();
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
    void ScriptGraphWindow::DrawMyBlueprintWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (!ImGui::Begin("Script Members")) { ImGui::PopStyleVar(); ImGui::End(); return; }

        if (!m_ScriptAsset)
        {
            ImGui::TextDisabled("No script open.");
            ImGui::End(); ImGui::PopStyleVar(); return;
        }

        auto& vars = m_ScriptAsset->GetVariables();
        auto& evts = m_ScriptAsset->GetCustomEvents();

        static const char* s_VarTypes[] = { "Float", "Bool", "Int", "Vec3", "String", "Vec2", "Vec4", "Quat", "Entity" };
        static const ImVec4 s_VarCols[] = {
            {0.12f,0.55f,0.24f,1}, // Float  — verde escuro
            {0.70f,0.16f,0.16f,1}, // Bool   — vermelho
            {0.31f,0.78f,0.31f,1}, // Int    — verde claro
            {0.78f,0.71f,0.12f,1}, // Vec3   — amarelo
            {0.78f,0.31f,0.59f,1}, // String — rosa
            {0.16f,0.78f,0.78f,1}, // Vec2   — ciano
            {0.63f,0.31f,0.86f,1}, // Vec4   — roxo
            {0.71f,0.55f,0.86f,1}, // Quat   — lavanda
            {0.24f,0.47f,0.78f,1}, // Entity — azul
        };

        float avail = ImGui::GetContentRegionAvail().x;

        // ── VARIABLES ─────────────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.3f, 0.5f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.4f, 0.6f, 1));
        bool varOpen = ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (varOpen)
        {
            ImGui::SetNextItemWidth(avail * 0.42f);
            ImGui::InputText("##vname", m_NewVarName, sizeof(m_NewVarName));
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(avail * 0.28f);
            ImGui::Combo("##vtype", &m_NewVarType, s_VarTypes, 9);
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1));
            if (ImGui::SmallButton("+ Var"))
            {
                ScriptVariable v;
                v.Name = (m_NewVarName[0] != 0) ? m_NewVarName : "NewVar";
                v.Type = (ScriptVarType)m_NewVarType;
                PushUndo("Add Variable");
                m_ScriptAsset->AddVariable(v);
                CommitUndo("Add Variable");
                m_NewVarName[0] = 0;
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();

            int removeVar = -1;
            for (int i = 0; i < (int)vars.size(); i++)
            {
                auto& v = vars[i];
                ImGui::PushID(i);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                if (ImGui::SmallButton("x")) removeVar = i;
                ImGui::PopStyleColor(3);
                ImGui::SameLine(0, 4);

                ImGui::PushStyleColor(ImGuiCol_Text, s_VarCols[(int)v.Type]);
                ImGui::TextUnformatted(s_VarTypes[(int)v.Type]);
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 6);

                bool sel = (m_SelectedVar == i);
                bool wantRename = (m_RenamingVar == i);

                if (wantRename)
                {
                    ImGui::SetNextItemWidth(avail * 0.52f);
                    if (m_RenameJustStarted)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_RenameJustStarted = false;
                    }
                    if (ImGui::InputText("##ren", m_RenameBuf, sizeof(m_RenameBuf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    {
                        if (m_RenameBuf[0] != 0)
                        {
                            std::string oldName = v.Name;
                            v.Name = m_RenameBuf;
                            // Update all nodes in graph that reference this variable
                            if (m_Graph)
                                for (auto& n : m_Graph->GetNodes())
                                    if ((n->Name == "Get Variable" || n->Name == "Set Variable")
                                        && n->StringValue == oldName)
                                        n->StringValue = v.Name;
                        }
                        m_RenamingVar = -1;
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_RenamingVar = -1;
                }
                else
                {
                    if (ImGui::Selectable(v.Name.c_str(), sel,
                        ImGuiSelectableFlags_AllowDoubleClick, ImVec2(avail * 0.52f, 0)))
                    {
                        m_SelectedVar = i;
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            m_RenamingVar = i;
                            m_RenameJustStarted = true;
                            strncpy(m_RenameBuf, v.Name.c_str(), sizeof(m_RenameBuf) - 1);
                            m_RenameBuf[sizeof(m_RenameBuf) - 1] = 0;
                        }
                    }
                    if (sel && ImGui::IsKeyPressed(ImGuiKey_F2))
                    {
                        m_RenamingVar = i;
                        m_RenameJustStarted = true;
                        strncpy(m_RenameBuf, v.Name.c_str(), sizeof(m_RenameBuf) - 1);
                        m_RenameBuf[sizeof(m_RenameBuf) - 1] = 0;
                    }
                }

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    std::string pl = "GetVar:" + v.Name;
                    ImGui::SetDragDropPayload("VAR_NODE", pl.c_str(), pl.size() + 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, s_VarCols[(int)v.Type]);
                    ImGui::Text("Get %s", v.Name.c_str());
                    ImGui::PopStyleColor();
                    ImGui::EndDragDropSource();
                }

                if (sel)
                {
                    // Only show type change here — rename via F2/double-click, values in Node tab
                    ImGui::Indent(16);
                    static const char* s_TypeLabels[] = { "Float","Bool","Int","Vec3","String","Vec2","Vec4","Quat","Entity" };
                    int typeIdx = (int)v.Type;
                    ImGui::SetNextItemWidth(avail * 0.6f);
                    if (ImGui::Combo("Type##vtype", &typeIdx, s_TypeLabels, 9))
                        v.Type = (ScriptVarType)typeIdx;
                    ImGui::TextDisabled("Select node to edit value in Node tab");
                    ImGui::Unindent(16);
                }
                ImGui::PopID();
            }
            // Delete variable — check if any nodes reference it first
            if (removeVar >= 0 && removeVar < (int)vars.size())
            {
                std::string varName = vars[removeVar].Name;
                bool inGraph = false;
                if (m_Graph)
                    for (auto& n : m_Graph->GetNodes())
                        if ((n->Name == "Get Variable" || n->Name == "Set Variable")
                            && n->StringValue == varName)
                        {
                            inGraph = true; break;
                        }

                if (inGraph)
                {
                    // Show confirmation modal
                    m_DeleteVarIndex = removeVar;
                    m_DeleteVarName = varName;
                    ImGui::OpenPopup("##ConfirmDeleteVar");
                }
                else
                {
                    PushUndo("Remove Variable");
                    m_ScriptAsset->RemoveVariable(removeVar);
                    CommitUndo("Remove Variable");
                    m_SelectedVar = -1;
                    m_RenamingVar = -1;
                }
            }

            // Confirmation modal
            ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Always);
            if (ImGui::BeginPopupModal("##ConfirmDeleteVar", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.8f, 0.2f, 1));
                ImGui::TextUnformatted("Warning");
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextWrapped("Variable '%s' is referenced by nodes in the graph.",
                    m_DeleteVarName.c_str());
                ImGui::TextWrapped("Deleting it will leave those nodes broken.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Delete nodes too checkbox
                ImGui::Checkbox("Also delete nodes from graph", &m_DeleteVarAlsoNodes);
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1));
                if (ImGui::Button("Delete", ImVec2(100, 0)))
                {
                    if (m_Graph)
                    {
                        for (auto& n : m_Graph->GetNodes())
                        {
                            if ((n->Name == "Get Variable" || n->Name == "Set Variable")
                                && n->StringValue == m_DeleteVarName)
                            {
                                if (m_DeleteVarAlsoNodes)
                                    m_PendingDeleteNodes.push_back(n->ID);
                                else
                                    // Keep node but clear its reference so it shows as broken
                                    n->StringValue = "[deleted] " + m_DeleteVarName;
                            }
                        }
                    }
                    PushUndo("Remove Variable");
                    m_ScriptAsset->RemoveVariable(m_DeleteVarIndex);
                    CommitUndo("Remove Variable");
                    m_SelectedVar = -1;
                    m_RenamingVar = -1;
                    m_DeleteVarIndex = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0)))
                {
                    m_DeleteVarIndex = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Spacing();
        }

        // ── OVERRIDE EVENTS ───────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.2f, 0.1f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.5f, 0.3f, 0.2f, 1));
        bool ovrOpen = ImGui::CollapsingHeader("Override Events", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (ovrOpen)
        {
            ImGui::TextDisabled("Double-click to add to graph:");
            ImGui::Spacing();
            static const struct { const char* label; const char* type; } s_Overrides[] = {
                { "On Start",     "OnStart"     },
                { "On Update",    "OnUpdate"    },
                { "On End",       "OnEnd"       },
                { "On Collision", "OnCollision" },
                { "On Event",     "OnEvent"     },
            };
            for (auto& ov : s_Overrides)
            {
                ImGui::PushID(ov.type);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.2f, 1));
                ImGui::Bullet();
                ImGui::PopStyleColor();
                ImGui::SameLine();

                if (ImGui::Selectable(ov.label, false, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_Graph)
                    {
                        ed::SetCurrentEditor(m_EdCtx);
                        ImVec2 canvasPos = ed::ScreenToCanvas(m_GraphWindowCenter);
                        auto* node = m_Graph->AddNode(ov.type);
                        if (node) ed::SetNodePosition(node->ID, canvasPos);
                        ed::SetCurrentEditor(nullptr);
                        m_ConsoleLines.push_back(std::string("[Info] Added: ") + ov.label);
                    }
                }

                // Drag to graph — same as COMP_NODE
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("COMP_NODE", ov.type, strlen(ov.type) + 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.2f, 1));
                    ImGui::Text("Add %s", ov.label);
                    ImGui::PopStyleColor();
                    ImGui::EndDragDropSource();
                }

                ImGui::PopID();
            }
        }


        // ── EVENT DISPATCHERS ─────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.2f, 0.3f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.3f, 0.4f, 1));
        bool evtOpen = ImGui::CollapsingHeader("Event Dispatchers", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (evtOpen)
        {
            ImGui::SetNextItemWidth(avail * 0.68f);
            ImGui::InputText("##evtname", m_NewEvtName, sizeof(m_NewEvtName));
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.25f, 1));
            if (ImGui::SmallButton("+ Event"))
            {
                ScriptCustomEvent e;
                e.Name = (m_NewEvtName[0] != 0) ? m_NewEvtName : "OnMyEvent";
                PushUndo("Add Event");
                m_ScriptAsset->AddCustomEvent(e);
                CommitUndo("Add Event");
                m_NewEvtName[0] = 0;
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();

            int removeEvt = -1;
            for (int i = 0; i < (int)evts.size(); i++)
            {
                auto& e = evts[i];
                ImGui::PushID(100 + i);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                if (ImGui::SmallButton("x")) removeEvt = i;
                ImGui::PopStyleColor(3);
                ImGui::SameLine(0, 4);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.5f, 0.6f, 1));
                ImGui::TextUnformatted("[D]");
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 6);
                ImGui::TextUnformatted(e.Name.c_str());

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    std::string pl = "Dispatch:" + e.Name;
                    ImGui::SetDragDropPayload("EVT_NODE", pl.c_str(), pl.size() + 1);
                    ImGui::Text("Dispatch %s", e.Name.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
            }
            if (removeEvt >= 0) { PushUndo("Remove Event"); m_ScriptAsset->RemoveCustomEvent(removeEvt); CommitUndo("Remove Event"); }
            ImGui::Spacing();
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }


    // ─────────────────────────────────────────

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
            if (ImGui::BeginTabItem("Object"))
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
                            m_ConsoleLines.push_back("[Info] Component removed.");
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
                                if (ImGui::Combo("Primitive", &curIdx, primitives, 4))
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
                                    ImGui::DragFloat("Mass", &def.Mass, 0.1f, 0.01f, 1000.f);
                                    ImGui::DragFloat("Friction", &def.Friction, 0.01f, 0.f, 1.f);
                                    ImGui::DragFloat("Restitution", &def.Restitution, 0.01f, 0.f, 1.f);
                                    ImGui::DragFloat("Linear Damp", &def.LinearDamping, 0.01f, 0.f, 1.f);
                                    ImGui::DragFloat("Angular Damp", &def.AngularDamping, 0.01f, 0.f, 1.f);
                                    ImGui::Checkbox("Gravity", &def.UseGravity);
                                    ImGui::TextDisabled("Lock Rotation:");
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
                                if (ImGui::Combo("Shape", &cur, shapes, 5))
                                    def.ColliderShape = shapeKeys[cur];

                                ImGui::Checkbox("Is Trigger", &def.IsTrigger);
                                ImGui::Checkbox("Debug Wireframe", &def.ShowDebug);
                                ImGui::DragFloat3("Offset", &def.ColliderOffsetX, 0.01f);

                                if (def.ColliderShape == "Box")
                                    ImGui::DragFloat3("Half Extent", &def.ColliderSizeX, 0.01f, 0.01f, 100.f);
                                else if (def.ColliderShape == "Sphere")
                                    ImGui::DragFloat("Radius", &def.ColliderRadius, 0.01f, 0.01f, 100.f);
                                else if (def.ColliderShape == "Capsule")
                                {
                                    ImGui::DragFloat("Height", &def.ColliderHeight, 0.01f, 0.1f, 10.f);
                                    ImGui::DragFloat("Capsule Radius", &def.ColliderCapsuleRadius, 0.01f, 0.01f, 5.f);
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
                                ImGui::DragFloat("Height", &def.CCHeight, 0.01f, 0.5f, 5.f);
                                ImGui::DragFloat("Radius", &def.CCRadius, 0.01f, 0.1f, 2.f);
                                ImGui::DragFloat("Max Slope", &def.CCMaxSlope, 0.5f, 0.f, 89.f);
                                ImGui::DragFloat("Step Height", &def.CCStepHeight, 0.01f, 0.f, 1.f);
                                ImGui::DragFloat("Max Speed", &def.CCMaxSpeed, 0.1f, 0.f, 50.f);
                                ImGui::DragFloat("Jump Force", &def.CCJumpForce, 0.1f, 0.f, 50.f);
                            }
                            // ── SPRING ARM ────────────────────────────────────
                            else if (def.Type == "SpringArm")
                            {
                                bool armChanged = false;
                                if (ImGui::DragFloat("Length##sa", &def.SALength, 1.f, 50.f, 1000.f, "%.0f")) armChanged = true;
                                if (ImGui::DragFloat("Height##sa", &def.SAHeightOffset, 0.1f, -10.f, 20.f, "%.2f")) armChanged = true;
                                float off[3] = { def.SASocketOffX, def.SASocketOffY, def.SASocketOffZ };
                                if (ImGui::DragFloat3("Socket Offset##sa", off, 0.05f)) {
                                    def.SASocketOffX = off[0]; def.SASocketOffY = off[1]; def.SASocketOffZ = off[2];
                                    armChanged = true;
                                }
                                if (ImGui::DragFloat("Smoothing##sa", &def.SALagSpeed, 0.1f, 0.5f, 30.f, "%.1f")) armChanged = true;
                                ImGui::Checkbox("Camera Lag##sa", &def.SAEnableLag);
                                ImGui::Checkbox("Mouse Rotates##sa", &def.SAMouseRotates);

                                // Atualiza preview + cena ativa (Play)
                                {
                                    auto& pr = m_PreviewScene->GetRegistry();
                                    auto& sa = pr.get_or_emplace<SpringArmComponent>(m_PreviewEntity);
                                    sa.Length = def.SALength / 100.0f;
                                    sa.HeightOffset = def.SAHeightOffset;
                                    sa.SocketOffset = { def.SASocketOffX, def.SASocketOffY, def.SASocketOffZ };
                                    sa.LagSpeed = def.SALagSpeed;
                                    sa.EnableCameraLag = def.SAEnableLag;
                                    sa.MouseRotates = def.SAMouseRotates;

                                    // Propaga para a cena ativa se em Play
                                    if (m_ActiveScene)
                                    {
                                        auto& ar = m_ActiveScene->GetRegistry();
                                        ar.view<SpringArmComponent, ScriptComponent>().each(
                                            [&](entt::entity e, SpringArmComponent& asa, ScriptComponent& sc)
                                            {
                                                if (sc.ScriptName == m_ScriptAsset->GetName())
                                                {
                                                    asa.Length = sa.Length;
                                                    asa.HeightOffset = sa.HeightOffset;
                                                    asa.SocketOffset = sa.SocketOffset;
                                                    asa.LagSpeed = sa.LagSpeed;
                                                }
                                            });
                                    }

                                    // Cria/atualiza entidade do mesh de câmera no preview
                                    if (m_CameraPreviewEntity == entt::null || !pr.valid(m_CameraPreviewEntity))
                                    {
                                        m_CameraPreviewEntity = m_PreviewScene->CreateEntity("CameraPreviewMesh");
                                        auto& mc = pr.emplace<MeshComponent>(m_CameraPreviewEntity);
                                        mc.Data = MeshFactory::CreateCamera();
                                        // Sem material — usa o default do renderer
                                    }

                                    // Posição do mesh câmera é calculada em DrawPreviewGizmo
                                    // para evitar conflito com o gizmo 3D durante o drag
                                }

                                ImGui::Spacing();
                                ImGui::TextDisabled("Parent index: %d", def.ParentIndex);
                            }
                            // ── CAMERA ───────────────────────────────────────
                            else if (def.Type == "Camera")
                            {
                                ImGui::DragFloat("FOV##cam", &def.CamFov, 0.5f, 10.f, 170.f, "%.1f°");
                                ImGui::DragFloat("Near Clip##cam", &def.CamNearClip, 0.01f, 0.001f, 10.f);
                                ImGui::DragFloat("Far Clip##cam", &def.CamFarClip, 1.f, 10.f, 10000.f);
                                ImGui::DragFloat("Sensibilidade##cam", &def.CamSensitivity, 0.005f, 0.01f, 5.f);
                                ImGui::Checkbox("Primary Camera##cam", &def.CamIsPrimary);

                                // Atualiza CameraComponent no preview
                                auto& pr = m_PreviewScene->GetRegistry();
                                auto& cam = pr.get_or_emplace<CameraComponent>(m_PreviewEntity);
                                cam.Fov = def.CamFov;
                                cam.NearClip = def.CamNearClip;
                                cam.FarClip = def.CamFarClip;
                                cam.Sensitivity = def.CamSensitivity;
                                cam.IsPrimary = def.CamIsPrimary;

                                ImGui::Spacing();
                                ImGui::TextDisabled("Parent index: %d", def.ParentIndex);
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
                            // Display name (variable nodes show "Get VarName")
                            std::string dispName = node->Name;
                            if (!node->StringValue.empty() &&
                                (node->Name == "Get Variable" || node->Name == "Set Variable"))
                                dispName = (node->Name == "Get Variable" ? "Get " : "Set ") + node->StringValue;

                            // Header color — variable nodes use per-type color
                            ImVec4 hcol;
                            if (node->Category == ScriptNodeCategory::Variable && m_ScriptAsset)
                            {
                                int ti = 0;
                                for (auto& v : m_ScriptAsset->GetVariables())
                                    if (v.Name == node->StringValue) { ti = (int)v.Type; break; }
                                ImColor c = axe::GetVariableNodeColor(ti);
                                hcol = { c.Value.x, c.Value.y, c.Value.z, 1.f };
                            }
                            else
                            {
                                ImColor hc = GetNodeHeaderColor(node->Category);
                                hcol = { hc.Value.x, hc.Value.y, hc.Value.z, 1.f };
                            }

                            ImGui::PushStyleColor(ImGuiCol_Text, hcol);
                            ImGui::TextUnformatted(dispName.c_str());
                            ImGui::PopStyleColor();
                            ImGui::Separator(); ImGui::Spacing();

                            // ── Variable node: show full variable properties ───────────
                            bool isVarNode = (node->Name == "Get Variable" || node->Name == "Set Variable")
                                && !node->StringValue.empty() && m_ScriptAsset;
                            if (isVarNode)
                            {
                                ScriptVariable* foundVar = nullptr;
                                for (auto& v : m_ScriptAsset->GetVariables())
                                    if (v.Name == node->StringValue) { foundVar = &v; break; }

                                if (foundVar)
                                {
                                    static const char* s_TypeNames[] = { "Float","Bool","Int","Vec3","String","Vec2","Vec4","Quat","Entity" };
                                    static const ImVec4 s_TypeCols[] = {
                                        {0.2f,0.7f,0.3f,1},{0.9f,0.2f,0.2f,1},
                                        {0.4f,0.9f,0.4f,1},{1.f,0.9f,0.2f,1},{0.95f,0.4f,0.7f,1},
                                        {0.16f,0.78f,0.78f,1},{0.63f,0.31f,0.86f,1},
                                        {0.71f,0.55f,0.86f,1},{0.24f,0.47f,0.78f,1}
                                    };
                                    int ti = (int)foundVar->Type;

                                    // Type badge
                                    ImGui::PushStyleColor(ImGuiCol_Text, s_TypeCols[ti]);
                                    ImGui::TextUnformatted(s_TypeNames[ti]);
                                    ImGui::PopStyleColor();
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(foundVar->Name.c_str());
                                    ImGui::Spacing();

                                    // Name (rename here too)
                                    ImGui::TextDisabled("Name:");
                                    ImGui::SetNextItemWidth(-1);
                                    char nbuf[64] = {};
                                    strncpy(nbuf, foundVar->Name.c_str(), 63);
                                    if (ImGui::InputText("##vname_nd", nbuf, 64,
                                        ImGuiInputTextFlags_EnterReturnsTrue))
                                    {
                                        if (nbuf[0])
                                        {
                                            std::string oldName = foundVar->Name;
                                            foundVar->Name = nbuf;
                                            // Update all nodes referencing this variable
                                            if (m_Graph)
                                                for (auto& n : m_Graph->GetNodes())
                                                    if ((n->Name == "Get Variable" || n->Name == "Set Variable")
                                                        && n->StringValue == oldName)
                                                        n->StringValue = foundVar->Name;
                                        }
                                    }
                                    ImGui::Spacing();

                                    // Default value
                                    ImGui::TextDisabled("Default Value:");
                                    ImGui::SetNextItemWidth(-1);
                                    switch (foundVar->Type)
                                    {
                                    case ScriptVarType::Float:
                                        ImGui::DragFloat("##nd_f", &foundVar->DefaultFloat, 0.01f);
                                        break;
                                    case ScriptVarType::Bool:
                                        ImGui::Checkbox("##nd_b", &foundVar->DefaultBool);
                                        break;
                                    case ScriptVarType::Int:
                                        ImGui::DragInt("##nd_i", &foundVar->DefaultInt);
                                        break;
                                    case ScriptVarType::Vec3:
                                    {
                                        float gap = 4.f;
                                        float w = ImGui::GetContentRegionAvail().x;
                                        float lbl = ImGui::CalcTextSize("X").x;
                                        float wf = (w - (lbl + 2.f + gap) * 3.f + gap) / 3.f;
                                        ImGui::AlignTextToFramePadding();
                                        ImGui::TextDisabled("X");
                                        ImGui::SameLine(0, 2);
                                        ImGui::SetNextItemWidth(wf);
                                        ImGui::DragFloat("##nd_x", &foundVar->DefaultVec3[0], 0.01f, 0, 0, "%.3f");
                                        ImGui::SameLine(0, gap);
                                        ImGui::AlignTextToFramePadding();
                                        ImGui::TextDisabled("Y");
                                        ImGui::SameLine(0, 2);
                                        ImGui::SetNextItemWidth(wf);
                                        ImGui::DragFloat("##nd_y", &foundVar->DefaultVec3[1], 0.01f, 0, 0, "%.3f");
                                        ImGui::SameLine(0, gap);
                                        ImGui::AlignTextToFramePadding();
                                        ImGui::TextDisabled("Z");
                                        ImGui::SameLine(0, 2);
                                        ImGui::SetNextItemWidth(wf);
                                        ImGui::DragFloat("##nd_z", &foundVar->DefaultVec3[2], 0.01f, 0, 0, "%.3f");
                                    }
                                    break;
                                    case ScriptVarType::String:
                                    {
                                        char sbuf[256] = {};
                                        strncpy(sbuf, foundVar->DefaultString.c_str(), 255);
                                        if (ImGui::InputText("##nd_s", sbuf, 256))
                                            foundVar->DefaultString = sbuf;
                                    }
                                    break;
                                    }
                                    ImGui::Spacing();

                                    // Exposed checkbox
                                    ImGui::Checkbox("Exposed (Inspector)##nd_exp", &foundVar->Exposed);
                                    ImGui::Spacing();
                                    ImGui::Separator(); ImGui::Spacing();
                                }
                                else
                                {
                                    ImGui::TextColored({ 0.9f,0.3f,0.3f,1 },
                                        "Variable '%s' not found.", node->StringValue.c_str());
                                    ImGui::TextDisabled("Declare it in Script Members.");
                                    ImGui::Spacing();
                                }
                            }
                            else if (node->Name == "Get Key" || node->Name == "Get Axis" ||
                                node->Name == "Print String")
                            {
                                const char* lbl =
                                    node->Name == "Get Key" ? "Key (e.g. W):" :
                                    node->Name == "Get Axis" ? "Axis (e.g. Horizontal):" :
                                    "Message:";
                                // Check if Key pin is connected — disable field if so
                                bool keyPinLinked = false;
                                if (node->Name == "Get Key")
                                    for (auto& p : node->Inputs)
                                        if (p.Name == "Key" && m_Graph->IsPinLinked(p.ID))
                                        {
                                            keyPinLinked = true; break;
                                        }

                                ImGui::TextDisabled("%s", lbl);
                                if (keyPinLinked)
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
                                    ImGui::TextUnformatted("(connected — value comes from node)");
                                    ImGui::PopStyleColor();
                                }
                                else
                                {
                                    ImGui::SetNextItemWidth(-1);
                                    char buf[128];
                                    std::strncpy(buf, node->StringValue.c_str(), sizeof(buf));
                                    buf[sizeof(buf) - 1] = '\0';
                                    if (ImGui::InputText("##sv", buf, sizeof(buf)))
                                        node->StringValue = buf;
                                }
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
        glm::vec2 delta(m_PreviewMouseDelta.x, m_PreviewMouseDelta.y);
        delta *= 0.003f;
        if (alt)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))        m_PreviewRenderer->OnMouseRotate(delta);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_PreviewRenderer->OnMousePan(delta);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))  m_PreviewRenderer->OnMouseZoom(delta.y * 10.f);
        }
        if (io.MouseWheel != 0.0f) m_PreviewRenderer->OnMouseZoom(io.MouseWheel);

        // ── Picking por clique esquerdo (sem Alt) ─────────────────────────────
        if (!alt && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_PreviewScene)
        {
            float w = m_PreviewBoundsMax.x - m_PreviewBoundsMin.x;
            float h = m_PreviewBoundsMax.y - m_PreviewBoundsMin.y;
            if (w > 0.f && h > 0.f)
            {
                // NDC do clique
                float nx = ((mousePos.x - m_PreviewBoundsMin.x) / w) * 2.f - 1.f;
                float ny = 1.f - ((mousePos.y - m_PreviewBoundsMin.y) / h) * 2.f;

                glm::mat4 view = m_PreviewRenderer->m_Camera->GetViewMatrix();
                glm::mat4 proj = m_PreviewRenderer->m_Camera->GetProjectionMatrix();
                glm::mat4 invVP = glm::inverse(proj * view);

                // Ray em world space
                glm::vec4 rayClipNear = { nx, ny, -1.f, 1.f };
                glm::vec4 rayClipFar = { nx, ny,  1.f, 1.f };
                glm::vec4 rayWorldNear = invVP * rayClipNear;
                glm::vec4 rayWorldFar = invVP * rayClipFar;
                rayWorldNear /= rayWorldNear.w;
                rayWorldFar /= rayWorldFar.w;

                glm::vec3 rayOrigin = glm::vec3(rayWorldNear);
                glm::vec3 rayDir = glm::normalize(glm::vec3(rayWorldFar) - rayOrigin);

                // Testa contra esfera de cada entidade com TransformComponent
                auto& reg = m_PreviewScene->GetRegistry();
                entt::entity bestEntity = entt::null;
                float bestDist = 1e9f;

                auto testSphere = [&](entt::entity e, float radius)
                    {
                        auto* tc = reg.try_get<TransformComponent>(e);
                        if (!tc) return;
                        glm::vec3 center = tc->Data.Position;
                        glm::vec3 oc = rayOrigin - center;
                        float b = glm::dot(oc, rayDir);
                        float c = glm::dot(oc, oc) - radius * radius;
                        float disc = b * b - c;
                        if (disc >= 0.f)
                        {
                            float t = -b - glm::sqrt(disc);
                            if (t > 0.001f && t < bestDist)
                            {
                                bestDist = t;
                                bestEntity = e;
                            }
                        }
                    };

                // Testa pawn
                testSphere(m_PreviewEntity, 0.5f);
                // Testa mesh câmera
                if (m_CameraPreviewEntity != entt::null && reg.valid(m_CameraPreviewEntity))
                    testSphere(m_CameraPreviewEntity, 0.25f);

                if (bestEntity != entt::null && m_ScriptAsset)
                {
                    if (bestEntity == m_CameraPreviewEntity)
                    {
                        // Seleciona SpringArm no Scene Graph
                        auto& comps = m_ScriptAsset->GetComponents();
                        for (int i = 0; i < (int)comps.size(); i++)
                            if (comps[i].Type == "SpringArm" || comps[i].Type == "Camera")
                            {
                                m_SelectedCompIndex = i; break;
                            }
                    }
                    else
                    {
                        // Seleciona o primeiro componente (Mesh/Transform do pawn)
                        m_SelectedCompIndex = 0;
                    }
                }
                else
                {
                    // Clique no vazio — deseleciona
                    m_SelectedCompIndex = -1;
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Undo / Redo ────────────────────────────────────────────────────────────
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

        // Store both snapshots in lambdas — apply deferred to avoid
        // corrupting node editor state mid-frame
        Command cmd;
        cmd.Name = actionName;
        cmd.Execute = nullptr;  // action already done — don't re-execute on Push
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
        // Lambda set m_PendingUndoSnapshot — apply it now
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
        // Redo: use the after-snapshot stored by the last Undo call
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
        // Usa flag para evitar SetCurrentEditor dentro do Begin/End
        // que corromperia o canvas interno
        if (!m_InsideNodeEditorFrame)
            ed::SetCurrentEditor(m_EdCtx);
        for (auto& node : m_Graph->GetNodes())
            node->Position = ed::GetNodePosition(node->ID);
        if (!m_InsideNodeEditorFrame)
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

        m_ConsoleLines.push_back("[Script Editor] Generating C++...");
        const std::vector<ScriptVariable>* assetVars =
            (m_ScriptAsset && !m_ScriptAsset->GetVariables().empty())
            ? &m_ScriptAsset->GetVariables() : nullptr;
        std::string code = ScriptGraphCompiler::Generate(*m_Graph, scriptName, assetVars);

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