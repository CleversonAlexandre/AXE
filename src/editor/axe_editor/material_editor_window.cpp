#include "material_editor_window.hpp"
#include "editor_icon_library.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

#include <utilities/widgets.h>
#include <imgui/imgui_internal.h>



namespace ed = ax::NodeEditor;

namespace axe
{
    MaterialEditorWindow::MaterialEditorWindow()
    {
        ed::Config config;
        config.SettingsFile = nullptr;
        m_NodeEditorContext = ed::CreateEditor(&config);
        m_Graph = std::make_unique<MaterialGraph>();
    }

    MaterialEditorWindow::~MaterialEditorWindow()
    {
        if (m_NodeEditorContext)
            ed::DestroyEditor(m_NodeEditorContext);
    }

    void MaterialEditorWindow::OpenMaterial(std::shared_ptr<MaterialAsset> asset)
    {
        if (m_NodeEditorContext)
        {
            ed::DestroyEditor(m_NodeEditorContext);
            m_NodeEditorContext = nullptr;
        }

        ed::Config config;
        config.SettingsFile = nullptr;
        m_NodeEditorContext = ed::CreateEditor(&config);

        m_Asset = asset;
        m_Open = true;
        m_FrameCount = 0;
        m_Graph = std::make_unique<MaterialGraph>();
    }

    void MaterialEditorWindow::Draw()
    {
        if (!m_Open || !m_Asset) return;

        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);

        std::string title = "Material Editor — " + m_Asset->GetName() + "###MaterialEditor";

        if (!ImGui::Begin(title.c_str(), &m_Open))
        {
            ImGui::End();
            return;
        }

        auto mat = m_Asset->GetMaterial();
        if (!mat) { ImGui::End(); return; }

