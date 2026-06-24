#include "material_compiler.hpp"
#include "axe/log/log.hpp"
#include <sstream>
#include <iomanip>
#include <unordered_set>

#include "axe/graphics/texture.hpp"

namespace axe
{
    MaterialCompiler::MaterialCompiler(MaterialGraph* graph)
        : m_Graph(graph)
    {}


    CompiledMaterial MaterialCompiler::Compile(MaterialGraph* graph)
    {
        MaterialCompiler compiler(graph);
        CompiledMaterial result;

        // 1. Localiza o Material Output node
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
            //AXE_CORE_ERROR("MaterialCompiler: {}", result.ErrorMessage);
            return result;
        }

        // 2. Atribui samplers — percorre todos os Texture Sample
        {
            int slot = 0;
            std::unordered_set<int> processed;

            // Primeiro — conectados ao Base Color (pin 0) via qualquer caminho
            std::function<Node* (ed::PinId)> findTextureSample =
                [&](ed::PinId startPin) -> Node*
                {
                    for (auto& node : graph->GetNodes())
                        for (auto& outPin : node->Outputs)
                        {
                            if (outPin.ID != startPin) continue;
                            if (node->Name == "Texture Sample") return node.get();
                            for (auto& inPin : node->Inputs)
                                for (auto& l2 : graph->GetLinks())
                                {
                                    if (l2.EndPin != inPin.ID) continue;
                                    Node* found = findTextureSample(l2.StartPin);
                                    if (found) return found;
                                }
                        }
                    return nullptr;
                };

            // Percorre pins do output em ordem
            for (auto& inputPin : outputNode->Inputs)
            {
                for (auto& link : graph->GetLinks())
                {
                    if (link.EndPin != inputPin.ID) continue;
                    Node* texNode = findTextureSample(link.StartPin);
                    if (texNode && !processed.count(texNode->ID.Get()))
                    {
                        std::string name = (slot == 0) ? "u_AlbedoMap"
                            : "u_Texture_" + std::to_string(slot);
                        compiler.m_NodeSamplers[texNode->ID.Get()] = name;
                        processed.insert(texNode->ID.Get());
                        ++slot;
                    }
                }
            }

            // Depois — todos os Texture Sample não processados ainda
            //for (auto& node : graph->GetNodes())
            //{
            //    if (node->Name != "Texture Sample") continue;
            //    if (processed.count(node->ID.Get())) continue;
            //    std::string name = (slot == 0) ? "u_AlbedoMap"
            //        : "u_Texture_" + std::to_string(slot);
            //    compiler.m_NodeSamplers[node->ID.Get()] = name;
            //    ++slot;
            //}

            for (auto& node : graph->GetNodes())
            {
                if (node->Name != "Texture Sample") continue;
                if (!node->Value.TextureVal) continue;

                auto it = compiler.m_NodeSamplers.find(node->ID.Get());
                if (it == compiler.m_NodeSamplers.end()) continue;

                if (it != compiler.m_NodeSamplers.end())
                    result.SamplerTextures[it->second] = node->Value.TextureVal;

                if (it->second == "u_AlbedoMap")
                    result.AlbedoTexture = node->Value.TextureVal;
                else if (it->second == "u_Texture_1")
                    result.NormalTexture = node->Value.TextureVal;
                else if (it->second == "u_Texture_2")
                    result.RoughnessTexture = node->Value.TextureVal;
                else if (it->second == "u_Texture_3")
                    result.MetallicTexture = node->Value.TextureVal;
            }
        }

        // 3. Percorre o grafo — agora m_NodeSamplers já está preenchido
        compiler.VisitNode(outputNode);

        // 4. Monta o Fragment Shader
        std::stringstream fs;

        fs << "#version 460 core\n";
        fs << "layout(location = 0) out vec4 FragColor;\n\n";
        fs << "in vec3 v_Normal;\n";
        fs << "in vec3 v_FragPos;\n";
        fs << "in vec2 v_TexCoord;\n\n";
        fs << "in vec3 v_Tangent;\n";
        fs << "in vec3 v_Bitangent;\n";
        fs << "uniform vec3  u_LightDirection;\n";
        fs << "uniform vec3  u_LightColor;\n";
        fs << "uniform float u_LightIntensity;\n";
        fs << "uniform vec3  u_CameraPosition;\n";
        fs << "uniform float u_Time;\n\n";
        fs << "uniform samplerCube u_IrradianceMap;\n";
        fs << "uniform samplerCube u_PrefilteredMap;\n";
        fs << "uniform sampler2D   u_BRDFLut;\n";
        fs << "uniform int         u_HasIBL;\n\n";
        fs << "uniform float u_IBLIntensity;\n";
        fs << "uniform float u_AmbientStrength;\n";

        // Declara samplers usando m_NodeSamplers já preenchido
        std::map<int, std::string> slotToSampler;
        for (auto& node : graph->GetNodes())
        {
            if (node->Name != "Texture Sample") continue;
            auto it = compiler.m_NodeSamplers.find(node->ID.Get());
            if (it != compiler.m_NodeSamplers.end())
                fs << "uniform sampler2D " << it->second << ";\n";
        }
        //std::map<int, std::string> slotToSampler;
        //for (auto& node : graph->GetNodes())
        //{
        //    if (node->Name != "Texture Sample") continue;
        //    auto it = compiler.m_NodeSamplers.find(node->ID.Get());
        //    if (it != compiler.m_NodeSamplers.end())
        //    {
        //        // Extrai o número do slot do nome
        //        int slot = 0;
        //        if (it->second != "u_AlbedoMap")
        //            slot = std::stoi(it->second.substr(std::string("u_Texture_").size()));
        //        slotToSampler[slot] = it->second;
        //    }
        //}
        //for (auto& [slot, name] : slotToSampler)
        //    fs << "uniform sampler2D " << name << ";\n";
        fs << "\n";

