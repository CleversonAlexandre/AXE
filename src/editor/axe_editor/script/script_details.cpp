// script_details.cpp
// DrawDetailsWindow / DrawScriptDetails / DrawSceneGraphWindow
// Painel de detalhes: tab Object (Transform + componentes) e tab Node (node selecionado).
// DrawSceneGraphWindow: hierarquia de componentes do script, add/remove, drag para graph.

#include "script_graph_window.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/input/input_mapping.hpp"
#include "axe/scene/components.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/material/material_asset.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/anim_graph_asset.hpp"
#include "editor/axe_editor/asset/asset_picker.hpp"
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include "editor/axe_editor/editor_icon_library.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <fstream>
#include <filesystem>
#include <functional> // std::function no desenho recursivo da arvore
#include <cstdio>     // std::snprintf no payload de arrasto
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    // Campo vetorial com X/Y/Z identificados — cada eixo tem sua letra desenhada
    // dentro da própria caixa (overlay via ImDrawList, não altera o DragFloat em
    // si), com a cor convencional de cada eixo (vermelho/verde/azul), no estilo
    // Unreal/Unity. Usado para Position, Rotation e Scale.
    static bool DragVec3Labeled(const char* id, float* v, float speed,
        float vmin = 0.0f, float vmax = 0.0f, const char* fmt = "%.3f")
    {
        static const ImVec4 axisCols[3] = {
            ImVec4(0.95f, 0.35f, 0.35f, 1), // X — vermelho
            ImVec4(0.45f, 0.85f, 0.35f, 1), // Y — verde
            ImVec4(0.35f, 0.55f, 0.95f, 1), // Z — azul
        };
        static const char* axisLabels[3] = { "X", "Y", "Z" };

        bool changed = false;
        float avail = ImGui::GetContentRegionAvail().x;
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        float fieldW = (avail - spacing * 2.f) / 3.f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGui::PushID(id);
        for (int axis = 0; axis < 3; axis++)
        {
            ImGui::PushID(axis);
            ImGui::SetNextItemWidth(fieldW);
            // Espaço reservado à esquerda do número para a letra do eixo
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.f, ImGui::GetStyle().FramePadding.y));
            if (ImGui::DragFloat("##v", &v[axis], speed, vmin, vmax, fmt))
                changed = true;
            ImGui::PopStyleVar();

            ImVec2 r0 = ImGui::GetItemRectMin();
            ImVec2 r1 = ImGui::GetItemRectMax();
            dl->AddText(ImVec2(r0.x + 6.f, (r0.y + r1.y) * 0.5f - ImGui::GetFontSize() * 0.5f),
                ImGui::ColorConvertFloat4ToU32(axisCols[axis]), axisLabels[axis]);

            ImGui::PopID();
            if (axis < 2) ImGui::SameLine(0, spacing);
        }
        ImGui::PopID();
        return changed;
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawDetailsWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (ImGui::Begin("Script Details"))
            DrawScriptDetails();
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Campos de cada tipo de componente — extraído de DrawScriptDetails para ser
    // reaproveitado também pelo collapse inline na lista do Scene Graph (drawComp).
    // Requer m_PreviewScene válido (mesma pré-condição de DrawScriptDetails).
    void ScriptGraphWindow::DrawComponentFields(ScriptComponentDef& def, int i)
    {
        if (!m_PreviewScene) return;
        auto& reg = m_PreviewScene->GetRegistry();

        // ── SkeletalMesh: personagem animado (.axeskel + .axeanim) ──────────
        //
        // Aqui moram DOIS assets, de proposito separados: o esqueleto e o
        // grafo. E este painel e o unico lugar do editor onde da pra VER os
        // parametros que o grafo expoe — sem isso, escrever "Speed" num no de
        // script e adivinhacao (e um typo silencioso quebra a transicao).
        if (def.Type == "SkeletalMesh")
        {
            bool changed = false;

            if (AssetPicker::Draw("Skeletal Mesh", def.AssetUUID,
                { AssetType::SkeletalMesh }, [](const AssetRecord&) {}))
                changed = true;

            if (AssetPicker::Draw("Anim Graph", def.AnimGraphUUID,
                { AssetType::AnimGraph }, [](const AssetRecord&) {}))
                changed = true;

            if (ImGui::Checkbox("Mostrar esqueleto", &def.ShowSkeleton))
                changed = true;

            // ── Parametros do AnimGraph ─────────────────────────────────────
            //
            // Lidos do .axeanim escolhido: sao os nomes EXATOS que os nos
            // "Set Anim Float/Bool/Trigger" precisam receber. Clicar copia.
            if (!def.AnimGraphUUID.empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Parametros do grafo");
                ImGui::Separator();

                const AssetRecord* grec = AssetDatabase::Get().GetByUUID(def.AnimGraphUUID);

                if (auto graph = grec ? AnimGraphAsset::LoadFromFile(grec->FilePath) : nullptr)
                {
                    const auto& params = graph->GetParameters();

                    if (params.empty())
                    {
                        ImGui::TextWrapped("Este grafo nao expoe parametros. Crie-os no AnimGraph (painel Parametros).");
                    }
                    else
                    {
                        for (const auto& p : params)
                        {
                            const char* tname =
                                p.Type == AnimParamType::Float ? "Float" :
                                p.Type == AnimParamType::Int ? "Int" :
                                p.Type == AnimParamType::Bool ? "Bool" : "Trigger";

                            ImGui::PushID(p.Name.c_str());

                            if (ImGui::SmallButton("copiar"))
                                ImGui::SetClipboardText(p.Name.c_str());

                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Copia o nome exato pra colar no no\n'Set Anim %s' do grafo de script.", tname);

                            ImGui::SameLine();
                            ImGui::Text("%s", p.Name.c_str());
                            ImGui::SameLine();
                            ImGui::TextDisabled("(%s)", tname);

                            ImGui::PopID();
                        }

                        ImGui::Spacing();
                        ImGui::TextWrapped("Use 'Set Anim Float/Bool' + 'Anim Trigger' no grafo de script para alimentar estes parametros.");
                    }
                }
                else
                {
                    ImGui::TextDisabled("Nao consegui abrir o .axeanim.");
                }
            }

            if (changed)
                SyncComponentsToPreview();
        }
        else if (def.Type == "Mesh")
        {
            static const char* prims[] = { "Sphere","Cube","Cylinder","Plane" };
            static const char* primUUIDs[] = {
                PrimitiveUUID::Sphere, PrimitiveUUID::Cube,
                PrimitiveUUID::Cylinder, PrimitiveUUID::Plane };
            int curIdx = 0;
            for (int p = 0; p < 4; p++)
                if (def.AssetUUID == primUUIDs[p]) { curIdx = p; break; }
            if (ImGui::Combo("Primitive", &curIdx, prims, 4))
            {
                def.AssetUUID = primUUIDs[curIdx];
                auto& mc = reg.get_or_emplace<MeshComponent>(m_PreviewEntity);
                mc.Data = MeshFactory::CreateByUUID(def.AssetUUID);
            }
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
                        mc.AssetUUID = uuid;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        else if (def.Type == "Material")
        {
            AssetPicker::Draw("Material", def.AssetUUID, { AssetType::Material },
                [&](const AssetRecord& record)
                {
                    def.AssetUUID = record.UUID;
                    auto matAsset = MaterialAsset::LoadFromFile(record.FilePath);
                    if (matAsset)
                    {
                        auto graphPath = record.FilePath;
                        graphPath.replace_extension(".axegraph");
                        if (std::filesystem::exists(graphPath))
                        {
                            try {
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

            if (m_InspectorWindow && !def.AssetUUID.empty())
                m_InspectorWindow->DrawMaterialGraphParams(def.AssetUUID, reg, m_PreviewEntity);
            else if (!def.AssetUUID.empty())
            {
                auto* mc = reg.try_get<MaterialComponent>(m_PreviewEntity);
                if (mc && mc->Data)
                {
                    ImGui::DragFloat("Metallic", &mc->Data->Metallic, 0.01f, 0.f, 1.f);
                    ImGui::DragFloat("Roughness", &mc->Data->Roughness, 0.01f, 0.f, 1.f);
                    ImGui::ColorEdit3("Cor Base", glm::value_ptr(mc->Data->Color));
                }
            }
        }
        else if (def.Type == "Rigidbody")
        {
            static const char* types[] = { "Static","Dynamic","Kinematic" };
            int typeIdx = (def.BodyType == "Dynamic") ? 1 : (def.BodyType == "Kinematic") ? 2 : 0;
            if (ImGui::Combo("Tipo", &typeIdx, types, 3)) def.BodyType = types[typeIdx];
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
        else if (def.Type.find("Collider") != std::string::npos)
        {
            static const char* shapes[] = { "Box","Sphere","Capsule","Mesh (Static)","Convex Hull" };
            static const char* shapeKeys[] = { "Box","Sphere","Capsule","Mesh","ConvexHull" };
            int cur = 0;
            for (int s = 0; s < 5; s++)
                if (def.ColliderShape == shapeKeys[s]) { cur = s; break; }
            if (ImGui::Combo("Shape", &cur, shapes, 5)) def.ColliderShape = shapeKeys[cur];
            ImGui::Checkbox("Is Trigger", &def.IsTrigger);
            ImGui::Checkbox("Debug Wireframe", &def.ShowDebug);
            ImGui::DragFloat3("Offset", &def.ColliderOffsetX, 0.01f);
            if (def.ColliderShape == "Box")     ImGui::DragFloat3("Half Extent", &def.ColliderSizeX, 0.01f, 0.01f, 100.f);
            else if (def.ColliderShape == "Sphere")  ImGui::DragFloat("Radius", &def.ColliderRadius, 0.01f, 0.01f, 100.f);
            else if (def.ColliderShape == "Capsule") {
                ImGui::DragFloat("Height", &def.ColliderHeight, 0.01f, 0.1f, 10.f);
                ImGui::DragFloat("Capsule Radius", &def.ColliderCapsuleRadius, 0.01f, 0.01f, 5.f);
            }
            else if (def.ColliderShape == "Mesh") {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.1f, 1));
                ImGui::TextWrapped("Mesh exato. Use apenas com Rigidbody Static.");
                ImGui::PopStyleColor();
            }
        }
        else if (def.Type == "CharacterController")
        {
            // Cada edicao precisa chegar ao componente REAL do preview —
            // o wireframe da capsula e desenhado a partir dele, nao do def.
            // Sem este sync, mexer em Height/Radius/Offset aqui nao mudava
            // NADA na tela (o SkeletalMesh e o SpringArm ja sincronizavam;
            // este painel era o unico que editava o def em silencio).
            bool ccChanged = false;

            if (ImGui::DragFloat("Height", &def.CCHeight, 0.01f, 0.5f, 5.f)) ccChanged = true;
            if (ImGui::DragFloat("Radius", &def.CCRadius, 0.01f, 0.1f, 2.f)) ccChanged = true;
            if (ImGui::DragFloat("Max Slope", &def.CCMaxSlope, 0.5f, 0.f, 89.f)) ccChanged = true;
            if (ImGui::DragFloat("Step Height", &def.CCStepHeight, 0.01f, 0.f, 1.f)) ccChanged = true;
            if (ImGui::DragFloat("Max Speed", &def.CCMaxSpeed, 0.1f, 0.f, 50.f)) ccChanged = true;
            if (ImGui::DragFloat("Jump Force", &def.CCJumpForce, 0.1f, 0.f, 50.f)) ccChanged = true;

            {
                float off[3] = { def.CCOffsetX, def.CCOffsetY, def.CCOffsetZ };

                if (ImGui::DragFloat3("Capsule Offset", off, 0.01f))
                {
                    def.CCOffsetX = off[0];
                    def.CCOffsetY = off[1];
                    def.CCOffsetZ = off[2];
                    ccChanged = true;
                }

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Move a CAPSULA sem mover o personagem.\nEm metros; Y ja parte de meia altura.\nPara pivo nos PES (Mixamo), deixe em 0,0,0.");
            }

            if (ImGui::Checkbox("Debug Wireframe", &def.CCShowDebug)) ccChanged = true;

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Desenha a capsula da fisica na viewport.\nA base dela fica nos PES do personagem.");

            ImGui::Checkbox("Orient Rotation To Movement", &def.CCOrientToMovement);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("O personagem GIRA para a direcao em que anda.\n"
                    "Com isto, a animacao de caminhar PRA FRENTE serve para\n"
                    "todas as direcoes — nao precisa de clipes laterais.");

            if (def.CCOrientToMovement)
                ImGui::DragFloat("Rotation Rate", &def.CCRotationRate, 10.f, 0.f, 2000.f, "%.0f deg/s");

            if (ccChanged)
                SyncComponentsToPreview();
        }
        else if (def.Type == "SpringArm")
        {
            bool armChanged = false;
            if (ImGui::DragFloat("Length##sa", &def.SALength, 1.f, 50.f, 1000.f, "%.0f")) armChanged = true;
            if (ImGui::DragFloat("Height##sa", &def.SAHeightOffset, 0.1f, -10.f, 20.f, "%.2f")) armChanged = true;
            float off[3] = { def.SASocketOffX,def.SASocketOffY,def.SASocketOffZ };
            if (ImGui::DragFloat3("Socket Offset##sa", off, 0.05f))
            {
                def.SASocketOffX = off[0]; def.SASocketOffY = off[1]; def.SASocketOffZ = off[2]; armChanged = true;
            }
            if (ImGui::DragFloat("Smoothing##sa", &def.SALagSpeed, 0.1f, 0.5f, 30.f, "%.1f")) armChanged = true;
            ImGui::Checkbox("Camera Lag##sa", &def.SAEnableLag);
            ImGui::Checkbox("Mouse Rotates##sa", &def.SAMouseRotates);

            auto& pr = m_PreviewScene->GetRegistry();
            auto& sa = pr.get_or_emplace<SpringArmComponent>(m_PreviewEntity);
            sa.Length = def.SALength / 100.0f;
            sa.HeightOffset = def.SAHeightOffset;
            sa.SocketOffset = { def.SASocketOffX, def.SASocketOffY, def.SASocketOffZ };
            sa.LagSpeed = def.SALagSpeed;
            sa.EnableCameraLag = def.SAEnableLag;
            sa.MouseRotates = def.SAMouseRotates;
        }
        else if (def.Type == "Camera")
        {
            ImGui::DragFloat("FOV##cam", &def.CamFov, 0.5f, 10.f, 170.f, "%.1f°");
            ImGui::DragFloat("Near Clip##cam", &def.CamNearClip, 0.01f, 0.001f, 10.f);
            ImGui::DragFloat("Far Clip##cam", &def.CamFarClip, 1.f, 10.f, 10000.f);
            ImGui::DragFloat("Sensibilidade##cam", &def.CamSensitivity, 0.005f, 0.01f, 5.f);
            ImGui::Checkbox("Primary Camera##cam", &def.CamIsPrimary);

            auto& pr = m_PreviewScene->GetRegistry();
            auto& cam = pr.get_or_emplace<CameraComponent>(m_PreviewEntity);
            cam.Fov = def.CamFov;
            cam.NearClip = def.CamNearClip;
            cam.FarClip = def.CamFarClip;
            cam.Sensitivity = def.CamSensitivity;
            cam.IsPrimary = def.CamIsPrimary;
            ImGui::TextDisabled("Parent index: %d", def.ParentIndex);
        }
    }

    void ScriptGraphWindow::DrawVariableDetailsPanel(ScriptVariable& v)
    {
        // Type — combo editável (estava faltando: eu tinha removido o combo
        // do card no Script Members para mover pra cá, mas só copiei o texto
        // estático, não o controle editável de fato).
        static const char* s_TypeNames[] = {
            "Float","Bool","Int","Vec3","String","Vec2","Vec4","Quat","Entity",
            "Float Array","Bool Array","Int Array","Vec3 Array","String Array",
            "Vec2 Array","Vec4 Array","Quat Array","Entity Array",
        };
        int ti = (int)v.Type;
        ImColor tc = GetVariableNodeColor(ti);
        ImVec4 typeCol(tc.Value.x, tc.Value.y, tc.Value.z, 1.f);
        ImGui::TextDisabled("Type:");
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleColor(ImGuiCol_Text, typeCol);
        if (ImGui::Combo("##nd_type", &ti, s_TypeNames, 18))
            v.Type = (ScriptVarType)ti;
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Rename
        ImGui::TextDisabled("Name:");
        ImGui::SetNextItemWidth(-1);
        char nbuf[64] = {};
        strncpy(nbuf, v.Name.c_str(), 63);
        if (ImGui::InputText("##vname_nd", nbuf, 64, ImGuiInputTextFlags_EnterReturnsTrue))
            if (nbuf[0])
            {
                std::string oldName = v.Name;
                v.Name = nbuf;
                if (m_Graph)
                    for (auto& n : m_Graph->GetNodes())
                        if ((n->Name == "Get Variable" || n->Name == "Set Variable") && n->StringValue == oldName)
                            n->StringValue = v.Name;
            }
        ImGui::Spacing();

        // Default value — arrays não têm um valor único editável campo-a-campo
        // (diferente de Float/Vec3/etc.); mostram só o tamanho inicial.
        if (IsArrayType(v.Type))
        {
            ImGui::TextDisabled("Tamanho inicial:");
            ImGui::SetNextItemWidth(-1);
            if (v.DefaultArraySize < 0) v.DefaultArraySize = 0;
            ImGui::DragInt("##nd_arrsize", &v.DefaultArraySize, 1, 0, 256);
        }
        else
        {
            ImGui::TextDisabled("Default Value:");
            ImGui::SetNextItemWidth(-1);
            switch (v.Type)
            {
            case ScriptVarType::Float:  ImGui::DragFloat("##nd_f", &v.DefaultFloat, 0.01f); break;
            case ScriptVarType::Bool:   ImGui::Checkbox("##nd_b", &v.DefaultBool); break;
            case ScriptVarType::Int:    ImGui::DragInt("##nd_i", &v.DefaultInt); break;
            case ScriptVarType::Vec3:
            {
                float gap = 4.f, w = ImGui::GetContentRegionAvail().x;
                float lbl = ImGui::CalcTextSize("X").x;
                float wf = (w - (lbl + 2.f + gap) * 3.f + gap) / 3.f;
                ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("X"); ImGui::SameLine(0, 2);
                ImGui::SetNextItemWidth(wf); ImGui::DragFloat("##nd_x", &v.DefaultVec3[0], 0.01f, 0, 0, "%.3f");
                ImGui::SameLine(0, gap); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Y"); ImGui::SameLine(0, 2);
                ImGui::SetNextItemWidth(wf); ImGui::DragFloat("##nd_y", &v.DefaultVec3[1], 0.01f, 0, 0, "%.3f");
                ImGui::SameLine(0, gap); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Z"); ImGui::SameLine(0, 2);
                ImGui::SetNextItemWidth(wf); ImGui::DragFloat("##nd_z", &v.DefaultVec3[2], 0.01f, 0, 0, "%.3f");
                break;
            }
            case ScriptVarType::String:
            {
                char sbuf[256] = {}; strncpy(sbuf, v.DefaultString.c_str(), 255);
                if (ImGui::InputText("##nd_s", sbuf, 256)) v.DefaultString = sbuf;
                break;
            }
            case ScriptVarType::Entity:
            {
                // ── Entity picker ────────────────────────────────────────────
                // DefaultString guarda o nome da entity referenciada na cena.
                // Exibe botão com o nome atual + dropdown com todas as entities.

                const std::string& current = v.DefaultString;
                const char* label = current.empty() ? "[ Nenhuma ]" : current.c_str();

                // Verifica se a entity ainda existe na cena ativa
                bool entityExists = false;
                if (m_ActiveScene && !current.empty())
                {
                    auto& reg = m_ActiveScene->GetRegistry();
                    reg.view<NameComponent>().each([&](entt::entity e, const NameComponent& nc) {
                        if (nc.Name == current) entityExists = true;
                        });
                }

                // Cor do botão: verde se existe, amarelo se não encontrada, cinza se vazio
                ImVec4 btnCol = current.empty()
                    ? ImVec4(0.25f, 0.25f, 0.25f, 1)
                    : (entityExists
                        ? ImVec4(0.15f, 0.40f, 0.15f, 1)
                        : ImVec4(0.45f, 0.35f, 0.05f, 1));

                ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(btnCol.x + 0.1f, btnCol.y + 0.1f, btnCol.z + 0.1f, 1));

                float bw = ImGui::GetContentRegionAvail().x - 26.f;
                if (ImGui::Button(label, ImVec2(bw, 0)))
                    ImGui::OpenPopup("##entity_picker");
                ImGui::PopStyleColor(2);

                // Botão X para limpar
                if (!current.empty())
                {
                    ImGui::SameLine(0, 2);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                    if (ImGui::SmallButton("x##clrent")) v.DefaultString.clear();
                    ImGui::PopStyleColor(3);
                }

                // Aviso se entity não encontrada na cena
                if (!current.empty() && !entityExists)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.7f, 0.1f, 1));
                    ImGui::TextWrapped("'%s' nao encontrada na cena.", current.c_str());
                    ImGui::PopStyleColor();
                }

                // Drag-and-drop: aceita entidade arrastada do Scene Graph do editor
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY_NAME"))
                        v.DefaultString = (const char*)p->Data;
                    ImGui::EndDragDropTarget();
                }

                // Popup com lista de entities da cena ativa
                ImGui::SetNextWindowSize(ImVec2(220, 280), ImGuiCond_Always);
                if (ImGui::BeginPopup("##entity_picker"))
                {
                    ImGui::TextDisabled("Selecione uma entity:");
                    ImGui::Separator();

                    // Campo de busca
                    static char searchBuf[64] = {};
                    if (ImGui::IsWindowAppearing())
                    {
                        ImGui::SetKeyboardFocusHere();
                        searchBuf[0] = '\0';
                    }
                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputTextWithHint("##entSearch", "Buscar...", searchBuf, sizeof(searchBuf));
                    ImGui::Separator();

                    std::string s = searchBuf;
                    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

                    if (m_ActiveScene)
                    {
                        auto& reg = m_ActiveScene->GetRegistry();
                        reg.view<NameComponent>().each([&](entt::entity e, const NameComponent& nc)
                            {
                                // Filtro de busca
                                std::string low = nc.Name;
                                std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                                if (!s.empty() && low.find(s) == std::string::npos) return;

                                bool selected = (nc.Name == current);
                                if (selected)
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1));

                                if (ImGui::Selectable(nc.Name.c_str(), selected))
                                {
                                    v.DefaultString = nc.Name;
                                    ImGui::CloseCurrentPopup();
                                }

                                if (selected) ImGui::PopStyleColor();
                            });
                    }
                    else
                    {
                        ImGui::TextDisabled("Nenhuma cena ativa.");
                    }
                    ImGui::EndPopup();
                }
                break;
            }
            default: break;
            }
        } // else (não-array)
        ImGui::Spacing();
        ImGui::Checkbox("Exposed (Inspector)##nd_exp", &v.Exposed);

        // Descrição da variável — texto livre, exibida apenas aqui (aba Node)
        ImGui::Spacing();
        ImGui::TextDisabled("Description:");
        static char s_DescBuf[256] = {};
        static std::string s_LastVarNameDesc;
        if (s_LastVarNameDesc != v.Name)
        {
            strncpy(s_DescBuf, v.Description.c_str(), 255);
            s_DescBuf[255] = 0;
            s_LastVarNameDesc = v.Name;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextMultiline("##nd_desc", s_DescBuf, sizeof(s_DescBuf), ImVec2(-1, 60)))
            v.Description = s_DescBuf;

        // Categoria da variável — exibe e permite editar
        ImGui::Spacing();
        ImGui::TextDisabled("Categoria:");
        ImGui::SameLine(0, 4);
        if (v.Category.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
            ImGui::TextUnformatted("(nenhuma)");
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.85f, 1.0f, 1));
            ImGui::TextUnformatted(v.Category.c_str());
            ImGui::PopStyleColor();
        }
        // Campo para mudar categoria diretamente na aba Node
        static char s_CatBuf[64] = {};
        static std::string s_LastVarName;
        if (s_LastVarName != v.Name)
        {
            strncpy(s_CatBuf, v.Category.c_str(), 63);
            s_CatBuf[63] = 0;
            s_LastVarName = v.Name;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##nd_cat", "Definir categoria...", s_CatBuf, 64))
            v.Category = s_CatBuf;

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawScriptDetails()
    {
        if (!m_PreviewScene) return;

        if (ImGui::BeginTabBar("##detailstabs"))
        {
            // ── Tab OBJECT ────────────────────────────────────────────────────
            if (ImGui::BeginTabItem("Object"))
            {
                auto& reg = m_PreviewScene->GetRegistry();

                // Transform
                ImGui::Text("Transform");
                auto* tc = reg.try_get<TransformComponent>(m_PreviewEntity);
                if (tc)
                {
                    bool changed = false;
                    ImGui::TextDisabled("Position");
                    if (DragVec3Labeled("pos", &tc->Data.Position.x, 0.1f)) changed = true;

                    glm::vec3 rotDeg = glm::degrees(tc->Data.Rotation);
                    ImGui::TextDisabled("Rotation");
                    if (DragVec3Labeled("rot", glm::value_ptr(rotDeg), 0.5f))
                    {
                        tc->Data.Rotation = glm::radians(rotDeg); changed = true;
                    }

                    // Scale — com cadeado: fechado = uniforme (qualquer eixo escala
                    // os 3 juntos, proporcionalmente); aberto = cada eixo livre.
                    glm::vec3 scaleCopy = tc->Data.Scale;
                    ImGui::TextDisabled("Scale");
                    ImGui::SameLine(ImGui::GetWindowWidth() - 34.f);
                    {
                        auto& icons = EditorIconLibrary::Get();
                        auto lockIcon = m_ScaleLocked ? icons.GetLockClosed() : icons.GetLockOpen();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
                        bool lockClicked;
                        if (lockIcon && lockIcon->IsLoaded())
                        {
                            lockClicked = ImGui::Button("##scalelock", ImVec2(20, 18));
                            ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
                            ImGui::GetWindowDrawList()->AddImage(
                                (ImTextureID)(uintptr_t)lockIcon->GetRendererID(),
                                ImVec2(r0.x + 3, r0.y + 1), ImVec2(r1.x - 3, r1.y - 1),
                                ImVec2(0, 1), ImVec2(1, 0));
                        }
                        else
                        {
                            lockClicked = ImGui::SmallButton(m_ScaleLocked ? "L" : "U");
                        }
                        ImGui::PopStyleColor(2);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(m_ScaleLocked
                                ? "Escala uniforme (clique para liberar por eixo)"
                                : "Escala livre por eixo (clique para travar uniforme)");
                        if (lockClicked) m_ScaleLocked = !m_ScaleLocked;
                    }

                    if (DragVec3Labeled("scale", &scaleCopy.x, 0.05f))
                    {
                        if (m_ScaleLocked)
                        {
                            // Identifica qual eixo mudou e aplica o mesmo fator aos outros 2
                            float old[3] = { tc->Data.Scale.x, tc->Data.Scale.y, tc->Data.Scale.z };
                            float neu[3] = { scaleCopy.x, scaleCopy.y, scaleCopy.z };
                            int axis = 0; float bestDelta = 0.f;
                            for (int a = 0; a < 3; a++)
                            {
                                float d = std::abs(neu[a] - old[a]);
                                if (d > bestDelta) { bestDelta = d; axis = a; }
                            }
                            float ratio = (old[axis] > 0.0001f) ? (neu[axis] / old[axis]) : 1.0f;
                            tc->Data.Scale.x = std::max(old[0] * ratio, 0.001f);
                            tc->Data.Scale.y = std::max(old[1] * ratio, 0.001f);
                            tc->Data.Scale.z = std::max(old[2] * ratio, 0.001f);
                        }
                        else
                        {
                            tc->Data.Scale.x = std::max(scaleCopy.x, 0.001f);
                            tc->Data.Scale.y = std::max(scaleCopy.y, 0.001f);
                            tc->Data.Scale.z = std::max(scaleCopy.z, 0.001f);
                        }
                        changed = true;
                    }
                    if (changed)
                    {
                        tc->Data.UseWorldMatrix = false;
                        tc->Data.WorldMatrix = tc->Data.GetMatrix();

                        // Persiste no ASSET: este transform e autoria (a
                        // escala do personagem Mixamo, por exemplo), nao
                        // estado do preview. Sem isto ele sumia no Save e
                        // nunca chegava na cena.
                        if (m_ScriptAsset)
                        {
                            m_ScriptAsset->RootPosX = tc->Data.Position.x;
                            m_ScriptAsset->RootPosY = tc->Data.Position.y;
                            m_ScriptAsset->RootPosZ = tc->Data.Position.z;

                            const glm::vec3 deg = glm::degrees(tc->Data.Rotation);
                            m_ScriptAsset->RootRotX = deg.x;
                            m_ScriptAsset->RootRotY = deg.y;
                            m_ScriptAsset->RootRotZ = deg.z;

                            m_ScriptAsset->RootScaleX = tc->Data.Scale.x;
                            m_ScriptAsset->RootScaleY = tc->Data.Scale.y;
                            m_ScriptAsset->RootScaleZ = tc->Data.Scale.z;
                        }
                    }
                }

                // Componentes do ScriptAsset
                if (m_ScriptAsset)
                {
                    ImGui::TextDisabled("Arraste o nome para dentro de outro  |  solte em Transform para tirar de dentro");

                    auto& comps = m_ScriptAsset->GetComponents();
                    for (int i = 0; i < (int)comps.size(); i++)
                    {
                        auto& def = comps[i];
                        ImGui::PushID(i);
                        ImGui::Separator(); ImGui::Spacing();

                        float available = ImGui::GetContentRegionAvail().x;
                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.25f, 1));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.32f, 1));
                        bool open = ImGui::CollapsingHeader(
                            (def.Type + "##hdr_" + std::to_string(i)).c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
                        ImGui::PopStyleColor(2);

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
                            ImGui::PopID(); break;
                        }

                        if (open)
                        {
                            DrawComponentFields(def, i);
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTabItem();
            }

            // ── Tab NODE ──────────────────────────────────────────────────────
            if (ImGui::BeginTabItem("Node"))
            {
                // Prioridade: variável selecionada diretamente na lista do
                // Script Members (m_SelectedVar) é mostrada sem depender de
                // nenhum node existir no canvas — diferente do fluxo abaixo,
                // que só funciona se houver um node Get/Set Variable
                // selecionado no grafo.
                if (m_SelectedVar >= 0 && m_ScriptAsset &&
                    m_SelectedVar < (int)m_ScriptAsset->GetVariables().size())
                {
                    DrawVariableDetailsPanel(m_ScriptAsset->GetVariables()[m_SelectedVar]);
                }
                else if (m_EdCtx)
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
                            std::string dispName = node->Name;
                            if (!node->StringValue.empty() &&
                                (node->Name == "Get Variable" || node->Name == "Set Variable"))
                                dispName = (node->Name == "Get Variable" ? "Get " : "Set ") + node->StringValue;

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

                            bool isVarNode = (node->Name == "Get Variable" || node->Name == "Set Variable")
                                && !node->StringValue.empty() && m_ScriptAsset;

                            if (isVarNode)
                            {
                                ScriptVariable* foundVar = nullptr;
                                for (auto& v : m_ScriptAsset->GetVariables())
                                    if (v.Name == node->StringValue) { foundVar = &v; break; }

                                if (!foundVar)
                                {
                                    ImGui::TextColored({ 0.9f,0.3f,0.3f,1 }, "Variable '%s' not found.", node->StringValue.c_str());
                                    ImGui::TextDisabled("Declare it in Script Members.");
                                    ImGui::Spacing();
                                }
                                else if (node->Name == "Set Variable")
                                {
                                    // BUGFIX: este painel chamava DrawVariableDetailsPanel
                                    // aqui, que edita o DEFAULT GLOBAL da variável (mesmo
                                    // campo lido por todo Get Variable e por qualquer
                                    // outro Set da mesma variável) — selecionar ESTE Set e
                                    // digitar um valor aqui mudava silenciosamente o
                                    // default de TODA a variável em todo lugar, incluindo
                                    // nodes Get. Igual ao bug já corrigido na caixinha
                                    // inline do canvas (e usa o mesmo helper agora —
                                    // DrawSetVariableLocalValueEditor — para não duplicar
                                    // o switch por tipo em dois arquivos). O default global
                                    // só deve ser editado selecionando a VARIÁVEL na lista
                                    // do Script Members, nunca a partir de um node Get/Set.
                                    ImGui::TextDisabled("Type:");
                                    ImGui::TextUnformatted(ScriptVarTypeToString(foundVar->Type).c_str());
                                    ImGui::Spacing();
                                    ImGui::TextDisabled("Name:");
                                    ImGui::TextUnformatted(foundVar->Name.c_str());
                                    ImGui::Spacing();
                                    ImGui::TextDisabled("Value (apenas deste node):");

                                    bool hasConnection = false;
                                    for (auto& link : m_Graph->GetLinks())
                                        for (auto& p : node->Inputs)
                                            if (p.Name == "Value" && (link.StartPin == p.ID || link.EndPin == p.ID))
                                            {
                                                hasConnection = true; break;
                                            }

                                    if (hasConnection)
                                        ImGui::TextDisabled("(pino Value conectado — valor vem do link)");
                                    else
                                        DrawSetVariableLocalValueEditor(node, foundVar->Type, -1.f);
                                    ImGui::Spacing();
                                }
                                else // Get Variable — sem valor local próprio (Get só lê em
                                     // runtime), então aqui mostra/edita o default GLOBAL da
                                     // variável (mesmo comportamento da Unreal: selecionar um
                                     // node Get nos Details edita o Default Value, idêntico a
                                     // selecionar a variável na lista My Blueprint — é o MESMO
                                     // valor por definição, então não há "vazamento" aqui).
                                {
                                    DrawVariableDetailsPanel(*foundVar);
                                }
                            }
                            else if (node->Name == "Get Action" || node->Name == "Get Axis" || node->Name == "Print String")
                            {
                                if (node->Name == "Print String")
                                {
                                    ImGui::TextDisabled("Message:");
                                    ImGui::SetNextItemWidth(-1);
                                    char buf[128]; strncpy(buf, node->StringValue.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = '\0';
                                    if (ImGui::InputText("##sv", buf, sizeof(buf))) node->StringValue = buf;
                                }
                                else
                                {
                                    // Get Action / Get Axis — mesmo combo do canvas
                                    // (script_node_draw.cpp), alimentado pelo InputMappingConfig.
                                    bool isGetAction = (node->Name == "Get Action");
                                    auto& cfg = axe::InputMappingConfig::Get();
                                    std::vector<std::string> names;
                                    if (isGetAction)
                                        for (auto& a : cfg.GetActions()) names.push_back(a.Name);
                                    else
                                        for (auto& a : cfg.GetAxes()) names.push_back(a.Name);

                                    int curIdx = -1;
                                    for (int i = 0; i < (int)names.size(); i++)
                                        if (names[i] == node->StringValue) { curIdx = i; break; }

                                    ImGui::TextDisabled(isGetAction ? "Action:" : "Axis:");
                                    ImGui::SetNextItemWidth(-1);
                                    const char* lbl = (curIdx >= 0) ? names[curIdx].c_str()
                                        : (names.empty() ? "(nenhuma configurada)" : "(selecione)");
                                    if (ImGui::BeginCombo("##detailsv", lbl))
                                    {
                                        for (int i = 0; i < (int)names.size(); i++)
                                        {
                                            bool sel = (i == curIdx);
                                            if (ImGui::Selectable(names[i].c_str(), sel))
                                            {
                                                node->StringValue = names[i];
                                                if (!isGetAction && m_Graph)
                                                {
                                                    auto* axis = cfg.FindAxis(names[i]);
                                                    if (axis) m_Graph->RebuildAxisOutputPins(node, (int)axis->ValueType);
                                                }
                                            }
                                            if (sel) ImGui::SetItemDefaultFocus();
                                        }
                                        if (names.empty())
                                            ImGui::TextDisabled("Configure em Project > Input Settings");
                                        ImGui::EndCombo();
                                    }
                                }
                                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                            }

                            // ── Parametro do AnimGraph, tambem aqui ──────────
                            //
                            // Pedido dele: "seria interessante ter nos dois,
                            // node e script details". Mesma funcao usada pelo
                            // popup do canvas, entao as duas telas nao tem como
                            // divergir.
                            {
                                int wantType = 0;
                                if (ScriptPin* paramPin = FindAnimParamPin(node, &wantType))
                                {
                                    ImGui::TextDisabled("Parametro do AnimGraph:");
                                    ImGui::SetNextItemWidth(-1);

                                    const std::string prev = paramPin->DefaultString.empty()
                                        ? std::string("(escolher)") : paramPin->DefaultString;

                                    if (ImGui::BeginCombo("##animparamdet", prev.c_str()))
                                    {
                                        DrawAnimParamList(paramPin, wantType);
                                        ImGui::EndCombo();
                                    }

                                    if (!paramPin->DefaultString.empty())
                                    {
                                        bool known = false;
                                        for (const auto& pr : CollectAnimGraphParams(false))
                                            if (pr.first == paramPin->DefaultString) { known = true; break; }

                                        if (!known)
                                            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f),
                                                "nao existe no grafo");
                                    }

                                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                                }
                            }

                            // Pins listados
                            for (auto& p : node->Inputs)
                            {
                                ImColor pc = GetPinColor(p.Type);
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(pc.Value.x, pc.Value.y, pc.Value.z, 1));
                                ImGui::BulletText("In: %s", p.Name.c_str());
                                ImGui::PopStyleColor();
                            }
                            for (auto& p : node->Outputs)
                            {
                                ImColor pc = GetPinColor(p.Type);
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(pc.Value.x, pc.Value.y, pc.Value.z, 1));
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

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawSceneGraphWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (ImGui::Begin("Scene Graph"))
        {
            // Cabeçalho — badge de tipo + nome
            if (m_ScriptAsset)
            {
                static const struct { ScriptClassType t; ImVec4 col; const char* icon; } badges[] = {
                    {ScriptClassType::Entity,       {0.4f,0.7f,1.f,1},  "Entity"      },
                    {ScriptClassType::Agent,        {0.3f,1.f,0.5f,1},  "Agent"       },
                    {ScriptClassType::Character,    {1.f,0.7f,0.2f,1},  "Character"   },
                    {ScriptClassType::StaticObject, {0.7f,0.7f,0.7f,1}, "StaticObject"},
                    {ScriptClassType::Trigger,      {1.f,0.4f,0.8f,1},  "Trigger"     },
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

            // Botão Add Component — compacto (ícone + "Add"), tooltip completo no hover
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.15f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.2f, 1));
            {
                auto addIcon = EditorIconLibrary::Get().GetAdd();
                const char* shortLabel = "Add";
                bool clicked;
                if (addIcon && addIcon->IsLoaded())
                {
                    float iconSz = 14.f;
                    float textW = ImGui::CalcTextSize(shortLabel).x;
                    ImVec2 btnSz(iconSz + 6.f + textW + 16.f, 0); // botão compacto, não mais full-width
                    clicked = ImGui::Button("##addcompbtn", btnSz);
                    ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float cy = (r0.y + r1.y) * 0.5f;
                    dl->AddImage((ImTextureID)(uintptr_t)addIcon->GetRendererID(),
                        ImVec2(r0.x + 8.f, cy - iconSz * 0.5f), ImVec2(r0.x + 8.f + iconSz, cy + iconSz * 0.5f),
                        ImVec2(0, 1), ImVec2(1, 0));
                    dl->AddText(ImVec2(r0.x + 8.f + iconSz + 6.f, cy - ImGui::GetFontSize() * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), shortLabel);
                }
                else
                {
                    clicked = ImGui::Button(shortLabel);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Add Component");
                if (clicked) ImGui::OpenPopup("##addcomp");
            }
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
                    {"Mesh",           "Mesh",                "Malha 3D do objeto",        {0.6f,0.9f,1.f,1}},
                    {"Skeletal Mesh",  "SkeletalMesh",        "Personagem animado (.axeskel + .axeanim)", {0.5f,0.85f,0.7f,1}},
                    {"Material",       "Material",            "Material PBR",               {1.f,0.7f,0.4f,1}},
                    {"Rigidbody",      "Rigidbody",           "Fisica dinamica",            {0.3f,0.7f,1.f,1}},
                    {"Collider",       "Collider",            "Colisao (Box/Sphere/Capsule)",{0.3f,1.f,0.5f,1}},
                    {"Character Ctrl", "CharacterController", "Character controller",       {1.f,0.7f,0.2f,1}},
                    {"Point Light",    "PointLight",          "Luz pontual",                {1.f,0.9f,0.3f,1}},
                    {"Spring Arm",     "SpringArm",           "Camera boom arm",            {0.9f,0.6f,1.f,1}},
                    {"Camera",         "Camera",              "Camera de jogo",             {0.7f,0.5f,1.f,1}},
                };
                for (auto& c : comps)
                {
                    std::string low = c.name;
                    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                    if (!s.empty() && low.find(s) == std::string::npos) continue;
                    ImGui::PushStyleColor(ImGuiCol_Text, c.col);
                    if (ImGui::MenuItem(c.name) && m_ScriptAsset)
                    {
                        ScriptComponentDef def; def.Type = c.type;
                        PushUndo("Add Component");
                        m_ScriptAsset->AddComponent(def);
                        CommitUndo("Add Component");
                        SyncComponentsToPreview();
                        m_ConsoleLines.push_back(std::string("[Info] Component added: ") + c.name);
                        m_CompSearchBuf[0] = '\0'; ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.desc);
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (m_ScriptAsset)
            {
                auto& comps = m_ScriptAsset->GetComponents();
                int removeIdx = -1;

                // Transform — sempre presente
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1));
                ImGui::Selectable("  Transform", false, ImGuiSelectableFlags_None,
                    ImVec2(ImGui::GetContentRegionAvail().x - 28.f, 0));
                ImGui::PopStyleColor();
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    std::string payload = "GetTransform";
                    ImGui::SetDragDropPayload("COMP_NODE", payload.c_str(), payload.size() + 1);
                    ImGui::TextUnformatted("Transform → Graph");
                    ImGui::EndDragDropSource();
                }

                // Soltar um componente AQUI o tira da hierarquia (vira raiz).
                //
                // Sem isto, sair de dentro de outro componente so era
                // possivel pelo botao "^" — ou apagando o componente. Como o
                // pai e guardado por INDICE, nao existe alvo natural para
                // "fora"; o Transform e a raiz de todos, entao ele e o lugar
                // que faz sentido.
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("COMP_DRAG"))
                    {
                        const int idx = ((const ComponentDragPayload*)p->Data)->Index;

                        if (idx >= 0 && idx < (int)comps.size())
                        {
                            comps[idx].ParentIndex = -1;
                            SyncComponentsToPreview();
                        }
                    }

                    ImGui::EndDragDropTarget();
                }

                auto getColor = [](const std::string& t) -> ImVec4 {
                    if (t == "Mesh")           return { 0.6f,0.9f,1.f,1 };
                    if (t == "Material")       return { 1.f,0.7f,0.4f,1 };
                    if (t == "Rigidbody")      return { 0.3f,0.7f,1.f,1 };
                    if (t.find("Collider") != std::string::npos) return { 0.3f,1.f,0.5f,1 };
                    if (t == "CharacterController") return { 1.f,0.7f,0.2f,1 };
                    if (t == "SpringArm")      return { 0.9f,0.6f,1.f,1 };
                    if (t == "Camera")         return { 0.7f,0.5f,1.f,1 };
                    return { 0.85f,0.85f,0.85f,1 };
                    };
                // Ícones vetoriais reais (PNG outline, ver EditorIconLibrary) em vez
                // do antigo texto [M]/[R]/[C]/[P]/[A]/[CAM] — alinhado ao estilo UE5.
                auto getIconTex = [](const std::string& t) -> std::shared_ptr<Texture2D> {
                    auto& icons = EditorIconLibrary::Get();
                    if (t == "Mesh")           return icons.GetMesh();
                    if (t == "Material")       return icons.GetMaterial();
                    if (t == "Rigidbody")      return icons.GetRigidbody();
                    if (t.find("Collider") != std::string::npos) return icons.GetCollider();
                    if (t == "CharacterController") return icons.GetCharacterController();
                    if (t == "SpringArm")      return icons.GetSpringArm();
                    if (t == "Camera")         return icons.GetCamera();
                    if (t == "Light")          return icons.GetDirectionalLight();
                    return nullptr; // sem ícone dedicado — cai no fallback de texto "?"
                    };

                auto drawComp = [&](int i, float indent) {
                    auto& def = comps[i];
                    ImVec4 col = getColor(def.Type);
                    bool isSelected = (m_SelectedCompIndex == i);
                    ImGui::PushID(i);
                    if (indent > 0) ImGui::Indent(indent);

                    // Aceita a soltura do reparent NO ULTIMO item submetido.
                    // Chamado depois de mais de um item da linha (hitbox e
                    // nome), de proposito: com overlap liberado, qual deles
                    // fica com o hover depende de onde o cursor esta, entao
                    // os dois precisam aceitar. Alvos redundantes nao se
                    // atrapalham — so um recebe o payload.
                    auto acceptReparent = [&]() {
                        if (!ImGui::BeginDragDropTarget())
                            return;

                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("COMP_DRAG"))
                        {
                            const int childIdx = ((const ComponentDragPayload*)p->Data)->Index;

                            // Proibe ciclo: nao da pra ser filho de si mesmo
                            // nem de um descendente seu (a arvore viraria um
                            // anel e o desenho recursivo entraria em loop).
                            bool cycle = (childIdx == i);

                            for (int p2 = comps[i].ParentIndex; p2 >= 0 && !cycle; )
                            {
                                if (p2 == childIdx) cycle = true;
                                else p2 = comps[p2].ParentIndex;
                            }

                            if (!cycle && childIdx < (int)comps.size())
                            {
                                comps[childIdx].ParentIndex = i;
                                SyncComponentsToPreview();
                            }
                        }

                        ImGui::EndDragDropTarget();
                        };

                    // ── Card estilo UE5: fundo arredondado + respiro vertical ──
                    float cardWidth = ImGui::GetContentRegionAvail().x;
                    float cardHeight = 34.f;
                    ImVec2 cardMin = ImGui::GetCursorScreenPos();
                    ImVec2 cardMax = ImVec2(cardMin.x + cardWidth, cardMin.y + cardHeight);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 cardBg = isSelected
                        ? ImGui::ColorConvertFloat4ToU32(ImVec4(col.x * 0.30f, col.y * 0.30f, col.z * 0.30f, 0.55f))
                        : ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.04f));
                    dl->AddRectFilled(cardMin, cardMax, cardBg, 6.0f);
                    if (isSelected)
                        dl->AddRect(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, 0.65f)), 6.0f, 0, 1.5f);

                    // ── Hitbox da linha inteira ───────────────────────────
                    //
                    // Um item invisivel do tamanho do card, submetido ANTES
                    // do conteudo, para que QUALQUER ponto da linha aceite a
                    // soltura do reparent.
                    //
                    // SetNextItemAllowOverlap e OBRIGATORIO aqui. Sem ele o
                    // ImGui da o hover para o PRIMEIRO item de uma regiao
                    // sobreposta e bloqueia todos os desenhados depois — foi
                    // o que aconteceu na V2: esta hitbox engoliu a alca, o
                    // nome e os botoes, e nada mais respondia. Nao existe
                    // regra de "o ultimo item vence"; a sobreposicao precisa
                    // ser pedida.
                    ImGui::SetNextItemAllowOverlap();
                    ImGui::InvisibleButton("##row", ImVec2(cardWidth, cardHeight));
                    acceptReparent();

                    // Realce da linha sob o cursor durante o arrasto. Usa o
                    // retangulo direto, e nao IsItemHovered: com overlap
                    // liberado, a hitbox NAO conta como hovered quando o
                    // cursor esta sobre um dos widgets por cima dela.
                    if (const ImGuiPayload* drag = ImGui::GetDragDropPayload())
                    {
                        if (drag->IsDataType("COMP_DRAG")
                            && ImGui::IsMouseHoveringRect(cardMin, cardMax)
                            && ((const ComponentDragPayload*)drag->Data)->Index != i)
                        {
                            dl->AddRect(cardMin, cardMax,
                                ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.8f, 1.f, 0.9f)),
                                6.0f, 0, 2.0f);
                        }
                    }

                    // Volta o cursor para o inicio do card: o conteudo e
                    // desenhado POR CIMA da hitbox.
                    ImGui::SetCursorScreenPos(cardMin);

                    ImGui::Dummy(ImVec2(8, cardHeight)); // respiro esquerdo dentro do card
                    ImGui::SameLine(0, 0);

                    ImGui::BeginGroup();
                    ImGui::Dummy(ImVec2(0, (cardHeight - 20.f) * 0.5f)); // centraliza verticalmente

                    // Collapse
                    bool& collapsed = m_CompCollapsed[i < 32 ? i : 0];
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
                    if (ImGui::SmallButton(collapsed ? ">" : "v")) collapsed = !collapsed;
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine(0, 6);

                    // Ícone vetorial real (20x20) com fallback de texto "?"
                    auto iconTex = getIconTex(def.Type);
                    if (iconTex && iconTex->IsLoaded())
                    {
                        ImGui::Image((ImTextureID)(uintptr_t)iconTex->GetRendererID(),
                            ImVec2(20, 20), ImVec2(0, 1), ImVec2(1, 0));
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, col);
                        ImGui::Dummy(ImVec2(20, 20));
                        ImVec2 p = ImGui::GetItemRectMin();
                        dl->AddText(ImVec2(p.x + 6, p.y + 2), ImGui::ColorConvertFloat4ToU32(col), "?");
                        ImGui::PopStyleColor();
                    }
                    ImGui::SameLine(0, 8);

                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.06f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.06f));
                    if (ImGui::Selectable(def.Type.c_str(), false, ImGuiSelectableFlags_None,
                        ImVec2(cardWidth - 84.f, 20)))
                        m_SelectedCompIndex = i;
                    ImGui::PopStyleColor(3);

                    // ── Arrasto do componente: UM gesto, dois destinos ────
                    //
                    // Antes existia uma alca "::" so para reparentar, porque
                    // o ImGui aceita um unico DragDropSource por item e o
                    // nome ja arrastava para o grafo. Agora o payload leva
                    // as DUAS informacoes (indice + node), e quem recebe
                    // escolhe o que usar: o canvas cria o node, uma linha de
                    // componente reparenta. Um gesto so, o mesmo que ele ja
                    // usava e que ja funcionava.
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        ComponentDragPayload drag;
                        drag.Index = i;

                        std::string node;
                        if (def.Type == "Rigidbody")                             node = "GetRigidbody";
                        else if (def.Type.find("Collider") != std::string::npos)      node = "GetCollider";
                        else if (def.Type == "CharacterController")                   node = "GetCharacterController";
                        else if (def.Type == "SpringArm")                             node = "GetSpringArm";
                        else if (def.Type == "Camera")                                node = "GetCamera";

                        // Cabe? Node[64] contra nomes de ~24 chars — sobra,
                        // mas o truncamento explicito evita surpresa se um
                        // nome novo crescer.
                        std::snprintf(drag.Node, sizeof(drag.Node), "%s", node.c_str());

                        ImGui::SetDragDropPayload("COMP_DRAG", &drag, sizeof(drag));
                        ImGui::PushStyleColor(ImGuiCol_Text, col);

                        if (node.empty())
                            ImGui::Text("Mover %s", def.Type.c_str());
                        else
                            ImGui::Text("%s → grafo ou para dentro de outro", def.Type.c_str());

                        ImGui::PopStyleColor();
                        ImGui::EndDragDropSource();
                    }

                    // O nome tambem aceita a soltura: quando o cursor esta
                    // sobre ele, e ELE que tem o hover, nao a hitbox.
                    acceptReparent();

                    // Desparentar: com o pai definido por INDICE, arrastar de
                    // volta pra raiz nao tem alvo natural — um botao resolve
                    // e e mais descobrivel que qualquer gesto.
                    if (def.ParentIndex >= 0)
                    {
                        ImGui::SameLine();

                        // O nome do pai e lido ANTES do clique. Depois de
                        // clicar, ParentIndex ja vale -1 no mesmo frame, e o
                        // tooltip (que ainda e avaliado, porque o cursor
                        // segue sobre o botao) fazia comps[-1] — indice sem
                        // sinal enorme, acesso fora de faixa, crash. O
                        // ponteiro continua valido depois do clique: so um
                        // int mudou, a string do pai nao foi tocada.
                        //
                        // A faixa tambem e checada: um ParentIndex velho,
                        // sobrevivente de uma remocao, nao pode derrubar o
                        // editor inteiro por causa de um tooltip.
                        const bool parentOk = def.ParentIndex >= 0
                            && def.ParentIndex < (int)comps.size();
                        const char* parentName = parentOk
                            ? comps[def.ParentIndex].Type.c_str() : "?";

                        if (ImGui::SmallButton("^"))
                        {
                            def.ParentIndex = -1;
                            SyncComponentsToPreview();
                        }

                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Tirar de dentro de '%s' (volta a ser raiz)", parentName);
                    }

                    ImGui::EndGroup();
                    ImGui::SameLine();

                    // X — alinhado à direita do card, mesma centralização vertical
                    ImGui::SetCursorScreenPos(ImVec2(cardMax.x - 26.f, cardMin.y + (cardHeight - 20.f) * 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                    if (ImGui::SmallButton("x")) removeIdx = i;
                    ImGui::PopStyleColor(3);

                    ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y + 3.f)); // espaçamento entre cards

                    if (indent > 0) ImGui::Unindent(indent);
                    ImGui::PopID();
                    };

                // "v"/">" controla a visibilidade dos FILHOS deste componente (ex.:
                // Camera dentro de SpringArm), não campos de propriedades — esses
                // ficam só no Script Details, para não duplicar dado editável em
                // dois lugares ao mesmo tempo.
                // Desenho RECURSIVO: cada filho sai logo abaixo do proprio
                // pai, com um nivel de indentacao a mais.
                //
                // Antes eram duas passadas planas — primeiro todas as raizes,
                // depois TODOS os filhos de qualquer pai, em ordem de indice.
                // Um filho do SpringArm acabava desenhado depois do Material
                // e parecia filho DELE: a hierarquia real estava certa, o
                // desenho e que mentia. Era exatamente o "comportamento
                // visual" que ele descreveu.
                // Blindagem do desenho recursivo. O ParentIndex e um INDICE
                // no vector, e indice envelhece mal: uma remocao antiga ou
                // um arquivo salvo por uma versao anterior pode deixar um
                // valor fora de faixa, ou ate um ciclo. Nada disso pode
                // derrubar o editor nem sumir com um componente da lista.
                std::vector<bool> drawn(comps.size(), false);

                auto parentOf = [&](int i) {
                    const int p = comps[i].ParentIndex;
                    return (p >= 0 && p < (int)comps.size() && p != i) ? p : -1;
                    };

                std::function<void(int, float)> drawBranch = [&](int i, float indent) {
                    if (i < 0 || i >= (int)comps.size() || drawn[i]) return;
                    drawn[i] = true;

                    drawComp(i, indent);

                    const bool collapsed = (i >= 0 && i < 32) ? m_CompCollapsed[i] : false;
                    if (collapsed) return;

                    for (int c = 0; c < (int)comps.size(); ++c)
                        if (parentOf(c) == i)
                            drawBranch(c, indent + 16.f);
                    };

                // Raiz = sem pai, ou com pai invalido (orfao). Sem o segundo
                // caso o componente simplesmente nao apareceria em lugar
                // nenhum e pareceria ter sumido.
                for (int i = 0; i < (int)comps.size(); i++)
                    if (parentOf(i) < 0) drawBranch(i, 0.f);

                // Rede de seguranca: se um ciclo residual deixou alguem de
                // fora, ele aparece na raiz em vez de desaparecer.
                for (int i = 0; i < (int)comps.size(); i++)
                    if (!drawn[i]) drawBranch(i, 0.f);

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
                    ImGui::BulletText("%s", n); ImGui::PopStyleColor();
                    };
                if (reg.all_of<TransformComponent>(m_Entity))           dc("Transform", { 0.8f,0.8f,0.8f,1 });
                if (reg.all_of<MeshComponent>(m_Entity))                dc("Mesh", { 0.6f,0.9f,1.f, 1 });
                if (reg.all_of<MaterialComponent>(m_Entity))            dc("Material", { 1.f, 0.7f,0.4f,1 });
                if (reg.all_of<RigidbodyComponent>(m_Entity))           dc("Rigidbody", { 0.4f,0.8f,1.f, 1 });
                if (reg.all_of<ColliderComponent>(m_Entity))            dc("Collider", { 0.4f,1.f, 0.6f,1 });
                if (reg.all_of<CharacterControllerComponent>(m_Entity)) dc("CharacterController", { 1.f, 0.8f,0.2f,1 });
                if (reg.all_of<LightComponent>(m_Entity))               dc("Light", { 1.f, 0.95f,0.4f,1 });
                if (reg.all_of<CameraComponent>(m_Entity))              dc("Camera", { 0.7f,0.5f,1.f, 1 });
                if (reg.all_of<ScriptComponent>(m_Entity))              dc("Script", { 1.f, 0.5f,0.7f,1 });
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextDisabled("Nodes: %d", m_Graph ? (int)m_Graph->GetNodes().size() : 0);
            ImGui::TextDisabled("Links:  %d", m_Graph ? (int)m_Graph->GetLinks().size() : 0);
            // Botão Save foi movido para a toolbar (ao lado de "Compilar"), em
            // script_graph_window.cpp — a pedido, para ficar mais acessível.
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace axe