        if (ImGui::BeginTable("##layout", 2,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("##params", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn("##graph", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            DrawMaterialParams(*mat);

            ImGui::TableSetColumnIndex(1);
            DrawNodeGraph();

            ImGui::EndTable();
        }

        ImGui::End();
    }

    void MaterialEditorWindow::DrawMaterialParams(Material& mat)
    {
        bool usePBR = mat.UsePBR;
        if (ImGui::Checkbox("PBR", &usePBR))
            mat.UsePBR = usePBR;

        ImGui::Separator();

        if (!mat.UsePBR)
        {
            ImGui::ColorEdit4("Cor", glm::value_ptr(mat.Color));
            ImGui::DragFloat("Specular", &mat.SpecularStrength, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Shininess", &mat.Shininess, 1.0f, 1.0f, 256.0f);
        }
        else
        {
            ImGui::DragFloat("Metallic", &mat.Metallic, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Roughness", &mat.Roughness, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("AO", &mat.AO, 0.01f, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Texturas:");
            ImGui::Spacing();

            DrawTextureSlot("Albedo", mat.AlbedoMap, mat.AlbedoUUID);
            DrawTextureSlot("Normal", mat.NormalMap, mat.NormalUUID);
            DrawTextureSlot("Roughness", mat.RoughnessMap, mat.RoughnessUUID);
            DrawTextureSlot("Metallic", mat.MetallicMap, mat.MetallicUUID);
            DrawTextureSlot("AO", mat.AOMap, mat.AOUUID);
        }

        ImGui::Separator();
        if (ImGui::Button("Salvar", ImVec2(-1, 0)))
            if (!m_Asset->GetFilePath().empty())
                m_Asset->Save(m_Asset->GetFilePath());

        //ImGui::Separator();
        //ImGui::Text("Adicionar Node:");
        //if (ImGui::Button("+ Texture Sample", ImVec2(-1, 0)))
        //    m_Graph->AddTextureSampleNode();
        //if (ImGui::Button("+ Color", ImVec2(-1, 0)))
        //    m_Graph->AddColorNode();
        //if (ImGui::Button("+ Float", ImVec2(-1, 0)))
        //    m_Graph->AddFloatNode();
    }

    void MaterialEditorWindow::DrawTextureSlot(const char* label,
        std::shared_ptr<Texture2D>& tex, std::string& uuid)
    {
        ImGui::PushID(label);

        ImVec2 size(48, 48);
        if (tex && tex->IsLoaded())
            ImGui::Image((ImTextureID)(uintptr_t)tex->GetRendererID(),
                size, ImVec2(0, 1), ImVec2(1, 0));
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::Button("##empty", size);
            ImGui::PopStyleColor();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
            {
                std::string dropped = (const char*)payload->Data;
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(dropped);
                if (record && record->Type == AssetType::Texture)
                {
                    tex = Texture2D::Create(record->FilePath.string());
                    uuid = dropped;
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", label);
        if (tex && tex->IsLoaded())
        {
            ImGui::TextDisabled("%dx%d", tex->GetWidth(), tex->GetHeight());
            if (ImGui::SmallButton("X")) { tex = nullptr; uuid = ""; }
        }
        else
            ImGui::TextDisabled("Nenhuma");
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::Spacing();
    }

    ImColor MaterialEditorWindow::GetPinColor(PinType type) const
    {
        switch (type)
        {
        case PinType::Float:    return ImColor(255, 210, 0);
        case PinType::Vec3:     return ImColor(0, 200, 100);
        case PinType::Vec4:     return ImColor(100, 150, 255);
        case PinType::Texture2D:return ImColor(200, 100, 200);
        default:                return ImColor(200, 200, 200);
        }
    }
    //void MaterialEditorWindow::DrawCommentNode(Node* node)
    //{
    //    constexpr float commentAlpha = 0.75f;
    //    constexpr float padding = 16.0f;            

    //    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentAlpha);
    //    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 64));
    //    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 64));

    //    ed::BeginNode(node->ID);
    //    ImGui::PushID(node->ID.AsPointer());        

    //    //Calcula largura disponível para o texto baseado no tamanho do node 
    //    float textWrapWidth = std::max(100.0f, node->Size.x - padding);

    //    //Pega posição da tela para calcular wrap corretamente
    //    ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
    //                   
    //    ImGui::PushTextWrapPos(cursorScreenPos.x + textWrapWidth);
    //    ImGui::TextWrapped("%s", node->Name.c_str());
    //    ImGui::PopTextWrapPos();

    //    ed::Group(node->Size);
    //    ImGui::PopID();
    //    ed::EndNode();

    //    ed::PopStyleColor(2);
    //    ImGui::PopStyleVar();

    //    //Atualiza tamanho do node
    //    ImVec2 newSize = ed::GetNodeSize(node->ID);
    //    if (newSize.x > 50 && newSize.y > 50)
    //        node->Size = newSize;

    //          
    //    if (ed::BeginGroupHint(node->ID))
    //    {
    //        auto bgAlpha = static_cast<int>(ImGui::GetStyle().Alpha * 255);
    //        auto min = ed::GetGroupMin();

    //      
    //        //ImGui::SetCursorScreenPos(min - ImVec2(-8, ImGui::GetTextLineHeightWithSpacing() + 4));
    //        ImGui::SetCursorScreenPos(min - ImVec2(8, 8));

    //        //Calcula  largura do hint baseado no tamanho do grupo
    //        auto max = ed::GetGroupMax();
    //        float hintWrapWidth = std::max(100.0f, (max.x - min.x) - padding);

    //        ImVec2 hintCursorPos = ImGui::GetCursorScreenPos();            
    //        ImGui::PushTextWrapPos(hintCursorPos.x + hintWrapWidth);
    //        ImGui::TextWrapped("%s", node->Name.c_str());
    //        ImGui::PopTextWrapPos();
    //        

    //        auto drawList = ed::GetHintBackgroundDrawList();            
    //        auto hintBounds = ImGui_GetItemRect();
    //        auto hintFrameBounds = ImRect_Expanded(hintBounds, 8, 4);
    //               
    //        drawList->AddRectFilled(
    //            hintFrameBounds.GetTL(),
    //            hintFrameBounds.GetBR(),
    //            IM_COL32(255, 255, 255, 64 * bgAlpha / 255), 4.0f);

    //        drawList->AddRect(
    //            hintFrameBounds.GetTL(),
    //            hintFrameBounds.GetBR(),
    //            IM_COL32(255, 255, 255, 128 * bgAlpha / 255), 4.0f);           
    //    }
    //    ed::EndGroupHint();

    //}

    void MaterialEditorWindow::UpdateCommentChildren(Node* commentNode)
    {
        commentNode->ChildNodeIDs.clear();

        ImVec2 commentPos = ed::GetNodePosition(commentNode->ID);
        ImVec2 commentMin = commentPos;
        ImVec2 commentMax = ImVec2(commentPos.x + commentNode->Size.x,
            commentPos.y + commentNode->Size.y);

        // Verifica todos os nodes
        for (auto& node : m_Graph->GetNodes())
        {
            //if (node->Type == NodeType::Comment || node->ID == commentNode->ID)
            //    continue;
            if (node->ID == commentNode->ID)
                continue;

            ImVec2 nodePos = ed::GetNodePosition(node->ID);
            ImVec2 nodeSize = ed::GetNodeSize(node->ID);
            ImVec2 nodeCenter = ImVec2(nodePos.x + nodeSize.x * 0.5f,
                nodePos.y + nodeSize.y * 0.5f);

            // Verifica se o centro do node está dentro do comment
            if (nodeCenter.x >= commentMin.x && nodeCenter.x <= commentMax.x &&
                nodeCenter.y >= commentMin.y && nodeCenter.y <= commentMax.y)
            {
                commentNode->ChildNodeIDs.push_back(node->ID.Get());
            }
        }
    }

    void MaterialEditorWindow::DrawCommentNode(Node* node)
    {
        constexpr float commentAlpha = 0.75f;
        constexpr float padding = 16.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentAlpha);
        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 64));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 64));

        ed::BeginNode(node->ID);
        ImGui::PushID(node->ID.AsPointer());

        // Calcula altura do texto
        float textWrapWidth = std::max(100.0f, node->Size.x - padding);
        ImFont* font = ImGui::GetFont();
        ImVec2 textSize = font->CalcTextSizeA(
            ImGui::GetFontSize(),
            FLT_MAX,
            textWrapWidth,
            node->Name.c_str()
        );

        float newTitleHeight = textSize.y + 16.0f; // padding vertical
        float deltaHeight = newTitleHeight - node->TitleHeight;

        // Se altura mudou, ajusta posição dos filhos
        if (std::abs(deltaHeight) > 0.1f)
        {
            for (int childID : node->ChildNodeIDs)
            {
                Node* child = m_Graph->FindNodeByID(childID);
                if (child)
                {
                    ImVec2 childPos = ed::GetNodePosition(child->ID);
                    ed::SetNodePosition(child->ID, ImVec2(childPos.x, childPos.y + deltaHeight));
                }
            }
            node->TitleHeight = newTitleHeight;
        }

        // Desenha texto
        ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
        ImGui::PushTextWrapPos(cursorScreenPos.x + textWrapWidth);
        ImGui::TextWrapped("%s", node->Name.c_str());
        ImGui::PopTextWrapPos();

        // Espaçador até o group
        ImGui::Dummy(ImVec2(0, 8));

        ed::Group(node->Size);
        ImGui::PopID();
        ed::EndNode();

        ed::PopStyleColor(2);
        ImGui::PopStyleVar();

        // Atualiza tamanho
        ImVec2 newSize = ed::GetNodeSize(node->ID);
        if (newSize.x > 50 && newSize.y > 50)
            node->Size = newSize;

        // Detecta nodes filhos (verifica quais estão dentro da área)
        UpdateCommentChildren(node);

        // Hint para zoom out
        if (ed::BeginGroupHint(node->ID))
        {
            auto bgAlpha = static_cast<int>(ImGui::GetStyle().Alpha * 255);
            auto min = ed::GetGroupMin();

            ImGui::SetCursorScreenPos(min + ImVec2(8, 8));

            // Pega só a primeira linha ou primeiros 20 caracteres
            std::string preview = node->Name;

            // Encontra primeira quebra de linha
            size_t newlinePos = preview.find('\n');
            if (newlinePos != std::string::npos)
                preview = preview.substr(0, newlinePos);

            // Limita a 20 caracteres
            if (preview.length() > 20)
                preview = preview.substr(0, 20) + "...";

            ImGui::TextUnformatted(preview.c_str());

            auto drawList = ed::GetHintBackgroundDrawList();
            auto hintBounds = ImGui_GetItemRect();
            auto hintFrameBounds = ImRect_Expanded(hintBounds, 8, 4);

            drawList->AddRectFilled(
                hintFrameBounds.GetTL(), hintFrameBounds.GetBR(),
                IM_COL32(255, 255, 255, 64 * bgAlpha / 255), 4.0f);

            drawList->AddRect(
                hintFrameBounds.GetTL(), hintFrameBounds.GetBR(),
                IM_COL32(255, 255, 255, 128 * bgAlpha / 255), 4.0f);

            ed::EndGroupHint();
        }
    }

