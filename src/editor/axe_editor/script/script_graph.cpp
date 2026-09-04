#include "script_graph.hpp"
#include "axe/log/log.hpp"
// std::remove_if / std::find — já eram usados neste .cpp por include
// transitivo; explicitados aqui porque S1 acrescentou mais usos e depender de
// include indireto é o tipo de coisa que quebra ao mexer em outro header.
#include <algorithm>
#include <vector>
#include <cmath>   // SC10: isfinite no conserto de posicao

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
        case ScriptNodeCategory::Input:    return ImColor(140, 40, 120);  // rosa
        case ScriptNodeCategory::Array:    return ImColor(110, 90, 180);  // roxo claro
        case ScriptNodeCategory::FlowControl: return ImColor(70, 100, 135); // azul acinzentado
        case ScriptNodeCategory::Function: return ImColor(40, 130, 120);  // verde-azulado
        case ScriptNodeCategory::Variable: return ImColor(180, 60, 140);  // pink base
        case ScriptNodeCategory::Print:    return ImColor(180, 60, 130);  // rosa
        default:                         return ImColor(80, 80, 80);
        }
    }

    ImColor GetPinColor(ScriptPinType type)
    {
        switch (type)
        {
        case ScriptPinType::Flow:     return ImColor(220, 120, 40);   // laranja
        case ScriptPinType::Float:    return ImColor(30, 140, 60);   // verde escuro
        case ScriptPinType::Vec3:     return ImColor(200, 180, 30);   // amarelo
        case ScriptPinType::Bool:     return ImColor(180, 40, 40);   // vermelho
        case ScriptPinType::Int:      return ImColor(80, 200, 80);   // verde claro
        case ScriptPinType::String:   return ImColor(200, 80, 150);  // rosa
        case ScriptPinType::Object:   return ImColor(60, 120, 200);  // azul
        case ScriptPinType::Vec2:     return ImColor(40, 200, 200);  // ciano
        case ScriptPinType::Vec4:     return ImColor(160, 80, 220);  // roxo
        case ScriptPinType::Quat:     return ImColor(180, 140, 220);  // lavanda
        case ScriptPinType::Wildcard: return ImColor(200, 200, 200);  // branco
            // Arrays — mesma família de cor do escalar correspondente, mais escura/
            // saturada (convenção comum em editores de Blueprint para "lista de X").
        case ScriptPinType::FloatArray:  return ImColor(15, 90, 40);   // verde escuro++
        case ScriptPinType::BoolArray:   return ImColor(120, 20, 20);  // vermelho++
        case ScriptPinType::IntArray:    return ImColor(40, 140, 40);  // verde claro++
        case ScriptPinType::Vec3Array:   return ImColor(140, 120, 10); // amarelo++
        case ScriptPinType::StringArray: return ImColor(140, 40, 100); // rosa++
        case ScriptPinType::Vec2Array:   return ImColor(20, 140, 140); // ciano++
        case ScriptPinType::Vec4Array:   return ImColor(110, 40, 160); // roxo++
        case ScriptPinType::QuatArray:   return ImColor(120, 90, 160); // lavanda++
        case ScriptPinType::EntityArray: return ImColor(30, 70, 140);  // azul++
        default:                      return ImColor(200, 200, 200);
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
            auto node = makeNode(baseId, "Print String", ScriptNodeCategory::Print);
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
            node->Outputs.emplace_back(m_NextId++, "A != B", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "A > B", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "A >= B", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "A < B", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "A <= B", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        // ── Logic (combinadores booleanos — faltava completamente) ──────────
        if (t == "And")
        {
            auto node = makeNode(baseId, "AND", ScriptNodeCategory::Logic);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Bool, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Bool, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Bool, ed::PinKind::Output);
            node->IntValue = 2; // espelha o número atual de inputs A/B/C/... (ver RebuildLogicInputs)
            return node;
        }
        if (t == "Or")
        {
            auto node = makeNode(baseId, "OR", ScriptNodeCategory::Logic);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Bool, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Bool, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Bool, ed::PinKind::Output);
            node->IntValue = 2;
            return node;
        }
        if (t == "Not")
        {
            auto node = makeNode(baseId, "NOT", ScriptNodeCategory::Logic);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Bool, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "Xor")
        {
            auto node = makeNode(baseId, "XOR", ScriptNodeCategory::Logic);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Bool, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Bool, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "GetVariable")
        {
            auto node = makeNode(baseId, "Get Variable", ScriptNodeCategory::Variable);
            node->Outputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "SetVariable")
        {
            auto node = makeNode(baseId, "Set Variable", ScriptNodeCategory::Variable);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Output);
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
        // ── Math (completando o conjunto — só existiam Add/Multiply) ──────────
        if (t == "Subtract")
        {
            auto node = makeNode(baseId, "Subtract", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "Divide")
        {
            auto node = makeNode(baseId, "Divide", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.back().Default.Float = 1.0f; // evita divisão por 0 no caso comum de B desconectado
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "Min")
        {
            auto node = makeNode(baseId, "Min", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "Max")
        {
            auto node = makeNode(baseId, "Max", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "Abs")
        {
            auto node = makeNode(baseId, "Abs", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "Negate")
        {
            auto node = makeNode(baseId, "Negate", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "Clamp")
        {
            auto node = makeNode(baseId, "Clamp", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Min", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Max", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.back().Default.Float = 1.0f; // intervalo inicial razoável: 0..1
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "Lerp")
        {
            auto node = makeNode(baseId, "Lerp", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Alpha", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }

        // ── Random — puramente de dados, sem estado/seed próprio (usa rand()
        // do <cstdlib>, já incluso no código gerado) ───────────────────────────
        if (t == "RandomFloat")
        {
            auto node = makeNode(baseId, "Random Float", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Min", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Max", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.back().Default.Float = 1.0f; // intervalo inicial razoável: 0..1
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "RandomInt")
        {
            auto node = makeNode(baseId, "Random Int", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Min", ScriptPinType::Int, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Max", ScriptPinType::Int, ed::PinKind::Input);
            node->Inputs.back().Default.Int = 100; // intervalo inicial razoável: 0..100
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Int, ed::PinKind::Output);
            return node;
        }
        if (t == "RandomBool")
        {
            auto node = makeNode(baseId, "Random Bool", ScriptNodeCategory::Math);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "RandomRange")
        {
            // Diferente do Random Float (que já cobre range de Float): aqui é
            // um PONTO aleatório dentro de uma caixa Vec3 — cada eixo (X/Y/Z)
            // sorteado independentemente entre Min e Max. Útil pra posição de
            // spawn, dispersão de partículas, etc.
            auto node = makeNode(baseId, "Random Range (Vec3)", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Min", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Max", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Inputs.back().Default.Vec = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }

        // ── String ops — você já tinha o tipo String, faltavam só os nodes
        // pra manipular ─────────────────────────────────────────────────────
        if (t == "Concat")
        {
            auto node = makeNode(baseId, "Concat", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::String, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::String, ed::PinKind::Output);
            return node;
        }
        if (t == "StringLength")
        {
            auto node = makeNode(baseId, "Length", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Int, ed::PinKind::Output);
            return node;
        }
        if (t == "Contains")
        {
            auto node = makeNode(baseId, "Contains", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::String, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "B", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "Substring")
        {
            auto node = makeNode(baseId, "Substring", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "A", ScriptPinType::String, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Start", ScriptPinType::Int, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Length", ScriptPinType::Int, ed::PinKind::Input);
            node->Inputs.back().Default.Int = 999999; // "até o fim" por padrão — substr clampa sozinho
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::String, ed::PinKind::Output);
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
        if (t == "GetAction")
        {
            auto node = makeNode(baseId, "Get Action", ScriptNodeCategory::Input);
            // Sem pin de input — a Action é escolhida via combo (node->StringValue),
            // alimentado por InputMappingConfig (ver script_node_draw.cpp).
            node->Outputs.emplace_back(m_NextId++, "Triggered", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Started", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Ongoing", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Completed", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "GetAxis")
        {
            auto node = makeNode(baseId, "Get Axis", ScriptNodeCategory::Input);
            // Começa com 1 pin (Axis1D, caso mais comum) — reconstruído
            // dinamicamente para 2/3 pins quando o usuário escolhe no combo
            // um Axis configurado como Axis2D/3D (ver RebuildAxisOutputPins,
            // chamado a cada frame que o node é desenhado em script_node_draw.cpp).
            node->Outputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Output);
            node->IntValue = 0; // espelha AxisValueType atual dos pins (0=1D,1=2D,2=3D)
            return node;
        }

        // ── ARRAY ── (genéricos — pin "Array" começa Wildcard, fixa no tipo
        // real assim que conectado a uma variável array; ver
        // RebuildArrayNodePins em script_node_draw.cpp, mesmo padrão usado
        // para os pins dinâmicos de Get Axis)
        if (t == "ArrayAdd")
        {
            auto node = makeNode(baseId, "Array Add", ScriptNodeCategory::Array);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Array", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Item", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            node->IntValue = -1; // -1 = tipo de array ainda desconhecido (nenhuma conexão ainda)
            return node;
        }
        if (t == "ArrayRemove")
        {
            auto node = makeNode(baseId, "Array Remove", ScriptNodeCategory::Array);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Array", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Index", ScriptPinType::Int, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            node->IntValue = -1;
            return node;
        }
        if (t == "ArrayGet")
        {
            auto node = makeNode(baseId, "Array Get", ScriptNodeCategory::Array);
            node->Inputs.emplace_back(m_NextId++, "Array", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Index", ScriptPinType::Int, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Item", ScriptPinType::Wildcard, ed::PinKind::Output);
            node->IntValue = -1;
            return node;
        }
        if (t == "ArrayLength")
        {
            auto node = makeNode(baseId, "Array Length", ScriptNodeCategory::Array);
            node->Inputs.emplace_back(m_NextId++, "Array", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Length", ScriptPinType::Int, ed::PinKind::Output);
            node->IntValue = -1;
            return node;
        }
        if (t == "ArrayClear")
        {
            auto node = makeNode(baseId, "Array Clear", ScriptNodeCategory::Array);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Array", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            node->IntValue = -1;
            return node;
        }

        // ── FLOW CONTROL ── (Sequence/For Loop/For Each Loop — cada um gerencia
        // seus próprios pins de Flow nomeados, sem um "Flow Out" genérico;
        // ver ScriptGraphCompiler::GenerateNode para a geração de código)
        if (t == "Sequence")
        {
            auto node = makeNode(baseId, "Sequence", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            // Começa com 2 pins "Then" — usuário pode adicionar/remover via
            // botões +/- no node (RebuildSequencePins), igual ao node Sequence
            // da Unreal.
            node->Outputs.emplace_back(m_NextId++, "Then 0", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Then 1", ScriptPinType::Flow, ed::PinKind::Output);
            node->IntValue = 2; // espelha o número atual de pins "Then"
            return node;
        }
        if (t == "ForLoop")
        {
            auto node = makeNode(baseId, "For Loop", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "First Index", ScriptPinType::Int, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Last Index", ScriptPinType::Int, ed::PinKind::Input);
            node->Inputs.back().Default.Int = 10; // intervalo inicial razoável: 0..10 inclusive
            node->Outputs.emplace_back(m_NextId++, "Loop Body", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Index", ScriptPinType::Int, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Completed", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "ForEachLoop")
        {
            auto node = makeNode(baseId, "For Each Loop", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            // Pin "Array" começa Wildcard e fixa no tipo concreto assim que
            // conectado — mesmo mecanismo dos nodes genéricos de Array (pin
            // nomeado "Array"/"Item" reaproveita RebuildArrayNodePins e a
            // lógica de aceite de link em script_node_graph.cpp sem precisar
            // de nenhum código adicional lá).
            node->Inputs.emplace_back(m_NextId++, "Array", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Loop Body", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Item", ScriptPinType::Wildcard, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Array Index", ScriptPinType::Int, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Completed", ScriptPinType::Flow, ed::PinKind::Output);
            node->IntValue = -1; // -1 = tipo de array ainda desconhecido (nenhuma conexão ainda)
            return node;
        }

        // ── FlowControl (continuação) — While/Break/Continue/Switch ──────────
        if (t == "WhileLoop")
        {
            auto node = makeNode(baseId, "While Loop", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Condition", ScriptPinType::Bool, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Loop Body", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Completed", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "Break")
        {
            // Terminal — só Flow In, sem Flow Out (igual Return Node). Só
            // faz sentido dentro do Loop Body de um For/For Each/While Loop;
            // não validamos isso no grafo (mesma postura do Return Node fora
            // de uma Function) — usar fora de um loop é erro de C++ na hora
            // de compilar, não um erro detectado no editor.
            auto node = makeNode(baseId, "Break", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            return node;
        }
        if (t == "Continue")
        {
            auto node = makeNode(baseId, "Continue", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            return node;
        }
        if (t == "SwitchOnInt")
        {
            auto node = makeNode(baseId, "Switch on Int", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Selection", ScriptPinType::Int, ed::PinKind::Input);
            // Começa com 2 casos (0 e 1) + Default — mesmo espírito do
            // Sequence, com +/- no node pra ajustar (ver RebuildSwitchPins).
            // Default fica sempre por último na lista de Outputs.
            node->Outputs.emplace_back(m_NextId++, "0", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "1", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Default", ScriptPinType::Flow, ed::PinKind::Output);
            node->IntValue = 2; // espelha o número de casos numerados (sem contar Default)
            return node;
        }
        if (t == "SwitchOnString")
        {
            // Mesmíssimo mecanismo do Switch on Int (RebuildSwitchPins não
            // precisou de nenhuma mudança — já preserva nomes de pins
            // existentes ao crescer/encolher, e pra String o nome do pin
            // É o próprio valor de comparação, editável inline no node, ver
            // script_node_draw.cpp). Só o Selection muda de Int pra String,
            // e o codegen (if/else if em vez de switch — C++ não tem switch
            // de string nativo).
            auto node = makeNode(baseId, "Switch on String", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Selection", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Case 0", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Case 1", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Default", ScriptPinType::Flow, ed::PinKind::Output);
            node->IntValue = 2;
            return node;
        }
        if (t == "Delay")
        {
            // Espera Duration segundos SEM travar o frame — usa uma LISTA
            // de instâncias em voo por node (não um timer único): disparar
            // o mesmo Delay duas vezes roda duas esperas concorrentes e
            // independentes, igual ao node "Delay" da Unreal (ver
            // ScriptGraphCompiler::Generate). V1: só funciona dentro de
            // Event bodies (OnStart/OnUpdate/OnCollision/OnEvent/OnEnd) —
            // dentro de Function ou Loop Body o compilador ignora o delay
            // (segue direto) e avisa no código gerado, porque "pausar no
            // meio de uma iteração/chamada" precisaria preservar o estado
            // do próprio loop/função também, isso fica pra depois.
            auto node = makeNode(baseId, "Delay", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Duration", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.back().Default.Float = 1.0f;
            node->Outputs.emplace_back(m_NextId++, "Completed", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── FUNCTIONS ── (Function Entry/Return Node nascem sem pins de
        // parâmetro — RebuildFunctionNodePins os preenche conforme a
        // assinatura da ScriptFunction. Call nodes não passam por aqui, ver
        // ScriptGraph::AddCallFunctionNode — o nome da função é dinâmico,
        // não dá pra ter um "if (t == ...)" fixo por função.)
        if (t == "FunctionEntry")
        {
            auto node = makeNode(baseId, "Function Entry", ScriptNodeCategory::Function);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "ReturnNode")
        {
            auto node = makeNode(baseId, "Return Node", ScriptNodeCategory::Function);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            return node;
        }

        // ── Reroute — "knot" puramente visual no fio, sem efeito no código
        // gerado. Aceita QUALQUER tipo de pin, inclusive Flow (FindNextFlow
        // Node/FindDataSource no compilador "veem através" dele). Criado
        // principalmente com duplo clique num fio (ver script_node_graph.cpp);
        // existe também no catálogo pra quem preferir criar solto.
        if (t == "Reroute")
        {
            auto node = makeNode(baseId, "Reroute", ScriptNodeCategory::Reroute);
            node->Inputs.emplace_back(m_NextId++, "In", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Out", ScriptPinType::Wildcard, ed::PinKind::Output);
            return node;
        }

        // ── Comment box — anotação visual pura, sem pins, sem efeito no
        // código gerado (o compilador nunca a encontra: sem Flow In/Out,
        // sem links possíveis, simplesmente não participa de nenhum
        // traversal). Usa o mecanismo nativo de Group do imgui-node-editor
        // (ed::Group) — arrastar o título move todos os nodes que estiverem
        // visualmente dentro da caixa, de graça, sem nenhum código nosso
        // pra rastrear "quais nodes pertencem a este comment".
        if (t == "Comment")
        {
            auto node = makeNode(baseId, "Comment", ScriptNodeCategory::Comment);
            node->StringValue = "Comment"; // texto do título, editável inline
            node->CommentSize = ImVec2(320, 240);
            return node;
        }

        // ── COMPONENTES — nodes gerados dinamicamente pelo ScriptAsset ──
        if (t == "GetTransform")
        {
            auto node = makeNode(baseId, "Get Transform", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "Position", ScriptPinType::Vec3, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Rotation", ScriptPinType::Vec3, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Scale", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "SetTransform")
        {
            auto node = makeNode(baseId, "Set Transform", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Position", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Rotation", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Scale", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "GetPosition")
        {
            auto node = makeNode(baseId, "Get Position", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "Position", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "SetPosition")
        {
            auto node = makeNode(baseId, "Set Position", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Position", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "GetRigidbody")
        {
            auto node = makeNode(baseId, "Get Rigidbody", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "Mass", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Velocity", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "SetRigidbodyVelocity")
        {
            auto node = makeNode(baseId, "Set Velocity", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Velocity", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "GetCollider")
        {
            auto node = makeNode(baseId, "Get Collider", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "Is Trigger", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Half Extent", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "GetCharacterController")
        {
            auto node = makeNode(baseId, "Get Character Ctrl", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "Is Grounded", ScriptPinType::Bool, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Velocity", ScriptPinType::Vec3, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Max Speed", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "CharacterMove")
        {
            auto node = makeNode(baseId, "Character Move", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Direction", ScriptPinType::Vec3, ed::PinKind::Input);
            {
                ScriptPin spd(m_NextId++, "Speed", ScriptPinType::Float, ed::PinKind::Input);
                spd.Default.Float = 5.0f;
                node->Inputs.push_back(spd);
            }
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "CharacterJump")
        {
            auto node = makeNode(baseId, "Character Jump", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            {
                ScriptPin force(m_NextId++, "Force", ScriptPinType::Float, ed::PinKind::Input);
                force.Default.Float = 5.0f;
                node->Inputs.push_back(force);
            }
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── Animação (AnimGraph) ──────────────────────────────────────────────
        //
        // Estes nós são a ÚNICA porta entre o gameplay e a animação. O script
        // não conhece estados nem clipes — escreve valores, e o AnimGraph
        // decide. É o que permite reeditar a state machine inteira sem tocar
        // no script.
        if (t == "SetAnimFloat")
        {
            auto node = makeNode(baseId, "Set Anim Float", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            {
                ScriptPin name(m_NextId++, "Parametro", ScriptPinType::String, ed::PinKind::Input);
                name.Default.Str = "Speed";
                node->Inputs.push_back(name);
            }
            {
                ScriptPin val(m_NextId++, "Valor", ScriptPinType::Float, ed::PinKind::Input);
                val.Default.Float = 0.0f;
                node->Inputs.push_back(val);
            }
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "SetAnimBool")
        {
            auto node = makeNode(baseId, "Set Anim Bool", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            {
                ScriptPin name(m_NextId++, "Parametro", ScriptPinType::String, ed::PinKind::Input);
                name.Default.Str = "IsGrounded";
                node->Inputs.push_back(name);
            }
            node->Inputs.emplace_back(m_NextId++, "Valor", ScriptPinType::Bool, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "SetAnimTrigger")
        {
            // Trigger é PULSO, não flag: dispara uma vez e é consumido pela
            // transição que o usou. Por isso não tem pino de valor — não
            // existe "desligar" um trigger, e o script não precisa lembrar
            // de fazê-lo no frame seguinte.
            auto node = makeNode(baseId, "Anim Trigger", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            {
                ScriptPin name(m_NextId++, "Parametro", ScriptPinType::String, ed::PinKind::Input);
                name.Default.Str = "Attack";
                node->Inputs.push_back(name);
            }
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "GetAnimState")
        {
            // Estado atual como string. Serve pra lógica que depende do que o
            // personagem está FAZENDO — ex: não deixar pular durante o ataque.
            auto node = makeNode(baseId, "Get Anim State", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "Estado", ScriptPinType::String, ed::PinKind::Output);
            return node;
        }

        // ── SpringArm ─────────────────────────────────────────────────────────
        if (t == "GetSpringArm")
        {
            auto node = makeNode(baseId, "Get Spring Arm", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "Length", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Height Offset", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Lag Speed", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "SetSpringArm")
        {
            auto node = makeNode(baseId, "Set Spring Arm", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Length", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Height Offset", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── Camera ────────────────────────────────────────────────────────────
        if (t == "GetCamera")
        {
            auto node = makeNode(baseId, "Get Camera", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "FOV", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Near Clip", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Far Clip", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "SetCameraFOV")
        {
            auto node = makeNode(baseId, "Set Camera FOV", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            {
                ScriptPin fov(m_NextId++, "FOV", ScriptPinType::Float, ed::PinKind::Input);
                fov.Default.Float = 60.0f;
                node->Inputs.push_back(fov);
            }
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── Destroy Entity ────────────────────────────────────────────────────
        // ── S3: referencia entre scripts ──────────────────────────────────────
        //
        // A identidade da classe alvo e o UUID do .axescript, guardado em
        // StringValue. O NOME de exibicao fica em StringLocalValue, so para a
        // tela: renomear o asset nao pode quebrar o cast, e dois scripts com o
        // mesmo nome nao podem virar o mesmo alvo.
        if (t == "CastToScript")
        {
            auto node = makeNode(baseId, "Cast To Script", ScriptNodeCategory::FlowControl);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Object", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Succeeded", ScriptPinType::Flow, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Failed", ScriptPinType::Flow, ed::PinKind::Output);
            // A saida "As" carrega a ENTIDADE, nao um ponteiro de script: e o
            // que os outros nodes ja sabem consumir, e um ponteiro cru no grafo
            // sobreviveria a um hot reload como lixo. O tipo e reconferido em
            // cada acesso, que custa um strcmp e nao pode ficar obsoleto.
            node->Outputs.emplace_back(m_NextId++, "As", ScriptPinType::Object, ed::PinKind::Output);
            return node;
        }

        if (t == "GetScriptVar")
        {
            auto node = makeNode(baseId, "Get Script Var", ScriptNodeCategory::Variable);
            node->Inputs.emplace_back(m_NextId++, "Object", ScriptPinType::Object, ed::PinKind::Input);
            // Wildcard ate o autor escolher a variavel no painel: o tipo vem do
            // manifesto do script alvo, e fixar Float aqui obrigaria um Cast
            // logo depois em todo uso que nao fosse Float.
            node->Outputs.emplace_back(m_NextId++, "Value", ScriptPinType::Wildcard, ed::PinKind::Output);
            return node;
        }

        if (t == "SetScriptVar")
        {
            auto node = makeNode(baseId, "Set Script Var", ScriptNodeCategory::Variable);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Object", ScriptPinType::Object, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        if (t == "DestroyEntity")
        {
            auto node = makeNode(baseId, "Destroy Entity", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── Particle System nodes ─────────────────────────────────────────────
        if (t == "ParticlePlay")
        {
            auto node = makeNode(baseId, "Particle Play", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "ParticleStop")
        {
            auto node = makeNode(baseId, "Particle Stop", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "ParticleRestart")
        {
            auto node = makeNode(baseId, "Particle Restart", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "ParticleBurst")
        {
            auto node = makeNode(baseId, "Particle Burst", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Emitter Index", ScriptPinType::Int, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Count", ScriptPinType::Int, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── Audio nodes ───────────────────────────────────────────────────────
        //
        // Dois grupos, com semanticas diferentes:
        //
        //   Audio Play/Stop/Set*  -> operam na AudioSourceComponent de uma
        //                            entidade (Target). Som que PERTENCE a
        //                            um objeto: motor, radio, chuveiro.
        //
        //   Play Sound 2D/At Loc  -> one-shot sem dono. Nao precisa de
        //                            entidade nem de componente.
        //
        // O pin "Sound" e String e recebe o NOME do asset. Um pin de asset
        // seria melhor, mas nao existe no grafo ainda — criar um tipo de pin
        // novo mexe em renderizacao, serializacao e cast, e e patch proprio.
        if (t == "AudioPlay")
        {
            auto node = makeNode(baseId, "Audio Play", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "AudioStop")
        {
            auto node = makeNode(baseId, "Audio Stop", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "AudioSetVolume")
        {
            auto node = makeNode(baseId, "Audio Set Volume", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            auto& v = node->Inputs.emplace_back(m_NextId++, "Volume", ScriptPinType::Float, ed::PinKind::Input);
            v.Default.Float = 1.0f;
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "AudioSetPitch")
        {
            auto node = makeNode(baseId, "Audio Set Pitch", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            auto& pi = node->Inputs.emplace_back(m_NextId++, "Pitch", ScriptPinType::Float, ed::PinKind::Input);
            pi.Default.Float = 1.0f;
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "AudioIsPlaying")
        {
            auto node = makeNode(baseId, "Audio Is Playing", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Playing", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "PlaySound2D")
        {
            auto node = makeNode(baseId, "Play Sound 2D", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Sound", ScriptPinType::String, ed::PinKind::Input);
            auto& v = node->Inputs.emplace_back(m_NextId++, "Volume", ScriptPinType::Float, ed::PinKind::Input);
            v.Default.Float = 1.0f;
            auto& pi = node->Inputs.emplace_back(m_NextId++, "Pitch", ScriptPinType::Float, ed::PinKind::Input);
            pi.Default.Float = 1.0f;
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "PlaySoundAtLocation")
        {
            auto node = makeNode(baseId, "Play Sound At Location", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Sound", ScriptPinType::String, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Location", ScriptPinType::Vec3, ed::PinKind::Input);
            auto& v = node->Inputs.emplace_back(m_NextId++, "Volume", ScriptPinType::Float, ed::PinKind::Input);
            v.Default.Float = 1.0f;
            auto& pi = node->Inputs.emplace_back(m_NextId++, "Pitch", ScriptPinType::Float, ed::PinKind::Input);
            pi.Default.Float = 1.0f;
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── Transform de outras entities ──────────────────────────────────────
        // "Get*" — dados puro (sem Flow). "Set*" — action com Flow.
        // Todos recebem "Target" (Object) — diferente dos existentes que
        // operam em GetTransform() (self). Permitem mover, rotacionar e
        // escalar qualquer entity pela referência no Blueprint.

        if (t == "GetOtherPosition")
        {
            auto node = makeNode(baseId, "Get Position", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Position", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "SetOtherPosition")
        {
            auto node = makeNode(baseId, "Set Position", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Position", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "GetOtherRotation")
        {
            auto node = makeNode(baseId, "Get Rotation", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Rotation", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "SetOtherRotation")
        {
            auto node = makeNode(baseId, "Set Rotation", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Rotation", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "GetOtherScale")
        {
            auto node = makeNode(baseId, "Get Scale", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Scale", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "SetOtherScale")
        {
            auto node = makeNode(baseId, "Set Scale", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Scale", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "GetForwardVector")
        {
            auto node = makeNode(baseId, "Get Forward Vector", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Forward", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "GetRightVector")
        {
            // Par do Get Forward Vector: o "para a direita" DA ENTIDADE.
            // Nao serve pro movimento do player (para isso e o Get Camera
            // Direction, que segue o mouse look) — serve pra mirar, empurrar
            // ou spawnar algo ao lado de um objeto.
            auto node = makeNode(baseId, "Get Right Vector", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Right", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }

        // ── Camera nodes ──────────────────────────────────────────────────────
        if (t == "GetCameraDirection")
        {
            // Movimento relativo a camera (mouse look de third person).
            // Entram os DOIS eixos crus do teclado, sai a direcao ja no
            // referencial da camera — pronta para o pino Direction do
            // Character Move. Os vetores da camera saem tambem, para quem
            // quiser montar outra coisa.
            auto node = makeNode(baseId, "Get Camera Direction", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Forward Axis", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Right Axis", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Direction", ScriptPinType::Vec3, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Camera Forward", ScriptPinType::Vec3, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Camera Right", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        // ── Cutscene ─────────────────────────────────────────────────────
        //
        // O alvo e o NOME da entidade que carrega o Sequence Player. Ver a
        // nota em ScriptSequenceProxy sobre por que nome e nao entidade: quem
        // dispara uma cutscene (um volume, uma tecla, o fim de uma fase) quase
        // nunca tem o objeto dela em maos.
        if (t == "PlaySequence")
        {
            auto node = makeNode(baseId, "Play Sequence", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Entity Name", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "StopSequence")
        {
            auto node = makeNode(baseId, "Stop Sequence", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Entity Name", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "IsSequencePlaying")
        {
            // Sem pinos de Flow: e uma PERGUNTA, nao uma acao. Ligar num
            // Branch e o uso normal ("so abre a porta quando a cutscene
            // acabar").
            auto node = makeNode(baseId, "Is Sequence Playing", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Entity Name", ScriptPinType::String, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Playing", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "CameraShake")
        {
            auto node = makeNode(baseId, "Camera Shake", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Intensity", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Duration", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            node->FloatValue = 0.5f; // default intensity
            return node;
        }
        if (t == "CameraFollow")
        {
            auto node = makeNode(baseId, "Camera Follow", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "CameraStopFollow")
        {
            auto node = makeNode(baseId, "Camera Stop Follow", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "SetCameraFOV")
        {
            auto node = makeNode(baseId, "Set Camera FOV", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "FOV", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── BP_CAMERA_V1 — controle do BRACO em tempo de jogo ────────────────
        //
        // O SpringArm da entidade e a configuracao INICIAL, lida uma vez no
        // OnStart. Estes cinco nos existem para mudar de ideia DURANTE a
        // partida: travar a camera de lado numa secao de plataforma, soltar de
        // volta numa arena, aproximar num corredor.
        //
        // Antes deles nao havia forma nenhuma de travar a camera por script —
        // so Shake, Follow, Stop Follow e FOV.
        if (t == "CameraLockRotation")
        {
            auto node = makeNode(baseId, "Camera Lock Rotation", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Yaw", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Pitch", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "CameraUnlockRotation")
        {
            auto node = makeNode(baseId, "Camera Unlock Rotation", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "CameraSetSocketOffset")
        {
            auto node = makeNode(baseId, "Camera Set Socket Offset", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Offset", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "CameraSetDistance")
        {
            auto node = makeNode(baseId, "Camera Set Distance", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Distance", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }
        if (t == "CameraSetHeight")
        {
            auto node = makeNode(baseId, "Camera Set Height", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Height", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);
            return node;
        }

        // ── Get Self — outputa a entity atual como Object, plugável em Target ─
        // Permite usar os novos Get/Set Position/Rotation/Scale em si mesmo,
        // ou passar para Particle Play etc. com target = self.
        if (t == "GetSelf")
        {
            auto node = makeNode(baseId, "Get Self", ScriptNodeCategory::Action);
            node->Outputs.emplace_back(m_NextId++, "Self", ScriptPinType::Object, ed::PinKind::Output);
            return node;
        }

        // ── IsValid (Entity) — puramente de dados, sem Flow. Útil depois de
        // um Destroy Entity, ou ao guardar referência de array (entidade pode
        // ter sido destruída/recém-reciclada desde então). Checa tanto
        // "não é entt::null" quanto registry.valid() — uma entt::entity não-
        // -nula pode ainda assim ter sido destruída e o slot reaproveitado.
        if (t == "IsValid")
        {
            auto node = makeNode(baseId, "Is Valid", ScriptNodeCategory::Action);
            node->Inputs.emplace_back(m_NextId++, "Target", ScriptPinType::Object, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Result", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }

        // ── Cast nodes ────────────────────────────────────────────────────────
        if (t == "ToFloat")
        {
            auto node = makeNode(baseId, "To Float", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Float", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "ToInt")
        {
            auto node = makeNode(baseId, "To Int", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Int", ScriptPinType::Int, ed::PinKind::Output);
            return node;
        }
        if (t == "ToBool")
        {
            auto node = makeNode(baseId, "To Bool", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Bool", ScriptPinType::Bool, ed::PinKind::Output);
            return node;
        }
        if (t == "ToString")
        {
            auto node = makeNode(baseId, "To String", ScriptNodeCategory::Print);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Wildcard, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "String", ScriptPinType::String, ed::PinKind::Output);
            return node;
        }
        if (t == "ToVec3")
        {
            auto node = makeNode(baseId, "To Vec3", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "X", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Y", ScriptPinType::Float, ed::PinKind::Input);
            node->Inputs.emplace_back(m_NextId++, "Z", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Vec3", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }
        if (t == "BreakVec3")
        {
            auto node = makeNode(baseId, "Break Vec3", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Vec3", ScriptPinType::Vec3, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "X", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Y", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Z", ScriptPinType::Float, ed::PinKind::Output);
            return node;
        }
        if (t == "FloatToVec3")
        {
            auto node = makeNode(baseId, "Float to Vec3", ScriptNodeCategory::Math);
            node->Inputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Input);
            node->Outputs.emplace_back(m_NextId++, "Vec3", ScriptPinType::Vec3, ed::PinKind::Output);
            return node;
        }

        // BUGFIX: este warning estava no MEIO da cadeia de ifs (antes da seção
        // de Cast nodes), disparando uma mensagem falsa de "tipo desconhecido"
        // para TODO node de Cast (ToFloat/ToInt/ToBool/ToString/ToVec3/
        // BreakVec3/FloatToVec3) criado — e cast nodes são inseridos
        // automaticamente o tempo todo, ao conectar pins incompatíveis, então
        // isso poluía o Script Console com warnings falsos numa operação
        // normal e frequente. Só dispara aqui agora, quando o tipo de fato
        // não bateu com NENHUM if acima (logo antes do return nullptr real).
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

    // S1 — AddLink passa a impor CARDINALIDADE, e não apenas registrar o fio.
    //
    // Regra (a mesma do Control Rig, pelas mesmas razões):
    //   • pin de ENTRADA de dados aceita UM fio  — dois valores no mesmo pin
    //     não têm desempate, e FindDataSource() do compilador pega o primeiro
    //     do vector: o programa mudava de comportamento ao reordenar links.
    //   • pin de SAÍDA de Flow aceita UM destino — dois destinos deixariam a
    //     ORDEM de execução indefinida, que é justamente o que o fio de Flow
    //     existe para definir.
    //   • pin de SAÍDA de dados pode se espalhar para N destinos (fan-out
    //     legítimo: um Speed alimenta cinco nodes).
    //   • pin de ENTRADA de Flow pode receber N fios (convergência legítima:
    //     dois caminhos chegando no mesmo Print String).
    //
    // Religar SUBSTITUI o fio anterior em vez de recusar — mesma escolha do
    // rig. Recusar obrigaria o autor a apagar o fio antigo antes de puxar o
    // novo, um passo a mais para expressar "quero que venha daqui agora".
    ScriptLink* ScriptGraph::AddLink(ed::PinId startPin, ed::PinId endPin)
    {
        const ScriptPin* sp = FindPin(startPin);
        const ScriptPin* ep = FindPin(endPin);

        if (sp && ep)
        {
            const bool isFlow = (sp->Type == ScriptPinType::Flow || ep->Type == ScriptPinType::Flow);

            // Entrada de dados ocupada → o fio antigo sai.
            if (!isFlow)
            {
                m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                    [&](const ScriptLink& l) { return l.EndPin == endPin; }),
                    m_Links.end());
            }
            // Saída de Flow ocupada → o destino antigo sai.
            else
            {
                m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                    [&](const ScriptLink& l) { return l.StartPin == startPin; }),
                    m_Links.end());
            }
        }

        int id = m_NextId++;
        m_Links.emplace_back(id, startPin, endPin);
        AXE_CORE_INFO("ScriptGraph: link {} → {}", startPin.Get(), endPin.Get());
        return &m_Links.back();
    }

    // ─── S1: consulta e integridade das ligações ──────────────────────────────

    const ScriptNode* ScriptGraph::FindNodeOfPin(ed::PinId id) const
    {
        for (const auto& n : m_Nodes)
        {
            for (const auto& p : n->Inputs)  if (p.ID == id) return n.get();
            for (const auto& p : n->Outputs) if (p.ID == id) return n.get();
        }
        return nullptr;
    }

    ScriptNode* ScriptGraph::FindNodeOfPin(ed::PinId id)
    {
        return const_cast<ScriptNode*>(
            static_cast<const ScriptGraph*>(this)->FindNodeOfPin(id));
    }

    bool ScriptGraph::WouldCreateDataCycle(ed::PinId fromOutput, ed::PinId toInput) const
    {
        const ScriptNode* src = FindNodeOfPin(fromOutput);
        const ScriptNode* dst = FindNodeOfPin(toInput);
        if (!src || !dst) return false;
        if (src == dst)   return true;   // auto-ligação já é o ciclo mínimo

        // Existe caminho de DADOS de dst de volta até src? Se existe, fechar
        // src → dst fecha o laço. Busca em profundidade iterativa: o grafo é
        // pequeno e recursão aqui seria trocar um estouro de pilha por outro.
        std::vector<const ScriptNode*> stack{ dst };
        std::vector<const ScriptNode*> seen;

        while (!stack.empty())
        {
            const ScriptNode* cur = stack.back();
            stack.pop_back();

            if (cur == src) return true;
            if (std::find(seen.begin(), seen.end(), cur) != seen.end()) continue;
            seen.push_back(cur);

            // Para onde os pins de SAÍDA de dados de 'cur' apontam?
            for (const auto& out : cur->Outputs)
            {
                if (out.Type == ScriptPinType::Flow) continue;
                for (const auto& l : m_Links)
                {
                    if (l.StartPin != out.ID) continue;
                    if (const ScriptNode* next = FindNodeOfPin(l.EndPin))
                        stack.push_back(next);
                }
            }
        }
        return false;
    }

    ScriptLinkQuery ScriptGraph::QueryLink(ed::PinId a, ed::PinId b) const
    {
        ScriptLinkQuery q;
        auto* self = const_cast<ScriptGraph*>(this);

        ScriptPin* pA = self->FindPin(a);
        ScriptPin* pB = self->FindPin(b);

        // 1 — Os dois pins existem e são distintos.
        if (!pA || !pB || pA == pB)
        {
            q.Message = "Invalid pin";
            return q;
        }

        // 2 — Direção: exatamente um Output e um Input.
        ScriptPin* o = (pA->Kind == ed::PinKind::Output) ? pA : pB;
        ScriptPin* i = (pA->Kind == ed::PinKind::Input) ? pA : pB;
        if (o->Kind != ed::PinKind::Output || i->Kind != ed::PinKind::Input)
        {
            q.Message = "Connect Output -> Input";
            return q;
        }

        // 3 — Mesmo node. Não existia checagem: dava para ligar a saída de um
        //     node na entrada dele mesmo e só descobrir no Compilar.
        const ScriptNode* nodeO = FindNodeOfPin(o->ID);
        const ScriptNode* nodeI = FindNodeOfPin(i->ID);
        if (nodeO && nodeO == nodeI)
        {
            q.Message = "A node cannot connect to itself";
            return q;
        }

        // 4 — Flow e dados nunca se misturam.
        const bool oFlow = (o->Type == ScriptPinType::Flow);
        const bool iFlow = (i->Type == ScriptPinType::Flow);
        const bool oWild = (o->Type == ScriptPinType::Wildcard);
        const bool iWild = (i->Type == ScriptPinType::Wildcard);
        // Reroute é a única exceção: ele transporta Flow também, e por isso
        // seus pins nascem Wildcard.
        const bool oIsReroute = (nodeO && nodeO->Name == "Reroute");
        const bool iIsReroute = (nodeI && nodeI->Name == "Reroute");
        if (oFlow != iFlow && !(oWild && oIsReroute) && !(iWild && iIsReroute))
        {
            q.Message = "Flow cannot connect to data";
            return q;
        }

        // 5 — Ciclo de dados. Só para fios de dados (ver WouldCreateDataCycle).
        if (!oFlow && !iFlow && WouldCreateDataCycle(o->ID, i->ID))
        {
            q.Message = "Data cycle - the value would depend on itself";
            return q;
        }

        // A partir daqui a ligação é possível; falta decidir COMO.
        // ReplacesExisting não bloqueia, só informa.
        for (const auto& l : m_Links)
        {
            if (!iFlow && l.EndPin == i->ID) { q.ReplacesExisting = true; break; }
            if (oFlow && l.StartPin == o->ID) { q.ReplacesExisting = true; break; }
        }

        // 6 — Tipos idênticos.
        if (ArePinsExact(o->Type, i->Type))
        {
            // Dois Reroutes ainda sem tipo fixado: aceita e não fixa nada — a
            // cadeia inteira se resolve assim que qualquer ponta encostar em
            // algo concreto.
            q.Action = ScriptLinkAction::Accept;
            q.LinkColorType = o->Type;
            // Ligação trivial não merece tooltip: o canvas só mostra balão
            // quando há algo a avisar. Mensagem vazia = silêncio.
            q.Message = q.ReplacesExisting ? "Replaces the existing connection" : "";
            return q;
        }

        // 7 — Wildcard de Reroute recebendo/entregando tipo concreto: o
        //     Reroute assume o tipo dos DOIS lados de uma vez, então a ponta
        //     ainda solta já nasce pronta.
        if (oWild != iWild && (oIsReroute || iIsReroute))
        {
            const ScriptNode* wildNode = oWild ? nodeO : nodeI;
            if (wildNode && wildNode->Name == "Reroute")
            {
                q.Action = ScriptLinkAction::AdoptWildcard;
                q.AdoptType = oWild ? i->Type : o->Type;
                q.AdoptNode = wildNode;
                q.LinkColorType = q.AdoptType;
                q.Message = "Reroute adopts the type";
                return q;
            }
        }

        // 8 — Wildcard de node genérico de Array (Array Add/Get/Length/...)
        //     encostando num pin de array real: fixa o node no tipo concreto.
        if ((oWild && IsArrayPinType(i->Type)) || (iWild && IsArrayPinType(o->Type)))
        {
            q.Action = ScriptLinkAction::AdoptArrayWildcard;
            q.AdoptType = oWild ? i->Type : o->Type;
            q.AdoptNode = oWild ? nodeO : nodeI;
            q.LinkColorType = q.AdoptType;
            q.Message = "Array adopts the element type";
            return q;
        }

        // 9 — Wildcard de node de Cast (ToFloat/ToInt/ToBool/ToString): a
        //     validação fina é IsWildcardCastCompatible, que já existia no
        //     header e nunca era chamada.
        if (iWild && !oWild)
        {
            ScriptPinType castOut = ScriptPinType::Wildcard;
            if (nodeI) for (const auto& p : nodeI->Outputs)
                if (p.Type != ScriptPinType::Flow) { castOut = p.Type; break; }

            if (castOut != ScriptPinType::Wildcard &&
                IsWildcardCastCompatible(o->Type, castOut))
            {
                q.Action = ScriptLinkAction::Accept;
                q.LinkColorType = o->Type;
                q.Message = "Connect (cast)";
                return q;
            }
            q.Message = "This Cast does not accept that input type";
            return q;
        }

        // 10 — Vec3 ↔ Vec4: cast implícito, aceito com aviso visual.
        auto isVec = [](ScriptPinType t) {
            return t == ScriptPinType::Vec3 || t == ScriptPinType::Vec4;
            };
        if (isVec(o->Type) && isVec(i->Type))
        {
            q.Action = ScriptLinkAction::AcceptImplicit;
            q.LinkColorType = i->Type;
            q.Message = "Implicit cast Vec3 <-> Vec4";
            return q;
        }

        // 11 — Conversão numérica/textual: insere o node de Cast no meio.
        auto isNumeric = [](ScriptPinType t) {
            return t == ScriptPinType::Float || t == ScriptPinType::Int || t == ScriptPinType::Bool;
            };
        if (isNumeric(o->Type))
        {
            if (i->Type == ScriptPinType::Float) { q.ConversionNode = "ToFloat";  q.Message = "Insert To Float (automatic)"; }
            else if (i->Type == ScriptPinType::Int) { q.ConversionNode = "ToInt";    q.Message = "Insert To Int (automatic)"; }
            else if (i->Type == ScriptPinType::Bool) { q.ConversionNode = "ToBool";   q.Message = "Insert To Bool (automatic)"; }
            else if (i->Type == ScriptPinType::String) { q.ConversionNode = "ToString"; q.Message = "Insert To String (automatic)"; }
        }
        else if (isVec(o->Type) && i->Type == ScriptPinType::String)
        {
            q.ConversionNode = "ToString"; q.Message = "Insert To String (automatic)";
        }

        if (q.ConversionNode)
        {
            q.Action = ScriptLinkAction::InsertConversion;
            q.LinkColorType = i->Type;
            return q;
        }

        // 12 — Sem caminho. A mensagem específica já existia e continua sendo
        //      a fonte do tooltip.
        q.Message = GetPinIncompatibleReason(o->Type, i->Type);
        return q;
    }

    int ScriptGraph::SanitizeLinks()
    {
        int removed = 0;
        std::vector<ScriptLink> kept;
        kept.reserve(m_Links.size());

        for (const auto& l : m_Links)
        {
            ScriptPin* sp = FindPin(l.StartPin);
            ScriptPin* ep = FindPin(l.EndPin);

            // Órfão: o pin sumiu (node deletado, assinatura de Function mudada).
            if (!sp || !ep) { removed++; continue; }

            // Invertido: os dois no mesmo sentido.
            if (sp->Kind != ed::PinKind::Output || ep->Kind != ed::PinKind::Input)
            {
                removed++; continue;
            }

            const bool isFlow = (sp->Type == ScriptPinType::Flow || ep->Type == ScriptPinType::Flow);

            bool duplicate = false;
            for (const auto& k : kept)
            {
                if (!isFlow && k.EndPin == l.EndPin) { duplicate = true; break; }
                if (isFlow && k.StartPin == l.StartPin) { duplicate = true; break; }
            }
            if (duplicate) { removed++; continue; }

            kept.push_back(l);
        }

        if (removed > 0)
        {
            AXE_CORE_WARN("ScriptGraph: {} ligacao(oes) invalida(s) descartada(s) no load "
                "(pin inexistente, sentido invertido ou pin ja ocupado).", removed);
            m_Links = std::move(kept);
        }
        return removed;
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

    void ScriptGraph::RebuildAxisOutputPins(ScriptNode* node, int axisValueType)
    {
        if (!node) return;
        // Idempotente: já está na configuração certa (mesmo número de pins
        // esperado para este AxisValueType) — não faz nada. Evita reconstruir
        // (e perder links válidos) a cada frame só porque a função é chamada
        // continuamente enquanto o node é desenhado.
        int expectedCount = (axisValueType == 0) ? 1 : (axisValueType == 1) ? 2 : 3;
        if (node->IntValue == axisValueType && (int)node->Outputs.size() == expectedCount)
            return;

        // Remove qualquer link que referenciava os pins antigos ANTES de
        // destruí-los — um pin removido com link ainda apontando para ele
        // deixaria o link "pendurado", referenciando um PinId que não existe
        // mais em nenhum node (FindPin retornaria nullptr para ele depois).
        for (auto& oldPin : node->Outputs)
        {
            m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                [&](const ScriptLink& l) { return l.StartPin == oldPin.ID || l.EndPin == oldPin.ID; }),
                m_Links.end());
        }

        node->Outputs.clear();
        if (axisValueType == 0)
        {
            node->Outputs.emplace_back(m_NextId++, "Value", ScriptPinType::Float, ed::PinKind::Output);
        }
        else if (axisValueType == 1)
        {
            node->Outputs.emplace_back(m_NextId++, "X", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Y", ScriptPinType::Float, ed::PinKind::Output);
        }
        else // 2 = Axis3D
        {
            node->Outputs.emplace_back(m_NextId++, "X", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Y", ScriptPinType::Float, ed::PinKind::Output);
            node->Outputs.emplace_back(m_NextId++, "Z", ScriptPinType::Float, ed::PinKind::Output);
        }
        node->IntValue = axisValueType;

        AXE_CORE_INFO("ScriptGraph: Get Axis '{}' reconstruído para AxisValueType={} ({} pins)",
            node->StringValue, axisValueType, expectedCount);
    }

    void ScriptGraph::RebuildArrayNodePins(ScriptNode* node, ScriptVarType arrayElementType)
    {
        if (!node) return;

        // node->IntValue guarda o ScriptVarType ESCALAR do elemento (não o
        // *Array) — comparação simples como int evita repetir o cast em cada
        // chamador. -1 = ainda desconectado.
        int newTypeIdx = (int)arrayElementType;
        if (node->IntValue == newTypeIdx) return; // idempotente — já está certo

        ScriptPinType elemPinType = ScriptVarTypeToPinType(arrayElementType);
        ScriptPinType arrayPinType = ScriptVarTypeToPinType(GetArrayType(arrayElementType));

        // Diferente de RebuildAxisOutputPins, aqui NÃO removemos/recriamos
        // pins — a contagem é fixa por node (Array/Item/Index/Length sempre
        // existem), só o TIPO de cada pin muda. Isso preserva qualquer link
        // já existente automaticamente, desde que o novo tipo continue
        // compatível (mesma família, então sempre compatível entre si).
        for (auto& pin : node->Inputs)
        {
            if (pin.Name == "Array") pin.Type = arrayPinType;
            if (pin.Name == "Item")  pin.Type = elemPinType;
        }
        for (auto& pin : node->Outputs)
        {
            if (pin.Name == "Item")  pin.Type = elemPinType;
        }

        node->IntValue = newTypeIdx;

        AXE_CORE_INFO("ScriptGraph: node de array '{}' fixado no tipo '{}'",
            node->Name, ScriptVarTypeToString(arrayElementType));
    }

    void ScriptGraph::RebuildSequencePins(ScriptNode* node, int pinCount)
    {
        if (!node) return;
        if (pinCount < 2) pinCount = 2;
        if (pinCount > 8) pinCount = 8;

        int current = (int)node->Outputs.size();
        if (current == pinCount) { node->IntValue = pinCount; return; } // idempotente

        if (pinCount < current)
        {
            // Remove pins "Then" do fim para o início, e qualquer link que
            // apontava para eles — mesmo cuidado de RebuildAxisOutputPins:
            // remover o pin sem remover o link primeiro deixaria o link
            // "pendurado", referenciando um PinId que não existe mais.
            for (int i = current - 1; i >= pinCount; i--)
            {
                auto& pin = node->Outputs[i];
                m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                    [&](const ScriptLink& l) { return l.StartPin == pin.ID || l.EndPin == pin.ID; }),
                    m_Links.end());
            }
            node->Outputs.erase(node->Outputs.begin() + pinCount, node->Outputs.end());
        }
        else
        {
            // Adiciona novos pins "Then N" no final — pins (e links) já
            // existentes ficam intactos, diferente de RebuildAxisOutputPins
            // que sempre limpa tudo (lá o número de pins está sempre 1:1 com
            // o AxisValueType, aqui o usuário controla incrementalmente).
            for (int i = current; i < pinCount; i++)
            {
                std::string name = "Then " + std::to_string(i);
                node->Outputs.emplace_back(m_NextId++, name.c_str(), ScriptPinType::Flow, ed::PinKind::Output);
            }
        }

        node->IntValue = pinCount;

        AXE_CORE_INFO("ScriptGraph: Sequence reconstruído para {} pins 'Then'", pinCount);
    }

    void ScriptGraph::RebuildSwitchPins(ScriptNode* node, int caseCount)
    {
        if (!node) return;
        if (caseCount < 1) caseCount = 1;
        if (caseCount > 16) caseCount = 16; // limite de sanidade — não tem motivo prático pra mais

        int current = (int)node->Outputs.size() - 1; // -1 por causa do "Default", sempre presente
        if (current == caseCount) { node->IntValue = caseCount; return; } // idempotente

        // Tira o "Default" temporariamente — ele é sempre o último elemento
        // e nunca é tocado, então é mais simples remover, ajustar os case
        // pins como um vetor "normal" (mesma técnica de RebuildSequencePins),
        // e devolver no fim.
        ScriptPin defaultPin = node->Outputs.back();
        node->Outputs.pop_back();

        if (caseCount < current)
        {
            for (int i = current - 1; i >= caseCount; i--)
            {
                auto& pin = node->Outputs[i];
                m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                    [&](const ScriptLink& l) { return l.StartPin == pin.ID || l.EndPin == pin.ID; }),
                    m_Links.end());
            }
            node->Outputs.erase(node->Outputs.begin() + caseCount, node->Outputs.end());
        }
        else
        {
            for (int i = current; i < caseCount; i++)
            {
                std::string name = std::to_string(i);
                node->Outputs.emplace_back(m_NextId++, name.c_str(), ScriptPinType::Flow, ed::PinKind::Output);
            }
        }

        node->Outputs.push_back(defaultPin); // "Default" de volta, sempre no fim
        node->IntValue = caseCount;

        AXE_CORE_INFO("ScriptGraph: Switch on Int reconstruído para {} casos + Default", caseCount);
    }

    void ScriptGraph::RebuildLogicInputs(ScriptNode* node, int inputCount)
    {
        if (!node) return;
        if (inputCount < 2) inputCount = 2;
        if (inputCount > 8) inputCount = 8; // A..H — limite de sanidade

        int current = (int)node->Inputs.size();
        if (current == inputCount) { node->IntValue = inputCount; return; } // idempotente

        if (inputCount < current)
        {
            for (int i = current - 1; i >= inputCount; i--)
            {
                auto& pin = node->Inputs[i];
                m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                    [&](const ScriptLink& l) { return l.StartPin == pin.ID || l.EndPin == pin.ID; }),
                    m_Links.end());
            }
            node->Inputs.erase(node->Inputs.begin() + inputCount, node->Inputs.end());
        }
        else
        {
            for (int i = current; i < inputCount; i++)
            {
                std::string name(1, (char)('A' + i)); // A, B, C, D...
                node->Inputs.emplace_back(m_NextId++, name.c_str(), ScriptPinType::Bool, ed::PinKind::Input);
            }
        }

        node->IntValue = inputCount;
        AXE_CORE_INFO("ScriptGraph: {} reconstruído para {} inputs (A..{})",
            node->Name, inputCount, (char)('A' + inputCount - 1));
    }

    void ScriptGraph::RebuildFunctionNodePins(ScriptNode* node, const ScriptFunction& func)
    {
        if (!node) return;

        bool isEntry = (node->Name == "Function Entry");
        bool isReturn = (node->Name == "Return Node");

        // Sincroniza um vetor de pins (Inputs OU Outputs) com a lista de
        // parâmetros atual. O índice 0 é sempre o pin fixo de Flow (In ou
        // Out, conforme o caso) e nunca é tocado aqui. Pins de parâmetro são
        // casados por NOME — se o nome não mudou, o pin (e qualquer link
        // nele) é preservado mesmo que o TIPO tenha mudado (só atualiza
        // Type); renomear um parâmetro é tratado como remover+adicionar
        // (perde o link antigo — mesma limitação que renomear uma variável).
        auto syncParams = [&](std::vector<ScriptPin>& pins,
            const std::vector<ScriptFunctionParam>& params, ed::PinKind kind)
            {
                const size_t fixed = 1; // Flow In/Out, sempre no índice 0

                // Remove pins cujo nome saiu da assinatura (e os links deles)
                for (size_t i = pins.size(); i-- > fixed; )
                {
                    bool stillExists = false;
                    for (auto& p : params) if (p.Name == pins[i].Name) { stillExists = true; break; }
                    if (!stillExists)
                    {
                        auto& pin = pins[i];
                        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                            [&](const ScriptLink& l) { return l.StartPin == pin.ID || l.EndPin == pin.ID; }),
                            m_Links.end());
                        pins.erase(pins.begin() + i);
                    }
                }

                // Reordena/atualiza pra bater com a ordem atual dos parâmetros
                std::vector<ScriptPin> existing(pins.begin() + fixed, pins.end());
                pins.erase(pins.begin() + fixed, pins.end());
                for (auto& param : params)
                {
                    ScriptPinType pinType = ScriptVarTypeToPinType(param.Type);
                    bool found = false;
                    for (auto& old : existing)
                        if (old.Name == param.Name) { old.Type = pinType; pins.push_back(old); found = true; break; }
                    if (!found)
                        pins.emplace_back(m_NextId++, param.Name.c_str(), pinType, kind);
                }
            };

        if (isEntry)
            syncParams(node->Outputs, func.Inputs, ed::PinKind::Output);
        else if (isReturn)
            syncParams(node->Inputs, func.Outputs, ed::PinKind::Input);
        else // Call <Function> — tem os dois lados
        {
            syncParams(node->Inputs, func.Inputs, ed::PinKind::Input);
            syncParams(node->Outputs, func.Outputs, ed::PinKind::Output);
        }

        AXE_CORE_INFO("ScriptGraph: pins de '{}' reconstruídos pra assinatura de '{}'",
            node->Name, func.Name);
    }

    ScriptNode* ScriptGraph::AddCallFunctionNode(const ScriptFunction& func)
    {
        int id = m_NextId++;
        auto node = std::make_unique<ScriptNode>(id, ("Call " + func.Name).c_str(), ScriptNodeCategory::Function);
        node->StringValue = func.Name; // chave de lookup — qual ScriptFunction este node chama
        node->Inputs.emplace_back(m_NextId++, "Flow In", ScriptPinType::Flow, ed::PinKind::Input);
        node->Outputs.emplace_back(m_NextId++, "Flow Out", ScriptPinType::Flow, ed::PinKind::Output);

        auto* ptr = node.get();
        m_Nodes.push_back(std::move(node));
        RebuildFunctionNodePins(ptr, func);
        AXE_CORE_INFO("ScriptGraph: node 'Call {}' adicionado (id={})", func.Name, id);
        return ptr;
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
            jn["category"] = ScriptNodeCategoryToString(node->Category);
            jn["pos"] = { node->Position.x, node->Position.y };
            jn["str_val"] = node->StringValue;
            jn["flt_val"] = node->FloatValue;
            jn["bool_val"] = node->BoolValue;
            jn["int_local"] = node->IntLocalValue;
            jn["vec3_val"] = { node->Vec3Value[0], node->Vec3Value[1], node->Vec3Value[2] };
            jn["int_val"] = node->IntValue;
            jn["str_local"] = node->StringLocalValue;
            jn["comment_w"] = node->CommentSize.x;
            jn["comment_h"] = node->CommentSize.y;
            jn["comment_color"] = { node->CommentColor[0], node->CommentColor[1], node->CommentColor[2] };

            for (const auto& pin : node->Inputs)
                jn["inputs"].push_back({
                    {"id", (int)pin.ID.Get()}, {"name", pin.Name},
                    {"type", ScriptPinTypeToString(pin.Type)}, {"kind", (int)pin.Kind},
                    // SC17 — um bloco "default" so, no lugar dos cinco campos.
                    // Os nomes antigos NAO sao mais gravados: manter os dois
                    // formatos garantiria que um dia divergissem, e a leitura
                    // ja aceita o antigo (ver DeserializeLegacyPin).
                    {"default", pin.Default.Serialize()}
                    });
            for (const auto& pin : node->Outputs)
                jn["outputs"].push_back({
                    {"id", (int)pin.ID.Get()}, {"name", pin.Name},
                    {"type", ScriptPinTypeToString(pin.Type)}, {"kind", (int)pin.Kind}
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

    // ─── SC8: copiar / colar um pedaço do grafo ───────────────────────────────

    nlohmann::json ScriptGraph::SerializeSubset(const std::vector<ed::NodeId>& nodes) const
    {
        // Reaproveita o Serialize() inteiro e filtra. Percorrer m_Nodes de novo
        // aqui, montando o JSON campo a campo, criaria uma SEGUNDA descrição do
        // que é um node — e a próxima vez que ScriptNode ganhasse um campo, uma
        // das duas ficaria para trás em silêncio. O custo é serializar o grafo
        // todo para copiar três nodes; é irrelevante num gesto de teclado.
        nlohmann::json full = Serialize();
        nlohmann::json out;
        out["next_id"] = 1;
        out["nodes"] = nlohmann::json::array();
        out["links"] = nlohmann::json::array();

        auto wanted = [&](int id) {
            for (const auto& n : nodes) if ((int)n.Get() == id) return true;
            return false;
            };

        // Pins que pertencem aos nodes copiados — só links entre eles entram.
        std::vector<int> pinIds;

        for (const auto& jn : full.value("nodes", nlohmann::json::array()))
        {
            if (!wanted(jn["id"].get<int>())) continue;
            out["nodes"].push_back(jn);
            for (const auto& jp : jn.value("inputs", nlohmann::json::array()))
                pinIds.push_back(jp["id"].get<int>());
            for (const auto& jp : jn.value("outputs", nlohmann::json::array()))
                pinIds.push_back(jp["id"].get<int>());
        }

        auto hasPin = [&](int id) {
            return std::find(pinIds.begin(), pinIds.end(), id) != pinIds.end();
            };

        for (const auto& jl : full.value("links", nlohmann::json::array()))
        {
            const int s = jl["start"].get<int>();
            const int e = jl["end"].get<int>();
            if (hasPin(s) && hasPin(e))
                out["links"].push_back(jl);
        }

        return out;
    }

    std::vector<ed::NodeId> ScriptGraph::PasteSubset(const nlohmann::json& j, ImVec2 offset)
    {
        std::vector<ed::NodeId> created;

        // Mapa id-antigo -> id-novo, para nodes e pins. Sem ele os links
        // colados apontariam para os pins do ORIGINAL, e a cópia nasceria
        // ligada ao que foi copiado.
        std::vector<std::pair<int, int>> pinRemap;

        auto parsePinType = [](const nlohmann::json& jp) -> ScriptPinType
            {
                if (jp["type"].is_string()) return ScriptPinTypeFromString(jp["type"].get<std::string>());
                return (ScriptPinType)jp["type"].get<int>();
            };

        for (const auto& jn : j.value("nodes", nlohmann::json::array()))
        {
            std::string name = jn["name"];
            ScriptNodeCategory cat;
            if (jn["category"].is_string())
                cat = ScriptNodeCategoryFromString(jn["category"].get<std::string>());
            else
                cat = (ScriptNodeCategory)jn["category"].get<int>();

            auto node = std::make_unique<ScriptNode>(m_NextId++, name.c_str(), cat);

            node->Position = { (float)jn["pos"][0] + offset.x, (float)jn["pos"][1] + offset.y };
            node->StringValue = jn.value("str_val", "");
            node->FloatValue = jn.value("flt_val", 0.0f);
            node->BoolValue = jn.value("bool_val", false);
            node->IntLocalValue = jn.value("int_local", 0);
            if (jn.contains("vec3_val") && jn["vec3_val"].is_array() && jn["vec3_val"].size() >= 3)
            {
                node->Vec3Value[0] = jn["vec3_val"][0];
                node->Vec3Value[1] = jn["vec3_val"][1];
                node->Vec3Value[2] = jn["vec3_val"][2];
            }
            node->IntValue = jn.value("int_val", 0);
            node->StringLocalValue = jn.value("str_local", "");
            node->CommentSize.x = jn.value("comment_w", 320.0f);
            node->CommentSize.y = jn.value("comment_h", 240.0f);
            if (jn.contains("comment_color") && jn["comment_color"].is_array() && jn["comment_color"].size() == 3)
            {
                node->CommentColor[0] = jn["comment_color"][0];
                node->CommentColor[1] = jn["comment_color"][1];
                node->CommentColor[2] = jn["comment_color"][2];
            }

            for (const auto& jp : jn.value("inputs", nlohmann::json::array()))
            {
                const int oldId = jp["id"].get<int>();
                const int newId = m_NextId++;
                pinRemap.emplace_back(oldId, newId);

                ScriptPin pin(newId, jp["name"].get<std::string>().c_str(),
                    parsePinType(jp), (ed::PinKind)jp["kind"].get<int>());
                // SC17 — formato novo se existir, antigo caso contrario. Todo
                // .axescript gravado antes deste patch cai no segundo ramo.
                if (jp.contains("default")) pin.Default.Deserialize(jp["default"]);
                else                        pin.Default.DeserializeLegacyPin(jp);
                node->Inputs.push_back(std::move(pin));
            }

            for (const auto& jp : jn.value("outputs", nlohmann::json::array()))
            {
                const int oldId = jp["id"].get<int>();
                const int newId = m_NextId++;
                pinRemap.emplace_back(oldId, newId);

                node->Outputs.emplace_back(newId, jp["name"].get<std::string>().c_str(),
                    parsePinType(jp), (ed::PinKind)jp["kind"].get<int>());
            }

            created.push_back(node->ID);
            m_Nodes.push_back(std::move(node));
        }

        auto remap = [&](int oldId, int& out) {
            for (const auto& pr : pinRemap)
                if (pr.first == oldId) { out = pr.second; return true; }
            return false;
            };

        for (const auto& jl : j.value("links", nlohmann::json::array()))
        {
            int s = 0, e = 0;
            if (!remap(jl["start"].get<int>(), s)) continue;
            if (!remap(jl["end"].get<int>(), e)) continue;
            m_Links.emplace_back(m_NextId++, ed::PinId(s), ed::PinId(e));
        }

        AXE_CORE_INFO("ScriptGraph: {} node(s) colado(s).", created.size());
        return created;
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
            // Aceita os dois formatos: string (novo, robusto contra reordenar
            // o enum) e int cru (formato antigo — best-effort, arquivos
            // salvos com esse formato podem já estar com a categoria errada
            // se foram salvos antes de alguma categoria nova ser inserida;
            // não tenta "adivinhar"/corrigir isso retroativamente).
            ScriptNodeCategory cat;
            if (jn["category"].is_string())
                cat = ScriptNodeCategoryFromString(jn["category"].get<std::string>());
            else
                cat = (ScriptNodeCategory)jn["category"].get<int>();

            auto node = std::make_unique<ScriptNode>(id, name.c_str(), cat);
            node->Position = { jn["pos"][0], jn["pos"][1] };

            // ── SC10: conserta posicao infinita gravada por versao com bug ────
            //
            // ed::GetNodePosition devolve (FLT_MAX, FLT_MAX) para um node que o
            // canvas ainda nao desenhou, e uma versao anterior gravava esse
            // valor direto no asset. O JSON aceita, o load aceita, e so o Fit
            // denuncia — calculando os limites do conteudo com infinito ele
            // some com a visao inteira e o grafo vira um ponto, sem erro
            // nenhum no console.
            //
            // Zerar aqui e seguro: node em (0,0) fica visivel e arrastavel, e
            // o autor conserta em um segundo. Deixar passar custa o arquivo.
            if (!std::isfinite(node->Position.x) || !std::isfinite(node->Position.y) ||
                std::fabs(node->Position.x) > 1.0e6f || std::fabs(node->Position.y) > 1.0e6f)
            {
                AXE_CORE_WARN("ScriptGraph: node '{}' tinha posicao invalida no arquivo "
                    "({}, {}) — reposicionado na origem.",
                    name, node->Position.x, node->Position.y);
                node->Position = { 0.0f, 0.0f };
            }
            node->StringValue = jn.value("str_val", "");
            node->FloatValue = jn.value("flt_val", 0.0f);
            node->BoolValue = jn.value("bool_val", false);
            node->IntLocalValue = jn.value("int_local", 0);
            if (jn.contains("vec3_val") && jn["vec3_val"].is_array() && jn["vec3_val"].size() >= 3)
            {
                node->Vec3Value[0] = jn["vec3_val"][0];
                node->Vec3Value[1] = jn["vec3_val"][1];
                node->Vec3Value[2] = jn["vec3_val"][2];
            }
            node->IntValue = jn.value("int_val", 0);
            node->StringLocalValue = jn.value("str_local", "");
            node->CommentSize.x = jn.value("comment_w", 320.0f);
            node->CommentSize.y = jn.value("comment_h", 240.0f);
            if (jn.contains("comment_color") && jn["comment_color"].is_array() && jn["comment_color"].size() == 3)
            {
                node->CommentColor[0] = jn["comment_color"][0];
                node->CommentColor[1] = jn["comment_color"][1];
                node->CommentColor[2] = jn["comment_color"][2];
            }

            // Aceita os dois formatos pro tipo do pin, mesma razão do
            // "category" lá em cima: string (novo, robusto contra reordenar
            // o enum) ou int cru (formato antigo — best-effort).
            auto parsePinType = [](const nlohmann::json& jp) -> ScriptPinType
                {
                    if (jp["type"].is_string()) return ScriptPinTypeFromString(jp["type"].get<std::string>());
                    return (ScriptPinType)jp["type"].get<int>();
                };

            for (const auto& jp : jn.value("inputs", nlohmann::json::array()))
            {
                ScriptPin pin(jp["id"].get<int>(), jp["name"].get<std::string>().c_str(),
                    parsePinType(jp), (ed::PinKind)jp["kind"].get<int>());
                // SC17 — formato novo se existir, antigo caso contrario. Todo
                // .axescript gravado antes deste patch cai no segundo ramo.
                if (jp.contains("default")) pin.Default.Deserialize(jp["default"]);
                else                        pin.Default.DeserializeLegacyPin(jp);
                node->Inputs.push_back(std::move(pin));
            }

            for (const auto& jp : jn.value("outputs", nlohmann::json::array()))
            {
                ScriptPin pin(jp["id"].get<int>(), jp["name"].get<std::string>().c_str(),
                    parsePinType(jp), (ed::PinKind)jp["kind"].get<int>());
                // SC17 — formato novo se existir, antigo caso contrario. Todo
                // .axescript gravado antes deste patch cai no segundo ramo.
                if (jp.contains("default")) pin.Default.Deserialize(jp["default"]);
                else                        pin.Default.DeserializeLegacyPin(jp);
                node->Outputs.push_back(std::move(pin));
            }

            m_Nodes.push_back(std::move(node));
        }

        for (const auto& jl : j.value("links", nlohmann::json::array()))
            m_Links.emplace_back(jl["id"].get<int>(), ed::PinId(jl["start"].get<int>()), ed::PinId(jl["end"].get<int>()));

        // ── SC18: migracao de valores que moravam no node ────────────────────
        //
        // Print String guardava a mensagem em node->StringValue, um campo de
        // uso geral que o mesmo struct reaproveita para nome de variavel,
        // Action selecionada e titulo de Comment. Agora ela mora no default do
        // pin Message, onde o resto dos valores de entrada ja mora.
        //
        // Migrar no load, e nao pedir para o autor redigitar: e o mesmo padrao
        // de alias usado para node retirado (ControlFollowBone, RerouteExec).
        // O campo antigo e LIMPO depois de copiado — deixar os dois
        // preenchidos e como o bug comecou.
        for (auto& n : m_Nodes)
        {
            if (n->Name != "Print String" || n->StringValue.empty()) continue;
            for (auto& in : n->Inputs)
            {
                if (in.Name != "Message") continue;
                if (in.Default.Str.empty())
                {
                    in.Default.Str = n->StringValue;
                    AXE_CORE_INFO("ScriptGraph: mensagem do Print String migrada "
                        "para o pin ('{}').", n->StringValue);
                }
                n->StringValue.clear();
                break;
            }
        }

        // S1 — .axescript salvo antes deste patch pode carregar fios que a
        // regra de cardinalidade nunca deveria ter deixado existir (dois
        // valores no mesmo pin de entrada, dois destinos na mesma saída de
        // Flow). Não são erro do arquivo: eram permitidos. Descartar aqui,
        // uma vez, é preferível a compilar um grafo cujo resultado depende da
        // ordem do vector — o mesmo raciocínio de "nunca deletar em silêncio"
        // vale ao contrário aqui, porque o fio extra JÁ era ignorado pelo
        // compilador; só não aparecia na tela.
        SanitizeLinks();
    }

} // namespace axe