        // 5. Função main
        fs << "void main()\n{\n";
        fs << "    // Normal da superfície\n";
        fs << "    vec3 N = normalize(v_Normal);\n\n";

        // Código gerado pelo percurso do grafo
        fs << compiler.m_FragmentCode;

        // 6. Resolve os inputs do Material Output
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
        std::string ao = resolveInput(6, "1.0");
        std::string specular = resolveInput(7, "0.5");

        // Converte tipos
        Pin* metallicSrc = (outputNode->Inputs.size() > 1)
            ? compiler.GetSourcePin(&outputNode->Inputs[1]) : nullptr;
        if (metallicSrc)
        {
            PinType t = compiler.GetPinType(metallicSrc->ID);
            if (t == PinType::Vec3 || t == PinType::Vec4)
                metallic = "dot(" + metallic + ", vec3(0.299, 0.587, 0.114))";
        }

        Pin* roughnessSrc = (outputNode->Inputs.size() > 2)
            ? compiler.GetSourcePin(&outputNode->Inputs[2]) : nullptr;
        if (roughnessSrc)
        {
            PinType t = compiler.GetPinType(roughnessSrc->ID);
            if (t == PinType::Vec3 || t == PinType::Vec4)
                roughness = "dot(" + roughness + ", vec3(0.299, 0.587, 0.114))";
        }

        Pin* opacitySrc = (outputNode->Inputs.size() > 5)
            ? compiler.GetSourcePin(&outputNode->Inputs[5]) : nullptr;
        if (opacitySrc)
        {
            PinType t = compiler.GetPinType(opacitySrc->ID);
            if (t == PinType::Vec3 || t == PinType::Vec4)
                opacity = "dot(" + opacity + ", vec3(0.299, 0.587, 0.114))";
        }

        // Opacity conectado a algo => este material precisa do forward
        // pass de transparência (vidro, etc.) — ver SceneRenderer.
        result.IsTransparent = (opacitySrc != nullptr);

        Pin* aoSrc = (outputNode->Inputs.size() > 6)
            ? compiler.GetSourcePin(&outputNode->Inputs[6]) : nullptr;
        if (aoSrc)
        {
            PinType t = compiler.GetPinType(aoSrc->ID);
            if (t == PinType::Vec3 || t == PinType::Vec4)
                ao = "(" + ao + ").r"; // mapas de AO são monocromáticos; .r basta
        }

        Pin* specularSrc = (outputNode->Inputs.size() > 7)
            ? compiler.GetSourcePin(&outputNode->Inputs[7]) : nullptr;
        if (specularSrc)
        {
            PinType t = compiler.GetPinType(specularSrc->ID);
            if (t == PinType::Vec3 || t == PinType::Vec4)
                specular = "dot(" + specular + ", vec3(0.299, 0.587, 0.114))";
        }

        Pin* baseColorSrc = compiler.GetSourcePin(&outputNode->Inputs[0]);
        if (baseColorSrc)
        {
            PinType t = compiler.GetPinType(baseColorSrc->ID);
            if (t == PinType::Float)
                baseColor = "vec3(" + baseColor + ")";
            else if (t == PinType::Vec4)
                baseColor = "(" + baseColor + ").rgb";
        }

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

        // Normal Map
        Pin* normalSrc = (outputNode->Inputs.size() > 3)
            ? compiler.GetSourcePin(&outputNode->Inputs[3]) : nullptr;
        if (normalSrc)
        {
            fs << "    // Normal Map conectado\n";
            fs << "    N = normalize(" << compiler.GetPinVariable(normalSrc->ID) << ");\n\n";
        }

        // Propriedades do material
        fs << "\n    // --- Propriedades do material ---\n";
        fs << "    vec3  matBaseColor = " << baseColor << ";\n";
        fs << "    float matMetallic  = " << metallic << ";\n";
        fs << "    float matRoughness = clamp(" << roughness << ", 0.05, 1.0);\n";
        fs << "    vec3  matEmissive  = " << emissive << ";\n";
        fs << "    float matOpacity   = " << opacity << ";\n\n";
        fs << "    float matAO = clamp(" << ao << ", 0.0, 1.0);\n";
        fs << "    float matSpecular = clamp(" << specular << ", 0.0, 1.0);\n";