    void MaterialEditorWindow::DrawNode(Node& node)
    {
        if (node.Type == NodeType::Comment)
        {
            DrawCommentNode(&node); 
            return;
        }

        const float PIN_RADIUS = 5.0f;
        const float NODE_WIDTH = 180.0f;

        auto drawPin = [&](Pin& pin, bool isInput)
            {
                auto col = GetPinColor(pin.Type);
                auto imcol = ImVec4(col.Value.x, col.Value.y, col.Value.z, 1.0f);

                ed::BeginPin(pin.ID, isInput ? ed::PinKind::Input : ed::PinKind::Output);
                ed::PinPivotAlignment(isInput ? ImVec2(0.0f, 0.5f) : ImVec2(1.0f, 0.5f));
                ed::PinPivotSize(ImVec2(0, 0));

                ImGui::BeginGroup();

                float iconSize = (float)m_PinIconSize;
                float textHeight = ImGui::GetTextLineHeight();
                float verticalOffset = (iconSize - textHeight) * 0.5f;

                
               

                if (isInput)
                {
                    // Input: ícone + texto (centralizado verticalmente)
                    ImVec2 iconPos = ImGui::GetCursorPos();
                    bool connected = m_Graph->IsPinLinked(pin.ID);
                    DrawPinIcon(pin, connected, 255);

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + verticalOffset);
                    ImGui::TextColored(imcol, "%s", pin.Name.c_str());

                   
                }
                else
                {
                    // Output: texto + ícone (centralizado verticalmente)
                    float contentWidth = ImGui::CalcTextSize(pin.Name.c_str()).x + iconSize + 8;
                    float availWidth = NODE_WIDTH * 0.5f - 8;
                    float offset = availWidth - contentWidth;

                    if (offset > 0)
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

                    // Salva posição Y base
                    float baseY = ImGui::GetCursorPosY();

                    // Texto centralizado
                    ImGui::SetCursorPosY(baseY + verticalOffset);
                    ImGui::TextColored(imcol, "%s", pin.Name.c_str());

                    // Ícone na mesma baseline (volta para baseY)
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(baseY);
                    bool connected = m_Graph->IsPinLinked(pin.ID);
                    DrawPinIcon(pin, connected, 255);
                }
                

                ImGui::EndGroup();
                ed::EndPin();
            };

