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

            for (const auto& link : ctx.graph->GetLinks())
            {
                if (link.StartPin != outPin.ID) continue;

                for (const auto& n : ctx.graph->GetNodes())
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
        for (const auto& link : ctx.graph->GetLinks())
        {
            if (link.EndPin != inputPin.ID) continue;

            for (const auto& n : ctx.graph->GetNodes())
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

        if (nodeName == "Get Action")
        {
            std::string actionLit = "\"" + srcNode->StringValue + "\"";

            if (srcPin->Name == "Triggered")
                return "(axe::Input::GetActionState(" + actionLit + ") == axe::ETriggerState::Triggered)";

            if (srcPin->Name == "Started")
                return "(axe::Input::GetActionState(" + actionLit + ") == axe::ETriggerState::Started)";

            if (srcPin->Name == "Ongoing")
                return "(axe::Input::GetActionState(" + actionLit + ") == axe::ETriggerState::Ongoing)";

            if (srcPin->Name == "Completed")
                return "(axe::Input::GetActionState(" + actionLit + ") == axe::ETriggerState::Completed)";
        }

        if (nodeName == "Get Axis")
        {
            // srcNode->IntValue espelha o AxisValueType (0=1D,1=2D,2=3D) — já
            // sincronizado com o número de pins pelo RebuildAxisOutputPins.
            std::string axisLit = "\"" + srcNode->StringValue + "\"";
            if (srcNode->IntValue == 0)
            {
                if (srcPin->Name == "Value")
                    return "axe::Input::GetAxisValue1D(" + axisLit + ")";
            }
            else if (srcNode->IntValue == 1) // Axis2D — pins "X"/"Y"
            {
                // _GetAxisValue2D escreve em ponteiros de saída — o compilador
                // gera uma chamada auxiliar única por avaliação de X OU Y, então
                // cada pin dispara sua própria leitura completa (X e Y lidos
                // juntos, mas só o componente pedido é retornado). Isso evita
                // precisar de um sistema de "múltiplos retornos" no compilador
                // atual, ao custo de ler o axis duas vezes se X e Y forem
                // ambos usados no grafo — aceitável dado o custo baixo da leitura.
                if (srcPin->Name == "X")
                    return "([&]{ auto _v = axe::Input::GetAxisValue2D(" + axisLit + "); return _v.x; }())";

                if (srcPin->Name == "Y")
                    return "([&]{ auto _v = axe::Input::GetAxisValue2D(" + axisLit + "); return _v.y; }())";
            }
            else // Axis3D — pins "X"/"Y"/"Z"
            {
                if (srcPin->Name == "X")
                    return "([&]{ auto _v = axe::Input::GetAxisValue3D(" + axisLit + "); return _v.x; }())";

                if (srcPin->Name == "Y")
                    return "([&]{ auto _v = axe::Input::GetAxisValue3D(" + axisLit + "); return _v.y; }())";

                if (srcPin->Name == "Z")
                    return "([&]{ auto _v = axe::Input::GetAxisValue3D(" + axisLit + "); return _v.z; }())";
            }
        }

        if (nodeName == "Array Get")
        {
            // node->IntValue == -1 = pin Array nunca conectado a um array real
            // ainda — sem tipo conhecido, não há expressão segura a gerar.
            if (srcNode->IntValue >= 0 && srcPin->Name == "Item")
            {
                std::string arrayExpr, indexExpr = "0";
                for (const auto& inp : srcNode->Inputs)
                {
                    if (inp.Name == "Array") arrayExpr = ResolvePin(ctx, inp);
                    if (inp.Name == "Index") indexExpr = ResolvePin(ctx, inp);
                }
                if (!arrayExpr.empty())
                {
                    // Bounds-check via lambda imediata: índice fora do range em
                    // operator[] é UB silencioso; aqui retorna um valor default
                    // (T{}) em vez de ler memória fora dos limites do vector.
                    return "([&]{ auto& _arr = " + arrayExpr + "; size_t _idx = (size_t)(" + indexExpr + ");"
                        + " return (_idx < _arr.size()) ? _arr[_idx] : std::remove_reference_t<decltype(_arr)>::value_type{}; }())";
                }
            }
        }

        if (nodeName == "Array Length")
        {
            if (srcNode->IntValue >= 0 && srcPin->Name == "Length")
            {
                std::string arrayExpr;
                for (const auto& inp : srcNode->Inputs)
                    if (inp.Name == "Array") arrayExpr = ResolvePin(ctx, inp);
                if (!arrayExpr.empty())
                    return "(int)" + arrayExpr + ".size()";
            }
        }

        // ── Flow Control ─────────────────────────────────────────────────────
        // Nomes das variáveis locais geradas são derivados do NodeId (único no
        // grafo), então loops/array iterations distintos nunca colidem mesmo
        // se aninhados — ver GenerateNode para onde essas variáveis são
        // declaradas (precisa usar exatamente o mesmo esquema de nome aqui).

        if (nodeName == "For Loop" && srcPin->Name == "Index")
            return "_forIdx" + std::to_string((int)srcNode->ID.Get());

        if (nodeName == "For Each Loop" && srcNode->IntValue >= 0)
        {
            std::string sid = std::to_string((int)srcNode->ID.Get());
            if (srcPin->Name == "Item")
                return "_feArr" + sid + "[_feIdx" + sid + "]";
            if (srcPin->Name == "Array Index")
                return "(int)_feIdx" + sid;
        }

        // ── Functions ────────────────────────────────────────────────────────

        if (nodeName == "Function Entry")
            // Os parâmetros da função são lidos com o MESMO nome usado na
            // assinatura do método gerado (ver Generate()) — o pin de saída
            // do Entry e o parâmetro do método compartilham o nome de
            // propósito, não precisa de variável intermediária.
            return srcPin->Name;

        if (srcNode->Category == ScriptNodeCategory::Function && nodeName != "Function Entry")
        {
            // Call <Function> — variáveis locais de resultado declaradas no
            // GenerateNode deste mesmo node (ver lá o esquema de nomes
            // _callResN / _callResN_0, _callResN_1, ...). Se a função tem só
            // 1 output, não há índice; com 2+, cada pin de saída lê a sua
            // variável pela posição na lista de Outputs.
            const ScriptFunction* func = nullptr;
            if (ctx.functions)
                for (auto& f : *ctx.functions) if (f.Name == srcNode->StringValue) { func = &f; break; }
            if (func)
            {
                std::string sid = std::to_string((int)srcNode->ID.Get());
                if (func->Outputs.size() == 1)
                    return "_callRes" + sid;
                for (size_t i = 0; i < func->Outputs.size(); i++)
                    if (func->Outputs[i].Name == srcPin->Name)
                        return "_callRes" + sid + "_" + std::to_string((int)i);
            }
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
            // BUGFIX: antes gerava sempre "std::to_string(v)", o que só
            // compila para Float/Int/Bool. IsWildcardCastCompatible() declara
            // explicitamente "ToString aceita qualquer tipo de dado" — a UI
            // deixa conectar Vec2/Vec3/Vec4/Quat/String no pin Wildcard, mas
            // std::to_string não tem overload pra esses tipos, então o código
            // gerado nem compilava (erro só apareceria ao clicar Compilar,
            // bem depois de o grafo "parecer" válido no editor). Agora o tipo
            // do pin de ORIGEM (não o resolvido por valor) decide a conversão.
            std::string v = "0";
            ScriptPinType srcType = ScriptPinType::Float;
            for (const auto& inp : srcNode->Inputs)
                if (inp.Name == "Value")
                {
                    auto [dataSrcNode, dataSrcPin] = FindDataSource(ctx, inp);
                    if (dataSrcPin) srcType = dataSrcPin->Type;
                    v = ResolvePin(ctx, inp);
                    break;
                }

            switch (srcType)
            {
            case ScriptPinType::Float:
            case ScriptPinType::Int:
            case ScriptPinType::Bool:
                return "std::to_string(" + v + ")";
            case ScriptPinType::String:
                return v; // já é std::string — nada a converter
            case ScriptPinType::Vec2:
                return "([&]{ auto _v = (" + v + "); return std::string(\"(\") + std::to_string(_v.x) + \", \" + std::to_string(_v.y) + \")\"; }())";
            case ScriptPinType::Vec3:
                return "([&]{ auto _v = (" + v + "); return std::string(\"(\") + std::to_string(_v.x) + \", \" + std::to_string(_v.y) + \", \" + std::to_string(_v.z) + \")\"; }())";
            case ScriptPinType::Vec4:
            case ScriptPinType::Quat:
                return "([&]{ auto _v = (" + v + "); return std::string(\"(\") + std::to_string(_v.x) + \", \" + std::to_string(_v.y) + \", \" + std::to_string(_v.z) + \", \" + std::to_string(_v.w) + \")\"; }())";
            default:
                // Object/Entity/qualquer Array — sem representação textual
                // segura e genérica (precisaria de um loop de join para
                // arrays); evita gerar código que não compila.
                return "std::string(\"<sem conversao para string>\")";
            }
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
                    for (const auto& link : ctx.graph->GetLinks())
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
                    for (const auto& link : ctx.graph->GetLinks())
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
                    for (const auto& link : ctx.graph->GetLinks())
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
            for (const auto& link : ctx.graph->GetLinks())
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
                for (const auto& link : ctx.graph->GetLinks())
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
                        // BUGFIX: antes lia node->StringValue, que é o NOME
                        // da variável (usado para montar varName acima) — ou
                        // seja, um Set Variable de String desconectado gerava
                        // "m_Foo = std::string(\"Foo\");" (a variável recebendo
                        // o próprio nome). StringLocalValue é o campo dedicado
                        // ao valor local, nunca compartilhado com o nome.
                        val = "std::string(\"" + node->StringLocalValue + "\")";
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

        else if (name == "Array Add")
        {
            // node->IntValue == -1 significa que o pin Array nunca foi conectado
            // a um array real — sem isso não sabemos o tipo do Item, então não
            // há nada seguro a gerar (RebuildArrayNodePins garante esse valor
            // assim que a conexão é feita no editor).
            if (node->IntValue >= 0)
            {
                std::string arrayExpr, itemExpr = "{}";
                for (const auto& inp : node->Inputs)
                {
                    if (inp.Name == "Array") arrayExpr = ResolvePin(ctx, inp);
                    if (inp.Name == "Item")  itemExpr = ResolvePin(ctx, inp);
                }
                if (!arrayExpr.empty())
                    ctx.Line(arrayExpr + ".push_back(" + itemExpr + ");");
            }
        }
        else if (name == "Array Remove")
        {
            if (node->IntValue >= 0)
            {
                std::string arrayExpr, indexExpr = "0";
                for (const auto& inp : node->Inputs)
                {
                    if (inp.Name == "Array") arrayExpr = ResolvePin(ctx, inp);
                    if (inp.Name == "Index") indexExpr = ResolvePin(ctx, inp);
                }
                if (!arrayExpr.empty())
                {
                    // Bounds-check antes do erase — índice fora do range em
                    // std::vector::erase é UB (não lança exceção), então a
                    // checagem aqui evita crash silencioso por um índice ruim
                    // vindo de um Get Action/cálculo do próprio grafo.
                    ctx.Line("if ((size_t)(" + indexExpr + ") < " + arrayExpr + ".size())");
                    ctx.Line("  " + arrayExpr + ".erase(" + arrayExpr + ".begin() + (" + indexExpr + "));");
                }
            }
        }
        else if (name == "Array Clear")
        {
            if (node->IntValue >= 0)
            {
                std::string arrayExpr;
                for (const auto& inp : node->Inputs)
                    if (inp.Name == "Array") arrayExpr = ResolvePin(ctx, inp);
                if (!arrayExpr.empty())
                    ctx.Line(arrayExpr + ".clear();");
            }
        }

        // ── Flow Control ─────────────────────────────────────────────────────

        else if (name == "Sequence")
        {
            // Sem um "Flow Out" único — executa cada pin "Then N" em ordem,
            // cada um isolado em seu próprio bloco para não misturar variáveis
            // locais que cada ramo possa declarar.
            int pinCount = node->IntValue >= 2 ? node->IntValue : (int)node->Outputs.size();
            for (int i = 0; i < pinCount; i++)
            {
                auto* next = FindNextFlowNode(ctx, node, "Then " + std::to_string(i));
                if (!next) continue;
                ctx.Line("{");
                ctx.indent++;
                GenerateNode(ctx, next, deltaTimeVar, depth + 1);
                ctx.indent--;
                ctx.Line("}");
            }
            return; // Sequence gerencia seus próprios flows
        }
        else if (name == "For Loop")
        {
            std::string first = "0", last = "10";
            for (const auto& inp : node->Inputs)
            {
                if (inp.Name == "First Index") first = ResolvePin(ctx, inp);
                if (inp.Name == "Last Index")  last = ResolvePin(ctx, inp);
            }
            // Nome derivado do NodeId — ver comentário em ResolvePin sobre
            // como o pin "Index" lê essa mesma variável.
            std::string idxVar = "_forIdx" + std::to_string((int)node->ID.Get());

            ctx.Line("for (int " + idxVar + " = " + first + "; " + idxVar + " <= " + last + "; " + idxVar + "++)");
            ctx.Line("{");
            ctx.indent++;
            auto* body = FindNextFlowNode(ctx, node, "Loop Body");
            GenerateNode(ctx, body, deltaTimeVar, depth + 1);
            ctx.indent--;
            ctx.Line("}");

            auto* completed = FindNextFlowNode(ctx, node, "Completed");
            if (completed) GenerateNode(ctx, completed, deltaTimeVar, depth + 1);
            return; // sem "Flow Out" genérico — Completed já foi seguido acima
        }
        else if (name == "For Each Loop")
        {
            // node->IntValue == -1 = pin Array nunca conectado a um array real
            // ainda — sem tipo conhecido, não há nada seguro a gerar (mesma
            // convenção dos nodes genéricos de Array).
            if (node->IntValue >= 0)
            {
                std::string arrayExpr;
                for (const auto& inp : node->Inputs)
                    if (inp.Name == "Array") arrayExpr = ResolvePin(ctx, inp);

                if (!arrayExpr.empty())
                {
                    std::string sid = std::to_string((int)node->ID.Get());
                    std::string arrVar = "_feArr" + sid;
                    std::string idxVar = "_feIdx" + sid;

                    ctx.Line("{");
                    ctx.indent++;
                    ctx.Line("auto& " + arrVar + " = " + arrayExpr + ";");
                    ctx.Line("for (size_t " + idxVar + " = 0; " + idxVar + " < " + arrVar + ".size(); " + idxVar + "++)");
                    ctx.Line("{");
                    ctx.indent++;
                    auto* body = FindNextFlowNode(ctx, node, "Loop Body");
                    GenerateNode(ctx, body, deltaTimeVar, depth + 1);
                    ctx.indent--;
                    ctx.Line("}");
                    ctx.indent--;
                    ctx.Line("}");

                    auto* completed = FindNextFlowNode(ctx, node, "Completed");
                    if (completed) GenerateNode(ctx, completed, deltaTimeVar, depth + 1);
                }
            }
            return; // sem "Flow Out" genérico — mesmo padrão do Branch/For Loop
        }

        // ── Functions ────────────────────────────────────────────────────────
        else if (name == "Return Node")
        {
            // Terminal — sem "Flow Out". ctx.currentFunction é setado por
            // GenerateFunctionBody antes de iniciar o traversal; sem ele não
            // dá pra saber os Outputs esperados (não devia acontecer no uso
            // normal, já que Return Node só existe dentro do grafo de uma
            // função, mas o guard evita crash em grafo corrompido/editado à mão).
            if (ctx.currentFunction)
            {
                auto& outs = ctx.currentFunction->Outputs;
                if (outs.size() == 1)
                {
                    std::string expr = "{}";
                    for (auto& inp : node->Inputs)
                        if (inp.Name == outs[0].Name) { expr = ResolvePin(ctx, inp); break; }
                    ctx.Line("return " + expr + ";");
                }
                else if (outs.size() >= 2)
                {
                    for (auto& outParam : outs)
                    {
                        std::string expr = "{}";
                        for (auto& inp : node->Inputs)
                            if (inp.Name == outParam.Name) { expr = ResolvePin(ctx, inp); break; }
                        ctx.Line(outParam.Name + " = " + expr + ";");
                    }
                    ctx.Line("return;");
                }
                else
                {
                    ctx.Line("return;");
                }
            }
            return;
        }
        else if (node->Category == ScriptNodeCategory::Function && name != "Function Entry")
        {
            // Call <Function> — node->StringValue é o NOME da função chamada
            // (mesma convenção de Get/Set Variable usando StringValue como
            // chave de lookup). Outputs.size() decide a forma da chamada:
            //   0 outputs -> statement puro:           Func(args);
            //   1 output  -> variável local de resultado: T _callResN = Func(args);
            //   2+ outputs-> out-params por referência:   Func(args, &out0, &out1, ...);
            // (ver ResolvePin para como os pins de saída leem essas variáveis)
            const ScriptFunction* func = nullptr;
            if (ctx.functions)
                for (auto& f : *ctx.functions) if (f.Name == node->StringValue) { func = &f; break; }

            if (func)
            {
                std::string sid = std::to_string((int)node->ID.Get());
                std::vector<std::string> argExprs;
                for (auto& param : func->Inputs)
                {
                    std::string expr = "{}";
                    for (auto& inp : node->Inputs)
                        if (inp.Name == param.Name) { expr = ResolvePin(ctx, inp); break; }
                    argExprs.push_back(expr);
                }

                std::string call = func->Name + "(";
                for (size_t i = 0; i < argExprs.size(); i++)
                {
                    if (i > 0) call += ", ";
                    call += argExprs[i];
                }

                if (func->Outputs.empty())
                {
                    call += ");";
                    ctx.Line(call);
                }
                else if (func->Outputs.size() == 1)
                {
                    call += ");";
                    ctx.Line(CppTypeNameFor(func->Outputs[0].Type) + " _callRes" + sid + " = " + call);
                }
                else
                {
                    for (size_t i = 0; i < func->Outputs.size(); i++)
                        ctx.Line(CppTypeNameFor(func->Outputs[i].Type) + " _callRes" + sid + "_" + std::to_string((int)i) + ";");
                    for (size_t i = 0; i < func->Outputs.size(); i++)
                    {
                        if (!argExprs.empty() || i > 0) call += ", ";
                        call += "_callRes" + sid + "_" + std::to_string((int)i);
                    }
                    call += ");";
                    ctx.Line(call);
                }
            }
            else
            {
                ctx.Line("// AVISO: função '" + node->StringValue + "' nao encontrada (removida?)");
            }
            // Segue o "Flow Out" normalmente — Call tem o pin padrão, igual
            // qualquer node de Action (cai no bloco genérico no fim da função).
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

    void ScriptGraphCompiler::GenerateFunctionBody(Context& ctx, const ScriptFunction& func)
    {
        if (!func.Graph)
        {
            ctx.Line("// ERRO: ScriptFunction '" + func.Name + "' sem Graph valido");
            if (func.Outputs.size() == 1) ctx.Line("return {};");
            return;
        }

        // Cada ScriptFunction tem seu PRÓPRIO ScriptGraph — troca ctx.graph
        // temporariamente (e restaura no fim, mesmo nos caminhos de erro)
        // porque qualquer ResolvePin/FindDataSource/FindNextFlowNode chamado
        // durante a geração deste corpo precisa buscar nodes/links DESTE
        // grafo, não do grafo principal nem do de outra função.
        const ScriptGraph* prevGraph = ctx.graph;
        const ScriptFunction* prevFunction = ctx.currentFunction;
        ctx.graph = func.Graph.get();
        ctx.currentFunction = &func;

        const ScriptNode* entry = nullptr;
        for (auto& n : ctx.graph->GetNodes())
            if (n->Name == "Function Entry") { entry = n.get(); break; }

        if (!entry)
        {
            ctx.Line("// ERRO: 'Function Entry' nao encontrado no grafo desta funcao");
        }
        else
        {
            auto* next = FindNextFlowNode(ctx, entry);
            if (next)
                GenerateNode(ctx, next, "", 0);
            else
            {
                // Corpo vazio — sem isso, uma função com 1 output (tipo de
                // retorno != void) geraria um método sem nenhum "return",
                // que nem compila. Não tenta resolver o caso geral de "nem
                // todo caminho do flow retorna" (precisaria de análise de
                // fluxo no grafo) — só cobre o caso trivial de nada conectado.
                ctx.Line("// nenhuma ação conectada");
                if (func.Outputs.size() == 1)
                    ctx.Line("return {};");
            }
        }

        ctx.graph = prevGraph;
        ctx.currentFunction = prevFunction;
    }

    // ─── Geração principal ────────────────────────────────────────────────────

    std::string ScriptGraphCompiler::CppTypeNameFor(ScriptVarType t)
    {
        switch (t)
        {
        case ScriptVarType::Float:      return "float";
        case ScriptVarType::Bool:       return "bool";
        case ScriptVarType::Int:        return "int";
        case ScriptVarType::Vec3:       return "glm::vec3";
        case ScriptVarType::Vec2:       return "glm::vec2";
        case ScriptVarType::Vec4:       return "glm::vec4";
        case ScriptVarType::Quat:       return "glm::quat";
        case ScriptVarType::String:     return "std::string";
            // Entity guarda o NOME como string, resolvido em runtime via
            // FindByName — mesmo padrão usado pra variáveis Entity comuns.
        case ScriptVarType::Entity:     return "std::string";
        case ScriptVarType::FloatArray:  return "std::vector<float>";
        case ScriptVarType::BoolArray:   return "std::vector<bool>";
        case ScriptVarType::IntArray:    return "std::vector<int>";
        case ScriptVarType::Vec3Array:   return "std::vector<glm::vec3>";
        case ScriptVarType::StringArray: return "std::vector<std::string>";
        case ScriptVarType::Vec2Array:   return "std::vector<glm::vec2>";
        case ScriptVarType::Vec4Array:   return "std::vector<glm::vec4>";
        case ScriptVarType::QuatArray:   return "std::vector<glm::quat>";
        case ScriptVarType::EntityArray: return "std::vector<std::string>";
        default:                         return "float";
        }
    }

    std::string ScriptGraphCompiler::Generate(const ScriptGraph& graph,
        const std::string& scriptName,
        const std::vector<ScriptVariable>* assetVars,
        const std::vector<ScriptFunction>* functions)
    {
        Context ctx{ &graph };
        ctx.assetVars = assetVars;
        ctx.functions = functions;

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
        ctx.code += "#include \"axe/scene/scene.hpp\"\n";
        ctx.code += "#include <vector>\n";
        ctx.code += "#include <type_traits>\n\n";

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
                    std::string typeName = CppTypeNameFor(v.Type);
                    std::string defaultVal;
                    switch (v.Type)
                    {
                    case ScriptVarType::Float:
                        defaultVal = std::to_string(v.DefaultFloat) + "f";
                        break;
                    case ScriptVarType::Bool:
                        defaultVal = v.DefaultBool ? "true" : "false";
                        break;
                    case ScriptVarType::Int:
                        defaultVal = std::to_string(v.DefaultInt);
                        break;
                    case ScriptVarType::Vec3:
                        defaultVal = "glm::vec3(" +
                            std::to_string(v.DefaultVec3[0]) + "f," +
                            std::to_string(v.DefaultVec3[1]) + "f," +
                            std::to_string(v.DefaultVec3[2]) + "f)";
                        break;
                    case ScriptVarType::String:
                        defaultVal = "\"" + v.DefaultString + "\"";
                        break;
                    case ScriptVarType::Vec2:
                        defaultVal = "glm::vec2(0.0f, 0.0f)";
                        break;
                    case ScriptVarType::Vec4:
                        defaultVal = "glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)";
                        break;
                    case ScriptVarType::Quat:
                        defaultVal = "glm::quat(1.0f, 0.0f, 0.0f, 0.0f)";  // identity (w,x,y,z)
                        break;
                    case ScriptVarType::Entity:
                        // Guarda o nome da entity como string; resolvido em runtime via FindByName
                        defaultVal = "\"" + v.DefaultString + "\"";
                        break;
                    case ScriptVarType::FloatArray:
                        defaultVal = "std::vector<float>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    case ScriptVarType::BoolArray:
                        defaultVal = "std::vector<bool>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    case ScriptVarType::IntArray:
                        defaultVal = "std::vector<int>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    case ScriptVarType::Vec3Array:
                        defaultVal = "std::vector<glm::vec3>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    case ScriptVarType::StringArray:
                        defaultVal = "std::vector<std::string>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    case ScriptVarType::Vec2Array:
                        defaultVal = "std::vector<glm::vec2>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    case ScriptVarType::Vec4Array:
                        defaultVal = "std::vector<glm::vec4>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    case ScriptVarType::QuatArray:
                        defaultVal = "std::vector<glm::quat>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    case ScriptVarType::EntityArray:
                        // Cada elemento é o NOME da entity (resolvido em runtime via
                        // FindByName), mesmo padrão usado para Entity escalar — evita
                        // guardar entt::entity crus que ficariam inválidos entre cenas.
                        defaultVal = "std::vector<std::string>(" + std::to_string(v.DefaultArraySize) + ")";
                        break;
                    default:
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

        // ── Functions (estilo Function da Unreal) ──────────────────────────────
        // Cada ScriptFunction vira um método de verdade na classe — Call
        // nodes em QUALQUER grafo (principal ou de outra função, recursão
        // inclusive) já funcionam sem nenhum cuidado extra de declaração:
        // métodos de uma mesma classe C++ podem se chamar entre si
        // independente da ordem textual, então não precisa de forward decl.
        if (functions && !functions->empty())
        {
            ctx.code += "    // Functions\n";
            for (const auto& func : *functions)
            {
                std::string sig;
                if (func.Outputs.size() == 1) sig = CppTypeNameFor(func.Outputs[0].Type) + " ";
                else sig = "void "; // 0 outputs, ou 2+ (saem por referência)

                sig += func.Name + "(";
                std::vector<std::string> params;
                for (auto& in : func.Inputs) params.push_back(CppTypeNameFor(in.Type) + " " + in.Name);
                if (func.Outputs.size() >= 2)
                    for (auto& out : func.Outputs) params.push_back(CppTypeNameFor(out.Type) + "& " + out.Name);
                for (size_t i = 0; i < params.size(); i++)
                {
                    if (i > 0) sig += ", ";
                    sig += params[i];
                }
                sig += ")";

                ctx.code += "    " + sig + "\n    {\n";
                ctx.indent = 2;
                GenerateFunctionBody(ctx, func);
                ctx.code += "    }\n\n";
            }
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