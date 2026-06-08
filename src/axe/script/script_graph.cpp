#include "script_graph.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    // ─── Cores ────────────────────────────────────────────────────────────────

    ImColor GetNodeHeaderColor(ScriptNodeCategory cat)
    {
        switch (cat)
        {
        case ScriptNodeCategory::Event:  return ImColor(180, 60, 40);   // vermelho
        case ScriptNodeCategory::Action: return ImColor(30, 120, 100);  // teal
        case ScriptNodeCategory::Logic:  return ImColor(160, 110, 20);   // âmbar
        case ScriptNodeCategory::Math:   return ImColor(40, 80, 160);  // azul
        case ScriptNodeCategory::Input:  return ImColor(140, 40, 120);  // rosa
        default:                         return ImColor(80, 80, 80);
        }
    }

    ImColor GetPinColor(ScriptPinType type)
    {
        switch (type)
        {
        case ScriptPinType::Flow:   return ImColor(220, 120, 40);   // laranja
        case ScriptPinType::Float:  return ImColor(60, 180, 100);  // verde
        case ScriptPinType::Vec3:   return ImColor(40, 140, 80);   // verde escuro
        case ScriptPinType::Bool:   return ImColor(220, 180, 40);   // amarelo
        case ScriptPinType::Int:    return ImColor(100, 180, 220);  // azul claro
        case ScriptPinType::String: return ImColor(140, 80, 200);  // roxo
        case ScriptPinType::Object: return ImColor(60, 120, 200);  // azul
        default:                    return ImColor(200, 200, 200);
        }
    }

    // ─── Fábrica de nodes ─────────────────────────────────────────────────────

    std::unique_ptr<ScriptNode> ScriptGraph::CreateNodeByType(const char* type, int baseId)
    {
        auto makeNode = [](int id, const char* name, ScriptNodeCategory cat)
            {
                return std::make_unique<ScriptNode>(id, name, cat);
            };

        std::string t = type;

        // ── EVENTOS ──
        if (t == "OnStart")
        {
            auto node = makeNode(baseId, "On Start", ScriptNodeCategory::Event);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "OnUpdate")
        {
            auto node = makeNode(baseId, "On Update", ScriptNodeCategory::Event);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Delta Time", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "OnEnd")
        {
            auto node = makeNode(baseId, "On End", ScriptNodeCategory::Event);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "OnCollision")
        {
            auto node = makeNode(baseId, "On Collision", ScriptNodeCategory::Event);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Other", ScriptPinType::Object, ed::PinKind::Output);
            return node;
        }
        if (t == "OnEvent")
        {
            auto node = makeNode(baseId, "On Event", ScriptNodeCategory::Event);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Event Name", ScriptPinType::String, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }

        // ── AÇÕES ──
        if (t == "Move")
        {
            auto node = makeNode(baseId, "Move", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Direction", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Speed", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "Rotate")
        {
            auto node = makeNode(baseId, "Rotate", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Axis", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Degrees", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "ApplyForce")
        {
            auto node = makeNode(baseId, "Apply Force", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Force", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "SendEvent")
        {
            auto node = makeNode(baseId, "Send Event", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Event Name", ScriptPinType::String, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "PrintString")
        {
            auto node = makeNode(baseId, "Print String", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Message", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── LÓGICA ──
        if (t == "Branch")
        {
            auto node = makeNode(baseId, "Branch", ScriptNodeCategory::Logic);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Condition", ScriptPinType::Bool, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "True", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "False", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "Compare")
        {
            auto node = makeNode(baseId, "Compare", ScriptNodeCategory::Logic);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "A == B", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "A > B", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "A < B", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "GetVariable")
        {
            auto node = makeNode(baseId, "Get Variable", ScriptNodeCategory::Logic);
            node->Outputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "SetVariable")
        {
            auto node = makeNode(baseId, "Set Variable", ScriptNodeCategory::Logic);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── MATH ──
        if (t == "Add")
        {
            auto node = makeNode(baseId, "Add", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "Multiply")
        {
            auto node = makeNode(baseId, "Multiply", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "MakeVec3")
        {
            auto node = makeNode(baseId, "Make Vec3", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "X", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Y", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Z", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Vec", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }

        // ── INPUT ──
        if (t == "GetKey")
        {
            auto node = makeNode(baseId, "Get Key", ScriptNodeCategory::Input);
            node->Inputs.emplace_back(m_NextId++, "Key", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Pressed", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Released", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Held", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "GetAxis")
        {
            auto node = makeNode(baseId, "Get Axis", ScriptNodeCategory::Input);
            node->Inputs.emplace_back(m_NextId++, "Axis Name", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }

        AXE_CORE_WARN("ScriptGraph: tipo de node desconhecido '{}'", type);
        return nullptr;
    }

    // ─── ScriptGraph ──────────────────────────────────────────────────────────

    ScriptNode* ScriptGraph::AddNode(const char* type)
    {
        int id = m_NextId++;
        auto node = CreateNodeByType(type, id);
        if (!node) return nullptr;
        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        AXE_CORE_INFO("ScriptGraph: node '{}' adicionado (id={})", type, id);
        return ptr;
    }

    void ScriptGraph::RemoveNode(ed::NodeId id)
    {
        // Coleta pins do node para remover links conectados
        ScriptNode* node = FindNode(id);
        if (node)
        {
            std::vector<ed::PinId> nodePins;
            for (auto& p : node->Inputs)  nodePins.push_back(p.ID);
            for (auto& p : node->Outputs) nodePins.push_back(p.ID);

            // Remove links que usam qualquer pin desse node
            auto it = m_Links.begin();
            while (it != m_Links.end())
            {
                bool connected = false;
                for (auto& pid : nodePins)
                    if (it->StartPin == pid || it->EndPin == pid) { connected = true; break; }
                it = connected ? m_Links.erase(it) : ++it;
            }
        }

        // Remove o node
        for (auto it = m_Nodes.begin(); it != m_Nodes.end(); ++it)
        {
            if ((*it)->ID == id)
            {
                m_Nodes.erase(it);
                return;
            }
        }
    }

    ScriptLink* ScriptGraph::AddLink(ed::PinId startPin, ed::PinId endPin)
    {
        int id = m_NextId++;
        m_Links.emplace_back(id, startPin, endPin);
        AXE_CORE_INFO("ScriptGraph: link {} → {}", startPin.Get(), endPin.Get());
        return &m_Links.back();
    }

    void ScriptGraph::RemoveLink(ed::LinkId id)
    {
        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
            [&](const ScriptLink& l) { return l.ID == id; }), m_Links.end());
    }

    ScriptPin* ScriptGraph::FindPin(ed::PinId id)
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

    ScriptNode* ScriptGraph::FindNode(ed::NodeId id)
    {
        for (auto& node : m_Nodes)
            if (node->ID == id) return node.get();
        return nullptr;
    }

    bool ScriptGraph::IsPinLinked(ed::PinId id) const
    {
        for (const auto& link : m_Links)
            if (link.StartPin == id || link.EndPin == id) return true;
        return false;
    }

    // ─── Serialização ─────────────────────────────────────────────────────────

    nlohmann::json ScriptGraph::Serialize() const
    {
        nlohmann::json j;
        j["next_id"] = m_NextId;

        for (const auto& node : m_Nodes)
        {
            nlohmann::json jn;
            jn["id"] = (int)node->ID.Get();
            jn["name"] = node->Name;
            jn["category"] = (int)node->Category;
            jn["pos"] = { node->Position.x, node->Position.y };
            jn["str_val"] = node->StringValue;
            jn["flt_val"] = node->FloatValue;

            for (const auto& pin : node->Inputs)
                jn["inputs"].push_back({
                    {"id", (int)pin.ID.Get()}, {"name", pin.Name},
                    {"type", (int)pin.Type}, {"kind", (int)pin.Kind}
                    });
            for (const auto& pin : node->Outputs)
                jn["outputs"].push_back({
                    {"id", (int)pin.ID.Get()}, {"name", pin.Name},
                    {"type", (int)pin.Type}, {"kind", (int)pin.Kind}
                    });

            j["nodes"].push_back(jn);
        }

        for (const auto& link : m_Links)
            j["links"].push_back({
                {"id",    (int)link.ID.Get()},
                {"start", (int)link.StartPin.Get()},
                {"end",   (int)link.EndPin.Get()}
                });

        return j;
    }

    void ScriptGraph::Deserialize(const nlohmann::json& j)
    {
        m_Nodes.clear();
        m_Links.clear();
        m_NextId = j.value("next_id", 1);

        for (const auto& jn : j.value("nodes", nlohmann::json::array()))
        {
            int id = jn["id"];
            std::string name = jn["name"];
            auto cat = (ScriptNodeCategory)jn["category"].get<int>();

            auto node = std::make_unique<ScriptNode>(id, name.c_str(), cat);
            node->Position = { jn["pos"][0], jn["pos"][1] };
            node->StringValue = jn.value("str_val", "");
            node->FloatValue = jn.value("flt_val", 0.0f);

            for (const auto& jp : jn.value("inputs", nlohmann::json::array()))
            {
                ScriptPin pin(jp["id"].get<int>(), jp["name"].get<std::string>().c_str(),
                    (ScriptPinType)jp["type"].get<int>(), (ed::PinKind)jp["kind"].get<int>());
                node->Inputs.push_back(std::move(pin));
            }

            for (const auto& jp : jn.value("outputs", nlohmann::json::array()))
            {
                ScriptPin pin(jp["id"].get<int>(), jp["name"].get<std::string>().c_str(),
                    (ScriptPinType)jp["type"].get<int>(), (ed::PinKind)jp["kind"].get<int>());
                node->Outputs.push_back(std::move(pin));
            }

            m_Nodes.push_back(std::move(node));
        }

        for (const auto& jl : j.value("links", nlohmann::json::array()))
            m_Links.emplace_back(jl["id"].get<int>(), ed::PinId(jl["start"].get<int>()), ed::PinId(jl["end"].get<int>()));
    }

} // namespace axe