        // ---- Node ----
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 4));
        ed::PushStyleVar(ed::StyleVar_NodeRounding, 8.0f);
        ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.5f);
        ed::BeginNode(node.ID);

        // Barra de título colorida
        {
            ImVec2 titlePos = ImGui::GetCursorScreenPos();
            float  titleH = ImGui::GetTextLineHeightWithSpacing() + 8.0f;
            ImVec2 titleEnd = ImVec2(titlePos.x + NODE_WIDTH, titlePos.y + titleH);

            ImGui::GetWindowDrawList()->AddRectFilled(
                titlePos, titleEnd,
                ImColor(node.Color.x, node.Color.y, node.Color.z, 1.0f),
                8.0f, ImDrawFlags_RoundCornersTop
            );

            // Texto do título centralizado
            ImVec2 textSize = ImGui::CalcTextSize(node.Name.c_str());
            ImGui::SetCursorScreenPos(ImVec2(
                titlePos.x + (NODE_WIDTH - textSize.x) * 0.5f,
                titlePos.y + 4.0f
            ));
            ImGui::TextUnformatted(node.Name.c_str());

            // Avança cursor para abaixo da barra
            ImGui::SetCursorScreenPos(ImVec2(titlePos.x, titlePos.y + titleH));
            ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
        }

        // Conteúdo do node
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);

        if (node.Value.Type == PinType::Float && node.Outputs.size() == 1)
        {
            ImGui::SetNextItemWidth(NODE_WIDTH - 16);
            ImGui::DragFloat("##val", &node.Value.FloatVal, 0.01f, 0.0f, 1.0f);
        }
        else if (node.Value.Type == PinType::Vec4 && !node.Outputs.empty())
        {
            ImGui::SetNextItemWidth(NODE_WIDTH - 16);
            ImGui::ColorEdit4("##col", &node.Value.Vec4Val.x,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        }

        if (node.Name == "Texture Sample")
        {
            float imgSize = NODE_WIDTH - 16;
            if (node.Value.TextureVal && node.Value.TextureVal->IsLoaded())
                ImGui::Image(
                    (ImTextureID)(uintptr_t)node.Value.TextureVal->GetRendererID(),
                    ImVec2(imgSize, imgSize), ImVec2(0, 1), ImVec2(1, 0));
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
                ImGui::Button("Arraste\numa textura", ImVec2(imgSize, imgSize * 0.6f));
                ImGui::PopStyleColor();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
                {
                    std::string uuid = (const char*)payload->Data;
                    const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                    if (record && record->Type == AssetType::Texture)
                    {
                        node.Value.TextureVal = Texture2D::Create(record->FilePath.string());
                        node.Value.TextureUUID = uuid;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
        }

        // Pins
        // Pins - input esquerda | output direita na MESMA linha
        int maxPins = (int)std::max(node.Inputs.size(), node.Outputs.size());

        const float inputColumnWidth = NODE_WIDTH * 0.5f - 8;
        const float outputColumnWidth = NODE_WIDTH * 0.5f - 8;

        for (int i = 0; i < maxPins; i++)
        {
            bool hasInput = i < (int)node.Inputs.size();
            bool hasOutput = i < (int)node.Outputs.size();

            // Input na esquerda
            if (hasInput)
            {
                drawPin(node.Inputs[i], true);
            }
            else
            {
                ImGui::Dummy(ImVec2(inputColumnWidth, ImGui::GetTextLineHeight()));
            }

            ImGui::SameLine(inputColumnWidth + 8); // Posiciona output à direita

            // Output na direita
            if (hasOutput)
            {
                // Calcula offset para alinhar à direita
                ImGui::BeginGroup();
                drawPin(node.Outputs[i], false);
                ImGui::EndGroup();
            }
            else
            {
                ImGui::Dummy(ImVec2(outputColumnWidth, ImGui::GetTextLineHeight()));
            }
        }

        ed::EndNode();
        ed::PopStyleVar(3);
    }

    void MaterialEditorWindow::DrawNodeGraph()
    {
        ed::SetCurrentEditor(m_NodeEditorContext);

        //Node* node;

        //node = m_Graph->AddComment(); ed::SetNodePosition(node->ID, ImVec2(112, 576)); ed::SetGroupSize(node->ID, ImVec2(384, 154));
        //node = m_Graph->AddComment(); ed::SetNodePosition(node->ID, ImVec2(800, 224)); ed::SetGroupSize(node->ID, ImVec2(640, 400));


        ed::Begin("MaterialGraph", ImVec2(0.0f, 0.0f));

        ed::NodeId doubleClickedNode = ed::GetDoubleClickedNode();

        for (auto& node : m_Graph->GetNodes())
        {
            // Se for o node clicado e for comentário, prepara edição
            if (doubleClickedNode && node->ID == doubleClickedNode && node->Type == NodeType::Comment)
            {
                m_EditingCommentNode = node.get();
                m_CommentEditBuffer = node->Name;
                ImVec2 nodePos = ed::GetNodePosition(node->ID);
                m_CommentEditPopupPos = ed::CanvasToScreen(nodePos);
            }

            DrawNode(*node);
        }

        //for (auto& node : m_Graph->GetNodes())
        //    DrawNode(*node);

        for (auto& link : m_Graph->GetLinks())
            ed::Link(link.ID, link.StartPin, link.EndPin);

        if (m_FrameCount > 2)
        {
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

                        auto drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                        ImGui::TextUnformatted(label);
                };

                //ed::PinId startPin = 0, endPin = 0;
                ed::PinId startPinId = 0, endPinId = 0;
                

                
                if (ed::QueryNewLink(&startPinId, &endPinId))
                {
                    auto startPin = m_Graph->FindPin(startPinId);
                    auto endPin = m_Graph->FindPin(endPinId);

                    newLinkPin = startPin ? startPin : endPin;

                    if (startPin->Kind == ed::PinKind::Input)
                    {
                        std::swap(startPin, endPin);
                        std::swap(startPinId, endPinId);
                    }

                    if (startPin && endPin)
                    {
                        if (endPin == startPin)
                        {
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else if (endPin->Kind == startPin->Kind)
                        {
                            showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }

                        else if (endPin->Type != startPin->Type)
                        {
                            showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 128, 128), 2.0f);
                        }
                        else
                        {
                            showLabel("+ Create Link", ImColor(32, 45, 32, 100));
                            if (ed::AcceptNewItem(ImColor(125, 255, 128), 1.0f))
                            {
                                m_Graph->AddLink(startPinId, endPinId);
                            }
                        }
                    }
           
                }


                ed::EndCreate();
            }

            if (ed::BeginDelete())
            {
                ed::LinkId deletedLink;
                while (ed::QueryDeletedLink(&deletedLink))
                    if (ed::AcceptDeletedItem())
                        m_Graph->RemoveLink(deletedLink);
                ed::EndDelete();
            }

            static ImVec2 contextMenuPos = ImVec2(0, 0);
# if 1

            auto openPopupPosition = ImGui::GetMousePos();

            ed::Suspend();
            if (ed::ShowNodeContextMenu(&m_Graph->contextNodeId))            
                ImGui::OpenPopup("Node Context Menu");     
            else if(ed::ShowLinkContextMenu(&m_Graph->contextLinkId))
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
                auto node = m_Graph->FindNode(m_Graph->contextNodeId)->get();
                

                ImGui::TextUnformatted("Node Context Menu");
                ImGui::Separator();
                if (node)
                {
                    ImGui::Text("ID: %p", node->ID.AsPointer());
                    ImGui::Text("Type: %s", node->Type == NodeType::Blueprint ? "Blueprint" : (node->Type == NodeType::Tree ? "Tree" : "Comment"));
                    ImGui::Text("Inputs: %d", (int)node->Inputs.size());
                    ImGui::Text("Outputs: %d", (int)node->Outputs.size());
                }
                else
                {
                    ImGui::Text("Unknow node: %p", m_Graph->contextNodeId.AsPointer());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete"))
                        ed::DeleteNode(m_Graph->contextNodeId);                    
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("Create New Node"))
            {
                auto newNodePosition = openPopupPosition;

                Node* node = nullptr;
                if (ImGui::MenuItem("Texture Sample"))
                    node = m_Graph->AddTextureSampleNode();
                ImGui::Separator();
                if (ImGui::MenuItem("Color"))
                    node = m_Graph->AddColorNode();
                ImGui::Separator();
                if (ImGui::MenuItem("Float"))
                    node = m_Graph->AddFloatNode();
                ImGui::Separator();
                if (ImGui::MenuItem("Comment"))                   
                    node = m_Graph->AddComment();
                   
      
                if (node)
                {
                    m_Graph->BuildNodes();
                    createNewNode = false;

                 //   ed::SetNodePosition(node->ID, newNodePosition);
                    ed::SetNodePosition(node->ID, newNodePosition);

                    if (auto startPin = newNodeLinkPin)
                    {
                        auto& pins = startPin->Kind == ed::PinKind::Input ? node->Outputs : node->Inputs;

                        for (auto& pin : pins)
                        {
                            if (CanCreateLink(startPin, &pin))
                            {
                                auto endPin = &pin;
                                if (startPin->Kind == ed::PinKind::Input)
                                    std::swap(startPin, endPin);                                

                                m_Graph->m_Links.emplace_back(m_Graph->GetNextID(), startPin->ID, endPin->ID);
                                m_Graph->m_Links.back().Color = GetIconColor(startPin->Type);
                            }
                        }
                    }
                }
                ImGui::EndPopup();
            }
            // Tecla C para criar comment agrupando seleção
            if (ImGui::IsKeyPressed(ImGuiKey_C) && !ImGui::GetIO().WantTextInput)
            {
                // Pega nodes selecionados
                int selectedCount = ed::GetSelectedObjectCount();
                if (selectedCount > 0)
                {
                    std::vector<ed::NodeId> selectedNodes(selectedCount);
                    ed::GetSelectedNodes(selectedNodes.data(), selectedCount);

                    if (!selectedNodes.empty())
                    {
                        // Calcula bounding box dos nodes selecionados
                        ImVec2 minPos(FLT_MAX, FLT_MAX);
                        ImVec2 maxPos(-FLT_MAX, -FLT_MAX);

                        for (auto nodeId : selectedNodes)
                        {
                            ImVec2 pos = ed::GetNodePosition(nodeId);
                            ImVec2 size = ed::GetNodeSize(nodeId);

                            minPos.x = std::min(minPos.x, pos.x);
                            minPos.y = std::min(minPos.y, pos.y);
                            maxPos.x = std::max(maxPos.x, pos.x + size.x);
                            maxPos.y = std::max(maxPos.y, pos.y + size.y);
                        }

                        // Adiciona margem
                        const float margin = 32.0f;
                        minPos.x -= margin;
                        minPos.y -= margin;
                        maxPos.x += margin;
                        maxPos.y += margin;

                        // Cria comment node
                        Node* comment = m_Graph->AddComment();
                        comment->Size = ImVec2(maxPos.x - minPos.x, maxPos.y - minPos.y);
                        ed::SetNodePosition(comment->ID, minPos);

                        // Atualiza filhos
                        UpdateCommentChildren(comment);

                        // Deseleciona tudo e seleciona só o comment
                        ed::ClearSelection();
                        ed::SelectNode(comment->ID, false);
                    }
                }
            }

            createNewNode = false;
            ImGui::PopStyleVar();
            ed::Resume();
                        
            

# endif
            
        }

        

       ed::Suspend();
       if (m_EditingCommentNode)
       {
           ImGui::SetNextWindowPos(m_CommentEditPopupPos, ImGuiCond_Always);
           ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
 
           ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
           if (ImGui::Begin("##EditComment", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
           {
               static char buffer[2048];
               static bool focusSet = false;
 
               if (!focusSet)
               {
                   strncpy(buffer, m_CommentEditBuffer.c_str(), sizeof(buffer) - 1);
                   buffer[sizeof(buffer) - 1] = '\0';
                   ImGui::SetKeyboardFocusHere();
                   focusSet = true;
               }
 
               ImGui::InputTextMultiline("##edit", buffer, sizeof(buffer),
                   ImVec2(384, 184), ImGuiInputTextFlags_AllowTabInput);
 
               if (!ImGui::IsWindowFocused() || ImGui::IsKeyPressed(ImGuiKey_Escape))
               {
                   if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
                       m_EditingCommentNode->Name = buffer;
 
                   m_EditingCommentNode = nullptr;
                   focusSet = false;
               }
 
               ImGui::End();
           }
           ImGui::PopStyleVar();
       }
       ed::Resume();
    
        m_FrameCount++;
        ed::End();
        ed::SetCurrentEditor(nullptr);
}

    ImColor MaterialEditorWindow::GetIconColor(PinType type)
    {
        switch (type)
        {
        case PinType::Float:    return ImColor(255, 210, 0);
        case PinType::Vec3:     return ImColor(0, 200, 100);
        case PinType::Vec4:     return ImColor(100, 150, 255);
        case PinType::Texture2D:return ImColor(200, 100, 200);
        default:                return ImColor(200, 200, 200);
        }
    };

    void MaterialEditorWindow::DrawPinIcon(const Pin& pin, bool connected, int alpha)
    {


        ax::Drawing::IconType iconType;
        ImColor color = GetIconColor(pin.Type);
        color.Value.w = alpha / 255.0f;
        switch (pin.Type)
        {
        case PinType::Float: iconType = ax::Drawing::IconType::Circle; break;
        case PinType::Vec3: iconType = ax::Drawing::IconType::Circle; break;
        case PinType::Vec4: iconType = ax::Drawing::IconType::Circle; break;
        case PinType::Texture2D: iconType = ax::Drawing::IconType::RoundSquare; break;
        default:
            return;


        }
        ax::Widgets::Icon(ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)), iconType, connected, color, ImColor(32, 32, 32, alpha));
    }

    ImRect MaterialEditorWindow::ImGui_GetItemRect()
    {
        return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }

    ImRect MaterialEditorWindow::ImRect_Expanded(const ImRect& rect, float x, float y)
    {
        auto result = rect;

        result.Min.x -= x;
        result.Min.y -= y;

        result.Max.x += x;
        result.Max.y += y;

        return result;

    }
   

    bool MaterialEditorWindow::CanCreateLink(Pin* a, Pin* b)
    {
        if (!a || !b || a == b || a->Kind == b->Kind || a->Type != b->Type || a->ParentNode == b->ParentNode)
            return false;

        return true;
    }
} // namespace axe