#include "material_compiler.hpp"
#include "axe/log/log.hpp"
#include <sstream>
#include <iomanip>

namespace axe
{
    MaterialCompiler::MaterialCompiler(MaterialGraph* graph)
        : m_Graph(graph)
    {}


    CompiledMaterial MaterialCompiler::Compile(MaterialGraph* graph)
    {
        MaterialCompiler compiler(graph);
        CompiledMaterial result;

        // 1 Localiza o Material Output node - ponto de partida do percurso
        Node* outputNode = nullptr;
        for (auto& node : graph->GetNodes())
        {
            if (node->Name == "Material Output")
            {
                outputNode = node.get();
                break;
            }
        }
             
        if (!outputNode)
        {
            result.ErrorMessage = "Material Output node not found";
            AXE_CORE_ERROR("MaterialCompiler: {}", result.ErrorMessage);
            return result;
        }

         // ---------------------------------------------------------------------
         // 2. Percorre o grafo em ordem topológica (DFS reverso)
         //    Começa pelo output e visita cada input recursivamente.
         //    Isso garante que o código de cada node é gerado ANTES de ser
         //    referenciado por outro node.
         // ---------------------------------------------------------------------        
        compiler.VisitNode(outputNode);


        // ---------------------------------------------------------------------
        // 3. Monta o Fragment Shader
        // ---------------------------------------------------------------------

        std::stringstream fs;
        // -- Cabeçalho --
        fs << "#version 460 core\n";
        fs << "layout(location = 0) out vec4 FragColor;\n\n";

        // -- Varyings do vertex shader --
        fs << "in vec3 v_Normal;\n";
        fs << "in vec3 v_FragPos;\n";
        fs << "in vec2 v_TexCoord;\n\n";

        // -- Uniforms de iluminação (setados pelo MeshRenderer) --
        fs << "uniform vec3  u_LightDirection;\n";
        fs << "uniform vec3  u_LightColor;\n";
        fs << "uniform float u_LightIntensity;\n";
        fs << "uniform vec3  u_CameraPosition;\n\n";

        // -- Samplers: um por node Texture Sample no grafo --
        //    Slot 0 → u_AlbedoMap  (compatível com Material::Apply())
        //    Slot 1+ → u_Texture_1, u_Texture_2, ...
        {
            int slot = 0;
            for (auto& node : graph->GetNodes())
            {
                if (node->Name != "Texture Sample") continue;
                std::string samplerName = (slot == 0)
                    ? "u_AlbedoMap"
                    : "u_Texture_" + std::to_string(slot);
                compiler.m_NodeSamplers[node->ID.Get()] = samplerName;
                fs << "uniform sampler2D " << samplerName << ";\n";
                ++slot;
            }
        }
        fs << "\n";

        // -- Função main --
        fs << "void main()\n{\n";
        fs << "    // Normal da superfície\n";
        fs << "    vec3 N = normalize(v_Normal);\n\n";

        // -- Código gerado pelo percurso do grafo --
        fs << compiler.m_FragmentCode;

        // ---------------------------------------------------------------------
        // 4. Resolve os inputs do Material Output
        //    Se não conectado, usa valor padrão.
        //
        //    Inputs (ordem definida pelo MaterialGraph):
        //      0 = Base Color (vec3)
        //      1 = Metallic   (float)
        //      2 = Roughness  (float)
        //      3 = Normal     (vec3)
        //      4 = Emissive   (vec3)
        //      5 = Opacity    (float)
        // ---------------------------------------------------------------------
        auto resolveInput = [&](int index, const std::string& fallback) -> std::string
            {
                if (index >= (int)outputNode->Inputs.size()) return fallback;
                Pin* src = compiler.GetSourcePin(&outputNode->Inputs[index]);
                if (!src) return fallback;
                return compiler.GetPinVariable(src->ID);
            };

        std::string baseColor = resolveInput(0, "vec3(0.7, 0.7, 0.7)");
        std::string metallic = resolveInput(1, "0.0");
        std::string roughness = resolveInput(2, "0.5");
        std::string emissive = resolveInput(4, "vec3(0.0)");
        std::string opacity = resolveInput(5, "1.0");
        // Conversão automática de tipo para Base Color (deve ser vec3)
        Pin* baseColorSrc = compiler.GetSourcePin(&outputNode->Inputs[0]);
        if (baseColorSrc)
        {
            PinType t = compiler.GetPinType(baseColorSrc->ID);
            if (t == PinType::Float)
                baseColor = "vec3(" + baseColor + ")";
            else if (t == PinType::Vec4)
                baseColor = "(" + baseColor + ").rgb";
        }
        // Conversão automática para Emissive (deve ser vec3)
        Pin* emissiveSrc = (outputNode->Inputs.size() > 4)
            ? compiler.GetSourcePin(&outputNode->Inputs[4]) : nullptr;
        if (emissiveSrc)
        {
            PinType t = compiler.GetPinType(emissiveSrc->ID);
            if (t == PinType::Float)
                emissive = "vec3(" + emissive + ")";
            else if (t == PinType::Vec4)
                emissive = "(" + emissive + ").rgb";
        }

        // Normal Map conectado substitui a normal da superfície
        Pin* normalSrc = (outputNode->Inputs.size() > 3)
            ? compiler.GetSourcePin(&outputNode->Inputs[3]) : nullptr;
        if (normalSrc)
        {
            fs << "    // Normal Map conectado\n";
            fs << "    N = normalize(" << compiler.GetPinVariable(normalSrc->ID) << ");\n\n";
        }
        // -- Variáveis finais do material --
        fs << "\n    // --- Propriedades do material ---\n";
        fs << "    vec3  matBaseColor = " << baseColor << ";\n";
        fs << "    float matMetallic  = " << metallic << ";\n";
        fs << "    float matRoughness = clamp(" << roughness << ", 0.05, 1.0);\n";
        fs << "    vec3  matEmissive  = " << emissive << ";\n";
        fs << "    float matOpacity   = " << opacity << ";\n\n";

        // ---------------------------------------------------------------------
        // 5. Iluminação Blinn-Phong
        //    Compatível com os uniforms do MeshRenderer.
        //    No futuro pode ser substituída por PBR Cook-Torrance sem mudar
        //    o compilador — basta trocar este bloco.
        // ---------------------------------------------------------------------
        fs << "    // --- Iluminação Blinn-Phong ---\n";
        fs << "    vec3  L       = normalize(-u_LightDirection);\n";
        fs << "    vec3  V       = normalize(u_CameraPosition - v_FragPos);\n";
        fs << "    vec3  H       = normalize(L + V);\n";
        fs << "    float NdotL   = max(dot(N, L), 0.0);\n";
        fs << "    float NdotH   = max(dot(N, H), 0.0);\n";
        fs << "    float specPow = mix(8.0, 128.0, 1.0 - matRoughness);\n\n";

        fs << "    vec3 ambient  = matBaseColor * 0.15 * u_LightColor;\n";
        fs << "    vec3 diffuse  = matBaseColor * NdotL * u_LightColor * u_LightIntensity;\n";
        fs << "    vec3 specular = vec3(pow(NdotH, specPow))\n";
        fs << "                   * u_LightColor * u_LightIntensity\n";
        fs << "                   * (1.0 - matRoughness) * (1.0 - matMetallic) * 0.5;\n\n";

        fs << "    vec3 finalColor = ambient + diffuse + specular + matEmissive;\n";
        fs << "    FragColor = vec4(finalColor, matOpacity);\n";
        fs << "}\n";

        result.FragmentShader = fs.str();

        // ---------------------------------------------------------------------
        // 6. Vertex Shader — padrão, independente do grafo
        // ---------------------------------------------------------------------
        result.VertexShader = R"(
        #version 460 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;
        layout(location = 2) in vec2 a_TexCoord;
 
        uniform mat4 u_Model;
        uniform mat4 u_ViewProjection;
        uniform mat3 u_NormalMatrix;
 
        out vec3 v_Normal;
        out vec3 v_FragPos;
        out vec2 v_TexCoord;
 
        void main()
        {
            vec4 worldPos  = u_Model * vec4(a_Position, 1.0);
            v_FragPos      = worldPos.xyz;
            v_Normal       = normalize(u_NormalMatrix * a_Normal);
            v_TexCoord     = a_TexCoord;
            gl_Position    = u_ViewProjection * worldPos;
        }
        )";

