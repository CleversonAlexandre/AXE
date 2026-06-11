#include "script_graph_compiler.hpp"
#include "axe/log/log.hpp"
#include <sstream>

namespace axe
{
    // ─── Helpers internos ─────────────────────────────────────────────────────

    const ScriptNode* ScriptGraphCompiler::FindNextFlowNode(
        const Context& ctx, const ScriptNode* node, const std::string& outPinName)
    {
        for (const auto& outPin : node->Outputs)
        {
            if (outPin.Type != ScriptPinType::Flow) continue;
            if (!outPinName.empty() && outPin.Name != outPinName) continue;

            for (const auto& link : ctx.graph.GetLinks())
            {
                if (link.StartPin != outPin.ID) continue;

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

        const std::string& nodeName = srcNode->Name;

        // ── Eventos ──────────────────────────────────────────────────────────

        if (nodeName == "On Update" && srcPin->Name == "Delta Time")
            return "deltaTime";

        // ── Input ─────────────────────────────────────────────────────────────

        if (nodeName == "Get Key")
        {
            // O editor guarda o valor em node->StringValue (não no pin DefaultString)
            std::string key = srcNode->StringValue.empty() ? "W" : srcNode->StringValue;
            if (srcPin->Name == "Held")     return "axe::Input::GetKey(axe::Key::" + key + ")";
            if (srcPin->Name == "Pressed")  return "axe::Input::GetKeyDown(axe::Key::" + key + ")";
            if (srcPin->Name == "Released") return "axe::Input::GetKeyUp(axe::Key::" + key + ")";
        }

        if (nodeName == "Get Axis")
        {
            // O editor guarda o valor em node->StringValue (não no pin DefaultString)
            std::string axis = srcNode->StringValue.empty() ? "Horizontal" : srcNode->StringValue;
            return "axe::Input::GetAxis(\"" + axis + "\")";
        }

        // ── Math ──────────────────────────────────────────────────────────────

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

        // ── Lógica ────────────────────────────────────────────────────────────

        if (nodeName == "Get Variable")
            return "m_" + (srcNode->StringValue.empty() ? "var" : srcNode->StringValue);

        // ── Componentes — leitura de dados ────────────────────────────────────

        if (nodeName == "Get Transform")
        {
            if (srcPin->Name == "Position") return "GetTransform().GetPosition()";
            if (srcPin->Name == "Rotation") return "GetTransform().GetRotation()";
            if (srcPin->Name == "Scale")    return "GetTransform().GetScale()";
        }

        if (nodeName == "Get Position")
        {
            if (srcPin->Name == "Position") return "GetTransform().GetPosition()";
        }

        if (nodeName == "Get Rigidbody")
        {
            if (srcPin->Name == "Velocity") return "GetRigidbody().GetVelocity()";
            if (srcPin->Name == "Mass")     return "GetRigidbody().GetMass()";
        }

        if (nodeName == "Get Collider")
        {
            // Valores estáticos do componente — lidos via contexto em runtime
            if (srcPin->Name == "Is Trigger")   return "GetContext().ScenePtr->GetRegistry().get<axe::ColliderComponent>(GetContext().Entity).IsTrigger";
            if (srcPin->Name == "Half Extent")  return "GetContext().ScenePtr->GetRegistry().get<axe::ColliderComponent>(GetContext().Entity).HalfExtent";
        }

        if (nodeName == "Get Character Ctrl")
        {
            if (srcPin->Name == "Is Grounded") return "GetCharacter().IsGrounded()";
            if (srcPin->Name == "Velocity")    return "GetCharacter().GetVelocity()";
            if (srcPin->Name == "Max Speed")   return "GetCharacter().GetMaxSpeed()";
        }

        // Fallback
        return "{}";
    }

    // ─── Geração de nodes de ação ─────────────────────────────────────────────

    void ScriptGraphCompiler::GenerateNode(Context& ctx,
        const ScriptNode* node, const std::string& deltaTimeVar, int depth)
    {
        if (!node || depth > 32) return;

        const std::string& name = node->Name;

        // deltaTime só existe dentro de OnUpdate — usa "1.0f" como fallback
        // nos outros eventos para evitar erro de compilação
        const std::string dt = deltaTimeVar.empty() ? "1.0f" : deltaTimeVar;

        // ── Movimento / Física ────────────────────────────────────────────────

        if (name == "Move")
        {
            std::string dir = "glm::vec3(0,0,1)", spd = "5.0f";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "Direction") dir = ResolvePin(ctx, inp);
                if (inp.Name == "Speed")     spd = ResolvePin(ctx, inp);
            }
            ctx.Line("GetTransform().Translate(" + dir + " * " + spd + " * " + dt + ");");
        }
        else if (name == "Rotate")
        {
            std::string axis = "glm::vec3(0,1,0)", deg = "90.0f";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "Axis")    axis = ResolvePin(ctx, inp);
                if (inp.Name == "Degrees") deg = ResolvePin(ctx, inp);
            }
            ctx.Line("GetTransform().Rotate(" + axis + ", glm::radians(" + deg + " * " + dt + "));");
        }
        else if (name == "Apply Force")
        {
            std::string force = "glm::vec3(0,0,0)";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Force") force = ResolvePin(ctx, inp);
            ctx.Line("GetRigidbody().AddForce(" + force + ");");
        }

        // ── Transform direto ──────────────────────────────────────────────────

        else if (name == "Set Transform")
        {
            std::string pos = "glm::vec3(0)", rot = "glm::vec3(0)", scl = "glm::vec3(1)";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "Position") pos = ResolvePin(ctx, inp);
                if (inp.Name == "Rotation") rot = ResolvePin(ctx, inp);
                if (inp.Name == "Scale")    scl = ResolvePin(ctx, inp);
            }
            ctx.Line("GetTransform().SetPosition(" + pos + ");");
            ctx.Line("GetTransform().SetRotation(" + rot + ");");
            ctx.Line("GetTransform().SetScale(" + scl + ");");
        }
        else if (name == "Set Position")
        {
            std::string pos = "glm::vec3(0)";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Position") pos = ResolvePin(ctx, inp);
            ctx.Line("GetTransform().SetPosition(" + pos + ");");
        }

        // ── Rigidbody ─────────────────────────────────────────────────────────

        else if (name == "Set Velocity")
        {
            std::string vel = "glm::vec3(0)";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Velocity") vel = ResolvePin(ctx, inp);
            ctx.Line("GetRigidbody().SetVelocity(" + vel + ");");
        }

        // ── CharacterController ───────────────────────────────────────────────

        else if (name == "Character Move")
        {
            std::string dir = "glm::vec3(0,0,1)", spd = "5.0f";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "Direction") dir = ResolvePin(ctx, inp);
                if (inp.Name == "Speed")     spd = ResolvePin(ctx, inp);
            }
            ctx.Line("GetCharacter().Move(" + dir + ", " + spd + ");");
        }
        else if (name == "Character Jump")
        {
            std::string force = "5.0f";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Force") force = ResolvePin(ctx, inp);
            ctx.Line("GetCharacter().Jump(" + force + ");");
        }

        // ── Eventos / IO ──────────────────────────────────────────────────────

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

        // ── Branch ────────────────────────────────────────────────────────────

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
            return; // Branch gerencia seus próprios flows
        }

        // ── Nodes puramente de leitura de dados (sem Flow Out) ────────────────
        // Get Transform / Get Position / Get Rigidbody / Get Collider /
        // Get Character Ctrl não emitem código de ação — são resolvidos via
        // ResolvePin quando conectados a um input de dados. Chegamos aqui só se
        // alguém conectar o Flow Out deles, o que não faz sentido; ignoramos.
        else if (name == "Get Transform" || name == "Get Position" ||
            name == "Get Rigidbody" || name == "Get Collider" ||
            name == "Get Character Ctrl")
        {
            // Noop — esses nodes só produzem dados, não ações
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

        // Coleta variáveis (GetVariable / SetVariable)
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

        // ── Header ────────────────────────────────────────────────────────────
        ctx.code += "// Gerado automaticamente pelo AXE Script Editor — nao edite manualmente\n";
        ctx.code += "#pragma once\n";
        ctx.code += "#include \"axe/script/script_base.hpp\"\n";
        ctx.code += "#include \"axe/utils/glm_config.hpp\"\n";
        ctx.code += "#include \"axe/input/input.hpp\"\n";
        ctx.code += "#include \"axe/log/log.hpp\"\n";
        ctx.code += "#include \"axe/physics/physics_components.hpp\"\n";
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

        // ── Helpers para detectar presença de evento ──────────────────────────
        auto hasEvent = [&](const std::string& evName) -> bool {
            for (const auto& node : graph.GetNodes())
                if (node->Name == evName) return true;
            return false;
            };

        // ── OnStart ───────────────────────────────────────────────────────────
        if (hasEvent("On Start"))
        {
            ctx.code += "    void OnStart() override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Start")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.code += "    }\n\n";
        }

        // ── OnUpdate ──────────────────────────────────────────────────────────
        if (hasEvent("On Update"))
        {
            ctx.code += "    void OnUpdate(float deltaTime) override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Update")
                    GenerateEventBody(ctx, node.get(), "deltaTime");
            ctx.code += "    }\n\n";
        }

        // ── OnEnd ─────────────────────────────────────────────────────────────
        if (hasEvent("On End"))
        {
            ctx.code += "    void OnEnd() override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On End")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.code += "    }\n\n";
        }

        // ── OnCollision ───────────────────────────────────────────────────────
        if (hasEvent("On Collision"))
        {
            ctx.code += "    void OnCollision(entt::entity other) override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Collision")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.code += "    }\n\n";
        }

        // ── OnEvent ───────────────────────────────────────────────────────────
        if (hasEvent("On Event"))
        {
            ctx.code += "    void OnEvent(const std::string& eventName, float value) override\n    {\n";
            ctx.indent = 2;
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Event")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.code += "    }\n\n";
        }

        // ── Factory function ──────────────────────────────────────────────────
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