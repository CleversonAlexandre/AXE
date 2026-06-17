// script_node_graph.cpp
// DrawGraphWindow e DrawNodeGraph — canvas do node editor, criação/deleção de links,
// context menu (categorias + componentes), pending nodes, drop targets, Split/Recombine Pin,
// Promote to Variable, Get/Set Variable popup.

#include "script_graph_window.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_asset.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>
#include <cctype>

namespace ed = ax::NodeEditor;

namespace axe
{
    // ── Tabelas estáticas de nodes e categorias (compartilhadas com DrawNode) ──
    struct NE { const char* label; const char* type; };
    static const NE sEv[] = { {"On Start","OnStart"},{"On Update","OnUpdate"},
        {"On End","OnEnd"},{"On Collision","OnCollision"},{"On Event","OnEvent"} };
    static const NE sAc[] = { {"Move","Move"},{"Rotate","Rotate"},{"Apply Force","ApplyForce"},
        {"Send Event","SendEvent"},{"Print String","PrintString"},{"Destroy Entity","DestroyEntity"} };
    static const NE sLo[] = { {"Branch","Branch"},{"Compare","Compare"},
        {"Get Variable","GetVariable"},{"Set Variable","SetVariable"} };
    static const NE sMa[] = { {"Add","Add"},{"Multiply","Multiply"},{"Make Vec3","MakeVec3"} };
    static const NE sIn[] = { {"Get Key","GetKey"},{"Get Axis","GetAxis"} };
    static const NE sCast[] = {
        {"To Float","ToFloat"},{"To Int","ToInt"},{"To Bool","ToBool"},
        {"To String","ToString"},{"To Vec3","ToVec3"},{"Break Vec3","BreakVec3"},
        {"Float to Vec3","FloatToVec3"},{nullptr,nullptr}
    };

    struct CatDef { const char* name; const NE* e; int n; ImVec4 col; };
    static const CatDef s_Cats[] = {
        {"Events",  sEv,   5, {0.85f,0.3f,0.2f, 1}},
        {"Actions", sAc,   6, {0.2f,0.7f,0.45f, 1}},
        {"Logic",   sLo,   4, {0.8f,0.6f,0.1f,  1}},
        {"Math",    sMa,   3, {0.3f,0.5f,0.9f,  1}},
        {"Input",   sIn,   2, {0.7f,0.2f,0.6f,  1}},
        {"Cast",    sCast, 7, {0.5f,0.8f,0.8f,  1}},
    };
    static const ImVec4 s_CtxCols[] = {
        {1.f,0.45f,0.35f,1},{0.3f,0.85f,0.55f,1},
        {1.f,0.78f,0.2f,1},{0.4f,0.65f,1.f,1},{0.85f,0.3f,0.75f,1},{0.5f,0.9f,0.9f,1}
    };

