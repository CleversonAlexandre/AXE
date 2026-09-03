#include "material_compiler.hpp"
#include "axe/material/light_material_evaluator.hpp"
#include "axe/material/material_cooked.hpp"   // B4 — .axeshader
#include "axe/log/log.hpp"
#include <sstream>
#include <iomanip>
#include <unordered_set>

#include "axe/graphics/texture.hpp"
#include "axe/graphics/shader.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{
    MaterialCompiler::MaterialCompiler(MaterialGraph* graph)
        : m_Graph(graph)
    {}

    // ── PKG9 — samplers por UUID, para o `.axeshader` ────────────────────────
    //
    // Os tres dominios nomeiam os samplers de formas diferentes ("u_AlbedoMap",
    // "u_LightTex_N", "u_PartTex_N") e decidem em momentos diferentes QUAIS nos
    // entram. O que todos tem em comum e o par final: `m_NodeSamplers` (no ->
    // nome do uniform) e o mapa de texturas ja filtrado.
    //
    // Por isso este helper roda no FIM, sobre o mapa final: qualquer sampler que
    // sobreviveu a filtragem do dominio entra, e nenhum outro. Derivar antes
    // significaria repetir a regra de filtragem de cada dominio — tres copias
    // para divergirem depois.
    //
    // Sampler sem UUID (textura arrastada de fora do projeto, por exemplo)
    // simplesmente nao entra: o cozido nao tem como reencontra-la, e inventar um
    // caminho seria pior que a ausencia.
    void MaterialCompiler::CollectSamplerUUIDs(const MaterialCompiler& compiler,
        MaterialGraph* graph,
        const std::map<std::string, std::shared_ptr<Texture2D>>& finalSamplers,
        CompiledMaterial& result)
    {
        if (!graph) return;

        for (auto& node : graph->GetNodes())
        {
            if (node->Name != "Texture Sample") continue;
            if (node->Value.TextureUUID.empty()) continue;

            auto it = compiler.m_NodeSamplers.find(node->ID.Get());
            if (it == compiler.m_NodeSamplers.end()) continue;
            if (!finalSamplers.count(it->second)) continue;

            result.SamplerTextureUUIDs[it->second] = node->Value.TextureUUID;
        }
    }


    CompiledMaterial MaterialCompiler::Compile(MaterialGraph* graph)
    {
        MaterialCompiler compiler(graph);
        CompiledMaterial result;

        // ── SHADING_MODEL_V1 — traducao UI -> contrato de runtime ────────────
        //
        // O UNICO ponto onde os dois enums se encontram. Se um shading model
        // novo for implementado, e aqui que ele entra — e o compilador falhando
        // em traduzir cai em DefaultLit, que e o comportamento seguro (um
        // placeholder da Unreal escolhido por engano renderiza como PBR normal,
        // e nao como lixo).
        const ShadingModelID shadingID =
            (graph->ShadingModel == MaterialShadingModel::Unlit) ? ShadingModelID::Unlit :
            (graph->ShadingModel == MaterialShadingModel::Toon) ? ShadingModelID::Toon :
            ShadingModelID::DefaultLit;

        int toonSteps = graph->ToonSteps;
        if (toonSteps < 2) toonSteps = 2;
        if (toonSteps > (int)kToonStepsMax) toonSteps = (int)kToonStepsMax;

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
            //
            // SRGB_TEXTURES_V1 — o INDICE do pin classifica a textura em COR
            // ou DADO. A ordem dos pins do Material Output e fixa e esta
            // documentada em MaterialGraph::AddMaterialOutputNode (novos pins
            // sempre entram no FIM, justamente para nao deslocar estes
            // indices): 0=Base Color, 1=Metallic, 2=Roughness, 3=Normal,
            // 4=Emissive, 5=Opacity, 6=AO, 7=Specular.
            int pinIndex = 0;
            for (auto& inputPin : outputNode->Inputs)
            {
                const bool pinIsColor = (pinIndex == 0 || pinIndex == 4);
                ++pinIndex;

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

                        if (pinIsColor)
                            compiler.m_SRGBSamplers.insert(texNode->ID.Get());
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
                {
                    result.SamplerTextures[it->second] = node->Value.TextureVal;

                    // B4 — o mesmo mapeamento, por UUID, para o `.axeshader`.
                    if (!node->Value.TextureUUID.empty())
                        result.SamplerTextureUUIDs[it->second] = node->Value.TextureUUID;
                }

                if (it->second == "u_AlbedoMap")
                {
                    result.AlbedoTexture = node->Value.TextureVal;
                    result.AlbedoSamplerName = it->second;   // B4
                }
                else if (it->second == "u_Texture_1")
                {
                    result.NormalTexture = node->Value.TextureVal;
                    result.NormalSamplerName = it->second;   // B4
                }
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
        // v_Age01 não existe como varying no Surface shader — declara como
        // constante 0.0 pra que o node "Particle Age" compile sem erro
        // mesmo em materiais Surface (comportamento definido, não crash).
        fs << "float v_Age01 = 0.0;\n";
        fs << "vec4  v_Color  = vec4(1.0);\n"; // fallback — sem sentido em Surface, mas não crasha

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
        // CUSTOM_NODE_V1 — as funcoes dos nodes Custom entram AQUI, entre as
        // declaracoes e o main(). GLSL exige a funcao declarada antes do uso, e
        // este e o unico ponto do shader gerado onde isso e verdade.
        fs << compiler.m_CustomFunctions;
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

        // Blend Mode Masked (alpha test): recorta via discard e continua
        // OPACO (deferred) — não entra no forward translúcido.
        bool isMasked = (graph->BlendMode == MaterialBlendMode::Masked);
        result.IsMasked = isMasked;
        result.AlphaCutoff = 0.5f;

        // Opacity conectado a algo => este material precisa do forward
        // pass de transparência (vidro, etc.) — ver SceneRenderer.
        // Exceção: Masked vai pelo deferred (o discard faz o recorte).
        result.IsTransparent = (opacitySrc != nullptr) && !isMasked;

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

        // ── SHADING_MODEL_V1 — o mesmo modelo, no caminho FORWARD ────────────
        //
        // Sem isto o preview do Material Editor (que roda forward) continuaria
        // mostrando PBR enquanto o viewport (deferred) mostra Toon. Um editor
        // cujo preview nao e o resultado nao serve para escolher aparencia —
        // que e a unica coisa que se faz num editor de material.
        //
        // A diferenca de FORMA entre os dois caminhos e proposital: aqui o
        // modelo e resolvido em TEMPO DE COMPILACAO (este shader pertence a UM
        // material, entao so o ramo escolhido e emitido — sem branch em runtime
        // e sem codigo morto); no deferred e um switch em runtime, porque um
        // shader so atende todos os materiais da tela.
        if (shadingID == ShadingModelID::Unlit)
        {
            fs << "    // Unlit — nenhuma luz, nenhuma sombra, nenhum ambient.\n";
            fs << "    vec3 finalColor = matBaseColor + matEmissive;\n\n";
        }
        else if (shadingID == ShadingModelID::Toon)
        {
            fs << "    // ── Toon ──────────────────────────────────────────\n";
            fs << "    const float toonSteps = " << toonSteps << ".0;\n";
            // Difusa em degraus. Sem /PI e sem kD: a celula quer a cor CHAPADA
            // do albedo em cada banda, e nao a resposta energeticamente
            // correta — a graca do estilo e justamente a superficie plana.
            fs << "    float toonBand = floor(clamp(NdotL, 0.0, 1.0) * toonSteps + 0.5) / toonSteps;\n";
            fs << "    vec3  toonDiffuse = matBaseColor * toonBand * u_LightColor * u_LightIntensity;\n";
            // Especular de corte duro. O limiar vem do ROUGHNESS: liso = brilho
            // pequeno e concentrado, aspero = maior e mais espalhado. Reusar um
            // parametro que o artista ja mexe evita inventar um slider novo
            // (e evita gastar o unico canal que ainda sobrava no G-Buffer).
            fs << "    float toonSpecCut = mix(0.995, 0.75, matRoughness);\n";
            fs << "    float toonSpec = step(toonSpecCut, pow(NdotH, 64.0)) * (1.0 - matRoughness);\n";
            // O ambient entra CHAPADO, sem quantizar: e ele que da cor a
            // banda de sombra. Quantizado tambem, a sombra ficaria preta e o
            // material perderia a leitura de volume por completo.
            fs << "    vec3 finalColor = ambient + toonDiffuse + vec3(toonSpec) + matEmissive;\n\n";
        }
        else
        {
            fs << "    vec3 finalColor = ambient + Lo + matEmissive;\n";
        }

        if (shadingID != ShadingModelID::Unlit)
            fs << "    finalColor = max(finalColor, matBaseColor * 0.02);\n\n";
        else
            fs << "\n";
        //fs << "    finalColor = finalColor / (finalColor + vec3(1.0));\n";
        //fs << "    finalColor = pow(finalColor, vec3(1.0 / 2.2));\n\n";
        if (isMasked)
        {
            // Alpha test: recorta o pixel e sai opaco (sem blend).
            fs << "    if (matOpacity < 0.5) discard;\n";
            fs << "    FragColor = vec4(finalColor, 1.0);\n";
        }
        else
        {
            fs << "    FragColor = vec4(finalColor, matOpacity);\n";
        }
        fs << "}\n";

        std::stringstream gs;
        gs << "#version 460 core\n";
        gs << "layout(location = 0) out vec3 g_Position;\n";
        gs << "layout(location = 1) out vec3 g_Normal;\n";
        gs << "layout(location = 2) out vec4 g_Albedo;\n";
        // SHADING_MODEL_V1 — era `out vec2`, e por isso .b/.a chegavam
        // INDEFINIDOS no attachment RGBA8. Virou vec4 para que o .b (id do
        // shading model) e o .a (bandas do toon) tenham valor de verdade.
        gs << "layout(location = 3) out vec4 g_PBR;\n";
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
        // Fallbacks pra nodes que existem no Fragment mas não no GeometryShader
        // (Particle Color, Particle Age). Sem isso o GeometryShader crasha se
        // o usuário usar esses nodes num material Surface inadvertidamente, e
        // qualquer uso legítimo compila limpo com valor neutro.
        gs << "float v_Age01 = 0.0;\n";
        gs << "vec4  v_Color  = vec4(1.0);\n\n";

        // CUSTOM_NODE_V1 — as funcoes dos nodes Custom entram AQUI, entre as
        // declaracoes e o main(). GLSL exige a funcao declarada antes do uso, e
        // este e o unico ponto do shader gerado onde isso e verdade.
        gs << compiler.m_CustomFunctions;
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

        // Masked: recorta ANTES de escrever no G-Buffer. O pixel descartado
        // não entra no deferred, então o fundo aparece pelo vão — recorte
        // limpo, com o material continuando opaco (luz/sombra normais).
        if (isMasked)
        {
            gs << "    float matOpacity = " << opacity << ";\n";
            gs << "    if (matOpacity < 0.5) discard;\n\n";
        }

        // Escreve no G-Buffer
        gs << "    g_Position = v_FragPos;\n";
        gs << "    g_Normal   = N;\n";
        gs << "    g_Albedo   = vec4(matBaseColor, matMetallic);\n";
        // SHADING_MODEL_V1 — .b = id do shading model, .a = bandas do toon.
        // Os dois codificados como UNORM (valor/255 e valor/16); o lighting
        // pass decodifica com o mesmo fator. Ver material_cooked.hpp.
        gs << "    g_PBR      = vec4(matRoughness, matAO, "
            << (int)shadingID << ".0 / 255.0, "
            << toonSteps << ".0 / " << (int)kToonStepsMax << ".0);\n";
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
    // CompileLightFunction — domínio "Light Function"
    //
    // Bem mais simples que Compile(): sem PBR, sem G-Buffer, sem depender
    // de geometria real. Só resolve o que alimenta o pin Emissive do
    // Material Output e gera um shader pequeno, avaliado uma vez por frame
    // num framebuffer mínimo (ver LightMaterialEvaluator) — não por pixel
    // da tela. v_FragPos/v_Normal/v_TexCoord existem só pra nodes que os
    // referenciam (World Position, UV Coordinate, Fresnel, etc.) não
    // falharem a compilar; têm valores neutros fixos, já que não há uma
    // superfície real sendo avaliada aqui.
    // =========================================================================
    CompiledMaterial MaterialCompiler::CompileLightFunction(MaterialGraph* graph)
    {
        MaterialCompiler compiler(graph);
        CompiledMaterial result;

        Node* outputNode = nullptr;
        for (auto& node : graph->GetNodes())
            if (node->Name == "Material Output") { outputNode = node.get(); break; }

        if (!outputNode)
        {
            result.ErrorMessage = "Material Output node not found";
            return result;
        }

        // Emissive é o pin 4 do Material Output
        if (outputNode->Inputs.size() <= 4)
        {
            result.ErrorMessage = "Material Output sem pin Emissive";
            return result;
        }
        compiler.VisitPin(&outputNode->Inputs[4]);

        Pin* emissiveSrc = compiler.GetSourcePin(&outputNode->Inputs[4]);
        std::string emissive = emissiveSrc ? compiler.GetPinVariable(emissiveSrc->ID) : "vec3(1.0)";
        if (emissiveSrc && compiler.GetPinType(emissiveSrc->ID) == PinType::Float)
            emissive = "vec3(" + emissive + ")";

        // Texture Sample eventualmente usado pelo Emissive — mesmo
        // esquema de slot por node usado em Compile(), só que mais simples
        // (não precisa achar especificamente Base Color/Normal).
        std::map<std::string, std::shared_ptr<Texture2D>> samplerTextures;
        {
            int slot = 0;
            for (auto& node : graph->GetNodes())
            {
                if (node->Name != "Texture Sample") continue;
                if (!compiler.m_VisitedNodes.count(node->ID.Get())) continue; // só os usados
                if (!node->Value.TextureVal) continue;
                std::string samplerName = "u_LightTex_" + std::to_string(slot++);
                compiler.m_NodeSamplers[node->ID.Get()] = samplerName;
                samplerTextures[samplerName] = node->Value.TextureVal;
            }
        }

        // Vertex shader — quad simples (sem transformação de mundo: este
        // shader nunca é desenhado numa malha real, só num retângulo cobrindo
        // o framebuffer mínimo onde o resultado é lido de volta).
        result.VertexShader = R"(
        #version 460 core
        layout(location = 0) in vec3 a_Position;
        void main() { gl_Position = vec4(a_Position.xy, 0.0, 1.0); }
    )";

        std::ostringstream fs;
        fs << "#version 460 core\n";
        fs << "out vec4 FragColor;\n\n";
        fs << "uniform float u_Time;\n";
        fs << "uniform vec3  u_CameraPosition;\n\n";
        for (auto& [samplerName, tex] : samplerTextures)
            fs << "uniform sampler2D " << samplerName << ";\n";
        fs << "\n";
        // CUSTOM_NODE_V1 — as funcoes dos nodes Custom entram AQUI, entre as
        // declaracoes e o main(). GLSL exige a funcao declarada antes do uso, e
        // este e o unico ponto do shader gerado onde isso e verdade.
        fs << compiler.m_CustomFunctions;
        fs << "void main()\n{\n";
        fs << "    // Valores neutros — não há uma superfície real sendo avaliada,\n";
        fs << "    // existem só pra nodes que dependem deles não falharem ao compilar.\n";
        fs << "    vec3 v_FragPos = vec3(0.0);\n";
        fs << "    vec3 v_Normal = vec3(0.0, 1.0, 0.0);\n";
        fs << "    vec3 N = v_Normal;\n";
        fs << "    vec2 v_TexCoord = vec2(0.5, 0.5);\n";
        fs << "    vec3 v_Tangent = vec3(1.0, 0.0, 0.0);\n";
        fs << "    vec3 v_Bitangent = vec3(0.0, 0.0, 1.0);\n";
        fs << "    float v_Age01 = 0.0;\n";
        fs << "    vec4  v_Color  = vec4(1.0);\n\n";
        fs << compiler.m_FragmentCode;
        fs << "\n    vec3 finalColor = " << emissive << ";\n";
        fs << "    FragColor = vec4(finalColor, 1.0);\n";
        fs << "}\n";

        result.FragmentShader = fs.str();
        result.SamplerTextures = samplerTextures;
        CollectSamplerUUIDs(compiler, graph, samplerTextures, result);   // PKG9
        result.Success = true;
        return result;
    }

    // =========================================================================
    //  POSTPROCESS_DOMAIN_V1 — dominio Post Process
    //
    //  Efeito de tela inteira. O shader roda UMA VEZ POR PIXEL DA TELA, num
    //  quad que cobre o framebuffer — nao ha malha, nao ha superficie, nao ha
    //  luz. O grafo resolve o pin EMISSIVE, que aqui significa "a cor final
    //  deste pixel".
    //
    //  ── POR QUE O PIN EMISSIVE, E NAO UM PIN NOVO "SCENE COLOR OUT" ─────────
    //
    //  Emissive ja e, nos outros dominios, o pin que quer dizer "cor que sai
    //  daqui sem passar por iluminacao". E exatamente a semantica de um efeito
    //  de tela. Light Function e Particle ja usam o mesmo pin pela mesma razao;
    //  um pin novo so para este dominio faria o Material Output crescer e
    //  deslocaria os indices que o compilador referencia por posicao.
    //
    //  ── O QUE O AUTOR TEM EM MAOS ──────────────────────────────────────────
    //
    //  `v_TexCoord` e a UV DA TELA (0..1), porque o quad e a tela — entao o
    //  node "UV Coordinate", que ja existe, vira coordenada de tela de graca,
    //  sem node novo. Alem disso: u_SceneColor (a imagem), u_ScreenSize,
    //  u_Intensity, u_IsHDR e u_Time.
    //
    //  Os nodes Scene Color e Scene Depth (ver GenerateNodeCode) leem a imagem.
    // =========================================================================
    CompiledMaterial MaterialCompiler::CompilePostProcess(MaterialGraph* graph)
    {
        MaterialCompiler compiler(graph);
        CompiledMaterial result;

        // POSTPROCESS_DOMAIN_V1b — a partir daqui, os nodes de tela podem
        // emitir u_SceneColor/u_ScreenSize: e ESTE shader que os declara.
        compiler.m_PostProcessTarget = true;

        Node* outputNode = nullptr;
        for (auto& node : graph->GetNodes())
            if (node->Name == "Material Output") { outputNode = node.get(); break; }

        if (!outputNode || outputNode->Inputs.size() <= 4)
        {
            result.ErrorMessage = "Material Output sem pin Emissive";
            return result;
        }

        compiler.VisitPin(&outputNode->Inputs[4]);
        Pin* emissiveSrc = compiler.GetSourcePin(&outputNode->Inputs[4]);

        // Sem nada ligado no Emissive, o efeito e a IDENTIDADE: devolve a cena
        // intacta. Nao e detalhe — um material recem-criado neste dominio nao
        // pode apagar a tela do usuario enquanto ele monta o grafo.
        std::string emissive = emissiveSrc
            ? compiler.GetPinVariable(emissiveSrc->ID)
            : "texture(u_SceneColor, v_TexCoord).rgb";

        if (emissiveSrc && compiler.GetPinType(emissiveSrc->ID) == PinType::Float)
            emissive = "vec3(" + emissive + ")";

        // Samplers do grafo. Comecam em u_PPTex_0; a unidade 0 do passe e
        // sempre a cena, e as texturas do material entram a partir da 1 —
        // ver OpenGLPostProcessPass::DrawUserEffect.
        std::map<std::string, std::shared_ptr<Texture2D>> samplerTextures;
        {
            int slot = 0;
            for (auto& node : graph->GetNodes())
            {
                if (node->Name != "Texture Sample") continue;
                if (!compiler.m_VisitedNodes.count(node->ID.Get())) continue;
                if (!node->Value.TextureVal) continue;
                std::string samplerName = "u_PPTex_" + std::to_string(slot++);
                compiler.m_NodeSamplers[node->ID.Get()] = samplerName;
                samplerTextures[samplerName] = node->Value.TextureVal;
            }
        }

        // Vertex shader do quad. Mesmo layout do s_QuadVert do
        // OpenGLPostProcessPass (posicao + uv), porque e o VAO dele que
        // desenha — se este layout divergir daquele, a imagem sai torta.
        result.VertexShader = R"(
        #version 460 core
        layout(location = 0) in vec2 a_Position;
        layout(location = 1) in vec2 a_TexCoord;
        out vec2 v_TexCoord;
        void main()
        {
            v_TexCoord  = a_TexCoord;
            gl_Position = vec4(a_Position, 0.0, 1.0);
        }
    )";

        std::ostringstream fs;
        fs << "#version 460 core\n";
        fs << "out vec4 FragColor;\n";
        fs << "in  vec2 v_TexCoord;\n\n";
        fs << "uniform sampler2D u_SceneColor;\n";

        // POSTPROCESS_GBUFFER_V1 — o G-Buffer, para o efeito poder ler a
        // GEOMETRIA e nao so a cor. Declarados SEMPRE, mesmo que o material
        // nao os use: GLSL descarta uniform nao referenciada, e declarar
        // condicionalmente faria o numero de unidades de textura variar por
        // material — que e exatamente o que quebraria o bind de unidade fixa
        // do OpenGLPostProcessPass::DrawUserEffect.
        fs << "uniform sampler2D u_ScenePosition;   // xyz = posicao no mundo\n";
        fs << "uniform sampler2D u_SceneNormal;     // xyz = normal do mundo\n";
        fs << "uniform sampler2D u_ScenePBR;        // r=rough g=ao b=shadingModel a=toonSteps\n";
        fs << "uniform int       u_HasSceneBuffers;\n";

        // POSTPROCESS_SKY_V1 — camera e sol. Sao o que falta para o efeito
        // desenhar o CEU: o ceu nao esta no G-Buffer (e desenhado depois do
        // lighting pass), entao aqueles pixels nao tem normal nem posicao.
        // Detecta-los e facil; saber PARA ONDE cada um olha exige a inversa da
        // view-projection.
        fs << "uniform mat4      u_InvViewProjection;\n";
        fs << "uniform vec3      u_SunDirection;   // aponta PARA onde a luz vai\n";
        fs << "uniform vec3      u_SunColor;\n";
        fs << "uniform float     u_SunIntensity;\n";

        fs << "uniform vec2      u_ScreenSize;\n";
        fs << "uniform float     u_Intensity;\n";
        fs << "uniform int       u_IsHDR;\n";
        fs << "uniform float     u_Time;\n";
        fs << "uniform vec3      u_CameraPosition;\n\n";
        for (auto& kv : samplerTextures)
            fs << "uniform sampler2D " << kv.first << ";\n";
        fs << "\n";

        // CUSTOM_NODE_V1 — as funcoes dos nodes Custom, antes do main().
        fs << compiler.m_CustomFunctions;

        fs << "void main()\n{\n";

        // Valores neutros para nodes que referenciam varyings de superficie.
        // v_TexCoord NAO entra aqui: nele mora a UV de tela, que e real.
        fs << "    vec3 v_FragPos   = vec3(0.0);\n";
        fs << "    vec3 v_Normal    = vec3(0.0, 1.0, 0.0);\n";
        fs << "    vec3 N           = v_Normal;\n";
        fs << "    vec3 v_Tangent   = vec3(1.0, 0.0, 0.0);\n";
        fs << "    vec3 v_Bitangent = vec3(0.0, 0.0, 1.0);\n";
        fs << "    float v_Age01    = 0.0;\n";
        fs << "    vec4  v_Color    = vec4(1.0);\n\n";

        fs << compiler.m_FragmentCode;

        fs << "\n    vec3 sceneColor = texture(u_SceneColor, v_TexCoord).rgb;\n";
        fs << "    vec3 effectColor = " << emissive << ";\n";

        // u_Intensity misturando com a CENA ORIGINAL, e nao um multiplicador da
        // saida: e o que faz "50% do efeito" querer dizer meio caminho entre a
        // imagem e o efeito, em vez de metade do brilho dele.
        fs << "    vec3 finalColor = mix(sceneColor, effectColor, clamp(u_Intensity, 0.0, 1.0));\n";
        fs << "    FragColor = vec4(finalColor, 1.0);\n";
        fs << "}\n";

        result.FragmentShader = fs.str();
        result.SamplerTextures = samplerTextures;
        CollectSamplerUUIDs(compiler, graph, samplerTextures, result);
        result.Success = true;
        return result;
    }

    CompiledMaterial MaterialCompiler::CompileEmissiveAverage(MaterialGraph* graph)
    {
        MaterialCompiler compiler(graph);
        CompiledMaterial result;

        Node* outputNode = nullptr;
        for (auto& node : graph->GetNodes())
            if (node->Name == "Material Output") { outputNode = node.get(); break; }

        if (!outputNode || outputNode->Inputs.size() <= 4)
        {
            result.ErrorMessage = "Material Output sem pin Emissive";
            return result;
        }

        compiler.VisitPin(&outputNode->Inputs[4]);
        Pin* emissiveSrc = compiler.GetSourcePin(&outputNode->Inputs[4]);

        // DIFERENTE do CompileLightFunction: pin desconectado aqui NÃO
        // vira vec3(1) — o material simplesmente não emite (Success=false
        // e o caller escreve vec3(0) no BakedEmissive).
        if (!emissiveSrc)
        {
            result.ErrorMessage = "sem emissive";
            return result;
        }

        std::string emissive = compiler.GetPinVariable(emissiveSrc->ID);
        if (compiler.GetPinType(emissiveSrc->ID) == PinType::Float)
            emissive = "vec3(" + emissive + ")";

        std::map<std::string, std::shared_ptr<Texture2D>> samplerTextures;
        {
            int slot = 0;
            for (auto& node : graph->GetNodes())
            {
                if (node->Name != "Texture Sample") continue;
                if (!compiler.m_VisitedNodes.count(node->ID.Get())) continue;
                if (!node->Value.TextureVal) continue;
                std::string samplerName = "u_LightTex_" + std::to_string(slot++);
                compiler.m_NodeSamplers[node->ID.Get()] = samplerName;
                samplerTextures[samplerName] = node->Value.TextureVal;
            }
        }

        // VS espalha UVs pelo quad — cada texel do FBO 8x8 avalia o
        // grafo num UV diferente; a média dos 64 vira o BakedEmissive
        result.VertexShader = R"(
        #version 460 core
        layout(location = 0) in vec3 a_Position;
        out vec2 vAvgUV;
        void main()
        {
            vAvgUV = a_Position.xy * 0.5 + 0.5;
            gl_Position = vec4(a_Position.xy, 0.0, 1.0);
        }
    )";

        std::ostringstream fs;
        fs << "#version 460 core\n";
        fs << "in vec2 vAvgUV;\n";
        fs << "out vec4 FragColor;\n\n";
        fs << "uniform float u_Time;\n";
        fs << "uniform vec3  u_CameraPosition;\n\n";
        for (auto& [samplerName, tex] : samplerTextures)
            fs << "uniform sampler2D " << samplerName << ";\n";
        fs << "\n";
        // CUSTOM_NODE_V1 — as funcoes dos nodes Custom entram AQUI, entre as
        // declaracoes e o main(). GLSL exige a funcao declarada antes do uso, e
        // este e o unico ponto do shader gerado onde isso e verdade.
        fs << compiler.m_CustomFunctions;
        fs << "void main()\n{\n";
        fs << "    vec3 v_FragPos = vec3(0.0);\n";
        fs << "    vec3 v_Normal = vec3(0.0, 1.0, 0.0);\n";
        fs << "    vec3 N = v_Normal;\n";
        fs << "    vec2 v_TexCoord = vAvgUV; // <- UV VARIA pelo quad (media real)\n";
        fs << "    vec3 v_Tangent = vec3(1.0, 0.0, 0.0);\n";
        fs << "    vec3 v_Bitangent = vec3(0.0, 0.0, 1.0);\n";
        fs << "    float v_Age01 = 0.0;\n";
        fs << "    vec4  v_Color  = vec4(1.0);\n\n";
        fs << compiler.m_FragmentCode;
        fs << "\n    vec3 finalColor = " << emissive << ";\n";
        fs << "    // /8: o readback e LDR; o EvaluateAverage multiplica de volta\n";
        fs << "    FragColor = vec4(finalColor / 8.0, 1.0);\n";
        fs << "}\n";

        result.FragmentShader = fs.str();
        result.SamplerTextures = samplerTextures;
        result.Success = true;
        return result;
    }

    glm::vec3 MaterialCompiler::ComputeBakedEmissive(MaterialGraph* graph)
    {
        if (!graph) return glm::vec3(0.0f);

        auto avg = CompileEmissiveAverage(graph);
        if (!avg.Success) return glm::vec3(0.0f); // sem emissive = não emite

        std::shared_ptr<Shader> shader;
        try { shader = Shader::Create(avg.VertexShader, avg.FragmentShader); }
        catch (...) { return glm::vec3(0.0f); }
        if (!shader) return glm::vec3(0.0f);

        static LightMaterialEvaluator s_AvgEvaluator;
        s_AvgEvaluator.Initialize();
        return s_AvgEvaluator.EvaluateAverage(shader, avg.SamplerTextures);
    }

    bool MaterialCompiler::CompileLightFunctionFromFile(const std::filesystem::path& materialFilePath,
        std::shared_ptr<Shader>& outShader,
        std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers)
    {
        auto graphPath = materialFilePath;
        graphPath.replace_extension(".axegraph");
        if (!std::filesystem::exists(graphPath))
        {
            AXE_CORE_WARN("CompileLightFunctionFromFile: grafo não encontrado em '{}'", graphPath.string());
            return false;
        }

        std::ifstream file(graphPath);
        if (!file.is_open())
        {
            AXE_CORE_WARN("CompileLightFunctionFromFile: falha ao abrir '{}'", graphPath.string());
            return false;
        }

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);
            MaterialGraph graph;
            graph.Deserialize(j);

            auto result = CompileLightFunction(&graph);
            if (!result.Success)
            {
                AXE_CORE_WARN("CompileLightFunctionFromFile: {}", result.ErrorMessage);
                return false;
            }

            auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
            if (!shader) return false;

            // PKG9 — deixa o `.axeshader` em dia com a compilacao que acabou de
            // acontecer. Este ponto e chamado pelo callback do EditorLayer no
            // load de cena, entao ABRIR a cena no editor cozinha as light
            // functions dela — mesmo efeito que o B4 tem para superficies.
            //
            // So cozinha se o grafo for MESMO deste dominio. O slot de Light
            // Material aceita qualquer `.axemat` (o AssetPicker filtra por TIPO
            // de asset, nao por dominio), e uma luz apontando para um material
            // de SUPERFICIE faria este ponto regravar o `.axeshader` dele com um
            // shader de luz — destruindo o cozido bom e deixando toda malha que
            // usa aquele material com defaults no jogo. Pior: o callback roda a
            // cada load de cena, entao recompilar o material nao salvaria.
            //
            // Compilar continua acontecendo nos dois casos (a luz recebe algum
            // shader, como antes); so o COZIMENTO exige que o dominio bata.
            if (graph.Domain == MaterialDomain::LightFunction)
                BakeShaderToDisk(result, materialFilePath, CookedMaterialDomain::LightFunction);
            else
                AXE_CORE_WARN("CompileLightFunctionFromFile: '{}' nao e um material de "
                    "Light Function - compilado, mas nao cozido.", materialFilePath.string());

            outShader = shader;
            outSamplers = result.SamplerTextures;
            return true;
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("CompileLightFunctionFromFile: erro ao compilar: {}", e.what());
            return false;
        }
    }


    // POSTPROCESS_DOMAIN_V1 — irmao do CompileLightFunctionFromFile, para o
    // callback que o EditorLayer registra. No editor o grafo e a fonte da
    // verdade; no jogo, o `.axeshader` cozido.
    bool MaterialCompiler::CompilePostProcessFromFile(const std::filesystem::path& materialFilePath,
        std::shared_ptr<Shader>& outShader,
        std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers)
    {
        auto graphPath = materialFilePath;
        graphPath.replace_extension(".axegraph");
        if (!std::filesystem::exists(graphPath)) return false;

        std::ifstream file(graphPath);
        if (!file.is_open()) return false;

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);
            MaterialGraph graph;
            graph.Deserialize(j);

            // ── A GUARDA QUE FALTAVA ─────────────────────────────────────────
            //
            // Sai ANTES de compilar se o material nao for deste dominio. O
            // AssetPicker do Inspector filtra por TIPO de asset (`.axemat`), e
            // nao por dominio — entao qualquer material pode ser arrastado para
            // o slot de efeito.
            //
            // Sem esta linha, um material de SUPERFICIE apontado ali seria
            // compilado como post process e desenhado sobre a tela inteira.
            // Devolver false deixa a imagem intacta, que e a resposta correta
            // para "voce escolheu o material errado".
            if (graph.Domain != MaterialDomain::PostProcess)
            {
                AXE_CORE_WARN("CompilePostProcessFromFile: '{}' nao tem Domain = Post Process "
                    "- efeito ignorado.", materialFilePath.string());
                return false;
            }

            auto result = CompilePostProcess(&graph);
            if (!result.Success)
            {
                AXE_CORE_WARN("CompilePostProcessFromFile: {}", result.ErrorMessage);
                return false;
            }

            auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
            if (!shader) return false;

            BakeShaderToDisk(result, materialFilePath, CookedMaterialDomain::PostProcess);

            outShader = shader;
            outSamplers = result.SamplerTextures;
            return true;
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("CompilePostProcessFromFile: erro ao compilar: {}", e.what());
            return false;
        }
    }

    // =========================================================================
    // CompileParticleFunction — domínio "Particle"
    //
    // Gera o vertex shader completo do ParticleRenderer (billboard, rotação,
    // billboarding com câmera right/up) mais um fragment shader gerado a
    // partir do grafo do usuário. O grafo expõe:
    //   Color   (vec3)  → RGB do billboard
    //   Opacity (float) → alpha final multiplicado pelo falloff radial
    //
    // Variáveis disponíveis no grafo (sem vazar GL):
    //   v_UV     (vec2)  — 0..1 no quad
    //   v_Color  (vec4)  — cor interpolada da partícula (start→end)
    //   v_Age01  (float) — 0 no nascimento, 1 na morte
    //   u_Time   (float) — segundos
    // =========================================================================
    CompiledMaterial MaterialCompiler::CompileParticleFunction(MaterialGraph* graph)
    {
        MaterialCompiler compiler(graph);
        CompiledMaterial result;

        Node* outputNode = nullptr;
        for (auto& node : graph->GetNodes())
            if (node->Name == "Material Output") { outputNode = node.get(); break; }

        if (!outputNode)
        {
            result.ErrorMessage = "Material Output node not found";
            return result;
        }

        // ── Passo 1: pré-popula m_NodeSamplers com nomes de partícula ────────
        // CRÍTICO: precisa acontecer ANTES de VisitPin. O VisitNode do
        // Texture Sample lê m_NodeSamplers pra saber o nome do uniform —
        // se ainda não estiver preenchido, usa o fallback "u_AlbedoMap"
        // (nome do domínio Surface) e o shader gerado não declara esse
        // uniform, causando erro de compilação GLSL.
        std::map<std::string, std::shared_ptr<Texture2D>> samplerTextures;
        {
            int slot = 0;
            for (auto& node : graph->GetNodes())
            {
                if (node->Name != "Texture Sample") continue;
                if (!node->Value.TextureVal) continue;
                std::string name = "u_PartTex_" + std::to_string(slot++);
                compiler.m_NodeSamplers[node->ID.Get()] = name;
                samplerTextures[name] = node->Value.TextureVal;
            }
        }

        // ── Passo 2: percorre o grafo (agora usa os nomes corretos) ──────────
        Pin* colorSrcPin = nullptr;
        std::string colorExpr = "v_Color.rgb";

        // Tenta Emissive (pin 4) primeiro — mais natural pra partículas
        if (outputNode->Inputs.size() > 4)
        {
            compiler.VisitPin(&outputNode->Inputs[4]);
            colorSrcPin = compiler.GetSourcePin(&outputNode->Inputs[4]);
        }
        // Fallback: Base Color (pin 0)
        if (!colorSrcPin && outputNode->Inputs.size() > 0)
        {
            compiler.VisitPin(&outputNode->Inputs[0]);
            colorSrcPin = compiler.GetSourcePin(&outputNode->Inputs[0]);
        }
        if (colorSrcPin)
        {
            colorExpr = compiler.GetPinVariable(colorSrcPin->ID);
            if (compiler.GetPinType(colorSrcPin->ID) == PinType::Float)
                colorExpr = "vec3(" + colorExpr + ")";
        }

        // Opacity (pin 5)
        std::string opacityExpr = "v_Color.a";
        if (outputNode->Inputs.size() > 5)
        {
            compiler.VisitPin(&outputNode->Inputs[5]);
            Pin* opSrc = compiler.GetSourcePin(&outputNode->Inputs[5]);
            if (opSrc)
            {
                opacityExpr = compiler.GetPinVariable(opSrc->ID);
                if (compiler.GetPinType(opSrc->ID) != PinType::Float)
                    opacityExpr = "(" + opacityExpr + ").r";
            }
        }

        // ── Passo 3: filtra samplerTextures pra só os nós visitados ──────────
        // (nós fora do caminho do grafo não precisam de uniform)
        {
            std::map<std::string, std::shared_ptr<Texture2D>> usedSamplers;
            for (auto& node : graph->GetNodes())
            {
                if (node->Name != "Texture Sample") continue;
                if (!compiler.m_VisitedNodes.count(node->ID.Get())) continue;
                auto it = compiler.m_NodeSamplers.find(node->ID.Get());
                if (it != compiler.m_NodeSamplers.end() && samplerTextures.count(it->second))
                    usedSamplers[it->second] = samplerTextures[it->second];
            }
            samplerTextures = std::move(usedSamplers);
        }

        // Vertex shader — mesmo layout do ParticleRenderer hardcoded,
        // mas agora como string compilável pelo MaterialCompiler.
        // Passa v_UV, v_Color e v_Age01 pro fragment.
        result.VertexShader = R"(
#version 460 core
layout(location = 0) in vec3  a_Center;
layout(location = 1) in vec2  a_Corner;
layout(location = 2) in vec4  a_Color;
layout(location = 3) in float a_Size;
layout(location = 4) in float a_Rotation;
layout(location = 5) in float a_Age01;
layout(location = 6) in vec3  a_Velocity;

uniform mat4  u_ViewProjection;
uniform vec3  u_CameraRight;
uniform vec3  u_CameraUp;
uniform float u_StretchAmount;

out vec2  v_UV;
out vec4  v_Color;
out float v_Age01;

void main()
{
    vec3 worldPos;
    float speed = length(a_Velocity);
    if (u_StretchAmount > 0.0 && speed > 0.001)
    {
        vec3 stretchDir    = normalize(a_Velocity);
        float stretchFactor = 1.0 + u_StretchAmount * speed;
        worldPos = a_Center
            + u_CameraRight * a_Corner.x * a_Size
            + stretchDir    * a_Corner.y * a_Size * stretchFactor;
    }
    else
    {
        float c = cos(a_Rotation);
        float s = sin(a_Rotation);
        vec2  rc = vec2(a_Corner.x * c - a_Corner.y * s,
                        a_Corner.x * s + a_Corner.y * c);
        worldPos = a_Center
            + (u_CameraRight * rc.x + u_CameraUp * rc.y) * a_Size;
    }

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
    v_UV     = a_Corner + vec2(0.5);
    v_Color  = a_Color;
    v_Age01  = a_Age01;
}
)";

        // Fragment shader gerado do grafo
        std::ostringstream fs;
        fs << "#version 460 core\n";
        fs << "in vec2  v_UV;\n";
        fs << "in vec4  v_Color;\n";
        fs << "in float v_Age01;\n\n";
        fs << "uniform float u_Time;\n";
        fs << "uniform int   u_FlipbookCols;\n";
        fs << "uniform int   u_FlipbookRows;\n";
        fs << "uniform float u_FlipbookCycles;\n\n";
        fs << "layout(location = 0) out vec4 FragColor;\n\n";
        for (auto& [name, tex] : samplerTextures)
            fs << "uniform sampler2D " << name << ";\n";
        // CUSTOM_NODE_V1 — as funcoes dos nodes Custom entram AQUI, entre as
        // declaracoes e o main(). GLSL exige a funcao declarada antes do uso, e
        // este e o unico ponto do shader gerado onde isso e verdade.
        fs << compiler.m_CustomFunctions;
        fs << "\nvoid main()\n{\n";
        fs << "    vec3 v_FragPos    = vec3(0.0);\n";
        fs << "    vec3 v_Normal     = vec3(0.0, 0.0, 1.0);\n";
        fs << "    vec3 N            = v_Normal;\n";
        fs << "    vec3 v_Tangent    = vec3(1.0, 0.0, 0.0);\n";
        fs << "    vec3 v_Bitangent  = vec3(0.0, 1.0, 0.0);\n";
        fs << "    float v_Age01_raw = v_Age01;\n";
        // Flipbook UV — degenera pra v_UV quando Cols=Rows=1
        fs << "    float _fb_total  = float(u_FlipbookCols * u_FlipbookRows);\n";
        fs << "    float _fb_frame  = mod(floor(v_Age01 * u_FlipbookCycles * _fb_total), _fb_total);\n";
        fs << "    float _fb_col    = mod(_fb_frame, float(u_FlipbookCols));\n";
        fs << "    float _fb_row    = floor(_fb_frame / float(u_FlipbookCols));\n";
        fs << "    vec2  _fb_cell   = vec2(1.0 / float(u_FlipbookCols), 1.0 / float(u_FlipbookRows));\n";
        fs << "    vec2 v_TexCoord  = v_UV * _fb_cell\n";
        fs << "        + vec2(_fb_col, float(u_FlipbookRows - 1) - _fb_row) * _fb_cell;\n\n";
        fs << compiler.m_FragmentCode;
        // Falloff radial — ocorre SEMPRE pra partícula parecer
        // circular, independentemente do material aplicado.
        fs << "\n    float _d       = length(v_UV - vec2(0.5));\n";
        fs << "    float _falloff = smoothstep(0.5, 0.0, _d);\n";
        fs << "    vec3  finalColor   = " << colorExpr << ";\n";
        fs << "    float finalOpacity = (" << opacityExpr << ") * _falloff;\n";
        fs << "    FragColor = vec4(finalColor, finalOpacity);\n";
        fs << "}\n";

        result.FragmentShader = fs.str();
        result.SamplerTextures = samplerTextures;
        CollectSamplerUUIDs(compiler, graph, samplerTextures, result);   // PKG9
        result.Success = true;
        return result;
    }

    bool MaterialCompiler::CompileParticleFunctionFromFile(const std::filesystem::path& materialFilePath,
        std::shared_ptr<Shader>& outShader,
        std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers)
    {
        auto graphPath = materialFilePath;
        graphPath.replace_extension(".axegraph");
        if (!std::filesystem::exists(graphPath))
        {
            AXE_CORE_WARN("CompileParticleFunctionFromFile: grafo não encontrado em '{}'", graphPath.string());
            return false;
        }

        std::ifstream file(graphPath);
        if (!file.is_open())
        {
            AXE_CORE_WARN("CompileParticleFunctionFromFile: falha ao abrir '{}'", graphPath.string());
            return false;
        }

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);
            MaterialGraph graph;
            graph.Deserialize(j);

            auto result = CompileParticleFunction(&graph);
            if (!result.Success)
            {
                AXE_CORE_WARN("CompileParticleFunctionFromFile: {}", result.ErrorMessage);
                return false;
            }

            auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
            if (!shader) return false;

            // PKG9 — mesma ideia do Light Function: cozinha no mesmo ponto em
            // que o editor ja resolve o material do emitter, e pela mesma razao
            // so quando o dominio do grafo bate (ver a nota longa la em cima).
            if (graph.Domain == MaterialDomain::Particle)
                BakeShaderToDisk(result, materialFilePath, CookedMaterialDomain::Particle);
            else
                AXE_CORE_WARN("CompileParticleFunctionFromFile: '{}' nao e um material de "
                    "Particle - compilado, mas nao cozido.", materialFilePath.string());

            outShader = shader;
            outSamplers = result.SamplerTextures;
            return true;
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("CompileParticleFunctionFromFile: erro: {}", e.what());
            return false;
        }
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

            // ── SRGB_TEXTURES_V1 — sRGB -> LINEAR ────────────────────────────
            //
            // Um PNG/JPG de cor esta codificado em sRGB. Ate aqui a engine
            // sampleava esse valor CODIFICADO e o usava como se fosse linear;
            // no fim do frame o post-process aplica pow(1/2.2) e CODIFICA DE
            // NOVO. O resultado e a imagem lavada: meio-tom alto demais,
            // sombra sem profundidade, e cor que nunca bate com o que foi
            // pintado no Substance/Photoshop.
            //
            // A conversao acontece AQUI, no GLSL, e nao no formato da textura
            // (GL_SRGB8), de proposito:
            //
            //   - a mesma textura pode ser cor num material e dado em outro; o
            //     formato e por OBJETO de textura (e o cache e por caminho),
            //     o GLSL e por MATERIAL. So o segundo consegue estar certo nos
            //     dois casos ao mesmo tempo.
            //   - o GLSL ja e cozido no `.axeshader`: o jogo herda a correcao
            //     sem uma linha de mudanca no formato nem no runtime.
            //
            // Custo: um pow por sample de cor. O alpha NAO entra — alpha e
            // sempre dado (opacidade, mascara), nunca cor.
            if (m_SRGBSamplers.count(node->ID.Get()))
            {
                code << "\n    " << var << ".rgb = pow(max(" << var
                    << ".rgb, vec3(0.0)), vec3(2.2));";
            }
        }

        // -----------------------------------------------------------------
        // POSTPROCESS_DOMAIN_V1 — Screen UV
        //
        // Devolve a UV de tela (que num quad fullscreen E o v_TexCoord) e o
        // tamanho de UM PIXEL em UV. O segundo e o que torna possivel escrever
        // qualquer efeito que precise do vizinho — blur, sharpen, outline,
        // scanline — sem o autor ter que saber a resolucao.
        //
        // Fora do dominio Post Process, u_ScreenSize nao existe; por isso o
        // fallback constante em vez de referenciar a uniform.
        // -----------------------------------------------------------------
        else if (node->Name == "Screen UV")
        {
            std::string uvVar = MakeVar("suv");
            std::string pxVar = MakeVar("spx");

            RegisterPin(node->Outputs[0].ID, uvVar, PinType::Vec2);
            RegisterPin(node->Outputs[1].ID, pxVar, PinType::Vec2);

            code << "vec2 " << uvVar << " = v_TexCoord;";
            code << "\n    vec2 " << pxVar << " = "
                << (m_PostProcessTarget ? "1.0 / max(u_ScreenSize, vec2(1.0))" : "vec2(0.0)") << ";";
        }

        // -----------------------------------------------------------------
        // POSTPROCESS_SKY_V1 — Scene Is Background
        //
        // 1.0 onde NAO ha geometria (ceu, ou o vazio da cena), 0.0 onde ha.
        //
        // Da para chegar nisso a mao pelo comprimento do Scene Normal — mas
        // "o normal tem comprimento zero" e conhecimento de dentro do
        // G-Buffer, e o autor de um material nao deveria precisar saber que o
        // fundo e representado assim. Um node explicito e a diferenca entre a
        // capacidade existir e ela ser descoberta.
        // -----------------------------------------------------------------
        else if (node->Name == "Scene Is Background")
        {
            std::string var = MakeVar("sbg");

            std::string uv = "v_TexCoord";
            if (!node->Inputs.empty())
            {
                Pin* uvSrc = GetSourcePin(&node->Inputs[0]);
                if (uvSrc)
                    uv = AdaptToType(GetPinVariable(uvSrc->ID),
                        GetPinType(uvSrc->ID), PinType::Vec2);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);

            if (m_PostProcessTarget)
                code << "float " << var << " = step(length(texture(u_SceneNormal, "
                << uv << ").rgb), 0.1);";
            else
                code << "float " << var << " = 0.0;  // so existe em Post Process";
        }

        // -----------------------------------------------------------------
        // POSTPROCESS_SKY_V1 — Screen Ray Direction
        //
        // Para onde ESTE pixel esta olhando, em direcao de mundo normalizada.
        //
        // E o que torna o ceu autoravel no grafo. Com o raio em maos, `.y` e a
        // elevacao (gradiente horizonte-zenite, banda por altura), o produto
        // escalar com u_SunDirection e o brilho em torno do sol, e `.xz` da a
        // coordenada para nuvens procedurais.
        //
        // Reconstruido do UV pela inversa da view-projection: leva o pixel ao
        // plano FAR em espaco de recorte e o traz de volta ao mundo. A divisao
        // por w e obrigatoria — sem ela a direcao fica errada fora do centro
        // da tela, que e justamente onde o ceu ocupa mais espaco.
        // -----------------------------------------------------------------
        else if (node->Name == "Screen Ray Direction")
        {
            std::string var = MakeVar("sray");

            std::string uv = "v_TexCoord";
            if (!node->Inputs.empty())
            {
                Pin* uvSrc = GetSourcePin(&node->Inputs[0]);
                if (uvSrc)
                    uv = AdaptToType(GetPinVariable(uvSrc->ID),
                        GetPinType(uvSrc->ID), PinType::Vec2);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);

            if (m_PostProcessTarget)
            {
                std::string tmp = MakeVar("sray_h");
                code << "vec4 " << tmp << " = u_InvViewProjection * vec4("
                    << uv << " * 2.0 - 1.0, 1.0, 1.0);";
                code << "\n    vec3 " << var << " = normalize("
                    << tmp << ".xyz / " << tmp << ".w - u_CameraPosition);";
            }
            else
            {
                code << "vec3 " << var << " = vec3(0.0, 1.0, 0.0);  // so existe em Post Process";
            }
        }

        // -----------------------------------------------------------------
        // POSTPROCESS_SKY_V1 — Sun
        //
        // A luz direcional da cena.
        //
        // "Direction" aponta PARA ONDE A LUZ VAI — a mesma convencao do
        // u_LightDirection do lighting pass, para nao existirem dois sentidos
        // de "direcao do sol" na engine.
        //
        // "To Sun" e o mesmo vetor NEGADO, e existe como saida propria porque
        // errar esse sinal e o engano mais comum de quem escreve ceu: o brilho
        // em volta do sol vira um brilho no lado oposto, e o bug parece de
        // matematica quando e so de convencao.
        // -----------------------------------------------------------------
        else if (node->Name == "Sun")
        {
            std::string dirVar = MakeVar("sundir");
            std::string toVar = MakeVar("tosun");
            std::string colVar = MakeVar("suncol");

            RegisterPin(node->Outputs[0].ID, dirVar, PinType::Vec3);
            RegisterPin(node->Outputs[1].ID, toVar, PinType::Vec3);
            RegisterPin(node->Outputs[2].ID, colVar, PinType::Vec3);
            // A intensidade tambem vira VARIAVEL, e nao a uniform direto: fora
            // do dominio Post Process a u_SunIntensity nao e declarada, e um
            // grafo que usasse esta saida num material de superficie quebraria
            // a compilacao com "undeclared identifier" — exatamente o bug do
            // u_SceneColor de duas rodadas atras.
            std::string intVar = MakeVar("sunint");
            RegisterPin(node->Outputs[3].ID, intVar, PinType::Float);

            if (m_PostProcessTarget)
            {
                code << "vec3 " << dirVar << " = normalize(u_SunDirection);";
                code << "\n    vec3 " << toVar << " = -" << dirVar << ";";
                code << "\n    vec3 " << colVar << " = u_SunColor;";
                code << "\n    float " << intVar << " = u_SunIntensity;";
            }
            else
            {
                code << "vec3 " << dirVar << " = vec3(0.0, -1.0, 0.0);";
                code << "\n    vec3 " << toVar << " = vec3(0.0, 1.0, 0.0);";
                code << "\n    vec3 " << colVar << " = vec3(1.0);";
                code << "\n    float " << intVar << " = 1.0;";
            }
        }

        // -----------------------------------------------------------------
        // POSTPROCESS_GBUFFER_V1 — Scene Depth
        //
        // Distancia LINEAR da camera ate a superficie, em unidades de mundo.
        //
        // Nao e o depth buffer cru, de proposito. O valor do depth buffer e
        // nao-linear e so significa alguma coisa junto com o near/far da
        // camera — comparar dois vizinhos ali daria um numero que muda de
        // sentido conforme a profundidade. Distancia em metros e comparavel,
        // e e o que uma deteccao de borda ou um fog por distancia querem.
        //
        // Sai do attachment de POSICAO, que ja existe no G-Buffer: nenhuma
        // uniform de near/far, nenhuma matriz inversa.
        // -----------------------------------------------------------------
        else if (node->Name == "Scene Depth")
        {
            std::string var = MakeVar("sdepth");

            std::string uv = "v_TexCoord";
            if (!node->Inputs.empty())
            {
                Pin* uvSrc = GetSourcePin(&node->Inputs[0]);
                if (uvSrc)
                    uv = AdaptToType(GetPinVariable(uvSrc->ID),
                        GetPinType(uvSrc->ID), PinType::Vec2);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);

            if (m_PostProcessTarget)
                code << "float " << var << " = length(texture(u_ScenePosition, "
                << uv << ").rgb - u_CameraPosition);";
            else
                code << "float " << var << " = 0.0;  // Scene Depth so existe em Post Process";
        }

        // -----------------------------------------------------------------
        // POSTPROCESS_GBUFFER_V1 — Scene Normal
        //
        // Normal do mundo. Pixel de FUNDO devolve (0,0,0) — comprimento zero,
        // e nao um vetor valido. Isso e util e proposital: comparar o
        // comprimento e como um material distingue "nao ha geometria aqui" de
        // "ha geometria virada para outro lado" — que e a diferenca entre
        // desenhar a silhueta e desenhar uma quina.
        // -----------------------------------------------------------------
        else if (node->Name == "Scene Normal")
        {
            std::string var = MakeVar("snormal");

            std::string uv = "v_TexCoord";
            if (!node->Inputs.empty())
            {
                Pin* uvSrc = GetSourcePin(&node->Inputs[0]);
                if (uvSrc)
                    uv = AdaptToType(GetPinVariable(uvSrc->ID),
                        GetPinType(uvSrc->ID), PinType::Vec2);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);

            if (m_PostProcessTarget)
                code << "vec3 " << var << " = texture(u_SceneNormal, " << uv << ").rgb;";
            else
                code << "vec3 " << var << " = vec3(0.0);  // so existe em Post Process";
        }

        // -----------------------------------------------------------------
        // POSTPROCESS_GBUFFER_V1 — Scene Shading Model
        //
        // 0 = DefaultLit, 1 = Unlit, 2 = Toon. E o que permite um efeito de
        // tela cheia agir SO sobre um tipo de material — desenhar contorno
        // apenas nos personagens toon e deixar o cenario PBR intacto, sem
        // mascara, sem stencil e sem um segundo passe.
        //
        // Decodifica o mesmo canal e com o mesmo fator do lighting pass (ver
        // ShadingModelID em axe/material/material_cooked.hpp). O +0.5 e o
        // mesmo de la, e pela mesma razao: sem ele um 2 que voltou como
        // 1.9999 viraria 1.
        // -----------------------------------------------------------------
        else if (node->Name == "Scene Shading Model")
        {
            std::string var = MakeVar("sshading");

            std::string uv = "v_TexCoord";
            if (!node->Inputs.empty())
            {
                Pin* uvSrc = GetSourcePin(&node->Inputs[0]);
                if (uvSrc)
                    uv = AdaptToType(GetPinVariable(uvSrc->ID),
                        GetPinType(uvSrc->ID), PinType::Vec2);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);

            if (m_PostProcessTarget)
                code << "float " << var << " = floor(texture(u_ScenePBR, "
                << uv << ").b * 255.0 + 0.5);";
            else
                code << "float " << var << " = 0.0;  // so existe em Post Process";
        }

        // -----------------------------------------------------------------
        // POSTPROCESS_DOMAIN_V1 — Scene Color
        //
        // A imagem da cena no pixel dado. UV desconectada = o proprio pixel.
        //
        // Fora do dominio Post Process compila para PRETO, e nao some do menu:
        // um node que desaparece esconde do usuario que ele existe; um node que
        // compila para um valor definido apenas nao faz nada — e isso da para
        // ver na tela e entender.
        // -----------------------------------------------------------------
        else if (node->Name == "Scene Color")
        {
            std::string var = MakeVar("scene");
            std::string uv = "v_TexCoord";
            if (!node->Inputs.empty())
            {
                Pin* uvSrc = GetSourcePin(&node->Inputs[0]);
                if (uvSrc)
                    uv = AdaptToType(GetPinVariable(uvSrc->ID),
                        GetPinType(uvSrc->ID), PinType::Vec2);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);

            // A condicao e a FLAG DO COMPILADOR, nao o dominio do grafo: o
            // mesmo grafo de post process tambem passa pelo compilador de
            // Surface (preview e material da cena), e la u_SceneColor nao
            // existe. Ver a nota em m_PostProcessTarget.
            if (m_PostProcessTarget)
                code << "vec3 " << var << " = texture(u_SceneColor, " << uv << ").rgb;";
            else
                code << "vec3 " << var << " = vec3(0.0);  // Scene Color so existe em Post Process";
        }

        // -----------------------------------------------------------------
        // CUSTOM_NODE_V1 — Custom (GLSL escrito a mao)
        //
        // Gera DUAS coisas: uma funcao, que vai para m_CustomFunctions e sera
        // inserida antes do main(), e a chamada dela, que e a linha deste node
        // no corpo do shader.
        //
        // O nome da funcao leva o ID do node justamente para que dois Custom no
        // mesmo grafo (ou o mesmo Custom em fs e gs) nunca colidam.
        // -----------------------------------------------------------------
        else if (node->Name == "Custom")
        {
            const std::string fnName = "axeCustom_" + std::to_string(node->ID.Get());
            const std::string var = MakeVar("custom");
            const std::string retType = GetGLSLType(node->CustomOutputType);

            // ── Assinatura ───────────────────────────────────────────────
            //
            // Os parametros levam o NOME QUE O USUARIO DEU ao pin. E o ponto
            // do node: se ele batizou a entrada de "Tint", o codigo dele fala
            // de `Tint`, e nao de `in0`.
            std::string signature = retType + " " + fnName + "(";
            std::string callArgs;

            for (size_t i = 0; i < node->Inputs.size(); i++)
            {
                Pin& pin = node->Inputs[i];
                const std::string pType = GetGLSLType(pin.Type);

                // Nome do parametro saneado: o campo do painel aceita qualquer
                // texto, e "Base Color" nao e identificador GLSL valido. Sem
                // isto, um espaco no nome do pin quebraria a funcao inteira com
                // um erro que nao aponta para o campo que o causou.
                std::string safe;
                for (char c : pin.Name)
                {
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_') safe += c;
                    else safe += '_';
                }
                if (safe.empty() || (safe[0] >= '0' && safe[0] <= '9'))
                    safe = "in_" + safe;

                if (i > 0) { signature += ", "; callArgs += ", "; }
                signature += pType + " " + safe;

                // Argumento da chamada: valor do pin ligado, adaptado ao tipo
                // declarado; ou o valor digitado no proprio pin, quando solto.
                Pin* src = GetSourcePin(&pin);
                if (src)
                {
                    callArgs += AdaptToType(GetPinVariable(src->ID),
                        GetPinType(src->ID), pin.Type);
                }
                else
                {
                    // Pin desconectado — mesma semantica dos outros nodes:
                    // o valor digitado nele. Para vetores, o escalar preenche
                    // todos os componentes (vec3(0.5) e o que se espera de um
                    // "meio" num pin de cor).
                    std::string lit = std::to_string(pin.DefaultFloat);
                    callArgs += (pin.Type == PinType::Float)
                        ? lit
                        : (pType + "(" + lit + ")");
                }
            }
            signature += ")";

            // ── Corpo ────────────────────────────────────────────────────
            //
            // Vai como o usuario escreveu, sem transformacao nenhuma. Se ele
            // esqueceu o `return`, o erro aparece no log de shader do proprio
            // Material Editor — que ja existe (material_shader_log.cpp) e e
            // exatamente o lugar certo para ele aparecer.
            m_CustomFunctions += "// Custom node " + std::to_string(node->ID.Get()) + "\n";
            m_CustomFunctions += signature + "\n{\n";
            m_CustomFunctions += node->CustomCode;
            m_CustomFunctions += "\n}\n\n";

            RegisterPin(node->Outputs[0].ID, var, node->CustomOutputType);
            code << retType << " " << var << " = " << fnName << "(" << callArgs << ");";
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
        // Particle Age — idade normalizada da partícula (v_Age01).
        // 0.0 = nasceu agora, 1.0 = morreu. Só tem valores reais no
        // domínio Particle; em outros domínios o prólogo fixa 0.0.
        // -----------------------------------------------------------------
        else if (node->Name == "Particle Age")
        {
            RegisterPin(node->Outputs[0].ID, "v_Age01", PinType::Float);
        }

        // -----------------------------------------------------------------
        // Particle Color — cor interpolada da partícula (v_Color, vec4).
        // RGB = cor atual (ColorStart→ColorEnd), Alpha = opacidade.
        // Indispensável pra combinar textura com as cores do emitter.
        // -----------------------------------------------------------------
        else if (node->Name == "Particle Color")
        {
            if (node->Outputs.size() > 0)
                RegisterPin(node->Outputs[0].ID, "v_Color", PinType::Vec4);
            if (node->Outputs.size() > 1)
                RegisterPin(node->Outputs[1].ID, "v_Color.rgb", PinType::Vec3);
            if (node->Outputs.size() > 2)
                RegisterPin(node->Outputs[2].ID, "v_Color.a", PinType::Float);
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
                    {
                        // Reroute é transparente: segue pro input dele até a
                        // fonte real (encadeamento de reroutes inclusive).
                        if (node->Name == "Reroute" && !node->Inputs.empty())
                            return GetSourceNode(&node->Inputs[0]);
                        return node.get();
                    }
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
                    {
                        // Mesmo pass-through: devolve o pin da fonte REAL,
                        // pulando o(s) reroute(s) no caminho.
                        if (node->Name == "Reroute" && !node->Inputs.empty())
                            return GetSourcePin(&node->Inputs[0]);
                        return &output;
                    }
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

    // ── CUSTOM_NODE_V1 — adaptacao de tipo ───────────────────────────────────
    //
    // Ver a nota na declaracao (material_compiler.hpp). Regras escolhidas para
    // ser o que o autor QUIS dizer, e nao o que o GLSL exigiria:
    //
    //   escalar -> vetor : replica em todos os componentes  (0.5 -> vec3(0.5))
    //   vetor  -> escalar: pega .x
    //   vec4   -> vec3   : .rgb  (descarta alpha)
    //   vec3   -> vec4   : alpha 1.0 (opaco), que e o default util
    //
    // Tipo igual devolve a expressao intacta — o caso comum nao paga nada.
    std::string MaterialCompiler::AdaptToType(const std::string& expr,
        PinType from, PinType to)
    {
        if (from == to) return expr;

        auto comps = [](PinType t) -> int
            {
                switch (t)
                {
                case PinType::Float: return 1;
                case PinType::Vec2:  return 2;
                case PinType::Vec3:  return 3;
                case PinType::Vec4:  return 4;
                default:             return 1;
                }
            };

        const int f = comps(from);
        const int t = comps(to);

        if (f == t) return expr;

        if (f == 1)
        {
            // Escalar para vetor: vec3(x) preenche os tres.
            return GetGLSLType(to) + "(" + expr + ")";
        }

        if (t == 1) return "(" + expr + ").x";
        if (t < f)
        {
            // Encolhe por swizzle.
            static const char* kSwz[5] = { "", ".x", ".xy", ".xyz", "" };
            return "(" + expr + ")" + kSwz[t];
        }

        // Cresce: completa com 0 e fecha em 1.0 no alpha, que e o unico
        // preenchimento que nao muda o significado de uma cor.
        if (f == 2 && t == 3) return "vec3(" + expr + ", 0.0)";
        if (f == 2 && t == 4) return "vec4(" + expr + ", 0.0, 1.0)";
        if (f == 3 && t == 4) return "vec4(" + expr + ", 1.0)";

        return expr;
    }

    // ── B4 — cozimento do shader (.axeshader) ────────────────────────────────
    //
    // O formato e o Save pertencem ao RUNTIME (CookedMaterial, em
    // src/axe/material/), pela mesma razao do skeletal_cooked: quem le e dono
    // do formato. Aqui so se transfere o resultado da compilacao para a
    // estrutura cozida — a dependencia aponta editor -> runtime.
    bool MaterialCompiler::BakeToDisk(const CompiledMaterial& result,
        const std::filesystem::path& materialFilePath,
        const glm::vec3& bakedEmissive)
    {
        if (!result.Success)
            return false;

        CookedMaterialData data;
        data.VertexShader = result.VertexShader;
        data.FragmentShader = result.FragmentShader;
        data.GeometryFragShader = result.GeometryFragShader;
        data.SamplerTextureUUIDs = result.SamplerTextureUUIDs;
        data.AlbedoSamplerName = result.AlbedoSamplerName;
        data.NormalSamplerName = result.NormalSamplerName;
        data.IsTransparent = result.IsTransparent;
        data.IsMasked = result.IsMasked;
        data.AlphaCutoff = result.AlphaCutoff;
        data.BakedEmissive = bakedEmissive;

        return CookedMaterial::Save(CookedMaterial::PathFor(materialFilePath), data);
    }

    // PKG9 — cozimento de Light Function e Particle.
    //
    // Mais simples que o de superficie porque estes dominios nao produzem
    // Material: nao ha albedo/normal para apontar, nem geometry shader, nem
    // BakedEmissive (o GI le o emissive do material de SUPERFICIE, e nenhum
    // destes dois e superficie). Sobra o que o consumidor guarda de verdade:
    // shader + samplers.
    bool MaterialCompiler::BakeShaderToDisk(const CompiledMaterial& result,
        const std::filesystem::path& materialFilePath,
        CookedMaterialDomain domain)
    {
        if (!result.Success)
            return false;

        CookedMaterialData data;
        data.Domain = domain;
        data.VertexShader = result.VertexShader;
        data.FragmentShader = result.FragmentShader;
        data.SamplerTextureUUIDs = result.SamplerTextureUUIDs;
        data.IsTransparent = result.IsTransparent;

        return CookedMaterial::Save(CookedMaterial::PathFor(materialFilePath), data);
    }
}