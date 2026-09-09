// material_node_graph.cpp
// Toolbar e lógica do grafo de nodes: criação/validação de links, menus de
// contexto (criar/excluir node, criar comment), atalhos de teclado e
// undo/redo de deleção de node (DeleteNodeWithHistory).

#include "material_editor_window.hpp"
#include "material_function.hpp"   // MATFUNC_V2 — drop de funcao no canvas

// MATERIAL_EDITOR_STYLE_V1 — widgets e icones compartilhados do editor.
// Ver a nota em editor_widgets.hpp sobre por que eles existem.
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"
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
        {"Fract","Fract"},                       // MATFUNC_V1
        // PRIMITIVES_V1 — builtins de GLSL, 1:1 com a linguagem.
        {"Floor","Floor"}, {"Ceil","Ceil"}, {"Round","Round"},
        {"Sqrt","Sqrt"}, {"Sign","Sign"}, {"Mod","Mod"},
    };
    static const MatNE s_MatVector[] = {
        {"Normalize","Normalize"}, {"Distance","Distance"}, {"Dot Product","DotProduct"},
        {"Cross Product","CrossProduct"}, {"Length","Length"}, {"Append","Append"},
        {"Vector Split","Vector Split"},
    };
    static const MatNE s_MatUtility[] = {
        {"World Position","World Position"}, {"Fresnel","Fresnel"}, {"Normal Map","Normal Map"},
        {"Camera Vector","Camera Vector"}, {"Reflection Vector","Reflection Vector"},
        {"Camera Position","Camera Position"},   // WATER_NODES_V1
        {"Desaturate","Desaturate"}, {"Noise","Noise"},
    };
    static const MatNE s_MatAnimation[] = {
        {"Time","Time"}, {"Panner","Panner"},
    };
    static const MatNE s_MatParticle[] = {
        {"Particle Age",   "Particle Age"},
        {"Particle Color", "Particle Color"},
    };

    // CUSTOM_NODE_V1 — categoria propria, e nao um item perdido em "Utility".
    // Este node nao e mais uma operacao: e a saida do grafo para tudo que a
    // lista acima nao cobre.
    static const MatNE s_MatCustom[] = {
        {"Custom (GLSL)", "Custom"},
    };

    // MATFUNC_V1 — categoria propria pelo mesmo motivo do Custom: nao sao mais
    // uma operacao, sao a fronteira do grafo. Function Input/Output so tem
    // efeito dentro de um `.axematfunc` e Material Function so fora dele, mas
    // os tres aparecem sempre — node que some conforme o contexto esconde do
    // autor que ele existe (a mesma decisao ja tomada nos nodes de Screen).
    static const MatNE s_MatFunction[] = {
        {"Material Function", "Material Function"},
        {"Function Input",    "Function Input"},
        {"Function Output",   "Function Output"},
    };

    // POSTPROCESS_DOMAIN_V1 — so tem efeito no dominio Post Process; fora dele
    // compilam para valor neutro (ver GenerateNodeCode). Ficam no menu de
    // qualquer jeito: node que some conforme o dominio esconde do usuario que
    // ele existe.
    static const MatNE s_MatScreen[] = {
        {"Scene Color",         "Scene Color"},
        {"Screen UV",           "Screen UV"},
        // POSTPROCESS_GBUFFER_V1 — a GEOMETRIA da cena, nao so a cor.
        {"Scene Depth",         "Scene Depth"},
        // WATER_NODES_V1 — o par do Scene Depth: a distancia ate ESTE pixel.
        {"Pixel Depth",         "Pixel Depth"},
        // do Scene Depth (aquele mede ao longo do raio de visao).
        {"Scene World Position","Scene World Position"},
        // SCENE_HEIGHT_V1 — a unica fonte independente de camera: vem da render
        // ortografica de topo, e nao do G-Buffer.
        {"Scene Height",        "Scene Height"},
        {"Scene Normal",        "Scene Normal"},
        {"Scene Shading Model", "Scene Shading Model"},
        // POSTPROCESS_SKY_V1 — o ceu nao esta no G-Buffer; estes tres sao o
        // que permite estiliza-lo no grafo.
        {"Scene Is Background",  "Scene Is Background"},
        {"Screen Ray Direction", "Screen Ray Direction"},
        {"Sun",                  "Sun"},
    };

    struct MatCatDef { const char* name; const MatNE* e; int n; ImVec4 col; };

    // As contagens agora sao IM_ARRAYSIZE. Eram numeros digitados ao lado de
    // cada array — e um numero MENOR que o array significa item que nunca
    // aparece no menu, sem erro nenhum para avisar. Ja aconteceu nesta engine,
    // no menu do Script Editor.
    static const MatCatDef s_MatCats[] = {
        {"Constants", s_MatConstants, IM_ARRAYSIZE(s_MatConstants), {0.55f, 0.55f, 0.95f, 1}},
        {"Texture",   s_MatTexture,   IM_ARRAYSIZE(s_MatTexture),   {0.9f,  0.55f, 0.2f,  1}},
        {"Math",      s_MatMath,      IM_ARRAYSIZE(s_MatMath),      {0.4f,  0.65f, 1.0f,  1}},
        {"Vector",    s_MatVector,    IM_ARRAYSIZE(s_MatVector),    {0.3f,  0.85f, 0.55f, 1}},
        {"Utility",   s_MatUtility,   IM_ARRAYSIZE(s_MatUtility),   {0.85f, 0.3f,  0.75f, 1}},
        {"Animation", s_MatAnimation, IM_ARRAYSIZE(s_MatAnimation), {0.95f, 0.35f, 0.6f,  1}},
        {"Particle",  s_MatParticle,  IM_ARRAYSIZE(s_MatParticle),  {0.1f,  0.75f, 0.55f, 1}},
        {"Custom",    s_MatCustom,    IM_ARRAYSIZE(s_MatCustom),    {0.75f, 0.45f, 0.15f, 1}},
        {"Function",  s_MatFunction,  IM_ARRAYSIZE(s_MatFunction),  {0.35f, 0.55f, 0.85f, 1}},  // MATFUNC_V1
        {"Screen",    s_MatScreen,    IM_ARRAYSIZE(s_MatScreen),    {0.2f,  0.6f,  0.8f,  1}},
    };


    // ═════════════════════════════════════════════════════════════════════════
    //  MATERIAL_EDITOR_STYLE_V1 — a barra do Material Graph
    //
    //  Era o ultimo lugar do editor que ainda desenhava ImageButton com PNG
    //  carregado do disco, enquanto Script, Control Rig, Anim Graph e Sequencer
    //  ja usam ui::IconButton com os glifos da Font Awesome subsetada.
    //
    //  A diferenca aparecia: os PNG nao acompanham o tamanho da fonte nem o DPI,
    //  nao herdam a cor de intencao (Accent) e ficam borrados em qualquer escala
    //  que nao seja a nativa. E, quando o icone nao carregava, o botao
    //  simplesmente NAO EXISTIA — repare que cada bloco antigo era um
    //  `if (icons.GetUndo())` em volta do botao inteiro.
    //
    //  Os tooltips continuam sendo os mesmos, e o IconButton os EXIGE.
    // ═════════════════════════════════════════════════════════════════════════
    void MaterialEditorWindow::DrawNodeGraphWindow()
    {
        if (ImGui::Begin("Material Graph"))
        {
            const bool canUndo = m_History.CanUndo();
            ImGui::BeginDisabled(!canUndo);
            if (ui::IconButton(ICON_UNDO,
                canUndo ? ("Desfazer: " + m_History.GetUndoName()).c_str() : "Nada a desfazer"))
                m_History.Undo();
            ImGui::EndDisabled();
            ImGui::SameLine();

            const bool canRedo = m_History.CanRedo();
            ImGui::BeginDisabled(!canRedo);
            if (ui::IconButton(ICON_REDO,
                canRedo ? ("Refazer: " + m_History.GetRedoName()).c_str() : "Nada a refazer"))
                m_History.Redo();
            ImGui::EndDisabled();

            ui::ToolbarSeparator();

            if (ui::IconButton(ICON_SAVE, "Salvar o grafo (.axegraph)"))
                SaveGraph();
            ImGui::SameLine();

            // Compilar e a ACAO PRINCIPAL desta janela — dai o Accent::Primary.
            // E o unico botao aqui que muda o que aparece na cena.
            if (ui::IconButton(ICON_BOLT,
                "Compilar e aplicar\n\nGera o GLSL a partir do grafo, aplica no\n"
                "material e regrava o .axeshader cozido.",
                ui::Accent::Primary))
            {
                if (m_Material)
                    CompileAndApply();
            }

            // O dominio do material fica na barra, e nao so escondido no painel
            // de parametros: ele muda COMPLETAMENTE o que o grafo significa, e
            // quem abre o material precisa ver isso sem procurar.
            if (m_Graph)
            {
                ui::ToolbarSeparator();

                const char* domainLabel =
                    m_Graph->Domain == MaterialDomain::LightFunction ? ICON_BOLT "  Light Function" :
                    m_Graph->Domain == MaterialDomain::Particle ? ICON_WAND "  Particle" :
                    m_Graph->Domain == MaterialDomain::PostProcess ? ICON_IMAGE "  Post Process" :
                    ICON_CUBE "  Surface";

                ImGui::TextDisabled("%s", domainLabel);
            }

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
                // ── MATFUNC_V2 — arrastar uma Material Function pro canvas ──
                //
                // Mesmo gesto da textura, e pela mesma razao: montar a chamada
                // pelo menu era criar o node, achar o Asset Picker no painel de
                // detalhes e procurar a funcao numa lista. Arrastar do browser
                // resolve os tres passos de uma vez, e ja com a assinatura
                // lida — o node cai no canvas com os pinos certos.
                else if (record && record->Type == AssetType::MaterialFunction)
                {
                    ImVec2 dropScreenPos = ImGui::GetMousePos();

                    std::string fnName;
                    std::vector<MaterialFunctionParam> fnIn, fnOut;

                    if (MaterialFunction::ReadSignature(record->FilePath,
                        fnName, fnIn, fnOut))
                    {
                        if (Node* node = m_Graph->AddMaterialFunctionNode())
                        {
                            node->StringValue = record->UUID;

                            // Ja monta os pinos e chama BuildNodes por dentro:
                            // sem ParentNode preenchido, CanCreateLink recusa
                            // qualquer fio no node recem-criado.
                            m_Graph->RebuildFunctionCallPins(node, fnIn, fnOut);
                            ed::SetNodePosition(node->ID, dropScreenPos);
                        }
                    }
                    else
                    {
                        LogWarning("[MATFUNC_V2] nao consegui ler a assinatura de '"
                            + record->FilePath.filename().string() + "'.");
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

                // CUSTOM_NODE_V1 — era `ci < 7` digitado. Com a categoria
                // Custom somando 8, o `7` teria feito ela nunca aparecer no
                // menu — e o node existiria, compilaria e seria inalcancavel.
                // Exatamente o bug que ja aconteceu no menu do Script Editor.
                static_assert(IM_ARRAYSIZE(s_MatCats) <= 10,   // MATFUNC_V1
                    "m_NodeCatOpen menor que a tabela de categorias");
                for (int ci = 0; ci < IM_ARRAYSIZE(s_MatCats); ci++)
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