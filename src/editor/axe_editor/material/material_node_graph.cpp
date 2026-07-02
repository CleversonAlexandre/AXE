// material_node_graph.cpp
// Toolbar e lógica do grafo de nodes: criação/validação de links, menus de
// contexto (criar/excluir node, criar comment), atalhos de teclado e
// undo/redo de deleção de node (DeleteNodeWithHistory).

#include "material_editor_window.hpp"
#include "editor/axe_editor/editor_icon_library.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"
#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>
#include <functional>
#include <algorithm>
#include <cctype>

namespace ed = ax::NodeEditor;

namespace axe
{
    // ── Tabela estática de nodes e categorias do menu "Create New Node" ──
    // Mesmo padrão visual/estrutural usado no Script Editor: cada entrada
    // é {label exibido, nome interno do node}; AddNodeByName() resolve o
    // nome interno pra fábrica certa.
    struct MatNE { const char* label; const char* type; };

    static const MatNE s_MatConstants[] = {
        {"Float","Float"}, {"Vec2","Vec2"}, {"Vec3","Vec3"}, {"Color","Color"},
    };
    static const MatNE s_MatTexture[] = {
        {"Texture Sample","Texture Sample"}, {"UV Coordinate","UV Coordinate"},
        {"Texture Coordinate","Texture Coordinate"},
    };
    static const MatNE s_MatMath[] = {
        {"Add","Add"}, {"Subtract","Subtract"}, {"Multiply","Multiply"}, {"Divide","Divide"},
        {"Power","Power"}, {"Clamp","Clamp"}, {"Abs","Abs"}, {"OneMinus","OneMinus"},
        {"Min","Min"}, {"Max","Max"}, {"Saturate","Saturate"}, {"Sine","Sine"},
        {"Cosine","Cosine"}, {"Step","Step"}, {"SmoothStep","SmoothStep"}, {"Lerp","Lerp"},
        {"If","If"},
    };
    static const MatNE s_MatVector[] = {
        {"Normalize","Normalize"}, {"Distance","Distance"}, {"Dot Product","DotProduct"},
        {"Cross Product","CrossProduct"}, {"Length","Length"}, {"Append","Append"},
        {"Vector Split","Vector Split"},
    };
    static const MatNE s_MatUtility[] = {
        {"World Position","World Position"}, {"Fresnel","Fresnel"}, {"Normal Map","Normal Map"},
        {"Camera Vector","Camera Vector"}, {"Reflection Vector","Reflection Vector"},
        {"Desaturate","Desaturate"}, {"Noise","Noise"},
    };
    static const MatNE s_MatAnimation[] = {
        {"Time","Time"}, {"Panner","Panner"},
    };
    static const MatNE s_MatParticle[] = {
        {"Particle Age",   "Particle Age"},
        {"Particle Color", "Particle Color"},
    };

    struct MatCatDef { const char* name; const MatNE* e; int n; ImVec4 col; };
    static const MatCatDef s_MatCats[] = {
        {"Constants", s_MatConstants, 4, {0.55f, 0.55f, 0.95f, 1}},
        {"Texture",   s_MatTexture,   3, {0.9f,  0.55f, 0.2f,  1}},
        {"Math",      s_MatMath,      17, {0.4f, 0.65f, 1.0f,  1}},
        {"Vector",    s_MatVector,    7, {0.3f,  0.85f, 0.55f, 1}},
        {"Utility",   s_MatUtility,   7, {0.85f, 0.3f,  0.75f, 1}},
        {"Animation", s_MatAnimation, 2, {0.95f, 0.35f, 0.6f,  1}},
        {"Particle",  s_MatParticle,  2, {0.1f,  0.75f, 0.55f, 1}},
    };


