#include "axe/script/script_asset.hpp"
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
            // Check if the Key input pin is connected to another node
            std::string keyExpr;
            for (const auto& inp : srcNode->Inputs)
            {
                if (inp.Name == "Key")
                {
                    // If pin is linked, resolve the connected value dynamically
                    auto [dataNode, dataPin] = FindDataSource(ctx, inp);
                    if (dataNode)
                    {
                        // Connected — resolve as runtime string expression
                        std::string resolved = ResolvePin(ctx, inp);
                        // Use runtime key lookup: axe::Input::GetKeyFromName(str)
                        if (srcPin->Name == "Held")
                            return "_GetKey((int)axe::Input::GetKeyCode(" + resolved + ".c_str()))";
                        if (srcPin->Name == "Pressed")
                            return "_GetKeyDown((int)axe::Input::GetKeyCode(" + resolved + ".c_str()))";
                        if (srcPin->Name == "Released")
                            return "_GetKeyUp((int)axe::Input::GetKeyCode(" + resolved + ".c_str()))";
                    }
                    break;
                }
            }
            // Not connected — use StringValue as hardcoded key name
            std::string key = srcNode->StringValue.empty() ? "W" : srcNode->StringValue;
            if (srcPin->Name == "Held")     return "_GetKey((int)axe::Key::" + key + ")";
            if (srcPin->Name == "Pressed")  return "_GetKeyDown((int)axe::Key::" + key + ")";
            if (srcPin->Name == "Released") return "_GetKeyUp((int)axe::Key::" + key + ")";
        }

        if (nodeName == "Get Axis")
        {
            // 0 = Horizontal, 1 = Vertical — int evita passar std::string cross-DLL
            std::string axis = srcNode->StringValue.empty() ? "Horizontal" : srcNode->StringValue;
            std::string idx = (axis == "Vertical") ? "1" : "0";
            return "_GetAxis(" + idx + ")";
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

        if (nodeName == "Get Variable" || nodeName == "Set Variable")
        {
            std::string base = "m_" + (srcNode->StringValue.empty() ? "var" : srcNode->StringValue);
            if (srcPin->Name == "X") return base + ".x";
            if (srcPin->Name == "Y") return base + ".y";
            if (srcPin->Name == "Z") return base + ".z";
            if (srcPin->Name == "W") return base + ".w";

            // Se o pin de saída é Object (variável do tipo Entity),
            // resolve como FindByName(m_varName) para obter a entt::entity
            if (srcPin->Type == ScriptPinType::Object)
                return "(GetContext().ScenePtr ? GetContext().ScenePtr->FindByName(" + base + ") : entt::null)";

            return base;
        }

        // ── Cast nodes ────────────────────────────────────────────────────────
        if (nodeName == "To Float")
        {
            std::string v = "0.0f";
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Value") { v = ResolvePin(ctx, inp); break; }
            return "(float)(" + v + ")";
        }
        if (nodeName == "To Int")
        {
            std::string v = "0";
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Value") { v = ResolvePin(ctx, inp); break; }
            return "(int)(" + v + ")";
        }
        if (nodeName == "To Bool")
        {
            std::string v = "false";
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Value") { v = ResolvePin(ctx, inp); break; }
            return "((" + v + ") != 0)";
        }
        if (nodeName == "To String")
        {
            std::string v = "0";
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Value") { v = ResolvePin(ctx, inp); break; }
            return "std::to_string(" + v + ")";
        }
        if (nodeName == "To Vec3")
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
        if (nodeName == "Break Vec3")
        {
            std::string v = "glm::vec3(0)";
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Vec3") { v = ResolvePin(ctx, inp); break; }
            if (srcPin->Name == "X") return v + ".x";
            if (srcPin->Name == "Y") return v + ".y";
            if (srcPin->Name == "Z") return v + ".z";
            return v + ".x";
        }
        if (nodeName == "Float to Vec3")
        {
            std::string v = "0.0f";
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Value") { v = ResolvePin(ctx, inp); break; }
            return "glm::vec3(" + v + ")";
        }

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

        if (nodeName == "Get Spring Arm")
        {
            if (srcPin->Name == "Length")        return "GetContext().ScenePtr->GetRegistry().get<axe::SpringArmComponent>(GetContext().Entity).Length";
            if (srcPin->Name == "Height Offset") return "GetContext().ScenePtr->GetRegistry().get<axe::SpringArmComponent>(GetContext().Entity).HeightOffset";
            if (srcPin->Name == "Lag Speed")     return "GetContext().ScenePtr->GetRegistry().get<axe::SpringArmComponent>(GetContext().Entity).LagSpeed";
        }

        if (nodeName == "Get Camera")
        {
            if (srcPin->Name == "FOV")       return "GetContext().ScenePtr->GetRegistry().get<axe::CameraComponent>(GetContext().Entity).Fov";
            if (srcPin->Name == "Near Clip") return "GetContext().ScenePtr->GetRegistry().get<axe::CameraComponent>(GetContext().Entity).NearClip";
            if (srcPin->Name == "Far Clip")  return "GetContext().ScenePtr->GetRegistry().get<axe::CameraComponent>(GetContext().Entity).FarClip";
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
                if (inp.Name == "Speed")
                {
                    std::string v = ResolvePin(ctx, inp);
                    bool connected = false;
                    for (const auto& link : ctx.graph.GetLinks())
                        if (link.EndPin == inp.ID) { connected = true; break; }
                    spd = (connected || v != "0.000000f") ? v : "5.0f";
                }
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
                if (inp.Name == "Speed")
                {
                    std::string v = ResolvePin(ctx, inp);
                    // Usa default 5.0f se o pin não está conectado E o valor é zero
                    // (pins antigos no disco têm DefaultFloat=0 antes do fix de serialização)
                    bool isConnected = false;
                    for (const auto& link : ctx.graph.GetLinks())
                        if (link.EndPin == inp.ID) { isConnected = true; break; }
                    spd = (isConnected || v != "0.000000f") ? v : "5.0f";
                }
            }
            ctx.Line("GetCharacter().Move(" + dir + ", " + spd + ");");
        }
        else if (name == "Character Jump")
        {
            std::string force = "5.0f";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Force")
                {
                    std::string v = ResolvePin(ctx, inp);
                    bool isConnected = false;
                    for (const auto& link : ctx.graph.GetLinks())
                        if (link.EndPin == inp.ID) { isConnected = true; break; }
                    force = (isConnected || v != "0.000000f") ? v : "5.0f";
                }
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
            // Verifica se o pin Message está conectado
            bool connected = false;
            for (const auto& link : ctx.graph.GetLinks())
                for (const auto& inp : node->Inputs)
                    if (inp.Name == "Message" && link.EndPin == inp.ID)
                    {
                        connected = true; break;
                    }

            std::string msg;
            if (connected)
            {
                // Pin conectado — resolve o valor dinamicamente
                for (const auto& inp : node->Inputs)
                    if (inp.Name == "Message") { msg = ResolvePin(ctx, inp); break; }
            }
            else
            {
                // Sem conexão — usa o texto digitado no Script Details (node->StringValue)
                msg = "\"" + node->StringValue + "\"";
            }

            ctx.Line("{ std::string _msg = (" + msg + "); axe::ScriptBase::PrintOnScreen(_msg.c_str()); }");
        }
        else if (name == "Set Variable")
        {
            std::string varName = "m_" + (node->StringValue.empty() ? "var" : node->StringValue);

            // Descobre o tipo da variável para gerar o literal correto
            ScriptVarType varType = ScriptVarType::Float;
            if (ctx.assetVars)
                for (auto& v : *ctx.assetVars)
                    if (v.Name == node->StringValue) { varType = v.Type; break; }

            // Split pin: generate X/Y/Z component assignments
            if (node->IntValue & 0x100)
            {
                std::string x = "0.0f", y = "0.0f", z = "0.0f";
                for (const auto& inp : node->Inputs)
                {
                    if (inp.Name == "X") x = ResolvePin(ctx, inp);
                    if (inp.Name == "Y") y = ResolvePin(ctx, inp);
                    if (inp.Name == "Z") z = ResolvePin(ctx, inp);
                }
                ctx.Line(varName + ".x = " + x + ";");
                ctx.Line(varName + ".y = " + y + ";");
                ctx.Line(varName + ".z = " + z + ";");
            }
            else
            {
                // Verifica se o pin Value está conectado
                bool connected = false;
                for (const auto& link : ctx.graph.GetLinks())
                    for (const auto& inp : node->Inputs)
                        if (inp.Name == "Value" && link.EndPin == inp.ID)
                        {
                            connected = true; break;
                        }

                std::string val;
                if (connected)
                {
                    // Pin conectado — resolve normalmente
                    for (const auto& inp : node->Inputs)
                        if (inp.Name == "Value") { val = ResolvePin(ctx, inp); break; }
                }
                else
                {
                    // Sem conexão — usa valor LOCAL do node (não o default da variável)
                    switch (varType)
                    {
                    case ScriptVarType::Bool:
                        val = node->BoolValue ? "true" : "false";
                        break;
                    case ScriptVarType::Int:
                        val = std::to_string(node->IntLocalValue);
                        break;
                    case ScriptVarType::Vec3:
                        val = "glm::vec3(" +
                            std::to_string(node->Vec3Value[0]) + "f, " +
                            std::to_string(node->Vec3Value[1]) + "f, " +
                            std::to_string(node->Vec3Value[2]) + "f)";
                        break;
                    case ScriptVarType::String:
                        val = "std::string(\"" + node->StringValue + "\")";
                        break;
                    default: // Float e outros
                        val = std::to_string(node->FloatValue) + "f";
                        break;
                    }
                }
                ctx.Line(varName + " = " + val + ";");
            }
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

        else if (name == "Set Spring Arm")
        {
            std::string len = "5.0f", hOff = "2.0f";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "Length")        len = ResolvePin(ctx, inp);
                if (inp.Name == "Height Offset") hOff = ResolvePin(ctx, inp);
            }
            ctx.Line("if (GetContext().ScenePtr->GetRegistry().all_of<axe::SpringArmComponent>(GetContext().Entity)) {");
            ctx.Line("  auto& _sa = GetContext().ScenePtr->GetRegistry().get<axe::SpringArmComponent>(GetContext().Entity);");
            ctx.Line("  _sa.Length = " + len + ";");
            ctx.Line("  _sa.HeightOffset = " + hOff + ";");
            ctx.Line("}");
        }
        else if (name == "Set Camera FOV")
        {
            std::string fov = "60.0f";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "FOV") fov = ResolvePin(ctx, inp);
            ctx.Line("if (GetContext().ScenePtr->GetRegistry().all_of<axe::CameraComponent>(GetContext().Entity)) {");
            ctx.Line("  GetContext().ScenePtr->GetRegistry().get<axe::CameraComponent>(GetContext().Entity).Fov = " + fov + ";");
            ctx.Line("}");
        }

        // ── Destroy Entity ────────────────────────────────────────────────────
        else if (name == "Destroy Entity")
        {
            std::string target = "entt::null";
            for (const auto& inp : node->Inputs)
                if (inp.Name == "Target") target = ResolvePin(ctx, inp);

            ctx.Line("{ // Destroy Entity");
            ctx.Line("  auto _destroyTarget = " + target + ";");
            ctx.Line("  if (_destroyTarget != entt::null)");
            ctx.Line("  {");

            if (ctx.eventName == "OnCollision")
            {
                ctx.Line("    if (_destroyTarget == other)");
                ctx.Line("    {");
                ctx.Line("      DestroyEntitySafe(_destroyTarget);");
                ctx.indent += 3;
                auto* next = FindNextFlowNode(ctx, node);
                GenerateNode(ctx, next, deltaTimeVar, depth + 1);
                ctx.indent -= 3;
                ctx.Line("    }");
            }
            else
            {
                ctx.Line("    DestroyEntitySafe(_destroyTarget);");
                ctx.indent++;
                auto* next = FindNextFlowNode(ctx, node);
                GenerateNode(ctx, next, deltaTimeVar, depth + 1);
                ctx.indent--;
            }

            ctx.Line("  }");
            ctx.Line("}");
            return;
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
        const std::string& scriptName,
        const std::vector<ScriptVariable>* assetVars)
    {
        Context ctx{ graph };
        ctx.assetVars = assetVars;

        // Coleta variáveis do ScriptAsset (tipadas) — preferido sobre inferência do grafo
        // As variáveis do grafo (Get/Set Variable) são geradas como float por fallback
        std::vector<std::string> variables; // fallback: vars do grafo não no asset

        // ── Header ────────────────────────────────────────────────────────────
        ctx.code += "// Gerado automaticamente pelo AXE Script Editor — nao edite manualmente\n";
        ctx.code += "#pragma once\n";
        ctx.code += "#include \"axe/script/script_base.hpp\"\n";
        ctx.code += "#include \"axe/utils/glm_config.hpp\"\n";
        ctx.code += "#include \"axe/input/input.hpp\"\n";
        ctx.code += "#include \"axe/log/log.hpp\"\n";
        ctx.code += "#include \"axe/scene/scene.hpp\"\n\n";

        ctx.code += "extern \"C\" {\n\n";
        ctx.code += "class " + scriptName + " : public axe::ScriptBase\n{\npublic:\n";

        // Variáveis membro — NOTE: o compiler recebe apenas o grafo, não o asset.
        // As variáveis tipadas são geradas via ScriptGraphWindow antes de chamar Generate().
        // Aqui geramos fallback float para variáveis referenciadas no grafo mas não declaradas.
        {
            if (assetVars && !assetVars->empty())
            {
                ctx.code += "    // Variables\n";
                for (auto& v : *assetVars)
                {
                    std::string typeName, defaultVal;
                    switch (v.Type)
                    {
                    case ScriptVarType::Float:
                        typeName = "float";
                        defaultVal = std::to_string(v.DefaultFloat) + "f";
                        break;
                    case ScriptVarType::Bool:
                        typeName = "bool";
                        defaultVal = v.DefaultBool ? "true" : "false";
                        break;
                    case ScriptVarType::Int:
                        typeName = "int";
                        defaultVal = std::to_string(v.DefaultInt);
                        break;
                    case ScriptVarType::Vec3:
                        typeName = "glm::vec3";
                        defaultVal = "glm::vec3(" +
                            std::to_string(v.DefaultVec3[0]) + "f," +
                            std::to_string(v.DefaultVec3[1]) + "f," +
                            std::to_string(v.DefaultVec3[2]) + "f)";
                        break;
                    case ScriptVarType::String:
                        typeName = "std::string";
                        defaultVal = "\"" + v.DefaultString + "\"";
                        break;
                    case ScriptVarType::Vec2:
                        typeName = "glm::vec2";
                        defaultVal = "glm::vec2(0.0f, 0.0f)";
                        break;
                    case ScriptVarType::Vec4:
                        typeName = "glm::vec4";
                        defaultVal = "glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)";
                        break;
                    case ScriptVarType::Quat:
                        typeName = "glm::quat";
                        defaultVal = "glm::quat(1.0f, 0.0f, 0.0f, 0.0f)";  // identity (w,x,y,z)
                        break;
                    case ScriptVarType::Entity:
                        // Guarda o nome da entity como string; resolvido em runtime via FindByName
                        typeName = "std::string";
                        defaultVal = "\"" + v.DefaultString + "\"";
                        break;
                    default:
                        typeName = "float";
                        defaultVal = "0.0f";
                        break;
                    }
                    ctx.code += "    " + typeName + " m_" + v.Name + " = " + defaultVal + ";\n";
                }
                ctx.code += "\n";
            }
            else
            {
                // Fallback: infere float para vars referenciadas no grafo mas não no asset
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
                if (!variables.empty())
                {
                    ctx.code += "    // Variables (float fallback)\n";
                    for (auto& v : variables)
                        ctx.code += "    float " + v + " = 0.0f;\n";
                    ctx.code += "\n";
                }
            }
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
            ctx.eventName = "OnCollision";
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Collision")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.eventName = "";
            ctx.code += "    }\n\n";
        }

        // ── OnEvent ───────────────────────────────────────────────────────────
        if (hasEvent("On Event"))
        {
            ctx.code += "    void OnEvent(const std::string& eventName, float value) override\n    {\n";
            ctx.indent = 2;
            ctx.eventName = "OnEvent";
            for (const auto& node : graph.GetNodes())
                if (node->Name == "On Event")
                    GenerateEventBody(ctx, node.get(), "");
            ctx.eventName = "";
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