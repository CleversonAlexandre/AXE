// script_details.cpp
// DrawDetailsWindow / DrawScriptDetails / DrawSceneGraphWindow
// Painel de detalhes: tab Object (Transform + componentes) e tab Node (node selecionado).
// DrawSceneGraphWindow: hierarquia de componentes do script, add/remove, drag para graph.

#include "script_graph_window.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/scene/components.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/material/material_compiler.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/asset/asset_database.hpp"
#include "editor/axe_editor/asset/asset_picker.hpp"
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace axe
{
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

                // Componentes do ScriptAsset
                if (m_ScriptAsset)
                {
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
                            if (def.Type == "Mesh")
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
                                ImGui::DragFloat("Height", &def.CCHeight, 0.01f, 0.5f, 5.f);
                                ImGui::DragFloat("Radius", &def.CCRadius, 0.01f, 0.1f, 2.f);
                                ImGui::DragFloat("Max Slope", &def.CCMaxSlope, 0.5f, 0.f, 89.f);
                                ImGui::DragFloat("Step Height", &def.CCStepHeight, 0.01f, 0.f, 1.f);
                                ImGui::DragFloat("Max Speed", &def.CCMaxSpeed, 0.1f, 0.f, 50.f);
                                ImGui::DragFloat("Jump Force", &def.CCJumpForce, 0.1f, 0.f, 50.f);
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
                        ImGui::PopID();
                    }
                }
                ImGui::EndTabItem();
            }

            // ── Tab NODE ──────────────────────────────────────────────────────
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

                                if (foundVar)
                                {
                                    static const char* s_TypeNames[] = { "Float","Bool","Int","Vec3","String","Vec2","Vec4","Quat","Entity" };
                                    static const ImVec4 s_TypeCols[] = {
                                        {0.2f,0.7f,0.3f,1},{0.9f,0.2f,0.2f,1},{0.4f,0.9f,0.4f,1},
                                        {1.f,0.9f,0.2f,1},{0.95f,0.4f,0.7f,1},{0.16f,0.78f,0.78f,1},
                                        {0.63f,0.31f,0.86f,1},{0.71f,0.55f,0.86f,1},{0.24f,0.47f,0.78f,1}
                                    };
                                    int ti = (int)foundVar->Type;
                                    ImGui::PushStyleColor(ImGuiCol_Text, s_TypeCols[ti]);
                                    ImGui::TextUnformatted(s_TypeNames[ti]);
                                    ImGui::PopStyleColor();
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(foundVar->Name.c_str());
                                    ImGui::Spacing();

                                    // Rename
                                    ImGui::TextDisabled("Name:");
                                    ImGui::SetNextItemWidth(-1);
                                    char nbuf[64] = {};
                                    strncpy(nbuf, foundVar->Name.c_str(), 63);
                                    if (ImGui::InputText("##vname_nd", nbuf, 64, ImGuiInputTextFlags_EnterReturnsTrue))
                                        if (nbuf[0])
                                        {
                                            std::string oldName = foundVar->Name;
                                            foundVar->Name = nbuf;
                                            if (m_Graph)
                                                for (auto& n : m_Graph->GetNodes())
                                                    if ((n->Name == "Get Variable" || n->Name == "Set Variable") && n->StringValue == oldName)
                                                        n->StringValue = foundVar->Name;
                                        }
                                    ImGui::Spacing();

                                    // Default value
                                    ImGui::TextDisabled("Default Value:");
                                    ImGui::SetNextItemWidth(-1);
                                    switch (foundVar->Type)
                                    {
                                    case ScriptVarType::Float:  ImGui::DragFloat("##nd_f", &foundVar->DefaultFloat, 0.01f); break;
                                    case ScriptVarType::Bool:   ImGui::Checkbox("##nd_b", &foundVar->DefaultBool); break;
                                    case ScriptVarType::Int:    ImGui::DragInt("##nd_i", &foundVar->DefaultInt); break;
                                    case ScriptVarType::Vec3:
                                    {
                                        float gap = 4.f, w = ImGui::GetContentRegionAvail().x;
                                        float lbl = ImGui::CalcTextSize("X").x;
                                        float wf = (w - (lbl + 2.f + gap) * 3.f + gap) / 3.f;
                                        ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("X"); ImGui::SameLine(0, 2);
                                        ImGui::SetNextItemWidth(wf); ImGui::DragFloat("##nd_x", &foundVar->DefaultVec3[0], 0.01f, 0, 0, "%.3f");
                                        ImGui::SameLine(0, gap); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Y"); ImGui::SameLine(0, 2);
                                        ImGui::SetNextItemWidth(wf); ImGui::DragFloat("##nd_y", &foundVar->DefaultVec3[1], 0.01f, 0, 0, "%.3f");
                                        ImGui::SameLine(0, gap); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Z"); ImGui::SameLine(0, 2);
                                        ImGui::SetNextItemWidth(wf); ImGui::DragFloat("##nd_z", &foundVar->DefaultVec3[2], 0.01f, 0, 0, "%.3f");
                                        break;
                                    }
                                    case ScriptVarType::String:
                                    {
                                        char sbuf[256] = {}; strncpy(sbuf, foundVar->DefaultString.c_str(), 255);
                                        if (ImGui::InputText("##nd_s", sbuf, 256)) foundVar->DefaultString = sbuf;
                                        break;
                                    }
                                    case ScriptVarType::Entity:
                                    {
                                        // ── Entity picker ────────────────────────────────────────────
                                        // DefaultString guarda o nome da entity referenciada na cena.
                                        // Exibe botão com o nome atual + dropdown com todas as entities.

                                        const std::string& current = foundVar->DefaultString;
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
                                            if (ImGui::SmallButton("x##clrent")) foundVar->DefaultString.clear();
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
                                                foundVar->DefaultString = (const char*)p->Data;
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
                                                            foundVar->DefaultString = nc.Name;
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
                                    ImGui::Spacing();
                                    ImGui::Checkbox("Exposed (Inspector)##nd_exp", &foundVar->Exposed);
                                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                                }
                                else
                                {
                                    ImGui::TextColored({ 0.9f,0.3f,0.3f,1 }, "Variable '%s' not found.", node->StringValue.c_str());
                                    ImGui::TextDisabled("Declare it in Script Members.");
                                    ImGui::Spacing();
                                }
                            }
                            else if (node->Name == "Get Key" || node->Name == "Get Axis" || node->Name == "Print String")
                            {
                                const char* lbl =
                                    node->Name == "Get Key" ? "Key (e.g. W):" :
                                    node->Name == "Get Axis" ? "Axis (e.g. Horizontal):" : "Message:";

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
                                    char buf[128]; strncpy(buf, node->StringValue.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = '\0';
                                    if (ImGui::InputText("##sv", buf, sizeof(buf))) node->StringValue = buf;
                                }
                                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
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

            // Botão + Add Component
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
                    {"Mesh",           "Mesh",                "Malha 3D do objeto",        {0.6f,0.9f,1.f,1}},
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
                auto getIcon = [](const std::string& t) -> const char* {
                    if (t == "Mesh")           return "[M]";
                    if (t == "Material")       return "[~]";
                    if (t == "Rigidbody")      return "[R]";
                    if (t.find("Collider") != std::string::npos) return "[C]";
                    if (t == "CharacterController") return "[P]";
                    if (t == "SpringArm")      return "[A]";
                    if (t == "Camera")         return "[CAM]";
                    if (t == "Light")          return "[L]";
                    return "[?]";
                    };

                auto drawComp = [&](int i, float indent) {
                    auto& def = comps[i];
                    ImVec4 col = getColor(def.Type);
                    bool isSelected = (m_SelectedCompIndex == i);
                    ImGui::PushID(i);
                    if (indent > 0) ImGui::Indent(indent);

                    // Collapse
                    bool& collapsed = m_CompCollapsed[i < 32 ? i : 0];
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
                    if (ImGui::SmallButton(collapsed ? ">" : "v")) collapsed = !collapsed;
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine(0, 2);

                    // X
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                    if (ImGui::SmallButton("x")) removeIdx = i;
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine(0, 4);

                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::TextUnformatted(getIcon(def.Type));
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 4);

                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    if (ImGui::Selectable(def.Type.c_str(), isSelected, ImGuiSelectableFlags_None,
                        ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                        m_SelectedCompIndex = i;
                    ImGui::PopStyleColor();

                    // Drag para graph
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        std::string node;
                        if (def.Type == "Rigidbody")                             node = "GetRigidbody";
                        else if (def.Type.find("Collider") != std::string::npos)      node = "GetCollider";
                        else if (def.Type == "CharacterController")                   node = "GetCharacterController";
                        else if (def.Type == "SpringArm")                             node = "GetSpringArm";
                        else if (def.Type == "Camera")                                node = "GetCamera";
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

                    // Drop — reparentar Camera → SpringArm
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

                for (int i = 0; i < (int)comps.size(); i++)
                    if (comps[i].ParentIndex == -1) drawComp(i, 0.f);
                for (int i = 0; i < (int)comps.size(); i++)
                    if (comps[i].ParentIndex >= 0)  drawComp(i, 16.f);

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

            // Botão Save
            if (m_ScriptAsset)
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.35f, 0.55f, 1));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.75f, 1));
                if (ImGui::Button("Save", ImVec2(bw, 0)) && !m_ScriptAsset->GetFilePath().empty())
                {
                    SaveNodePositions();
                    m_ScriptAsset->Save(m_ScriptAsset->GetFilePath());
                    m_ConsoleLines.push_back("[Info] Script salvo: " + m_ScriptAsset->GetFilePath().string());
                }
                ImGui::PopStyleColor(2);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace axe