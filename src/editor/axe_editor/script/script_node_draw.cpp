// script_node_draw.cpp
// DrawNode — renderização individual de cada ScriptNode no canvas Blueprint:
// header colorido, pins com ícones, inline value editors (Set Variable),
// sincronização de tipo de variable nodes.

#include "script_graph_window.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/input/input_mapping.hpp"
#include "axe/animation/anim_graph_asset.hpp" // combo de parametros do AnimGraph
#include "axe/asset/asset_database.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <utilities/widgets.h>
#include <utilities/drawing.h>
#include <algorithm>
#include <vector>
#include <string>
#include <limits> // quiet_NaN no reroute (pino solto)
#include <cmath>  // std::sqrt na forca da curva do reroute


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
        if (IsArrayPinType(t)) return IconType::Grid;
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

        // ── Comment box — caminho totalmente separado, igual o de conversão
        // abaixo: usa ed::Group (suporte nativo da lib — arrastar o título
        // move junto todo node visualmente dentro da caixa, de graça) em vez
        // do header+pins normal, já que Comment não tem nenhum pin.
        if (node->Name == "Comment")
        {
            const float commentAlpha = 0.75f;
            ImColor bg(node->CommentColor[0], node->CommentColor[1], node->CommentColor[2], 60.f / 255.f);
            ImColor border(node->CommentColor[0], node->CommentColor[1], node->CommentColor[2], 200.f / 255.f);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentAlpha);
            ed::PushStyleColor(ed::StyleColor_NodeBg, bg);
            ed::PushStyleColor(ed::StyleColor_NodeBorder, border);
            ed::BeginNode(node->ID);

            bool isRenaming = (m_RenamingComment == (int)node->ID.Get());
            if (isRenaming)
            {
                if (m_RenameCommentJustStarted) { ImGui::SetKeyboardFocusHere(); m_RenameCommentJustStarted = false; }
                char buf[128];
                strncpy(buf, node->StringValue.c_str(), 127); buf[127] = 0;
                ImGui::SetNextItemWidth(std::max(node->CommentSize.x - 16.f, 80.f));
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
                // igual ao "Comment Color" da Unreal, sem picker completo
                // pra não pesar a interação de algo tão simples).
                if (ImGui::BeginPopupContextItem("##commentcolor"))
                {
                    static const float s_Presets[][3] = {
                        {0.10f,0.35f,0.45f}, {0.85f,0.85f,0.85f}, {0.75f,0.35f,0.10f},
                        {0.55f,0.15f,0.55f}, {0.70f,0.12f,0.12f}, {0.15f,0.55f,0.20f},
                    };
                    ImGui::TextDisabled("Cor do Comment:");
                    for (int s = 0; s < 6; s++)
                    {
                        ImGui::SameLine();
                        ImGui::PushID(s);
                        ImVec4 sc(s_Presets[s][0], s_Presets[s][1], s_Presets[s][2], 1);
                        ImGui::PushStyleColor(ImGuiCol_Button, sc);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sc);
                        if (ImGui::Button("##swatch", ImVec2(18, 18)))
                        {
                            PushUndo("Change Comment Color");
                            node->CommentColor[0] = s_Presets[s][0];
                            node->CommentColor[1] = s_Presets[s][1];
                            node->CommentColor[2] = s_Presets[s][2];
                            CommitUndo("Change Comment Color");
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                    }
                    ImGui::EndPopup();
                }
            }

            ed::Group(node->CommentSize);
            ed::EndNode();
            ed::PopStyleColor(2);
            ImGui::PopStyleVar();

            // Lê de volta o tamanho atual — o usuário pode ter redimensionado
            // arrastando a borda (resize é tratado internamente pela lib);
            // sem isso, o tamanho não sobreviveria ao próximo frame/save.
            node->CommentSize = ed::GetNodeSize(node->ID);

            ImGui::PopID();
            return;
        }

        // ── Reroute — "knot" minúsculo, ainda mais compacto que os nodes de
        // conversão abaixo: sem seta central, sem header, os dois pins quase
        // colados (dão a impressão visual de um único pontinho no fio,
        // mesmo sendo dois ed::PinId separados por trás).
        if (node->Name == "Reroute")
        {
            // ── Pinos que ACOMPANHAM o fio ────────────────────────────────
            //
            // A tangente do fio sai do pino na direcao fixada pelos style
            // vars SourceDirection (+1,0 = para a direita) e TargetDirection
            // (-1,0 = para a esquerda), lidos dentro do BeginPin. Com um
            // reroute posicionado a ESQUERDA da origem, ou a DIREITA do
            // destino, essas direcoes apontam para o lado errado e o fio
            // desenha um laco por fora — o "no" que aparecia quando ele
            // reorganizava os nodes.
            //
            // Aqui a direcao de cada pino e decidida pela posicao REAL do
            // outro extremo do fio: e o mesmo comportamento do reroute da
            // Unreal, onde o pontinho simplesmente segue o caminho.
            const ImVec2 selfPos = ed::GetNodePosition(node->ID);

            // Posicao aproximada do outro extremo do fio ligado a este pino
            // (borda do node do lado que encara o reroute, na meia altura).
            // Devolve NaN quando o pino esta solto — nesse caso vale o padrao.
            auto otherSidePos = [&](ed::PinId pinId) -> ImVec2
                {
                    const float nan = std::numeric_limits<float>::quiet_NaN();

                    for (const auto& link : m_Graph->GetLinks())
                    {
                        ed::PinId otherId;
                        if (link.StartPin == pinId)      otherId = link.EndPin;
                        else if (link.EndPin == pinId)   otherId = link.StartPin;
                        else                             continue;

                        for (const auto& n : m_Graph->GetNodes())
                        {
                            const ImVec2 p = ed::GetNodePosition(n->ID);
                            const ImVec2 s = ed::GetNodeSize(n->ID);

                            for (const auto& pin : n->Inputs)
                                if (pin.ID == otherId) return ImVec2(p.x, p.y + s.y * 0.5f);
                            for (const auto& pin : n->Outputs)
                                if (pin.ID == otherId) return ImVec2(p.x + s.x, p.y + s.y * 0.5f);
                        }
                    }

                    return ImVec2(nan, nan);
                };

            // As DUAS direcoes sao decididas antes de desenhar, porque uma
            // depende da outra: quando apontam para o MESMO lado, o fio faz
            // meia-volta.
            float inDirX = -1.0f, outDirX = 1.0f;
            ImVec2 srcPos(std::numeric_limits<float>::quiet_NaN(), 0.f);
            ImVec2 dstPos(std::numeric_limits<float>::quiet_NaN(), 0.f);

            if (!node->Inputs.empty())
            {
                // Origem a DIREITA do reroute? O fio chega pela direita.
                srcPos = otherSidePos(node->Inputs[0].ID);
                if (srcPos.x == srcPos.x && srcPos.x > selfPos.x) inDirX = 1.0f;
            }

            if (!node->Outputs.empty())
            {
                // Destino a ESQUERDA? O fio sai pela esquerda.
                dstPos = otherSidePos(node->Outputs[0].ID);
                if (dstPos.x == dstPos.x && dstPos.x < selfPos.x) outDirX = -1.0f;
            }

            // MEIA-VOLTA: origem e destino do mesmo lado do reroute, entao os
            // dois pinos apontam para la e os pontos de controle da bezier vao
            // para a MESMA direcao. Com o LinkStrength padrao (100, fixo) isso
            // estufa num laco quando o reroute esta perto; com um valor fixo
            // PEQUENO — que foi minha primeira tentativa — o fio sai quase
            // reto do pontinho quando esta longe, e achata.
            //
            // O que a Unreal faz, e o que faz o reroute se ajeitar em QUALQUER
            // posicao, e a tangente ACOMPANHAR a distancia ate o outro extremo:
            // perto vira cotovelo justo, longe abre um arco amplo. Como cada
            // ponta viaja uma distancia propria, cada pino leva a sua.
            const bool uTurn = (inDirX == outDirX);

            auto strengthFor = [&](const ImVec2& other) -> float
                {
                    if (!(other.x == other.x)) return 100.0f; // solto: padrao

                    const float dx = other.x - selfPos.x;
                    const float dy = other.y - selfPos.y;
                    const float dist = std::sqrt(dx * dx + dy * dy);

                    // 0.45 da uma curva cheia sem virar espiral; os limites
                    // evitam tangente nula em fio curtissimo e arco gigante
                    // em fio que atravessa o canvas inteiro.
                    return std::clamp(dist * 0.45f, 25.0f, 260.0f);
                };

            // Direcao da tangente. No caso em S ela e horizontal — e o que
            // ele aprovou, nao se mexe. Na MEIA-VOLTA ela MIRA o alvo.
            //
            // Com as duas tangentes horizontais, as duas metades da meia-volta
            // saiam do pontinho no mesmo rumo e quase sobrepostas — o embolado
            // dos reroutes de float. Mirando, um lobo sobe em direcao a origem
            // e o outro desce em direcao ao destino: eles se separam sozinhos,
            // que e o que faz a meia-volta da Unreal ficar legivel.
            auto dirFor = [&](const ImVec2& other, float fallbackX) -> ImVec2
                {
                    if (!uTurn || !(other.x == other.x))
                        return ImVec2(fallbackX, 0.0f);

                    const float dx = other.x - selfPos.x;
                    const float dy = other.y - selfPos.y;
                    const float len = std::sqrt(dx * dx + dy * dy);

                    if (len < 1.0f) return ImVec2(fallbackX, 0.0f);

                    // Componente vertical limitada: mirar em cheio um alvo
                    // quase na vertical deixaria a tangente apontando pra
                    // cima e o fio sairia do pontinho por cima, esquisito.
                    const float ny = std::clamp(dy / len, -0.6f, 0.6f);
                    const float nx = std::sqrt(std::max(1.0f - ny * ny, 0.0f))
                        * (dx < 0.0f ? -1.0f : 1.0f);

                    return ImVec2(nx, ny);
                };

            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 30));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 90));
            ed::PushStyleVar(ed::StyleVar_NodeRounding, 20.0f);
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);
            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(2, 2, 2, 2));
            ed::BeginNode(node->ID);

            float dotSz = ICON_SZ * 0.45f;
            ImGui::BeginGroup();
            if (!node->Inputs.empty())
            {
                auto& pin = node->Inputs[0];

                // Cada ponta leva a SUA forca: as duas viajam distancias
                // diferentes, entao um valor unico deixaria uma delas torta.
                if (uTurn) ed::PushStyleVar(ed::StyleVar_LinkStrength, strengthFor(srcPos));
                ed::PushStyleVar(ed::StyleVar_TargetDirection, dirFor(srcPos, inDirX));
                ed::BeginPin(pin.ID, ed::PinKind::Input);
                ax::Widgets::Icon(ImVec2(dotSz, dotSz), PinIcon(pin.Type),
                    m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), { 0,0,0,0 });
                ed::EndPin();
                ed::PopStyleVar(uTurn ? 2 : 1);
            }
            ImGui::SameLine(0, 0);
            if (!node->Outputs.empty())
            {
                auto& pin = node->Outputs[0];

                if (uTurn) ed::PushStyleVar(ed::StyleVar_LinkStrength, strengthFor(dstPos));
                ed::PushStyleVar(ed::StyleVar_SourceDirection, dirFor(dstPos, outDirX));
                ed::BeginPin(pin.ID, ed::PinKind::Output);
                ax::Widgets::Icon(ImVec2(dotSz, dotSz), PinIcon(pin.Type),
                    m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), { 0,0,0,0 });
                ed::EndPin();
                ed::PopStyleVar(uTurn ? 2 : 1);
            }
            ImGui::EndGroup();

            ed::EndNode();
            ed::PopStyleVar(3);
            ed::PopStyleColor(2);
            ImGui::PopID();
            return;
        }

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
        // Switch on String usa campo editável de largura fixa (90px) pros
        // pins de case, em vez de texto — garante espaço mesmo que o nome
        // atual seja curto (senão o campo poderia "vazar" pra fora do node).
        if (node->Name == "Switch on String") outW = std::max(outW, 90.0f);
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

        // ── Seletor de parametro do AnimGraph ────────────────────────────────
        //
        // Set Anim Float/Bool e Anim Trigger recebem o nome do parametro num
        // pin String cujo DefaultString vinha CRAVADO na criacao do node
        // ("Speed", "IsGrounded", "Attack") e nao tinha editor em lugar
        // nenhum — nao havia como trocar. O Float parecia funcionar por
        // coincidencia (o default "Speed" batia com o parametro criado); o
        // Bool escrevia eternamente em "IsGrounded". Silencioso dos dois
        // lados, porque AnimParameters::SetBool faz m_Values[name] e o
        // operator[] CRIA a entrada — nascia um parametro fantasma.
        //
        // O widget aqui e um BOTAO, nao um combo: popup aberto de dentro de um
        // node do imgui-node-editor nao expande. Ele registra o pedido e o
        // popup sai no bloco suspenso, depois dos nodes.
        {
            int wantType = 0;
            if (ScriptPin* paramPin = FindAnimParamPin(node, &wantType))
            {
                const std::string preview = paramPin->DefaultString.empty()
                    ? std::string("(escolher parametro)") : paramPin->DefaultString;

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);

                if (ImGui::Button((preview + "##animparam").c_str(), ImVec2(nodeW - 16.f, 0)))
                {
                    m_ComboNodeId = node->ID;
                    m_ComboKind = 2;
                    m_ComboRequested = true;
                    CaptureComboAnchor();
                }

                // Nome que nao existe mais no grafo (renomeado/removido) fica
                // vermelho em vez de falhar calado no Play.
                if (!paramPin->DefaultString.empty())
                {
                    bool known = false;
                    for (const auto& pr : CollectAnimGraphParams(false))
                        if (pr.first == paramPin->DefaultString) { known = true; break; }

                    if (!known)
                    {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
                        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "nao existe no grafo");
                    }
                }

                ImGui::Spacing();
            }
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
            const char* comboLabel = (curIdx >= 0) ? names[curIdx].c_str()
                : (names.empty() ? "(nenhuma configurada)" : "(selecione)");

            // Botao, nao combo: ver a nota do seletor de parametro acima —
            // popup aberto dentro de um node do node-editor nao expande, o
            // que obrigava a trocar a Action sempre pelo Script Details.
            if (ImGui::Button((std::string(comboLabel) + "##selname").c_str(),
                ImVec2(nodeW - 16.f, 0)))
            {
                m_ComboNodeId = node->ID;
                m_ComboKind = 1;
                m_ComboRequested = true;
                CaptureComboAnchor();
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

        // ── Botões +/- de casos no node Switch on Int/String ──────────────────
        // node->IntValue espelha o número de casos NUMERADOS (sem contar o
        // "Default", que é fixo e nunca aparece nesse contador). Mesmo
        // RebuildSwitchPins serve pros dois — só o tipo do pin Selection e
        // o codegen (switch vs if/else if) mudam entre Int e String.
        if (node->Name == "Switch on Int" || node->Name == "Switch on String")
        {
            int caseCount = node->IntValue >= 1 ? node->IntValue : (int)node->Outputs.size() - 1;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
            if (ImGui::SmallButton("-##swminus"))
            {
                PushUndo("Remove Switch Case");
                m_Graph->RebuildSwitchPins(node, caseCount - 1);
                CommitUndo("Remove Switch Case");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d casos", caseCount);
            ImGui::SameLine();
            if (ImGui::SmallButton("+##swplus"))
            {
                PushUndo("Add Switch Case");
                m_Graph->RebuildSwitchPins(node, caseCount + 1);
                CommitUndo("Add Switch Case");
            }
            ImGui::Spacing();
        }

        // ── Botões +/- de entradas no AND/OR ───────────────────────────────────
        // Combina A, B, C... com && ou || em cadeia (ver ResolvePin) — antes
        // só aceitava A/B fixos.
        if (node->Name == "AND" || node->Name == "OR")
        {
            int inputCount = node->IntValue >= 2 ? node->IntValue : (int)node->Inputs.size();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
            if (ImGui::SmallButton("-##logicminus"))
            {
                PushUndo("Remove Logic Input");
                m_Graph->RebuildLogicInputs(node, inputCount - 1);
                CommitUndo("Remove Logic Input");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d entradas", inputCount);
            ImGui::SameLine();
            if (ImGui::SmallButton("+##logicplus"))
            {
                PushUndo("Add Logic Input");
                m_Graph->RebuildLogicInputs(node, inputCount + 1);
                CommitUndo("Add Logic Input");
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
                // Switch on String: o NOME do pin é o valor comparado no
                // codegen (ver GenerateNode) — vira um campo editável em vez
                // de texto estático. "Default" fica sempre fixo, sem editor.
                bool editableCase = (node->Name == "Switch on String" && pin.Name != "Default");

                if (editableCase)
                {
                    float boxW = 90.f;
                    ImGui::SameLine(nodeW - boxW - 3.f - ICON_SZ - 4.f);
                    ImGui::SetCursorPosY(rowY);
                    char buf[64];
                    strncpy(buf, pin.Name.c_str(), 63); buf[63] = 0;
                    ImGui::SetNextItemWidth(boxW);
                    ImGui::PushID((int)pin.ID.Get());
                    if (ImGui::InputText("##caseval", buf, sizeof(buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    {
                        PushUndo("Rename Switch Case");
                        pin.Name = buf;
                        CommitUndo("Rename Switch Case");
                    }
                    ImGui::PopID();
                    ImGui::SameLine(0, 3);
                    ImGui::SetCursorPosY(rowY);
                    ed::BeginPin(pin.ID, ed::PinKind::Output);
                    ax::Widgets::Icon(ImVec2(ICON_SZ, ICON_SZ), PinIcon(pin.Type),
                        m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), { 0.1f,0.1f,0.1f,0.8f });
                    ed::EndPin();
                }
                else
                {
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

    // ── Parametros do AnimGraph — fonte unica ─────────────────────────────────
    //
    // Usada pelo popup do canvas E pelo painel Script Details. Duas telas
    // lendo a mesma funcao nao tem como divergir.

    // Converte o retangulo do botao recem-submetido de espaco de CANVAS para
    // espaco de TELA. Chamada logo apos o Button, enquanto ele ainda e o
    // "ultimo item" — o popup depois nasce exatamente embaixo dele, com a
    // mesma largura, em vez de aparecer solto no meio do canvas.
    void ScriptGraphWindow::CaptureComboAnchor()
    {
        const ImVec2 mn = ed::CanvasToScreen(ImGui::GetItemRectMin());
        const ImVec2 mx = ed::CanvasToScreen(ImGui::GetItemRectMax());

        m_ComboAnchor = ImVec2(mn.x, mx.y + 2.f);
        m_ComboWidth = mx.x - mn.x;
    }

    ScriptPin* ScriptGraphWindow::FindAnimParamPin(ScriptNode* node, int* wantType)
    {
        if (!node) return nullptr;

        int want;
        if (node->Name == "Set Anim Float")   want = (int)AnimParamType::Float;
        else if (node->Name == "Set Anim Bool")    want = (int)AnimParamType::Bool;
        else if (node->Name == "Anim Trigger")     want = (int)AnimParamType::Trigger;
        else return nullptr;

        for (auto& inp : node->Inputs)
            if (inp.Name == "Parametro")
            {
                if (wantType) *wantType = want;
                return &inp;
            }

        return nullptr;
    }

    std::vector<std::pair<std::string, int>>
        ScriptGraphWindow::CollectAnimGraphParams(bool forceReload)
    {
        // Cache por UUID: sem isto seria um LoadFromFile por node e por frame.
        // forceReload existe para o momento em que o popup ABRE — assim um
        // parametro criado no AnimGraph com o Script Editor aberto aparece
        // sem precisar reabrir nada.
        static std::string s_Uuid;
        static std::vector<std::pair<std::string, int>> s_Params;

        std::string uuid;
        if (m_ScriptAsset)
            for (const auto& c : m_ScriptAsset->GetComponents())
                if (!c.AnimGraphUUID.empty()) { uuid = c.AnimGraphUUID; break; }

        if (uuid != s_Uuid || forceReload)
        {
            s_Uuid = uuid;
            s_Params.clear();

            if (!uuid.empty())
            {
                const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);
                if (auto graph = rec ? AnimGraphAsset::LoadFromFile(rec->FilePath) : nullptr)
                    for (const auto& p : graph->GetParameters())
                        s_Params.emplace_back(p.Name, (int)p.Type);
            }
        }

        return s_Params;
    }

    bool ScriptGraphWindow::DrawAnimParamList(ScriptPin* paramPin, int wantType)
    {
        if (!paramPin) return false;

        const auto params = CollectAnimGraphParams(true);
        bool picked = false, any = false;

        for (const auto& p : params)
        {
            // Int serve para o Set Anim Float: o getter converte entre tipos.
            const bool fits = (p.second == wantType)
                || (wantType == (int)AnimParamType::Float && p.second == (int)AnimParamType::Int);
            if (!fits) continue;

            any = true;
            if (ImGui::Selectable(p.first.c_str(), p.first == paramPin->DefaultString))
            {
                paramPin->DefaultString = p.first;
                picked = true;
            }
        }

        if (!any)
            ImGui::TextDisabled(params.empty()
                ? "Sem AnimGraph no componente Skeletal Mesh"
                : "O grafo nao tem parametro deste tipo");

        return picked;
    }

} // namespace axe
