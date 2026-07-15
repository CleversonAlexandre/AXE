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
#include <cfloat>

namespace ed = ax::NodeEditor;

// Helper global — detecta pin splitado ("X","Y","Z" ou "Nome.X","Nome.Y","Nome.Z")
// Fora do namespace axe pra não ter conflito com AXE_API
static bool IsSplitPin(const axe::ScriptPin& p)
{
    const std::string& n = p.Name;
    if (n == "X" || n == "Y" || n == "Z") return true;
    if (n.size() >= 2)
    {
        const std::string suf = n.substr(n.size() - 2);
        return suf == ".X" || suf == ".Y" || suf == ".Z";
    }
    return false;
}

namespace axe
{
    // ── Tabelas estáticas de nodes e categorias (compartilhadas com DrawNode) ──
    struct NE { const char* label; const char* type; };
    static const NE sEv[] = { {"On Start","OnStart"},{"On Update","OnUpdate"},
        {"On End","OnEnd"},{"On Collision","OnCollision"},{"On Event","OnEvent"} };
    static const NE sAc[] = { {"Move","Move"},{"Rotate","Rotate"},{"Apply Force","ApplyForce"},
        {"Send Event","SendEvent"},{"Print String","PrintString"},{"Destroy Entity","DestroyEntity"},
        {"Is Valid","IsValid"},{"Get Self","GetSelf"},
        {"Particle Play","ParticlePlay"},{"Particle Stop","ParticleStop"},
        {"Particle Restart","ParticleRestart"},{"Particle Burst","ParticleBurst"},

        // ── Animação (AnimGraph) ──────────────────────────────────────────────
        //
        // Ficam nas Acoes GERAIS, e nao presos a um componente declarado.
        //
        // Motivo pratico: o pawn que dirige a animacao raramente e o mesmo
        // objeto que TEM o SkeletalMeshComponent — e, se fossem gated por
        // componente, o no simplesmente nao apareceria no menu e voce ficaria
        // procurando um bug que nao existe.
        //
        // Escrever num personagem sem AnimGraph e no-op silencioso, entao nao
        // ha risco.
        {"Set Anim Float","SetAnimFloat"},{"Set Anim Bool","SetAnimBool"},
        {"Anim Trigger","SetAnimTrigger"},{"Get Anim State","GetAnimState"} };
    static const NE sTr[] = {
        {"Get Position","GetOtherPosition"},{"Set Position","SetOtherPosition"},
        {"Get Rotation","GetOtherRotation"},{"Set Rotation","SetOtherRotation"},
        {"Get Scale","GetOtherScale"},{"Set Scale","SetOtherScale"},
        {"Get Forward Vector","GetForwardVector"} };
    static const NE sCam[] = {
        {"Camera Shake","CameraShake"},{"Camera Follow","CameraFollow"},
        {"Camera Stop Follow","CameraStopFollow"},{"Set Camera FOV","SetCameraFOV"} };
    static const NE sLo[] = { {"Branch","Branch"},{"Compare","Compare"},
        {"Get Variable","GetVariable"},{"Set Variable","SetVariable"},
        {"AND","And"},{"OR","Or"},{"NOT","Not"},{"XOR","Xor"} };
    static const NE sMa[] = { {"Add","Add"},{"Subtract","Subtract"},{"Multiply","Multiply"},
        {"Divide","Divide"},{"Min","Min"},{"Max","Max"},{"Abs","Abs"},{"Negate","Negate"},
        {"Clamp","Clamp"},{"Lerp","Lerp"},{"Make Vec3","MakeVec3"},
        {"Random Float","RandomFloat"},{"Random Int","RandomInt"},{"Random Bool","RandomBool"},
        {"Random Range (Vec3)","RandomRange"},
        {"Concat","Concat"},{"String Length","StringLength"},{"Contains","Contains"},{"Substring","Substring"} };
    static const NE sIn[] = { {"Get Action","GetAction"},{"Get Axis","GetAxis"} };
    static const NE sArr[] = { {"Array Add","ArrayAdd"},{"Array Remove","ArrayRemove"},
        {"Array Get","ArrayGet"},{"Array Length","ArrayLength"},{"Array Clear","ArrayClear"} };
    static const NE sFlow[] = { {"Sequence","Sequence"},{"For Loop","ForLoop"},{"For Each Loop","ForEachLoop"},
        {"While Loop","WhileLoop"},{"Break","Break"},{"Continue","Continue"},{"Switch on Int","SwitchOnInt"},
        {"Switch on String","SwitchOnString"},{"Delay","Delay"} };
    static const NE sCast[] = {
        {"To Float","ToFloat"},{"To Int","ToInt"},{"To Bool","ToBool"},
        {"To String","ToString"},{"To Vec3","ToVec3"},{"Break Vec3","BreakVec3"},
        {"Float to Vec3","FloatToVec3"},{nullptr,nullptr}
    };

