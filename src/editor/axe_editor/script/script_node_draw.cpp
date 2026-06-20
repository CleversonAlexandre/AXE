// script_node_draw.cpp
// DrawNode — renderização individual de cada ScriptNode no canvas Blueprint:
// header colorido, pins com ícones, inline value editors (Set Variable),
// sincronização de tipo de variable nodes.

#include "script_graph_window.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/input/input_mapping.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <utilities/widgets.h>
#include <utilities/drawing.h>
#include <algorithm>
#include <vector>
#include <string>


namespace ed = ax::NodeEditor;
using IconType = ax::Drawing::IconType;

namespace axe
{
    static IconType PinIcon(ScriptPinType t)
    {
        if (t == ScriptPinType::Flow) return IconType::Flow;
        // Pins de array usam o ícone de grade 3x3 nativo da lib (mesmo glifo
        // já usado nas badges do Script Members) em vez do círculo — convenção
        // visual da Unreal para "isso é uma lista". O parâmetro "filled" do
        // DrawIcon (passado em cada call site abaixo) já trata sozinho o caso
        // vazado/preenchido conforme o pin está conectado ou não.
        if ((int)t >= (int)ScriptPinType::FloatArray) return IconType::Grid;
        return IconType::Circle;
    }

    static ImVec4 PinCol(ScriptPinType t)
    {
        ImColor c = GetPinColor(t); return { c.Value.x,c.Value.y,c.Value.z,c.Value.w };
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Editor do valor LOCAL de um Set Variable — ver comentário da declaração
    // em script_graph_window.hpp. Chamada tanto pelo canvas (DrawNode, abaixo)
    // quanto pelo painel Script Details (script_details.cpp).
    void ScriptGraphWindow::DrawSetVariableLocalValueEditor(ScriptNode* node, ScriptVarType varType, float width)
    {
        switch (varType)
        {
        case ScriptVarType::Float:
            ImGui::SetNextItemWidth(width);
            ImGui::DragFloat("##setlocal", &node->FloatValue, 0.01f);
            break;
        case ScriptVarType::Bool:
            ImGui::Checkbox("##setlocal", &node->BoolValue);
            break;
        case ScriptVarType::Int:
            ImGui::SetNextItemWidth(width);
            ImGui::DragInt("##setlocal", &node->IntLocalValue);
            break;
        case ScriptVarType::Vec3:
        {
            float avail = (width > 0.f) ? width : ImGui::GetContentRegionAvail().x;
            float gap = 4.f;
            float lbl = ImGui::CalcTextSize("X").x + 2.f;
            float fw = std::max((avail - lbl * 3.f - gap * 2.f) / 3.f, 38.f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("X"); ImGui::SameLine(0, 2);
            ImGui::SetNextItemWidth(fw);
            ImGui::DragFloat("##slx", &node->Vec3Value[0], 0.01f, 0, 0, "%.3f");
            ImGui::SameLine(0, gap);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Y"); ImGui::SameLine(0, 2);
            ImGui::SetNextItemWidth(fw);
            ImGui::DragFloat("##sly", &node->Vec3Value[1], 0.01f, 0, 0, "%.3f");
            ImGui::SameLine(0, gap);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Z"); ImGui::SameLine(0, 2);
            ImGui::SetNextItemWidth(fw);
            ImGui::DragFloat("##slz", &node->Vec3Value[2], 0.01f, 0, 0, "%.3f");
            break;
        }
        case ScriptVarType::String:
        {
            ImGui::SetNextItemWidth(width);
            char buf[128] = {};
            strncpy(buf, node->StringLocalValue.c_str(), 127);
            if (ImGui::InputText("##setlocal", buf, 128)) node->StringLocalValue = buf;
            break;
        }
        default: break;
        }
    }

    static ImVec4 HdrCol(ScriptNodeCategory c)
    {
        ImColor x = GetNodeHeaderColor(c); return { x.Value.x,x.Value.y,x.Value.z,x.Value.w };
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawNode(ScriptNode* node)
    {
        if (!node) return;

        // BUGFIX (causa raiz real do "editar um node afeta o outro"):
        // ed::BeginNode() NÃO empurra um ID do ImGui por node — confirmado
        // lendo imgui_node_editor.cpp (NodeBuilder::Begin só posiciona o
        // cursor, não toca na pilha de ID). Sem um PushID aqui, TODO widget
        // com label fixo desenhado dentro de um node (DragFloat("##setlocal"),
        // o combo "##selname" de Get Action/Axis, os botões do Sequence, etc.)
        // compartilha o MESMO ID do ImGui com o widget homônimo de qualquer
        // OUTRO node — os dados (node->FloatValue de cada node) são de fato
        // campos separados, mas o ImGui, ao não distinguir os widgets,
        // confunde o estado de edição/foco entre eles (exatamente o sintoma:
        // editar um node altera visualmente o outro). PushID(node) escopa
        // toda a sub-árvore de IDs deste node, eliminando a colisão de uma
        // vez para qualquer widget de qualquer node, presente ou futuro.
        ImGui::PushID((int)node->ID.Get());

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
            ImGui::PopID();
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

            // Atualiza tipo dos pins Value — ScriptVarTypeToPinType cobre os
            // 18 tipos (9 escalares + 9 arrays); o switch manual anterior
            // só tratava 8 e sobrescrevia arrays de volta para Float aqui
            // mesmo depois de corrigidos em outro lugar (causa raiz real do
            // pin de array nunca mudar de cor/ícone — esta era a sincronização
            // que de fato roda por último a cada frame).
            ScriptPinType pinType = ScriptVarTypeToPinType((ScriptVarType)varTypeIdx);
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
                    // BUGFIX: este editor antes lia/escrevia var->DefaultFloat/
                    // DefaultBool/DefaultInt/DefaultVec3/DefaultString — o
                    // valor DEFAULT GLOBAL da variável, compartilhado com
                    // todo Get Variable e com o painel Script Details. Editar
                    // o valor aqui mudava silenciosamente o default de TODA
                    // a variável (visível em qualquer Get da mesma variável),
                    // e o compilador nem lia esse campo — ele já usa os
                    // campos LOCAIS do node (FloatValue/BoolValue/
                    // IntLocalValue/Vec3Value/StringLocalValue, ver
                    // ScriptGraphCompiler::GenerateNode "Set Variable") desde
                    // sempre. Agora chama o mesmo helper usado pelo painel
                    // Script Details (DrawSetVariableLocalValueEditor) — uma
                    // função só, sem duplicar o switch por tipo em dois
                    // arquivos.
                    float pad = (var->Type == ScriptVarType::Vec3) ? 4.f : 8.f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);

                    float width = nodeW - 16.f;
                    if (var->Type == ScriptVarType::Vec3)
                    {
                        float edPd = ed::GetStyle().NodePadding.z;
                        width = nodeW - pad - (pad + edPd);
                    }
                    DrawSetVariableLocalValueEditor(node, var->Type, width);
                }
            }
            ImGui::Spacing();
        }