        result.Success = true;
        AXE_CORE_INFO("MaterialCompiler: Compilation successful.");
        return result;
    }

   // =========================================================================
   // Percurso do grafo
   // =========================================================================


    void MaterialCompiler::VisitNode(Node* node)
    {
        if (!node) return;

        int id = node->ID.Get();
        if (m_VisitedNodes.count(id)) return;
        m_VisitedNodes.insert(id);

        //Visita inputs primeiro (garante ordem topológica)        
        for (auto& input : node->Inputs)        
            VisitPin(&input);        

        // Depois gera código para este node
        std::string code = GenerateNodeCode(node);
        if (!code.empty())
            m_FragmentCode += code + "\n";        
    }

    void MaterialCompiler::VisitPin(Pin* pin)
    {
        if (!pin || pin->Kind != ed::PinKind::Input) return;

        // Encontra o node fonte conectado a este input
        Node* src = GetSourceNode(pin);
        if (src)VisitNode(src);
    }

       // =========================================================================
       // Geração de código GLSL por tipo de node
       // =========================================================================


    std::string MaterialCompiler::GenerateNodeCode(Node* node)
    {
        if (node->Name == "Material Output") return "";

        std::stringstream code;

        // -----------------------------------------------------------------
        // Float — constante escalar
        // -----------------------------------------------------------------
        if (node->Name == "Float")
        {
            std::string var = MakeVar("float");
            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = " << node->Value.FloatVal << ";";
        }

        // -----------------------------------------------------------------
        // Color — constante vec4 com outputs RGBA e RGB
        // -----------------------------------------------------------------
        else if (node->Name == "Color")
        {
            std::string var = MakeVar("color");
            auto& c = node->Value.Vec4Val;

            auto f = [](float v) {
                std::ostringstream s;
                s << std::fixed << std::setprecision(4) << v;
                return s.str();
                };

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec4); // RGBA
            RegisterPin(node->Outputs[1].ID, var + ".rgb", PinType::Vec3); // RGB
            code << "vec4 " << var << " = vec4("
                << f(c.x) << ", " << f(c.y) << ", "
                << f(c.z) << ", " << f(c.w) << ");";
        }

        // -----------------------------------------------------------------
        // UV Coordinate
        // -----------------------------------------------------------------
        else if (node->Name == "UV Coordinate")
        {
            std::string var = MakeVar("uv");
            RegisterPin(node->Outputs[0].ID, var, PinType::Vec2);
            code << "vec2 " << var << " = v_TexCoord;";
        }

        // -----------------------------------------------------------------
        // Texture Sample
        // outputs: RGBA (vec4), RGB (vec3), R (float)
        // -----------------------------------------------------------------
        else if (node->Name == "Texture Sample")
        {
            std::string var = MakeVar("tex");
            std::string sampler = "u_AlbedoMap";

            auto it = m_NodeSamplers.find(node->ID.Get());
            if (it != m_NodeSamplers.end())
                sampler = it->second;

            std::string uv = "v_TexCoord";
            if (!node->Inputs.empty())
            {
                Pin* uvSrc = GetSourcePin(&node->Inputs[0]);
                if (uvSrc) uv = GetPinVariable(uvSrc->ID);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec4);
            RegisterPin(node->Outputs[1].ID, var + ".rgb", PinType::Vec3);
            RegisterPin(node->Outputs[2].ID, var + ".r", PinType::Float);
            code << "vec4 " << var << " = texture(" << sampler << ", " << uv << ");";
        }

        // -----------------------------------------------------------------
        // Multiply — A * B
        // Tipo resultado: o "maior" tipo entre A e B
        // (float * vec3 → vec3, GLSL aceita nativamente)
        // -----------------------------------------------------------------
        else if (node->Name == "Multiply")
        {
            std::string var = MakeVar("mul");
            std::string valA = "1.0", valB = "1.0";
            PinType typeA = PinType::Float, typeB = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }

            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) { valB = GetPinVariable(srcB->ID); typeB = GetPinType(srcB->ID); }

            PinType resultType = (typeA >= typeB) ? typeA : typeB;
            RegisterPin(node->Outputs[0].ID, var, resultType);
            code << GetGLSLType(resultType) << " " << var
                << " = " << valA << " * " << valB << ";";
        }

        // -----------------------------------------------------------------
        // Add — A + B
        // -----------------------------------------------------------------
        else if (node->Name == "Add")
        {
            std::string var = MakeVar("add");
            std::string valA = "0.0", valB = "0.0";
            PinType typeA = PinType::Float, typeB = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }

            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) { valB = GetPinVariable(srcB->ID); typeB = GetPinType(srcB->ID); }

            PinType resultType = (typeA >= typeB) ? typeA : typeB;
            RegisterPin(node->Outputs[0].ID, var, resultType);
            code << GetGLSLType(resultType) << " " << var
                << " = " << valA << " + " << valB << ";";
        }

        // -----------------------------------------------------------------
        // Subtract — A - B
        // -----------------------------------------------------------------
        else if (node->Name == "Subtract")
        {
            std::string var = MakeVar("sub");
            std::string valA = "0.0", valB = "0.0";
            PinType typeA = PinType::Float, typeB = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }

            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) { valB = GetPinVariable(srcB->ID); typeB = GetPinType(srcB->ID); }

            PinType resultType = (typeA >= typeB) ? typeA : typeB;
            RegisterPin(node->Outputs[0].ID, var, resultType);
            code << GetGLSLType(resultType) << " " << var
                << " = " << valA << " - " << valB << ";";
        }

        // -----------------------------------------------------------------
        // Divide — A / B  (protegido contra divisão por zero)
        // -----------------------------------------------------------------
        else if (node->Name == "Divide")
        {
            std::string var = MakeVar("div");
            std::string valA = "1.0", valB = "1.0";
            PinType typeA = PinType::Float, typeB = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }

            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) { valB = GetPinVariable(srcB->ID); typeB = GetPinType(srcB->ID); }

            PinType resultType = (typeA >= typeB) ? typeA : typeB;
            RegisterPin(node->Outputs[0].ID, var, resultType);
            code << GetGLSLType(resultType) << " " << var
                << " = " << valA << " / max(" << valB << ", 0.0001);";
        }

        // -----------------------------------------------------------------
        // Power — pow(A, B)
        // Útil para controle de contraste, curvas de roughness, etc.
        // -----------------------------------------------------------------
        else if (node->Name == "Power")
        {
            std::string var = MakeVar("pw");
            std::string valA = "1.0", valB = "2.0";
            PinType typeA = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }

            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) valB = GetPinVariable(srcB->ID);

            // pow() em GLSL requer base >= 0
            RegisterPin(node->Outputs[0].ID, var, typeA);
            code << GetGLSLType(typeA) << " " << var
                << " = pow(max(" << valA << ", 0.0), " << valB << ");";
        }

        // -----------------------------------------------------------------
        // Lerp — mix(A, B, Alpha)
        // Alpha=0 → A, Alpha=1 → B
        // -----------------------------------------------------------------
        else if (node->Name == "Lerp")
        {
            std::string var = MakeVar("lerp");
            std::string valA = "0.0", valB = "1.0", alpha = "0.5";
            PinType typeA = PinType::Float, typeB = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }

            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) { valB = GetPinVariable(srcB->ID); typeB = GetPinType(srcB->ID); }

            Pin* srcAlpha = GetSourcePin(&node->Inputs[2]);
            if (srcAlpha) alpha = GetPinVariable(srcAlpha->ID);

            PinType resultType = (typeA >= typeB) ? typeA : typeB;
            RegisterPin(node->Outputs[0].ID, var, resultType);
            code << GetGLSLType(resultType) << " " << var
                << " = mix(" << valA << ", " << valB
                << ", clamp(" << alpha << ", 0.0, 1.0));";
        }

        // Clamp
        else if (node->Name == "Clamp")
        {
            std::string var = MakeVar("clamp");
            std::string val = "0.0", minVal = "0.0", maxVal = "1.0";
            PinType     typeV = PinType::Float;

            Pin* srcVal = GetSourcePin(&node->Inputs[0]);
            if (srcVal) { val = GetPinVariable(srcVal->ID); typeV = GetPinType(srcVal->ID); }

            Pin* srcMin = GetSourcePin(&node->Inputs[1]);
            if (srcMin) minVal = GetPinVariable(srcMin->ID);

            Pin* srcMax = GetSourcePin(&node->Inputs[2]);
            if (srcMax) maxVal = GetPinVariable(srcMax->ID);

            RegisterPin(node->Outputs[0].ID, var, typeV);
            code << GetGLSLType(typeV) << " " << var
                << " = clamp(" << val << ", " << minVal << ", " << maxVal << ");";
                }

                // Abs
        else if (node->Name == "Abs")
        {
            std::string var = MakeVar("abs");
            std::string val = "0.0";
            PinType     type = PinType::Float;

            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) { val = GetPinVariable(src->ID); type = GetPinType(src->ID); }

            RegisterPin(node->Outputs[0].ID, var, type);
            code << GetGLSLType(type) << " " << var << " = abs(" << val << ");";
            }

            // OneMinus
        else if (node->Name == "OneMinus")
        {
            std::string var = MakeVar("oneminus");
            std::string val = "0.0";
            PinType     type = PinType::Float;

            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) { val = GetPinVariable(src->ID); type = GetPinType(src->ID); }

            RegisterPin(node->Outputs[0].ID, var, type);
            code << GetGLSLType(type) << " " << var << " = 1.0 - " << val << ";";
            }

            // World Position
        else if (node->Name == "World Position")
        {
            std::string var = MakeVar("worldpos");
            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);  // XYZ
            RegisterPin(node->Outputs[1].ID, var + ".x", PinType::Float); // X
            RegisterPin(node->Outputs[2].ID, var + ".y", PinType::Float); // Y
            RegisterPin(node->Outputs[3].ID, var + ".z", PinType::Float); // Z
            code << "vec3 " << var << " = v_FragPos;";
            }

            // Fresnel
        else if (node->Name == "Fresnel")
        {
            std::string var = MakeVar("fresnel");
            std::string exponent = "5.0";
            std::string normal = "N";

            Pin* srcExp = GetSourcePin(&node->Inputs[0]);
            if (srcExp) exponent = GetPinVariable(srcExp->ID);

            Pin* srcNorm = GetSourcePin(&node->Inputs[1]);
            if (srcNorm) normal = GetPinVariable(srcNorm->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = pow(1.0 - max(dot(normalize("
                << normal << "), normalize(v_FragPos - u_CameraPosition)), 0.0), "
                << exponent << ");";
                }

        return code.str();
    }

    // =========================================================================
     // Helpers
     // =========================================================================

    void MaterialCompiler::RegisterPin(ed::PinId pinId, const std::string& variable, PinType type)
    {
        m_PinVariables[pinId.Get()] = { variable, type };
    }

    std::string MaterialCompiler::GetPinVariable(ed::PinId pinId)
    {
        auto it = m_PinVariables.find(pinId.Get());
        if (it != m_PinVariables.end())
            return it->second.variable;
        AXE_CORE_WARN("MaterialCompiler: pin {} not registered, using 0.0", pinId.Get());
        return "0.0";
    }

    PinType MaterialCompiler::GetPinType(ed::PinId pinId)
    {
        auto it = m_PinVariables.find(pinId.Get());
        if (it != m_PinVariables.end())
            return it->second.type;
        return PinType::Float;
    }

    std::string MaterialCompiler::MakeVar(const std::string& prefix)
    {
        return prefix + "_" + std::to_string(m_VariableCounter++);
    }

    Node* MaterialCompiler::GetSourceNode(Pin* inputPin)
    {
        for (auto& link : m_Graph->GetLinks())
        {
            if (link.EndPin != inputPin->ID) continue;
            for (auto& node : m_Graph->GetNodes())
                for (auto& output : node->Outputs)
                    if (output.ID == link.StartPin)
                        return node.get();
        }
        return nullptr;
    }

    Pin* MaterialCompiler::GetSourcePin(Pin* inputPin)
    {
        for (auto& link : m_Graph->GetLinks())
        {
            if (link.EndPin != inputPin->ID) continue;
            for (auto& node : m_Graph->GetNodes())
                for (auto& output : node->Outputs)
                    if (output.ID == link.StartPin)
                        return &output;
        }
        return nullptr;
    }

    std::string MaterialCompiler::GetGLSLType(PinType type)
    {
        switch (type)
        {
        case PinType::Float:     return "float";
        case PinType::Vec2:      return "vec2";
        case PinType::Vec3:      return "vec3";
        case PinType::Vec4:      return "vec4";
        case PinType::Texture2D: return "sampler2D";
        default:                 return "float";
        }
    }
}