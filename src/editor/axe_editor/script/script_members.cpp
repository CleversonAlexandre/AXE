// script_members.cpp
// DrawMyBlueprintWindow — painel "Script Members":
// Variables agrupadas por categoria (colapso + drag and drop entre categorias),
// Override Events, Event Dispatchers.

#include "script_graph_window.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_graph.hpp"
#include "editor/axe_editor/editor_icon_library.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace axe
{
    void ScriptGraphWindow::DrawMyBlueprintWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (!ImGui::Begin("Script Members")) { ImGui::PopStyleVar(); ImGui::End(); return; }

        if (!m_ScriptAsset)
        {
            ImGui::TextDisabled("No script open.");
            ImGui::End(); ImGui::PopStyleVar(); return;
        }

        auto& vars = m_ScriptAsset->GetVariables();
        auto& evts = m_ScriptAsset->GetCustomEvents();

        static const char* s_VarTypes[] = {
            "Float","Bool","Int","Vec3","String","Vec2","Vec4","Quat","Entity",
            "Float Array","Bool Array","Int Array","Vec3 Array","String Array",
            "Vec2 Array","Vec4 Array","Quat Array","Entity Array",
        };
        static const ImVec4 s_VarCols[] = {
            {0.12f,0.55f,0.24f,1}, // Float
            {0.70f,0.16f,0.16f,1}, // Bool
            {0.31f,0.78f,0.31f,1}, // Int
            {0.78f,0.71f,0.12f,1}, // Vec3
            {0.78f,0.31f,0.59f,1}, // String
            {0.16f,0.78f,0.78f,1}, // Vec2
            {0.63f,0.31f,0.86f,1}, // Vec4
            {0.71f,0.55f,0.86f,1}, // Quat
            {0.24f,0.47f,0.78f,1}, // Entity
            // Arrays — mesma cor da família escalar, levemente mais escura
            // (mesma convenção usada em GetPinColor/GetVariableNodeColor).
            {0.06f,0.35f,0.16f,1}, // FloatArray
            {0.47f,0.08f,0.08f,1}, // BoolArray
            {0.16f,0.55f,0.16f,1}, // IntArray
            {0.55f,0.47f,0.04f,1}, // Vec3Array
            {0.55f,0.16f,0.39f,1}, // StringArray
            {0.08f,0.55f,0.55f,1}, // Vec2Array
            {0.43f,0.16f,0.63f,1}, // Vec4Array
            {0.47f,0.35f,0.63f,1}, // QuatArray
            {0.12f,0.27f,0.55f,1}, // EntityArray
        };

        float avail = ImGui::GetContentRegionAvail().x;

        // ── VARIABLES ─────────────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.3f, 0.5f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.4f, 0.6f, 1));
        bool varOpen = ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (varOpen)
        {
            // ── Criação — só Nome e Tipo ──────────────────────────────────────
            ImGui::SetNextItemWidth(avail * 0.40f);
            ImGui::InputText("##vname", m_NewVarName, sizeof(m_NewVarName));
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(avail * 0.28f);
            ImGui::Combo("##vtype", &m_NewVarType, s_VarTypes, 18);
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.18f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.55f, 0.24f, 1));
            {
                auto addIcon = EditorIconLibrary::Get().GetAdd();
                bool clicked;
                if (addIcon && addIcon->IsLoaded())
                {
                    float iconSz = 13.f;
                    const char* lbl = "Var";
                    float textW = ImGui::CalcTextSize(lbl).x;
                    ImVec2 btnSz(iconSz + 5.f + textW + 14.f, 0);
                    clicked = ImGui::Button("##addvarbtn", btnSz);
                    ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float cy = (r0.y + r1.y) * 0.5f;
                    dl->AddImage((ImTextureID)(uintptr_t)addIcon->GetRendererID(),
                        ImVec2(r0.x + 7.f, cy - iconSz * 0.5f), ImVec2(r0.x + 7.f + iconSz, cy + iconSz * 0.5f),
                        ImVec2(0, 1), ImVec2(1, 0));
                    dl->AddText(ImVec2(r0.x + 7.f + iconSz + 5.f, cy - ImGui::GetFontSize() * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), lbl);
                }
                else
                {
                    clicked = ImGui::SmallButton("+ Var");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Adicionar nova variável");
                if (clicked)
                {
                    ScriptVariable v;
                    v.Name = (m_NewVarName[0] != 0) ? m_NewVarName : "NewVar";
                    v.Type = (ScriptVarType)m_NewVarType;
                    // Categoria vazia por padrão — definida na aba Node
                    PushUndo("Add Variable");
                    m_ScriptAsset->AddVariable(v);
                    CommitUndo("Add Variable");
                    m_NewVarName[0] = 0;
                }
            }
            ImGui::PopStyleColor(2);
            ImGui::Spacing();

            // ── Coleta categorias únicas (sem categoria sempre primeiro) ───────
            std::vector<std::string> categories;
            categories.push_back("");
            for (auto& v : vars)
            {
                if (v.Category.empty()) continue;
                bool found = false;
                for (auto& c : categories) if (c == v.Category) { found = true; break; }
                if (!found) categories.push_back(v.Category);
            }

            int removeVar = -1;
            std::string deleteCat;
            std::string dragDropTargetCat; // categoria de destino do drag
            bool dragDropTargetIsRoot = false;

            for (const auto& cat : categories)
            {
                bool catOpen = true;

                if (!cat.empty())
                {
                    int catCount = (int)std::count_if(vars.begin(), vars.end(),
                        [&](const ScriptVariable& v) { return v.Category == cat; });
                    float headerAvail = ImGui::GetContentRegionAvail().x;

                    // Renomear categoria (duplo clique)
                    bool isRenaming = (m_RenamingCat == cat);
                    if (isRenaming)
                    {
                        if (m_RenameCatJustStarted) { ImGui::SetKeyboardFocusHere(); m_RenameCatJustStarted = false; }
                        ImGui::SetNextItemWidth(headerAvail - 30.f);
                        if (ImGui::InputText("##rencat", m_RenameCatBuf, sizeof(m_RenameCatBuf),
                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                        {
                            std::string newCat = m_RenameCatBuf;
                            if (!newCat.empty() && newCat != cat)
                                for (auto& v : vars)
                                    if (v.Category == cat) v.Category = newCat;
                            m_RenamingCat.clear();
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_RenamingCat.clear();
                        // Drop target para mover vars para esta categoria enquanto renomeia
                        if (ImGui::BeginDragDropTarget())
                        {
                            if (ImGui::AcceptDragDropPayload("VAR_RECAT"))
                                if (m_DragVarIndex >= 0 && m_DragVarIndex < (int)vars.size())
                                    vars[m_DragVarIndex].Category = cat;
                            ImGui::EndDragDropTarget();
                        }
                        continue;
                    }

                    // Header da categoria
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.22f, 0.38f, 1));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.30f, 0.48f, 1));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.85f, 1.0f, 1));
                    catOpen = ImGui::TreeNodeEx(("##cat_" + cat).c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap,
                        "%s  (%d)", cat.c_str(), catCount);
                    ImGui::PopStyleColor(3);

                    // Drop target no header da categoria — precisa ficar logo após o
                    // TreeNodeEx (item ao qual deve se vincular). Se ficar depois do
                    // botão "x" (SameLine), o BeginDragDropTarget se vincula à área
                    // minúscula do botão em vez da linha inteira do header, e soltar
                    // sobre o texto da categoria não ativa o drop.
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (ImGui::AcceptDragDropPayload("VAR_RECAT"))
                            if (m_DragVarIndex >= 0 && m_DragVarIndex < (int)vars.size())
                                vars[m_DragVarIndex].Category = cat;
                        ImGui::EndDragDropTarget();
                    }

                    // Duplo clique → renomear
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        m_RenamingCat = cat;
                        strncpy(m_RenameCatBuf, cat.c_str(), 63);
                        m_RenameCatBuf[63] = 0;
                        m_RenameCatJustStarted = true;
                    }

                    // Botão X — remove categoria (vars voltam para sem categoria)
                    ImGui::SameLine(headerAvail - 18.f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1));
                    if (ImGui::SmallButton(("x##delcat_" + cat).c_str()))
                        deleteCat = cat;
                    ImGui::PopStyleColor(3);

                    if (!catOpen) continue;
                    ImGui::Indent(8.f);
                }
                else
                {
                    // Área "sem categoria" — drop target invisível no topo
                    ImGui::Dummy(ImVec2(avail, 2.f));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (ImGui::AcceptDragDropPayload("VAR_RECAT"))
                            if (m_DragVarIndex >= 0 && m_DragVarIndex < (int)vars.size())
                                vars[m_DragVarIndex].Category = "";
                        ImGui::EndDragDropTarget();
                    }
                }

                // ── Variáveis desta categoria ─────────────────────────────────
                for (int i = 0; i < (int)vars.size(); i++)
                {
                    auto& v = vars[i];
                    if (v.Category != cat) continue;

                    ImGui::PushID(i);
                    bool sel = (m_SelectedVar == i);
                    bool wantRename = (m_RenamingVar == i);
                    ImVec4 typeCol = s_VarCols[(int)v.Type];

                    // ── Card estilo UE5: fundo arredondado + respiro vertical ──
                    // Altura SEMPRE compacta — a expansão de detalhes (Type,
                    // Default Value, Tamanho, etc.) vive no Script Details →
                    // Node quando a variável é selecionada, não dentro do card.
                    float cardWidth = ImGui::GetContentRegionAvail().x;
                    float headerH = 36.f;
                    float cardHeight = headerH ;
                    ImVec2 cardMin = ImGui::GetCursorScreenPos();
                    ImVec2 cardMax = ImVec2(cardMin.x + cardWidth, cardMin.y + cardHeight);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 cardBg = sel
                        ? ImGui::ColorConvertFloat4ToU32(ImVec4(typeCol.x * 0.30f, typeCol.y * 0.30f, typeCol.z * 0.30f, 0.55f))
                        : ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.04f));
                    dl->AddRectFilled(cardMin, cardMax, cardBg, 6.0f);
                    if (sel)
                        dl->AddRect(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(ImVec4(typeCol.x, typeCol.y, typeCol.z, 0.65f)), 6.0f, 0, 1.5f);

                    ImGui::Dummy(ImVec2(8, headerH)); // respiro esquerdo
                    ImGui::SameLine(0, 0);
                    ImGui::BeginGroup();
                    ImGui::Dummy(ImVec2(0, (headerH - 30.f) * 0.5f)); // centraliza verticalmente

                    // Badge de tipo — "pill" colorida em vez de texto cru.
                    // Para arrays, mostra só o nome BASE (ex.: "String", não
                    // "String Array") — o "é um array" é comunicado por um
                    // pequeno ícone de grade 2x2 ao lado, não pelo texto da
                    // badge, mantendo o card do mesmo tamanho compacto de
                    // antes independente do tipo.
                    bool isArr = IsArrayType(v.Type);
                    std::string typeLabelStr = s_VarTypes[(int)v.Type];
                    if (isArr)
                    {
                        size_t pos = typeLabelStr.find(" Array");
                        if (pos != std::string::npos) typeLabelStr = typeLabelStr.substr(0, pos);
                    }
                    const char* typeLabel = typeLabelStr.c_str();
                    ImVec2 badgeStart = ImGui::GetCursorScreenPos();
                    ImVec2 textSz = ImGui::CalcTextSize(typeLabel);
                    ImVec2 badgeSz = ImVec2(textSz.x + 14.f, 20.f);
                    ImU32 badgeBg = ImGui::ColorConvertFloat4ToU32(ImVec4(typeCol.x, typeCol.y, typeCol.z, 0.22f));
                    ImU32 badgeBorder = ImGui::ColorConvertFloat4ToU32(ImVec4(typeCol.x, typeCol.y, typeCol.z, 0.9f));
                    dl->AddRectFilled(badgeStart, ImVec2(badgeStart.x + badgeSz.x, badgeStart.y + badgeSz.y), badgeBg, 10.0f);
                    dl->AddRect(badgeStart, ImVec2(badgeStart.x + badgeSz.x, badgeStart.y + badgeSz.y), badgeBorder, 10.0f, 0, 1.2f);
                    dl->AddText(ImVec2(badgeStart.x + 7.f, badgeStart.y + (badgeSz.y - ImGui::GetFontSize()) * 0.5f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(typeCol.x * 0.5f + 0.5f, typeCol.y * 0.5f + 0.5f, typeCol.z * 0.5f + 0.5f, 1)),
                        typeLabel);
                    ImGui::Dummy(badgeSz);
                    ImGui::SameLine(0, isArr ? 4.f : 8.f);

                    if (isArr)
                    {
                        // Ícone "grade 2x2" desenhado diretamente (sem asset
                        // extra) — convenção visual usada pela Unreal para
                        // indicar arrays nos painéis de variável.
                        ImVec2 gp = ImGui::GetCursorScreenPos();
                        float gs = 5.f, gap = 1.5f;
                        ImU32 gridCol = ImGui::ColorConvertFloat4ToU32(ImVec4(typeCol.x, typeCol.y, typeCol.z, 0.95f));
                        for (int gy = 0; gy < 2; gy++)
                            for (int gx = 0; gx < 2; gx++)
                            {
                                ImVec2 cellMin(gp.x + gx * (gs + gap), gp.y + 5.f + gy * (gs + gap));
                                dl->AddRectFilled(cellMin, ImVec2(cellMin.x + gs, cellMin.y + gs), gridCol, 1.5f);
                            }
                        ImGui::Dummy(ImVec2(gs * 2.f + gap, 20.f));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Array");
                        ImGui::SameLine(0, 8);
                    }

                    if (wantRename)
                    {
                        ImGui::SetNextItemWidth(avail * 0.45f);
                        if (m_RenameJustStarted) { ImGui::SetKeyboardFocusHere(); m_RenameJustStarted = false; }
                        if (ImGui::InputText("##ren", m_RenameBuf, sizeof(m_RenameBuf),
                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                        {
                            if (m_RenameBuf[0] != 0)
                            {
                                std::string oldName = v.Name;
                                v.Name = m_RenameBuf;
                                if (m_Graph)
                                    for (auto& n : m_Graph->GetNodes())
                                        if ((n->Name == "Get Variable" || n->Name == "Set Variable")
                                            && n->StringValue == oldName)
                                            n->StringValue = v.Name;
                            }
                            m_RenamingVar = -1;
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_RenamingVar = -1;
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.06f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.06f));
                        if (ImGui::Selectable(v.Name.c_str(), false,
                            ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cardWidth - badgeSz.x - 56.f, 20)))
                        {
                            m_SelectedVar = i;
                            // Sincroniza buffer de edição de categoria
                            if (m_VarCatEditIdx != i)
                            {
                                strncpy(m_VarCatEditBuf, v.Category.c_str(), 63);
                                m_VarCatEditBuf[63] = 0;
                                m_VarCatEditIdx = i;
                            }
                            if (ImGui::IsMouseDoubleClicked(0))
                            {
                                m_RenamingVar = i; m_RenameJustStarted = true;
                                strncpy(m_RenameBuf, v.Name.c_str(), sizeof(m_RenameBuf) - 1);
                                m_RenameBuf[sizeof(m_RenameBuf) - 1] = 0;
                            }
                        }
                        ImGui::PopStyleColor(2);
                        if (sel && ImGui::IsKeyPressed(ImGuiKey_F2))
                        {
                            m_RenamingVar = i; m_RenameJustStarted = true;
                            strncpy(m_RenameBuf, v.Name.c_str(), sizeof(m_RenameBuf) - 1);
                            m_RenameBuf[sizeof(m_RenameBuf) - 1] = 0;
                        }
                    }

                    // ── Drag and drop — payload único (VAR_RECAT) ──────────────────
                    // IMPORTANTE: ImGui::SetDragDropPayload mantém apenas o ÚLTIMO
                    // payload definido na sessão como o "active payload" interno do
                    // ImGui (g.DragDropPayload é um único objeto, não uma lista).
                    // Definir VAR_RECAT e depois VAR_NODE na mesma sessão fazia o
                    // VAR_NODE sempre vencer — por isso o header de categoria nunca
                    // recebia o aceite, mesmo com o target sendo alcançado. Solução:
                    // um único payload (VAR_RECAT, com o índice), consumido tanto
                    // pelos headers de categoria quanto pelo canvas do graph (que
                    // resolve o nome da variável a partir do índice).
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        m_DragVarIndex = i;
                        ImGui::SetDragDropPayload("VAR_RECAT", &i, sizeof(int));

                        ImGui::PushStyleColor(ImGuiCol_Text, typeCol);
                        ImGui::Text("Get %s", v.Name.c_str());
                        ImGui::PopStyleColor();
                        ImGui::EndDragDropSource();
                    }

                    // Sem expansão dentro do card — selecionar a variável mostra
                    // seus detalhes (Type, Tamanho, etc.) no Script Details → Node,
                    // igual já funciona para Float/Vec3/etc. O card permanece
                    // sempre compacto, independente de estar selecionado ou não.

                    ImGui::EndGroup();
                    ImGui::SameLine();

                    // X — alinhado à direita do card, com margem real para o
                    // retângulo de hover do botão não tocar/vazar visualmente
                    // pela borda arredondada do card (cardMax.x é a borda exata,
                    // sem nenhuma margem — o hover reto "vazava" visualmente
                    // sobre o canto arredondado do fundo).
                    ImGui::SetCursorScreenPos(ImVec2(cardMax.x - 30.f, cardMin.y + (headerH - 20.f) * 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                    if (ImGui::SmallButton("x")) removeVar = i;
                    ImGui::PopStyleColor(3);

                    ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y + 3.f)); // espaçamento entre cards
                    ImGui::PopID();
                }

                if (!cat.empty())
                {
                    ImGui::Unindent(8.f);
                    ImGui::TreePop();
                }
            }

            // ── Processa remoção de categoria ─────────────────────────────────
            if (!deleteCat.empty())
                for (auto& v : vars)
                    if (v.Category == deleteCat) v.Category = "";

            // ── Deleção de variável ───────────────────────────────────────────
            if (removeVar >= 0 && removeVar < (int)vars.size())
            {
                std::string varName = vars[removeVar].Name;
                bool inGraph = false;
                if (m_Graph)
                    for (auto& n : m_Graph->GetNodes())
                        if ((n->Name == "Get Variable" || n->Name == "Set Variable")
                            && n->StringValue == varName)
                        {
                            inGraph = true; break;
                        }

                if (inGraph)
                {
                    m_DeleteVarIndex = removeVar;
                    m_DeleteVarName = varName;
                    ImGui::OpenPopup("##ConfirmDeleteVar");
                }
                else
                {
                    PushUndo("Remove Variable");
                    m_ScriptAsset->RemoveVariable(removeVar);
                    CommitUndo("Remove Variable");
                    m_SelectedVar = m_RenamingVar = m_VarCatEditIdx = -1;
                }
            }

            // Modal de confirmação de deleção
            ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Always);
            if (ImGui::BeginPopupModal("##ConfirmDeleteVar", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.8f, 0.2f, 1));
                ImGui::TextWrapped("A variável '%s' é referenciada em nodes do graph.", m_DeleteVarName.c_str());
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::Checkbox("Deletar nodes relacionados também", &m_DeleteVarAlsoNodes);
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1));
                if (ImGui::Button("Delete", ImVec2(100, 0)))
                {
                    if (m_Graph)
                        for (auto& n : m_Graph->GetNodes())
                            if ((n->Name == "Get Variable" || n->Name == "Set Variable")
                                && n->StringValue == m_DeleteVarName)
                            {
                                if (m_DeleteVarAlsoNodes)
                                    m_PendingDeleteNodes.push_back(n->ID);
                                else
                                    n->StringValue = "[deleted] " + m_DeleteVarName;
                            }
                    PushUndo("Remove Variable");
                    m_ScriptAsset->RemoveVariable(m_DeleteVarIndex);
                    CommitUndo("Remove Variable");
                    m_SelectedVar = m_RenamingVar = m_VarCatEditIdx = -1;
                    m_DeleteVarIndex = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0)))
                {
                    m_DeleteVarIndex = -1; ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Spacing();
        }

        // ── OVERRIDE EVENTS ───────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.2f, 0.1f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.5f, 0.3f, 0.2f, 1));
        bool ovrOpen = ImGui::CollapsingHeader("Override Events", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (ovrOpen)
        {
            ImGui::TextDisabled("Double-click to add to graph:");
            ImGui::Spacing();
            static const struct { const char* label; const char* type; } s_Overrides[] = {
                {"On Start","OnStart"},{"On Update","OnUpdate"},{"On End","OnEnd"},
                {"On Collision","OnCollision"},{"On Event","OnEvent"},
            };
            const ImVec4 ovrCol = ImVec4(1.f, 0.62f, 0.28f, 1);
            ImDrawList* dl = ImGui::GetWindowDrawList();

            for (size_t idx = 0; idx < sizeof(s_Overrides) / sizeof(s_Overrides[0]); idx++)
            {
                auto& ov = s_Overrides[idx];
                ImGui::PushID(ov.type);

                // ── Linha estilizada (lista vertical — escala bem com muitos itens) ──
                float rowWidth = ImGui::GetContentRegionAvail().x;
                float rowHeight = 24.f;
                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImVec2 rowMax = ImVec2(rowMin.x + rowWidth, rowMin.y + rowHeight);
                bool hovered = ImGui::IsMouseHoveringRect(rowMin, rowMax) && ImGui::IsWindowHovered();

                if (hovered)
                    dl->AddRectFilled(rowMin, rowMax,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(ovrCol.x, ovrCol.y, ovrCol.z, 0.14f)), 4.0f);

                // Marcador (diamante) — substitui o bullet cru por algo mais alinhado
                // ao estilo geral, mas mantém a leitura vertical de lista.
                float cy = rowMin.y + rowHeight * 0.5f;
                float mx = rowMin.x + 10.f;
                float ms = 3.5f;
                ImU32 markCol = ImGui::ColorConvertFloat4ToU32(ovrCol);
                dl->AddQuadFilled(
                    ImVec2(mx, cy - ms), ImVec2(mx + ms, cy),
                    ImVec2(mx, cy + ms), ImVec2(mx - ms, cy), markCol);

                dl->AddText(ImVec2(rowMin.x + 24.f, rowMin.y + 4.f), markCol, ov.label);

                ImGui::InvisibleButton("##ovrrow", ImVec2(rowWidth, rowHeight));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_Graph)
                {
                    ed::SetCurrentEditor(m_EdCtx);
                    auto* node = m_Graph->AddNode(ov.type);
                    if (node) ed::SetNodePosition(node->ID, ed::ScreenToCanvas(m_GraphWindowCenter));
                    ed::SetCurrentEditor(nullptr);
                    m_ConsoleLines.push_back(std::string("[Info] Added: ") + ov.label);
                }

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("COMP_NODE", ov.type, strlen(ov.type) + 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, ovrCol);
                    ImGui::Text("Add %s", ov.label);
                    ImGui::PopStyleColor();
                    ImGui::EndDragDropSource();
                }

                ImGui::PopID();
            }
            ImGui::Spacing();
        }

        // ── EVENT DISPATCHERS ─────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.2f, 0.3f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.3f, 0.4f, 1));
        bool evtOpen = ImGui::CollapsingHeader("Event Dispatchers", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (evtOpen)
        {
            ImGui::SetNextItemWidth(avail * 0.62f);
            ImGui::InputText("##evtname", m_NewEvtName, sizeof(m_NewEvtName));
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.16f, 0.26f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.22f, 0.34f, 1));
            {
                auto addIcon = EditorIconLibrary::Get().GetAdd();
                bool clicked;
                if (addIcon && addIcon->IsLoaded())
                {
                    float iconSz = 13.f;
                    const char* lbl = "Event";
                    float textW = ImGui::CalcTextSize(lbl).x;
                    ImVec2 btnSz(iconSz + 5.f + textW + 14.f, 0);
                    clicked = ImGui::Button("##addevtbtn", btnSz);
                    ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float cy = (r0.y + r1.y) * 0.5f;
                    dl->AddImage((ImTextureID)(uintptr_t)addIcon->GetRendererID(),
                        ImVec2(r0.x + 7.f, cy - iconSz * 0.5f), ImVec2(r0.x + 7.f + iconSz, cy + iconSz * 0.5f),
                        ImVec2(0, 1), ImVec2(1, 0));
                    dl->AddText(ImVec2(r0.x + 7.f + iconSz + 5.f, cy - ImGui::GetFontSize() * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), lbl);
                }
                else
                {
                    clicked = ImGui::SmallButton("+ Event");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Adicionar novo Event Dispatcher");
                if (clicked)
                {
                    ScriptCustomEvent e;
                    e.Name = (m_NewEvtName[0] != 0) ? m_NewEvtName : "OnMyEvent";
                    PushUndo("Add Event");
                    m_ScriptAsset->AddCustomEvent(e);
                    CommitUndo("Add Event");
                    m_NewEvtName[0] = 0;
                }
            }
            ImGui::PopStyleColor(2);
            ImGui::Spacing();

            int removeEvt = -1;
            const ImVec4 evtCol = ImVec4(1.f, 0.5f, 0.6f, 1);
            ImDrawList* dlEvt = ImGui::GetWindowDrawList();
            for (int i = 0; i < (int)evts.size(); i++)
            {
                auto& e = evts[i];
                ImGui::PushID(100 + i);

                float cardWidth = ImGui::GetContentRegionAvail().x;
                float cardHeight = 28.f;
                ImVec2 cardMin = ImGui::GetCursorScreenPos();
                ImVec2 cardMax = ImVec2(cardMin.x + cardWidth, cardMin.y + cardHeight);
                dlEvt->AddRectFilled(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.04f)), 6.0f);

                ImGui::Dummy(ImVec2(8, cardHeight));
                ImGui::SameLine(0, 0);
                ImGui::BeginGroup();
                ImGui::Dummy(ImVec2(0, (cardHeight - 18.f) * 0.5f));

                // Badge "D" (Dispatcher) — pill colorida, mesmo padrão das vars
                ImVec2 badgeStart = ImGui::GetCursorScreenPos();
                ImVec2 badgeSz = ImVec2(22.f, 18.f);
                dlEvt->AddRectFilled(badgeStart, ImVec2(badgeStart.x + badgeSz.x, badgeStart.y + badgeSz.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(evtCol.x, evtCol.y, evtCol.z, 0.22f)), 9.0f);
                dlEvt->AddRect(badgeStart, ImVec2(badgeStart.x + badgeSz.x, badgeStart.y + badgeSz.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(evtCol.x, evtCol.y, evtCol.z, 0.9f)), 9.0f, 0, 1.2f);
                dlEvt->AddText(ImVec2(badgeStart.x + 6.f, badgeStart.y + 1.f),
                    ImGui::ColorConvertFloat4ToU32(evtCol), "D");
                ImGui::Dummy(badgeSz);
                ImGui::SameLine(0, 8);

                ImGui::TextUnformatted(e.Name.c_str());

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    std::string pl = "Dispatch:" + e.Name;
                    ImGui::SetDragDropPayload("EVT_NODE", pl.c_str(), pl.size() + 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, evtCol);
                    ImGui::Text("Dispatch %s", e.Name.c_str());
                    ImGui::PopStyleColor();
                    ImGui::EndDragDropSource();
                }

                ImGui::EndGroup();
                ImGui::SameLine();

                ImGui::SetCursorScreenPos(ImVec2(cardMax.x - 26.f, cardMin.y + (cardHeight - 20.f) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                if (ImGui::SmallButton("x")) removeEvt = i;
                ImGui::PopStyleColor(3);

                ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y + 3.f));

                ImGui::PopID();
            }
            if (removeEvt >= 0)
            {
                PushUndo("Remove Event");
                m_ScriptAsset->RemoveCustomEvent(removeEvt);
                CommitUndo("Remove Event");
            }
            ImGui::Spacing();
        }

        // ── FUNCTIONS (estilo Function da Unreal) ──────────────────────────────
        auto& funcs = m_ScriptAsset->GetFunctions();
        const ImVec4 funcCol = ImVec4(0.35f, 0.78f, 0.71f, 1); // verde-azulado, bate com o header do node no canvas

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.42f, 0.4f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.52f, 0.5f, 1));
        bool funcOpen = ImGui::CollapsingHeader("Functions", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (funcOpen)
        {
            ImGui::SetNextItemWidth(avail * 0.62f);
            ImGui::InputText("##funcname", m_NewFuncName, sizeof(m_NewFuncName));
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.38f, 0.35f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.5f, 0.46f, 1));
            if (ImGui::SmallButton("+ Func"))
            {
                std::string name = (m_NewFuncName[0] != 0) ? m_NewFuncName : "NewFunction";
                PushUndo("Add Function");
                m_ScriptAsset->AddFunction(name);
                CommitUndo("Add Function");
                m_NewFuncName[0] = 0;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Criar nova Function (estilo Unreal — com Inputs/Outputs próprios)");
            ImGui::PopStyleColor(2);
            ImGui::Spacing();

            int removeFunc = -1;
            ImDrawList* dlFn = ImGui::GetWindowDrawList();

            for (int i = 0; i < (int)funcs.size(); i++)
            {
                auto& fn = funcs[i];
                ImGui::PushID(200 + i);

                bool isExpanded = (m_ExpandedFunc == fn.Name);
                bool isEditing = (m_EditingFunctionIndex == i);

                // ── Card da função (mesmo padrão visual do Event Dispatcher) ──
                float cardWidth = ImGui::GetContentRegionAvail().x;
                float cardHeight = 28.f;
                ImVec2 cardMin = ImGui::GetCursorScreenPos();
                ImVec2 cardMax = ImVec2(cardMin.x + cardWidth, cardMin.y + cardHeight);
                ImVec4 bgTint = isEditing ? ImVec4(funcCol.x, funcCol.y, funcCol.z, 0.16f) : ImVec4(1, 1, 1, 0.04f);
                dlFn->AddRectFilled(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(bgTint), 6.0f);
                if (isEditing)
                    dlFn->AddRect(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(funcCol), 6.0f, 0, 1.2f);

                // BUGFIX: antes, o drag-source ficava amarrado só ao texto
                // pequeno do subtítulo "(N in, M out)" (último item antes do
                // BeginDragDropSource) — uma área minúscula. Depois, um botão
                // invisível separado (desenhado por cima, cobrindo o resto da
                // linha) capturava o clique pra abrir o grafo, mas também
                // "tampava" boa parte da área de arrastar. Resultado: dava pra
                // arrastar só de um pontinho específico, quase impossível de
                // acertar — por isso "não consigo arrastar".
                // Agora: UM botão invisível cobre a linha inteira, é a base
                // tanto do clique (abre o grafo) quanto do drag (cria o node
                // Call no canvas) — desenhado primeiro, com o conteúdo visual
                // (badge/nome/subtítulo) sobreposto depois (Dummy/Text não
                // competem por input, então não tampam o botão).
                float interactiveWidth = std::max(cardWidth - 56.f, 1.f);
                bool rowClicked = ImGui::InvisibleButton("##fnrow", ImVec2(interactiveWidth, cardHeight));
                bool rowDragged = false;
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    rowDragged = true;
                    ImGui::SetDragDropPayload("FUNC_NODE", fn.Name.c_str(), fn.Name.size() + 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, funcCol);
                    ImGui::Text("Call %s", fn.Name.c_str());
                    ImGui::PopStyleColor();
                    ImGui::EndDragDropSource();
                }
                if (rowClicked && !rowDragged)
                    SwitchToFunctionGraph(&fn);
                if (!rowDragged && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Clique para editar — arraste para o canvas para criar um node Call");

                // ── Conteúdo visual, desenhado por cima do botão invisível ─────
                ImGui::SetCursorScreenPos(cardMin);
                ImGui::Dummy(ImVec2(8, cardHeight));
                ImGui::SameLine(0, 0);
                ImGui::BeginGroup();
                ImGui::Dummy(ImVec2(0, (cardHeight - 18.f) * 0.5f));

                // Badge "F" — mesma convenção visual do "D" de Event Dispatcher
                ImVec2 badgeStart = ImGui::GetCursorScreenPos();
                ImVec2 badgeSz = ImVec2(22.f, 18.f);
                dlFn->AddRectFilled(badgeStart, ImVec2(badgeStart.x + badgeSz.x, badgeStart.y + badgeSz.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(funcCol.x, funcCol.y, funcCol.z, 0.22f)), 9.0f);
                dlFn->AddRect(badgeStart, ImVec2(badgeStart.x + badgeSz.x, badgeStart.y + badgeSz.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(funcCol.x, funcCol.y, funcCol.z, 0.9f)), 9.0f, 0, 1.2f);
                dlFn->AddText(ImVec2(badgeStart.x + 6.f, badgeStart.y + 1.f),
                    ImGui::ColorConvertFloat4ToU32(funcCol), "F");
                ImGui::Dummy(badgeSz);
                ImGui::SameLine(0, 8);

                std::string subtitle = "(" + std::to_string(fn.Inputs.size()) + " in, " +
                    std::to_string(fn.Outputs.size()) + " out)";
                ImGui::TextUnformatted(fn.Name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s", subtitle.c_str());
                ImGui::EndGroup();

                ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y));

                // Botão expandir/recolher editor de parâmetros
                ImGui::SetCursorScreenPos(ImVec2(cardMax.x - 52.f, cardMin.y + (cardHeight - 20.f) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(funcCol.x, funcCol.y, funcCol.z, 0.3f));
                if (ImGui::SmallButton(isExpanded ? "v" : ">"))
                    m_ExpandedFunc = isExpanded ? "" : fn.Name;
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Editar parametros (Inputs/Outputs)");

                ImGui::SetCursorScreenPos(ImVec2(cardMax.x - 26.f, cardMin.y + (cardHeight - 20.f) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                if (ImGui::SmallButton("x")) removeFunc = i;
                ImGui::PopStyleColor(3);

                ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y + 3.f));

                // ── Editor de parâmetros (Inputs/Outputs) — accordion ──────────
                if (isExpanded)
                {
                    ImGui::Indent(20.f);
                    bool sigChanged = false;

                    auto drawParamList = [&](const char* label, std::vector<ScriptFunctionParam>& params)
                        {
                            ImGui::TextDisabled("%s", label);
                            for (int p = 0; p < (int)params.size(); p++)
                            {
                                ImGui::PushID(p);
                                char nameBuf[64];
                                strncpy(nameBuf, params[p].Name.c_str(), 63); nameBuf[63] = 0;
                                ImGui::SetNextItemWidth(avail * 0.32f);
                                if (ImGui::InputText("##pname", nameBuf, sizeof(nameBuf)))
                                {
                                    params[p].Name = nameBuf;
                                    sigChanged = true;
                                }
                                ImGui::SameLine(0, 4);
                                int curType = (int)params[p].Type;
                                ImGui::SetNextItemWidth(avail * 0.32f);
                                if (ImGui::Combo("##ptype", &curType, s_VarTypes, 18))
                                {
                                    params[p].Type = (ScriptVarType)curType;
                                    sigChanged = true;
                                }
                                ImGui::SameLine(0, 4);
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                                if (ImGui::SmallButton("x"))
                                {
                                    params.erase(params.begin() + p);
                                    sigChanged = true;
                                    ImGui::PopStyleColor();
                                    ImGui::PopID();
                                    break; // índices mudaram — recomeça no próximo frame
                                }
                                ImGui::PopStyleColor();
                                ImGui::PopID();
                            }
                            if (ImGui::SmallButton((std::string("+ ") + label).c_str()))
                            {
                                params.push_back({ "Param", ScriptVarType::Float });
                                sigChanged = true;
                            }
                        };

                    drawParamList("Inputs", fn.Inputs);
                    ImGui::Spacing();
                    drawParamList("Outputs", fn.Outputs);

                    if (sigChanged)
                    {
                        PushUndo("Edit Function Signature");
                        RebuildFunctionCallSites(fn);
                        CommitUndo("Edit Function Signature");
                    }

                    ImGui::Unindent(20.f);
                    ImGui::Spacing();
                }

                ImGui::PopID();
            }
            if (removeFunc >= 0)
            {
                // Se a função removida é a que está aberta no canvas, volta
                // pro grafo principal primeiro — senão m_Graph ficaria
                // apontando pra um ScriptGraph que está prestes a ser destruído.
                if (m_EditingFunctionIndex == removeFunc)
                    SwitchToMainGraph();
                else if (m_EditingFunctionIndex > removeFunc)
                    // A função sendo editada está DEPOIS da removida na lista —
                    // erase desloca todo mundo depois dela um índice pra baixo.
                    // m_Graph continua válido (é um ponteiro pro ScriptGraph em
                    // si, que não se move), mas o índice precisa acompanhar o
                    // deslocamento ou passaria a apontar pra função errada na
                    // lista (destaque visual errado, mesmo com o canvas certo).
                    m_EditingFunctionIndex--;
                if (m_ExpandedFunc == funcs[removeFunc].Name)
                    m_ExpandedFunc.clear();
                PushUndo("Remove Function");
                m_ScriptAsset->RemoveFunction(removeFunc);
                CommitUndo("Remove Function");
            }
            ImGui::Spacing();
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace axe