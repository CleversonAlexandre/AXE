// script_node_graph.cpp
// DrawGraphWindow e DrawNodeGraph — canvas do node editor, criação/deleção de links,
// context menu (categorias + componentes), pending nodes, drop targets, Split/Recombine Pin,
// Promote to Variable, Get/Set Variable popup.

#include "script_graph_window.hpp"
#include "editor/axe_editor/script/script_graph.hpp"
#include "editor/axe_editor/script/script_asset.hpp"
#include "axe/input/input_mapping.hpp" // popup do combo de Get Action/Axis
#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>   // SC2: std::abs(float) na curva do fio carregado

namespace ed = ax::NodeEditor;

// Helper global — detecta pin splitado ("X","Y","Z" ou "Nome.X","Nome.Y","Nome.Z")
// Fora do namespace axe pra não ter conflito com AXE_API
static bool IsSplitPin(const axe::ScriptPin& p)
{
    // SC20 — reconhece W tambem. Sem isso, o quarto componente de um Vec4 ou
    // Quat splitado nao era visto como parte do split: o Recombine deixava o
    // pin W orfao no node e o flag de split continuava ligado para sempre.
    const std::string& n = p.Name;
    if (n == "X" || n == "Y" || n == "Z" || n == "W") return true;
    if (n.size() >= 2)
    {
        const std::string suf = n.substr(n.size() - 2);
        return suf == ".X" || suf == ".Y" || suf == ".Z" || suf == ".W";
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
        {"Get Forward Vector","GetForwardVector"},
        {"Get Right Vector","GetRightVector"} };
    static const NE sAud[] = {
        {"Audio Play","AudioPlay"},{"Audio Stop","AudioStop"},
        {"Audio Set Volume","AudioSetVolume"},{"Audio Set Pitch","AudioSetPitch"},
        {"Audio Is Playing","AudioIsPlaying"},
        {"Play Sound 2D","PlaySound2D"},
        {"Play Sound At Location","PlaySoundAtLocation"} };
    static const NE sCam[] = {
        {"Get Camera Direction","GetCameraDirection"},
        {"Camera Shake","CameraShake"},{"Camera Follow","CameraFollow"},
        {"Camera Stop Follow","CameraStopFollow"},{"Set Camera FOV","SetCameraFOV"} };
    // Cutscene — comeca/para uma sequence do Sequencer pelo NOME da entidade
    // que carrega o Sequence Player.
    static const NE sSeq[] = {
        {"Play Sequence","PlaySequence"},
        {"Stop Sequence","StopSequence"},
        {"Is Sequence Playing","IsSequencePlaying"} };
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
        {"Float to Vec3","FloatToVec3"},
        // S3 — referencia entre scripts. Ficam em Cast porque e o que sao:
        // "esta entidade roda o script X?" e a mesma pergunta de um cast de
        // tipo, so que atravessando a fronteira de DLL.
        {"Cast To Script","CastToScript"},
        {"Get Script Var","GetScriptVar"},
        {"Set Script Var","SetScriptVar"},
        {nullptr,nullptr}
    };

    // Conta as entradas do proprio array, ignorando a sentinela {nullptr,
    // nullptr} quando existe (so o sCast tem uma).
    //
    // Antes cada categoria trazia o numero escrito a mao, e errar esse numero
    // faz o no EXISTIR e nunca aparecer no menu — bug silencioso que ja
    // aconteceu com o "Particle Burst" (11 escrito, 12 entradas). Derivando do
    // array, adicionar um no passa a ser uma linha so.
    template <size_t N>
    static int NECount(const NE(&a)[N])
    {
        return (N > 0 && a[N - 1].label == nullptr) ? (int)N - 1 : (int)N;
    }

    struct CatDef { const char* name; const NE* e; int n; ImVec4 col; };
    static const CatDef s_Cats[] = {
        {"Events",        sEv,   NECount(sEv),   {0.85f,0.3f,0.2f,  1}},
        {"Actions",       sAc,   NECount(sAc),   {0.2f, 0.7f,0.45f, 1}},
        {"Transform",     sTr,   NECount(sTr),   {0.9f, 0.65f,0.2f, 1}},
        {"Camera",        sCam,  NECount(sCam),  {0.3f, 0.7f,0.95f, 1}},
        {"Cutscene",      sSeq,  NECount(sSeq),  {0.95f,0.45f,0.55f,1}},
        {"Audio",         sAud,  NECount(sAud),  {0.85f,0.5f,0.85f, 1}},
        {"Logic",         sLo,   NECount(sLo),   {0.8f, 0.6f,0.1f,  1}},
        {"Math",          sMa,   NECount(sMa),   {0.3f, 0.5f,0.9f,  1}},
        {"Input",         sIn,   NECount(sIn),   {0.7f, 0.2f,0.6f,  1}},
        {"Array",         sArr,  NECount(sArr),  {0.55f,0.45f,0.85f,1}},
        {"Flow Control",  sFlow, NECount(sFlow), {0.45f,0.6f,0.75f, 1}},
        {"Cast",          sCast, NECount(sCast), {0.5f, 0.8f,0.8f,  1}},
    };
    // Uma cor por categoria de s_Cats, na MESMA ordem. Inserir categoria
    // sem inserir cor aqui le fora do array.
    //
    // SC18 — eram NOVE cores para ONZE categorias. Junto com o "ci < 8"
    // escrito a mao no laco do menu, as tres ultimas (Array, Flow Control,
    // Cast) simplesmente nao apareciam: For Loop, For Each, While, Switch on
    // Int e Switch on String existiam na fabrica de nodes e nao tinham como
    // ser criados pela interface.
    static const ImVec4 s_CtxCols[] = {
        {1.f,0.45f,0.35f,1},{0.3f,0.85f,0.55f,1},
        {1.f,0.78f,0.2f,1},{0.4f,0.65f,1.f,1},
        {0.98f,0.55f,0.62f,1},  // Cutscene — inserida DEPOIS de Camera, e a
        // cor tem de entrar na MESMA posicao aqui
{0.85f,0.5f,0.85f,1},{0.85f,0.3f,0.75f,1},
{0.7f,0.6f,0.95f,1},{0.55f,0.75f,0.95f,1},
{0.6f,0.5f,0.9f,1},    // Array
{0.5f,0.68f,0.85f,1},  // Flow Control
{0.5f,0.9f,0.9f,1}     // Cast
    };
    static_assert(sizeof(s_CtxCols) / sizeof(s_CtxCols[0]) ==
        sizeof(s_Cats) / sizeof(s_Cats[0]),
        "s_CtxCols precisa de uma cor por categoria de s_Cats");

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
    //  SC2 — suporte ao fio carregado (Ctrl + clique)
    // ─────────────────────────────────────────────────────────────────────────

    void ScriptGraphWindow::TrackPinRect(ed::PinId id)
    {
        // Chamada logo depois do ícone do pin, ainda entre BeginPin/EndPin: o
        // "item" corrente é o próprio ícone, então o centro do rect é
        // exatamente de onde o fio sai na tela.
        ImVec2 a = ImGui::GetItemRectMin();
        ImVec2 b = ImGui::GetItemRectMax();
        m_PinCanvasPos.emplace_back((int)id.Get(),
            ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f));
    }

    bool ScriptGraphWindow::GetPinCanvasPos(ed::PinId id, ImVec2& out) const
    {
        for (const auto& e : m_PinCanvasPos)
            if (e.first == (int)id.Get()) { out = e.second; return true; }
        return false;
    }

    void ScriptGraphWindow::CancelCarriedWire()
    {
        if (!m_CarryingWire) return;

        // Devolve cada fio ao pin de origem. AddLink volta a impor
        // cardinalidade, então se o usuário conseguiu ocupar aquele pin no meio
        // do gesto, o estado final continua consistente.
        ScriptPin* from = m_Graph ? m_Graph->FindPin(m_CarriedFromPin) : nullptr;
        if (from && m_Graph)
        {
            for (auto& remote : m_CarriedRemotePins)
            {
                ScriptPin* rp = m_Graph->FindPin(remote);
                if (!rp) continue;
                ScriptPin* o = (from->Kind == ed::PinKind::Output) ? from : rp;
                ScriptPin* i = (from->Kind == ed::PinKind::Input) ? from : rp;
                m_Graph->AddLink(o->ID, i->ID);
            }
        }

        // Sem CommitUndo: o grafo voltou ao que era, então registrar um passo
        // de undo aqui encheria o histórico de entradas que não mudam nada.
        m_SnapshotBeforeAction.clear();
        m_CarryingWire = false;
        m_CarriedRemotePins.clear();
        m_CarriedFromPin = {};
    }

    void ScriptGraphWindow::DrawCarriedWire()
    {
        if (!m_CarryingWire || !m_Graph) return;

        // Punho FECHADO enquanto o fio está na mão — o par aberto/fechado é o
        // que faz o gesto se explicar sozinho.
        m_HandCursor = HandCursor::Closed;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetMousePos();

        // Verde quando o pin sob o cursor aceita, vermelho quando recusa,
        // âmbar quando não há pin nenhum embaixo — a cor responde antes de o
        // usuário soltar o botão, igual ao drag normal.
        ed::PinId hovered = ed::GetHoveredPin();
        // Sem pin sob o cursor o gesto CORTA a ligação ao soltar — então o
        // preview já avisa disso, em vermelho. Antes esta era a cor "neutra"
        // de "ainda procurando", e nada dizia que largar ali destruía o fio.
        ImU32 col = IM_COL32(200, 70, 70, 230);
        const char* hint = "Release here to disconnect";

        if (hovered != ed::PinId{})
        {
            bool anyOk = false;
            for (auto& remote : m_CarriedRemotePins)
            {
                ScriptLinkQuery q = m_Graph->QueryLink(remote, hovered);
                if (q.Action == ScriptLinkAction::Accept ||
                    q.Action == ScriptLinkAction::AcceptImplicit ||
                    q.Action == ScriptLinkAction::AdoptWildcard)
                {
                    anyOk = true; break;
                }
                hint = q.Message;
            }
            col = anyOk ? IM_COL32(80, 220, 120, 235) : IM_COL32(220, 60, 60, 235);
            if (anyOk) hint = nullptr;
        }

        for (auto& remote : m_CarriedRemotePins)
        {
            ImVec2 from;
            if (!GetPinCanvasPos(remote, from)) continue;   // pin fora de vista

            // Mesma curva do fio normal do node editor: tangentes horizontais
            // proporcionais à distância, com um piso para o traço não colapsar
            // numa reta quando as pontas estão perto.
            const float dx = std::abs(mouse.x - from.x);
            const float strength = (dx < 60.0f) ? 60.0f : dx * 0.5f;
            const bool  fromIsOutput = [&] {
                ScriptPin* p = m_Graph->FindPin(remote);
                return p && p->Kind == ed::PinKind::Output;
                }();
            const float s1 = fromIsOutput ? strength : -strength;

            dl->AddBezierCubic(
                from,
                ImVec2(from.x + s1, from.y),
                ImVec2(mouse.x - s1, mouse.y),
                mouse,
                col, 2.5f);
        }

        // Bolinha no cursor: marca onde a ponta solta está, que é o que o
        // usuário está mirando.
        dl->AddCircleFilled(mouse, 4.5f, col);

        if (hint && hint[0])
        {
            ed::Suspend();
            ImGui::SetTooltip("%s", hint);
            ed::Resume();
        }
    }

    void ScriptGraphWindow::DrawHandCursor()
    {
        if (m_HandCursor == HandCursor::None) return;

        ImGuiIO& io = ImGui::GetIO();

        // Se o backend não sabe esconder o cursor do sistema, desenhar o nosso
        // por cima daria DOIS cursores na tela. Nesse caso cai no cursor de mão
        // padrão do ImGui — pior, mas correto.
        if (!(io.BackendFlags & ImGuiBackendFlags_HasMouseCursors))
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            return;
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);

        const bool  closed = (m_HandCursor == HandCursor::Closed);
        const ImVec2 m = io.MousePos;          // espaço de tela — fora do canvas
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        const ImU32 fill = IM_COL32(245, 245, 245, 255);
        const ImU32 line = IM_COL32(20, 20, 20, 230);

        // O "hotspot" é o centro da palma: é dali que o fio sai, então é ali
        // que o ponteiro precisa estar.
        const float s = 1.0f;                  // escala; 1.0 ≈ 22px de altura
        auto P = [&](float x, float y) { return ImVec2(m.x + x * s, m.y + y * s); };

        // Cada peça é desenhada duas vezes: contorno escuro por baixo, um pouco
        // maior, para a mão continuar legível sobre node claro ou fio claro.
        auto Finger = [&](float x, float top, float bottom, float w)
            {
                dl->AddLine(P(x, top), P(x, bottom), line, w * s + 2.0f * s);
                dl->AddLine(P(x, top), P(x, bottom), fill, w * s);
            };

        if (!closed)
        {
            // ── Palma aberta ─────────────────────────────────────────────────
            // Quatro dedos estendidos, levemente em leque, e o polegar aberto
            // para a esquerda.
            dl->AddRectFilled(P(-6, -3), P(6, 9), line, 5.0f * s);
            dl->AddRectFilled(P(-5, -2), P(5, 8), fill, 4.0f * s);

            Finger(-3.5f, -11.0f, -2.0f, 2.6f);
            Finger(-1.0f, -13.0f, -2.0f, 2.6f);
            Finger(1.5f, -12.5f, -2.0f, 2.6f);
            Finger(4.0f, -10.0f, -2.0f, 2.6f);
            // Polegar: sai da lateral, apontando para fora.
            dl->AddLine(P(-5, 2), P(-11, -4), line, 4.6f * s);
            dl->AddLine(P(-5, 2), P(-11, -4), fill, 2.6f * s);
        }
        else
        {
            // ── Punho fechado ────────────────────────────────────────────────
            // Mesma silhueta, dedos recolhidos: só as juntas aparecem no topo,
            // e o polegar cruza a frente. A mudança de altura entre os dois
            // estados é o que dá a sensação de "agarrou".
            dl->AddRectFilled(P(-6, -5), P(6, 9), line, 5.0f * s);
            dl->AddRectFilled(P(-5, -4), P(5, 8), fill, 4.0f * s);

            // Juntas — bolinhas encostadas na borda de cima.
            for (int k = 0; k < 4; k++)
            {
                float x = -3.5f + k * 2.4f;
                dl->AddCircleFilled(P(x, -5.0f), 1.9f * s, line);
                dl->AddCircleFilled(P(x, -5.0f), 1.2f * s, fill);
            }
            // Polegar dobrado por cima da frente do punho.
            dl->AddLine(P(-5.5f, 1.5f), P(1.5f, 1.5f), line, 4.6f * s);
            dl->AddLine(P(-5.5f, 1.5f), P(1.5f, 1.5f), fill, 2.6f * s);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ScriptGraphWindow::DrawNodeGraph()
    {
        if (!m_Graph) return;

        ed::SetCurrentEditor(m_EdCtx);
        ed::Begin("##SG", ImVec2(0, 0));
        m_InsideNodeEditorFrame = true;

        // SC2 — as posições de pin valem só para ESTE frame: o usuário move
        // node, dá zoom, colapsa categoria. Guardar entre frames daria um fio
        // preso a onde o pin estava, não a onde está.
        m_PinCanvasPos.clear();
        m_SuppressCtxMenuThisFrame = false;
        m_HandCursor = HandCursor::None;

        // Restaura posições salvas no JSON na primeira frame
        if (m_FirstFrame && m_Graph)
            for (auto& node : m_Graph->GetNodes())
                if (node->Position.x != 0.f || node->Position.y != 0.f)
                    ed::SetNodePosition(node->ID, node->Position);

        // SC9 — depois de um Undo/Redo o canvas ainda mostra as posições de
        // antes. Empurrar o modelo de volta aqui, dentro do Begin/End, é o
        // único ponto em que ed::SetNodePosition tem contexto garantido.
        //
        // Sem isto o undo de um arrasto se anulava sozinho: o modelo voltava,
        // o canvas não, e o detector de arrasto logo abaixo interpretava a
        // diferença como um movimento novo do usuário — gravando a posição do
        // canvas por cima da restaurada, no mesmo frame. Era exatamente o
        // sintoma de "arrastar e dar Ctrl+Z não faz nada" no grafo principal;
        // dentro de uma Function parecia funcionar só porque a troca de grafo
        // levantava m_FirstFrame e reaplicava tudo por outro caminho.
        if (m_PendingPositionSync && m_Graph)
        {
            for (auto& node : m_Graph->GetNodes())
                ed::SetNodePosition(node->ID, node->Position);
            m_PendingPositionSync = false;

        }

        // SC10 — seleção adiada (nodes recém-colados). Depois do
        // posicionamento, para a moldura de seleção já sair no lugar certo.
        if (!m_PendingSelectNodes.empty())
        {
            ed::ClearSelection();
            for (const auto& id : m_PendingSelectNodes)
                ed::SelectNode(id, true);
            m_PendingSelectNodes.clear();
        }

        for (auto& n : m_Graph->GetNodes()) DrawNode(n.get());

        // ── SC11: arrastar node entra no histórico ───────────────────────────
        //
        // Dois eventos exatos do mouse, e nenhuma estimativa. No CLIQUE guarda
        // onde cada node está; na SOLTURA compara. Entre os dois não interessa
        // o que acontece.
        //
        // A versão anterior tentava perceber o movimento a cada frame e fechar
        // o passo "quando o gesto parecesse ter acabado". Nunca consegui provar
        // onde falhava — e é justamente por isso que ela saiu: um undo que
        // funciona quase sempre custa a confiança inteira do autor no Ctrl+Z.
        //
        // A tolerância de 0.5px não é folga arbitrária. ed::SetNodePosition faz
        // FloorRect nas coordenadas, então uma posição fracionária vinda do
        // JSON (123.456) volta do canvas como 123.0. Sem tolerância, TODO node
        // apareceria movido já no segundo frame, sem ninguém ter tocado nele.
        if (!m_FirstFrame && m_Graph)
        {
            // ── 1) Soltou o botão: houve arrasto? ─────────────────────────────
            //
            // Vem ANTES da captura, senão o refresh de repouso abaixo apagaria
            // a referência no mesmo frame em que ela é usada.
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !m_DragStartPositions.empty())
            {
                bool moved = false;
                for (const auto& pr : m_DragStartPositions)
                {
                    const ImVec2 now = ed::GetNodePosition(ed::NodeId(pr.first));
                    if (!std::isfinite(now.x) || !std::isfinite(now.y)) continue;
                    if (std::fabs(now.x - pr.second.x) > 0.5f ||
                        std::fabs(now.y - pr.second.y) > 0.5f)
                    {
                        moved = true; break;
                    }
                }
                if (moved) CommitNodeDrag();
            }

            // ── 2) Enquanto o botão está SOLTO, a referência é o agora ────────
            //
            // Aqui estava o erro do SC11. Eu capturava no IsMouseClicked e só
            // se ImGui::IsWindowHovered() fosse verdadeiro — mas neste ponto do
            // código a "janela corrente" é a criada por ed::Begin, e o hover
            // dela não responde o que eu supunha. A condição nunca era
            // satisfeita, m_DragStartPositions ficava vazia, e o ramo de
            // soltura acima nunca rodava. O mecanismo estava certo; o gatilho
            // nunca disparava.
            //
            // Depender de um ÚNICO evento (o clique) é frágil por natureza:
            // basta ele ser consumido em algum caminho para o gesto inteiro
            // deixar de ser registrado, em silêncio. Manter a referência
            // atualizada em todo frame de repouso não depende de evento nenhum
            // — quando o botão desce, a última referência gravada é, por
            // construção, a posição imediatamente anterior ao gesto.
            //
            // Custo: um vetor de N ImVec2 por frame parado. Um grafo grande
            // tem dezenas de nodes; é ruído perto de desenhá-los.
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                m_DragStartPositions.clear();
                m_DragStartPositions.reserve(m_Graph->GetNodes().size());
                for (const auto& n : m_Graph->GetNodes())
                    m_DragStartPositions.emplace_back((int)n->ID.Get(), ed::GetNodePosition(n->ID));
            }
        }

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
        // SC8 — exige a tecla LIMPA. Sem esta checagem, Ctrl+C no canvas criava
        // um Comment em vez de copiar: o atalho olhava so a letra e ignorava o
        // modificador. Alt e Shift entram na mesma regra por antecipacao —
        // qualquer combinacao futura com C deixaria de colidir sozinha.
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsWindowHovered() && !io.WantTextInput &&
            !io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
            ImGui::IsKeyPressed(ImGuiKey_C, false))
        {
            PushUndo("Add Comment");
            CreateCommentNode(ImGui::GetMousePos());
            CommitUndo("Add Comment");
        }

        // ── SC2: Ctrl + clique para desplugar / mover fio ─────────────────────
        //
        // Ctrl + botão ESQUERDO num pin conectado arranca os fios dele e passa
        // a arrastá-los; soltar sobre um pin compatível replugue tudo lá.
        // Ctrl + botão DIREITO num pin conectado despluga na hora.
        // Ambos são o gesto da Unreal, e ambos são estado NOSSO: o
        // imgui-node-editor só sabe iniciar um link a partir de um drag que ele
        // próprio detectou, e não há API para injetar um drag sintético.
        //
        // Um pin de saída pode ter vários fios. Pegar "só um deles" exigiria
        // perguntar qual — então vêm todos juntos, como na Unreal.
        {
            ed::PinId hoveredPin = ed::GetHoveredPin();
            const bool ctrl = ImGui::GetIO().KeyCtrl;

            if (!m_CarryingWire && ctrl && hoveredPin != ed::PinId{} &&
                m_Graph->IsPinLinked(hoveredPin))
            {
                // Palma ABERTA ao passar com Ctrl sobre um pin conectado: o
                // cursor avisa que o gesto existe ANTES do clique. Sem isso o
                // atalho só é descoberto por quem já sabe que ele está lá.
                m_HandCursor = HandCursor::Open;

                // Ctrl + direito: desplugar e acabou.
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    PushUndo("Break Links");
                    std::vector<ed::LinkId> doomed;
                    for (const auto& l : m_Graph->GetLinks())
                        if (l.StartPin == hoveredPin || l.EndPin == hoveredPin)
                            doomed.push_back(l.ID);
                    for (auto& id : doomed) m_Graph->RemoveLink(id);
                    CommitUndo("Break Links");
                    m_ConsoleLines.push_back("[Info] " + std::to_string(doomed.size()) +
                        " wire(s) disconnected.");
                    m_SuppressCtxMenuThisFrame = true;
                }
                // Ctrl + esquerdo: arranca e carrega.
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    // PushUndo aqui e CommitUndo só no drop: o snapshot fica
                    // guardado em m_SnapshotBeforeAction enquanto o fio está
                    // no ar, então um Ctrl+Z depois desfaz o movimento INTEIRO
                    // (arrancar + replugar), não as duas metades separadas.
                    PushUndo("Move Link");

                    m_CarriedRemotePins.clear();
                    m_CarriedFromPin = hoveredPin;

                    std::vector<ed::LinkId> doomed;
                    for (const auto& l : m_Graph->GetLinks())
                    {
                        if (l.StartPin == hoveredPin) { m_CarriedRemotePins.push_back(l.EndPin);   doomed.push_back(l.ID); }
                        else if (l.EndPin == hoveredPin) { m_CarriedRemotePins.push_back(l.StartPin); doomed.push_back(l.ID); }
                    }
                    for (auto& id : doomed) m_Graph->RemoveLink(id);

                    m_CarryingWire = !m_CarriedRemotePins.empty();
                    if (!m_CarryingWire) m_SnapshotBeforeAction.clear();
                }
            }

            // ── Enquanto carrega: solta no pin sob o cursor ────────────────────
            if (m_CarryingWire)
            {
                // Esc ou botão direito abortam e devolvem os fios ao lugar.
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                    ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    CancelCarriedWire();
                    m_SuppressCtxMenuThisFrame = true;
                }
                else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    ed::PinId target = ed::GetHoveredPin();
                    int connected = 0;

                    if (target != ed::PinId{})
                    {
                        // Cada fio é validado por QueryLink — o mesmo veredito
                        // do drag normal. Um destino que sirva para dois dos
                        // três fios conecta os dois e recusa o terceiro, em vez
                        // de recusar tudo: replugar parcialmente é mais útil do
                        // que obrigar a refazer o gesto.
                        for (auto& remote : m_CarriedRemotePins)
                        {
                            ScriptLinkQuery q = m_Graph->QueryLink(remote, target);
                            if (!q.Allowed()) continue;

                            ScriptPin* rp = m_Graph->FindPin(remote);
                            ScriptPin* tp = m_Graph->FindPin(target);
                            if (!rp || !tp) continue;

                            ScriptPin* o = (rp->Kind == ed::PinKind::Output) ? rp : tp;
                            ScriptPin* i = (rp->Kind == ed::PinKind::Input) ? rp : tp;

                            // Só as ações diretas: inserir node de conversão no
                            // meio de um gesto de MOVER seria uma segunda coisa
                            // acontecendo sem o usuário ter pedido.
                            if (q.Action == ScriptLinkAction::Accept ||
                                q.Action == ScriptLinkAction::AcceptImplicit)
                            {
                                m_Graph->AddLink(o->ID, i->ID);
                                connected++;
                            }
                            else if (q.Action == ScriptLinkAction::AdoptWildcard)
                            {
                                ScriptPin* wp = (o->Type == ScriptPinType::Wildcard) ? o : i;
                                ScriptNode* wn = m_Graph->FindNodeOfPin(wp->ID);
                                m_Graph->AddLink(o->ID, i->ID);
                                if (wn)
                                {
                                    for (auto& p : wn->Inputs)  p.Type = q.AdoptType;
                                    for (auto& p : wn->Outputs) p.Type = q.AdoptType;
                                }
                                connected++;
                            }
                        }
                    }

                    if (connected > 0)
                    {
                        CommitUndo("Move Link");
                        m_ConsoleLines.push_back("[Info] " + std::to_string(connected) +
                            " wire(s) reconnected.");
                        m_CarryingWire = false;
                        m_CarriedRemotePins.clear();
                        m_CarriedFromPin = {};
                    }
                    else if (target == ed::PinId{})
                    {
                        // ── Soltou no VAZIO: a ligação morre aqui ────────────
                        //
                        // Antes isto devolvia o fio ao pin de origem, e o
                        // gesto de "arrancar e jogar fora" era impossível —
                        // A→B, puxar de B e soltar no nada devolvia para B.
                        //
                        // A distinção que vale é entre soltar no VAZIO e soltar
                        // num pin que RECUSOU. Largar no vazio é uma decisão:
                        // "não quero mais isso ligado". Largar num pin que não
                        // aceitou é uma tentativa que falhou, e apagar a
                        // ligação antiga como efeito colateral de uma
                        // tentativa frustrada seria punir a mira.
                        //
                        // O snapshot já foi tirado no PushUndo do momento em
                        // que o fio foi arrancado, então um Ctrl+Z devolve tudo.
                        CommitUndo("Break Link");
                        m_ConsoleLines.push_back("[Info] " + std::to_string(m_CarriedRemotePins.size()) +
                            " wire(s) disconnected.");
                        m_CarryingWire = false;
                        m_CarriedRemotePins.clear();
                        m_CarriedFromPin = {};
                    }
                    else
                    {
                        // Soltou em pin incompatível: devolve ao lugar.
                        CancelCarriedWire();
                    }
                }
            }
        }

        // ── Criação de links ──────────────────────────────────────────────────
        //
        // S1 — este bloco tinha ~240 linhas de if/else encadeados com três
        // cópias das lambdas isNumeric/isVec e quatro varreduras ad-hoc de
        // GetNodes() só para descobrir se um pin Wildcard pertencia a um
        // Reroute. Toda essa decisão migrou para ScriptGraph::QueryLink(): o
        // canvas agora PERGUNTA e apenas traduz o veredito em cor e tooltip.
        //
        // Por que no modelo e não aqui: a mesma pergunta precisa ser respondida
        // em pelo menos três lugares diferentes — o drag do usuário (aqui), o
        // arraste de pin para o vazio que promove um node novo (mais abaixo) e
        // o load de um .axescript antigo (SanitizeLinks). Três respostas
        // escritas separadamente divergem; foi exatamente o que aconteceu com
        // ArePinsCompatible(), que existia no header e nunca era chamada.
        // SC2 — o fio branco "fantasma" que aparecia junto do nosso preview era
        // ESTE: os dois primeiros argumentos são a cor e a espessura do link que
        // o node editor desenha enquanto o usuário arrasta. Como o Ctrl+arraste
        // também é um arraste aos olhos dele, ele desenhava o dele por baixo do
        // nosso. Alpha 0 e espessura 0 enquanto carregamos: o node editor
        // continua rodando normalmente (precisamos do QueryNewLink para
        // recusá-lo), só não pinta nada.
        const ImVec4 createCol = m_CarryingWire
            ? ImVec4(0, 0, 0, 0)
            : ImVec4(1, 1, 1, 1);
        if (ed::BeginCreate(createCol, m_CarryingWire ? 0.0f : 2.0f))
        {
            ed::PinId sId, eId;
            if (ed::QueryNewLink(&sId, &eId))
            {
                // SC2 — enquanto um fio está sendo carregado por Ctrl+arraste,
                // o node editor também acha que está criando um link a partir
                // do pin clicado. Deixar os dois caminhos rodarem produziria um
                // fio a mais, ancorado no pin ERRADO (no que foi agarrado, não
                // na outra ponta). O nosso gesto vence; o dele é recusado.
                if (m_CarryingWire)
                {
                    // Transparente pelo mesmo motivo do BeginCreate acima:
                    // RejectNewItem também PINTA o link recusado, e essa era a
                    // segunda fonte do fio fantasma.
                    ed::RejectNewItem(ImColor(0, 0, 0, 0), 0.0f);
                }
                else
                {
                    ScriptLinkQuery q = m_Graph->QueryLink(sId, eId);

                    // Normaliza saída/entrada do mesmo jeito que o modelo fez, para
                    // usar os IDs na ordem certa ao criar o link.
                    ScriptPin* pA = m_Graph->FindPin(sId);
                    ScriptPin* pB = m_Graph->FindPin(eId);
                    ScriptPin* o = (pA && pA->Kind == ed::PinKind::Output) ? pA : pB;
                    ScriptPin* i = (pA && pA->Kind == ed::PinKind::Input) ? pA : pB;

                    if (!q.Allowed() || !o || !i)
                    {
                        ed::RejectNewItem(ImColor(220, 40, 40), 2.0f);
                        ed::Suspend();
                        ImGui::SetTooltip("%s", q.Message);
                        ed::Resume();
                    }
                    else
                    {
                        // Cor do preview: verde do tipo quando é ligação limpa,
                        // laranja quando há cast/substituição envolvida. A cor
                        // avisa ANTES de soltar o botão que algo além de "ligar"
                        // vai acontecer.
                        const bool warn =
                            q.Action == ScriptLinkAction::AcceptImplicit ||
                            q.Action == ScriptLinkAction::InsertConversion ||
                            q.ReplacesExisting;
                        const ImColor previewCol = warn
                            ? ImColor(220, 140, 40)
                            : GetPinColor(q.LinkColorType);

                        if (ed::AcceptNewItem(previewCol, 2.5f))
                        {
                            switch (q.Action)
                            {
                            case ScriptLinkAction::Accept:
                            case ScriptLinkAction::AcceptImplicit:
                            {
                                PushUndo("Add Link");
                                m_Graph->AddLink(o->ID, i->ID);
                                CommitUndo("Add Link");
                                break;
                            }

                            case ScriptLinkAction::AdoptWildcard:
                            {
                                // Reroute fixa os DOIS pins de uma vez, para a
                                // ponta ainda solta já nascer pronta para receber
                                // qualquer fio do mesmo tipo.
                                PushUndo("Add Link (reroute)");
                                ScriptPin* wp = (o->Type == ScriptPinType::Wildcard) ? o : i;
                                ScriptNode* wn = m_Graph->FindNodeOfPin(wp->ID);
                                m_Graph->AddLink(o->ID, i->ID);
                                if (wn)
                                {
                                    for (auto& p : wn->Inputs)  p.Type = q.AdoptType;
                                    for (auto& p : wn->Outputs) p.Type = q.AdoptType;
                                }
                                CommitUndo("Add Link (reroute)");
                                break;
                            }

                            case ScriptLinkAction::AdoptArrayWildcard:
                            {
                                PushUndo("Add Link (array)");
                                ScriptPin* ap = (o->Type == ScriptPinType::Wildcard) ? o : i;
                                ScriptNode* an = m_Graph->FindNodeOfPin(ap->ID);
                                m_Graph->AddLink(o->ID, i->ID);
                                if (an)
                                {
                                    // AdoptType é o tipo do PIN (*Array). O
                                    // RebuildArrayNodePins quer o ScriptVarType do
                                    // ELEMENTO — a conversão fica de fora do
                                    // modelo porque é regra de reconstrução de
                                    // pins, não de compatibilidade.
                                    ScriptVarType concrete = (ScriptVarType)(
                                        (int)q.AdoptType - (int)ScriptPinType::FloatArray
                                        + (int)ScriptVarType::FloatArray);
                                    m_Graph->RebuildArrayNodePins(an, GetElementType(concrete));
                                }
                                CommitUndo("Add Link (array)");
                                break;
                            }

                            case ScriptLinkAction::InsertConversion:
                            {
                                PushUndo(std::string("Add Conversion: ") + q.ConversionNode);

                                // Posiciona o node de conversão no meio do caminho.
                                // FindNodeOfPin substitui a busca por endereço de
                                // pin que existia aqui — busca por endereço deixa
                                // de valer assim que qualquer vector de pins
                                // realoca.
                                ImVec2 posO{ 0,0 }, posI{ 0,0 };
                                if (auto* nO = m_Graph->FindNodeOfPin(o->ID)) posO = ed::GetNodePosition(nO->ID);
                                if (auto* nI = m_Graph->FindNodeOfPin(i->ID)) posI = ed::GetNodePosition(nI->ID);
                                ImVec2 midPos((posO.x + posI.x) * 0.5f, (posO.y + posI.y) * 0.5f);

                                if (auto* conv = m_Graph->AddNode(q.ConversionNode))
                                {
                                    ed::SetNodePosition(conv->ID, midPos);
                                    for (auto& p : conv->Inputs)
                                        if (p.Name == "Value") { m_Graph->AddLink(o->ID, p.ID); break; }
                                    for (auto& p : conv->Outputs)
                                        if (p.Name == "Value") { m_Graph->AddLink(p.ID, i->ID); break; }
                                    m_ConsoleLines.push_back(
                                        std::string("[Info] Automatic conversion: ") + q.ConversionNode);
                                }
                                CommitUndo(std::string("Add Conversion: ") + q.ConversionNode);
                                break;
                            }

                            default:
                                break;
                            }
                        }

                        // Só interrompe o desenho para um balão se houver algo a
                        // dizer — ligação trivial devolve Message vazia.
                        if (q.Message && q.Message[0])
                        {
                            ed::Suspend();
                            ImGui::SetTooltip("%s", q.Message);
                            ed::Resume();
                        }
                    }
                } // fim do else de m_CarryingWire
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
                    m_ConsoleLines.push_back("[Aviso] '" + node->Name + "' nao pode ser deletado - e gerido automaticamente pela Function.");
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
                    m_ConsoleLines.push_back("[Aviso] '" + node->Name + "' nao pode ser deletado - e gerido automaticamente pela Function.");
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
                        case ScriptVarType::Float: node->FloatValue = v.Default.Float; break;
                        case ScriptVarType::Bool:  node->BoolValue = v.Default.Bool; break;
                        case ScriptVarType::Int:   node->IntLocalValue = v.Default.Int; break;
                        case ScriptVarType::Vec3:
                            node->Vec3Value[0] = v.Default.Vec.x;
                            node->Vec3Value[1] = v.Default.Vec.y;
                            node->Vec3Value[2] = v.Default.Vec.z;
                            break;
                        case ScriptVarType::String: node->StringLocalValue = v.Default.Str; break;
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
        // SC2 — Ctrl + botão direito num pin JÁ significa "desplugar". Deixar o
        // menu de contexto abrir junto daria os dois ao mesmo tempo: o fio some
        // e um menu aparece por cima, como se o menu tivesse feito aquilo.
        if (openPinCtx && (ImGui::GetIO().KeyCtrl || m_SuppressCtxMenuThisFrame))
            openPinCtx = false;
        if (openPinCtx) m_CtxPinId = ctxPinId;

        ed::NodeId ctxMenuNodeId;
        bool openNodeCtx = ed::ShowNodeContextMenu(&ctxMenuNodeId);
        if (openNodeCtx) m_CtxNodeId = ctxMenuNodeId;

        ed::Suspend();
        if (openPinCtx)  ImGui::OpenPopup("##PinCtx");
        if (openNodeCtx) ImGui::OpenPopup("##NodeCtx");

        // ── Popup dos combos que moram DENTRO dos nodes ───────────────────────
        //
        // Aberto aqui, e nao no node: dentro de BeginNode/EndNode o popup do
        // ImGui simplesmente nao expande, porque o canvas do node-editor esta
        // com a propria transformacao ativa. Por isso o widget la e um botao
        // que so registra o pedido — era o motivo de a Action so poder ser
        // trocada pelo Script Details.
        if (m_ComboRequested)
        {
            ImGui::OpenPopup("##NodeCombo");
            m_ComboRequested = false;
        }

        // Aparencia de combo, nao de menu solto: nasce colado no botao, com a
        // largura dele, fundo e realce iguais aos do dropdown do Script
        // Details. Sem isto o popup saia pequeno e no meio do canvas.
        ImGui::SetNextWindowPos(m_ComboAnchor);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::max(m_ComboWidth, 140.f), 0.f),
            ImVec2(std::max(m_ComboWidth, 140.f), 320.f));

        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));

        if (ImGui::BeginPopup("##NodeCombo"))
        {
            ScriptNode* cn = nullptr;
            for (auto& n : m_Graph->GetNodes())
                if (n->ID == m_ComboNodeId) { cn = n.get(); break; }

            // Item ocupando a largura toda, como no combo do painel.
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));

            if (!cn)
            {
                ImGui::TextDisabled(" node removido");
            }
            else if (m_ComboKind == 2)
            {
                int wantType = 0;
                if (ScriptPin* paramPin = FindAnimParamPin(cn, &wantType))
                    if (DrawAnimParamList(paramPin, wantType)) ImGui::CloseCurrentPopup();
            }
            else if (m_ComboKind == 1)
            {
                const bool isGetAction = (cn->Name == "Get Action");
                auto& cfg = InputMappingConfig::Get();

                std::vector<std::string> names;
                if (isGetAction) for (auto& a : cfg.GetActions()) names.push_back(a.Name);
                else             for (auto& a : cfg.GetAxes())    names.push_back(a.Name);

                for (const auto& nm : names)
                {
                    if (ImGui::Selectable(nm.c_str(), nm == cn->StringValue))
                    {
                        cn->StringValue = nm;

                        if (!isGetAction)
                        {
                            auto* axis = cfg.FindAxis(nm);
                            if (axis) m_Graph->RebuildAxisOutputPins(cn, (int)axis->ValueType);
                        }

                        ImGui::CloseCurrentPopup();
                    }
                }

                if (names.empty())
                    ImGui::TextDisabled(" Configure em Project > Input Settings");
            }

            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(2);

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
                // SC20 — qualquer tipo vetorial, nao so Vec3.
                auto isVecVar = [](ScriptVarType t) {
                    return ScriptPinComponentCount(ScriptVarTypeToPinType(t)) > 0;
                    };
                int vt = ctxNode->IntValue & ScriptNodeBits::VarTypeMask;
                isVec3Var = isVecVar((ScriptVarType)vt);
                if (!isVec3Var)
                    for (auto& v : m_ScriptAsset->GetVariables())
                        if (v.Name == ctxNode->StringValue)
                        {
                            isVec3Var = isVecVar(v.Type); break;
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
                hasClickedVec3Pin = ScriptPinComponentCount(ctxPin->Type) > 0;
                hasClickedSplitPin = IsSplitPin(*ctxPin); // Float "X","Y","Z","Nome.X" etc.
            }
            bool hasAnyVec3Pin = false;
            if (ctxNode && !ctxPin)
            {
                for (auto& p : ctxNode->Outputs)
                    if (ScriptPinComponentCount(p.Type) > 0 || IsSplitPin(p)) { hasAnyVec3Pin = true; break; }
                if (!hasAnyVec3Pin)
                    for (auto& p : ctxNode->Inputs)
                        if (ScriptPinComponentCount(p.Type) > 0 || IsSplitPin(p)) { hasAnyVec3Pin = true; break; }
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
                        if (ScriptPinComponentCount(p.Type) > 0 || IsSplitPin(p)) { clickedOutput = true; break; }
                    if (!clickedOutput)
                        clickedInput = true;
                }

                bool outputSplit = false, inputSplit = false;
                for (auto& p : ctxNode->Outputs) if (IsSplitPin(p)) { outputSplit = true; break; }
                for (auto& p : ctxNode->Inputs)  if (IsSplitPin(p)) { inputSplit = true; break; }
                bool thisSideSplit = clickedOutput ? outputSplit : inputSplit;

                // Nome do pin Vec3 que vai ser splitado (pode ser "Value", "Position", "Velocity", etc.)
                std::string vec3PinName = "Value"; // default para variáveis
                // SC20 — o TIPO tambem precisa ser guardado, nao so o nome:
                // o Recombine tem de remontar o mesmo tipo que foi aberto, e
                // um Vec4 e um Quat abrem os mesmos quatro pins.
                ScriptPinType splitType = ScriptPinType::Vec3;

                if (ctxPin && ScriptPinComponentCount(ctxPin->Type) > 0)
                {
                    vec3PinName = ctxPin->Name;
                    splitType = ctxPin->Type;
                }
                else if (!ctxPin)
                {
                    auto& pins = clickedOutput ? ctxNode->Outputs : ctxNode->Inputs;
                    for (auto& p : pins)
                        if (ScriptPinComponentCount(p.Type) > 0)
                        {
                            vec3PinName = p.Name; splitType = p.Type; break;
                        }
                }
                else if (isVarNode)
                {
                    // Pin ja splitado num node de variavel: o tipo vem da
                    // propria variavel, que e quem sabe.
                    for (auto& v : m_ScriptAsset->GetVariables())
                        if (v.Name == ctxNode->StringValue)
                        {
                            splitType = ScriptVarTypeToPinType(v.Type); break;
                        }
                }

                const int nComp = ScriptPinComponentCount(splitType);

                if (!thisSideSplit && nComp > 0 && ImGui::MenuItem("Split Struct Pin"))
                {
                    PushUndo("Split Pin");

                    const std::string prefix = (vec3PinName == "Value") ? "" : vec3PinName + ".";
                    const std::string pinToRemove = vec3PinName;

                    auto& pins = clickedOutput ? ctxNode->Outputs : ctxNode->Inputs;
                    const ed::PinKind kind = clickedOutput ? ed::PinKind::Output : ed::PinKind::Input;

                    // Guarda o valor do pin ANTES de apaga-lo, para distribuir
                    // pelos componentes. Sem isto, abrir um pin com valor
                    // digitado zerava tudo — o autor perdia o que escreveu ao
                    // fazer um gesto que so deveria mudar a APRESENTACAO.
                    ScriptValue oldVal;
                    for (const auto& p : pins)
                        if (p.Name == pinToRemove) { oldVal = p.Default; break; }

                    pins.erase(std::remove_if(pins.begin(), pins.end(),
                        [&pinToRemove](const ScriptPin& p) { return p.Name == pinToRemove; }), pins.end());

                    const float* comp = &oldVal.Vec.x;
                    for (int i = 0; i < nComp; i++)
                    {
                        const std::string nm = prefix + ScriptPinComponentName(i);
                        pins.emplace_back(m_Graph->GetNextId(), nm.c_str(), ScriptPinType::Float, kind);
                        pins.back().Default.Float = comp[i];
                    }

                    ctxNode->IntValue |= ScriptNodeBits::SplitPin;
                    SetSplitSourceType(ctxNode->IntValue, splitType);

                    CommitUndo("Split Pin");
                    m_ConsoleLines.push_back("[Info] Pin split: " + vec3PinName +
                        " (" + std::to_string(nComp) + " components)");
                }
                else if (thisSideSplit && ImGui::MenuItem("Recombine Pin"))
                {
                    PushUndo("Recombine Pin");

                    // SC20 — o tipo a remontar vem dos bits do node, gravados
                    // no Split. Antes era Vec3 cravado: recombinar um Vec4
                    // devolveria um Vec3, perdendo o W sem avisar. Grafo antigo
                    // (sem os bits) devolve Vec3, que era o unico caso possivel
                    // na epoca — ver GetSplitSourceType.
                    const ScriptPinType rebuiltType = GetSplitSourceType(ctxNode->IntValue);
                    const std::string recombinedName = vec3PinName;

                    auto& pins = clickedOutput ? ctxNode->Outputs : ctxNode->Inputs;
                    const ed::PinKind kind = clickedOutput ? ed::PinKind::Output : ed::PinKind::Input;

                    // Recolhe os valores dos componentes de volta para o vetor,
                    // pelo SUFIXO — a ordem no vector nao e garantida depois de
                    // um Split parcial ou de um load.
                    ScriptValue merged;
                    float* mc = &merged.Vec.x;
                    for (const auto& p : pins)
                    {
                        if (!IsSplitPin(p)) continue;
                        const char last = p.Name.empty() ? 'X' : p.Name.back();
                        const int idx = (last == 'X') ? 0 : (last == 'Y') ? 1 : (last == 'Z') ? 2 : 3;
                        mc[idx] = p.Default.Float;
                    }

                    pins.erase(std::remove_if(pins.begin(), pins.end(),
                        [](const ScriptPin& p) { return IsSplitPin(p); }), pins.end());

                    pins.emplace_back(m_Graph->GetNextId(), recombinedName.c_str(), rebuiltType, kind);
                    pins.back().Default = merged;

                    outputSplit = inputSplit = false;
                    for (auto& p : ctxNode->Outputs) if (IsSplitPin(p)) { outputSplit = true; break; }
                    for (auto& p : ctxNode->Inputs)  if (IsSplitPin(p)) { inputSplit = true; break; }
                    if (!outputSplit && !inputSplit)
                    {
                        ctxNode->IntValue &= ~ScriptNodeBits::SplitPin;
                        ctxNode->IntValue &= ~ScriptNodeBits::SplitTypeMask;
                    }

                    CommitUndo("Recombine Pin");
                    m_ConsoleLines.push_back("[Info] Pin recombined: " + vec3PinName);
                }
            }

            // ── SC20: Exec Pins e Promote NAO sao alternativa ao Split ───────
            //
            // Este bloco era um "else if" do canSplitRecombine. Consequencia:
            // em qualquer pin que PUDESSE ser splitado — ou seja, todo Vec3 —
            // o menu mostrava Split e mais nada. "Promote to Variable" existia
            // no codigo e era inalcancavel justamente nos pins onde promover
            // faz mais sentido. Voce viu isso como "Vec3 nao tem promote".
            //
            // As tres acoes sao ortogonais: abrir um pin em componentes, dar
            // fluxo ao node e criar uma variavel a partir do pin nao competem
            // entre si. Viraram blocos independentes, com separador entre eles.
            if (canSplitRecombine && ctxNode && ctxPin && ctxPin->Type != ScriptPinType::Flow)
                ImGui::Separator();

            if (ctxNode && ctxPin && ctxPin->Type != ScriptPinType::Flow)
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

                // Wildcard nao tem tipo ainda; promover daria uma variavel
                // Float arbitraria com nome do pin — pior que nao oferecer.
                const bool canPromote = ctxPin->Type != ScriptPinType::Wildcard;

                if (canPromote && ImGui::MenuItem("Promote to Variable"))
                {
                    PushUndo("Promote to Variable");

                    ScriptVariable newVar;
                    newVar.Name = ctxPin->Name.empty() ? "NewVar" : ctxPin->Name;

                    // SC20 — o switch cobria cinco tipos e mandava o resto para
                    // Float. Um pin Vec4, Quat, Vec2 ou Entity virava uma
                    // variavel Float com o nome certo e o tipo errado.
                    // PinTypeToVarType e a mesma tabela que o resto do sistema
                    // usa, entao um tipo novo entra em um lugar so.
                    newVar.Type = PinTypeToVarType(ctxPin->Type);

                    // Leva o valor digitado no pin junto: promover e mover o
                    // valor para uma variavel, nao descarta-lo.
                    newVar.Default = ctxPin->Default;

                    if (m_ScriptAsset) m_ScriptAsset->AddVariable(newVar);

                    bool isInput = ctxPin->Kind == ed::PinKind::Input;
                    m_PendingNodeType = isInput ? "SetVariable" : "GetVariable";
                    m_PendingNodeStrValue = newVar.Name;
                    m_PendingNodePos = m_GraphWindowCenter;
                    m_PendingPromotePinId = ctxPin->ID;
                    m_PendingPromoteIsInput = isInput;
                    m_PendingPromoteVarType = (int)newVar.Type;
                    m_PendingPromotePinType = ctxPin->Type;

                    CommitUndo("Promote to Variable");
                    m_ConsoleLines.push_back("[Info] Promoted to variable: " + newVar.Name);
                }
            }
            else if (!canSplitRecombine)
            {
                // So quando NENHUM dos dois blocos desenhou nada. Antes este
                // else pertencia ao if de Exec/Promote e podia aparecer logo
                // abaixo do Split, dizendo "sem acoes" numa lista que tinha
                // acoes.
                ImGui::TextDisabled("No actions available");
            }

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
                    ImGui::SetTooltip("Managed automatically by the Function - cannot be deleted.");
            }
            ImGui::EndPopup();
        }

        if (ed::ShowBackgroundContextMenu() && !m_SuppressCtxMenuThisFrame)
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

            // SC18 — TODAS as categorias, derivado do array.
            //
            // Este limite era um 8 escrito a mao, e o comentario anterior o
            // defendia dizendo que Array / Flow Control / Cast ficavam de fora
            // "de proposito". Nao ficavam: os nodes existem na fabrica
            // (ForLoop, ForEachLoop, WhileLoop, SwitchOnInt, SwitchOnString) e
            // simplesmente nao tinham como ser criados pela interface. O
            // numero ja havia migrado de 7 para 8 quando a categoria Audio
            // entrou no meio — a proxima categoria inserida quebraria de novo.
            //
            // Derivar do tamanho do array e a unica forma de isso nao voltar.
            constexpr int kCatCount = (int)(sizeof(s_Cats) / sizeof(s_Cats[0]));
            for (int ci = 0; ci < kCatCount; ci++)
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

        // SC2 — por último, para o fio provisório ficar por cima de tudo, e
        // ainda DENTRO do Begin/End porque as posições de pin estão em espaço
        // de canvas (mesmo espaço de ImGui::GetMousePos() aqui dentro).
        DrawCarriedWire();

        m_InsideNodeEditorFrame = false;
        ed::End();

        // DEPOIS do ed::End(): aqui o espaço é de tela, então o cursor tem
        // tamanho fixo independente do zoom do canvas. E é a última chamada de
        // SetMouseCursor do frame, que é a que vale — o canvas define o cursor
        // dele ao fechar o frame (pan, redimensionar grupo) e sobrescreveria.
        DrawHandCursor();

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
            // COMP_DRAG vem do Scene Graph e carrega indice + node: aqui so
            // o node interessa. Node vazio (Mesh, Material...) = componente
            // que existe na hierarquia mas nao tem node correspondente;
            // soltar no canvas nao deve criar nada.
            std::string nodeTypeFromComp;

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMP_DRAG"))
            {
                const auto* drag = (const ComponentDragPayload*)payload->Data;
                if (drag->Node[0] != '\0')
                    nodeTypeFromComp = drag->Node;
            }

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMP_NODE"))
            {
                nodeTypeFromComp = (const char*)payload->Data;
            }

            if (!nodeTypeFromComp.empty())
            {
                const std::string& nodeType = nodeTypeFromComp;
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