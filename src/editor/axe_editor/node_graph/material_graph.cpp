#include "material_graph.hpp"
#include "axe/log/log.hpp"
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

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddTextureSampleNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Texture Sample");
        node->Color = ImVec4(0.35f, 0.18f, 0.18f, 1.0f); // vermelho escuro

        node->Inputs.emplace_back(GetNextID(), "Texture", PinType::Texture2D, ed::PinKind::Input);
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
        node->Inputs.emplace_back(GetNextID(), "A (Vec3)", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "B (Float)", PinType::Float, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "Result (Vec4)", PinType::Vec4, ed::PinKind::Output);
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }

    Node* MaterialGraph::AddVectorSplitNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Vector Split");
        node->Color = ImVec4(0.5f, 0.3f, 0.1f, 1.0f);
        node->Inputs.emplace_back(GetNextID(), "Value", PinType::Vec3, ed::PinKind::Input);
        node->Outputs.emplace_back(GetNextID(), "X", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Y", PinType::Float, ed::PinKind::Output);
        node->Outputs.emplace_back(GetNextID(), "Z", PinType::Float, ed::PinKind::Output);
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
        node->Outputs.emplace_back(GetNextID(), "Result", PinType::Float, ed::PinKind::Output);
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

            // IDs dos pins — necessários para reconstruir os links
            nlohmann::json inputIds = nlohmann::json::array();
            nlohmann::json outputIds = nlohmann::json::array();
            nlohmann::json inputDefaults = nlohmann::json::array();
            for (auto& pin : node->Inputs)
            {
                inputIds.push_back(pin.ID.Get());
                inputDefaults.push_back(pin.DefaultFloat);
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
        j["shading_model"] = (int)ShadingModel;

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
        if (name == "Reflection Vector") return AddReflectionVectorNode();
        if (name == "Time")            return AddTimeNode();
        if (name == "Panner")          return AddPannerNode();
        if (name == "Min")             return AddMinNode();
        if (name == "Max")             return AddMaxNode();
        if (name == "Saturate")        return AddSaturateNode();
        if (name == "Length")          return AddLengthNode();
        if (name == "CrossProduct")    return AddCrossProductNode();
        if (name == "If")              return AddIfNode();
        if (name == "Noise")           return AddNoiseNode();
        if (name == "Vec2")            return AddVec2Node();
        if (name == "Vec3")            return AddVec3Node();
        if (name == "Texture Coordinate") return AddTextureCoordinateNode();
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
        ShadingModel = (MaterialShadingModel)j.value("shading_model", (int)MaterialShadingModel::DefaultLit);

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