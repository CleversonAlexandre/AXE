#include "script_graph_window.hpp"
#include "axe/script/script_graph_compiler.hpp"
#include "axe/script/script_compiler.hpp"
#include "axe/log/log.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <utilities/widgets.h>
#include <utilities/drawing.h>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ed = ax::NodeEditor;
using IconType = ax::Drawing::IconType;

namespace axe
{
    static const float ICON_SZ = 20.0f;
    static const float PIN_H = 22.0f;

    // ── Helpers ───────────────────────────────────────────────────────────────

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

    // ── Node entries (shared entre list e context menu) ───────────────────────

    struct NE { const char* label; const char* type; };
    static const NE sEv[] = { {"On Start","OnStart"},{"On Update","OnUpdate"},
        {"On End","OnEnd"},{"On Collision","OnCollision"},{"On Event","OnEvent"} };
    static const NE sAc[] = { {"Move","Move"},{"Rotate","Rotate"},{"Apply Force","ApplyForce"},
        {"Send Event","SendEvent"},{"Print String","PrintString"} };
    static const NE sLo[] = { {"Branch","Branch"},{"Compare","Compare"},
        {"Get Variable","GetVariable"},{"Set Variable","SetVariable"} };
    static const NE sMa[] = { {"Add","Add"},{"Multiply","Multiply"},{"Make Vec3","MakeVec3"} };
    static const NE sIn[] = { {"Get Key","GetKey"},{"Get Axis","GetAxis"} };

    struct CatDef { const char* name; const NE* e; int n; ImVec4 col; };
    static const CatDef s_Cats[] = {
        {"Eventos",    sEv, 5, {0.85f,0.3f,0.2f,1}},
        {"Acoes",      sAc, 5, {0.2f,0.7f,0.45f,1}},
        {"Logica",     sLo, 4, {0.8f,0.6f,0.1f,1}},
        {"Matematica", sMa, 3, {0.3f,0.5f,0.9f,1}},
        {"Input",      sIn, 2, {0.7f,0.2f,0.6f,1}},
    };

    static const ImVec4 s_CtxCols[] = {
        {1.f,0.45f,0.35f,1},{0.3f,0.85f,0.55f,1},
        {1.f,0.78f,0.2f,1},{0.4f,0.65f,1.f,1},{0.85f,0.3f,0.75f,1} };

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    ScriptGraphWindow::ScriptGraphWindow() = default;
    ScriptGraphWindow::~ScriptGraphWindow() { Shutdown(); }

    void ScriptGraphWindow::Initialize()
    {
        ed::Config cfg; cfg.SettingsFile = nullptr;
        m_EdCtx = ed::CreateEditor(&cfg);
    }

    void ScriptGraphWindow::Shutdown()
    {
        if (m_EdCtx) { ed::DestroyEditor(m_EdCtx); m_EdCtx = nullptr; }
    }

    void ScriptGraphWindow::OpenForEntity(entt::entity entity, ScriptComponent* comp)
    {
        m_Entity = entity;
        m_Component = comp;
        m_Graph = comp ? comp->Graph.get() : nullptr;
        m_IsOpen = true;
        m_FirstFrame = true;
        m_SearchBuf[0] = m_CtxBuf[0] = m_CompSearchBuf[0] = '\0';
        m_ConsoleLines.clear();
        m_ConsoleLines.push_back("[Script Editor] Pronto.");
    }

    void ScriptGraphWindow::Close()
    {
        m_IsOpen = false; m_Graph = nullptr; m_Component = nullptr; m_Entity = entt::null;
    }

    // ── Draw ──────────────────────────────────────────────────────────────────

    void ScriptGraphWindow::Draw()
    {
        if (!m_IsOpen || !m_Graph) return;

        ImGui::SetNextWindowSize(ImVec2(1200, 750), ImGuiCond_FirstUseEver);
        std::string title = "Script Editor — " +
            (m_Component ? m_Component->ScriptName : "?") + "###ScriptGraphWin";

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool vis = ImGui::Begin(title.c_str(), &m_IsOpen,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();
        if (!vis) { ImGui::End(); return; }

        // ── Toolbar ──────────────────────────────────────────────────────────
        if (ImGui::BeginMenuBar())
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.45f, 0.13f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1));
            if (ImGui::Button("  Compilar  ")) CompileScript();
            ImGui::PopStyleColor(2);

