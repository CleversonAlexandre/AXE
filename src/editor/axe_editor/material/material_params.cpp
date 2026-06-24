// material_params.cpp
// Painel "Material Params": inspeção do node selecionado no grafo (ou, na
// ausência de seleção, os parâmetros globais do material) e o widget de
// slot de textura usado pelos parâmetros legados (não-PBR).

#include "material_editor_window.hpp"
#include "axe/asset/asset_database.hpp"
#include "editor/axe_editor/asset/asset_picker.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace axe
{

    void MaterialEditorWindow::DrawMaterialParamsWindow()
    {
        ed::SetCurrentEditor(m_NodeEditorContext);

        if (ImGui::Begin("Material Params"))
            DrawMaterialParams(*m_Material);
        ImGui::End();

        ed::SetCurrentEditor(nullptr);
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
                else if (node->Name == "Vec2" && node->IsConstant)
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat2("##val2", &node->Value.Vec2Val.x, 0.01f);
                }
                else if (node->Name == "Vec3" && node->IsConstant)
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat3("##val3", &node->Value.Vec3Val.x, 0.01f);
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

        // --- Material Domain / Blend Mode / Shading Model ---
        // Estrutura inspirada na Unreal. Só os itens marcados como
        // disponíveis são realmente suportados pelo motor — o resto
        // aparece no dropdown (pra já deixar o caminho familiar) mas fica
        // desabilitado, sem fingir que funciona.
        auto drawDomainCombo = [](const char* label, const char* const* names,
            const bool* available, int count, int& current)
            {
                ImGui::TextDisabled("%s", label);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo((std::string("##") + label).c_str(), names[current]))
                {
                    for (int i = 0; i < count; i++)
                    {
                        ImGuiSelectableFlags flags = available[i] ? 0 : ImGuiSelectableFlags_Disabled;
                        std::string itemLabel = available[i]
                            ? names[i] : std::string(names[i]) + " (indisponível)";
                        if (ImGui::Selectable(itemLabel.c_str(), current == i, flags))
                            current = i;
                    }
                    ImGui::EndCombo();
                }
            };

        static const char* s_DomainNames[] = {
            "Surface", "Light Function", "Deferred Decal", "Volume", "Post Process", "User Interface" };
        static const bool s_DomainAvailable[] = { true, true, false, false, false, false };
        int domain = (int)m_Graph->Domain;
        drawDomainCombo("Material Domain", s_DomainNames, s_DomainAvailable, 6, domain);
        m_Graph->Domain = (MaterialDomain)domain;

        static const char* s_BlendNames[] = {
            "Opaque", "Masked", "Translucent", "Additive", "Modulate", "Alpha Composite", "Alpha Holdout" };
        static const bool s_BlendAvailable[] = { true, true, true, true, false, false, false };
        int blend = (int)m_Graph->BlendMode;
        drawDomainCombo("Blend Mode", s_BlendNames, s_BlendAvailable, 7, blend);
        m_Graph->BlendMode = (MaterialBlendMode)blend;

        static const char* s_ShadingNames[] = {
            "Default Lit", "Unlit", "Subsurface", "Clear Coat", "Preintegrated Skin",
            "Two Sided Foliage", "Hair", "Cloth", "Eye", "Single Layer Water",
            "Thin Translucent", "From Material Expression" };
        static const bool s_ShadingAvailable[] = {
            true, true, false, false, false, false, false, false, false, false, false, false };
        int shading = (int)m_Graph->ShadingModel;
        drawDomainCombo("Shading Model", s_ShadingNames, s_ShadingAvailable, 12, shading);
        m_Graph->ShadingModel = (MaterialShadingModel)shading;

        if (m_Graph->Domain == MaterialDomain::LightFunction)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f),
                "Light Function: só o pin Emissive do Material Output\n"
                "é usado — os outros pins ficam acinzentados no grafo.");
        }

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

} // namespace axe