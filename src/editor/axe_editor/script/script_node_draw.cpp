// script_node_draw.cpp
// DrawNode — renderização individual de cada ScriptNode no canvas Blueprint:
// header colorido, pins com ícones, inline value editors (Set Variable),
// sincronização de tipo de variable nodes.

#include "script_graph_window.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_asset.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <utilities/widgets.h>
#include <utilities/drawing.h>
#include <algorithm>


namespace ed = ax::NodeEditor;
using IconType = ax::Drawing::IconType;

namespace axe
{
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

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawNode(ScriptNode* node)
    {
        if (!node) return;

        // ── Nodes de conversão — visual compacto estilo Unreal ────────────────
        static const char* s_ConvNodes[] = {
            "To Float","To Int","To Bool","To String","To Vec3","Break Vec3","Float to Vec3",nullptr
        };
        bool isConvNode = false;
        for (int ci = 0; s_ConvNodes[ci]; ci++)
            if (node->Name == s_ConvNodes[ci]) { isConvNode = true; break; }

        if (isConvNode)
        {
            // Estilo compacto: sem header, borda arredondada laranja, pins lado a lado
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(28, 28, 28, 245));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(180, 120, 30, 220));
            ed::PushStyleVar(ed::StyleVar_NodeRounding, 12.0f);
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.5f);
            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(4, 4, 4, 4));
            ed::BeginNode(node->ID);

            // Layout: [InputPin] •→• [OutputPin]  tudo numa linha
            ImGui::BeginGroup();
            for (auto& pin : node->Inputs)
            {
                ed::BeginPin(pin.ID, ed::PinKind::Input);
                ax::Widgets::Icon(ImVec2(ICON_SZ, ICON_SZ), PinIcon(pin.Type),
                    m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), { 0.1f,0.1f,0.1f,0.8f });
                ed::EndPin();
                ImGui::SameLine(0, 2);
            }

            // Seta central
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.5f, 0.1f, 1.f));
            ImGui::TextUnformatted("\xe2\x86\x92");  // →
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 2);

            for (auto& pin : node->Outputs)
            {
                ed::BeginPin(pin.ID, ed::PinKind::Output);
                ax::Widgets::Icon(ImVec2(ICON_SZ, ICON_SZ), PinIcon(pin.Type),
                    m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), { 0.1f,0.1f,0.1f,0.8f });
                ed::EndPin();
                ImGui::SameLine(0, 2);
            }
            ImGui::EndGroup();

            ed::EndNode();
            ed::PopStyleVar(3);
            ed::PopStyleColor(2);
            return;
        }

        float inW = 0, outW = 0;
        for (auto& p : node->Inputs)  inW = std::max(inW, ImGui::CalcTextSize(p.Name.c_str()).x);
        for (auto& p : node->Outputs) outW = std::max(outW, ImGui::CalcTextSize(p.Name.c_str()).x);
        float titleW = ImGui::CalcTextSize(node->Name.c_str()).x + 16.0f;
        float nodeW = std::max(titleW, inW + outW + ICON_SZ * 2 + 24.0f);
        nodeW = std::max(nodeW, 160.0f);

        // Vec3 variable nodes precisam de mais largura para campos X/Y/Z
        if (node->Category == ScriptNodeCategory::Variable && !node->StringValue.empty() && m_ScriptAsset)
            for (auto& v : m_ScriptAsset->GetVariables())
                if (v.Name == node->StringValue && v.Type == ScriptVarType::Vec3)
                {
                    nodeW = std::max(nodeW, 185.0f); break;
                }

        // ── Cor do header ─────────────────────────────────────────────────────
        ImVec4 hcol;
        if (node->Category == ScriptNodeCategory::Variable && m_ScriptAsset)
        {
            int varTypeIdx = node->IntValue & 0xFF;
            // Sync from asset
            for (auto& v : m_ScriptAsset->GetVariables())
                if (v.Name == node->StringValue)
                {
                    varTypeIdx = (int)v.Type;
                    node->IntValue = (node->IntValue & 0x100) | varTypeIdx;
                    break;
                }

            // Atualiza tipo dos pins Value
            ScriptPinType pinType = ScriptPinType::Float;
            switch ((ScriptVarType)varTypeIdx) {
            case ScriptVarType::Bool:   pinType = ScriptPinType::Bool;   break;
            case ScriptVarType::Int:    pinType = ScriptPinType::Int;    break;
            case ScriptVarType::Vec3:   pinType = ScriptPinType::Vec3;   break;
            case ScriptVarType::String: pinType = ScriptPinType::String; break;
            case ScriptVarType::Vec2:   pinType = ScriptPinType::Vec2;   break;
            case ScriptVarType::Vec4:   pinType = ScriptPinType::Vec4;   break;
            case ScriptVarType::Quat:   pinType = ScriptPinType::Quat;   break;
            case ScriptVarType::Entity: pinType = ScriptPinType::Object; break;
            default:                    pinType = ScriptPinType::Float;  break;
            }
            for (auto& p : node->Inputs)  if (p.Name == "Value") p.Type = pinType;
            for (auto& p : node->Outputs) if (p.Name == "Value") p.Type = pinType;

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

        // ── Header ───────────────────────────────────────────────────────────
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float  hh = ImGui::GetTextLineHeight() + 10.0f;
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + nodeW, p.y + hh), hc, 6.f, ImDrawFlags_RoundCornersTop);
            dl->AddRectFilled(ImVec2(p.x, p.y + hh - 3), ImVec2(p.x + nodeW, p.y + hh), hcDark, 0);

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

        // ── Inline value editor para Set Variable ─────────────────────────────
        bool isSetVar = (node->Name == "Set Variable" && !node->StringValue.empty());
        if (isSetVar && m_ScriptAsset)
        {
            ScriptVariable* var = nullptr;
            for (auto& v : m_ScriptAsset->GetVariables())
                if (v.Name == node->StringValue) { var = &v; break; }

            if (var)
            {
                bool hasConnection = false;
                for (auto& link : m_Graph->GetLinks())
                    for (auto& p : node->Inputs)
                        if (p.Name == "Value" && (link.StartPin == p.ID || link.EndPin == p.ID))
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
                        float pad = 4.f, gap = 4.f;
                        float lbl = ImGui::CalcTextSize("X").x + 2.f;
                        float edPd = ed::GetStyle().NodePadding.z;
                        float fw = std::max((nodeW - pad - (pad + edPd) - lbl * 3.f - gap * 2.f) / 3.f, 38.f);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("X"); ImGui::SameLine(0, 2);
                        ImGui::SetNextItemWidth(fw);
                        ImGui::DragFloat("##nvx", &var->DefaultVec3[0], 0.01f, 0, 0, "%.3f");
                        ImGui::SameLine(0, gap);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Y"); ImGui::SameLine(0, 2);
                        ImGui::SetNextItemWidth(fw);
                        ImGui::DragFloat("##nvy", &var->DefaultVec3[1], 0.01f, 0, 0, "%.3f");
                        ImGui::SameLine(0, gap);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Z"); ImGui::SameLine(0, 2);
                        ImGui::SetNextItemWidth(fw);
                        ImGui::DragFloat("##nvz", &var->DefaultVec3[2], 0.01f, 0, 0, "%.3f");
                        break;
                    }
                    case ScriptVarType::String:
                    {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
                        ImGui::SetNextItemWidth(nodeW - 16.f);
                        char buf[128] = {};
                        strncpy(buf, var->DefaultString.c_str(), 127);
                        if (ImGui::InputText("##nv", buf, 128)) var->DefaultString = buf;
                        break;
                    }
                    default: break;
                    }
                }
            }
            ImGui::Spacing();
        }

        // ── Pins ─────────────────────────────────────────────────────────────
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

} // namespace axe