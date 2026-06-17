// script_members.cpp
// DrawMyBlueprintWindow — painel "Script Members":
// Variables agrupadas por categoria (colapso + drag and drop entre categorias),
// Override Events, Event Dispatchers.

#include "script_graph_window.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_graph.hpp"
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

        static const char* s_VarTypes[] = { "Float","Bool","Int","Vec3","String","Vec2","Vec4","Quat","Entity" };
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
            ImGui::SetNextItemWidth(avail * 0.42f);
            ImGui::InputText("##vname", m_NewVarName, sizeof(m_NewVarName));
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(avail * 0.30f);
            ImGui::Combo("##vtype", &m_NewVarType, s_VarTypes, 9);
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1));
            if (ImGui::SmallButton("+ Var"))
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
            ImGui::PopStyleColor();
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
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("VAR_RECAT"))
                                vars[*(int*)p->Data].Category = cat;
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

                    // Drop target no header da categoria
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("VAR_RECAT"))
                            vars[*(int*)p->Data].Category = cat;
                        ImGui::EndDragDropTarget();
                    }

                    if (!catOpen) continue;
                    ImGui::Indent(8.f);
                }
                else
                {
                    // Área "sem categoria" — drop target invisível no topo
                    ImGui::Dummy(ImVec2(avail, 2.f));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("VAR_RECAT"))
                            vars[*(int*)p->Data].Category = "";
                        ImGui::EndDragDropTarget();
                    }
                }

                // ── Variáveis desta categoria ─────────────────────────────────
                for (int i = 0; i < (int)vars.size(); i++)
                {
                    auto& v = vars[i];
                    if (v.Category != cat) continue;

                    ImGui::PushID(i);

                    // Botão X
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                    if (ImGui::SmallButton("x")) removeVar = i;
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine(0, 4);

                    // Badge de tipo
                    ImGui::PushStyleColor(ImGuiCol_Text, s_VarCols[(int)v.Type]);
                    ImGui::TextUnformatted(s_VarTypes[(int)v.Type]);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 6);

                    bool sel = (m_SelectedVar == i);
                    bool wantRename = (m_RenamingVar == i);

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
                        if (ImGui::Selectable(v.Name.c_str(), sel,
                            ImGuiSelectableFlags_AllowDoubleClick, ImVec2(avail * 0.45f, 0)))
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
                        if (sel && ImGui::IsKeyPressed(ImGuiKey_F2))
                        {
                            m_RenamingVar = i; m_RenameJustStarted = true;
                            strncpy(m_RenameBuf, v.Name.c_str(), sizeof(m_RenameBuf) - 1);
                            m_RenameBuf[sizeof(m_RenameBuf) - 1] = 0;
                        }
                    }

                    // ── Drag and drop para recategorizar ─────────────────────
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        // Payload duplo: VAR_NODE para criar node, VAR_RECAT para mover de categoria
                        ImGui::SetDragDropPayload("VAR_RECAT", &i, sizeof(int));
                        ImGui::PushStyleColor(ImGuiCol_Text, s_VarCols[(int)v.Type]);
                        ImGui::Text("%s  [arrastar para categoria]", v.Name.c_str());
                        ImGui::PopStyleColor();
                        ImGui::EndDragDropSource();
                    }
                    // Também suporta drag para o graph (VAR_NODE)
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        std::string pl = "GetVar:" + v.Name;
                        ImGui::SetDragDropPayload("VAR_NODE", pl.c_str(), pl.size() + 1);
                        ImGui::PushStyleColor(ImGuiCol_Text, s_VarCols[(int)v.Type]);
                        ImGui::Text("Get %s", v.Name.c_str());
                        ImGui::PopStyleColor();
                        ImGui::EndDragDropSource();
                    }

                    // Expansão quando selecionado — só Tipo, categoria fica na aba Node
                    if (sel)
                    {
                        ImGui::Indent(16);
                        int typeIdx = (int)v.Type;
                        ImGui::SetNextItemWidth(avail * 0.5f);
                        if (ImGui::Combo("Type##vtype", &typeIdx, s_VarTypes, 9))
                            v.Type = (ScriptVarType)typeIdx;
                        ImGui::TextDisabled("Categoria: %s",
                            v.Category.empty() ? "(nenhuma)" : v.Category.c_str());
                        ImGui::Unindent(16);
                    }
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
            for (auto& ov : s_Overrides)
            {
                ImGui::PushID(ov.type);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.2f, 1));
                ImGui::Bullet(); ImGui::PopStyleColor();
                ImGui::SameLine();

                if (ImGui::Selectable(ov.label, false, ImGuiSelectableFlags_AllowDoubleClick))
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_Graph)
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
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.2f, 1));
                    ImGui::Text("Add %s", ov.label);
                    ImGui::PopStyleColor();
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
            }
        }

        // ── EVENT DISPATCHERS ─────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.2f, 0.3f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.3f, 0.4f, 1));
        bool evtOpen = ImGui::CollapsingHeader("Event Dispatchers", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (evtOpen)
        {
            ImGui::SetNextItemWidth(avail * 0.68f);
            ImGui::InputText("##evtname", m_NewEvtName, sizeof(m_NewEvtName));
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.25f, 1));
            if (ImGui::SmallButton("+ Event"))
            {
                ScriptCustomEvent e;
                e.Name = (m_NewEvtName[0] != 0) ? m_NewEvtName : "OnMyEvent";
                PushUndo("Add Event");
                m_ScriptAsset->AddCustomEvent(e);
                CommitUndo("Add Event");
                m_NewEvtName[0] = 0;
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();

            int removeEvt = -1;
            for (int i = 0; i < (int)evts.size(); i++)
            {
                auto& e = evts[i];
                ImGui::PushID(100 + i);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
                if (ImGui::SmallButton("x")) removeEvt = i;
                ImGui::PopStyleColor(3);
                ImGui::SameLine(0, 4);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.5f, 0.6f, 1));
                ImGui::TextUnformatted("[D]");
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 6);
                ImGui::TextUnformatted(e.Name.c_str());

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    std::string pl = "Dispatch:" + e.Name;
                    ImGui::SetDragDropPayload("EVT_NODE", pl.c_str(), pl.size() + 1);
                    ImGui::Text("Dispatch %s", e.Name.c_str());
                    ImGui::EndDragDropSource();
                }
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

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace axe