        // 7. PBR Cook-Torrance
        fs << "    // --- PBR Cook-Torrance ---\n";
        fs << "    vec3  L     = normalize(-u_LightDirection);\n";
        fs << "    vec3  V     = normalize(u_CameraPosition - v_FragPos);\n";
        fs << "    vec3  H     = normalize(L + V);\n";
        fs << "    float NdotL = max(dot(N, L), 0.0);\n";
        fs << "    float NdotV = max(dot(N, V), 0.0);\n";
        fs << "    float NdotH = max(dot(N, H), 0.0);\n\n";
        fs << "    vec3 F0 = mix(vec3(0.04), matBaseColor, matMetallic);\n\n";
        fs << "    float alpha  = matRoughness * matRoughness;\n";
        fs << "    float alpha2 = alpha * alpha;\n";
        fs << "    float denom  = (NdotH * NdotH) * (alpha2 - 1.0) + 1.0;\n";
        fs << "    float D      = alpha2 / (3.14159265 * denom * denom);\n\n";
        fs << "    float k   = (matRoughness + 1.0) * (matRoughness + 1.0) / 8.0;\n";
        fs << "    float Gv  = NdotV / (NdotV * (1.0 - k) + k);\n";
        fs << "    float Gl  = NdotL / (NdotL * (1.0 - k) + k);\n";
        fs << "    float G   = Gv * Gl;\n\n";
        fs << "    float cosTheta = max(dot(H, V), 0.0);\n";
        fs << "    vec3  F        = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);\n\n";
        fs << "    vec3 numerator   = D * G * F;\n";
        fs << "    float denomSpec  = 4.0 * NdotV * NdotL + 0.0001;\n";
        fs << "    vec3  specular   = numerator / denomSpec;\n\n";
        fs << "    vec3 kD = (vec3(1.0) - F) * (1.0 - matMetallic);\n";
        fs << "    vec3 diffuse = kD * matBaseColor / 3.14159265;\n\n";
        fs << "    vec3 radiance = u_LightColor * u_LightIntensity;\n";
        fs << "    vec3 Lo       = (diffuse + specular) * radiance * NdotL;\n\n";

        // 8. Ambient IBL ou fallback
        fs << "    vec3 ambient;\n";
        fs << "    if (u_HasIBL == 1)\n";
        fs << "    {\n";
        fs << "        vec3 F_amb  = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);\n";
        fs << "        vec3 kD_amb = (vec3(1.0) - F_amb) * (1.0 - matMetallic);\n";
        fs << "        vec3 irradiance  = texture(u_IrradianceMap, N).rgb;\n";
        fs << "        vec3 diffuse_ibl = irradiance * matBaseColor;\n";
        fs << "        const float MAX_REFLECTION_LOD = 4.0;\n";
        fs << "        vec3 R = reflect(-V, N);\n";
        fs << "        vec3 prefilteredColor = textureLod(u_PrefilteredMap, R,\n";
        fs << "                                matRoughness * MAX_REFLECTION_LOD).rgb;\n";
        fs << "        vec2 brdf = texture(u_BRDFLut, vec2(NdotV, matRoughness)).rg;\n";
        fs << "        vec3 specular_ibl = prefilteredColor * (F_amb * brdf.x + brdf.y);\n";
        fs << "        ambient = (kD_amb * diffuse_ibl + specular_ibl) * matAO * u_IBLIntensity;\n";
        fs << "    }\n";
        fs << "    else\n";
        fs << "    {\n";
        fs << "        ambient = matBaseColor * u_AmbientStrength * u_LightColor;\n";
        fs << "    }\n\n";

        fs << "    vec3 finalColor = ambient + Lo + matEmissive;\n";
        fs << "    finalColor = max(finalColor, matBaseColor * 0.02);\n\n";
        //fs << "    finalColor = finalColor / (finalColor + vec3(1.0));\n";
        //fs << "    finalColor = pow(finalColor, vec3(1.0 / 2.2));\n\n";
        fs << "    FragColor = vec4(finalColor, matOpacity);\n";
        fs << "}\n";

        std::stringstream gs;
        gs << "#version 460 core\n";
        gs << "layout(location = 0) out vec3 g_Position;\n";
        gs << "layout(location = 1) out vec3 g_Normal;\n";
        gs << "layout(location = 2) out vec4 g_Albedo;\n";
        gs << "layout(location = 3) out vec2 g_PBR;\n";
        gs << "layout(location = 4) out vec3 g_Emissive;\n\n";
        gs << "in vec3 v_Normal;\n";
        gs << "in vec3 v_FragPos;\n";
        gs << "in vec2 v_TexCoord;\n";
        gs << "in vec3 v_Tangent;\n";
        gs << "in vec3 v_Bitangent;\n\n";

        // u_CameraPosition e u_Time faltavam aqui — sem eles, nodes como
        // Fresnel/Camera Vector/Reflection Vector/Time/Panner compilavam
        // certinho no preview (caminho forward) mas falhavam ao compilar
        // o Geometry Shader do caminho deferred (usado na cena principal),
        // pois referenciavam um uniform nunca declarado.
        gs << "uniform vec3  u_CameraPosition;\n";
        gs << "uniform float u_Time;\n\n";

        // Declara os mesmos samplers do forward
        for (auto& node : graph->GetNodes())
        {
            if (node->Name != "Texture Sample") continue;
            auto it = compiler.m_NodeSamplers.find(node->ID.Get());
            if (it != compiler.m_NodeSamplers.end())
                gs << "uniform sampler2D " << it->second << ";\n";
        }
        gs << "\n";

        gs << "void main()\n{\n";
        gs << "    vec3 N = normalize(v_Normal);\n\n";

        // Reutiliza o código dos nodes gerado pelo grafo
        gs << compiler.m_FragmentCode;

        // Normal Map
        if (normalSrc)
            gs << "    N = normalize(" << compiler.GetPinVariable(normalSrc->ID) << ");\n\n";

