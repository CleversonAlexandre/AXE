#include "material_graph.hpp"


namespace axe
{
    
    MaterialGraph::MaterialGraph()
    {
        // Cria o Output node por padrão
       // AddOutputNode();
    }

    Node* MaterialGraph::AddOutputNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Output");
        node->Color = ImVec4(0.18f, 0.35f, 0.18f, 1.0f); // verde escuro

        // Pins de entrada
        node->Inputs.emplace_back(GetNextID(), "Albedo", PinType::Vec4, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Normal", PinType::Vec3, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Roughness", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "Metallic", PinType::Float, ed::PinKind::Input);
        node->Inputs.emplace_back(GetNextID(), "AO", PinType::Float, ed::PinKind::Input);

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


    Node* MaterialGraph::AddFloatNode()
    {
        auto node = std::make_unique<Node>(GetNextID(), "Float");
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
        

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        return ptr;
    }


    void MaterialGraph::AddLink(ed::PinId startPin, ed::PinId endPin)
    {
        m_Links.emplace_back(GetNextID(), startPin, endPin);
    }

    void MaterialGraph::RemoveLink(ed::LinkId id)
    {
        m_Links.erase(
            std::remove_if(m_Links.begin(), m_Links.end(),
                [id](const Link& l) { return l.ID == id; }),
            m_Links.end()
        );
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
   

} // namespace axe