    void MaterialEditorWindow::DrawNodeGraphWindow()
    {
        if (ImGui::Begin("Material Graph"))
        {
            // Toolbar
            auto& icons = EditorIconLibrary::Get();
            float btnSize = 24.0f;

            // Undo
            bool canUndo = m_History.CanUndo();
            if (!canUndo) ImGui::BeginDisabled();
            if (icons.GetUndo())
            {
                if (ImGui::ImageButton("##undo",
                    (ImTextureID)(uintptr_t)icons.GetUndo()->GetRendererID(),
                    ImVec2(btnSize, btnSize),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                ))
                    m_History.Undo();
            }
            if (!canUndo) ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Undo: %s", m_History.GetUndoName().c_str());

            ImGui::SameLine();

            // Redo
            bool canRedo = m_History.CanRedo();
            if (!canRedo) ImGui::BeginDisabled();
            if (icons.GetRedo())
            {
                if (ImGui::ImageButton("##redo",
                    (ImTextureID)(uintptr_t)icons.GetRedo()->GetRendererID(),
                    ImVec2(btnSize, btnSize),
                    ImVec2(0, 1),
                    ImVec2(1, 0)))
                    m_History.Redo();
            }
            if (!canRedo) ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Redo: %s", m_History.GetRedoName().c_str());

            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Salvar
            if (icons.GetSave())
            {
                if (ImGui::ImageButton("##save",
                    (ImTextureID)(uintptr_t)icons.GetSave()->GetRendererID(),
                    ImVec2(btnSize, btnSize),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                ))
                    SaveGraph();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Salvar grafo");

            ImGui::SameLine();

            // Compilar
            if (icons.GetCompile())
            {
                if (ImGui::ImageButton("##compile",
                    (ImTextureID)(uintptr_t)icons.GetCompile()->GetRendererID(),
                    ImVec2(btnSize, btnSize),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                ))
                {
                    // Chama o mesmo código do "Compile and Apply"
                    if (m_Material)
                        CompileAndApply();


                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Compilar e aplicar");

            ImGui::Separator();
            DrawNodeGraph();
        }
        ImGui::End();
    }


    void MaterialEditorWindow::DrawNodeGraph()
    {
        ed::SetCurrentEditor(m_NodeEditorContext);
        ed::Begin("MaterialGraph", ImVec2(0.0f, 0.0f));

        // Arrastar uma textura do Asset Browser pro canvas cria
        // automaticamente um node "Texture Sample" já com a textura
        // atribuída — igual ao Material Editor da Unreal. O Begin() do
        // node editor emite um Dummy cobrindo toda a área visível do
        // canvas como último item, então o drag-drop target abaixo
        // funciona em qualquer lugar do fundo (sem interferir nos nodes,
        // que têm seus próprios itens desenhados por cima depois).
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
            {
                std::string uuid = (const char*)payload->Data;
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                if (record && record->Type == AssetType::Texture)
                {
                    ImVec2 dropScreenPos = ImGui::GetMousePos();
                    Node* node = m_Graph->AddTextureSampleNode();
                    if (node)
                    {
                        node->Value.TextureVal = Texture2D::Create(record->FilePath.string());
                        node->Value.TextureUUID = record->UUID;
                        m_Graph->BuildNodes();
                        ed::SetNodePosition(node->ID, dropScreenPos);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        for (auto& [id, pos] : m_Graph->GetPendingPositions())
            ed::SetNodePosition(ed::NodeId(id), pos);
        m_Graph->ClearPendingPositions();

        for (auto& node : m_Graph->GetNodes())
            m_Graph->UpdateNodePosition(node->ID.Get(), ed::GetNodePosition(node->ID));

        for (auto& node : m_Graph->GetNodes())
            DrawNode(*node);

        for (auto& link : m_Graph->GetLinks())
            ed::Link(link.ID, link.StartPin, link.EndPin);

        // ── Duplo clique num fio insere um Reroute ─────────────────────────
        // Estilo Unreal / igual ao Script Editor: o fio se parte em
        // origem -> Reroute -> destino. Posição via m_PendingPositions (é
        // aplicada no topo do Draw, dentro do contexto do canvas) — assim
        // funciona tanto agora quanto no redo (que roda fora do contexto).
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
                    PinType linkType = PinType::Any;
                    if (auto* pStart = m_Graph->FindPin(startId)) linkType = pStart->Type;
                    ImVec2 pos = ImGui::GetMousePos();
                    auto rerouteId = std::make_shared<int>(0);

                    m_History.Push({
                        "Insert Reroute",
                        [this, startId, endId, linkType, pos, rerouteId]()
                        {
                            // Remove o fio direto, se ainda existir
                            for (auto& l : m_Graph->GetLinks())
                                if (l.StartPin == startId && l.EndPin == endId)
                                {
 m_Graph->RemoveLink(l.ID); break;
}

auto* r = m_Graph->AddNodeByName("Reroute");
if (r)
{
    // Fixa o tipo no tipo do fio (cor do wire certa)
    r->Inputs[0].Type = linkType;
    r->Outputs[0].Type = linkType;
    *rerouteId = (int)r->ID.Get();
    m_Graph->m_PendingPositions[(int)r->ID.Get()] = pos;
    m_Graph->AddLink(startId, r->Inputs[0].ID);
    m_Graph->AddLink(r->Outputs[0].ID, endId);
}
},
[this, startId, endId, rerouteId]()
{
                            // DeleteNode remove o Reroute E seus 2 links juntos
                            if (*rerouteId != 0)
                            {
                                m_Graph->DeleteNode(ed::NodeId(*rerouteId));
                                *rerouteId = 0;
                            }
                            m_Graph->AddLink(startId, endId); // restaura o fio direto
                        }
                        });
                }
            }
        }

        if (m_FrameCount > 2)
        {
            // --- Criação de links ---
            if (ed::BeginCreate())
            {
                Pin* newLinkPin = nullptr;

                auto showLabel = [](const char* label, ImColor color)
                    {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
                        auto size = ImGui::CalcTextSize(label);
                        auto padding = ImGui::GetStyle().FramePadding;
                        auto spacing = ImGui::GetStyle().ItemSpacing;
                        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));
                        auto rectMin = ImGui::GetCursorScreenPos() - padding;
                        auto rectMax = ImGui::GetCursorScreenPos() + size + padding;
                        ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                        ImGui::TextUnformatted(label);
                    };

                ed::PinId startPinId = 0, endPinId = 0;
                if (ed::QueryNewLink(&startPinId, &endPinId))
                {
                    auto startPin = m_Graph->FindPin(startPinId);
                    auto endPin = m_Graph->FindPin(endPinId);
                    newLinkPin = startPin ? startPin : endPin;

                    if (startPin && startPin->Kind == ed::PinKind::Input)
                    {
                        std::swap(startPin, endPin);
                        std::swap(startPinId, endPinId);
                    }

                    if (startPin && endPin)
                    {
                        if (endPin == startPin)
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        else if (endPin->Kind == startPin->Kind)
                        {
                            showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else if (endPin->Type != startPin->Type &&
                            endPin->Type != PinType::Any &&
                            startPin->Type != PinType::Any &&
                            endPin->Type != PinType::Vec3 && // Material Output aceita qualquer vec
                            startPin->Type != PinType::Vec3)
                        {
                            showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 128, 128), 2.0f);
                        }
                        else
                        {
                            showLabel("+ Create Link", ImColor(32, 45, 32, 100));
                            if (ed::AcceptNewItem(ImColor(125, 255, 128), 1.0f))
                            {
                                // Procura se o input já tem uma conexão
                                Link oldLink(0, 0, 0);
                                bool hasOldLink = false;
                                for (auto& link : m_Graph->GetLinks())
                                {
                                    if (link.EndPin == endPinId)
                                    {
                                        oldLink = link;
                                        hasOldLink = true;
                                        break;
                                    }

                                    AXE_CORE_INFO("Links Criados: '{}'", link.ID.Get());
                                }

                                if (hasOldLink)
                                {
                                    // Substitui sem registrar no histórico
                                    m_Graph->RemoveLink(oldLink.ID);
                                    m_Graph->AddLink(startPinId, endPinId);
                                }
                                else
                                {
                                    // Nova conexão — registra no histórico normalmente
                                    m_History.Push({
                                        "Add Link",
                                        [this, startPinId, endPinId]() { m_Graph->AddLink(startPinId, endPinId); },
                                        [this]() {
                                            if (!m_Graph->GetLinks().empty())
                                                m_Graph->RemoveLink(m_Graph->GetLinks().back().ID);
                                        }
                                        });
                                }
                            }
                        }
                    }
                }

                ed::EndCreate();
            }

            // --- Deleção de links ---
            if (ed::BeginDelete())
            {
                ed::LinkId deletedLink;
                //while (ed::QueryDeletedLink(&deletedLink))
                //    if (ed::AcceptDeletedItem())
                //        m_Graph->RemoveLink(deletedLink);

                while (ed::QueryDeletedLink(&deletedLink))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        auto linkId = deletedLink;
                        // Salva o link antes de deletar para poder restaurar
                        Link savedLink;
                        for (auto& l : m_Graph->GetLinks())
                            if (l.ID == linkId) { savedLink = l; break; }

                        m_History.Push({
                          "Remove Link",
                          [this, savedLink]() {
                                // Remove pelo EndPin — funciona mesmo que o ID tenha mudado
                                for (auto& l : m_Graph->GetLinks())
                                {
                                    if (l.EndPin == savedLink.EndPin && l.StartPin == savedLink.StartPin)
                                    {
                                        m_Graph->RemoveLink(l.ID);
                                        break;
                                    }
                                }
                            },
                            [this, savedLink]() {
                                m_Graph->AddLink(savedLink.StartPin, savedLink.EndPin);
                            }
                            });
                    }
                }

                ed::EndDelete();
            }