        // ── Combo de seleção para Get Action / Get Axis ───────────────────────
        // Substitui o antigo pin de string solta (onde a tecla era digitada à
        // mão) por um dropdown alimentado pelo InputMappingConfig — a Action/
        // Axis precisa já existir no Input Settings para aparecer aqui.
        bool isGetAction = (node->Name == "Get Action");
        bool isGetAxis = (node->Name == "Get Axis");
        if (isGetAction || isGetAxis)
        {
            auto& cfg = InputMappingConfig::Get();
            std::vector<std::string> names;
            if (isGetAction)
                for (auto& a : cfg.GetActions()) names.push_back(a.Name);
            else
                for (auto& a : cfg.GetAxes()) names.push_back(a.Name);

            int curIdx = -1;
            for (int i = 0; i < (int)names.size(); i++)
                if (names[i] == node->StringValue) { curIdx = i; break; }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
            ImGui::SetNextItemWidth(nodeW - 16.f);
            const char* comboLabel = (curIdx >= 0) ? names[curIdx].c_str()
                : (names.empty() ? "(nenhuma configurada)" : "(selecione)");
            if (ImGui::BeginCombo("##selname", comboLabel))
            {
                for (int i = 0; i < (int)names.size(); i++)
                {
                    bool sel = (i == curIdx);
                    if (ImGui::Selectable(names[i].c_str(), sel))
                    {
                        node->StringValue = names[i];
                        if (isGetAxis)
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

            // Cobre o caso de um grafo carregado do disco (Deserialize): o
            // StringValue já está setado, mas os pins podem não refletir o
            // AxisValueType atual ainda (ex.: o Axis Mapping foi reconfigurado
            // de 1D para 2D no Input Settings depois que o grafo foi salvo).
            // RebuildAxisOutputPins é idempotente — seguro de chamar todo frame.
            if (isGetAxis && curIdx >= 0)
            {
                auto* axis = cfg.FindAxis(node->StringValue);
                if (axis) m_Graph->RebuildAxisOutputPins(node, (int)axis->ValueType);
            }
            ImGui::Spacing();
        }

        // ── Botões +/- de pins "Then" no node Sequence ────────────────────────
        // Mesmo padrão visual do Sequence da Unreal: o usuário controla quantos
        // ramos paralelos existem. RebuildSequencePins preserva pins/links já
        // existentes ao crescer, e só remove do fim ao encolher.
        if (node->Name == "Sequence")
        {
            int pinCount = (int)node->Outputs.size();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
            if (ImGui::SmallButton("-##seqminus"))
            {
                PushUndo("Remove Sequence Pin");
                m_Graph->RebuildSequencePins(node, pinCount - 1);
                CommitUndo("Remove Sequence Pin");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d", pinCount);
            ImGui::SameLine();
            if (ImGui::SmallButton("+##seqplus"))
            {
                PushUndo("Add Sequence Pin");
                m_Graph->RebuildSequencePins(node, pinCount + 1);
                CommitUndo("Add Sequence Pin");
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
        ImGui::PopID();
    }

} // namespace axe