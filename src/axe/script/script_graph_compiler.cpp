#include "script_graph_compiler.hpp"
#include "axe/log/log.hpp"
#include <sstream>

namespace axe
{
    // ─── Helpers internos ─────────────────────────────────────────────────────

    const ScriptNode* ScriptGraphCompiler::FindNextFlowNode(
        const Context& ctx, const ScriptNode* node, const std::string& outPinName)
    {
        // Acha o pin de saída de flow com o nome dado
        for (const auto& outPin : node->Outputs)
        {
            if (outPin.Type != ScriptPinType::Flow) continue;
            if (!outPinName.empty() && outPin.Name != outPinName) continue;

            // Segue o link
            for (const auto& link : ctx.graph.GetLinks())
            {
                if (link.StartPin != outPin.ID) continue;

                // Acha o node destino
                for (const auto& n : ctx.graph.GetNodes())
                    for (const auto& inPin : n->Inputs)
                        if (inPin.ID == link.EndPin)
                            return n.get();
            }
        }
        return nullptr;
    }

    std::pair<const ScriptNode*, const ScriptPin*>
        ScriptGraphCompiler::FindDataSource(const Context& ctx, const ScriptPin& inputPin)
    {
        for (const auto& link : ctx.graph.GetLinks())
        {
            if (link.EndPin != inputPin.ID) continue;

            for (const auto& n : ctx.graph.GetNodes())
                for (const auto& outPin : n->Outputs)
                    if (outPin.ID == link.StartPin)
                        return { n.get(), &outPin };
        }
        return { nullptr, nullptr };
    }

    std::string ScriptGraphCompiler::ResolvePin(const Context& ctx, const ScriptPin& pin)
    {
        // Verifica se há um link conectado a este pin
        auto [srcNode, srcPin] = FindDataSource(ctx, pin);

        if (!srcNode)
        {
            // Sem conexão — usa valor padrão
            switch (pin.Type)
            {
            case ScriptPinType::Float:  return std::to_string(pin.DefaultFloat) + "f";
            case ScriptPinType::Bool:   return pin.DefaultBool ? "true" : "false";
            case ScriptPinType::Int:    return std::to_string(pin.DefaultInt);
            case ScriptPinType::String: return "\"" + pin.DefaultString + "\"";
            case ScriptPinType::Vec3:
                return "glm::vec3(" +
                    std::to_string(pin.DefaultVec3.x) + "f, " +
                    std::to_string(pin.DefaultVec3.y) + "f, " +
                    std::to_string(pin.DefaultVec3.z) + "f)";
            default: return "{}";
            }
        }

        // Gera código baseado no tipo do node fonte
        const std::string& nodeName = srcNode->Name;

        if (nodeName == "On Update" && srcPin->Name == "Delta Time")
            return "deltaTime";

        if (nodeName == "Get Key")
        {
            // Acha o pin de entrada Key do GetKey
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Key")
                {
                    std::string key = inp.DefaultString.empty() ? "W" : inp.DefaultString;
                    if (srcPin->Name == "Held")
                        return "axe::Input::GetKey(axe::Key::" + key + ")";
                    if (srcPin->Name == "Pressed")
                        return "axe::Input::GetKeyDown(axe::Key::" + key + ")";
                    if (srcPin->Name == "Released")
                        return "axe::Input::GetKeyUp(axe::Key::" + key + ")";
                }
        }

        if (nodeName == "Get Axis")
        {
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Axis Name")
                {
                    std::string axis = inp.DefaultString.empty() ? "Horizontal" : inp.DefaultString;
                    return "axe::Input::GetAxis(\"" + axis + "\")";
                }
        }

        if (nodeName == "Add")
        {
            std::string a = "0.0f", b = "0.0f";
            for (const auto& inp : srcNode->Inputs)
            {
                if (inp.Name == "A") a = ResolvePin(ctx, inp);
                if (inp.Name == "B") b = ResolvePin(ctx, inp);
            }
            return "(" + a + " + " + b + ")";
        }

        if (nodeName == "Multiply")
        {
            std::string a = "0.0f", b = "0.0f";
            for (const auto& inp : srcNode->Inputs)
            {
                if (inp.Name == "A") a = ResolvePin(ctx, inp);
                if (inp.Name == "B") b = ResolvePin(ctx, inp);
            }
            return "(" + a + " * " + b + ")";
        }

        if (nodeName == "Make Vec3")
        {
            std::string x = "0.0f", y = "0.0f", z = "0.0f";
            for (const auto& inp : srcNode->Inputs)
            {
                if (inp.Name == "X") x = ResolvePin(ctx, inp);
                if (inp.Name == "Y") y = ResolvePin(ctx, inp);
                if (inp.Name == "Z") z = ResolvePin(ctx, inp);
            }
            return "glm::vec3(" + x + ", " + y + ", " + z + ")";
        }

        if (nodeName == "Compare")
        {
            std::string a = "0.0f", b = "0.0f";
            for (const auto& inp : srcNode->Inputs)
            {
                if (inp.Name == "A") a = ResolvePin(ctx, inp);
                if (inp.Name == "B") b = ResolvePin(ctx, inp);
            }
            if (srcPin->Name == "A == B") return "(" + a + " == " + b + ")";
            if (srcPin->Name == "A > B")  return "(" + a + " > " + b + ")";
            if (srcPin->Name == "A < B")  return "(" + a + " < " + b + ")";
        }

        if (nodeName == "Get Variable")
            return "m_" + (srcNode->StringValue.empty() ? "var" : srcNode->StringValue);

        // Fallback
        return "{}";
    }