        // Propriedades do material
        gs << "    vec3  matBaseColor = " << baseColor << ";\n";
        gs << "    float matMetallic  = " << metallic << ";\n";
        gs << "    float matRoughness = clamp(" << roughness << ", 0.05, 1.0);\n";
        gs << "    float matAO        = clamp(" << ao << ", 0.0, 1.0);\n";
        gs << "    vec3  matEmissive  = " << emissive << ";\n\n";

        // Escreve no G-Buffer
        gs << "    g_Position = v_FragPos;\n";
        gs << "    g_Normal   = N;\n";
        gs << "    g_Albedo   = vec4(matBaseColor, matMetallic);\n";
        gs << "    g_PBR      = vec2(matRoughness, matAO);\n";
        gs << "    g_Emissive = matEmissive;\n";
        gs << "}\n";

        result.FragmentShader = fs.str();
        result.GeometryFragShader = gs.str();
        //AXE_CORE_INFO("GeometryFragShader:\n{}", result.GeometryFragShader.substr(0, 500));
        // 9. Vertex Shader
        result.VertexShader = R"(
        #version 460 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;
        layout(location = 2) in vec2 a_TexCoord;
        layout(location = 3) in vec3 a_Tangent;
        layout(location = 4) in vec3 a_Bitangent;

        uniform mat4 u_Model;
        uniform mat4 u_ViewProjection;
        uniform mat3 u_NormalMatrix;
        

        out vec3 v_Normal;
        out vec3 v_FragPos;
        out vec2 v_TexCoord;
        out vec3 v_Tangent;
        out vec3 v_Bitangent;

