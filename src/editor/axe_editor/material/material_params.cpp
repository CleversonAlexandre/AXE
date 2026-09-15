// material_params.cpp
// Painel "Material Params": inspeção do node selecionado no grafo (ou, na
// ausência de seleção, os parâmetros globais do material) e o widget de
// slot de textura usado pelos parâmetros legados (não-PBR).

#include "material_editor_window.hpp"
#include "material_function.hpp"   // MATFUNC_V1

// MATERIAL_EDITOR_STYLE_V1 — os mesmos ui::SectionHeader/IconButton e glifos
// que o Script Editor, o Control Rig e o Anim Graph ja usam.
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"

#include "axe/asset/asset_database.hpp"
#include "editor/axe_editor/asset/asset_picker.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <filesystem>
#include <cstdio>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace axe
{

    void MaterialEditorWindow::DrawMaterialParamsWindow()
    {
        ed::SetCurrentEditor(m_NodeEditorContext);

        if (ImGui::Begin("Material Params"))
            DrawMaterialParams(m_Material.get());   // MATFUNC_V1 — nulo em modo funcao
        ImGui::End();

        ed::SetCurrentEditor(nullptr);
    }


    void MaterialEditorWindow::DrawMaterialParams(Material* matPtr)
    {
        // Verifica se há um node selecionado no graph
        int selectedCount = ed::GetSelectedObjectCount();
        ed::NodeId selectedNodeId = 0;

        if (selectedCount > 0)
        {
            std::vector<ed::NodeId> selectedNodes(selectedCount);
            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), selectedCount);
            if (nodeCount > 0)
                selectedNodeId = selectedNodes[0];
        }

        if (selectedNodeId)
        {
            // Encontra o node selecionado
            auto nodePtr = m_Graph->FindNode(selectedNodeId);
            if (nodePtr)
            {
                Node* node = nodePtr->get();

                // MATERIAL_EDITOR_STYLE_V1 — cabecalho na mesma forma do
                // "Functions"/"Parameters" dos outros paineis, com o icone
                // dizendo QUE TIPO de node e antes de o nome ser lido.
                const char* nodeIcon =
                    node->Name == "Custom" ? ICON_CODE :
                    node->Name == "Texture Sample" ? ICON_IMAGE :
                    node->Name == "Scene Color" ? ICON_IMAGE :
                    node->Name == "Screen UV" ? ICON_BORDER_ALL :
                    node->Name == "Color" ? ICON_PALETTE :
                    node->Name == "Time" ? ICON_CLOCK :
                    node->Name == "Material Function" ? ICON_CODE :        // MATFUNC_V1
                    node->Name == "Function Input" ? ICON_CIRCLE_NODES :   // MATFUNC_V1
                    node->Name == "Function Output" ? ICON_CIRCLE_NODES :  // MATFUNC_V1
                    ICON_CIRCLE_NODES;

                ui::SectionHeader(nodeIcon, node->Name.c_str(), ui::Accent::Primary);

                if (node->Name == "Float" && node->IsConstant)
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##val", &node->Value.FloatVal, 0.01f, 0.0f, 10.0f);
                }
                else if (node->Name == "Vec2" && node->IsConstant)
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat2("##val2", &node->Value.Vec2Val.x, 0.01f);
                }
                else if (node->Name == "Vec3" && node->IsConstant)
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat3("##val3", &node->Value.Vec3Val.x, 0.01f);
                }
                else if (node->Name == "Color" && node->IsConstant)
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::ColorEdit4("##col", &node->Value.Vec4Val.x);
                }
                else if (node->Name == "Texture Sample")
                {
                    ImGui::Text("Textura:");
                    ImGui::Spacing();

                    std::string uuid = node->Value.TextureUUID;
                    AssetPicker::Draw("##tex",
                        node->Value.TextureUUID,
                        { AssetType::Texture },
                        [&](const AssetRecord& record)
                        {
                            node->Value.TextureVal = Texture2D::Create(record.FilePath.string());
                            node->Value.TextureUUID = record.UUID;
                        });
                }
                // ═══════════════════════════════════════════════════════════
                //  MATFUNC_V1 — Function Input / Function Output
                //
                //  Duas coisas so: o NOME do parametro e o TIPO. O nome e o que
                //  casa este node com o pino no node de chamada — a ligacao e
                //  por nome, nunca por posicao, para reordenar os parametros da
                //  funcao nao trocar o significado de um material ja pronto.
                // ═══════════════════════════════════════════════════════════
                else if (node->Name == "Function Input" || node->Name == "Function Output")
                {
                    ImGui::TextDisabled("Nome do parametro");
                    ImGui::SetNextItemWidth(-1);

                    char nameBuf[64];
                    std::snprintf(nameBuf, sizeof(nameBuf), "%s", node->StringValue.c_str());
                    if (ImGui::InputText("##fnparamname", nameBuf, sizeof(nameBuf)))
                    {
                        node->StringValue = nameBuf;
                        m_Graph->SyncFunctionIONode(node);
                    }

                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "E por este nome que o pino aparece no node de chamada,\n"
                            "e e por ele que a ligacao e mantida quando a assinatura\n"
                            "da funcao muda. Renomear DESFAZ o fio ligado nele nos\n"
                            "materiais que ja chamam esta funcao.");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tipo");
                    ImGui::SetNextItemWidth(-1);

                    static const char* kFnTypes[] = { "Float", "Vec2", "Vec3", "Vec4" };
                    int typeIdx = (int)node->CustomOutputType;
                    if (typeIdx < 0 || typeIdx > 3) typeIdx = 0;

                    if (ImGui::Combo("##fnparamtype", &typeIdx, kFnTypes, IM_ARRAYSIZE(kFnTypes)))
                    {
                        node->CustomOutputType = (PinType)typeIdx;
                        m_Graph->SyncFunctionIONode(node);
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled(
                        "A assinatura da funcao e refeita a partir destes nodes\n"
                        "toda vez que o asset e salvo. A ORDEM dos pinos e a\n"
                        "ordem VERTICAL deles no canvas.");
                }
                // ═══════════════════════════════════════════════════════════
                //  MATFUNC_V1 — o node de chamada
                //
                //  Um Asset Picker e um botao. Os pinos NAO sao editaveis aqui:
                //  eles sao a assinatura da funcao escolhida, e o unico lugar
                //  de mexer neles e dentro do `.axematfunc`.
                // ═══════════════════════════════════════════════════════════
                else if (node->Name == "Material Function")
                {
                    ImGui::TextDisabled("Funcao");
                    ImGui::Spacing();

                    AssetPicker::Draw("##matfunc",
                        node->StringValue,
                        { AssetType::MaterialFunction },
                        [&](const AssetRecord& record)
                        {
                            node->StringValue = record.UUID;

                            std::string fnName;
                            std::vector<MaterialFunctionParam> ins, outs;
                            if (MaterialFunction::ReadSignature(record.FilePath, fnName, ins, outs))
                                m_Graph->RebuildFunctionCallPins(node, ins, outs);
                        });

                    ImGui::Spacing();

                    // Recarregar a assinatura e uma acao MANUAL de proposito.
                    // Reler o asset a cada frame poria I/O de disco no laco de
                    // desenho; e reler sozinho ao abrir o material faria os
                    // pinos mudarem debaixo do autor sem ele pedir — inclusive
                    // levando fios embora, se alguem tiver renomeado um
                    // parametro do outro lado.
                    if (ImGui::Button("Recarregar assinatura", ImVec2(-1, 0)))
                    {
                        const AssetRecord* rec =
                            AssetDatabase::Get().GetByUUID(node->StringValue);

                        if (rec && std::filesystem::exists(rec->FilePath))
                        {
                            std::string fnName;
                            std::vector<MaterialFunctionParam> ins, outs;
                            if (MaterialFunction::ReadSignature(rec->FilePath, fnName, ins, outs))
                            {
                                m_Graph->RebuildFunctionCallPins(node, ins, outs);
                                LogInfo("[MATFUNC_V1] assinatura de '" + fnName + "' recarregada.");
                            }
                        }
                        else
                        {
                            LogWarning("[MATFUNC_V1] o asset desta funcao nao foi encontrado.");
                        }
                    }

                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Le de novo as entradas e saidas do .axematfunc e poe os\n"
                            "pinos deste node em dia. Os fios sao mantidos pelos pinos\n"
                            "de mesmo NOME; pino que sumiu da funcao perde o fio.");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Entradas: %d   Saidas: %d",
                        (int)node->Inputs.size(), (int)node->Outputs.size());
                }
                // ═══════════════════════════════════════════════════════════
                //  CUSTOM_NODE_V1 — o editor do node Custom
                //
                //  Tres coisas, nesta ordem, porque e a ordem em que se pensa:
                //  o tipo da saida, as entradas (nome + tipo), e o codigo que
                //  usa as entradas para produzir a saida.
                // ═══════════════════════════════════════════════════════════
                else if (node->Name == "Custom")
                {
                    static const char* kTypeNames[] = { "Float", "Vec2", "Vec3", "Vec4" };

                    // ── Tipo da saida ────────────────────────────────────
                    ImGui::TextDisabled("Output Type");
                    ImGui::SetNextItemWidth(-1);
                    int outT = (int)node->CustomOutputType;
                    if (outT > 3) outT = 0;   // Texture2D/Any nao valem como saida
                    if (ImGui::Combo("##customout", &outT, kTypeNames, IM_ARRAYSIZE(kTypeNames)))
                    {
                        node->CustomOutputType = (PinType)outT;
                        // O pin PRECISA acompanhar: e o Type dele que o
                        // compilador usa para tipar quem consome este node.
                        // Sem esta linha, mudar a saida para Vec3 geraria uma
                        // funcao que devolve vec3 atribuida a um float.
                        if (!node->Outputs.empty())
                            node->Outputs[0].Type = node->CustomOutputType;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Inputs");

                    int removeIndex = -1;
                    for (int i = 0; i < (int)node->Inputs.size(); i++)
                    {
                        ImGui::PushID(i);

                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%s", node->Inputs[i].Name.c_str());
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::InputText("##name", buf, sizeof(buf)))
                            node->Inputs[i].Name = buf;
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Este e o nome da VARIAVEL dentro do seu codigo.");

                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(70.0f);
                        int t = (int)node->Inputs[i].Type;
                        if (t > 3) t = 0;
                        if (ImGui::Combo("##type", &t, kTypeNames, IM_ARRAYSIZE(kTypeNames)))
                            node->Inputs[i].Type = (PinType)t;

                        ImGui::SameLine();
                        // Nunca deixa ficar sem nenhuma entrada: um Custom sem
                        // parametro ainda compila, mas o node fica sem pino e
                        // vira uma constante escondida no grafo.
                        ImGui::BeginDisabled(node->Inputs.size() <= 1);
                        if (ImGui::SmallButton("-")) removeIndex = i;
                        ImGui::EndDisabled();

                        ImGui::PopID();
                    }

                    bool pinsChanged = false;

                    if (removeIndex >= 0)
                    {
                        // Remover um pin invalida os LINKS que chegavam nele.
                        // Deixar link pendurado num pin que nao existe mais e
                        // como o grafo trava depois; melhor cortar aqui.
                        ed::PinId dead = node->Inputs[removeIndex].ID;
                        m_Graph->RemoveLinksForPin(dead);
                        node->Inputs.erase(node->Inputs.begin() + removeIndex);
                        pinsChanged = true;
                    }

                    if (ImGui::SmallButton("+ Input"))
                    {
                        node->Inputs.emplace_back(m_Graph->GetNextID(),
                            "In", PinType::Float, ed::PinKind::Input);
                        pinsChanged = true;
                    }

                    if (pinsChanged)
                    {
                        // BuildNodes religa Pin::ParentNode/Kind. Sem ele, o
                        // pin novo existe mas nao sabe de quem e — e o
                        // CanCreateLink do editor de nodes rejeita a ligacao.
                        m_Graph->BuildNodes();
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Code (GLSL)");

                    // Buffer estatico grande + copia por node: o InputTextMultiline
                    // do ImGui quer um char*, e guardar um buffer por node
                    // custaria memoria a toa. A copia acontece a cada frame em
                    // que ESTE node esta selecionado — que e um node so.
                    static char s_CodeBuf[4096];
                    static int  s_CodeBufNode = -1;
                    if (s_CodeBufNode != (int)node->ID.Get())
                    {
                        std::snprintf(s_CodeBuf, sizeof(s_CodeBuf), "%s",
                            node->CustomCode.c_str());
                        s_CodeBufNode = (int)node->ID.Get();
                    }

                    if (ImGui::InputTextMultiline("##customcode", s_CodeBuf,
                        sizeof(s_CodeBuf), ImVec2(-1, 200)))
                    {
                        node->CustomCode = s_CodeBuf;
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f),
                        "Corpo de uma funcao: precisa terminar em 'return'.\n"
                        "As entradas acima viram variaveis com o nome que voce deu.\n"
                        "Disponiveis tambem: v_TexCoord, v_FragPos, v_Normal,\n"
                        "u_Time, u_CameraPosition.");

                    ImGui::Spacing();
                    ImGui::TextDisabled(
                        "Erro de GLSL aparece no Shader Log ao compilar.");
                }
                else
                {
                    // Mostra os pins de input do node
                    ImGui::Text("Inputs:");
                    ImGui::Spacing();
                    for (auto& pin : node->Inputs)
                        ImGui::TextDisabled("• %s (%s)", pin.Name.c_str(),
                            pin.Type == PinType::Float ? "Float" :
                            pin.Type == PinType::Vec3 ? "Vec3" :
                            pin.Type == PinType::Vec4 ? "Vec4" : "?");

                    ImGui::Spacing();
                    ImGui::Text("Outputs:");
                    ImGui::Spacing();
                    for (auto& pin : node->Outputs)
                        ImGui::TextDisabled("• %s", pin.Name.c_str());
                }
                return;
            }
        }

        // Sem node selecionado — mostra parâmetros globais do material
        ImGui::TextDisabled(ICON_CIRCLE_INFO "  Selecione um node para editar.");
        ImGui::Spacing();

        // ── MATFUNC_V1 ───────────────────────────────────────────────────────
        //
        // Uma Material Function nao tem Domain, Blend Mode nem Shading Model, e
        // isso nao e economia de trabalho: ela nao vira shader nenhum. Quem
        // decide como o pixel e sombreado e o MATERIAL que a chama. Mostrar
        // esses combos aqui prometeria um controle que nao existe — e a mesma
        // funcao chamada de dois materiais com blend diferente obedeceria os
        // dois, o que so faz sentido se a escolha nao morar nela.
        if (IsFunctionMode())
        {
            ui::SectionHeader(ICON_CODE, "Material Function", ui::Accent::Primary);

            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_FunctionAsset->GetName().c_str());
            ImGui::TextDisabled("Nome");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##fnname", nameBuf, sizeof(nameBuf)))
                m_FunctionAsset->SetName(nameBuf);

            ImGui::Spacing();
            ImGui::TextDisabled("Descricao");

            char descBuf[256];
            std::snprintf(descBuf, sizeof(descBuf), "%s",
                m_FunctionAsset->GetDescription().c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##fndesc", descBuf, sizeof(descBuf)))
                m_FunctionAsset->SetDescription(descBuf);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ui::SectionHeader(ICON_CIRCLE_NODES, "Assinatura", ui::Accent::Neutral);
            ImGui::TextDisabled(
                "Derivada dos nodes Function Input e Function Output do canvas,\n"
                "toda vez que a funcao e salva. A ordem dos pinos e a ordem\n"
                "VERTICAL dos nodes.");
            ImGui::Spacing();

            for (const auto& in : m_FunctionAsset->GetInputs())
                ImGui::BulletText("entrada  %s : %s", in.Name.c_str(), PinTypeToString(in.Type));
            for (const auto& out : m_FunctionAsset->GetOutputs())
                ImGui::BulletText("saida    %s : %s", out.Name.c_str(), PinTypeToString(out.Type));

            if (m_FunctionAsset->GetOutputs().empty())
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f),
                    "Sem nenhum Function Output, esta funcao nao devolve nada\n"
                    "e o node de chamada aparece sem pino de saida.");
            }

            return;
        }

        if (!matPtr) return;
        Material& mat = *matPtr;

        ui::SectionHeader(ICON_SLIDERS, "Material", ui::Accent::Primary);

        // --- Material Domain / Blend Mode / Shading Model ---
        // Estrutura inspirada na Unreal. Só os itens marcados como
        // disponíveis são realmente suportados pelo motor — o resto
        // aparece no dropdown (pra já deixar o caminho familiar) mas fica
        // desabilitado, sem fingir que funciona.
        auto drawDomainCombo = [](const char* label, const char* const* names,
            const bool* available, int count, int& current)
            {
                ImGui::TextDisabled("%s", label);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo((std::string("##") + label).c_str(), names[current]))
                {
                    for (int i = 0; i < count; i++)
                    {
                        ImGuiSelectableFlags flags = available[i] ? 0 : ImGuiSelectableFlags_Disabled;
                        std::string itemLabel = available[i]
                            ? names[i] : std::string(names[i]) + " (indisponível)";
                        if (ImGui::Selectable(itemLabel.c_str(), current == i, flags))
                            current = i;
                    }
                    ImGui::EndCombo();
                }
            };

        // As contagens agora sao IM_ARRAYSIZE, e nao numeros digitados.
        //
        // Nao e preciosismo: ja aconteceu na engine (a tabela do menu do
        // Script Editor tinha 11 digitado para 12 entradas, e o ultimo item
        // simplesmente nunca aparecia). Aqui o erro seria pior — o array de
        // "disponivel" e o de nomes sao lidos com o MESMO indice, entao um
        // descompasso leria fora do array. Este arquivo acabou de ganhar uma
        // entrada nova em Shading Model; a proxima nao vai precisar lembrar
        // de mexer no numero.
        static const char* s_DomainNames[] = {
            "Surface", "Light Function", "Particle", "Deferred Decal", "Volume", "Post Process", "User Interface" };
        // POSTPROCESS_DOMAIN_V1 — "Post Process" (indice 5) passou a ser REAL.
        // VOLUME_DOMAIN_V1    — "Volume" (indice 4) passou a ser REAL.
        static const bool s_DomainAvailable[] = { true, true, true, false, true, true, false };
        static_assert(IM_ARRAYSIZE(s_DomainNames) == IM_ARRAYSIZE(s_DomainAvailable),
            "Material Domain: nomes e disponibilidade fora de sincronia");
        int domain = (int)m_Graph->Domain;
        // Mapeia o enum (que tem Particle=2, DeferredDecal=3...) pra o índice do combo
        drawDomainCombo("Material Domain", s_DomainNames, s_DomainAvailable,
            IM_ARRAYSIZE(s_DomainNames), domain);
        m_Graph->Domain = (MaterialDomain)domain;

        static const char* s_BlendNames[] = {
            "Opaque", "Masked", "Translucent", "Additive", "Modulate", "Alpha Composite", "Alpha Holdout" };
        static const bool s_BlendAvailable[] = { true, true, true, true, false, false, false };
        static_assert(IM_ARRAYSIZE(s_BlendNames) == IM_ARRAYSIZE(s_BlendAvailable),
            "Blend Mode: nomes e disponibilidade fora de sincronia");
        int blend = (int)m_Graph->BlendMode;
        drawDomainCombo("Blend Mode", s_BlendNames, s_BlendAvailable,
            IM_ARRAYSIZE(s_BlendNames), blend);
        m_Graph->BlendMode = (MaterialBlendMode)blend;

        // ── TWO_SIDED_V1 ─────────────────────────────────────────────────────
        //
        // Mesmo lugar em que a Unreal poe: ao lado do Blend Mode, porque e
        // disso que ele e vizinho — decide como a superficie e RASTERIZADA, e
        // nao o que o grafo calcula.
        //
        // So muda algo no translucido: a pipeline opaca desta engine ja
        // desenha as duas faces. A dica embaixo diz isso, para ninguem marcar
        // a caixa num material Opaque esperando mudanca.
        ImGui::Checkbox("Two Sided", &m_Graph->TwoSided);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Desenha as duas faces do triangulo, com a normal virada na de tras.\n"
                "Para plano de agua, folhagem, pano — malha de uma folha so.\n"
                "Deixe desligado no vidro: um volume fechado fica manchado sem o\n"
                "descarte da face de tras.\n\n"
                "Sem efeito em Blend Mode Opaque/Masked (ja desenham as duas).");

        // SHADING_MODEL_V1 — "Toon" e novo e REAL. A ordem aqui espelha o
        // enum MaterialShadingModel (node_types.hpp), incluindo o Toon no fim.
        static const char* s_ShadingNames[] = {
            "Default Lit", "Unlit", "Subsurface", "Clear Coat", "Preintegrated Skin",
            "Two Sided Foliage", "Hair", "Cloth", "Eye", "Single Layer Water",
            "Thin Translucent", "From Material Expression", "Toon" };
        static const bool s_ShadingAvailable[] = {
            true, true, false, false, false, false, false, false, false, false, false, false, true };
        static_assert(IM_ARRAYSIZE(s_ShadingNames) == IM_ARRAYSIZE(s_ShadingAvailable),
            "Shading Model: nomes e disponibilidade fora de sincronia");
        int shading = (int)m_Graph->ShadingModel;
        drawDomainCombo("Shading Model", s_ShadingNames, s_ShadingAvailable,
            IM_ARRAYSIZE(s_ShadingNames), shading);
        m_Graph->ShadingModel = (MaterialShadingModel)shading;

        // SHADING_MODEL_V1 — controles que so existem no Toon. Aparecem
        // condicionalmente porque um slider de bandas num material DefaultLit
        // seria um controle que nao faz nada, que e como o Shading Model
        // inteiro estava ate esta rodada.
        if (m_Graph->ShadingModel == MaterialShadingModel::Toon)
        {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderInt("##toonsteps", &m_Graph->ToonSteps, 2, 8, "Bandas: %d");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Degraus da luz difusa.\n"
                    "3 = celula classica (luz / meio-tom / sombra).\n"
                    "Acima de 8 ja e indistinguivel de sombreamento continuo.");

            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f),
                "Toon: o tamanho do brilho especular vem do\n"
                "Roughness — quanto mais liso, menor e mais duro.");

            ImGui::Spacing();
            ImGui::TextDisabled("Recompile para aplicar (Compile/Save).");
        }

        // ── WPO_V1 — recalculo da normal a partir do deslocamento ────────────
        //
        // So no dominio Surface: os outros nao tem estagio de vertice com
        // geometria real. Aparece SEMPRE que o dominio e Surface, e nao so
        // quando o pin esta ligado — o painel de parametros nao sabe o estado
        // das ligacoes, e um controle que some sem explicacao e pior que um
        // controle sem efeito.
        if (m_Graph->Domain == MaterialDomain::Surface)
        {
            ImGui::Spacing();
            ImGui::Checkbox("Recalcular Normal (WPO)", &m_Graph->RecomputeNormalFromWPO);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "So tem efeito com o pin 'World Position Offset' ligado.\n\n"
                    "Deslocar o vertice move a superficie mas NAO muda a normal:\n"
                    "a onda se mexe e continua recebendo luz como um plano.\n"
                    "Ligado, o vertex shader avalia o deslocamento em dois pontos\n"
                    "vizinhos e tira a normal da superficie deformada.\n\n"
                    "Custa 3 avaliacoes do subgrafo por vertice em vez de 1.\n\n"
                    "IMPORTANTE: a inclinacao e medida deslocando a POSICAO DE\n"
                    "MUNDO. Uma onda montada a partir de UV Coordinate da o mesmo\n"
                    "valor nos tres pontos e a normal sai plana — use o node\n"
                    "'World Position'. O Compile avisa no log quando isso acontece.\n\n"
                    "Deixe DESLIGADO em vento de folhagem: ali o deslocamento e\n"
                    "quase uma translacao, e a normal recalculada sairia errada.");

            if (m_Graph->RecomputeNormalFromWPO)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##wpodelta", &m_Graph->WPONormalDelta,
                    0.005f, 0.001f, 2.0f, "Delta: %.3f m");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Distancia, em unidades de mundo, entre os pontos de amostra.\n\n"
                        "Pequeno demais vira ruido de precisao; grande demais\n"
                        "atravessa a crista e achata a onda.\n"
                        "0.05 serve para agua em escala de metros;\n"
                        "onda de 20 cm pede algo perto de 0.01.");
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Recompile para aplicar (Compile/Save).");
        }

        if (m_Graph->Domain == MaterialDomain::LightFunction)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f),
                "Light Function: só o pin Emissive do Material Output\n"
                "é usado — os outros pins ficam acinzentados no grafo.");
        }

        // ── VOLUME_DOMAIN_V1 ─────────────────────────────────────────────────
        //
        // O texto e mais longo que o dos outros dominios de proposito: este e o
        // unico em que o corpo do grafo NAO e avaliado uma vez por pixel, e
        // quem nao souber disso vai montar contas caras sem perceber.
        if (m_Graph->Domain == MaterialDomain::Volume)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                "Volume: o grafo descreve o MEIO, e nao uma superfície.");
            ImGui::Spacing();
            ImGui::BulletText("Base Color -> cor que a névoa espalha");
            ImGui::BulletText("Opacity    -> densidade por metro");
            ImGui::BulletText("Emissive   -> luz que o próprio meio emite");
            ImGui::Spacing();
            ImGui::TextDisabled(
                "World Position é a posição da AMOSTRA no ar,\n"
                "avaliada uma vez por passo do raio (Ray Steps).\n"
                "Pixel Depth é a distância dessa amostra à câmera.\n\n"
                "Pino solto = o valor do Inspector (Cor do Fog,\n"
                "Densidade + queda por altura). Grafo vazio dá\n"
                "exatamente o fog de sempre.\n\n"
                "O contrário também vale: pino LIGADO apaga o\n"
                "campo do Inspector. Base Color apaga Cor do Fog;\n"
                "Opacity apaga Densidade, Height Base e Height\n"
                "Falloff (os três só vivem no fallback dele).\n"
                "Use o node 'Fog Settings' e multiplique por ele\n"
                "para os quatro voltarem a ter efeito.\n\n"
                "Sem mapa de altura aqui: Scene Height/Distance\n"
                "devolvem 'nada aqui'. É o passe de superfície que\n"
                "os liga, e ele já terminou quando o fog roda.\n\n"
                "O raio de luz atravessando a sombra é do passe,\n"
                "não do grafo — não precisa montar nada para ter.\n"
                "O node 'Sun Light' devolve quanto do sol chega a\n"
                "ESTE ponto do ar (0 sombra, 1 sol), para você usar\n"
                "no que quiser: névoa mais densa na sombra, poeira\n"
                "que só brilha dentro do feixe, cor mais fria fora.\n\n"
                "Precisa de 'Fog Ativo' marcado no Post Process,\n"
                "e do material escolhido em Volume Material.");
        }

        if (m_Graph->Domain == MaterialDomain::Particle)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f),
                "Particle: use Emissive (color) e Opacity.\n"
                "Vars disponíveis: v_UV, v_Color, v_Age01, u_Time.");
        }

        ImGui::Spacing();
        ui::SectionHeader(ICON_PALETTE, "Surface", ui::Accent::Neutral);

        // ═══════════════════════════════════════════════════════════════════
        //  GRAPH_OWNS_SURFACE_V1 — este bloco inteiro nao tem fio ligado
        //
        //  ── O SINTOMA QUE O CLEVER RELATOU ──────────────────────────────
        //
        //  Desmarcar "PBR", compilar, e ver a caixa voltar sozinha para
        //  marcada — sem aviso e sem jeito de manter desmarcada.
        //
        //  ── E O SINTOMA ESTAVA CERTO ────────────────────────────────────
        //
        //  `UsePBR = true` e forcado em DOIS pontos (CompileAndApply e ao
        //  abrir o editor), e forcar esta CORRETO: o shader que o
        //  MaterialCompiler gera e sempre o caminho PBR. O Blinn-Phong mora
        //  no shader FIXO do MeshRenderer, que um material de grafo nunca usa.
        //
        //  O defeito nao era o valor voltar — era a caixa existir. E com ela
        //  os seis campos abaixo: Color, Specular, Shininess, Metallic,
        //  Roughness e AO viram uniforms (u_Color, u_Metallic, ...) que o
        //  shader gerado NAO DECLARA. Mexer neles nunca mudou um pixel de um
        //  material de grafo.
        //
        //  Mesma familia do Shading Model antes do SHADING_MODEL_V1: menu sem
        //  fio ligado. A correcao ali foi ligar o fio; aqui nao ha fio a
        //  ligar, porque quem decide estes valores E O GRAFO — sao os pinos
        //  Base Color, Metallic, Roughness e AO do Material Output.
        //
        //  Ficam VISIVEIS e desabilitados, e nao escondidos: sumir sem
        //  explicacao mandaria o autor procurar onde o controle foi parar.
        // ═══════════════════════════════════════════════════════════════════
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f),
            "Quem manda aqui e o grafo.");
        ImGui::TextWrapped(
            "Os pinos Base Color, Metallic, Roughness e Ambient Occlusion do "
            "Material Output substituem estes campos. O shader gerado nem "
            "declara as uniforms deles.");
        ImGui::Spacing();

        ImGui::BeginDisabled(true);
        bool usePBR = mat.UsePBR;
        ImGui::Checkbox("PBR", &usePBR);
        ImGui::Separator();
        ImGui::DragFloat("Metallic", &mat.Metallic, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Roughness", &mat.Roughness, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("AO", &mat.AO, 0.01f, 0.0f, 1.0f);
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                "Desabilitado de proposito.\n\n"
                "Um material do Material Editor e sempre PBR: o shader gerado\n"
                "a partir do grafo nao tem o caminho Blinn-Phong, que vive no\n"
                "shader fixo do MeshRenderer.\n\n"
                "Estes campos so valem para material SEM grafo, aplicado\n"
                "direto num mesh pelo Inspector.");
    }

    // -------------------------------------------------------------------------
    // Texture slot
    // -------------------------------------------------------------------------


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

} // namespace axe