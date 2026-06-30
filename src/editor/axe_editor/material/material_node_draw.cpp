// material_node_draw.cpp
// Desenho visual de cada node/pin/comment no canvas do Material Graph:
// header colorido, pins com ícone por tipo, conteúdo inline (float/color/
// textura), comment boxes redimensionáveis, e os helpers de cor/compatibi-
// lidade de pin usados pelo restante do editor.

#include "material_editor_window.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>
#include <utilities/widgets.h>
#include <utilities/drawing.h>
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace axe
{

    void MaterialEditorWindow::DrawNode(Node& node)
    {
        if (node.Type == NodeType::Comment)
        {
            DrawCommentNode(&node);
            return;
        }

        if (node.Name == "Reroute")
        {
            DrawRerouteNode(node);
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

                // Domínio Light Function: só o Emissive do Material Output
                // tem efeito (vira a cor/intensidade da luz) — os outros
                // pins (Base Color, Metallic, Roughness, etc.) ficam
                // acinzentados pra deixar claro que são ignorados, igual a
                // Unreal faz quando troca o Material Domain.
                bool dimmedByDomain = (node.Name == "Material Output")
                    && (m_Graph->Domain == MaterialDomain::LightFunction)
                    && (pin.Name != "Emissive");
                if (dimmedByDomain)
                    imcol = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);

                ed::BeginPin(pin.ID, isInput ? ed::PinKind::Input : ed::PinKind::Output);
                ed::PinPivotAlignment(isInput ? ImVec2(0.0f, 0.5f) : ImVec2(1.0f, 0.5f));
                ed::PinPivotSize(ImVec2(0, 0));
                ImGui::BeginGroup();

                if (isInput)
                {
                    bool connected = m_Graph->IsPinLinked(pin.ID);
                    DrawPinIcon(pin, connected, dimmedByDomain ? 90 : 255);
                    ImGui::SameLine();

                    // Pin Float desconectado — edita o valor direto, sem
                    // precisar criar um node "Float" só pra constante (igual
                    // a digitar direto no pin na Unreal).
                    //
                    // Exceção: os pins do Material Output (Base Color, Metallic,
                    // Roughness, Normal, Emissive, Opacity, etc.) são saídas
                    // finais do material, não constantes editáveis — para eles
                    // mostramos sempre o nome do pin (o campo numérico solto
                    // estava aparecendo deslocado, longe do node).
                    bool allowInlineEdit = (node.Name != "Material Output");

                    if (allowInlineEdit && !connected && pin.Type == PinType::Float)
                    {
                        ImGui::PushID(&pin);
                        ImGui::SetNextItemWidth(60.0f);
                        ImGui::DragFloat("##pindef", &pin.DefaultFloat, 0.01f, 0.0f, 0.0f, "%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", pin.Name.c_str());
                        ImGui::PopID();
                    }
                    else
                    {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + verticalOffset);
                        ImGui::TextColored(imcol, "%s", pin.Name.c_str());
                    }
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
            else if (node.Value.Type == PinType::Vec2 && node.Outputs.size() == 1)
            {
                ImGui::SetNextItemWidth(NODE_WIDTH - 16);
                ImGui::DragFloat2("##val2", &node.Value.Vec2Val.x, 0.01f);
            }
            else if (node.Value.Type == PinType::Vec3 && node.Outputs.size() == 1)
            {
                ImGui::SetNextItemWidth(NODE_WIDTH - 16);
                ImGui::DragFloat3("##val3", &node.Value.Vec3Val.x, 0.01f);
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


    void MaterialEditorWindow::DrawRerouteNode(Node& node)
    {
        if (node.Inputs.empty() || node.Outputs.empty()) return;

        // Knot minúsculo: sem título nem corpo, só os dois pins coladinhos
        // pra "dobrar" o fio — mesmo espírito do Reroute do Script Editor.
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(4, 2, 4, 2));
        ed::PushStyleVar(ed::StyleVar_NodeRounding, 4.0f);
        ed::BeginNode(node.ID);

        Pin& in = node.Inputs[0];
        Pin& out = node.Outputs[0];

        ed::BeginPin(in.ID, ed::PinKind::Input);
        ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
        ed::PinPivotSize(ImVec2(0, 0));
        DrawPinIcon(in, m_Graph->IsPinLinked(in.ID), 255);
        ed::EndPin();

        ImGui::SameLine();

        ed::BeginPin(out.ID, ed::PinKind::Output);
        ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
        ed::PinPivotSize(ImVec2(0, 0));
        DrawPinIcon(out, m_Graph->IsPinLinked(out.ID), 255);
        ed::EndPin();

        ed::EndNode();
        ed::PopStyleVar(2);
    }

    void MaterialEditorWindow::DrawCommentNode(Node* node)
    {
        // Mesmo tratamento usado no Script Editor: ed::Group (suporte nativo
        // da lib — arrastar o título move junto todo node visualmente dentro
        // da caixa, de graça) em vez do header+pins normal, já que Comment
        // não tem nenhum pin.
        const float commentAlpha = 0.75f;
        ImColor bg(node->CommentColor[0], node->CommentColor[1], node->CommentColor[2], 60.f / 255.f);
        ImColor border(node->CommentColor[0], node->CommentColor[1], node->CommentColor[2], 200.f / 255.f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentAlpha);
        ed::PushStyleColor(ed::StyleColor_NodeBg, bg);
        ed::PushStyleColor(ed::StyleColor_NodeBorder, border);
        ed::BeginNode(node->ID);
        ImGui::PushID((int)node->ID.Get());

        bool isRenaming = (m_RenamingComment == (int)node->ID.Get());
        if (isRenaming)
        {
            if (m_RenameCommentJustStarted) { ImGui::SetKeyboardFocusHere(); m_RenameCommentJustStarted = false; }
            char buf[128];
            strncpy(buf, node->StringValue.c_str(), 127); buf[127] = 0;
            ImGui::SetNextItemWidth(std::max(node->Size.x - 16.f, 80.f));
            if (ImGui::InputText("##commenttitle", buf, sizeof(buf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                node->StringValue = buf;
                m_RenamingComment = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_RenamingComment = -1;
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            ImGui::TextUnformatted(node->StringValue.empty() ? "Comment" : node->StringValue.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_RenamingComment = (int)node->ID.Get();
                m_RenameCommentJustStarted = true;
            }
            // Botão direito no título — paleta rápida de cor (presets,
            // igual ao "Comment Color" da Unreal, sem picker completo pra
            // não pesar a interação de algo tão simples).
            if (ImGui::BeginPopupContextItem("##matcommentcolor"))
            {
                static const float s_Presets[][3] = {
                    {0.10f,0.35f,0.45f}, {0.85f,0.85f,0.85f}, {0.75f,0.35f,0.10f},
                    {0.55f,0.15f,0.55f}, {0.70f,0.12f,0.12f}, {0.15f,0.55f,0.20f},
                };
                ImGui::TextDisabled("Comment Color:");
                for (int s = 0; s < 6; s++)
                {
                    ImGui::SameLine();
                    ImGui::PushID(s);
                    ImVec4 sc(s_Presets[s][0], s_Presets[s][1], s_Presets[s][2], 1);
                    ImGui::PushStyleColor(ImGuiCol_Button, sc);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sc);
                    if (ImGui::Button("##swatch", ImVec2(18, 18)))
                    {
                        node->CommentColor[0] = s_Presets[s][0];
                        node->CommentColor[1] = s_Presets[s][1];
                        node->CommentColor[2] = s_Presets[s][2];
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }
        }

        ed::Group(node->Size);
        ImGui::PopID();
        ed::EndNode();

        ed::PopStyleColor(2);
        ImGui::PopStyleVar();

        // Lê de volta o tamanho atual — o usuário pode ter redimensionado
        // arrastando a borda (resize é tratado internamente pela lib).
        ImVec2 newSize = ed::GetNodeSize(node->ID);
        if (newSize.x > 50 && newSize.y > 50)
            node->Size = newSize;

        UpdateCommentChildren(node);

        if (ed::BeginGroupHint(node->ID))
        {
            auto bgAlpha = static_cast<int>(ImGui::GetStyle().Alpha * 255);
            auto min = ed::GetGroupMin();
            ImGui::SetCursorScreenPos(min + ImVec2(8, 8));

            std::string preview = node->StringValue.empty() ? "Comment" : node->StringValue;
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


} // namespace axe