            // --- Context menus ---
            auto openPopupPosition = ImGui::GetMousePos();

            ed::Suspend();
            if (ed::ShowNodeContextMenu(&m_Graph->contextNodeId))
                ImGui::OpenPopup("Node Context Menu");
            else if (ed::ShowLinkContextMenu(&m_Graph->contextLinkId))
                ImGui::OpenPopup("Link Context Menu");
            else if (ed::ShowBackgroundContextMenu())
            {
                ImGui::OpenPopup("Create New Node");
                newNodeLinkPin = nullptr;
            }
            ed::Resume();

            ed::Suspend();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

            if (ImGui::BeginPopup("Node Context Menu"))
            {
                auto nodePtr = m_Graph->FindNode(m_Graph->contextNodeId);
                auto node = nodePtr ? nodePtr->get() : nullptr;

                ImGui::TextUnformatted("Node Context Menu");
                ImGui::Separator();
                if (node)
                {
                    ImGui::Text("ID: %p", node->ID.AsPointer());
                    ImGui::Text("Inputs: %d", (int)node->Inputs.size());
                    ImGui::Text("Outputs: %d", (int)node->Outputs.size());

                    if (ImGui::MenuItem("Delete"))
                    {
                        auto nodeId = m_Graph->contextNodeId;
                        DeleteNodeWithHistory(nodeId);
                        // ed::DeleteNode(nodeId);
                    }
                }
                else
                {
                    if (ImGui::MenuItem("Delete"))
                        ed::DeleteNode(m_Graph->contextNodeId);
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("Create New Node"))
            {
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::InputTextWithHint("##matsearch", "Search node...",
                    m_NodeSearchBuf, sizeof(m_NodeSearchBuf));
                ImGui::Separator();

                std::string s = m_NodeSearchBuf;
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                bool filtering = !s.empty();

                Node* node = nullptr;

                auto spawnNode = [&](const char* type)
                    {
                        node = m_Graph->AddNodeByName(type);
                        m_NodeSearchBuf[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    };

                // "Material Output" e "Comment" ficam em destaque no topo,
                // fora das categorias — não são nodes de cálculo comuns
                // (mesmo tratamento dado a "+ Add Comment" no Script Editor).
                if (!filtering || std::string("material output").find(s) != std::string::npos)
                    if (ImGui::MenuItem("+ Material Output")) spawnNode("Material Output");
                if (!filtering || std::string("comment").find(s) != std::string::npos)
                    if (ImGui::MenuItem("+ Add Comment")) spawnNode("Comment");
                if (!filtering || std::string("reroute").find(s) != std::string::npos)
                    if (ImGui::MenuItem("+ Add Reroute")) spawnNode("Reroute");
                if (!filtering)
                    ImGui::Separator();

                for (int ci = 0; ci < 7; ci++)
                {
                    auto& cat = s_MatCats[ci];
                    ImVec4 col = cat.col;

                    bool anyMatch = false;
                    if (filtering)
                        for (int i = 0; i < cat.n; i++)
                        {
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
                        m_NodeCatOpen[ci] ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                    if (!filtering) m_NodeCatOpen[ci] = show;
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

                if (node)
                {
                    m_Graph->BuildNodes();
                    ed::SetNodePosition(node->ID, openPopupPosition);

                    if (auto startPin = newNodeLinkPin)
                    {
                        auto& pins = startPin->Kind == ed::PinKind::Input
                            ? node->Outputs : node->Inputs;
                        for (auto& pin : pins)
                        {
                            if (CanCreateLink(startPin, &pin))
                            {
                                auto endPin = &pin;
                                if (startPin->Kind == ed::PinKind::Input)
                                    std::swap(startPin, endPin);
                                m_Graph->m_Links.emplace_back(
                                    m_Graph->GetNextID(), startPin->ID, endPin->ID);
                                m_Graph->m_Links.back().Color = GetIconColor(startPin->Type);
                            }
                        }
                    }
                }
                ImGui::EndPopup();
            }

            // Tecla C — cria comment agrupando seleção
            if (ImGui::IsKeyPressed(ImGuiKey_C) && !ImGui::GetIO().WantTextInput)
            {
                int selectedCount = ed::GetSelectedObjectCount();
                if (selectedCount > 0)
                {
                    std::vector<ed::NodeId> selectedNodes(selectedCount);
                    ed::GetSelectedNodes(selectedNodes.data(), selectedCount);

                    if (!selectedNodes.empty())
                    {
                        ImVec2 minPos(FLT_MAX, FLT_MAX), maxPos(-FLT_MAX, -FLT_MAX);
                        for (auto nodeId : selectedNodes)
                        {
                            ImVec2 pos = ed::GetNodePosition(nodeId);
                            //ImVec2 pos = m_Graph->GetNodePosition(nodeId.Get()); // ← nodeId.Get()
                            ImVec2 size = ed::GetNodeSize(nodeId);
                            minPos.x = std::min(minPos.x, pos.x);
                            minPos.y = std::min(minPos.y, pos.y);
                            maxPos.x = std::max(maxPos.x, pos.x + size.x);
                            maxPos.y = std::max(maxPos.y, pos.y + size.y);
                        }
                        const float margin = 32.0f;
                        minPos.x -= margin; minPos.y -= margin;
                        maxPos.x += margin; maxPos.y += margin;

                        Node* comment = m_Graph->AddComment();
                        comment->Size = ImVec2(maxPos.x - minPos.x, maxPos.y - minPos.y);
                        ed::SetNodePosition(comment->ID, minPos);
                        UpdateCommentChildren(comment);
                        ed::ClearSelection();
                        ed::SelectNode(comment->ID, false);
                    }
                }
            }

            ImGui::PopStyleVar();
            ed::Resume();
        }

        ed::Suspend();
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput)
        {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_History.Undo();

            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_History.Redo();

            // Tecla Delete — deleta nodes selecionados
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !io.WantTextInput)
            {
                int selectedCount = ed::GetSelectedObjectCount();
                if (selectedCount > 0)
                {
                    std::vector<ed::NodeId> selectedNodes(selectedCount);
                    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), selectedCount);

                    for (int i = 0; i < nodeCount; i++)
                    {
                        DeleteNodeWithHistory(selectedNodes[i]);
                    }
                }
            }
        }


        ed::Resume();

        m_FrameCount++;
        ed::End();
        ed::SetCurrentEditor(nullptr);
    }

    // -------------------------------------------------------------------------
    // Undo/redo de deleção de node
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::DeleteNodeWithHistory(ed::NodeId nodeId)
    {
        auto nodePtr = m_Graph->FindNode(nodeId);
        if (!nodePtr) return;
        auto* node = nodePtr->get();

        // Salva o estado completo do node antes de deletar
        std::string nodeName = node->Name;
        ImVec2      nodePos = m_Graph->GetNodePosition(nodeId.Get());

        // Salva os valores do node
        float       floatVal = node->Value.FloatVal;
        glm::vec2   vec2Val = node->Value.Vec2Val;
        glm::vec3   vec3Val = node->Value.Vec3Val;
        glm::vec4   vec4Val = { node->Value.Vec4Val.x, node->Value.Vec4Val.y,
                                  node->Value.Vec4Val.z, node->Value.Vec4Val.w };
        std::string textureUUID = node->Value.TextureUUID;
        auto        textureVal = node->Value.TextureVal;

        // Salva os links conectados a este node
        std::vector<Link> connectedLinks;
        for (auto& link : m_Graph->GetLinks())
        {
            for (auto& pin : node->Inputs)
                if (link.EndPin == pin.ID || link.StartPin == pin.ID)
                {
                    connectedLinks.push_back(link); break;
                }
            for (auto& pin : node->Outputs)
                if (link.EndPin == pin.ID || link.StartPin == pin.ID)
                {
                    connectedLinks.push_back(link); break;
                }
        }

        m_History.Push({
            "Delete Node: " + nodeName,

            // Execute — deleta o node
            [this, nodeId]()
            {
                m_Graph->DeleteNode(nodeId);
                ed::DeleteNode(nodeId);
            },

            // Undo — recria o node com todos os dados
            [this, nodeName, nodePos, floatVal, vec2Val, vec3Val, vec4Val, textureUUID, textureVal, connectedLinks]()
            {
                // Recria o node pelo nome — dispatch centralizado em
                // MaterialGraph::AddNodeByName(), cobre automaticamente
                // qualquer node novo adicionado no futuro, sem precisar
                // lembrar de atualizar este undo também.
                Node* newNode = m_Graph->AddNodeByName(nodeName);

                if (!newNode) return;

                // Restaura posição
                m_Graph->m_PendingPositions[newNode->ID.Get()] = nodePos;

                // Restaura valores
                newNode->Value.FloatVal = floatVal;
                newNode->Value.Vec2Val = vec2Val;
                newNode->Value.Vec3Val = vec3Val;
                newNode->Value.Vec4Val = { vec4Val.x, vec4Val.y, vec4Val.z, vec4Val.w };
                newNode->Value.TextureUUID = textureUUID;
                newNode->Value.TextureVal = textureVal;

                // Nota: os links não podem ser restaurados porque os IDs dos pins
                // mudam ao recriar o node. O usuário precisará reconectar.
                // (Uma solução completa exigiria salvar o mapeamento de pins)

                m_Graph->BuildNodes();
            }
            });
    }


} // namespace axe