    struct CompNodeEntry { const char* label; const char* type; };
    static const CompNodeEntry s_TransformNodes[] = {
        {"Get Transform","GetTransform"},{"Set Transform","SetTransform"},
        {"Get Position","GetPosition"},{"Set Position","SetPosition"},
    };
    static const CompNodeEntry s_RigidbodyNodes[] = {
        {"Get Rigidbody","GetRigidbody"},{"Set Velocity","SetRigidbodyVelocity"},{"Apply Force","ApplyForce"},
    };
    static const CompNodeEntry s_ColliderNodes[] = {
        {"Get Collider","GetCollider"},{"On Collision","OnCollision"},
    };
    static const CompNodeEntry s_CCNodes[] = {
        {"Get Character Ctrl","GetCharacterController"},
        {"Character Move","CharacterMove"},{"Character Jump","CharacterJump"},
    };
    static const CompNodeEntry s_SpringArmNodes[] = {
        {"Get Spring Arm","GetSpringArm"},{"Set Spring Arm","SetSpringArm"},
    };
    static const CompNodeEntry s_CameraNodes[] = {
        {"Get Camera","GetCamera"},{"Set Camera FOV","SetCameraFOV"},
    };

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
    void ScriptGraphWindow::DrawNodeGraph()
    {
        if (!m_Graph) return;

        ed::SetCurrentEditor(m_EdCtx);
        ed::Begin("##SG", ImVec2(0, 0));
        m_InsideNodeEditorFrame = true;

        // Restaura posições salvas no JSON na primeira frame
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

        // ── Criação de links ──────────────────────────────────────────────────
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
                        ed::Suspend();
                        ImGui::SetTooltip("Conecte Output → Input");
                        ed::Resume();
                    }
                    else if (o->Type == i->Type)
                    {
                        // Tipos idênticos — aceita direto
                        if (ed::AcceptNewItem(GetPinColor(o->Type), 2.5f))
                        {
                            PushUndo("Add Link");
                            m_Graph->AddLink(o->ID, i->ID);
                            CommitUndo("Add Link");
                        }
                    }
                    else
                    {
                        // Tipos diferentes — verifica se há conversão automática disponível
                        auto isNumeric = [](ScriptPinType t) {
                            return t == ScriptPinType::Float || t == ScriptPinType::Int || t == ScriptPinType::Bool;
                            };
                        auto isVec = [](ScriptPinType t) {
                            return t == ScriptPinType::Vec3 || t == ScriptPinType::Vec4;
                            };

                        // Determina o node de conversão adequado
                        const char* convNodeType = nullptr;
                        const char* convTooltip = nullptr;

                        if (isNumeric(o->Type) && i->Type == ScriptPinType::Float)
                        {
                            convNodeType = "ToFloat";  convTooltip = "Inserir To Float";
                        }
                        else if (isNumeric(o->Type) && i->Type == ScriptPinType::Int)
                        {
                            convNodeType = "ToInt";    convTooltip = "Inserir To Int";
                        }
                        else if (isNumeric(o->Type) && i->Type == ScriptPinType::Bool)
                        {
                            convNodeType = "ToBool";   convTooltip = "Inserir To Bool";
                        }
                        else if ((isNumeric(o->Type) || isVec(o->Type)) && i->Type == ScriptPinType::String)
                        {
                            convNodeType = "ToString"; convTooltip = "Inserir To String";
                        }
                        else if (isVec(o->Type) && isVec(i->Type))
                        {
                            // Vec3 ↔ Vec4 — aceita com aviso
                            if (ed::AcceptNewItem(ImColor(220, 140, 40), 2.5f))
                            {
                                PushUndo("Add Link (cast Vec)");
                                m_Graph->AddLink(o->ID, i->ID);
                                CommitUndo("Add Link (cast Vec)");
                            }
                            ed::Suspend();
                            ImGui::SetTooltip("Cast implícito Vec3 ↔ Vec4");
                            ed::Resume();
                        }

                        if (convNodeType)
                        {
                            // Mostra preview laranja e tooltip
                            if (ed::AcceptNewItem(ImColor(220, 140, 40), 2.5f))
                            {
                                // Insere o node de conversão automaticamente
                                PushUndo(std::string("Add Conversion: ") + convNodeType);

                                // Posiciona entre os dois pins
                                ImVec2 posO = ed::GetNodePosition(
                                    [&]() -> ed::NodeId {
                                        for (auto& n : m_Graph->GetNodes())
                                            for (auto& p : n->Outputs)
                                                if (&p == o) return n->ID;
                                        return ed::NodeId{};
                                    }());
                                ImVec2 posI = ed::GetNodePosition(
                                    [&]() -> ed::NodeId {
                                        for (auto& n : m_Graph->GetNodes())
                                            for (auto& p : n->Inputs)
                                                if (&p == i) return n->ID;
                                        return ed::NodeId{};
                                    }());
                                ImVec2 midPos = ImVec2(
                                    (posO.x + posI.x) * 0.5f,
                                    (posO.y + posI.y) * 0.5f);

                                auto* conv = m_Graph->AddNode(convNodeType);
                                if (conv)
                                {
                                    ed::SetNodePosition(conv->ID, midPos);
                                    // Conecta: output original → input do conv → output do conv → input destino
                                    for (auto& p : conv->Inputs)
                                        if (p.Name == "Value") { m_Graph->AddLink(o->ID, p.ID); break; }
                                    for (auto& p : conv->Outputs)
                                        if (p.Name == "Value") { m_Graph->AddLink(p.ID, i->ID); break; }
                                    m_ConsoleLines.push_back(std::string("[Info] Conversão automática: ") + convNodeType);
                                }
                                CommitUndo(std::string("Add Conversion: ") + convNodeType);
                            }
                            ed::Suspend();
                            ImGui::SetTooltip("%s (automático)", convTooltip);
                            ed::Resume();
                        }
                        else if (o->Type != ScriptPinType::Flow && i->Type != ScriptPinType::Flow)
                        {
                            // Sem conversão disponível — rejeita com tooltip específico
                            ed::RejectNewItem(ImColor(220, 40, 40), 2);
                            ed::Suspend();
                            ImGui::SetTooltip("%s", GetPinIncompatibleReason(o->Type, i->Type));
                            ed::Resume();
                        }
                        else
                        {
                            // Flow misturado com dados
                            ed::RejectNewItem(ImColor(220, 40, 40), 2);
                            ed::Suspend();
                            ImGui::SetTooltip("Flow nao conecta com dados");
                            ed::Resume();
                        }
                    }
                }
            }
        }
        ed::EndCreate();

        // ── Deleção pendente de nodes ─────────────────────────────────────────
        for (auto& pendNid : m_PendingDeleteNodes)
        {
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
        if (!m_PendingDeleteNodes.empty()) PushUndo("Delete Nodes");
        m_PendingDeleteNodes.clear();

        // ── Deleção via tecla Delete ──────────────────────────────────────────
        if (ed::BeginDelete())
        {
            ed::LinkId lid;
            bool anyDeleted = false;
            while (ed::QueryDeletedLink(&lid))
                if (ed::AcceptDeletedItem())
                {
                    if (!anyDeleted) { PushUndo("Delete"); anyDeleted = true; }
                    m_Graph->RemoveLink(lid);
                }
            ed::NodeId nid;
            while (ed::QueryDeletedNode(&nid))
                if (ed::AcceptDeletedItem())
                {
                    if (!anyDeleted) { PushUndo("Delete"); anyDeleted = true; }
                    m_Graph->RemoveNode(nid);
                }
            if (anyDeleted) CommitUndo("Delete");
        }
        ed::EndDelete();

        // ── Pending node (criado via drag de Script Members / Override Events) ─
        if (!m_PendingNodeType.empty() && m_Graph)
        {
            PushUndo("Add Node: " + m_PendingNodeType);
            auto* node = m_Graph->AddNode(m_PendingNodeType.c_str());
            if (node)
            {
                ImVec2 pos = m_PendingNodePos;

                if (m_PendingPromotePinId != ed::PinId{})
                {
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

                ImVec2 finalPos = pos;
                if (m_PendingNodeType == "GetVariable" || m_PendingNodeType == "SetVariable")
                    finalPos = ed::ScreenToCanvas(pos);
                ed::SetNodePosition(node->ID, finalPos);

                if (!m_PendingNodeStrValue.empty())
                    node->StringValue = m_PendingNodeStrValue;

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
                    for (auto& p : node->Inputs)  if (p.Name == "Value") p.Type = pt;
                    for (auto& p : node->Outputs) if (p.Name == "Value") p.Type = pt;
                    m_PendingVarType = 0;
                }

                if (m_PendingPromotePinId != ed::PinId{})
                {
                    node->IntValue = m_PendingPromoteVarType;
                    for (auto& p : node->Inputs)  if (p.Name == "Value") p.Type = m_PendingPromotePinType;
                    for (auto& p : node->Outputs) if (p.Name == "Value") p.Type = m_PendingPromotePinType;

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

        // ── Pin context (Split / Recombine / Promote) ─────────────────────────
        if (ImGui::BeginPopup("##PinCtx"))
        {
            ScriptPin* ctxPin = m_Graph->FindPin(m_CtxPinId);
            ScriptNode* ctxNode = nullptr;
            if (ctxPin)
                for (auto& n : m_Graph->GetNodes())
                {
                    for (auto& p : n->Inputs)  if (&p == ctxPin) { ctxNode = n.get(); break; }
                    if (ctxNode) break;
                    for (auto& p : n->Outputs) if (&p == ctxPin) { ctxNode = n.get(); break; }
                    if (ctxNode) break;
                }

            bool isVarNode = ctxNode &&
                (ctxNode->Name == "Get Variable" || ctxNode->Name == "Set Variable");
            bool isVec3Var = false;
            if (isVarNode && m_ScriptAsset)
            {
                int vt = ctxNode->IntValue & 0xFF;
                isVec3Var = (vt == (int)ScriptVarType::Vec3);
                if (!isVec3Var)
                    for (auto& v : m_ScriptAsset->GetVariables())
                        if (v.Name == ctxNode->StringValue)
                        {
                            isVec3Var = (v.Type == ScriptVarType::Vec3); break;
                        }
            }

            if (isVarNode && isVec3Var)
            {
                bool clickedOutput = ctxPin && ctxPin->Kind == ed::PinKind::Output;
                bool clickedInput = ctxPin && ctxPin->Kind == ed::PinKind::Input;

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
                        ctxNode->Outputs.erase(std::remove_if(ctxNode->Outputs.begin(), ctxNode->Outputs.end(),
                            [](const ScriptPin& p) { return p.Name == "Value"; }), ctxNode->Outputs.end());
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), "X", ScriptPinType::Float, ed::PinKind::Output);
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), "Y", ScriptPinType::Float, ed::PinKind::Output);
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), "Z", ScriptPinType::Float, ed::PinKind::Output);
                    }
                    else
                    {
                        ctxNode->Inputs.erase(std::remove_if(ctxNode->Inputs.begin(), ctxNode->Inputs.end(),
                            [](const ScriptPin& p) { return p.Name == "Value"; }), ctxNode->Inputs.end());
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
                        ctxNode->Outputs.erase(std::remove_if(ctxNode->Outputs.begin(), ctxNode->Outputs.end(),
                            [](const ScriptPin& p) { return p.Name == "X" || p.Name == "Y" || p.Name == "Z"; }), ctxNode->Outputs.end());
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), "Value", ScriptPinType::Vec3, ed::PinKind::Output);
                    }
                    else
                    {
                        ctxNode->Inputs.erase(std::remove_if(ctxNode->Inputs.begin(), ctxNode->Inputs.end(),
                            [](const ScriptPin& p) { return p.Name == "X" || p.Name == "Y" || p.Name == "Z"; }), ctxNode->Inputs.end());
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), "Value", ScriptPinType::Vec3, ed::PinKind::Input);
                    }
                    outputSplit = inputSplit = false;
                    for (auto& p : ctxNode->Outputs) if (p.Name == "X" || p.Name == "Y" || p.Name == "Z") { outputSplit = true; break; }
                    for (auto& p : ctxNode->Inputs)  if (p.Name == "X" || p.Name == "Y" || p.Name == "Z") { inputSplit = true; break; }
                    if (!outputSplit && !inputSplit) ctxNode->IntValue &= ~0x100;
                    m_ConsoleLines.push_back("[Info] Pin recombined: " + ctxNode->StringValue);
                }
            }
            else if (ctxNode && ctxPin && ctxPin->Type != ScriptPinType::Flow)
            {
                // ── Show Exec Pins ────────────────────────────────────────────
                // Adiciona Flow In/Out ao node de dados para encadeá-lo no flow
                bool hasExec = false;
                for (auto& p : ctxNode->Inputs)  if (p.Type == ScriptPinType::Flow) { hasExec = true; break; }
                for (auto& p : ctxNode->Outputs) if (p.Type == ScriptPinType::Flow) { hasExec = true; break; }

                if (!hasExec && ImGui::MenuItem("Show Exec Pins"))
                {
                    // Insere Flow In no início dos inputs e Flow Out no início dos outputs
                    ctxNode->Inputs.emplace(ctxNode->Inputs.begin(),
                        m_Graph->GetNextId(), "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
                    ctxNode->Outputs.emplace(ctxNode->Outputs.begin(),
                        m_Graph->GetNextId(), "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
                    m_ConsoleLines.push_back("[Info] Exec pins adicionados: " + ctxNode->Name);
                }
                else if (hasExec && ImGui::MenuItem("Hide Exec Pins"))
                {
                    ctxNode->Inputs.erase(std::remove_if(ctxNode->Inputs.begin(), ctxNode->Inputs.end(),
                        [](const ScriptPin& p) { return p.Type == ScriptPinType::Flow; }), ctxNode->Inputs.end());
                    ctxNode->Outputs.erase(std::remove_if(ctxNode->Outputs.begin(), ctxNode->Outputs.end(),
                        [](const ScriptPin& p) { return p.Type == ScriptPinType::Flow; }), ctxNode->Outputs.end());
                    m_ConsoleLines.push_back("[Info] Exec pins removidos: " + ctxNode->Name);
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Promote to Variable"))
                {
                    ScriptVariable newVar;
                    newVar.Name = ctxPin->Name.empty() ? "NewVar" : ctxPin->Name;
                    switch (ctxPin->Type) {
                    case ScriptPinType::Bool:   newVar.Type = ScriptVarType::Bool;   break;
                    case ScriptPinType::Int:    newVar.Type = ScriptVarType::Int;    break;
                    case ScriptPinType::Vec3:   newVar.Type = ScriptVarType::Vec3;   break;
                    case ScriptPinType::String: newVar.Type = ScriptVarType::String; break;
                    default:                    newVar.Type = ScriptVarType::Float;  break;
                    }
                    if (m_ScriptAsset) m_ScriptAsset->AddVariable(newVar);

                    bool isInput = ctxPin->Kind == ed::PinKind::Input;
                    m_PendingNodeType = isInput ? "SetVariable" : "GetVariable";
                    m_PendingNodeStrValue = newVar.Name;
                    m_PendingNodePos = m_GraphWindowCenter;
                    m_PendingPromotePinId = ctxPin->ID;
                    m_PendingPromoteIsInput = isInput;
                    m_PendingPromoteVarType = (int)newVar.Type;
                    m_PendingPromotePinType = ctxPin->Type;
                }
            }
            else
                ImGui::TextDisabled("No actions available");

            ImGui::EndPopup();
        }

        // ── Node context ──────────────────────────────────────────────────────
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
            ScriptVarType vt = ScriptVarType::Float;
            if (m_ScriptAsset)
                for (auto& v : m_ScriptAsset->GetVariables())
                    if (v.Name == m_VarDropName) { vt = v.Type; break; }

            ImColor vc = axe::GetVariableNodeColor((int)vt);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ vc.Value.x, vc.Value.y, vc.Value.z, 1.f });
            ImGui::TextUnformatted(m_VarDropName.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();

            if (ImGui::MenuItem("Get"))
            {
                m_PendingNodeType = "GetVariable"; m_PendingNodePos = m_VarDropPos;
                m_PendingNodeStrValue = m_VarDropName; m_PendingVarType = (int)vt;
                m_VarDropIsCanvas = false; ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Set"))
            {
                m_PendingNodeType = "SetVariable"; m_PendingNodePos = m_VarDropPos;
                m_PendingNodeStrValue = m_VarDropName; m_PendingVarType = (int)vt;
                m_VarDropIsCanvas = false; ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ── Background context popup ──────────────────────────────────────────
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

            // Categorias estáticas
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
                bool show = filtering || ImGui::CollapsingHeader(cat.name,
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

            // Categoria Components
            if (m_ScriptAsset && !m_ScriptAsset->GetComponents().empty())
            {
                ImVec4 compCol = { 0.6f, 0.85f, 1.0f, 1.f };
                bool anyCompMatch = !filtering;
                if (filtering)
                {
                    std::string tl = "transform";
                    if (tl.find(s) != std::string::npos) anyCompMatch = true;
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
                    bool showComp = filtering || ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen);
                    ImGui::PopStyleColor(2);

                    if (showComp)
                    {
                        ImGui::Indent(6);
                        auto drawGroup = [&](const CompNodeEntry* entries, int count, ImVec4 col)
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

                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 0.6f));
                        ImGui::TextUnformatted("-- Transform --");
                        ImGui::PopStyleColor();
                        drawGroup(s_TransformNodes, 4, { 0.75f,0.75f,0.75f,1 });

                        for (auto& def : m_ScriptAsset->GetComponents())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));
                            ImGui::Text("-- %s --", def.Type.c_str());
                            ImGui::PopStyleColor();

                            if (def.Type == "Rigidbody")                              drawGroup(s_RigidbodyNodes, 3, { 0.3f,0.8f,1.f,1 });
                            else if (def.Type.find("Collider") != std::string::npos)       drawGroup(s_ColliderNodes, 2, { 0.3f,1.f,0.5f,1 });
                            else if (def.Type == "CharacterController")                    drawGroup(s_CCNodes, 3, { 1.f,0.7f,0.2f,1 });
                            else if (def.Type == "SpringArm")                              drawGroup(s_SpringArmNodes, 2, { 0.9f,0.6f,1.f,1 });
                            else if (def.Type == "Camera")                                 drawGroup(s_CameraNodes, 2, { 0.7f,0.5f,1.f,1 });
                        }
                        ImGui::Unindent(6);
                    }
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar();
        ed::Resume();

        if (m_FirstFrame) { ed::NavigateToContent(); m_FirstFrame = false; }
        m_InsideNodeEditorFrame = false;
        ed::End();
        ed::SetCurrentEditor(nullptr);

        // ── Drop target no canvas ─────────────────────────────────────────────
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
                    if (node) { ed::SetNodePosition(node->ID, canvasPos); m_ConsoleLines.push_back("[Info] Node created: " + nodeType); }
                    ed::SetCurrentEditor(nullptr);
                }
            }

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VAR_NODE"))
            {
                std::string data = (const char*)payload->Data;
                m_VarDropName = data.substr(data.find(':') + 1);
                m_VarDropPos = ImGui::GetMousePos();
                m_VarDropPending = true;
            }

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

} // namespace axe