            ImGui::SameLine(0, 8);
            if (ImGui::Button("Fit"))
            {
                ed::SetCurrentEditor(m_EdCtx); ed::NavigateToContent(); ed::SetCurrentEditor(nullptr);
            }

            ImGui::SameLine(0, 8);
            ImGui::TextDisabled("%s", m_Component ? m_Component->ScriptName.c_str() : "—");

            if (m_MsgTimer > 0)
            {
                m_MsgTimer -= ImGui::GetIO().DeltaTime;
                ImGui::SameLine(0, 16);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    m_MsgOk ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.3f, 0.3f, 1));
                ImGui::TextUnformatted(m_Msg.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndMenuBar();
        }

        float totalH = ImGui::GetContentRegionAvail().y;
        float totalW = ImGui::GetContentRegionAvail().x;
        float sideW = 200.0f;
        float graphH = totalH * 0.74f;
        float consoleH = totalH - graphH - 2;

        // ── Painel esquerdo ───────────────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        ImGui::BeginChild("##Comp", ImVec2(sideW, totalH), true);
        DrawComponentsPanel();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 0);

        // ── Centro: Graph + Console ───────────────────────────────────────────
        ImGui::BeginGroup();
        ImGui::BeginChild("##GC", ImVec2(totalW - sideW * 2, graphH), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawGraph();
        ImGui::EndChild();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1));
        ImGui::BeginChild("##Con", ImVec2(totalW - sideW * 2, consoleH), true);
        DrawConsole();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        ImGui::SameLine(0, 0);

        // ── Painel direito: Details ───────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        ImGui::BeginChild("##Details", ImVec2(sideW, totalH), true);
        DrawDetailsPanel();
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::End();
    }

    // ── Graph ─────────────────────────────────────────────────────────────────

    void ScriptGraphWindow::DrawGraph()
    {
        ed::SetCurrentEditor(m_EdCtx);
        ed::Begin("##SG", ImVec2(0, 0));

        for (auto& n : m_Graph->GetNodes()) DrawNode(n.get());

        for (auto& lk : m_Graph->GetLinks())
        {
            auto* p = m_Graph->FindPin(lk.StartPin);
            ImColor c = p ? GetPinColor(p->Type) : ImColor(255, 255, 255);
            ed::Link(lk.ID, lk.StartPin, lk.EndPin, c, 2.0f);
        }

        // Criação de link
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
                        ed::RejectNewItem(ImColor(255, 0, 0), 2);
                    else if (o->Type != i->Type)
                        ed::RejectNewItem(ImColor(255, 128, 0), 2);
                    else if (ed::AcceptNewItem(GetPinColor(o->Type), 2.5f))
                        m_Graph->AddLink(o->ID, i->ID);
                }
            }
        }
        ed::EndCreate();

        // Deleção
        if (ed::BeginDelete())
        {
            ed::LinkId lid;
            while (ed::QueryDeletedLink(&lid))
                if (ed::AcceptDeletedItem()) m_Graph->RemoveLink(lid);
            ed::NodeId nid;
            while (ed::QueryDeletedNode(&nid))
                if (ed::AcceptDeletedItem()) m_Graph->RemoveNode(nid);
        }
        ed::EndDelete();

        // Context menu no clique direito
        ed::Suspend();
        if (ed::ShowBackgroundContextMenu())
        {
            m_ShowCtx = true; m_CtxPos = ImGui::GetMousePos(); m_CtxBuf[0] = '\0';
        }
        DrawContextMenu();
        ed::Resume();

        if (m_FirstFrame) { ed::NavigateToContent(); m_FirstFrame = false; }
        ed::End();
        ed::SetCurrentEditor(nullptr);
    }

    // ── DrawNode ─────────────────────────────────────────────────────────────

    void ScriptGraphWindow::DrawNode(ScriptNode* node)
    {
        if (!node) return;

        float inW = 0, outW = 0;
        for (auto& p : node->Inputs)
            inW = std::max(inW, ImGui::CalcTextSize(p.Name.c_str()).x);
        for (auto& p : node->Outputs)
            outW = std::max(outW, ImGui::CalcTextSize(p.Name.c_str()).x);
        float titleW = ImGui::CalcTextSize(node->Name.c_str()).x + 16.0f;
        float nodeW = std::max(titleW, inW + outW + ICON_SZ * 2 + 24.0f);
        nodeW = std::max(nodeW, 160.0f);

        ImVec4 hcol = HdrCol(node->Category);
        ImColor hc(hcol.x, hcol.y, hcol.z, 1.0f);
        ImColor hcDark(hcol.x * .55f, hcol.y * .55f, hcol.z * .55f, 1.0f);

        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(32, 32, 32, 240));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(55, 55, 55, 200));
        ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
        ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 4));

        ed::BeginNode(node->ID);

        // Header
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float  hh = ImGui::GetTextLineHeight() + 10.0f;
            auto* dl = ImGui::GetWindowDrawList();

            dl->AddRectFilled(p, ImVec2(p.x + nodeW, p.y + hh), hc, 6.0f, ImDrawFlags_RoundCornersTop);
            dl->AddRectFilled(ImVec2(p.x, p.y + hh - 3), ImVec2(p.x + nodeW, p.y + hh), hcDark, 0);

            float tw = ImGui::CalcTextSize(node->Name.c_str()).x;
            ImGui::SetCursorScreenPos(ImVec2(p.x + (nodeW - tw) * .5f, p.y + 5));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            ImGui::TextUnformatted(node->Name.c_str());
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + hh));
            ImGui::Dummy(ImVec2(nodeW, 4));
        }

        // Pins
        int maxP = (int)std::max(node->Inputs.size(), node->Outputs.size());
        for (int i = 0; i < maxP; i++)
        {
            bool hasIn = i < (int)node->Inputs.size();
            bool hasOut = i < (int)node->Outputs.size();
            float rowY = ImGui::GetCursorPosY();

            // Input — ícone dentro do BeginPin
            if (hasIn)
            {
                auto& pin = node->Inputs[i];
                ImGui::SetCursorPosY(rowY);
                ed::BeginPin(pin.ID, ed::PinKind::Input);
                ax::Widgets::Icon(ImVec2(ICON_SZ, ICON_SZ), PinIcon(pin.Type),
                    m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), ImVec4(0.1f, 0.1f, 0.1f, 0.8f));
                ed::EndPin();
                ImGui::SameLine(0, 3);
                ImGui::SetCursorPosY(rowY + (ICON_SZ - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.88f, 1));
                ImGui::TextUnformatted(pin.Name.c_str());
                ImGui::PopStyleColor();
            }

            // Output — texto fora, ícone dentro do BeginPin
            if (hasOut)
            {
                auto& pin = node->Outputs[i];
                float textW = ImGui::CalcTextSize(pin.Name.c_str()).x;
                float totW = textW + 3.0f + ICON_SZ;

                ImGui::SameLine(nodeW - totW - 2.0f);
                ImGui::SetCursorPosY(rowY + (ICON_SZ - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.88f, 1));
                ImGui::TextUnformatted(pin.Name.c_str());
                ImGui::PopStyleColor();

                ImGui::SameLine(0, 3);
                ImGui::SetCursorPosY(rowY);
                ed::BeginPin(pin.ID, ed::PinKind::Output);
                ax::Widgets::Icon(ImVec2(ICON_SZ, ICON_SZ), PinIcon(pin.Type),
                    m_Graph->IsPinLinked(pin.ID), PinCol(pin.Type), ImVec4(0.1f, 0.1f, 0.1f, 0.8f));
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

    // ── Components Panel ─────────────────────────────────────────────────────

    void ScriptGraphWindow::DrawComponentsPanel()
    {
        if (!m_Component || m_Entity == entt::null)
        {
            ImGui::TextDisabled("Nenhum objeto."); return;
        }

        // 3D Preview
        float pw = ImGui::GetContentRegionAvail().x;
        float ph = pw; // quadrado
        if (m_ViewportTexture)
        {
            ImGui::TextDisabled("Preview");
            ImGui::Image(m_ViewportTexture, ImVec2(pw, ph), ImVec2(0, 1), ImVec2(1, 0));
        }
        else
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(p,
                ImVec2(p.x + pw, p.y + ph), ImColor(20, 20, 25, 255), 4.0f);
            float tw = ImGui::CalcTextSize("Sem Preview").x;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(p.x + (pw - tw) * 0.5f, p.y + ph * 0.5f - 8),
                ImColor(80, 80, 90, 255), "Sem Preview");
            ImGui::Dummy(ImVec2(pw, ph));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Nome do script
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.6f, 1));
        ImGui::TextUnformatted(m_Component->ScriptName.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Botão "+" elegante
        float bw = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.15f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.2f, 1));
        if (ImGui::Button("+ Adicionar Componente", ImVec2(bw, 0)))
            ImGui::OpenPopup("##addcomp");
        ImGui::PopStyleColor(2);

        // Popup de componentes com busca
        ImGui::SetNextWindowSize(ImVec2(200, 280), ImGuiCond_Always);
        if (ImGui::BeginPopup("##addcomp"))
        {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##cs2", "Pesquisar...", m_CompSearchBuf, sizeof(m_CompSearchBuf));
            ImGui::Separator();

            std::string s = m_CompSearchBuf;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);

            struct CE { const char* name; const char* desc; ImVec4 col; };
            static const CE comps[] = {
                {"Rigidbody",        "Fisica dinamica",        ImVec4(0.3f,0.7f,1.0f,1)},
                {"Collider Box",     "Colisao retangular",     ImVec4(0.3f,1.0f,0.5f,1)},
                {"Collider Sphere",  "Colisao esferica",       ImVec4(0.3f,1.0f,0.5f,1)},
                {"Collider Capsule", "Colisao capsula",        ImVec4(0.3f,1.0f,0.5f,1)},
                {"Mesh Collider",    "Colisao por mesh",       ImVec4(0.3f,1.0f,0.5f,1)},
                {"Character Ctrl",   "Controlador personagem", ImVec4(1.0f,0.7f,0.2f,1)},
                {"Point Light",      "Luz pontual",            ImVec4(1.0f,0.9f,0.3f,1)},
                {"Camera",           "Camera de jogo",         ImVec4(0.7f,0.5f,1.0f,1)},
            };

            for (auto& c : comps)
            {
                std::string low = c.name;
                std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                if (!s.empty() && low.find(s) == std::string::npos) continue;

                ImGui::PushStyleColor(ImGuiCol_Text, c.col);
                if (ImGui::MenuItem(c.name))
                {
                    m_ConsoleLines.push_back(
                        std::string("[Info] Use o Inspector para adicionar ") + c.name);
                    m_CompSearchBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.desc);
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("Nodes: %d", (int)m_Graph->GetNodes().size());
        ImGui::TextDisabled("Links:  %d", (int)m_Graph->GetLinks().size());
    }

    // ── Details Panel ─────────────────────────────────────────────────────────

    void ScriptGraphWindow::DrawDetailsPanel()
    {
        ImGui::TextDisabled("Details");
        ImGui::Separator();
        ImGui::Spacing();

        ed::SetCurrentEditor(m_EdCtx);
        int sel = ed::GetSelectedObjectCount();
        ed::NodeId selNode; int got = 0;
        if (sel > 0) got = ed::GetSelectedNodes(&selNode, 1);
        ed::SetCurrentEditor(nullptr);

        if (sel == 0 || got == 0)
        {
            ImGui::TextDisabled("Selecione um node.");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextDisabled("Dicas:");
            ImGui::TextWrapped("Clique direito no canvas para adicionar nodes");
            ImGui::Spacing();
            ImGui::TextWrapped("Arraste pins para conectar");
            ImGui::Spacing();
            ImGui::TextWrapped("Delete para remover selecionados");
            return;
        }

        auto* node = m_Graph->FindNode(selNode);
        if (!node) { ImGui::TextDisabled("Link selecionado."); return; }

        ImColor hc = GetNodeHeaderColor(node->Category);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(hc.Value.x, hc.Value.y, hc.Value.z, 1));
        ImGui::TextUnformatted(node->Name.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Campos editáveis por tipo
        if (node->Name == "Get Key" || node->Name == "Get Axis" ||
            node->Name == "Get Variable" || node->Name == "Set Variable" ||
            node->Name == "Print String")
        {
            const char* lbl =
                node->Name == "Get Key" ? "Tecla (ex: W):" :
                node->Name == "Get Axis" ? "Eixo (ex: Horizontal):" :
                node->Name == "Print String" ? "Mensagem:" : "Variavel:";
            ImGui::TextDisabled("%s", lbl);
            ImGui::SetNextItemWidth(-1);
            char buf[128];
            std::strncpy(buf, node->StringValue.c_str(), sizeof(buf));
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("##sv", buf, sizeof(buf)))
                node->StringValue = buf;
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        // Lista de pins
        if (!node->Inputs.empty())
        {
            ImGui::TextDisabled("Inputs:");
            for (auto& p : node->Inputs)
            {
                ImColor pc = GetPinColor(p.Type);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(pc.Value.x, pc.Value.y, pc.Value.z, 1));
                ImGui::BulletText("%s", p.Name.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
        }
        if (!node->Outputs.empty())
        {
            ImGui::TextDisabled("Outputs:");
            for (auto& p : node->Outputs)
            {
                ImColor pc = GetPinColor(p.Type);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(pc.Value.x, pc.Value.y, pc.Value.z, 1));
                ImGui::BulletText("%s", p.Name.c_str());
                ImGui::PopStyleColor();
            }
        }
    }

    // ── DrawNodeList (mantido por compatibilidade — não exibido) ──────────────

    void ScriptGraphWindow::DrawNodeList() { /* nodes via context menu */ }

    // ── Context Menu (clique direito no canvas) ───────────────────────────────

    void ScriptGraphWindow::DrawContextMenu()
    {
        if (!m_ShowCtx) return;
        ImGui::OpenPopup("##ctx");
        m_ShowCtx = false;

        ImGui::SetNextWindowSize(ImVec2(215, 330), ImGuiCond_Always);
        if (!ImGui::BeginPopup("##ctx")) return;

        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##cs", "Pesquisar node...", m_CtxBuf, sizeof(m_CtxBuf));
        ImGui::Separator();

        std::string s = m_CtxBuf;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        bool filtering = !s.empty();

        for (int ci = 0; ci < 5; ci++)
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
            bool show = filtering ||
                ImGui::CollapsingHeader(cat.name,
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
                if (ImGui::MenuItem(l.c_str()))
                {
                    SpawnAt(cat.e[i].type, m_CtxPos);
                    m_CtxBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
            }
            ImGui::Unindent(6);
        }
        ImGui::EndPopup();
    }

    // ── Console ───────────────────────────────────────────────────────────────

    void ScriptGraphWindow::DrawConsole()
    {
        ImGui::TextDisabled("Console");
        ImGui::SameLine();
        if (ImGui::SmallButton("Limpar")) m_ConsoleLines.clear();
        ImGui::Separator();

        for (auto& line : m_ConsoleLines)
        {
            bool err = line.find("[ERROR]") != std::string::npos;
            bool warn = line.find("[WARN]") != std::string::npos;
            ImGui::PushStyleColor(ImGuiCol_Text,
                err ? ImVec4(1, .3f, .3f, 1) :
                warn ? ImVec4(1, .8f, .2f, 1) :
                ImVec4(.8f, .8f, .8f, 1));
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }

    // ── SpawnAt ───────────────────────────────────────────────────────────────

    void ScriptGraphWindow::SpawnAt(const char* type, ImVec2 pos)
    {
        if (!m_Graph) return;
        auto* node = m_Graph->AddNode(type);
        if (!node) return;
        ed::SetCurrentEditor(m_EdCtx);
        if (pos.x != 0 || pos.y != 0) ed::SetNodePosition(node->ID, pos);
        ed::SetCurrentEditor(nullptr);
    }

    // ── CompileScript ─────────────────────────────────────────────────────────

    void ScriptGraphWindow::CompileScript()
    {
        if (!m_Component || !m_Graph) return;
        m_ConsoleLines.push_back("[Script Editor] Gerando C++...");

        std::string code = ScriptGraphCompiler::Generate(*m_Graph, m_Component->ScriptName);

        std::filesystem::path dir = "temp_scripts";
        std::filesystem::create_directories(dir);
        auto cpp = (dir / (m_Component->ScriptName + ".cpp")).string();
        auto dll = (dir / (m_Component->ScriptName + ".dll")).string();

        std::ofstream f(cpp); f << code; f.close();
        m_ConsoleLines.push_back("[Script Editor] .cpp salvo: " + cpp);
        m_Msg = "Compilando..."; m_MsgOk = true; m_MsgTimer = 2.0f;

        bool ok = ScriptCompiler::Compile(cpp, dll, "src",
            [this](const std::string& msg, bool success)
            {
                m_Msg = success ? "Compilado!" : "Erro";
                m_MsgOk = success;
                m_MsgTimer = 5.0f;
                std::istringstream ss(msg);
                std::string ln;
                while (std::getline(ss, ln))
                    m_ConsoleLines.push_back(success ? "[OK] " + ln : "[ERROR] " + ln);
            });

        if (ok) { m_Component->DllPath = dll; m_Component->IsCompiled = true; }
    }

} // namespace axe