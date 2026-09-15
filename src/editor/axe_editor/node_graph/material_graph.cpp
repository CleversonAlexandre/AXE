#include "material_graph.hpp"
#include "axe/log/log.hpp"
#include <algorithm>   // MATFUNC_V1 — find_if no RebuildFunctionCallPins
#include <nlohmann/json.hpp>
#include "axe/asset/asset_database.hpp"
namespace axe
{

    MaterialGraph::MaterialGraph()
    {
        // Cria o Output node por padrão
       // AddOutputNode();
        //AddMaterialOutputNode();
    }

    Node* MaterialGraph::AddMaterialOutputNode()
    {
        // Só permite um Material Output
        if (m_MaterialOutputNode)
        {
            AXE_EDITOR_WARN("MaterialGraph: Já existe um Material Output node!");
            return m_MaterialOutputNode;
        }

        auto node = std::make_unique<Node>(GetNextID(), "Material Output");
        node->Color = ImVec4(0.8f, 0.2f, 0.2f, 1.0f); // verde escuro

        // Pins de entrada
        node->Inputs.emplace_back(GetNextID(), "Base Color", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Metallic", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Roughness", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Normal", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Emissive", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Opacity", PinType::Float, ed::PinKind::Input);
        // Novos: índices 6 (AO) e 7 (Specular) — ver MaterialCompiler.
        // IMPORTANTE: sempre adicionar no FIM para não deslocar os índices
        // dos pins existentes (o compilador referencia por índice).
        node->Inputs.emplace_back(GetNextID(), "Ambient Occlusion", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Specular", PinType::Float, ed::PinKind::Input);

        // ── WPO_V1 — indice 8, World Position Offset ─────────────────────────
        //
        //  O UNICO pin do Material Output que NAO e resolvido no fragmento:
        //  ele alimenta o estagio de VERTICE. Deslocamento em ESPACO DE MUNDO,
        //  somado a posicao ja transformada pelo u_Model — mesma semantica do
        //  pin homonimo da Unreal, e a razao pela qual escalar ou girar o
        //  objeto nao distorce a onda.
        //
        //  Continua valendo a regra dos indices: pin novo SEMPRE no fim. O
        //  Deserialize casa os `input_ids` salvos por POSICAO e para no menor
        //  dos dois tamanhos, entao um `.axegraph` gravado com 8 pins abre
        //  aqui com este nono simplesmente desconectado.
        node->Inputs.emplace_back(GetNextID(), "World Position Offset", PinType::Vec3, ed::PinKind::Input);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddTextureSampleNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Texture Sample");
        node->Color = ImVec4(0.35f, 0.18f, 0.18f, 1.0f); // vermelho escuro

        // ── TEXSAMPLE_UV_V1 — este pino sempre foi a UV, com o nome errado ──
        //
        //  Ele nascia "Texture" e tipado Texture2D, mas o compilador SEMPRE o
        //  leu como fonte de UV (ver a emissao do Texture Sample: pega
        //  GetSourcePin(&node->Inputs[0]) e usa como coordenada). A textura em
        //  si nunca passou por aqui — ela vem do painel, por
        //  node->Value.TextureUUID.
        //
        //  E o tipo errado nao era cosmetico: a validacao de link recusa
        //  ligacao entre tipos diferentes (com excecao so para Any e Vec3), e
        //  NENHUM node produz saida Texture2D. Ou seja, o pino era
        //  impossivel de conectar — a UV do Texture Sample ficava presa em
        //  v_TexCoord para sempre.
        //
        //  Isso bloqueava o caso mais comum de textura em material: UV com
        //  tiling e rolagem (Texture Coordinate / Panner). Sem ele nao ha
        //  normal map de agua, que precisa de duas camadas rolando em
        //  velocidades diferentes.
        //
        //  Trocar tipo e nome nao quebra grafo salvo: como nada podia estar
        //  ligado nele, nao ha link a remapear, e a CONTAGEM de pinos nao
        //  muda — o Deserialize casa `input_ids` por posicao.
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "RGBA", PinType::Vec4, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "RGB", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "R", PinType::Float, ed::PinKind::Output);
        // Canal Alpha separado — útil como máscara de Opacity sem precisar
        // de uma textura dedicada: basta empacotar a máscara no alpha de
        // uma textura já existente (ex: o canal alpha do Base Color ou do
        // AO, que normalmente fica sem uso).
        node->Outputs.emplace_back(GetNextID(), "A", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddNormalMapNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Normal Map");
        node->Color = ImVec4(0.2f, 0.3f, 0.5f, 1.0f); // azul

        node->Inputs.emplace_back(GetNextID(), "Texture", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Strength", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;

        node->Outputs.emplace_back(GetNextID(), "Normal", PinType::Vec3, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddColorNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Color");
        node->Color = ImVec4(0.18f, 0.18f, 0.35f, 1.0f); // azul escuro
        node->Value.Type = PinType::Vec4;
        node->Value.Vec4Val = { 1.0f, 1.0f, 1.0f, 1.0f };
        node->IsConstant = true;

        node->Outputs.emplace_back(GetNextID(), "RGBA", PinType::Vec4, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "RGB", PinType::Vec3, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }
    Node* MaterialGraph::FindNodeByID(int id)
    {
        for (auto& node : m_Nodes)
        {
            if (node->ID.Get() == id)
                return node.get();
        }
        return nullptr;

    }

    bool MaterialGraph::CanCreateLink(Pin* a, Pin* b)
    {
        if (!a || !b || a == b) return false;
        if (a->Kind == b->Kind) return false;
        if (a->ParentNode == b->ParentNode) return false;

        // Any aceita qualquer tipo
        if (a->Type == PinType::Any || b->Type == PinType::Any) return true;

        return a->Type == b->Type;
    }


    Node* MaterialGraph::AddFloatNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Float");
        node->IsConstant = true;
        node->Color = ImVec4(0.25f, 0.25f, 0.10f, 1.0f); // amarelo escuro
        node->Value.Type = PinType::Float;
        node->Value.FloatVal = 0.0f;

        node->Outputs.emplace_back(GetNextID(), "Value", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddComment()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Comment");
        node->Color = ImVec4(159 / 255.0f, 159 / 255.0f, 159 / 255.0f, 1.0f);
        node->Type = NodeType::Comment;
        node->Size = ImVec2(300, 200);
        node->StringValue = ""; // exibe "Comment" como placeholder até o usuário nomear


        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }
    Node* MaterialGraph::AddMultiplyNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Multiply");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f); //Verde Escuro

        //Dois inputs (A e B)
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;

        //Um output (resultado)
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddAddNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Add");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f); //Verde Escuro

        node->Inputs.emplace_back(GetNextID(), "A", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Float, ed::PinKind::Input);

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddLerpNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Lerp");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f); //Verde Escuro

        node->Inputs.emplace_back(GetNextID(), "A", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Inputs.emplace_back(GetNextID(), "Alpha", PinType::Float, ed::PinKind::Input).DefaultFloat = 0.5f;

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddUVNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "UV Coordinate");
        node->Color = ImVec4(0.5f, 0.4f, 0.2f, 1.0f); // Laranja

        // Sem inputs, apenas outputs
        node->Outputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Output);
        // U e V isolados — úteis pra máscaras baseadas em UV (ex: opacity
        // por altura na própria malha, consistente em todas as instâncias
        // do objeto, diferente de World Position que é absoluto no mundo).
        node->Outputs.emplace_back(GetNextID(), "U", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "V", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddSubtractNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Subtract");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f); //Verde Escuro

        node->Inputs.emplace_back(GetNextID(), "A", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Float, ed::PinKind::Input);

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddDivideNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Divide");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f); //Verde Escuro

        node->Inputs.emplace_back(GetNextID(), "A", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddPowerNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Power");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f); //Verde Escuro

        node->Inputs.emplace_back(GetNextID(), "A", PinType::Any, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Float, ed::PinKind::Input).DefaultFloat = 2.0f;

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    void MaterialGraph::AddLink(ed::PinId startPin, ed::PinId endPin)
    {
        m_Links.emplace_back(GetNextID(), startPin, endPin);

        //AXE_EDITOR_INFO("AddLink: {} -> {} | Total links: {}",
            //startPin.Get(), endPin.Get(), m_Links.size());
    }

    void MaterialGraph::RemoveLink(ed::LinkId id)
    {
        //AXE_EDITOR_INFO("RemoveLink: {} | Total antes: {}", id.Get(), m_Links.size());

        m_Links.erase(
            std::remove_if(m_Links.begin(), m_Links.end(),
                [id](const Link& l) { return l.ID == id; }),
            m_Links.end()
        );

        AXE_EDITOR_INFO("RemoveLink: total depois: {}", m_Links.size());
    }

    // CUSTOM_NODE_V1 — ver a nota na declaracao.
    void MaterialGraph::RemoveLinksForPin(ed::PinId pin)
    {
        m_Links.erase(
            std::remove_if(m_Links.begin(), m_Links.end(),
                [pin](const Link& l) { return l.StartPin == pin || l.EndPin == pin; }),
            m_Links.end()
        );
    }

    Node* MaterialGraph::AddClampNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Clamp");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);

        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Any, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Min", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Max", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // MATFUNC_V1 — a peca que faltava para QUALQUER padrao repetido.
    // fract() e o que transforma uma rampa continua (profundidade, distancia,
    // tempo) numa serie de faixas: cada vez que a rampa passa de um inteiro,
    // o valor volta a zero. Sem ele nao ha listra, anel, degrau nem faixa
    // periodica nenhuma no grafo — so gradiente.
    Node* MaterialGraph::AddFractNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Fract");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);

        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Any, ed::PinKind::Input);

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddAbsNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Abs");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);

        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Any, ed::PinKind::Input);

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }
    Node* MaterialGraph::AddRerouteNode()
    {
        // "Knot" puramente visual pra dobrar/organizar fios. Pins Any em
        // ambos os lados (CanCreateLink já aceita Any com tudo) e o
        // compilador é transparente a ele (segue pro input até a fonte real),
        // então não altera o resultado — só a aparência do grafo.
        auto node = std::make_unique<Node>(GetNextID(), "Reroute");
        node->Inputs.emplace_back(GetNextID(), "", PinType::Any, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "", PinType::Any, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddOneMinusNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "OneMinus");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);

        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Any, ed::PinKind::Input);

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // ── WPO_V1 — Vertex Normal ───────────────────────────────────────────────
    //
    //  A normal GEOMETRICA da superficie, em espaco de mundo.
    //
    //  ── POR QUE ESTE NODE PRECISA EXISTIR ──────────────────────────────────
    //
    //  Nao havia como LER a normal no grafo. `Fresnel` a consome por dentro,
    //  `Normal Map` a usa para montar o espaco tangente — mas nenhum node a
    //  devolvia como valor, entao nao dava para fazer conta com ela.
    //
    //  E o companheiro obrigatorio do World Position Offset: "empurre a
    //  superficie ao longo da propria normal" e a forma mais comum de
    //  deslocamento (onda, inflar, extrudar casca de contorno), e sem este
    //  node ela so era possivel para malha cuja normal o autor ja sabe de cor.
    //  E o `VertexNormalWS` da Unreal.
    //
    //  ── O QUE ELE NAO E ────────────────────────────────────────────────────
    //
    //  Nao e a normal DEPOIS do Normal Map, e nem a virada do Two Sided: e a
    //  interpolada da malha, a mesma nos dois estagios. No vertice ela e a
    //  unica que existe (nao ha mapa de normal antes do fragmento), e essa
    //  igualdade e o que faz um mesmo subgrafo dar o mesmo resultado ligado no
    //  WPO ou no Base Color.
    Node* MaterialGraph::AddVertexNormalNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Vertex Normal");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f); // mesma familia do World Position

        node->Outputs.emplace_back(GetNextID(), "XYZ", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "X", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Y", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Z", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddWorldPositionNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "World Position");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f); // laranja escuro

        // Sem inputs — só expõe v_FragPos
        node->Outputs.emplace_back(GetNextID(), "XYZ", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "X", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Y", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Z", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // ── WATER_NODES_V1 ──────────────────────────────────────────────────
    //
    // A posicao da CAMERA no mundo. Irmao do World Position, e o par que
    // faltava: com os dois, `Distance` da qualquer efeito por distancia.
    Node* MaterialGraph::AddCameraPositionNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Camera Position");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f); // mesma familia do World Position

        node->Outputs.emplace_back(GetNextID(), "XYZ", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "X", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Y", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Z", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // Distancia da camera ate ESTE pixel, em metros — o par exato do
    // Scene Depth, que da a distancia ate o que esta ATRAS dele.
    //
    // Existe como node proprio, e nao como Distance(World Position, Camera
    // Position), porque e a conta mais repetida de todo material que reage a
    // profundidade: a subtracao `Scene Depth - Pixel Depth` e a espessura de
    // agua, a borda de intersecao e o comeco de qualquer nevoa.
    // ── SCENE_HEIGHT_V1 ──────────────────────────────────────────────────────
    //
    //  Le o mapa de topo da cena — a render ortografica de cima que o
    //  SceneHeightPass produz. E a unica fonte do grafo independente de camera.
    //
    //  A entrada e uma POSICAO DE MUNDO, e nao uma UV de tela, e a diferenca e
    //  o ponto todo: com ela da para perguntar "o que ha embaixo DAQUELE ponto
    //  ali", inclusive de um lugar que a camera nao enxerga. Solta, vale a
    //  posicao do proprio pixel.
    Node* MaterialGraph::AddSceneHeightNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Scene Height");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);

        node->Inputs.emplace_back(GetNextID(), "World Position", PinType::Vec3, ed::PinKind::Input);

        node->Outputs.emplace_back(GetNextID(), "Height", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Distance", PinType::Float, ed::PinKind::Output);

        // SCENE_HEIGHT_V2 — a altura da geometria MAIS PROXIMA, e nao a deste
        // ponto. E o que separa uma margem de um objeto pairando sobre a agua.
        // Entra no FIM da lista: o Load remapeia pino por posicao parando no
        // menor dos dois tamanhos, entao material salvo antes disto abre sem
        // deslocar ligacao nenhuma.
        // SCENE_HEIGHT_V3 — a BASE, nao o topo. Ver axeSceneNearestBase.
        node->Outputs.emplace_back(GetNextID(), "Nearest Base", PinType::Float, ed::PinKind::Output);

        // SCENE_HEIGHT_V4 — o topo DA MESMA geometria vizinha. Com a base,
        // permite ao grafo perguntar se aquela coluna atravessa a lamina, em
        // vez de so "desce ate ela".
        //
        // No FIM da lista: Deserialize remapeia pino por posicao, entao um
        // .axegraph salvo antes disto continua abrindo com as tres primeiras
        // saidas intactas.
        node->Outputs.emplace_back(GetNextID(), "Nearest Top", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddSceneWorldPositionNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Scene World Position");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);

        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);

        node->Outputs.emplace_back(GetNextID(), "Position", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "X", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Y", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Z", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddPixelDepthNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Pixel Depth");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f); // familia dos nodes de tela

        node->Outputs.emplace_back(GetNextID(), "Dist", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddFresnelNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Fresnel");
        node->Color = ImVec4(0.1f, 0.3f, 0.5f, 1.0f); // azul escuro

        node->Inputs.emplace_back(GetNextID(), "Exponent", PinType::Float, ed::PinKind::Input).DefaultFloat = 5.0f;
        node->Inputs.emplace_back(GetNextID(), "Normal", PinType::Vec3, ed::PinKind::Input);

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // =========================================================================
    // Lote inspirado na Unreal — math/vector utilities
    // =========================================================================

    Node* MaterialGraph::AddSineNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Sine");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Float, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddCosineNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Cosine");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Float, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddStepNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Step");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Edge", PinType::Float, ed::PinKind::Input).DefaultFloat = 0.5f;
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Float, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddSmoothStepNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "SmoothStep");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Min", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Max", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Float, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddNormalizeNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Normalize");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Vec3, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddDistanceNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Distance");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Vec3, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddDotProductNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "DotProduct");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Vec3, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddDesaturateNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Desaturate");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Color", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Fraction", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddAppendNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Append");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f);
        // PRIMITIVES_V1 — os pinos eram "A (Vec3)" + "B (Float)" -> "Result
        // (Vec4)". O nome dizia a verdade: era um "Vec3 mais W", nao um
        // Append. Agora e Any/Any -> Any, e o tipo real sai da conta de
        // componentes no compilador.
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Any, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Any, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddVectorSplitNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Vector Split");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f);
        // PRIMITIVES_V1 — aceita qualquer vetor, e ganha o W.
        //
        // O W entra no FIM da lista de saidas: Load() remapeia pino por
        // posicao parando no menor dos dois tamanhos, entao um material salvo
        // antes disto abre sem deslocar ligacao nenhuma.
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Any, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "X", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Y", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Z", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "W", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddCameraVectorNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Camera Vector");
        node->Color = ImVec4(0.1f, 0.3f, 0.5f, 1.0f);
        // Sem inputs — só expõe a direção da câmera (view direction)
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddReflectionVectorNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Reflection Vector");
        node->Color = ImVec4(0.1f, 0.3f, 0.5f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Normal", PinType::Vec3, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // -------------------------------------------------------------------
    // Animação — usam o uniform u_Time, atualizado por frame pelo renderer
    // -------------------------------------------------------------------

    Node* MaterialGraph::AddTimeNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Time");
        node->Color = ImVec4(0.5f, 0.1f, 0.4f, 1.0f); // magenta escuro
        // Sem inputs — só expõe o tempo de execução em segundos
        node->Outputs.emplace_back(GetNextID(), "Seconds", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddParticleAgeNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Particle Age");
        node->Color = ImVec4(0.1f, 0.55f, 0.4f, 1.0f);
        node->Outputs.emplace_back(GetNextID(), "Age 0-1", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddParticleColorNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Particle Color");
        node->Color = ImVec4(0.1f, 0.55f, 0.4f, 1.0f); // verde-teal (família Particle)
        // Expõe v_Color (vec4) — a cor interpolada da partícula entre
        // ColorStart e ColorEnd ao longo da vida. Use o pin RGB pra tingir
        // uma textura, e o Alpha pra controlar a opacidade por partícula.
        node->Outputs.emplace_back(GetNextID(), "RGBA", PinType::Vec4, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "RGB", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Alpha", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddPannerNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Panner");
        node->Color = ImVec4(0.5f, 0.1f, 0.4f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Speed X", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Inputs.emplace_back(GetNextID(), "Speed Y", PinType::Float, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // -------------------------------------------------------------------
    // Mais math/vetor/constantes
    // -------------------------------------------------------------------

    Node* MaterialGraph::AddMinNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Min");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Any, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Any, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddMaxNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Max");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Any, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Any, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddSaturateNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Saturate");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Any, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddLengthNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Length");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Vec3, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddCrossProductNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "CrossProduct");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Vec3, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddIfNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "If");
        node->Color = ImVec4(0.5f, 0.45f, 0.1f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "A > B", PinType::Any, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "A == B", PinType::Any, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "A < B", PinType::Any, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddNoiseNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Noise");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);

        // NOISE_SMOOTH_V1 — pinos NOVOS, e eles entram no FIM da lista.
        //
        // Load() remapeia pino por POSICAO, com o laco parando no menor dos
        // dois tamanhos (`i < ids.size() && i < node->Inputs.size()`). Um
        // .axegraph salvo antes desta versao guarda um unico input_id (UV): o
        // laco para nele e Scale/Detail ficam com o default daqui, sem que
        // nenhuma ligacao existente se desloque. Inserir no MEIO empurraria
        // todas as posicoes seguintes e reconectaria fios sozinho — por isso
        // pino novo em node que ja foi salvo em disco vai sempre no fim.
        //
        // Scale existe porque a UV de uma malha vai de 0 a 1: sem multiplicar,
        // o ruido cabe uma celula inteira dentro do objeto e sai quase liso.
        node->Inputs.emplace_back(GetNextID(), "Scale", PinType::Float, ed::PinKind::Input).DefaultFloat = 8.0f;
        node->Inputs.emplace_back(GetNextID(), "Detail", PinType::Float, ed::PinKind::Input).DefaultFloat = 3.0f;

        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // =========================================================================
    //  MATFUNC_V1 — os tres nodes de Material Function
    // =========================================================================

    Node* MaterialGraph::AddFunctionInputNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Function Input");
        node->Color = ImVec4(0.30f, 0.70f, 0.45f, 1.0f);
        node->StringValue = "In";
        node->CustomOutputType = PinType::Float;

        node->Outputs.emplace_back(GetNextID(), "In", PinType::Float, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddFunctionOutputNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Function Output");
        node->Color = ImVec4(0.70f, 0.30f, 0.35f, 1.0f);
        node->StringValue = "Out";
        node->CustomOutputType = PinType::Float;

        node->Inputs.emplace_back(GetNextID(), "Out", PinType::Float, ed::PinKind::Input);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddMaterialFunctionNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Material Function");
        node->Color = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);

        // Nasce SEM asset e SEM pino nenhum. Os pinos so existem depois que o
        // autor escolhe o `.axematfunc` no painel de detalhes — sao a
        // assinatura da funcao escolhida, e nao ha pino generico que faca
        // sentido antes disso.
        node->StringValue.clear();

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    void MaterialGraph::SyncFunctionIONode(Node* node)
    {
        if (!node) return;

        if (node->Name == "Function Input" && !node->Outputs.empty())
        {
            node->Outputs[0].Name = node->StringValue;
            node->Outputs[0].Type = node->CustomOutputType;
        }
        else if (node->Name == "Function Output" && !node->Inputs.empty())
        {
            node->Inputs[0].Name = node->StringValue;
            node->Inputs[0].Type = node->CustomOutputType;
        }
    }

    void MaterialGraph::RebuildFunctionCallPins(Node* node,
        const std::vector<MaterialFunctionParam>& inputs,
        const std::vector<MaterialFunctionParam>& outputs)
    {
        if (!node) return;

        auto sync = [&](std::vector<Pin>& pins,
            const std::vector<MaterialFunctionParam>& want,
            ed::PinKind kind)
            {
                std::vector<Pin> rebuilt;
                rebuilt.reserve(want.size());

                for (const auto& w : want)
                {
                    auto it = std::find_if(pins.begin(), pins.end(),
                        [&](const Pin& p) { return p.Name == w.Name; });

                    if (it != pins.end())
                    {
                        // Mesmo ID -> o link ligado nele sobrevive. So o tipo
                        // acompanha a assinatura nova.
                        Pin kept = *it;
                        kept.Type = w.Type;
                        rebuilt.push_back(kept);
                        pins.erase(it);
                    }
                    else
                    {
                        rebuilt.push_back(Pin(GetNextID(), w.Name.c_str(), w.Type, kind));
                    }
                }

                // O que sobrou nao existe mais na assinatura. O link tem que
                // ir junto, ANTES do pino sumir.
                for (auto& dead : pins)
                    RemoveLinksForPin(dead.ID);

                pins = std::move(rebuilt);
            };

        sync(node->Inputs, inputs, ed::PinKind::Input);
        sync(node->Outputs, outputs, ed::PinKind::Output);

        // Sem isto o pino novo fica com ParentNode nulo e CanCreateLink recusa
        // a ligacao — a mesma pegadinha ja documentada no node Custom.
        BuildNodes();
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  PRIMITIVES_V1 — builtins de GLSL que faltavam no grafo
    //
    //  Cada uma e 1:1 com uma funcao da linguagem e preserva o tipo do que
    //  entra. Node assim nao e receita embutida: e a linguagem exposta no
    //  grafo. Sem elas, arredondar, quantizar ou tirar raiz obrigava a cair no
    //  node Custom e escrever GLSL a mao.
    // ═════════════════════════════════════════════════════════════════════════
    static Node* MakeUnaryMathNode(std::vector<std::unique_ptr<Node>>& nodes,
        MaterialGraph& graph, const char* name)
    {
        auto node = std::make_unique<Node>(graph.GetNextID(), name);
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(graph.GetNextID(), "Value", PinType::Any, ed::PinKind::Input);
        node->Outputs.emplace_back(graph.GetNextID(), "Result", PinType::Any, ed::PinKind::Output);
        auto* ptr = node.get();
        nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddFloorNode() { return MakeUnaryMathNode(m_Nodes, *this, "Floor"); }
    Node* MaterialGraph::AddCeilNode() { return MakeUnaryMathNode(m_Nodes, *this, "Ceil"); }
    Node* MaterialGraph::AddRoundNode() { return MakeUnaryMathNode(m_Nodes, *this, "Round"); }
    Node* MaterialGraph::AddSqrtNode() { return MakeUnaryMathNode(m_Nodes, *this, "Sqrt"); }
    Node* MaterialGraph::AddSignNode() { return MakeUnaryMathNode(m_Nodes, *this, "Sign"); }

    Node* MaterialGraph::AddModNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Mod");
        node->Color = ImVec4(0.2f, 0.4f, 0.2f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "A", PinType::Any, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B", PinType::Any, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Any, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddVec2Node()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Vec2");
        node->Color = ImVec4(0.15f, 0.15f, 0.45f, 1.0f);
        node->IsConstant = true;
        node->Value.Type = PinType::Vec2;
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Vec2, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddVec3Node()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Vec3");
        node->Color = ImVec4(0.15f, 0.15f, 0.45f, 1.0f);
        node->IsConstant = true;
        node->Value.Type = PinType::Vec3;
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddTextureCoordinateNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Texture Coordinate");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "U Tiling", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Inputs.emplace_back(GetNextID(), "V Tiling", PinType::Float, ed::PinKind::Input).DefaultFloat = 1.0f;
        node->Inputs.emplace_back(GetNextID(), "U Offset", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "V Offset", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Rotation", PinType::Float, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  POSTPROCESS_DOMAIN_V1 — leitura da imagem da cena
    //
    //  So fazem sentido no dominio Post Process. Fora dele compilam para preto
    //  (ver GenerateNodeCode): um node que some do menu conforme o dominio
    //  esconde do usuario que ele existe; um node que compila para um valor
    //  definido apenas nao faz nada, e isso e inspecionavel.
    //
    //  A UV NAO e um pin obrigatorio: desconectada, le o proprio pixel. Ligada,
    //  le OUTRO pixel — e e assim que se escreve blur, aberracao cromatica,
    //  distorcao e pixelizacao. Sem essa entrada, o dominio inteiro se
    //  limitaria a ajustes de cor por pixel.
    // ═════════════════════════════════════════════════════════════════════════
    Node* MaterialGraph::AddSceneColorNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Scene Color");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "RGB", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // POSTPROCESS_GBUFFER_V1 — os tres leem o G-Buffer. Todos com pin de UV
    // opcional pelo mesmo motivo do Scene Color: sem poder amostrar OUTRO
    // pixel nao existe deteccao de borda, blur, nem distorcao.
    Node* MaterialGraph::AddSceneDepthNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Scene Depth");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Dist", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddSceneNormalNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Scene Normal");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Normal", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddSceneShadingModelNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Scene Shading Model");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "ID", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // POSTPROCESS_SKY_V1 — o que faltava para o CEU ser autoravel no grafo.
    Node* MaterialGraph::AddSceneIsBackgroundNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Scene Is Background");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Is Sky", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddScreenRayDirectionNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Screen Ray Direction");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Dir", PinType::Vec3, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddSunNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Sun");
        node->Color = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
        // Sem entradas: e um dado do frame, nao uma operacao.
        node->Outputs.emplace_back(GetNextID(), "Direction", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "To Sun", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Color", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Intensity", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // ── VOLUME_SUN_V2b — Fog Settings ────────────────────────────────────────
    //
    //  Os valores que o autor ajusta no Inspector, como VALORES DO GRAFO.
    //
    //  Por que ele existe, e por que ele nao e um node "de tapar buraco": ao
    //  ligar Opacity e Base Color num material de Volume, QUATRO controles do
    //  Inspector — Densidade, Cor do Fog, Height Base e Height Falloff — param
    //  de fazer efeito. Nao e bug: pino ligado e o grafo que manda, e o driver
    //  ate remove as uniforms que ninguem le. Mas o resultado pratico e um
    //  painel com quatro botoes mortos, e quem os gira nao tem como saber.
    //
    //  Havia dois consertos possiveis. Esconder os campos seria tirar do
    //  artista o ajuste fino que ele quer ter em cena, e obrigaria o Inspector
    //  a saber quais pinos do grafo estao ligados — que ele nao sabe e nao
    //  deveria saber. O outro e este: devolver os valores AO GRAFO, e deixar o
    //  autor escrever `Fog Settings > Density * ruido -> Opacity`. Ai o slider
    //  volta a funcionar porque ELE decidiu que funciona, e o painel deixa de
    //  competir com o grafo pela mesma decisao.
    //
    //  Sem entradas: e dado do frame, como o node Sun.
    Node* MaterialGraph::AddFogSettingsNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Fog Settings");
        node->Color = ImVec4(0.55f, 0.70f, 0.85f, 1.0f);
        node->Outputs.emplace_back(GetNextID(), "Density", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Fog Color", PinType::Vec3, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Height Base", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Height Falloff", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // ── VOLUME_SUN_V2 — Sun Light ────────────────────────────────────────────
    //
    //  "Quanto do sol chega a ESTE ponto?" — 0 na sombra, 1 no sol.
    //
    //  Justificativa sob a regra de nao criar node redundante: NAO ha como o
    //  grafo responder isso hoje. O node Sun devolve a direcao, a cor e a
    //  intensidade da luz, mas nenhum deles sabe se ha uma pedra no caminho; o
    //  mapa de sombra nunca esteve exposto ao grafo em dominio nenhum. E
    //  informacao nova, e nao uma combinacao de nodes que ja existem.
    //
    //  O que ela abre: nevoa mais densa na sombra (a bruma fria embaixo da
    //  arvore), poeira que so brilha dentro do raio de luz, cor mais fria fora
    //  do sol. Todos casos em que o AUTOR decide o que a sombra faz — que e o
    //  oposto do contorno toon embutido que ele recusou.
    //
    //  Entrada de posicao OPCIONAL: solta usa o ponto que esta sendo avaliado
    //  (a amostra do ray march). Ligada, permite perguntar por outro ponto —
    //  por exemplo, amostrar um metro acima para saber se o topo da coluna de
    //  ar pega sol.
    Node* MaterialGraph::AddSunLightNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Sun Light");
        node->Color = ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "World Position", PinType::Vec3, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Lit", PinType::Float, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddSceneUVNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Screen UV");
        node->Color = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
        node->Outputs.emplace_back(GetNextID(), "UV", PinType::Vec2, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Pixel Size", PinType::Vec2, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  CUSTOM_NODE_V1
    //
    //  Nasce com UMA entrada e saida Float — o menor node que ja faz alguma
    //  coisa. O codigo inicial nao e placeholder vazio de proposito: um node
    //  recem-criado ja compila e ja produz imagem, entao o primeiro contato do
    //  usuario com ele nao e um erro de shader.
    // ═════════════════════════════════════════════════════════════════════════
    Node* MaterialGraph::AddCustomNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Custom");
        node->Color = ImVec4(0.75f, 0.45f, 0.15f, 1.0f);   // ambar — "aqui tem codigo"

        node->Inputs.emplace_back(GetNextID(), "In", PinType::Float, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Out", PinType::Float, ed::PinKind::Output);

        node->CustomOutputType = PinType::Float;
        node->CustomCode = "return In;";

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Pin* MaterialGraph::FindPin(ed::PinId id)
    {
        for (auto& node : m_Nodes)
        {
            for (auto& pin : node->Inputs)
                if (pin.ID == id) return &pin;
            for (auto& pin : node->Outputs)
                if (pin.ID == id) return &pin;
        }
        return nullptr;
    }

    bool MaterialGraph::IsPinLinked(ed::PinId id) const
    {
        for (auto& link : m_Links)
            if (link.StartPin == id || link.EndPin == id)
                return true;
        return false;
    }

    std::unique_ptr<Node>* MaterialGraph::FindNode(ed::NodeId id)
    {
        for (auto& node : m_Nodes)
            if (node->ID == id)
                return &node;

        return nullptr;
    }
    void MaterialGraph::BuildNode(std::unique_ptr<Node>* node)
    {
        auto currNode = node->get();

        for (auto& input : currNode->Inputs)
        {
            input.ParentNode = currNode;
            input.Kind = ed::PinKind::Input;
        }
        for (auto& input : currNode->Outputs)
        {
            input.ParentNode = currNode;
            input.Kind = ed::PinKind::Output;
        }
    }

    void MaterialGraph::BuildNodes()
    {
        for (auto& node : m_Nodes)
            BuildNode(&node);
    }

    nlohmann::json MaterialGraph::Serialize() const
    {
        nlohmann::json j;

        j["nodes"] = nlohmann::json::array();
        for (const auto& node : m_Nodes)
        {
            nlohmann::json nodeJson;
            nodeJson["id"] = node->ID.Get();
            nodeJson["name"] = node->Name;
            nodeJson["type"] = (int)node->Type;

            // Posição no canvas
            //ImVec2 pos = ed::GetNodePosition(node->ID);
            ImVec2 pos = GetNodePosition(node->ID.Get());
            nodeJson["pos_x"] = pos.x;
            nodeJson["pos_y"] = pos.y;

            // Tamanho (comments)
            nodeJson["size_x"] = node->Size.x;
            nodeJson["size_y"] = node->Size.y;

            // Texto e cor do Comment (Name continua sendo "Comment" sempre)
            if (node->Type == NodeType::Comment)
            {
                nodeJson["comment_text"] = node->StringValue;
                nodeJson["comment_color"] = {
                    node->CommentColor[0], node->CommentColor[1], node->CommentColor[2] };
            }

            // Valores constantes (Float, Color)
            if (node->IsConstant)
            {
                nodeJson["value_type"] = (int)node->Value.Type;

                if (node->Value.Type == PinType::Float)
                    nodeJson["value_float"] = node->Value.FloatVal;
                else if (node->Value.Type == PinType::Vec2)
                    nodeJson["value_vec2"] = { node->Value.Vec2Val.x, node->Value.Vec2Val.y };
                else if (node->Value.Type == PinType::Vec3)
                    nodeJson["value_vec3"] = {
                        node->Value.Vec3Val.x, node->Value.Vec3Val.y, node->Value.Vec3Val.z };
                else if (node->Value.Type == PinType::Vec4)
                    nodeJson["value_vec4"] = {
                        node->Value.Vec4Val.x, node->Value.Vec4Val.y,
                        node->Value.Vec4Val.z, node->Value.Vec4Val.w
                };
            }

            // Textura
            if (!node->Value.TextureUUID.empty())
                nodeJson["texture_uuid"] = node->Value.TextureUUID;

            // CUSTOM_NODE_V1 — codigo, tipo de saida e a LISTA de entradas.
            //
            // Este e o unico node cujos pins nao sao deduziveis do nome: a
            // fabrica cria um "In" Float, e o usuario pode ter cinco entradas
            // com outros nomes e tipos. Sem gravar a lista, reabrir o material
            // devolveria um node de uma entrada — e os links das outras, que
            // sao remapeados POR POSICAO, cairiam no pin errado ou sumiriam.
            if (node->Name == "Custom")
            {
                nodeJson["custom_code"] = node->CustomCode;
                nodeJson["custom_output_type"] = (int)node->CustomOutputType;

                nlohmann::json customInputs = nlohmann::json::array();
                for (auto& pin : node->Inputs)
                {
                    nlohmann::json in;
                    in["name"] = pin.Name;
                    in["type"] = (int)pin.Type;
                    customInputs.push_back(in);
                }
                nodeJson["custom_inputs"] = customInputs;
            }

            // IDs dos pins — necessários para reconstruir os links
            nlohmann::json inputIds = nlohmann::json::array();
            nlohmann::json outputIds = nlohmann::json::array();
            nlohmann::json inputDefaults = nlohmann::json::array();
            for (auto& pin : node->Inputs)
            {
                inputIds.push_back(pin.ID.Get());
                inputDefaults.push_back(pin.DefaultFloat);
            }
            // ── MATFUNC_V1 ───────────────────────────────────────────────
            //
            // Function Input/Output: nome e tipo do parametro. Sao os campos
            // livres do Node (StringValue, CustomOutputType) e, como o
            // comment_text e o custom_code, so vao ao JSON para os nodes que
            // os usam — gravar incondicionalmente encheria de chave morta
            // todo node do arquivo.
            if (node->Name == "Function Input" || node->Name == "Function Output")
            {
                nodeJson["func_param_name"] = node->StringValue;
                nodeJson["func_param_type"] = PinTypeToString(node->CustomOutputType);
            }
            // Material Function: o UUID do asset E a assinatura em cache.
            //
            // A assinatura vai junto de proposito, mesmo sendo copia do que
            // esta no `.axematfunc`. Sem ela, abrir um material cujo asset de
            // funcao foi movido, renomeado ou ainda nao indexado faria o node
            // voltar SEM PINO NENHUM — e todos os links dele sumiriam no
            // Deserialize, em silencio, destruindo o grafo do autor por causa
            // de um arquivo temporariamente ausente. Com o cache, o node volta
            // inteiro, os fios ficam, e o que aparece e um aviso.
            else if (node->Name == "Material Function")
            {
                nodeJson["func_uuid"] = node->StringValue;

                nlohmann::json fnIn = nlohmann::json::array();
                for (auto& pin : node->Inputs)
                    fnIn.push_back({ {"name", pin.Name}, {"type", PinTypeToString(pin.Type)} });
                nodeJson["func_inputs"] = fnIn;

                nlohmann::json fnOut = nlohmann::json::array();
                for (auto& pin : node->Outputs)
                    fnOut.push_back({ {"name", pin.Name}, {"type", PinTypeToString(pin.Type)} });
                nodeJson["func_outputs"] = fnOut;
            }

            for (auto& pin : node->Outputs) outputIds.push_back(pin.ID.Get());
            nodeJson["input_ids"] = inputIds;
            nodeJson["output_ids"] = outputIds;
            nodeJson["input_defaults"] = inputDefaults;

            j["nodes"].push_back(nodeJson);
        }

        j["links"] = nlohmann::json::array();
        for (const auto& link : m_Links)
        {
            nlohmann::json linkJson;
            linkJson["id"] = link.ID.Get();
            linkJson["start_pin"] = link.StartPin.Get();
            linkJson["end_pin"] = link.EndPin.Get();
            j["links"].push_back(linkJson);
        }

        // Configuração de nível de MATERIAL (não pertence a nenhum node).
        // Sem isto, Domain/BlendMode/ShadingModel voltavam ao default
        // (Surface/Opaque/DefaultLit) toda vez que o material era reaberto —
        // um grafo Light Function virava Surface ao reabrir o motor.
        j["domain"] = (int)Domain;
        j["blend_mode"] = (int)BlendMode;
        j["two_sided"] = TwoSided;                     // TWO_SIDED_V1
        j["shading_model"] = (int)ShadingModel;
        j["toon_steps"] = ToonSteps;   // SHADING_MODEL_V1

        // WPO_V1 — chaves NOMEADAS, entao a ordem nao importa e grafo antigo
        // (sem elas) cai no default de sempre no Deserialize.
        j["wpo_recompute_normal"] = RecomputeNormalFromWPO;
        j["wpo_normal_delta"] = WPONormalDelta;

        return j;
    }

    Node* MaterialGraph::AddNodeByName(const std::string& name)
    {
        if (name == "Material Output") return AddMaterialOutputNode();
        if (name == "Texture Sample")  return AddTextureSampleNode();
        if (name == "Float")           return AddFloatNode();
        if (name == "Color")           return AddColorNode();
        if (name == "UV Coordinate")   return AddUVNode();
        if (name == "Multiply")        return AddMultiplyNode();
        if (name == "Add")             return AddAddNode();
        if (name == "Subtract")        return AddSubtractNode();
        if (name == "Divide")          return AddDivideNode();
        if (name == "Power")           return AddPowerNode();
        if (name == "Lerp")            return AddLerpNode();
        if (name == "Comment")         return AddComment();
        if (name == "Reroute")         return AddRerouteNode();
        if (name == "Clamp")           return AddClampNode();
        if (name == "Abs")             return AddAbsNode();
        if (name == "OneMinus")        return AddOneMinusNode();
        if (name == "World Position")  return AddWorldPositionNode();
        if (name == "Vertex Normal")   return AddVertexNormalNode();   // WPO_V1
        if (name == "Fresnel")         return AddFresnelNode();
        if (name == "Normal Map")      return AddNormalMapNode();
        if (name == "Sine")            return AddSineNode();
        if (name == "Cosine")          return AddCosineNode();
        if (name == "Step")            return AddStepNode();
        if (name == "SmoothStep")      return AddSmoothStepNode();
        if (name == "Normalize")       return AddNormalizeNode();
        if (name == "Distance")        return AddDistanceNode();
        if (name == "DotProduct")      return AddDotProductNode();
        if (name == "Desaturate")      return AddDesaturateNode();
        if (name == "Append")          return AddAppendNode();
        if (name == "Vector Split")    return AddVectorSplitNode();
        if (name == "Camera Vector")   return AddCameraVectorNode();
        if (name == "Camera Position") return AddCameraPositionNode();   // WATER_NODES_V1
        if (name == "Pixel Depth")     return AddPixelDepthNode();       // WATER_NODES_V1
        if (name == "Scene World Position") return AddSceneWorldPositionNode(); // WATER_DEPTH_V1
        if (name == "Scene Height")    return AddSceneHeightNode();          // SCENE_HEIGHT_V1
        if (name == "Reflection Vector") return AddReflectionVectorNode();
        if (name == "Time")            return AddTimeNode();
        if (name == "Particle Age")    return AddParticleAgeNode();
        if (name == "Particle Color")  return AddParticleColorNode();
        if (name == "Panner")          return AddPannerNode();
        if (name == "Min")             return AddMinNode();
        if (name == "Max")             return AddMaxNode();
        if (name == "Saturate")        return AddSaturateNode();
        if (name == "Length")          return AddLengthNode();
        if (name == "CrossProduct")    return AddCrossProductNode();
        if (name == "If")              return AddIfNode();
        if (name == "Fract")           return AddFractNode();       // MATFUNC_V1
        if (name == "Floor")           return AddFloorNode();       // PRIMITIVES_V1
        if (name == "Ceil")            return AddCeilNode();        // PRIMITIVES_V1
        if (name == "Round")           return AddRoundNode();       // PRIMITIVES_V1
        if (name == "Sqrt")            return AddSqrtNode();        // PRIMITIVES_V1
        if (name == "Sign")            return AddSignNode();        // PRIMITIVES_V1
        if (name == "Mod")             return AddModNode();         // PRIMITIVES_V1
        if (name == "Function Input")   return AddFunctionInputNode();     // MATFUNC_V1
        if (name == "Function Output")  return AddFunctionOutputNode();    // MATFUNC_V1
        if (name == "Material Function") return AddMaterialFunctionNode(); // MATFUNC_V1
        if (name == "Noise")           return AddNoiseNode();
        if (name == "Vec2")            return AddVec2Node();
        if (name == "Vec3")            return AddVec3Node();
        if (name == "Texture Coordinate") return AddTextureCoordinateNode();
        if (name == "Custom")          return AddCustomNode();   // CUSTOM_NODE_V1
        if (name == "Scene Color")     return AddSceneColorNode();  // POSTPROCESS_DOMAIN_V1
        if (name == "Screen UV")       return AddSceneUVNode();     // POSTPROCESS_DOMAIN_V1
        if (name == "Scene Depth")     return AddSceneDepthNode();        // POSTPROCESS_GBUFFER_V1
        if (name == "Scene Normal")    return AddSceneNormalNode();       // POSTPROCESS_GBUFFER_V1
        if (name == "Scene Shading Model") return AddSceneShadingModelNode(); // POSTPROCESS_GBUFFER_V1
        if (name == "Scene Is Background")  return AddSceneIsBackgroundNode();  // POSTPROCESS_SKY_V1
        if (name == "Screen Ray Direction") return AddScreenRayDirectionNode(); // POSTPROCESS_SKY_V1
        if (name == "Sun")                  return AddSunNode();                // POSTPROCESS_SKY_V1
        if (name == "Sun Light")            return AddSunLightNode();           // VOLUME_SUN_V2
        if (name == "Fog Settings")         return AddFogSettingsNode();        // VOLUME_SUN_V2b

        // ── PRIMITIVES_V1 — nome desconhecido nao pode ser silencioso ────────
        //
        // Quem chama isto no Deserialize faz `if (!node) continue;`. Ou seja:
        // um node cujo nome nao existe mais SOME do grafo, e todos os fios
        // dele vao junto — sem uma linha de log.
        //
        // Isso acontece de verdade quando um node e removido da engine, como
        // Water Depth e Shore Distance foram nesta mesma rodada. Sem este
        // aviso, o autor abriria o material, veria a cadeia quebrada num lugar
        // qualquer e nao teria como saber que faltou um node.
        AXE_EDITOR_WARN("[PRIMITIVES_V1] node '{}' nao existe nesta versao da engine: "
            "ele foi descartado do grafo e as ligacoes dele foram perdidas. "
            "Se o material veio de uma versao anterior, refaca esse trecho.", name);

        return nullptr;
    }

    void MaterialGraph::Deserialize(const nlohmann::json& j)
    {
        m_Nodes.clear();
        m_Links.clear();
        m_MaterialOutputNode = nullptr;
        m_PinRemap.clear();

        // Config de nível de material. .value(...) com default garante que
        // grafos salvos ANTES desses campos existirem continuam abrindo
        // normalmente (caem em Surface/Opaque/DefaultLit, comportamento antigo).
        Domain = (MaterialDomain)j.value("domain", (int)MaterialDomain::Surface);
        BlendMode = (MaterialBlendMode)j.value("blend_mode", (int)MaterialBlendMode::Opaque);
        TwoSided = j.value("two_sided", false);        // TWO_SIDED_V1
        ShadingModel = (MaterialShadingModel)j.value("shading_model", (int)MaterialShadingModel::DefaultLit);
        ToonSteps = j.value("toon_steps", 3);   // SHADING_MODEL_V1

        // WPO_V1 — false/0.05 reproduzem o comportamento anterior a esta
        // rodada, entao todo `.axegraph` ja salvo abre identico.
        RecomputeNormalFromWPO = j.value("wpo_recompute_normal", false);
        WPONormalDelta = j.value("wpo_normal_delta", 0.05f);

        // Reconstrói cada node pelo nome — dispatch centralizado em
        // AddNodeByName() (mesma função usada pelo menu de criação e pelo
        // undo de deleção, eliminando a antiga triplicação do if/else).
        for (const auto& nodeJson : j["nodes"])
        {
            std::string name = nodeJson["name"].get<std::string>();
            Node* node = AddNodeByName(name);

            if (!node) continue;

            // Restaura posição no canvas
            //ed::SetNodePosition(node->ID, ImVec2(
            //    nodeJson["pos_x"].get<float>(),
            //    nodeJson["pos_y"].get<float>()));

            m_PendingPositions[node->ID.Get()] = ImVec2(
                nodeJson["pos_x"].get<float>(),
                nodeJson["pos_y"].get<float>());


            // Restaura tamanho (comments)
            if (nodeJson.contains("size_x"))
                node->Size = ImVec2(
                    nodeJson["size_x"].get<float>(),
                    nodeJson["size_y"].get<float>());

            // Restaura texto e cor do Comment
            if (nodeJson.contains("comment_text"))
                node->StringValue = nodeJson["comment_text"].get<std::string>();
            if (nodeJson.contains("comment_color"))
            {
                auto& c = nodeJson["comment_color"];
                node->CommentColor[0] = c[0]; node->CommentColor[1] = c[1]; node->CommentColor[2] = c[2];
            }

            // Restaura valores constantes
            if (nodeJson.contains("value_float"))
                node->Value.FloatVal = nodeJson["value_float"].get<float>();

            if (nodeJson.contains("value_vec2"))
            {
                auto& v = nodeJson["value_vec2"];
                node->Value.Vec2Val = { v[0], v[1] };
            }

            if (nodeJson.contains("value_vec3"))
            {
                auto& v = nodeJson["value_vec3"];
                node->Value.Vec3Val = { v[0], v[1], v[2] };
            }

            if (nodeJson.contains("value_vec4"))
            {
                auto& v = nodeJson["value_vec4"];
                node->Value.Vec4Val = { v[0], v[1], v[2], v[3] };
            }

            // Restaura textura
            if (nodeJson.contains("texture_uuid"))
            {
                node->Value.TextureUUID = nodeJson["texture_uuid"].get<std::string>();
                const AssetRecord* record = AssetDatabase::Get().GetByUUID(node->Value.TextureUUID);
                if (record && std::filesystem::exists(record->FilePath))
                    node->Value.TextureVal = Texture2D::Create(record->FilePath.string());
            }

            // ── CUSTOM_NODE_V1 — RECONSTROI OS PINS ──────────────────────────
            //
            // Tem de acontecer AQUI, antes do bloco de remapeamento logo
            // abaixo: o remap casa os IDs salvos com os pins atuais POR
            // POSICAO, e o `AddNodeByName` acima devolveu o node com a lista
            // padrao de uma entrada so. Se este bloco viesse depois, os links
            // do segundo pin em diante seriam descartados silenciosamente
            // (o remap ja teria rodado com `i < node->Inputs.size()` == 1).
            if (node->Name == "Custom")
            {
                node->CustomCode = nodeJson.value("custom_code", std::string("return In;"));
                node->CustomOutputType =
                    (PinType)nodeJson.value("custom_output_type", (int)PinType::Float);

                if (nodeJson.contains("custom_inputs"))
                {
                    node->Inputs.clear();
                    for (const auto& in : nodeJson["custom_inputs"])
                    {
                        node->Inputs.emplace_back(
                            GetNextID(),
                            in.value("name", std::string("In")).c_str(),
                            (PinType)in.value("type", (int)PinType::Float),
                            ed::PinKind::Input);
                    }
                }

                // A saida existe sempre (uma so), mas o TIPO dela e do usuario.
                if (!node->Outputs.empty())
                    node->Outputs[0].Type = node->CustomOutputType;
            }

            // ── MATFUNC_V1 ───────────────────────────────────────────────
            //
            // Tem que vir AQUI, antes do remapeamento logo abaixo, pela mesma
            // razao ja documentada no bloco do Custom: o remapeamento casa
            // pino por POSICAO no vetor, entao os pinos precisam existir na
            // quantidade final antes dele rodar. Um node Material Function sai
            // da fabrica com ZERO pinos.
            if (node->Name == "Function Input" || node->Name == "Function Output")
            {
                node->StringValue = nodeJson.value("func_param_name", std::string("In"));
                node->CustomOutputType =
                    PinTypeFromString(nodeJson.value("func_param_type", std::string("Float")));
                SyncFunctionIONode(node);
            }
            else if (node->Name == "Material Function")
            {
                node->StringValue = nodeJson.value("func_uuid", std::string());

                auto readParams = [](const nlohmann::json& arr,
                    std::vector<MaterialFunctionParam>& out)
                    {
                        out.clear();
                        if (!arr.is_array()) return;
                        for (const auto& e : arr)
                        {
                            MaterialFunctionParam p;
                            p.Name = e.value("name", std::string("In"));
                            p.Type = PinTypeFromString(e.value("type", std::string("Float")));
                            out.push_back(p);
                        }
                    };

                std::vector<MaterialFunctionParam> fnIn, fnOut;
                if (nodeJson.contains("func_inputs"))  readParams(nodeJson["func_inputs"], fnIn);
                if (nodeJson.contains("func_outputs")) readParams(nodeJson["func_outputs"], fnOut);

                RebuildFunctionCallPins(node, fnIn, fnOut);
            }

            // Remapeia IDs dos pins: salvo → atual (por posição)
            if (nodeJson.contains("input_ids"))
            {
                auto& ids = nodeJson["input_ids"];
                for (int i = 0; i < (int)ids.size() && i < (int)node->Inputs.size(); i++)
                    m_PinRemap[ids[i].get<int>()] = node->Inputs[i].ID.Get();
            }

            // Restaura o valor digitado direto em cada pin (sem precisar
            // de um node constante) — ver Pin::DefaultFloat
            if (nodeJson.contains("input_defaults"))
            {
                auto& defs = nodeJson["input_defaults"];
                for (int i = 0; i < (int)defs.size() && i < (int)node->Inputs.size(); i++)
                    node->Inputs[i].DefaultFloat = defs[i].get<float>();
            }
            if (nodeJson.contains("output_ids"))
            {
                auto& ids = nodeJson["output_ids"];
                for (int i = 0; i < (int)ids.size() && i < (int)node->Outputs.size(); i++)
                    m_PinRemap[ids[i].get<int>()] = node->Outputs[i].ID.Get();
            }
        }


        // Reconstrói links usando o remap
        for (const auto& linkJson : j["links"])
        {
            int savedStart = linkJson["start_pin"].get<int>();
            int savedEnd = linkJson["end_pin"].get<int>();

            auto itStart = m_PinRemap.find(savedStart);
            auto itEnd = m_PinRemap.find(savedEnd);

            if (itStart != m_PinRemap.end() && itEnd != m_PinRemap.end())
            {
                // Verifica se já existe link para este endPin
                bool alreadyConnected = false;
                for (auto& l : m_Links)
                    if (l.EndPin.Get() == itEnd->second)
                    {
                        alreadyConnected = true; break;
                    }

                if (!alreadyConnected)
                    AddLink(ed::PinId(itStart->second), ed::PinId(itEnd->second));
                else
                    AXE_CORE_WARN("Deserialize: link duplicado ignorado ({} -> {})",
                        savedStart, savedEnd);
            }
        }

        BuildNodes();
        //AXE_CORE_INFO("MaterialGraph: grafo desserializado com {} nodes e {} links",
        //    m_Nodes.size(), m_Links.size());
    }

    Pin* MaterialGraph::FindPinByOriginalId(int savedId)
    {
        // Usa o remap para encontrar o ID atual
        auto it = m_IdRemap.find(savedId);
        if (it == m_IdRemap.end()) return nullptr;
        int currentId = it->second;
        return FindPin(ed::PinId(currentId));
    }
    void MaterialGraph::DeleteNode(ed::NodeId nodeId)
    {
        // Remove todos os links conectados a este node
        auto* nodePtr = FindNode(nodeId);
        if (!nodePtr) return;
        auto* node = nodePtr->get();

        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
            [&](const Link& l)
            {
                for (auto& pin : node->Inputs)
                    if (l.StartPin == pin.ID || l.EndPin == pin.ID) return true;
                for (auto& pin : node->Outputs)
                    if (l.StartPin == pin.ID || l.EndPin == pin.ID) return true;
                return false;
            }), m_Links.end());

        // Remove o node
        m_Nodes.erase(std::remove_if(m_Nodes.begin(), m_Nodes.end(),
            [nodeId](const std::unique_ptr<Node>& n) { return n->ID == nodeId; }),
            m_Nodes.end());
    }
} // namespace axe