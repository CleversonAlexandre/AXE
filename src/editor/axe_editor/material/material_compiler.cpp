#include "material_compiler.hpp"
#include "axe/material/light_material_evaluator.hpp"
#include "axe/material/material_cooked.hpp"   // B4 — .axeshader
#include "axe/log/log.hpp"
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <algorithm>
#include "axe/asset/asset_database.hpp"   // MATFUNC_V1

#include "axe/graphics/texture.hpp"
#include "axe/graphics/shader.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{
    // ═════════════════════════════════════════════════════════════════════════
    //  SCENE_DEPTH_SURFACE_V1 — os nodes de TELA num material de superficie
    //
    //  ── O PROBLEMA ─────────────────────────────────────────────────────────
    //
    //  `Scene Depth` e `Screen UV` so funcionavam no dominio Post Process.
    //  Fora dele emitiam constante (0.0 / vec2(0.0)) — o node aparecia no
    //  menu, ligava, compilava, e nao fazia nada.
    //
    //  E exatamente o que falta para agua estilizada: a cor pela profundidade
    //  da lamina e a espuma na linha da praia sao, as duas, a distancia entre
    //  a superficie da agua e o fundo atras dela.
    //
    //  ── POR QUE HELPER, E NAO UM `if` NA EMISSAO DO NODE ───────────────────
    //
    //  O codigo dos nodes e gerado UMA VEZ (`m_FragmentCode`) e colado em SEIS
    //  shaders — inclusive nos DOIS do dominio Surface: o forward (`fs`) e o
    //  do G-Buffer (`gs`). Um `if` na emissao teria de escolher um texto so
    //  para os dois.
    //
    //  E os dois PRECISAM diferir: no caminho opaco o shader ESCREVE no
    //  attachment de posicao do G-Buffer, e ler de uma textura anexada ao FBO
    //  corrente e comportamento indefinido em GL. O opaco tem de receber zero.
    //
    //  Entao o texto gerado e sempre o mesmo — `axeSceneDepth(axeScreenUV())` —
    //  e o que muda e a DEFINICAO das funcoes, colada em cada shader.
    // ═════════════════════════════════════════════════════════════════════════
    namespace
    {
        // =====================================================================
        //  NOISE_SMOOTH_V1 — ruido CONTINUO
        //
        //  O node "Noise" emitia a receita de uma linha que circula em todo
        //  tutorial de shader:
        //
        //      fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453)
        //
        //  Isso NAO e ruido no sentido util: e um HASH. Dois pixels vizinhos
        //  recebem valores completamente independentes, entao o que aparece na
        //  tela e CHUVISCO DE TELEVISAO. Nao ha escala espacial, nao ha nada
        //  para ajustar, e nenhum valor que o autor digite muda esse fato — o
        //  defeito esta no node, nao no grafo de quem o usou.
        //
        //  O que faltava e INTERPOLACAO. O ruido de valor amostra o hash nos
        //  quatro cantos de uma celula da grade e mistura entre eles com a
        //  curva de Hermite (3t^2 - 2t^3), cuja derivada e zero nas bordas —
        //  e por isso que a celula vizinha continua de forma suave em vez de
        //  a grade aparecer. axeFbm empilha oitavas: cada uma com o dobro da
        //  frequencia e metade da amplitude, que e o que da ondulacao em vez
        //  de bolhas do mesmo tamanho.
        //
        //  Sao funcoes PURAS: nao leem uniform nem sampler. Por isso valem
        //  para os seis shaders sem variante por dominio, ao contrario de
        //  SceneHelpersGLSL logo abaixo.
        // =====================================================================
        std::string NoiseHelpersGLSL()
        {
            return
                "// NOISE_SMOOTH_V1\n"
                "float axeHash21(vec2 p) {\n"
                "    p = fract(p * vec2(123.34, 456.21));\n"
                "    p += dot(p, p + 45.32);\n"
                "    return fract(p.x * p.y);\n"
                "}\n"
                "float axeValueNoise(vec2 p) {\n"
                "    vec2 i = floor(p);\n"
                "    vec2 f = fract(p);\n"
                "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
                "    float a = axeHash21(i);\n"
                "    float b = axeHash21(i + vec2(1.0, 0.0));\n"
                "    float c = axeHash21(i + vec2(0.0, 1.0));\n"
                "    float d = axeHash21(i + vec2(1.0, 1.0));\n"
                "    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n"
                "}\n"
                "float axeFbm(vec2 p, int octaves) {\n"
                "    float sum = 0.0;\n"
                "    float amp = 0.5;\n"
                "    float norm = 0.0;\n"
                "    for (int o = 0; o < 8; ++o) {\n"
                "        if (o >= octaves) break;\n"
                "        sum  += amp * axeValueNoise(p);\n"
                "        norm += amp;\n"
                "        p    *= 2.02;\n"
                "        amp  *= 0.5;\n"
                "    }\n"
                "    return sum / max(norm, 0.0001);\n"
                "}\n\n";
        }

        enum class SceneHelperMode
        {
            Stub,          // dominio sem acesso a tela: devolve neutro
            Surface,       // forward de superficie: le o G-Buffer ja resolvido
            PostProcess,   // quad de tela cheia: v_TexCoord JA e a UV da tela
        };

        std::string SceneHelpersGLSL(SceneHelperMode mode)
        {
            switch (mode)
            {
            case SceneHelperMode::PostProcess:
                // u_ScenePosition, u_ScreenSize e u_CameraPosition ja estao
                // declarados no cabecalho deste dominio — redeclarar e erro
                // de compilacao em GLSL.
                return
                    "// SCENE_DEPTH_SURFACE_V1\n"
                    "vec2  axeScreenUV()    { return v_TexCoord; }\n"
                    "vec2  axeScreenTexel() { return 1.0 / max(u_ScreenSize, vec2(1.0)); }\n"
                    "float axeSceneDepth(vec2 uv) {\n"
                    "    return length(texture(u_ScenePosition, uv).rgb - u_CameraPosition);\n"
                    "}\n"
                    // PRIMITIVES_V1 — a posicao de mundo do que esta atras, crua. Este
                    // dominio nao trata o fundo, pela mesma razao ja escrita acima:
                    // aqui existe o node `Scene Is Background` para decidir isso.
                    "vec3 axeSceneWorldPos(vec2 uv) {\n"
                    "    return texture(u_ScenePosition, uv).rgb;\n"
                    "}\n"
                    // SCENE_BG_V1 — aqui o teste continua sendo o da NORMAL,
                    // que e o que este dominio ja usava. Trocar mudaria o
                    // resultado de material de post process ja escrito.
                    "float axeSceneIsBackground(vec2 uv) {\n"
                    "    return step(length(texture(u_SceneNormal, uv).rgb), 0.1);\n"
                    "}\n"
                    // SCENE_HEIGHT_V1 — o mapa de topo so e ligado no passe
                    // de superficie. Aqui as funcoes existem para o mesmo grafo
                    // compilar nos dois dominios, devolvendo "nada aqui".
                    "float axeSceneHeight(vec3 worldPos) { return -1e9; }\n"
                    "float axeSceneDistance(vec3 worldPos) { return 1e6; }\n"
                    "float axeSceneNearestBase(vec3 worldPos) { return 1e9; }\n"
                    "float axeSceneNearestTop(vec3 worldPos) { return -1e9; }\n\n";

            case SceneHelperMode::Surface:
                // u_CameraPosition ja vem do cabecalho do Surface; os outros
                // tres sao daqui. Declarados SEMPRE, mesmo que o grafo nao use
                // node de tela nenhum: uniform nao referenciada e removida pelo
                // proprio compilador de GLSL, e a alternativa seria varrer o
                // grafo antes de montar o cabecalho.
                //
                // u_HasSceneDepth e o interruptor: este mesmo shader roda no
                // PREVIEW do Material Editor e no passe opaco, onde nao ha
                // textura ligada. Sem ele, esses caminhos amostrariam a unidade
                // de textura 9 com o que estivesse la.
                //
                // ── O FUNDO DEVOLVE INFINITO, E ISSO NAO E DETALHE ─────────
                //
                // Onde nao ha geometria opaca atras, o attachment de posicao
                // esta no valor de limpeza: (0,0,0). A conta ingenua daria
                // `length(vec3(0) - u_CameraPosition)` — a distancia ate a
                // ORIGEM DO MUNDO, um numero pequeno e sem relacao nenhuma com
                // a cena.
                //
                // Numa agua isso e visivel e absurdo: MAR ABERTO (nada atras)
                // leria profundidade rasa e ficaria COBERTO DE ESPUMA, que e
                // exatamente o oposto do certo. "Nada atras" significa
                // profundidade infinita.
                //
                // O dominio Post Process NAO recebe este tratamento de
                // proposito: la ja existe o node `Scene Is Background` para
                // decidir isso explicitamente, e mudar o valor mudaria
                // materiais de post process ja escritos.
                return
                    "// SCENE_DEPTH_SURFACE_V1\n"
                    "uniform sampler2D u_ScenePosition;\n"
                    "uniform vec2      u_ScreenSize;\n"
                    "uniform int       u_HasSceneDepth;\n"
                    "vec2  axeScreenUV()    { return gl_FragCoord.xy / max(u_ScreenSize, vec2(1.0)); }\n"
                    "vec2  axeScreenTexel() { return 1.0 / max(u_ScreenSize, vec2(1.0)); }\n"
                    "float axeSceneDepth(vec2 uv) {\n"
                    "    if (u_HasSceneDepth == 0) return 0.0;\n"
                    "    vec3 p = texture(u_ScenePosition, uv).rgb;\n"
                    "    if (dot(p, p) < 1e-8) return 1e6;\n"
                    "    return length(p - u_CameraPosition);\n"
                    "}\n"
                    // PRIMITIVES_V1 — a posicao de mundo do que esta atras, crua.
                    // Fonte de dado, como o axeSceneDepth logo acima: expoe o
                    // attachment de posicao do G-Buffer, que o grafo nao alcanca
                    // sozinho. A composicao (profundidade vertical, distancia
                    // horizontal, o que for) se monta no grafo, com Subtract,
                    // Append e Length.
                    //
                    // Devolve o valor de limpeza (0,0,0) onde nao ha nada atras.
                    // Quem usar isto para medir distancia tem de tratar esse caso
                    // — e por isso o node avisa no painel de detalhes.
                    "vec3 axeSceneWorldPos(vec2 uv) {\n"
                    "    if (u_HasSceneDepth == 0) return vec3(0.0);\n"
                    "    return texture(u_ScenePosition, uv).rgb;\n"
                    "}\n"
                    // ── SCENE_BG_V1 — "ha geometria atras deste pixel?" ────
                    //
                    //  Esta pergunta SEMPRE foi respondivel no Surface: o
                    //  attachment de posicao fica no valor de limpeza onde nao
                    //  ha nada atras, e e o mesmo teste que o axeSceneDepth ja
                    //  usava para devolver infinito. O node "Scene Is
                    //  Background" e que emitia `0.0` fixo fora do Post
                    //  Process — ou seja, mentia dizendo "sempre ha geometria".
                    //
                    //  E e uma mentira cara. Quem mede distancia com o
                    //  axeSceneWorldPos recebe (0,0,0) no ceu, e `length(W.xz -
                    //  0)` vira a DISTANCIA ATE A ORIGEM DO MUNDO: aneis
                    //  concentricos centrados no ponto (0,0,0) da cena, em vez
                    //  de espuma em volta de cada objeto.
                    //
                    //  Continua sendo pergunta de DADO, e nao receita: o
                    //  tratamento do fundo se monta no grafo, com um Lerp.
                    "float axeSceneIsBackground(vec2 uv) {\n"
                    "    if (u_HasSceneDepth == 0) return 0.0;\n"
                    "    vec3 p = texture(u_ScenePosition, uv).rgb;\n"
                    "    return dot(p, p) < 1e-8 ? 1.0 : 0.0;\n"
                    "}\n"
                    // ── SCENE_HEIGHT_V1 — o mapa de topo ──────────────────
                    //
                    //  Estas duas sao as unicas fontes de dado do grafo que NAO
                    //  dependem de onde a camera esta. Todo o resto — Scene
                    //  Depth, Scene Normal, Scene World Position — le o
                    //  G-Buffer, que so conhece o que a camera enxerga, e por
                    //  isso qualquer distancia medida com eles muda quando a
                    //  camera gira.
                    //
                    //  Aqui a fonte e uma render ortografica de cima. A mesma
                    //  de qualquer angulo. Ver scene_height_pass.hpp.
                    //
                    //  axeSceneHeight  — o Y de mundo do que esta EMBAIXO deste
                    //                    ponto. Da coluna d'agua de verdade.
                    //  axeSceneDistance— a distancia HORIZONTAL, em metros, ate
                    //                    a geometria mais proxima. E a
                    //                    distancia ate a margem, e ela existe
                    //                    mesmo do lado que a camera nao ve.
                    //
                    //  Fora do quadrado coberto pelo mapa os dois devolvem o
                    //  valor de "nada aqui", em vez de repetir a borda: repetir
                    //  faria a espuma se esticar em faixas ate o horizonte.
                    "uniform sampler2D u_SceneHeightMap;\n"
                    "uniform sampler2D u_SceneSeedMap;\n"
                    "uniform mat4      u_SceneHeightMatrix;\n"
                    "uniform int       u_HasSceneHeight;\n"
                    "vec2 axeSceneHeightUV(vec3 worldPos) {\n"
                    "    vec4 p = u_SceneHeightMatrix * vec4(worldPos, 1.0);\n"
                    "    return (p.xy / p.w) * 0.5 + 0.5;\n"
                    "}\n"
                    "bool axeSceneHeightInside(vec2 uv) {\n"
                    "    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;\n"
                    "}\n"
                    "float axeSceneHeight(vec3 worldPos) {\n"
                    "    if (u_HasSceneHeight == 0) return -1e9;\n"
                    "    vec2 uv = axeSceneHeightUV(worldPos);\n"
                    "    if (!axeSceneHeightInside(uv)) return -1e9;\n"
                    "    return texture(u_SceneHeightMap, uv).r;\n"
                    "}\n"
                    "float axeSceneDistance(vec3 worldPos) {\n"
                    "    if (u_HasSceneHeight == 0) return 1e6;\n"
                    "    vec2 uv = axeSceneHeightUV(worldPos);\n"
                    "    if (!axeSceneHeightInside(uv)) return 1e6;\n"
                    "    vec4 seed = texture(u_SceneSeedMap, uv);\n"
                    "    if (seed.x > 1e8) return 1e6;\n"
                    "    return length(worldPos.xz - seed.xy);\n"
                    "}\n"
                    // ── SCENE_HEIGHT_V3 — a BASE da geometria mais proxima ─
                    //
                    //  O V2 devolvia a altura do TOPO, e o topo nao separa as
                    //  duas coisas que precisam ser separadas: uma pedra que
                    //  sai da agua e um cubo que paira sobre ela tem, os dois,
                    //  o topo acima da lamina. Filtrar por topo jogava fora a
                    //  margem de verdade junto com o objeto suspenso — e era
                    //  isso que apagava a espuma quando dois cubos ficavam na
                    //  mesma coluna: o de cima escondia o que estava na agua.
                    //
                    //  A BASE separa: a pedra desce ate abaixo da superficie, o
                    //  cubo suspenso nao. O teste no grafo vira uma comparacao
                    //  direta — base <= altura da agua e margem.
                    //
                    //  1e9 e a resposta para "nao ha nada": absurdamente alto,
                    //  logo nada encosta, logo sem espuma. O contrario do -1e9
                    //  do V2, e de proposito — o sentinela tem que cair no lado
                    //  SEGURO do teste.
                    "float axeSceneNearestBase(vec3 worldPos) {\n"
                    "    if (u_HasSceneHeight == 0) return 1e9;\n"
                    "    vec2 uv = axeSceneHeightUV(worldPos);\n"
                    "    if (!axeSceneHeightInside(uv)) return 1e9;\n"
                    "    vec4 seed = texture(u_SceneSeedMap, uv);\n"
                    "    if (seed.x > 1e8) return 1e9;\n"
                    "    return seed.z;\n"
                    "}\n"
                    // ── SCENE_HEIGHT_V4 — o TOPO da geometria mais proxima ──
                    //
                    //  A companheira obrigatoria do Nearest Base. Sozinha, a
                    //  base diz "isto desce ate a agua" — e isso e verdade na
                    //  pegada inteira de uma malha afundada, nao so onde ela
                    //  ainda sai da lamina.
                    //
                    //  Com as duas, o grafo pergunta o que interessa: a coluna
                    //  ATRAVESSA esta altura? base abaixo E topo acima. Quem
                    //  define "esta altura" continua sendo o material — o
                    //  passe nao sabe o que e agua.
                    "float axeSceneNearestTop(vec3 worldPos) {\n"
                    "    if (u_HasSceneHeight == 0) return -1e9;\n"
                    "    vec2 uv = axeSceneHeightUV(worldPos);\n"
                    "    if (!axeSceneHeightInside(uv)) return -1e9;\n"
                    "    vec4 seed = texture(u_SceneSeedMap, uv);\n"
                    "    if (seed.x > 1e8) return -1e9;\n"
                    "    return seed.w;\n"
                    "}\n\n";

            default:
                return
                    "// SCENE_DEPTH_SURFACE_V1 — dominio sem leitura de tela\n"
                    "vec2  axeScreenUV()    { return vec2(0.0); }\n"
                    "vec2  axeScreenTexel() { return vec2(0.0); }\n"
                    "float axeSceneDepth(vec2 uv) { return 0.0; }\n"
                    "vec3  axeSceneWorldPos(vec2 uv) { return vec3(0.0); }\n"
                    // SCENE_BG_V1 — dominio que nao le a tela nao sabe
                    // responder. Devolve 0.0, que e o valor que o node ja dava
                    // fora do Post Process: nao muda material nenhum existente.
                    "float axeSceneIsBackground(vec2 uv) { return 0.0; }\n"
                    // SCENE_HEIGHT_V1 — o mapa de topo so e ligado no passe
                    // de superficie. Aqui as funcoes existem para o mesmo grafo
                    // compilar nos dois dominios, devolvendo "nada aqui".
                    "float axeSceneHeight(vec3 worldPos) { return -1e9; }\n"
                    "float axeSceneDistance(vec3 worldPos) { return 1e6; }\n"
                    "float axeSceneNearestBase(vec3 worldPos) { return 1e9; }\n"
                    "float axeSceneNearestTop(vec3 worldPos) { return -1e9; }\n\n";
            }
        }

        // true se o grafo tem ao menos um node que le a tela. Usado SO para
        // avisar quem ligou Scene Depth num material Opaque, onde ele sempre
        // devolvera zero — o node compila, e o silencio seria pior.
        bool GraphReadsScene(MaterialGraph* graph)
        {
            if (!graph) return false;

            for (auto& node : graph->GetNodes())
                if (node->Name == "Scene Depth" || node->Name == "Screen UV")
                    return true;

            return false;
        }
    }

    // ── MATFUNC_V1 ───────────────────────────────────────────────────────────
    //
    // Estatico porque as entradas do MaterialCompiler sao TODAS metodos
    // estaticos que criam a instancia, compilam e a destroem — e a janela do
    // Material Editor precisa dos erros DEPOIS que ela ja morreu.
    //
    // Erro de GRAFO nao aparece no log do driver: um asset de funcao sumido
    // gera GLSL que compila limpo, so que fazendo a conta errada. Sem este
    // canal, o unico sintoma seria o material ficar visualmente errado sem
    // nenhuma mensagem — a pior forma de defeito que esta engine ja teve.
    //
    // O preco de ser estatico e um so material compilando por vez, que e
    // exatamente o que o MaterialEditorWindow faz (um unico m_Asset).
    std::string MaterialCompiler::s_LastFunctionErrors;

    const std::string& MaterialCompiler::LastFunctionErrors()
    {
        return s_LastFunctionErrors;
    }

    MaterialCompiler::MaterialCompiler(MaterialGraph* graph)
        : m_Graph(graph)
    {
        // Zera por COMPILACAO. Sem isso o log acumularia o erro de uma
        // compilacao anterior ja corrigida, e o autor iria atras de um
        // problema que nao existe mais.
        s_LastFunctionErrors.clear();
    }

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
        fs << SceneHelpersGLSL(SceneHelperMode::Surface);
        fs << NoiseHelpersGLSL();   // NOISE_SMOOTH_V1
        fs << compiler.m_CustomFunctions;
        fs << "void main()\n{\n";
        // ── TWO_SIDED_V1 ─────────────────────────────────────────────────
        //
        //  A normal interpolada e a da FACE, e a face de tras tem a normal
        //  apontando para longe de quem olha. Sem virar, tudo que depende de N
        //  responde ao contrario ali: o Fresnel acende no meio em vez da borda,
        //  a luz difusa da zero, e o autor acaba pondo um One Minus no grafo
        //  para compensar — o que quebra o material assim que ele e visto do
        //  outro lado.
        //
        //  gl_FrontFacing e a mesma resposta que a Unreal da nos materiais Two
        //  Sided. Custa uma instrucao e vale para toda malha aberta: plano de
        //  agua, folhagem, pano, cartaz.
        fs << "    // Normal da superfície (TWO_SIDED_V1: virada na face de tras)\n";
        fs << "    vec3 N = normalize(v_Normal);\n";
        fs << "    if (!gl_FrontFacing) N = -N;\n\n";

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
        result.TwoSided = graph->TwoSided;   // TWO_SIDED_V1

        // SCENE_HEIGHT_V6 — o codigo gerado ja tem as chamadas inlinadas das
        // Material Functions, entao procurar AQUI acha o uso venha ele de onde
        // vier. Uma varredura de nodes do grafo de fora nao acharia.
        result.UsesSceneHeight =
            compiler.m_FragmentCode.find("axeSceneHeight(") != std::string::npos ||
            compiler.m_FragmentCode.find("axeSceneDistance(") != std::string::npos ||
            compiler.m_FragmentCode.find("axeSceneNearestBase(") != std::string::npos ||
            compiler.m_FragmentCode.find("axeSceneNearestTop(") != std::string::npos;

        bool isMasked = (graph->BlendMode == MaterialBlendMode::Masked);
        result.IsMasked = isMasked;
        result.AlphaCutoff = 0.5f;

        // Opacity conectado a algo => este material precisa do forward
        // pass de transparência (vidro, etc.) — ver SceneRenderer.
        // Exceção: Masked vai pelo deferred (o discard faz o recorte).
        result.IsTransparent = (opacitySrc != nullptr) && !isMasked;

        // ── SCENE_DEPTH_SURFACE_V1 — o aviso que evita a tarde perdida ──────
        //
        // Scene Depth so tem valor no passe FORWARD, e um material so entra
        // nele quando e transparente: pino Opacity conectado e Blend Mode fora
        // de Masked.
        //
        // Sem este aviso, ligar Scene Depth num material Opaque compila limpo,
        // desenha, e devolve zero para sempre. O autor ve a agua chapada e nao
        // tem o que investigar — o pior formato de defeito que existe, e que
        // esta engine ja teve com uniform declarada e nunca enviada.
        // Carimbo de versao: o Clever confere isto no log ANTES de investigar
        // qualquer coisa no material. Se a linha nao aparecer ao compilar um
        // material, o binario e antigo e nao adianta olhar o grafo.
        AXE_CORE_INFO("[SCENE_HEIGHT_V6] material compilado — usa o mapa de altura: {}. "
            "A fatia vem do Y deste mesh; nada a configurar fora do grafo.",
            result.UsesSceneHeight ? "sim" : "nao");
        AXE_CORE_INFO("[TWO_SIDED_V1] normal virada nas faces de tras; "
            "Two Sided deste material = {}.", graph->TwoSided ? "sim" : "nao");

        if (GraphReadsScene(graph) && !result.IsTransparent)
        {
            AXE_CORE_WARN("[SCENE_DEPTH_SURFACE_V1] Este material usa Scene Depth ou "
                "Screen UV, mas NAO e translucido — os dois vao devolver zero. "
                "Ligue algo no pino Opacity e ponha o Blend Mode em Translucent "
                "para ele entrar no passe forward, o unico que enxerga a cena "
                "atras da superficie.");
        }

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
            fs << "    N = normalize(" << compiler.GetPinVariable(normalSrc->ID) << ");\n";
            // TWO_SIDED_V1 — o pin Normal SUBSTITUI N, entao a virada tem de
            // ser refeita sobre o valor novo. A de cima continua valendo: e ela
            // que faz o grafo LER a normal certa (um Fresnel montado com nodes
            // usa N antes desta linha).
            fs << "    if (!gl_FrontFacing) N = -N;\n\n";
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
        gs << SceneHelpersGLSL(SceneHelperMode::Stub);
        gs << NoiseHelpersGLSL();   // NOISE_SMOOTH_V1
        gs << compiler.m_CustomFunctions;
        gs << "void main()\n{\n";
        gs << "    vec3 N = normalize(v_Normal);\n";
        gs << "    if (!gl_FrontFacing) N = -N;\n\n";   // TWO_SIDED_V1

        // Reutiliza o código dos nodes gerado pelo grafo
        gs << compiler.m_FragmentCode;

        // Normal Map
        if (normalSrc)
        {
            gs << "    N = normalize(" << compiler.GetPinVariable(normalSrc->ID) << ");\n";
            gs << "    if (!gl_FrontFacing) N = -N;\n\n";   // TWO_SIDED_V1
        }

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
        fs << SceneHelpersGLSL(SceneHelperMode::Stub);
        fs << NoiseHelpersGLSL();   // NOISE_SMOOTH_V1
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
        fs << SceneHelpersGLSL(SceneHelperMode::PostProcess);
        fs << NoiseHelpersGLSL();   // NOISE_SMOOTH_V1
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
        fs << SceneHelpersGLSL(SceneHelperMode::Stub);
        fs << NoiseHelpersGLSL();   // NOISE_SMOOTH_V1
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
        fs << SceneHelpersGLSL(SceneHelperMode::Stub);
        fs << NoiseHelpersGLSL();   // NOISE_SMOOTH_V1
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


    // =========================================================================
    //  MATFUNC_V1 — inlining de uma Material Function
    //
    //  Chamada de funcao nao vira funcao GLSL: o grafo dela e PERCORRIDO aqui
    //  dentro, no mesmo compilador, emitindo no mesmo m_FragmentCode, com
    //  m_Graph trocado por baixo. E o que o GenerateFunctionBody do compilador
    //  de Script faz, e pela mesma razao: o gerador emite comandos em sequencia
    //  num unico fluxo, e embrulhar isso numa funcao de verdade pediria um
    //  parametro de saida para cada Function Output.
    //
    //  Em tres tempos:
    //
    //   1. AMARRA AS ENTRADAS — ainda no grafo do CHAMADOR, porque e la que
    //      moram os links que chegam no node de chamada. Cada Function Input
    //      da funcao recebe uma variavel propria com a expressao que chegou.
    //   2. PERCORRE O CORPO — agora com m_Graph = grafo da funcao, partindo de
    //      cada Function Output para tras.
    //   3. PUBLICA AS SAIDAS — uma variavel de apelido por pino de saida do
    //      node de chamada.
    //
    //  Entradas e saidas casam por NOME, nunca por posicao. Reordenar os
    //  parametros da funcao, ou inserir um no meio, nao pode trocar o
    //  significado de um material que ja estava certo.
    // =========================================================================
    void MaterialCompiler::InlineMaterialFunction(Node* callNode)
    {
        auto fail = [&](const std::string& msg)
            {
                AXE_CORE_WARN("[MATFUNC_V1] {}", msg);
                s_LastFunctionErrors += msg + "\n";

                // Toda saida da chamada vira valor neutro. O material continua
                // compilando: um asset fora do lugar nao pode derrubar o shader
                // inteiro, e o autor precisa do resto do grafo funcionando para
                // conseguir consertar.
                for (auto& out : callNode->Outputs)
                {
                    std::string var = MakeVar("mfnerr");
                    m_FragmentCode += GetGLSLType(out.Type) + " " + var + " = "
                        + AdaptToType("0.0", PinType::Float, out.Type) + ";\n";
                    RegisterPin(out.ID, var, out.Type);
                }
            };

        const std::string uuid = callNode->StringValue;

        if (uuid.empty())
        {
            fail("um node Material Function esta sem asset escolhido.");
            return;
        }

        // Ciclo. Sem esta checagem uma funcao que chama a si mesma inlinaria
        // para sempre: nao ha erro, nao ha mensagem — o editor simplesmente
        // congela e come a memoria.
        if (std::find(m_FunctionStack.begin(), m_FunctionStack.end(), uuid)
            != m_FunctionStack.end())
        {
            fail("ciclo de Material Function: a funcao de UUID " + uuid
                + " chama a si mesma, direta ou indiretamente.");
            return;
        }

        if (m_FunctionStack.size() >= 16)
        {
            fail("Material Function aninhada demais (limite de 16 niveis).");
            return;
        }

        const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);
        if (!rec || !std::filesystem::exists(rec->FilePath))
        {
            fail("o asset da Material Function (UUID " + uuid + ") nao foi "
                "encontrado. Os pinos do node vem do cache salvo no material, "
                "entao as ligacoes estao intactas — e so reapontar o asset.");
            return;
        }

        // Faixa de ID exclusiva para este grafo. Ver MaterialGraph::SeedNextID.
        const int idBase = m_NextFunctionIDBase;
        m_NextFunctionIDBase += 1000000;

        auto fn = MaterialFunction::LoadFromFile(rec->FilePath, idBase);
        if (!fn || !fn->GetGraph())
        {
            fail("nao consegui ler a Material Function '" + rec->FilePath.string() + "'.");
            return;
        }

        // O grafo tem que sobreviver ao fim desta funcao: as variaveis
        // registradas em m_PinVariables sao de pinos que moram dentro dele.
        m_InlinedFunctions.push_back(fn);
        MaterialGraph* fnGraph = fn->GetGraph();

        m_FragmentCode += "\n// --- Material Function: " + fn->GetName() + " ---\n";

        // ── 1. ENTRADAS ──────────────────────────────────────────────────────
        // Ainda com m_Graph no CHAMADOR: GetSourcePin tem que enxergar os links
        // que chegam no node de chamada.
        for (auto& n : fnGraph->GetNodes())
        {
            if (n->Name != "Function Input" || n->Outputs.empty()) continue;

            const PinType type = n->CustomOutputType;

            Pin* callPin = nullptr;
            for (auto& p : callNode->Inputs)
                if (p.Name == n->StringValue) { callPin = &p; break; }

            std::string expr;
            if (!callPin)
            {
                // A funcao ganhou um parametro depois que este material foi
                // salvo: os pinos do node de chamada vieram do cache antigo.
                expr = AdaptToType("0.0", PinType::Float, type);
                const std::string msg = "a funcao '" + fn->GetName() + "' tem a entrada '"
                    + n->StringValue + "' que o node de chamada nao tem. Abra o node e "
                    "clique em Recarregar assinatura.";
                AXE_CORE_WARN("[MATFUNC_V1] {}", msg);
                s_LastFunctionErrors += msg + "\n";
            }
            else if (Pin* src = GetSourcePin(callPin))
            {
                expr = AdaptToType(GetPinVariable(src->ID), GetPinType(src->ID), type);
            }
            else
            {
                // Pino solto usa o valor digitado nele, igual a qualquer outro
                // node do grafo.
                expr = AdaptToType(std::to_string(callPin->DefaultFloat), PinType::Float, type);
            }

            const std::string var = MakeVar("fnin");
            m_FragmentCode += GetGLSLType(type) + " " + var + " = " + expr + ";\n";

            RegisterPin(n->Outputs[0].ID, var, type);

            // Ja resolvido: marcar como visitado impede que o percurso do corpo
            // tente gerar codigo para ele.
            m_VisitedNodes.insert(n->ID.Get());
        }

        // ── 2. CORPO ─────────────────────────────────────────────────────────
        MaterialGraph* savedGraph = m_Graph;
        m_Graph = fnGraph;
        m_FunctionStack.push_back(uuid);

        struct Resolved { std::string Var; PinType Type; };
        std::unordered_map<std::string, Resolved> resolved;

        for (auto& n : fnGraph->GetNodes())
        {
            if (n->Name != "Function Output" || n->Inputs.empty()) continue;

            VisitNode(n.get());

            Resolved r{ AdaptToType("0.0", PinType::Float, n->CustomOutputType),
                        n->CustomOutputType };

            if (Pin* src = GetSourcePin(&n->Inputs[0]))
                r.Var = AdaptToType(GetPinVariable(src->ID), GetPinType(src->ID),
                    n->CustomOutputType);

            resolved[n->StringValue] = r;
        }

        m_FunctionStack.pop_back();
        m_Graph = savedGraph;

        // ── 3. SAIDAS ────────────────────────────────────────────────────────
        // Uma variavel de apelido por pino, em vez de apontar direto para a
        // variavel interna da funcao. Custa nada (o compilador de GLSL some com
        // ela) e deixa o codigo gerado legivel quando alguem for ler o
        // .axeshader para depurar.
        for (auto& out : callNode->Outputs)
        {
            const std::string var = MakeVar("mfn");

            auto it = resolved.find(out.Name);
            std::string expr;

            if (it != resolved.end())
            {
                expr = AdaptToType(it->second.Var, it->second.Type, out.Type);
            }
            else
            {
                expr = AdaptToType("0.0", PinType::Float, out.Type);
                const std::string msg = "a funcao '" + fn->GetName() + "' nao tem a saida '"
                    + out.Name + "' que o node de chamada espera. Abra o node e clique em "
                    "Recarregar assinatura.";
                AXE_CORE_WARN("[MATFUNC_V1] {}", msg);
                s_LastFunctionErrors += msg + "\n";
            }

            m_FragmentCode += GetGLSLType(out.Type) + " " + var + " = " + expr + ";\n";
            RegisterPin(out.ID, var, out.Type);
        }

        m_FragmentCode += "// --- fim: " + fn->GetName() + " ---\n";
    }

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

        // ═════════════════════════════════════════════════════════════════════
        //  PRIMITIVES_V1 — o tipo de um pino, em numero de componentes
        //
        //  A auditoria que originou esta rodada varreu os ~60 ramos abaixo
        //  perguntando "este node respeita o tipo do que esta ligado nele?".
        //  Vinte e um respondiam NAO — emitiam um tipo fixo no GLSL,
        //  independentemente da entrada.
        //
        //  Para a maioria isso e correto: um node Color E vec4, um World
        //  Position E vec3. Mas em Append e Vector Split — as duas unicas
        //  ferramentas do grafo para MONTAR e DESMONTAR vetores — era um
        //  defeito, e um defeito grave: sem elas o autor nao consegue trocar
        //  componentes de lugar, e toda conta que precise disso vira um pedido
        //  de node novo. Foi assim que apareceram Water Depth e Shore
        //  Distance, e por isso os dois foram embora nesta mesma rodada.
        // ═════════════════════════════════════════════════════════════════════
        auto compsOf = [](PinType t) -> int
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
        auto typeOfComps = [](int n) -> PinType
            {
                switch (n)
                {
                case 2:  return PinType::Vec2;
                case 3:  return PinType::Vec3;
                case 4:  return PinType::Vec4;
                default: return PinType::Float;
                }
            };

        // Builtin unaria que PRESERVA o tipo: abs, floor, fract, sin...
        // Escrita uma vez porque cada copia dela era uma chance a mais de
        // alguem fixar `float` na saida sem perceber — que e exatamente o que
        // aconteceu com Sine e Cosine.
        auto emitUnary = [&](const char* fn, const char* prefix) -> void
            {
                std::string var = MakeVar(prefix);
                std::string val = std::to_string(node->Inputs[0].DefaultFloat);
                PinType     type = PinType::Float;

                if (Pin* src = GetSourcePin(&node->Inputs[0]))
                {
                    val = GetPinVariable(src->ID);
                    type = GetPinType(src->ID);
                }

                RegisterPin(node->Outputs[0].ID, var, type);
                code << GetGLSLType(type) << " " << var << " = "
                    << fn << "(" << val << ");";
            };

        // -----------------------------------------------------------------
        // Float — constante escalar
        // -----------------------------------------------------------------
        if (node->Name == "Float")
        {
            std::string var = MakeVar("float");
            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            // NOISE_SMOOTH_V1 — `<<` num float 3.0f imprime "3", e "float x = 3;"
            // so passa porque GLSL promove int para float na atribuicao. Onde a
            // expressao entra num argumento com sobrecarga (step, mix, um dos
            // helpers), esse mesmo literal vira "no matching overloaded
            // function". std::to_string sempre poe a casa decimal.
            code << "float " << var << " = "
                << std::to_string(node->Value.FloatVal) << ";";
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

            // SCENE_DEPTH_SURFACE_V1 — passou a sair dos helpers, que sabem
            // o que "UV de tela" significa em cada dominio: no Post Process e
            // o v_TexCoord do quad; num material de superficie e o
            // gl_FragCoord dividido pelo tamanho da tela; nos demais, zero.
            code << "vec2 " << uvVar << " = axeScreenUV();";
            code << "\n    vec2 " << pxVar << " = axeScreenTexel();";
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

            // SCENE_BG_V1 — o `if (m_PostProcessTarget)` sumiu daqui: qual e o
            // teste certo passou a ser propriedade do PROLOGO de cada dominio,
            // como ja acontece com axeSceneDepth e axeSceneWorldPos. Um lugar
            // so decide, e o node apenas pergunta.
            code << "float " << var << " = axeSceneIsBackground(" << uv << ");";
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

            // SCENE_DEPTH_SURFACE_V1 — o default do pino UV deixou de ser
            // v_TexCoord e virou axeScreenUV().
            //
            // Num quad de tela cheia os dois sao a mesma coisa, entao o Post
            // Process nao muda. Numa MALHA sao coisas diferentes: v_TexCoord e
            // a UV do modelo, e amostrar o G-Buffer com ela leria um pixel
            // arbitrario da tela. O que a agua quer e "o que esta atras DESTE
            // pixel", que e gl_FragCoord.
            std::string uv = "axeScreenUV()";
            if (!node->Inputs.empty())
            {
                Pin* uvSrc = GetSourcePin(&node->Inputs[0]);
                if (uvSrc)
                    uv = AdaptToType(GetPinVariable(uvSrc->ID),
                        GetPinType(uvSrc->ID), PinType::Vec2);
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);

            code << "float " << var << " = axeSceneDepth(" << uv << ");";
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

            // PRIMITIVES_V2 — o expoente vira escalar COM AVISO, e nao em
            // silencio: mandar uma cor para o expoente e engano de ligacao, e o
            // autor precisa saber. Ver a nota no Lerp.
            Pin* srcB = GetSourcePin(&node->Inputs[1]);
            if (srcB)
                valB = ForceFloat(node->Name, "B",
                    GetPinVariable(srcB->ID), GetPinType(srcB->ID));

            // pow(genX, genX): o expoente TEM de ter o mesmo numero de
            // componentes da base. `pow(vec2, float)` nao existe — e era mais
            // um lugar em que Vec2 caia fora dos dois ifs escritos a mao.
            RegisterPin(node->Outputs[0].ID, var, typeA);
            const std::string zero = AdaptToType("0.0", PinType::Float, typeA);
            const std::string expo = AdaptToType(valB, PinType::Float, typeA);
            code << GetGLSLType(typeA) << " " << var
                << " = pow(max(" << valA << ", " << zero << "), " << expo << ");";
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

            // ── PRIMITIVES_V2 ────────────────────────────────────────────
            //
            //  As conversoes daqui eram escritas a mao e so enxergavam Vec3 e
            //  Vec4. Vec2 nao aparecia em nenhuma linha — e por isso, no
            //  instante em que o Append passou a produzir Vec2 de verdade,
            //  este node comecou a gerar `mix(vec3, vec3, vec2)`.
            //
            //  Nao e caso isolado: Clamp e Power tinham a mesma cegueira, cada
            //  um com a sua copia da conversao. Agora os tres chamam o
            //  AdaptToType, que ja trata os quatro tipos — e a correcao apaga
            //  codigo em vez de acrescentar.
            //
            //  O Alpha e o caso separado: `mix` aceita escalar ou o mesmo tipo
            //  de A e B, nunca um terceiro. Forcar escalar e a escolha
            //  previsivel, e o ForceFloat AVISA nomeando node e pino — que e a
            //  diferenca entre "use um Length aqui" e um erro de GLSL numa
            //  linha que o autor nunca escreveu.
            Pin* srcAlpha = GetSourcePin(&node->Inputs[2]);
            if (srcAlpha)
                alpha = ForceFloat(node->Name, "Alpha",
                    GetPinVariable(srcAlpha->ID), GetPinType(srcAlpha->ID));

            PinType resultType = (compsOf(typeA) >= compsOf(typeB)) ? typeA : typeB;
            valA = AdaptToType(valA, typeA, resultType);
            valB = AdaptToType(valB, typeB, resultType);

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

            // PRIMITIVES_V2 — era conversao a mao, cega para Vec2. Ver a nota
            // no Lerp.
            Pin* srcMin = GetSourcePin(&node->Inputs[1]);
            if (srcMin)
                minVal = AdaptToType(GetPinVariable(srcMin->ID),
                    GetPinType(srcMin->ID), typeV);

            Pin* srcMax = GetSourcePin(&node->Inputs[2]);
            if (srcMax)
                maxVal = AdaptToType(GetPinVariable(srcMax->ID),
                    GetPinType(srcMax->ID), typeV);

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
            emitUnary("sin", "sine");   // PRIMITIVES_V1 — componente a componente
        }

        // -----------------------------------------------------------------
        // Cosine — cos(x)
        // -----------------------------------------------------------------
        else if (node->Name == "Cosine")
        {
            emitUnary("cos", "cosine");   // PRIMITIVES_V1
        }

        // -----------------------------------------------------------------
        // Step — step(edge, x)
        // -----------------------------------------------------------------
        else if (node->Name == "Step")
        {
            std::string var = MakeVar("step");
            std::string edge = std::to_string(node->Inputs[0].DefaultFloat);
            std::string val = std::to_string(node->Inputs[1].DefaultFloat);

            // STEP_TYPES_V1 — ver a nota no SmoothStep logo abaixo. Os dois
            // pinos sao Float declarados e a saida e Float; um vetor ligado
            // aqui tem de virar escalar ANTES de entrar no step(), senao o
            // erro sai como "no matching overloaded function found" numa linha
            // de GLSL que o autor nao escreveu.
            Pin* srcEdge = GetSourcePin(&node->Inputs[0]);
            if (srcEdge) edge = ForceFloat(node->Name, "Edge",
                GetPinVariable(srcEdge->ID), GetPinType(srcEdge->ID));

            Pin* srcVal = GetSourcePin(&node->Inputs[1]);
            if (srcVal) val = ForceFloat(node->Name, "Value",
                GetPinVariable(srcVal->ID), GetPinType(srcVal->ID));

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

            // ── STEP_TYPES_V1 ────────────────────────────────────────────
            //
            // Os tres pinos sao declarados Float e a saida e Float, mas o
            // codigo pegava a variavel da origem CRUA. Ligar um Vec3 (uma cor,
            // uma posicao) em qualquer um deles gerava
            // `smoothstep(vec3, float, float)`, que nao casa com nenhuma
            // sobrecarga do GLSL — e o driver responde
            // "'smoothstep' : no matching overloaded function found" apontando
            // uma linha de um fonte gerado que ninguem escreveu.
            //
            // Nao ha erro de GRAFO nesse caso: os pinos dizem Float e o editor
            // aceita a ligacao. Entao o conserto e aqui: converter, e AVISAR
            // qual pino precisou de conversao.
            Pin* srcMin = GetSourcePin(&node->Inputs[0]);
            if (srcMin) minVal = ForceFloat(node->Name, "Min",
                GetPinVariable(srcMin->ID), GetPinType(srcMin->ID));

            Pin* srcMax = GetSourcePin(&node->Inputs[1]);
            if (srcMax) maxVal = ForceFloat(node->Name, "Max",
                GetPinVariable(srcMax->ID), GetPinType(srcMax->ID));

            Pin* srcVal = GetSourcePin(&node->Inputs[2]);
            if (srcVal) val = ForceFloat(node->Name, "Value",
                GetPinVariable(srcVal->ID), GetPinType(srcVal->ID));

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = smoothstep(" << minVal << ", " << maxVal << ", " << val << ");";
        }

        // -----------------------------------------------------------------
        // Normalize — normalize(v)
        // -----------------------------------------------------------------
        else if (node->Name == "Normalize")
        {
            // PRIMITIVES_V1 — normalize vale para vec2/3/4; so nao vale para
            // escalar, e ai o vetor para cima continua sendo o fallback.
            std::string var = MakeVar("normalize");
            std::string val = "vec3(0.0, 1.0, 0.0)";
            PinType     type = PinType::Vec3;

            if (Pin* src = GetSourcePin(&node->Inputs[0]))
            {
                val = GetPinVariable(src->ID);
                PinType t = GetPinType(src->ID);
                if (t == PinType::Vec2 || t == PinType::Vec3 || t == PinType::Vec4)
                    type = t;
                else
                    val = "vec3(" + val + ")";
            }

            RegisterPin(node->Outputs[0].ID, var, type);
            code << GetGLSLType(type) << " " << var << " = normalize(" << val << ");";
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
        // ── PRIMITIVES_V1 — Append vira um CONSTRUTOR ────────────────────────
        //
        //  Antes: `vec4 v = vec4(A, B);`, com A declarado Vec3 e B Float. Era
        //  um "Vec3 mais W", e nao um Append.
        //
        //  Ligar dois Floats gerava `vec4(f, f)` — GLSL invalido, com erro numa
        //  linha que o autor nunca escreveu. E como nao havia outro jeito de
        //  MONTAR um vec2 no grafo, uma conta tao banal quanto
        //  `length(vec2(dx, dz))` era simplesmente impossivel de desenhar.
        //
        //  Agora o tipo da saida e a SOMA dos componentes das entradas:
        //  Float+Float da Vec2, Vec2+Float da Vec3, Vec2+Vec2 da Vec4. Passou
        //  de quatro, satura em vec4 e avisa — inventar um vec5 nao existe.
        else if (node->Name == "Append")
        {
            std::string var = MakeVar("append");

            std::string valA = std::to_string(node->Inputs[0].DefaultFloat);
            PinType     typeA = PinType::Float;
            if (Pin* srcA = GetSourcePin(&node->Inputs[0]))
            {
                valA = GetPinVariable(srcA->ID);
                typeA = GetPinType(srcA->ID);
            }

            std::string valB = std::to_string(node->Inputs[1].DefaultFloat);
            PinType     typeB = PinType::Float;
            if (Pin* srcB = GetSourcePin(&node->Inputs[1]))
            {
                valB = GetPinVariable(srcB->ID);
                typeB = GetPinType(srcB->ID);
            }

            int total = compsOf(typeA) + compsOf(typeB);
            if (total > 4)
            {
                AXE_CORE_WARN("[PRIMITIVES_V1] Append: {} + {} componentes passam de 4 — "
                    "o resultado foi cortado em vec4.", compsOf(typeA), compsOf(typeB));
                total = 4;
            }

            PinType outType = typeOfComps(total);
            RegisterPin(node->Outputs[0].ID, var, outType);
            code << GetGLSLType(outType) << " " << var << " = "
                << GetGLSLType(outType) << "(" << valA << ", " << valB << ");";
        }

        // -----------------------------------------------------------------
        // Vector Split — separa um Vec3 em X, Y, Z
        // -----------------------------------------------------------------
        // ── PRIMITIVES_V1 — Vector Split aceita qualquer vetor ───────────────
        //
        //  Antes emitia `vec3 split = <origem>;` sempre. Um Vec4 ligado aqui
        //  virava `vec3 x = algumVec4;`, que e GLSL invalido; um Vec2, idem.
        //  Na pratica so funcionava com Vec3 — e o par dele, o Append, so
        //  funcionava com Vec3+Float. Os dois juntos fechavam a porta.
        //
        //  O pino W e NOVO e entra no fim da lista: Load() remapeia pino por
        //  posicao parando no menor dos dois tamanhos, entao material salvo
        //  antes disto abre sem deslocar fio nenhum.
        //
        //  Componente que nao existe na origem devolve 0.0 em vez de swizzle
        //  invalido — pedir .w de um vec2 e engano do autor, e o certo e ele
        //  ver zero e um aviso, nao um erro de GLSL sem linha correspondente.
        else if (node->Name == "Vector Split")
        {
            std::string var = MakeVar("split");
            std::string val = "vec3(0.0)";
            PinType     type = PinType::Vec3;

            if (Pin* src = GetSourcePin(&node->Inputs[0]))
            {
                val = GetPinVariable(src->ID);
                type = GetPinType(src->ID);
            }

            const int n = compsOf(type);
            code << GetGLSLType(type) << " " << var << " = " << val << ";";

            static const char* kSwz[4] = { ".x", ".y", ".z", ".w" };
            for (int i = 0; i < (int)node->Outputs.size(); i++)
            {
                if (i < n)
                    RegisterPin(node->Outputs[i].ID,
                        n == 1 ? var : var + kSwz[i], PinType::Float);
                else
                    RegisterPin(node->Outputs[i].ID, "0.0", PinType::Float);
            }
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
        // WATER_NODES_V1 — Camera Position
        //
        // u_CameraPosition ja e declarada em TODOS os shaders de superficie
        // (forward e G-Buffer) desde a correcao do Camera Vector, entao este
        // node nao precisa de encanamento nenhum: e so expor o que ja chega.
        //
        // Sai em variavel local, e nao referenciando a uniform direto, pela
        // razao que ja mordeu no `u_SunIntensity`: fora do dominio certo a
        // uniform pode nao existir, e a copia local mantem o resto do grafo
        // compilando.
        // -----------------------------------------------------------------
        else if (node->Name == "Camera Position")
        {
            std::string var = MakeVar("campos");
            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            RegisterPin(node->Outputs[1].ID, var + ".x", PinType::Float);
            RegisterPin(node->Outputs[2].ID, var + ".y", PinType::Float);
            RegisterPin(node->Outputs[3].ID, var + ".z", PinType::Float);
            code << "vec3 " << var << " = u_CameraPosition;";
        }

        // -----------------------------------------------------------------
        // WATER_NODES_V1 — Pixel Depth
        //
        // Distancia da camera ate ESTE fragmento, em metros. Mesma unidade e
        // mesma definicao do Scene Depth (que mede ate o que esta ATRAS), de
        // proposito: subtrair um do outro tem de dar espessura em metros, e
        // nao um numero sem sentido fisico.
        // -----------------------------------------------------------------

        // ── SCENE_HEIGHT_V1 — Scene Height ───────────────────────────────────
        //
        //  A UNICA fonte do grafo que nao depende de onde a camera esta. Vem da
        //  render ortografica de topo, e nao do G-Buffer.
        //
        //   Height   — o Y de mundo do que esta EMBAIXO deste ponto. Subtrair
        //              da altura da superficie da a coluna d'agua real.
        //   Distance — a distancia HORIZONTAL, em metros, ate a geometria mais
        //              proxima. E a distancia ate a margem, e existe tambem do
        //              lado que a camera nao enxerga.
        //
        //  Continua sendo node de DADO, e nao receita: como isso vira espuma,
        //  cor ou transparencia se monta no grafo.
        else if (node->Name == "Scene Height")
        {
            std::string worldPos = "v_FragPos";
            if (Pin* src = GetSourcePin(&node->Inputs[0]))
                worldPos = AdaptToType(GetPinVariable(src->ID),
                    GetPinType(src->ID), PinType::Vec3);

            std::string hVar = MakeVar("sheight");
            std::string dVar = MakeVar("sdist");
            std::string nVar = MakeVar("snear");
            std::string tVar = MakeVar("sntop");

            RegisterPin(node->Outputs[0].ID, hVar, PinType::Float);
            RegisterPin(node->Outputs[1].ID, dVar, PinType::Float);
            if (node->Outputs.size() > 2)
                RegisterPin(node->Outputs[2].ID, nVar, PinType::Float);
            // SCENE_HEIGHT_V4 — o pino novo entra no FIM da lista, e um grafo
            // salvo antes desta versao simplesmente nao o tem. O `if` e o que
            // deixa os dois casos compilarem sem migracao de arquivo.
            if (node->Outputs.size() > 3)
                RegisterPin(node->Outputs[3].ID, tVar, PinType::Float);

            code << "float " << hVar << " = axeSceneHeight(" << worldPos << ");\n"
                << "float " << dVar << " = axeSceneDistance(" << worldPos << ");\n"
                << "float " << nVar << " = axeSceneNearestBase(" << worldPos << ");\n"
                << "float " << tVar << " = axeSceneNearestTop(" << worldPos << ");";
        }

        //  Scene World Position: a posicao de mundo do que esta atras, crua.
        //
        //  Cuidado que o Water Depth ja resolve por dentro: onde nao ha nada
        //  atras, o attachment esta no valor de limpeza e isto devolve
        //  (0,0,0) — nao a posicao de um ponto real. Quem usar este node para
        //  medir profundidade tem que tratar esse caso.
        else if (node->Name == "Scene World Position")
        {
            std::string var = MakeVar("swpos");
            std::string uv = "axeScreenUV()";

            if (Pin* src = GetSourcePin(&node->Inputs[0]))
                if (GetPinType(src->ID) == PinType::Vec2)
                    uv = GetPinVariable(src->ID);

            RegisterPin(node->Outputs[0].ID, var, PinType::Vec3);
            RegisterPin(node->Outputs[1].ID, var + ".x", PinType::Float);
            RegisterPin(node->Outputs[2].ID, var + ".y", PinType::Float);
            RegisterPin(node->Outputs[3].ID, var + ".z", PinType::Float);
            code << "vec3 " << var << " = axeSceneWorldPos(" << uv << ");";
        }

        else if (node->Name == "Pixel Depth")
        {
            std::string var = MakeVar("pdepth");
            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = length(u_CameraPosition - v_FragPos);";
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
        // ── MATFUNC_V1 — os tres nodes de Material Function ──────────────────
        //
        // A chamada escreve direto em m_FragmentCode (ver InlineMaterialFunction)
        // e por isso devolve string vazia aqui.
        else if (node->Name == "Material Function")
        {
            InlineMaterialFunction(node);
        }

        // Dentro de uma funcao, este node nunca chega aqui: o inlining registra
        // a variavel dele e o marca como visitado antes de percorrer o corpo.
        // Chegar aqui significa um Function Input solto num material comum.
        else if (node->Name == "Function Input")
        {
            const PinType type = node->CustomOutputType;
            std::string var = MakeVar("fnin_solto");

            AXE_CORE_WARN("[MATFUNC_V1] o node Function Input '{}' esta num grafo que "
                "nao e uma Material Function — nao ha chamador para alimenta-lo, entao "
                "ele vale zero. Function Input so faz sentido dentro de um .axematfunc.",
                node->StringValue);

            RegisterPin(node->Outputs[0].ID, var, type);
            code << GetGLSLType(type) << " " << var << " = "
                << AdaptToType("0.0", PinType::Float, type) << ";";
        }

        // Terminal, como o Material Output: quem le o valor dele e o inlining,
        // pelo link que chega no pino de entrada. Nao emite linha nenhuma.
        else if (node->Name == "Function Output")
        {
        }

        // ── PRIMITIVES_V1 — as builtins de GLSL que faltavam ─────────────────
        //
        //  Todas sao 1:1 com uma funcao da linguagem e preservam o tipo. Um
        //  node assim NAO e composicao disfarcada: e a linguagem exposta no
        //  grafo, que e o contrario de embutir uma receita pronta. Sem elas,
        //  arredondar, quantizar ou tirar raiz obrigava a cair no node Custom.
        else if (node->Name == "Floor") { emitUnary("floor", "floor"); }
        else if (node->Name == "Ceil") { emitUnary("ceil", "ceil"); }
        else if (node->Name == "Round") { emitUnary("round", "round"); }
        else if (node->Name == "Sqrt") { emitUnary("sqrt", "sqrt"); }
        else if (node->Name == "Sign") { emitUnary("sign", "sign"); }

        //  mod e binaria: o tipo vem de A, e B pode ser escalar (GLSL aceita
        //  mod(vec3, float)) ou do mesmo tipo.
        else if (node->Name == "Mod")
        {
            std::string var = MakeVar("mod");

            std::string valA = std::to_string(node->Inputs[0].DefaultFloat);
            PinType     type = PinType::Float;
            if (Pin* srcA = GetSourcePin(&node->Inputs[0]))
            {
                valA = GetPinVariable(srcA->ID);
                type = GetPinType(srcA->ID);
            }

            std::string valB = std::to_string(node->Inputs[1].DefaultFloat);
            if (Pin* srcB = GetSourcePin(&node->Inputs[1]))
                valB = GetPinVariable(srcB->ID);

            RegisterPin(node->Outputs[0].ID, var, type);
            code << GetGLSLType(type) << " " << var
                << " = mod(" << valA << ", " << valB << ");";
        }

        // MATFUNC_V1 — fract() em qualquer tipo, como o Abs logo acima.
        else if (node->Name == "Fract")
        {
            std::string var = MakeVar("fract");
            std::string val = "0.0";
            PinType     type = PinType::Float;

            Pin* src = GetSourcePin(&node->Inputs[0]);
            if (src) { val = GetPinVariable(src->ID); type = GetPinType(src->ID); }

            RegisterPin(node->Outputs[0].ID, var, type);
            code << GetGLSLType(type) << " " << var << " = fract(" << val << ");";
        }

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

            // NOISE_SMOOTH_V1 — Scale e Detail sao pinos NOVOS. Um .axegraph
            // salvo antes desta versao tem so o pino UV, entao os guardas de
            // tamanho abaixo nao sao decoracao: sem eles, abrir um material
            // antigo indexaria fora do vetor.
            std::string scale = "8.0";
            if (node->Inputs.size() > 1)
            {
                scale = std::to_string(node->Inputs[1].DefaultFloat);
                if (Pin* srcScale = GetSourcePin(&node->Inputs[1]))
                    scale = ForceFloat(node->Name, "Scale",
                        GetPinVariable(srcScale->ID), GetPinType(srcScale->ID));
            }

            std::string detail = "3.0";
            if (node->Inputs.size() > 2)
            {
                detail = std::to_string(node->Inputs[2].DefaultFloat);
                if (Pin* srcDetail = GetSourcePin(&node->Inputs[2]))
                    detail = ForceFloat(node->Name, "Detail",
                        GetPinVariable(srcDetail->ID), GetPinType(srcDetail->ID));
            }

            RegisterPin(node->Outputs[0].ID, var, PinType::Float);
            code << "float " << var << " = axeFbm((" << uv << ") * " << scale
                << ", int(clamp(" << detail << ", 1.0, 8.0)));";
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
    // ── STEP_TYPES_V1 ────────────────────────────────────────────────────────
    //
    // AdaptToType com um aviso. Existe porque a conversao silenciosa resolve o
    // erro de compilacao e ESCONDE o engano: o autor ligou uma cor num pino que
    // pede um numero, e o material passa a funcionar com o canal .x da cor —
    // que quase nunca e o que ele queria.
    //
    // O aviso vai para o console do engine com o nome do node e do pino, entao
    // ele sabe ONDE, e nao so que aconteceu.
    std::string MaterialCompiler::ForceFloat(const std::string& nodeName,
        const char* pinName,
        const std::string& expr,
        PinType from)
    {
        if (from == PinType::Float) return expr;

        AXE_CORE_WARN("[STEP_TYPES_V1] '{}': o pino '{}' pede um numero e recebeu um "
            "vetor — usando o primeiro componente. Se voce queria o brilho, ponha um "
            "Desaturate ou um Vector Split antes.", nodeName, pinName);

        return AdaptToType(expr, from, PinType::Float);
    }

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
        data.TwoSided = result.TwoSided;      // TWO_SIDED_V1
        data.UsesSceneHeight = result.UsesSceneHeight;   // SCENE_HEIGHT_V6
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
        data.TwoSided = result.TwoSided;      // TWO_SIDED_V1
        data.UsesSceneHeight = result.UsesSceneHeight;   // SCENE_HEIGHT_V6

        return CookedMaterial::Save(CookedMaterial::PathFor(materialFilePath), data);
    }
}