    // ─── Geração de nodes de ação ─────────────────────────────────────────────

    void ScriptGraphCompiler::GenerateNode(Context& ctx,
        const ScriptNode* node, const std::string& deltaTimeVar, int depth)
    {
        if (!node || depth > 32) return; // evita recursão infinita

        const std::string& name = node->Name;

        // ── Ações ──
        if (name == "Move")
        {
            std::string dir = "glm::vec3(0,0,1)", spd = "5.0f";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "Direction") dir = ResolvePin(ctx, inp);
                if (inp.Name == "Speed")     spd = ResolvePin(ctx, inp);
            }
            ctx.Line("GetTransform().Translate(" + dir + " * " + spd + " * deltaTime);");
        }
        else if (name == "Rotate")
        {
            std::string axis = "glm::vec3(0,1,0)", deg = "90.0f";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "Axis")    axis = ResolvePin(ctx, inp);
                if (inp.Name == "Degrees") deg = ResolvePin(ctx, inp);
            }
            ctx.Line("GetTransform().Rotate(" + axis + ", glm::radians(" + deg + " * deltaTime));");
        }
        else if (name == "Apply Force")
        {
            std::string force = "glm::vec3(0,0,0)";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Force") force = ResolvePin(ctx, inp);
            ctx.Line("GetPhysics().AddForce(" + force + ");");
        }
        else if (name == "Send Event")
        {
            std::string target = "entt::null", evName = "\"\"", val = "0.0f";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "Target")     target = ResolvePin(ctx, inp);
                if (inp.Name == "Event Name") evName = ResolvePin(ctx, inp);
                if (inp.Name == "Value")      val = ResolvePin(ctx, inp);
            }
            ctx.Line("GetEventBus().Send(" + target + ", " + evName + ", " + val + ");");
        }
        else if (name == "Print String")
        {
            std::string msg = "\"\"";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Message") msg = ResolvePin(ctx, inp);
            ctx.Line("AXE_SCRIPT_LOG(" + msg + ");");
        }
        else if (name == "Set Variable")
        {
            std::string val = "0.0f";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Value") val = ResolvePin(ctx, inp);
            std::string varName = "m_" + (node->StringValue.empty() ? "var" : node->StringValue);
            ctx.Line(varName + " = " + val + ";");
        }
        // ── Branch ──
        else if (name == "Branch")
        {
            std::string cond = "false";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Condition") cond = ResolvePin(ctx, inp);

            ctx.Line("if (" + cond + ")");
            ctx.Line("{");
            ctx.indent++;
            auto* trueNode = FindNextFlowNode(ctx, node, "True");
            GenerateNode(ctx, trueNode, deltaTimeVar, depth + 1);
            ctx.indent--;
            ctx.Line("}");

            auto* falseNode = FindNextFlowNode(ctx, node, "False");
            if (falseNode)
            {
                ctx.Line("else");
                ctx.Line("{");
                ctx.indent++;
                GenerateNode(ctx, falseNode, deltaTimeVar, depth + 1);
                ctx.indent--;
                ctx.Line("}");
            }
            return; // Branch já segue seus próprios flows
        }

        // Segue o Flow Out
        auto* next = FindNextFlowNode(ctx, node);
        GenerateNode(ctx, next, deltaTimeVar, depth + 1);
    }

    void ScriptGraphCompiler::GenerateEventBody(Context& ctx,
        const ScriptNode* eventNode, const std::string& deltaTimeVar)
    {
        auto* next = FindNextFlowNode(ctx, eventNode);
        if (!next)
        {
            ctx.Line("// nenhuma ação conectada");
            return;
        }
        GenerateNode(ctx, next, deltaTimeVar, 0);
    }

    // ─── Geração principal ────────────────────────────────────────────────────

    std::string ScriptGraphCompiler::Generate(const ScriptGraph& graph,
        const std::string& scriptName)
    {
        Context ctx{ graph };

        // Coleta variáveis (GetVariable/SetVariable)
        std::vector<std::string> variables;
        for (const auto& node : graph.GetNodes())
        {
            if ((node->Name == "Get Variable" || node->Name == "Set Variable")
                && !node->StringValue.empty())
            {
                std::string var = "m_" + node->StringValue;
                bool found = false;
                for (auto& v : variables) if (v == var) { found = true; break; }
                if (!found) variables.push_back(var);
            }
        }

        // ── Header ──
        ctx.code += "// Gerado automaticamente pelo AXE Script Editor — nao edite manualmente\n";
        ctx.code += "#pragma once\n";
        ctx.code += "#include \"axe/script/script_base.hpp\"\n";
        ctx.code += "#include \"axe/utils/glm_config.hpp\"\n";
        ctx.code += "#include \"axe/input/input.hpp\"\n";
        ctx.code += "#include \"axe/log/log.hpp\"\n";
        ctx.code += "#define AXE_SCRIPT_LOG(msg) AXE_CORE_INFO(msg)\n\n";

        ctx.code += "extern \"C\" {\n\n";
        ctx.code += "class " + scriptName + " : public axe::ScriptBase\n{\npublic:\n";

        // Variáveis membro
        if (!variables.empty())
        {
            ctx.code += "    // Variáveis\n";
            for (auto& v : variables)
                ctx.code += "    float " + v + " = 0.0f;\n";
            ctx.code += "\n";
        }

        // ── OnStart ──
        bool hasOnStart = false;
        for (const auto& node : graph.GetNodes())
            if (node->Name == "On Start") { hasOnStart = true; break; }

        if (hasOnStart)
        {
            ctx.code += "    void OnStart() override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Start")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.code += "    }\n\n";
        }

        // ── OnUpdate ──
        bool hasOnUpdate = false;
        for (const auto& node : graph.GetNodes())
            if (node->Name == "On Update") { hasOnUpdate = true; break; }

        if (hasOnUpdate)
        {
            ctx.code += "    void OnUpdate(float deltaTime) override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Update")
                    GenerateEventBody(ctx, node.get(), "deltaTime");
            ctx.code += "    }\n\n";
        }

        // ── OnEnd ──
        bool hasOnEnd = false;
        for (const auto& node : graph.GetNodes())
            if (node->Name == "On End") { hasOnEnd = true; break; }

        if (hasOnEnd)
        {
            ctx.code += "    void OnEnd() override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On End")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.code += "    }\n\n";
        }

        // ── OnCollision ──
        bool hasOnCollision = false;
        for (const auto& node : graph.GetNodes())
            if (node->Name == "On Collision") { hasOnCollision = true; break; }

        if (hasOnCollision)
        {
            ctx.code += "    void OnCollision(entt::entity other) override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Collision")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.code += "    }\n\n";
        }

        // ── OnEvent ──
        bool hasOnEvent = false;
        for (const auto& node : graph.GetNodes())
            if (node->Name == "On Event") { hasOnEvent = true; break; }

        if (hasOnEvent)
        {
            ctx.code += "    void OnEvent(const std::string& eventName, float value) override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Event")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.code += "    }\n\n";
        }

        // ── Factory function ──
        ctx.code += "};\n\n";
        ctx.code += "__declspec(dllexport) axe::ScriptBase* CreateScript()\n";
        ctx.code += "{\n";
        ctx.code += "    return new " + scriptName + "();\n";
        ctx.code += "}\n\n";
        ctx.code += "} // extern \"C\"\n";

        AXE_CORE_INFO("ScriptGraphCompiler: gerado '{}' ({} bytes)",
            scriptName, ctx.code.size());
        return ctx.code;
    }

} // namespace axe