        void main()
        {
            vec4 worldPos  = u_Model * vec4(a_Position, 1.0);
            v_FragPos      = worldPos.xyz;
            v_Normal       = normalize(u_NormalMatrix * a_Normal);
            v_Tangent      = normalize(u_NormalMatrix * a_Tangent);
            v_Bitangent    = normalize(u_NormalMatrix * a_Bitangent);
            v_TexCoord     = a_TexCoord;
            gl_Position    = u_ViewProjection * worldPos;
        }
    )";

        result.Success = true;

        // Log temporário
        {
            std::istringstream stream(result.FragmentShader);
            std::string line;
            int lineNum = 0;
            std::string lines;
            while (std::getline(stream, line) && lineNum < 40)
                lines += std::to_string(++lineNum) + ": " + line + "\n";
            //AXE_CORE_INFO("Fragment:\n{}", lines);
        }

        //AXE_CORE_INFO("MaterialCompiler: Compilation successful.");
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

        //AXE_CORE_INFO("GenerateNodeCode: '{}'", node->Name);

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
            RegisterPin(node->Outputs[1].ID, var + ".x", PinType::Float);
            RegisterPin(node->Outputs[2].ID, var + ".y", PinType::Float);
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
            RegisterPin(node->Outputs[3].ID, var + ".a", PinType::Float);
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
            std::string valA = std::to_string(node->Inputs[0].DefaultFloat);
            std::string valB = std::to_string(node->Inputs[1].DefaultFloat);
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
            std::string valA = std::to_string(node->Inputs[0].DefaultFloat);
            std::string valB = std::to_string(node->Inputs[1].DefaultFloat);
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
            std::string valA = std::to_string(node->Inputs[0].DefaultFloat);
            std::string valB = std::to_string(node->Inputs[1].DefaultFloat);
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
            std::string valA = std::to_string(node->Inputs[0].DefaultFloat);
            std::string valB = std::to_string(node->Inputs[1].DefaultFloat);
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
            std::string valA = "1.0", valB = std::to_string(node->Inputs[1].DefaultFloat);
            PinType typeA = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }

            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB)
            {
                PinType typeB = GetPinType(srcB->ID);
                valB = GetPinVariable(srcB->ID);
                // B deve ser float — se for vec, converte para luminância
                if (typeB == PinType::Vec3 || typeB == PinType::Vec4)
                    valB = "dot(" + valB + ", vec3(0.299, 0.587, 0.114))";
            }

            // pow(vec3, float) não existe em GLSL — usa vec3(float) para o expoente
            RegisterPin(node->Outputs[0].ID, var, typeA);
            if (typeA == PinType::Vec3 || typeA == PinType::Vec4)
                code << GetGLSLType(typeA) << " " << var
                << " = pow(max(" << valA << ", vec3(0.0)), vec3(" << valB << "));";
            else
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
            std::string valA = std::to_string(node->Inputs[0].DefaultFloat);
            std::string valB = std::to_string(node->Inputs[1].DefaultFloat);
            std::string alpha = std::to_string(node->Inputs[2].DefaultFloat);
            PinType typeA = PinType::Float, typeB = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }

            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) { valB = GetPinVariable(srcB->ID); typeB = GetPinType(srcB->ID); }

            Pin* srcAlpha = GetSourcePin(&node->Inputs[2]);
            if (srcAlpha) alpha = GetPinVariable(srcAlpha->ID);

            // Tipo resultado: o maior entre A e B
            PinType resultType = (typeA >= typeB) ? typeA : typeB;

            // Converte A e B para o mesmo tipo
            if (resultType == PinType::Vec3 && typeA == PinType::Float)
                valA = "vec3(" + valA + ")";
            if (resultType == PinType::Vec3 && typeB == PinType::Float)
                valB = "vec3(" + valB + ")";
            if (resultType == PinType::Vec4 && typeA != PinType::Vec4)
                valA = "vec4(" + valA + ", 1.0)";
            if (resultType == PinType::Vec4 && typeB != PinType::Vec4)
                valB = "vec4(" + valB + ", 1.0)";

            RegisterPin(node->Outputs[0].ID, var, resultType);
            code << GetGLSLType(resultType) << " " << var
                << " = mix(" << valA << ", " << valB
                << ", clamp(" << alpha << ", 0.0, 1.0));";
        }

        // Clamp
        else if (node->Name == "Clamp")
        {
            std::string var = MakeVar("clamp");
            std::string val = "0.0";
            std::string minVal = std::to_string(node->Inputs[1].DefaultFloat);
            std::string maxVal = std::to_string(node->Inputs[2].DefaultFloat);
            PinType     typeV = PinType::Float;

            Pin* srcVal = GetSourcePin(&node->Inputs[0]);
            if (srcVal) { val = GetPinVariable(srcVal->ID); typeV = GetPinType(srcVal->ID); }

            Pin* srcMin = GetSourcePin(&node->Inputs[1]);
            if (srcMin)
            {
                minVal = GetPinVariable(srcMin->ID);
                // Converte min para o mesmo tipo de val
                PinType typeMin = GetPinType(srcMin->ID);
                if (typeV == PinType::Vec3 && typeMin == PinType::Float)
                    minVal = "vec3(" + minVal + ")";
                else if (typeV == PinType::Float && typeMin == PinType::Vec3)
                    minVal = "dot(" + minVal + ", vec3(0.299, 0.587, 0.114))";
            }

            Pin* srcMax = GetSourcePin(&node->Inputs[2]);
            if (srcMax)
            {
                maxVal = GetPinVariable(srcMax->ID);
                // Converte max para o mesmo tipo de val
                PinType typeMax = GetPinType(srcMax->ID);
                if (typeV == PinType::Vec3 && typeMax == PinType::Float)
                    maxVal = "vec3(" + maxVal + ")";
                else if (typeV == PinType::Float && typeMax == PinType::Vec3)
                    maxVal = "dot(" + maxVal + ", vec3(0.299, 0.587, 0.114))";
            }

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

            // Usa vec3(1.0) quando o valor é vec3
            std::string one = (type == PinType::Vec3) ? "vec3(1.0)" :
                (type == PinType::Vec4) ? "vec4(1.0)" : "1.0";
            code << GetGLSLType(type) << " " << var << " = " << one << " - " << val << ";";
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
            std::string exponent = std::to_string(node->Inputs[0].DefaultFloat);
            std::string normal = "N";

            Pin* srcExp = GetSourcePin(&node->Inputs[0]);
            if (srcExp)
            {
                PinType t = GetPinType(srcExp->ID);
                if (t == PinType::Float) // só aceita float
                    exponent = GetPinVariable(srcExp->ID);
                // vec ignorado — usa 5.0
            }

            Pin* srcNorm = GetSourcePin(&node->Inputs[1]);
            if (srcNorm)
            {
                PinType t = GetPinType(srcNorm->ID);
                if (t == PinType::Vec3) // só aceita vec3
                    normal = GetPinVariable(srcNorm->ID);
                // float ignorado — usa N
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var
                << " = pow(1.0 - max(dot(normalize(" << normal
                << "), normalize(u_CameraPosition - v_FragPos)), 0.0), "
                << exponent << ");";
        }

        //Normal Map
        else if (node->Name == "Normal Map")
        {
            std::string var = MakeVar("normalmap");
            std::string texVal = "vec3(0.5, 0.5, 1.0)"; // normal padrão
            std::string strength = std::to_string(node->Inputs[1].DefaultFloat);

            Pin* srcTex = GetSourcePin(&node->Inputs[0]);
            if (srcTex)
            {
                PinType t = GetPinType(srcTex->ID);
                if (t == PinType::Vec3 || t == PinType::Vec4)
                    texVal = GetPinVariable(srcTex->ID);
            }

            Pin* srcStr = GetSourcePin(&node->Inputs[1]);
            if (srcStr)
            {
                PinType t = GetPinType(srcStr->ID);
                if (t == PinType::Float)
                    strength = GetPinVariable(srcStr->ID);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            code << "vec3 " << var << "_raw = " << texVal << ";\n";
            code << "vec3 " << var << "_ts  = normalize(" << var
                << "_raw * 2.0 - 1.0);\n";
            code << "vec3 " << var << " = normalize(mat3(v_Tangent, v_Bitangent, v_Normal) * "
                << var << "_ts * vec3(" << strength << ", " << strength << ", 1.0));";
        }

        // -----------------------------------------------------------------
        // Sine — sin(x)
        // -----------------------------------------------------------------
        else if (node->Name == "Sine")
        {
            std::string var = MakeVar("sine");
            std::string val = std::to_string(node->Inputs[0].DefaultFloat);
            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) val = GetPinVariable(src->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = sin(" << val << ");";
        }

        // -----------------------------------------------------------------
        // Cosine — cos(x)
        // -----------------------------------------------------------------
        else if (node->Name == "Cosine")
        {
            std::string var = MakeVar("cosine");
            std::string val = std::to_string(node->Inputs[0].DefaultFloat);
            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) val = GetPinVariable(src->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = cos(" << val << ");";
        }

        // -----------------------------------------------------------------
        // Step — step(edge, x)
        // -----------------------------------------------------------------
        else if (node->Name == "Step")
        {
            std::string var = MakeVar("step");
            std::string edge = std::to_string(node->Inputs[0].DefaultFloat);
            std::string val = std::to_string(node->Inputs[1].DefaultFloat);

            Pin* srcEdge = GetSourcePin(&node->Inputs[0]);
            if (srcEdge) edge = GetPinVariable(srcEdge->ID);
            Pin* srcVal = GetSourcePin(&node->Inputs[1]);
            if (srcVal) val = GetPinVariable(srcVal->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = step(" << edge << ", " << val << ");";
        }

        // -----------------------------------------------------------------
        // SmoothStep — smoothstep(min, max, x)
        // -----------------------------------------------------------------
        else if (node->Name == "SmoothStep")
        {
            std::string var = MakeVar("smoothstep");
            std::string minVal = std::to_string(node->Inputs[0].DefaultFloat);
            std::string maxVal = std::to_string(node->Inputs[1].DefaultFloat);
            std::string val = std::to_string(node->Inputs[2].DefaultFloat);

            Pin* srcMin = GetSourcePin(&node->Inputs[0]);
            if (srcMin) minVal = GetPinVariable(srcMin->ID);
            Pin* srcMax = GetSourcePin(&node->Inputs[1]);
            if (srcMax) maxVal = GetPinVariable(srcMax->ID);
            Pin* srcVal = GetSourcePin(&node->Inputs[2]);
            if (srcVal) val = GetPinVariable(srcVal->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = smoothstep(" << minVal << ", " << maxVal << ", " << val << ");";
        }

        // -----------------------------------------------------------------
        // Normalize — normalize(v)
        // -----------------------------------------------------------------
        else if (node->Name == "Normalize")
        {
            std::string var = MakeVar("normalize");
            std::string val = "vec3(0.0, 1.0, 0.0)";
            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) val = GetPinVariable(src->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            code << "vec3 " << var << " = normalize(" << val << ");";
        }

        // -----------------------------------------------------------------
        // Distance — distance(A, B)
        // -----------------------------------------------------------------
        else if (node->Name == "Distance")
        {
            std::string var = MakeVar("dist");
            std::string valA = "vec3(0.0)", valB = "vec3(0.0)";

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) valA = GetPinVariable(srcA->ID);
            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) valB = GetPinVariable(srcB->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = distance(" << valA << ", " << valB << ");";
        }

        // -----------------------------------------------------------------
        // DotProduct — dot(A, B)
        // -----------------------------------------------------------------
        else if (node->Name == "DotProduct")
        {
            std::string var = MakeVar("dotp");
            std::string valA = "vec3(0.0)", valB = "vec3(0.0)";

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) valA = GetPinVariable(srcA->ID);
            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) valB = GetPinVariable(srcB->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = dot(" << valA << ", " << valB << ");";
        }

        // -----------------------------------------------------------------
        // Desaturate — mistura entre a cor original e seu nível de cinza
        // (luminância) de acordo com Fraction (0 = original, 1 = P&B)
        // -----------------------------------------------------------------
        else if (node->Name == "Desaturate")
        {
            std::string var = MakeVar("desat");
            std::string color = "vec3(1.0)";
            std::string fraction = std::to_string(node->Inputs[1].DefaultFloat);

            Pin* srcColor = GetSourcePin(&node->Inputs[0]);
            if (srcColor) color = GetPinVariable(srcColor->ID);
            Pin* srcFrac = GetSourcePin(&node->Inputs[1]);
            if (srcFrac) fraction = GetPinVariable(srcFrac->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            code << "float " << var << "_lum = dot(" << color << ", vec3(0.299, 0.587, 0.114));\n";
            code << "vec3 " << var << " = mix(" << color << ", vec3(" << var << "_lum), " << fraction << ");";
        }

        // -----------------------------------------------------------------
        // Append — combina um Vec3 e um Float num Vec4 (ex: RGB + Alpha,
        // ou qualquer empacotamento de canais)
        // -----------------------------------------------------------------
        else if (node->Name == "Append")
        {
            std::string var = MakeVar("append");
            std::string valA = "vec3(0.0)";
            std::string valB = std::to_string(node->Inputs[1].DefaultFloat);

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) valA = GetPinVariable(srcA->ID);
            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) valB = GetPinVariable(srcB->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec4);
            code << "vec4 " << var << " = vec4(" << valA << ", " << valB << ");";
        }

        // -----------------------------------------------------------------
        // Vector Split — separa um Vec3 em X, Y, Z
        // -----------------------------------------------------------------
        else if (node->Name == "Vector Split")
        {
            std::string var = MakeVar("split");
            std::string val = "vec3(0.0)";
            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) val = GetPinVariable(src->ID);

            code << "vec3 " << var << " = " << val << ";";
            RegisterPin(node->Outputs[0].ID, var + ".x", PinType::Float);
            RegisterPin(node->Outputs[1].ID, var + ".y", PinType::Float);
            RegisterPin(node->Outputs[2].ID, var + ".z", PinType::Float);
        }

        // -----------------------------------------------------------------
        // Camera Vector — direção (normalizada) da superfície até a câmera
        // -----------------------------------------------------------------
        else if (node->Name == "Camera Vector")
        {
            std::string var = MakeVar("camvec");
            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            code << "vec3 " << var << " = normalize(u_CameraPosition - v_FragPos);";
        }

        // -----------------------------------------------------------------
        // Reflection Vector — reflexo do vetor de visão em torno da normal
        // -----------------------------------------------------------------
        else if (node->Name == "Reflection Vector")
        {
            std::string var = MakeVar("reflvec");
            std::string normal = "N";

            Pin* srcNorm = GetSourcePin(&node->Inputs[0]);
            if (srcNorm)
            {
                PinType t = GetPinType(srcNorm->ID);
                if (t == PinType::Vec3) normal = GetPinVariable(srcNorm->ID);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            code << "vec3 " << var << " = reflect(-normalize(u_CameraPosition - v_FragPos), normalize("
                << normal << "));";
        }

        // -----------------------------------------------------------------
        // Time — tempo de execução em segundos (u_Time, atualizado por
        // frame pelo renderer). Base para animar materiais.
        // -----------------------------------------------------------------
        else if (node->Name == "Time")
        {
            RegisterPin(node->Outputs[0].ID, "u_Time", PinType::Float);
            // Sem declaração de variável — u_Time já é um uniform global,
            // referenciá-lo direto evita uma cópia desnecessária.
        }

        // -----------------------------------------------------------------
        // Panner — desloca um UV ao longo do tempo (água, energia,
        // hologramas, etc.) — igual ao node "Panner" da Unreal.
        // -----------------------------------------------------------------
        else if (node->Name == "Panner")
        {
            std::string var = MakeVar("pan");
            std::string uv = "v_TexCoord";
            std::string speedX = std::to_string(node->Inputs[1].DefaultFloat);
            std::string speedY = std::to_string(node->Inputs[2].DefaultFloat);

            Pin* srcUV = GetSourcePin(&node->Inputs[0]);
            if (srcUV)
            {
                PinType t = GetPinType(srcUV->ID);
                if (t == PinType::Vec2) uv = GetPinVariable(srcUV->ID);
            }

            Pin* srcSpeedX = GetSourcePin(&node->Inputs[1]);
            if (srcSpeedX) speedX = GetPinVariable(srcSpeedX->ID);
            Pin* srcSpeedY = GetSourcePin(&node->Inputs[2]);
            if (srcSpeedY) speedY = GetPinVariable(srcSpeedY->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec2);
            code << "vec2 " << var << " = " << uv
                << " + u_Time * vec2(" << speedX << ", " << speedY << ");";
        }

        // -----------------------------------------------------------------
        // Min — min(A, B)
        // -----------------------------------------------------------------
        else if (node->Name == "Min")
        {
            std::string var = MakeVar("min");
            std::string valA = "0.0", valB = "0.0";
            PinType typeA = PinType::Float, typeB = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }
            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) { valB = GetPinVariable(srcB->ID); typeB = GetPinType(srcB->ID); }

            PinType resultType = (typeA >= typeB) ? typeA : typeB;
            RegisterPin(node->Outputs[0].ID, var, resultType);
            code << GetGLSLType(resultType) << " " << var
                << " = min(" << valA << ", " << valB << ");";
        }

        // -----------------------------------------------------------------
        // Max — max(A, B)
        // -----------------------------------------------------------------
        else if (node->Name == "Max")
        {
            std::string var = MakeVar("max");
            std::string valA = "0.0", valB = "0.0";
            PinType typeA = PinType::Float, typeB = PinType::Float;

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) { valA = GetPinVariable(srcA->ID); typeA = GetPinType(srcA->ID); }
            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) { valB = GetPinVariable(srcB->ID); typeB = GetPinType(srcB->ID); }

            PinType resultType = (typeA >= typeB) ? typeA : typeB;
            RegisterPin(node->Outputs[0].ID, var, resultType);
            code << GetGLSLType(resultType) << " " << var
                << " = max(" << valA << ", " << valB << ");";
        }

        // -----------------------------------------------------------------
        // Saturate — clamp(value, 0, 1). GLSL aceita clamp(vecN, float, float)
        // nativamente, então funciona igual pra float/vec2/vec3/vec4.
        // -----------------------------------------------------------------
        else if (node->Name == "Saturate")
        {
            std::string var = MakeVar("sat");
            std::string val = "0.0";
            PinType type = PinType::Float;

            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) { val = GetPinVariable(src->ID); type = GetPinType(src->ID); }

            RegisterPin(node->Outputs[0].ID, var, type);
            code << GetGLSLType(type) << " " << var << " = clamp(" << val << ", 0.0, 1.0);";
        }

        // -----------------------------------------------------------------
        // Length — length(v)
        // -----------------------------------------------------------------
        else if (node->Name == "Length")
        {
            std::string var = MakeVar("len");
            std::string val = "vec3(0.0)";
            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) val = GetPinVariable(src->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = length(" << val << ");";
        }

        // -----------------------------------------------------------------
        // CrossProduct — cross(A, B)
        // -----------------------------------------------------------------
        else if (node->Name == "CrossProduct")
        {
            std::string var = MakeVar("cross");
            std::string valA = "vec3(0.0)", valB = "vec3(0.0)";

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) valA = GetPinVariable(srcA->ID);
            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) valB = GetPinVariable(srcB->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            code << "vec3 " << var << " = cross(" << valA << ", " << valB << ");";
        }

        // -----------------------------------------------------------------
        // If — compara A e B, escolhe entre 3 valores (A>B / A==B / A<B).
        // Igual ao node "If" da Unreal.
        // -----------------------------------------------------------------
        else if (node->Name == "If")
        {
            std::string var = MakeVar("ifres");
            std::string a = std::to_string(node->Inputs[0].DefaultFloat);
            std::string b = std::to_string(node->Inputs[1].DefaultFloat);

            Pin* srcA = GetSourcePin(&node->Inputs[0]);
            if (srcA) a = GetPinVariable(srcA->ID);
            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB) b = GetPinVariable(srcB->ID);

            std::string greater = "0.0", equal = "0.0", less = "0.0";
            PinType type = PinType::Float;

            Pin* srcGreater = GetSourcePin(&node->Inputs[2]);
            if (srcGreater) { greater = GetPinVariable(srcGreater->ID); type = GetPinType(srcGreater->ID); }
            Pin* srcEqual = GetSourcePin(&node->Inputs[3]);
            if (srcEqual) equal = GetPinVariable(srcEqual->ID);
            Pin* srcLess = GetSourcePin(&node->Inputs[4]);
            if (srcLess) less = GetPinVariable(srcLess->ID);

            RegisterPin(node->Outputs[0].ID, var, type);
            code << GetGLSLType(type) << " " << var << " = (" << a << " > " << b << ") ? ("
                << greater << ") : ((" << a << " < " << b << ") ? (" << less << ") : (" << equal << "));";
        }

        // -----------------------------------------------------------------
        // Noise — ruído pseudo-aleatório baseado em UV (hash determinístico,
        // sem necessidade de textura). Útil pra quebrar padrões repetitivos.
        // -----------------------------------------------------------------
        else if (node->Name == "Noise")
        {
            std::string var = MakeVar("noise");
            std::string uv = "v_TexCoord";

            Pin* srcUV = GetSourcePin(&node->Inputs[0]);
            if (srcUV)
            {
                PinType t = GetPinType(srcUV->ID);
                if (t == PinType::Vec2) uv = GetPinVariable(srcUV->ID);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = fract(sin(dot(" << uv
                << ", vec2(12.9898, 78.233))) * 43758.5453);";
        }

        // -----------------------------------------------------------------
        // Vec2 / Vec3 — constantes vetoriais
        // -----------------------------------------------------------------
        else if (node->Name == "Vec2")
        {
            std::string var = MakeVar("vec2");
            auto& v = node->Value.Vec2Val;
            RegisterPin(node->Outputs[0].ID, var, PinType::Vec2);
            code << "vec2 " << var << " = vec2(" << v.x << ", " << v.y << ");";
        }
        else if (node->Name == "Vec3")
        {
            std::string var = MakeVar("vec3");
            auto& v = node->Value.Vec3Val;
            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            code << "vec3 " << var << " = vec3(" << v.x << ", " << v.y << ", " << v.z << ");";
        }

        // -----------------------------------------------------------------
        // Texture Coordinate — UV com tiling, offset e rotação (em graus,
        // pivotada no centro 0.5,0.5) — igual ao "Texture Coordinate" da
        // Unreal, mais completo que o "UV Coordinate" simples.
        // -----------------------------------------------------------------
        else if (node->Name == "Texture Coordinate")
        {
            std::string var = MakeVar("texcoord");
            std::string uTiling = std::to_string(node->Inputs[0].DefaultFloat);
            std::string vTiling = std::to_string(node->Inputs[1].DefaultFloat);
            std::string uOffset = std::to_string(node->Inputs[2].DefaultFloat);
            std::string vOffset = std::to_string(node->Inputs[3].DefaultFloat);
            std::string rotation = std::to_string(node->Inputs[4].DefaultFloat);

            Pin* srcUT = GetSourcePin(&node->Inputs[0]); if (srcUT) uTiling = GetPinVariable(srcUT->ID);
            Pin* srcVT = GetSourcePin(&node->Inputs[1]); if (srcVT) vTiling = GetPinVariable(srcVT->ID);
            Pin* srcUO = GetSourcePin(&node->Inputs[2]); if (srcUO) uOffset = GetPinVariable(srcUO->ID);
            Pin* srcVO = GetSourcePin(&node->Inputs[3]); if (srcVO) vOffset = GetPinVariable(srcVO->ID);
            Pin* srcRot = GetSourcePin(&node->Inputs[4]); if (srcRot) rotation = GetPinVariable(srcRot->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec2);
            code << "float " << var << "_rad = radians(" << rotation << ");\n";
            code << "vec2 " << var << "_centered = v_TexCoord - vec2(0.5);\n";
            code << "vec2 " << var << "_rotated = vec2(\n";
            code << "    " << var << "_centered.x * cos(" << var << "_rad) - " << var << "_centered.y * sin(" << var << "_rad),\n";
            code << "    " << var << "_centered.x * sin(" << var << "_rad) + " << var << "_centered.y * cos(" << var << "_rad)\n";
            code << ") + vec2(0.5);\n";
            code << "vec2 " << var << " = " << var << "_rotated * vec2(" << uTiling << ", " << vTiling
                << ") + vec2(" << uOffset << ", " << vOffset << ");";
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
        //AXE_CORE_WARN("MaterialCompiler: pin {} not registered, using 0.0", pinId.Get());
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