    struct CatDef { const char* name; const NE* e; int n; ImVec4 col; };
    static const CatDef s_Cats[] = {
        {"Events",        sEv,   5,  {0.85f,0.3f,0.2f,  1}},
        // 16 = 12 originais + 4 de animacao.
        //
        // NOTA: estava 11 com 12 entradas no array — o "Particle Burst" nunca
        // aparecia no menu. Contagem hardcoded e um convite a esse tipo de bug
        // silencioso; o ideal seria std::size(sAc).
        {"Actions",       sAc,  16,  {0.2f, 0.7f,0.45f, 1}},
        {"Transform",     sTr,   7,  {0.9f, 0.65f,0.2f, 1}},
        {"Camera",        sCam,  4,  {0.3f, 0.7f,0.95f, 1}},
        {"Logic",         sLo,   8,  {0.8f, 0.6f,0.1f,  1}},
        {"Math",          sMa,  19,  {0.3f, 0.5f,0.9f,  1}},
        {"Input",         sIn,   2,  {0.7f, 0.2f,0.6f,  1}},
        {"Array",         sArr,  5,  {0.55f,0.45f,0.85f,1}},
        {"Flow Control",  sFlow, 9,  {0.45f,0.6f,0.75f, 1}},
        {"Cast",          sCast, 7,  {0.5f, 0.8f,0.8f,  1}},
    };
    static const ImVec4 s_CtxCols[] = {
        {1.f,0.45f,0.35f,1},{0.3f,0.85f,0.55f,1},
        {1.f,0.78f,0.2f,1},{0.4f,0.65f,1.f,1},{0.85f,0.3f,0.75f,1},{0.7f,0.6f,0.95f,1},
        {0.55f,0.75f,0.95f,1},{0.5f,0.9f,0.9f,1}
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

    void ScriptGraphWindow::CreateCommentNode(ImVec2 fallbackPos)
    {
        if (!m_Graph) return;

        int selCount = ed::GetSelectedObjectCount();
        std::vector<ed::NodeId> selNodes(selCount > 0 ? selCount : 0);
        int got = selCount > 0 ? ed::GetSelectedNodes(selNodes.data(), selCount) : 0;

        if (got > 0)
        {
            const float kPad = 40.f;       // margem lateral/inferior
            const float kTitlePad = 70.f;  // espaço extra acima pro título do Comment
            ImVec2 bmin(FLT_MAX, FLT_MAX), bmax(-FLT_MAX, -FLT_MAX);
            for (int i = 0; i < got; i++)
            {
                ImVec2 p = ed::GetNodePosition(selNodes[i]);
                ImVec2 sz = ed::GetNodeSize(selNodes[i]);
                bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y);
                bmax.x = std::max(bmax.x, p.x + sz.x); bmax.y = std::max(bmax.y, p.y + sz.y);
            }
            auto* node = m_Graph->AddNode("Comment");
            if (node)
            {
                node->CommentSize = ImVec2(bmax.x - bmin.x + kPad * 2.f, bmax.y - bmin.y + kPad + kTitlePad);
                ed::SetNodePosition(node->ID, ImVec2(bmin.x - kPad, bmin.y - kTitlePad));
                m_ConsoleLines.push_back("[Info] Comment criado envolvendo " + std::to_string(got) + " node(s).");
            }
        }
        else
        {
            auto* node = m_Graph->AddNode("Comment");
            if (node)
            {
                ed::SetNodePosition(node->ID, fallbackPos);
                m_ConsoleLines.push_back("[Info] Node criado: Comment");
            }
        }
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

        // ── Duplo clique num fio insere um Reroute ─────────────────────────────
        // Mesmo atalho da Unreal: o fio se parte em dois (origem -> Reroute,
        // Reroute -> destino), com o tipo do Reroute já fixado no tipo do
        // fio original — sem precisar reconectar nada manualmente depois.
        // Posição em screen-space puro, sem ed::ScreenToCanvas — igual ao
        // spawnNode (menu de criar node) logo abaixo, que já funciona: dentro
        // do contexto ativo do canvas (mesmo Begin/End), ed::SetNodePosition
        // já espera a posição NESSE espaço. ScreenToCanvas só entra quando a
        // posição vem de FORA desse contexto (drag do painel Script Members,
        // ou os handlers que rodam depois do ed::End()) — confundir isso foi
        // o que fazia o node nascer em (0,0), longe de tudo.
        {
            ed::LinkId hoveredLink = ed::GetHoveredLink();
            if (hoveredLink != ed::LinkId{} && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                ed::PinId startId{}, endId{};
                bool found = false;
                for (auto& lk : m_Graph->GetLinks())
                    if (lk.ID == hoveredLink) { startId = lk.StartPin; endId = lk.EndPin; found = true; break; }

                if (found)
                {
                    auto* pStart = m_Graph->FindPin(startId);
                    auto* pEnd = m_Graph->FindPin(endId);
                    if (pStart && pEnd)
                    {
                        PushUndo("Insert Reroute");
                        ScriptPinType t = pStart->Type;
                        auto* reroute = m_Graph->AddNode("Reroute");
                        if (reroute)
                        {
                            reroute->Inputs[0].Type = t;
                            reroute->Outputs[0].Type = t;
                            ed::SetNodePosition(reroute->ID, ImGui::GetMousePos());
                            m_Graph->RemoveLink(hoveredLink);
                            m_Graph->AddLink(startId, reroute->Inputs[0].ID);
                            m_Graph->AddLink(reroute->Outputs[0].ID, endId);
                            m_ConsoleLines.push_back("[Info] Reroute inserido no fio.");
                        }
                        CommitUndo("Insert Reroute");
                    }
                }
            }
        }

        // ── Atalho 'C' cria um Comment ──────────────────────────────────────────
        // Mesmo atalho da Unreal — envolve a seleção atual, se houver (ver
        // CreateCommentNode). Só dispara com o canvas em foco/hover e fora
        // de qualquer campo de texto (renomear variável, busca do menu, etc.)
        // — senão digitar a letra 'c' em qualquer InputText do editor criaria
        // um Comment sem querer. Mesma posição crua (sem ScreenToCanvas) do
        // Reroute acima, pelo mesmo motivo.
        if (ImGui::IsWindowHovered() && !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_C, false))
        {
            PushUndo("Add Comment");
            CreateCommentNode(ImGui::GetMousePos());
            CommitUndo("Add Comment");
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
                    else if (o->Type == ScriptPinType::Wildcard && IsArrayPinType(i->Type))
                    {
                        // Pin Wildcard de saída de um node genérico de Array (Array
                        // Get) conectando a um pin de array real de ENTRADA — fixa o
                        // node Wildcard no tipo concreto. Verificado ANTES do bloco de
                        // Cast (mais abaixo) para não competir com a lógica de
                        // conversão entre tipos escalares, que continua tratando
                        // Wildcard normalmente para os casos não-array (To Float etc.).
                        if (ed::AcceptNewItem(GetPinColor(i->Type), 2.5f))
                        {
                            PushUndo("Add Link (array)");
                            m_Graph->AddLink(o->ID, i->ID);
                            ScriptVarType concreteVarType = (ScriptVarType)(
                                (int)i->Type - (int)ScriptPinType::FloatArray + (int)ScriptVarType::FloatArray);
                            ScriptVarType elemType = GetElementType(concreteVarType);
                            for (auto& n : m_Graph->GetNodes())
                            {
                                bool owns = false;
                                for (auto& p : n->Outputs) if (&p == o) owns = true;
                                if (owns) { m_Graph->RebuildArrayNodePins(n.get(), elemType); break; }
                            }
                            CommitUndo("Add Link (array)");
                        }
                    }
                    else if (i->Type == ScriptPinType::Wildcard && IsArrayPinType(o->Type))
                    {
                        // Mesmo caso, mas com o pin Wildcard de ENTRADA (Array Add/
                        // Remove/Get/Length/Clear) recebendo de uma saída de array real.
                        if (ed::AcceptNewItem(GetPinColor(o->Type), 2.5f))
                        {
                            PushUndo("Add Link (array)");
                            m_Graph->AddLink(o->ID, i->ID);
                            ScriptVarType concreteVarType = (ScriptVarType)(
                                (int)o->Type - (int)ScriptPinType::FloatArray + (int)ScriptVarType::FloatArray);
                            ScriptVarType elemType = GetElementType(concreteVarType);
                            for (auto& n : m_Graph->GetNodes())
                            {
                                bool owns = false;
                                for (auto& p : n->Inputs) if (&p == i) owns = true;
                                if (owns) { m_Graph->RebuildArrayNodePins(n.get(), elemType); break; }
                            }
                            CommitUndo("Add Link (array)");
                        }
                    }
                    else if (o->Type == ScriptPinType::Wildcard && i->Type == ScriptPinType::Wildcard &&
                        [&] { for (auto& n : m_Graph->GetNodes()) if (n->Name == "Reroute") for (auto& p : n->Outputs) if (&p == o) return true; return false; }())
                    {
                        // Caso raro: dois Reroutes ainda não fixados sendo
                        // ligados entre si (nenhum lado tem tipo concreto
                        // ainda). Aceita sem fixar nada — fica resolvido
                        // automaticamente assim que QUALQUER um dos dois
                        // lados da cadeia for conectado a algo concreto,
                        // já que o bloco abaixo sempre propaga pra ambos os
                        // pins do Reroute envolvido naquela conexão.
                        if (ed::AcceptNewItem(GetPinColor(ScriptPinType::Wildcard), 2.5f))
                        {
                            PushUndo("Add Link (reroute)");
                            m_Graph->AddLink(o->ID, i->ID);
                            CommitUndo("Add Link (reroute)");
                        }
                    }
                    else if ((o->Type == ScriptPinType::Wildcard) != (i->Type == ScriptPinType::Wildcard) &&
                        [&] {
                            // Só entra aqui se o lado Wildcard pertencer a um
                            // node Reroute — outros usos de Wildcard (Array,
                            // Cast) já têm suas próprias regras nos blocos
                            // acima/abaixo, não competem com esta aqui.
                            ScriptPin* w = (o->Type == ScriptPinType::Wildcard) ? o : i;
                            for (auto& n : m_Graph->GetNodes())
                            {
                                if (n->Name != "Reroute") continue;
                                for (auto& p : n->Inputs)  if (&p == w) return true;
                                for (auto& p : n->Outputs) if (&p == w) return true;
                            }
                            return false;
                        }())
                    {
                        // Reroute aceita QUALQUER tipo, inclusive Flow — fixa
                        // os DOIS pins do Reroute (In e Out) pro tipo
                        // concreto do outro lado, então o lado ainda solto já
                        // sai pronto pra aceitar qualquer link do mesmo tipo,
                        // sem precisar resolver de novo depois.
                        ScriptPinType concreteType = (o->Type != ScriptPinType::Wildcard) ? o->Type : i->Type;
                        if (ed::AcceptNewItem(GetPinColor(concreteType), 2.5f))
                        {
                            PushUndo("Add Link (reroute)");
                            m_Graph->AddLink(o->ID, i->ID);
                            ScriptPin* w = (o->Type == ScriptPinType::Wildcard) ? o : i;
                            for (auto& n : m_Graph->GetNodes())
                            {
                                bool owns = false;
                                for (auto& p : n->Inputs)  if (&p == w) owns = true;
                                for (auto& p : n->Outputs) if (&p == w) owns = true;
                                if (owns)
                                {
                                    for (auto& p : n->Inputs)  p.Type = concreteType;
                                    for (auto& p : n->Outputs) p.Type = concreteType;
                                    break;
                                }
                            }
                            CommitUndo("Add Link (reroute)");
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
                // BUGFIX/proteção: Function Entry e Return Node são geridos
                // automaticamente por ScriptAsset::AddFunction — deletar um
                // dos dois deixaria a função sem entrada ou sem saída,
                // quebrando RebuildFunctionCallSites/GenerateFunctionBody
                // (que esperam encontrar exatamente um de cada no grafo).
                if (node->Name == "Function Entry" || node->Name == "Return Node")
                {
                    m_ConsoleLines.push_back("[Aviso] '" + node->Name + "' nao pode ser deletado — e gerido automaticamente pela Function.");
                    continue;
                }

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
            {
                // Mesma proteção do bloco acima, agora pro caminho da tecla
                // Delete/Backspace e do "Delete" do menu de contexto — os dois
                // caminhos convergem aqui no imgui-node-editor.
                auto* node = m_Graph->FindNode(nid);
                if (node && (node->Name == "Function Entry" || node->Name == "Return Node"))
                {
                    ed::RejectDeletedItem();
                    m_ConsoleLines.push_back("[Aviso] '" + node->Name + "' nao pode ser deletado — e gerido automaticamente pela Function.");
                    continue;
                }
                if (ed::AcceptDeletedItem())
                {
                    if (!anyDeleted) { PushUndo("Delete"); anyDeleted = true; }
                    m_Graph->RemoveNode(nid);
                }
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
                    // ScriptVarTypeToPinType cobre todos os 18 tipos (9
                    // escalares + 9 arrays) — o switch manual anterior só
                    // tratava Bool/Int/Vec3/String, deixando Vec2/Vec4/Quat/
                    // Entity/qualquer Array cair no default (sempre Float).
                    ScriptPinType pt = ScriptVarTypeToPinType((ScriptVarType)m_PendingVarType);
                    for (auto& p : node->Inputs)  if (p.Name == "Value") p.Type = pt;
                    for (auto& p : node->Outputs) if (p.Name == "Value") p.Type = pt;
                    m_PendingVarType = 0;
                }

                // Cópia ÚNICA do default da variável para os campos locais do
                // node, só na criação — não é um binding contínuo (ver bugfix
                // em script_node_draw.cpp). Sem isso, um Set Variable recém
                // criado começaria em 0/false/"" mesmo quando a variável já
                // tem um default diferente, o que seria uma surpresa
                // desagradável. Depois de criado, o valor é independente.
                if (m_PendingNodeType == "SetVariable" && m_ScriptAsset)
                {
                    for (auto& v : m_ScriptAsset->GetVariables())
                    {
                        if (v.Name != node->StringValue) continue;
                        switch (v.Type)
                        {
                        case ScriptVarType::Float: node->FloatValue = v.DefaultFloat; break;
                        case ScriptVarType::Bool:  node->BoolValue = v.DefaultBool; break;
                        case ScriptVarType::Int:   node->IntLocalValue = v.DefaultInt; break;
                        case ScriptVarType::Vec3:
                            node->Vec3Value[0] = v.DefaultVec3[0];
                            node->Vec3Value[1] = v.DefaultVec3[1];
                            node->Vec3Value[2] = v.DefaultVec3[2];
                            break;
                        case ScriptVarType::String: node->StringLocalValue = v.DefaultString; break;
                        default: break;
                        }
                        break;
                    }
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
        // openPopupPosition fica em screen-space mesmo (sem ed::ScreenToCanvas)
        // de propósito: dentro do contexto ativo do canvas (mesmo Begin/End),
        // ed::SetNodePosition já espera a posição NESSE espaço — é só quando
        // a posição vem de FORA desse contexto (drag do painel Script
        // Members, ou os handlers que rodam depois do ed::End()) que
        // ed::ScreenToCanvas entra. Confundir isso foi o que causou os nodes
        // nascendo em (0,0), longe de tudo.
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

            // Detecta se o pin clicado (ou qualquer pin do node) é Vec3 —
            // permite Split/Recombine em QUALQUER node com pin Vec3,
            // não só em variáveis. Ex: Velocity de Get Rigidbody,
            // Position de Get Position, Forward de Get Forward Vector, etc.
            bool hasClickedVec3Pin = false;
            bool hasClickedSplitPin = false; // pin já splitado (Float tipo "Nome.X")
            if (ctxNode && ctxPin)
            {
                hasClickedVec3Pin = (ctxPin->Type == ScriptPinType::Vec3);
                hasClickedSplitPin = IsSplitPin(*ctxPin); // Float "X","Y","Z","Nome.X" etc.
            }
            bool hasAnyVec3Pin = false;
            if (ctxNode && !ctxPin)
            {
                for (auto& p : ctxNode->Outputs)
                    if (p.Type == ScriptPinType::Vec3 || IsSplitPin(p)) { hasAnyVec3Pin = true; break; }
                if (!hasAnyVec3Pin)
                    for (auto& p : ctxNode->Inputs)
                        if (p.Type == ScriptPinType::Vec3 || IsSplitPin(p)) { hasAnyVec3Pin = true; break; }
            }
            bool canSplitRecombine = (isVarNode && isVec3Var)
                || hasClickedVec3Pin
                || hasClickedSplitPin  // <-- clicou num pin já splitado → mostra Recombine
                || hasAnyVec3Pin;

            if (canSplitRecombine)
            {
                bool clickedOutput = ctxPin && ctxPin->Kind == ed::PinKind::Output;
                bool clickedInput = ctxPin && ctxPin->Kind == ed::PinKind::Input;

                if (!ctxPin)
                {
                    // Sem pin específico: prefere lado com pin Vec3 ou split
                    for (auto& p : ctxNode->Outputs)
                        if (p.Type == ScriptPinType::Vec3 || IsSplitPin(p)) { clickedOutput = true; break; }
                    if (!clickedOutput)
                        clickedInput = true;
                }

                bool outputSplit = false, inputSplit = false;
                for (auto& p : ctxNode->Outputs) if (IsSplitPin(p)) { outputSplit = true; break; }
                for (auto& p : ctxNode->Inputs)  if (IsSplitPin(p)) { inputSplit = true; break; }
                bool thisSideSplit = clickedOutput ? outputSplit : inputSplit;

                // Nome do pin Vec3 que vai ser splitado (pode ser "Value", "Position", "Velocity", etc.)
                std::string vec3PinName = "Value"; // default para variáveis
                if (ctxPin && ctxPin->Type == ScriptPinType::Vec3)
                    vec3PinName = ctxPin->Name;
                else if (!ctxPin)
                {
                    // Acha o primeiro pin Vec3 no lado correto
                    auto& pins = clickedOutput ? ctxNode->Outputs : ctxNode->Inputs;
                    for (auto& p : pins)
                        if (p.Type == ScriptPinType::Vec3) { vec3PinName = p.Name; break; }
                }

                if (!thisSideSplit && ImGui::MenuItem("Split Struct Pin"))
                {
                    std::string prefix = (vec3PinName == "Value") ? "" : vec3PinName + ".";
                    std::string nameX = prefix + "X";
                    std::string nameY = prefix + "Y";
                    std::string nameZ = prefix + "Z";
                    std::string pinToRemove = vec3PinName;

                    if (clickedOutput)
                    {
                        ctxNode->Outputs.erase(std::remove_if(ctxNode->Outputs.begin(), ctxNode->Outputs.end(),
                            [pinToRemove](const ScriptPin& p) { return p.Name == pinToRemove; }), ctxNode->Outputs.end());
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), nameX.c_str(), ScriptPinType::Float, ed::PinKind::Output);
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), nameY.c_str(), ScriptPinType::Float, ed::PinKind::Output);
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), nameZ.c_str(), ScriptPinType::Float, ed::PinKind::Output);
                    }
                    else
                    {
                        ctxNode->Inputs.erase(std::remove_if(ctxNode->Inputs.begin(), ctxNode->Inputs.end(),
                            [pinToRemove](const ScriptPin& p) { return p.Name == pinToRemove; }), ctxNode->Inputs.end());
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), nameX.c_str(), ScriptPinType::Float, ed::PinKind::Input);
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), nameY.c_str(), ScriptPinType::Float, ed::PinKind::Input);
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), nameZ.c_str(), ScriptPinType::Float, ed::PinKind::Input);
                    }
                    ctxNode->IntValue |= 0x100;
                    m_ConsoleLines.push_back("[Info] Pin split: " + vec3PinName);
                }
                else if (thisSideSplit && ImGui::MenuItem("Recombine Pin"))
                {
                    std::string recombinedName = vec3PinName;
                    if (clickedOutput)
                    {
                        ctxNode->Outputs.erase(std::remove_if(ctxNode->Outputs.begin(), ctxNode->Outputs.end(),
                            [](const ScriptPin& p) {
                                const std::string& n = p.Name;
                                return (n.size() >= 2 && (n.substr(n.size() - 2) == ".X" ||
                                    n.substr(n.size() - 2) == ".Y" ||
                                    n.substr(n.size() - 2) == ".Z"))
                                    || n == "X" || n == "Y" || n == "Z";
                            }), ctxNode->Outputs.end());
                        ctxNode->Outputs.emplace_back(m_Graph->GetNextId(), recombinedName.c_str(), ScriptPinType::Vec3, ed::PinKind::Output);
                    }
                    else
                    {
                        ctxNode->Inputs.erase(std::remove_if(ctxNode->Inputs.begin(), ctxNode->Inputs.end(),
                            [](const ScriptPin& p) {
                                const std::string& n = p.Name;
                                return (n.size() >= 2 && (n.substr(n.size() - 2) == ".X" ||
                                    n.substr(n.size() - 2) == ".Y" ||
                                    n.substr(n.size() - 2) == ".Z"))
                                    || n == "X" || n == "Y" || n == "Z";
                            }), ctxNode->Inputs.end());
                        ctxNode->Inputs.emplace_back(m_Graph->GetNextId(), recombinedName.c_str(), ScriptPinType::Vec3, ed::PinKind::Input);
                    }
                    outputSplit = inputSplit = false;
                    for (auto& p : ctxNode->Outputs) if (IsSplitPin(p)) { outputSplit = true; break; }
                    for (auto& p : ctxNode->Inputs)  if (IsSplitPin(p)) { inputSplit = true; break; }
                    if (!outputSplit && !inputSplit) ctxNode->IntValue &= ~0x100;
                    m_ConsoleLines.push_back("[Info] Pin recombined: " + vec3PinName);
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
                // Mesma proteção do bloco de QueryDeletedNode acima, agora
                // desabilitando visualmente a opção em vez de só rejeitar
                // silenciosamente — mais claro pro usuário entender o porquê
                // antes mesmo de tentar.
                bool isProtected = (node->Name == "Function Entry" || node->Name == "Return Node");
                if (ImGui::MenuItem("Delete Node", nullptr, false, !isProtected))
                {
                    ed::DeleteNode(ctxMenuNodeId);
                    m_ConsoleLines.push_back("[Info] Node deleted.");
                }
                if (isProtected && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Gerido automaticamente pela Function — não pode ser deletado.");
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

            // "Add Comment" fica fora da árvore de categorias, em destaque no
            // topo — não é um node de lógica, é só anotação visual (mesmo
            // tratamento de destaque que a Unreal dá pra "Add Comment...").
            // Se houver nodes selecionados no momento, a caixa nasce já
            // dimensionada pra envolvê-los (com uma margem), igual ao atalho
            // C da Unreal — senão nasce em branco, no ponto do clique.
            if (!filtering || std::string("comment").find(s) != std::string::npos)
            {
                if (ImGui::MenuItem("+ Add Comment"))
                {
                    CreateCommentNode(openPopupPosition);
                    m_CtxBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
            }
            // "Add Reroute" — forma alternativa de criar (a principal é
            // duplo clique direto no fio, que já fixa o tipo automaticamente
            // pelo que está conectado; criado solto pelo menu nasce Wildcard
            // dos dois lados, igual qualquer node Wildcard novo).
            if (!filtering || std::string("reroute").find(s) != std::string::npos)
            {
                if (ImGui::MenuItem("+ Add Reroute"))
                    spawnNode("Reroute");
            }
            if (!filtering || std::string("comment").find(s) != std::string::npos ||
                std::string("reroute").find(s) != std::string::npos)
                ImGui::Separator();

            // Categorias estáticas (Cast fica fora — índice 7 — porque é
            // auto-inserida ao conectar pins incompatíveis, não criada à mão)
            for (int ci = 0; ci < 7; ci++)
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

        // ── Libera m_SelectedVar quando a seleção do canvas muda ──────────────
        // Precisa ser feito AQUI (ainda dentro do contexto m_EdCtx, antes do
        // SetCurrentEditor(nullptr) abaixo) e por EDGE — só quando o node
        // selecionado é diferente do frame anterior — para não entrar em
        // conflito com o clique do usuário numa variável da lista do Script
        // Members (que define m_SelectedVar diretamente em script_members.cpp
        // e deve continuar valendo até o canvas mudar de novo).
        {
            int selCount = ed::GetSelectedObjectCount();
            ed::NodeId curSel;
            int got = (selCount > 0) ? ed::GetSelectedNodes(&curSel, 1) : 0;
            ed::NodeId newSel = (got > 0) ? curSel : ed::NodeId{};
            if (newSel != ed::NodeId{} && newSel != m_LastCanvasSelectedNode)
                m_SelectedVar = -1;
            m_LastCanvasSelectedNode = newSel;
        }

        // ── Duplo clique num node "Call <Function>" abre o grafo dela ─────────
        // ed::GetDoubleClickedNode() precisa do contexto ainda ativo (igual
        // GetSelectedNodes acima) — por isso vem antes do SetCurrentEditor
        // (nullptr). Igual à Unreal: dar duplo clique numa Function chamada
        // no grafo principal "entra" nela, não só seleciona.
        {
            ed::NodeId dblNode = ed::GetDoubleClickedNode();
            if (dblNode != ed::NodeId{} && m_Graph && m_ScriptAsset)
            {
                auto* node = m_Graph->FindNode(dblNode);
                if (node && node->Category == ScriptNodeCategory::Function &&
                    node->Name != "Function Entry" && node->Name != "Return Node")
                {
                    auto* func = m_ScriptAsset->FindFunction(node->StringValue);
                    if (func) SwitchToFunctionGraph(func);
                }
            }
        }

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

            // VAR_RECAT também é usado aqui — mesmo payload da fonte em
            // script_members.cpp, agora consumido para criar um node "Get"
            // ao soltar no canvas (em vez do antigo payload separado VAR_NODE,
            // removido porque SetDragDropPayload só mantém um payload ativo
            // por sessão de drag — ter dois causava o bug de recategorização
            // nunca funcionar).
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VAR_RECAT"))
            {
                int idx = *(int*)payload->Data;
                if (m_ScriptAsset && idx >= 0 && idx < (int)m_ScriptAsset->GetVariables().size())
                {
                    m_VarDropName = m_ScriptAsset->GetVariables()[idx].Name;
                    m_VarDropPos = ImGui::GetMousePos();
                    m_VarDropPending = true;
                }
            }

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EVT_NODE"))
            {
                std::string data = (const char*)payload->Data;
                m_PendingNodeType = "SendEvent";
                m_PendingNodePos = ImGui::GetMousePos();
                m_PendingNodeStrValue = data.substr(data.find(':') + 1);
            }

            // FUNC_NODE — arrastar uma Function do Script Members sempre cria
            // um node "Call <Function>" direto (sem popup de escolha, diferente
            // de variável — só existe um tipo de node possível pra uma Function).
            // Funciona igual independente de qual grafo está aberto no momento
            // (principal ou de outra função) — inclusive permite uma função
            // chamar a si mesma (recursão) ou chamar outra função.
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FUNC_NODE"))
            {
                std::string funcName = (const char*)payload->Data;
                if (m_Graph && m_EdCtx && m_ScriptAsset)
                {
                    ScriptFunction* func = m_ScriptAsset->FindFunction(funcName);
                    if (func)
                    {
                        ed::SetCurrentEditor(m_EdCtx);
                        ImVec2 dropPos = ImGui::GetMousePos();
                        ImVec2 canvasPos = ed::ScreenToCanvas(dropPos);
                        auto* node = m_Graph->AddCallFunctionNode(*func);
                        if (node)
                        {
                            ed::SetNodePosition(node->ID, canvasPos);
                            m_ConsoleLines.push_back("[Info] Node created: Call " + funcName);
                        }
                        ed::SetCurrentEditor(nullptr);
                    }
                }
            }

            ImGui::EndDragDropTarget();
        }
    }

} // namespace axe