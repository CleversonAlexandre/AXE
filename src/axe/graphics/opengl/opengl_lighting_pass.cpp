#include "opengl_lighting_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/graphics/texture3d.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>

namespace axe
{
    static const char* s_QuadVert = R"(
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

    static const char* s_LightingFrag = R"(
    #version 460 core
    out vec4 FragColor;
    in vec2 v_TexCoord;

    // G-Buffer
    uniform sampler2D u_Position;
    uniform sampler2D u_Normal;
    uniform sampler2D u_Albedo;
    uniform sampler2D u_PBR;
    uniform sampler2D u_Emissive;

    // SSAO
    uniform sampler2D u_SSAO;
    uniform int       u_HasSSAO;
    uniform int       u_SSAODebug;

    // Shadow — suporta CSM (texture array) ou shadow map simples (fallback)
    //
    // SHADOW_HWPCF_V1 — a MESMA textura chega em duas unidades, com dois
    // sampler objects diferentes (ver cascaded_shadow_pass.hpp):
    //   u_ShadowMapCSM (unidade 11) — sampler2DArrayShadow: a unidade de
    //     textura compara e ja devolve 0..1 filtrado bilinearmente. 1 = passou
    //     no teste = ILUMINADO.
    //   u_ShadowMapRaw (unidade 12) — sampler2DArray: profundidade crua, que
    //     e o que a busca de bloqueador do PCSS precisa.
    uniform sampler2DArrayShadow u_ShadowMapCSM;   // comparacao por hardware
    uniform sampler2DArray       u_ShadowMapRaw;   // profundidade crua (PCSS)
    uniform sampler2D      u_ShadowMap;            // shadow map simples (fallback)
    uniform mat4  u_LightSpaceMatrixCSM[4];        // matrizes por cascade
    uniform float u_CascadeSplitDepths[4];         // splits em view space (z negativo)
    uniform float u_CascadeTexelWorld[4];          // SHADOW_ACNE_V1 — metros por texel
    uniform float u_CascadeDepthRange[4];          // PCSS_V1 — metros do intervalo [0,1]
    uniform float u_SunAngularRadius;              // PCSS_V1 — tan(metade do angulo); 0 = PCF fixo
    uniform float u_MaxPenumbraTexels;
    uniform int   u_CascadeCount;                  // 0 = sem CSM
    uniform mat4  u_LightSpaceMatrix;
    uniform int   u_HasShadowMap;
    uniform mat4  u_View;
    uniform float u_ShadowBias;

    // ── CONTACT_SHADOW_V1 ────────────────────────────────────────────────
    uniform mat4  u_ViewProjection;        // para projetar o raio na tela
    uniform float u_ContactShadowLength;   // metros; 0 = desligado

    // Luz direcional
    uniform vec3  u_LightDirection;
    uniform vec3  u_LightColor;
    uniform float u_LightIntensity;
    uniform float u_AmbientStrength;
    uniform float u_AmbientShadowFactor; // 0=ambient bloqueado por sombra, 1=ambient livre
    uniform vec3  u_SkyLightColor;       // SKY_LIGHT_V1 — tinta do ambiente
    uniform float u_AmbientFloor;        // SKY_SUN_GATE_V1b — piso, 0 = sem luz
    uniform vec3  u_CameraPosition;
    uniform int   u_HasLight;

    // Cookie da luz direcional — projeção planar (paralela), com tiling
    uniform sampler2D u_DirCookie;
    uniform int       u_HasDirCookie;
    uniform vec3      u_DirCookieRight;
    uniform vec3      u_DirCookieUp;
    uniform float     u_DirCookieScale;

    // IBL
    uniform samplerCube u_IrradianceMap;
    uniform samplerCube u_PrefilteredMap;
    uniform sampler2D   u_BRDFLut;
    uniform int         u_HasIBL;
    uniform float       u_IBLIntensity;

    // Point Lights
    struct PointLight {
        vec3  Position;
        vec3  Color;
        float Intensity;
        float Radius;

        // Spot Light
        int   IsSpot;
        vec3  Direction;
        float InnerCutoff; // cos(InnerConeAngle), pré-calculado na CPU
        float OuterCutoff; // cos(OuterConeAngle)

        // Cookie — projeção cônica (perspectiva). Right/Up são a base
        // ortonormal perpendicular à Direction (mesma do gizmo do cone);
        // TanOuterAngle normaliza o offset pra -1..1 na borda do cone.
        // CookieIndex: -1 = sem cookie, 0..3 = slot em u_PointCookies.
        vec3  Right;
        vec3  Up;
        float TanOuterAngle;
        int   CookieIndex;

        // Sombra omnidirecional — camada no u_PointShadowArray
        // (-1 = luz sem sombra neste frame) e bias em metros
        int   ShadowLayer;
        float ShadowBias;
    };
    uniform PointLight u_PointLights[16];
    uniform int        u_NumPointLights;

    // Sombras de Point/Spot Light — cube map array R32F no unit 28: cada
    // camada guarda a distância linear até a luz (normalizada pelo raio)
    // vista de dentro dela. Uma textura só pra até 4 luzes sombreadas.
    uniform samplerCubeArray u_PointShadowArray;

    // Cookies de Point Light — limite de 4 simultâneas na cena (texturas
    // são caras de bindar; além desse número a luz funciona normal, só
    // sem o padrão projetado)
    uniform sampler2D u_PointCookies[4];

    // ── Interior Volumes ─────────────────────────────────────────────────
    // Caixas (OBB) que bloqueiam a luz "de fora" (sol direto + ambient +
    // IBL) para dentro de ambientes fechados — resolve o light leaking do
    // ambient/IBL, que é aplicado em todos os pixels sem respeitar paredes.
    // Point Lights, partículas e emissive NÃO são afetados: a iluminação
    // interna fica a cargo delas. Máximo de 8 volumes simultâneos.
    uniform int   u_NumInteriorVolumes;
    uniform mat4  u_InteriorWorldToLocal[8]; // world → local da caixa (sem escala)
    uniform vec3  u_InteriorHalfExtents[8];  // metade do tamanho mundial
    uniform float u_InteriorIntensity[8];    // 0=sem efeito, 1=bloqueio total
    uniform float u_InteriorBlend[8];        // transição na borda, em metros
    uniform int   u_InteriorAffect[8];       // bit 0 = luz direta, bit 1 = ambient/IBL

    // ── Light Probes (GI-lite) ───────────────────────────────────────────
    // Grid 3D de irradiância em Spherical Harmonics L1 (4 texturas RGBA16F
    // — SH0 rgb + visibilidade do céu no alpha; SH1 x/y/z). Dentro do
    // volume, a irradiância das probes SUBSTITUI o IBL difuso global do
    // céu — probes em salas fechadas só enxergaram paredes escuras no
    // bake, então o interior escurece sozinho, sem volume manual. A
    // interpolação trilinear entre probes é de graça (sampler3D linear).
    // Até 2 volumes simultâneos (4 sampler3D cada — units 16-23).
    // Sobrepostos combinam por média ponderada pelo peso do feather.
    // Indexar sampler arrays com a variável do loop é legal: o índice é
    // "dynamically uniform" (mesmo valor pra todos os fragments do draw).
    uniform int       u_NumProbeVolumes;
    uniform mat4      u_ProbeWorldToLocal[2]; // sem escala (escala → HalfExtents)
    uniform vec3      u_ProbeHalfExtents[2];
    uniform float     u_ProbeIntensity[2];
    uniform float     u_ProbeFeather[2];      // transição na borda (m)
    uniform int       u_ProbeOccludeSun[2];   // Occlusion Probes por volume
    uniform sampler3D u_ProbeSH0[2];
    uniform sampler3D u_ProbeSH1X[2];
    uniform sampler3D u_ProbeSH1Y[2];
    uniform sampler3D u_ProbeSH1Z[2];

    // ── Reflection Probes ────────────────────────────────────────────────
    // Cubemaps locais pré-filtrados (GGX, 5 mips) — units 24-27. Dentro da
    // caixa de influência, SUBSTITUEM o reflexo do céu no especular: as SH
    // resolvem o difuso, o cubemap resolve o reflexo mostrando as PAREDES
    // da sala. Box projection ancora o reflexo por paralaxe.
    uniform int         u_NumReflProbes;
    uniform mat4        u_ReflWorldToLocal[4]; // sem escala
    uniform vec3        u_ReflHalfExtents[4];
    uniform vec3        u_ReflPosition[4];     // ponto de captura (mundo)
    uniform float       u_ReflIntensity[4];
    uniform float       u_ReflFeather[4];
    uniform int         u_ReflBoxProj[4];
    uniform samplerCube u_ReflCube[4];

    const float PI = 3.14159265359;

    float DistributionGGX(vec3 N, vec3 H, float roughness)
    {
        float a  = roughness * roughness;
        float a2 = a * a;
        float NdotH = max(dot(N, H), 0.0);
        float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
        return a2 / (PI * denom * denom);
    }

    float GeometrySchlickGGX(float NdotV, float roughness)
    {
        float r = roughness + 1.0;
        float k = (r * r) / 8.0;
        return NdotV / (NdotV * (1.0 - k) + k);
    }

    float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
    {
        return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
               GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
    }

    vec3 FresnelSchlick(float cosTheta, vec3 F0)
    {
        return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  PCSS_V1 — disco de Poisson
    //
    //  16 pontos distribuidos de forma quase-uniforme mas NAO regular. Uma
    //  grade regular com raio variavel produz padrao de moire visivel na
    //  penumbra.
    //
    //  ── SHADOW_ROTDISK_V1 — CORRECAO DO QUE ESTE COMENTARIO PROMETIA ────
    //
    //  O Poisson resolve o moire da GRADE, e so ele. Com a tabela FIXA, os
    //  mesmos 16 offsets sao usados em todo pixel da tela, entao o erro de
    //  amostragem continua sendo uma FUNCAO DETERMINISTICA DA POSICAO.
    //
    //  Onde o shadow map esta sub-amostrado — chao visto em angulo rasante,
    //  cascade longe onde um texel cobre dezenas de centimetros — funcao
    //  deterministica da posicao e exatamente a receita de um padrao de
    //  interferencia. E o "efeito de agua" / as ondas concentricas no chao.
    //
    //  A correcao e girar o disco por um angulo diferente em cada pixel. O
    //  erro nao diminui: ele deixa de ser correlacionado entre pixels
    //  vizinhos e vira granulado fino, que o olho le como textura em vez de
    //  como estrutura. E o que a Unreal faz.
    //
    //  O angulo vem de Interleaved Gradient Noise (Jorge Jimenez), que e
    //  barato e distribui bem em blocos pequenos. Em ESPACO DE TELA de
    //  proposito: assim o TAA, que trabalha em espaco de tela, consegue
    //  integrar o granulado entre frames e some com ele. Sem TAA o
    //  granulado fica estatico na tela — ainda muito melhor que a onda.
    // ═════════════════════════════════════════════════════════════════════
    float ShadowDitherAngle(vec2 fragCoord)
    {
        float n = fract(52.9829189 *
                        fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
        return n * 6.28318530718;
    }

    const vec2 kPoisson16[16] = vec2[](
        vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
        vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
        vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
        vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
        vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790));

    // ── SHADOW_HWPCF_V1 — um tap com comparacao no hardware ──────────────
    //
    // Devolve OCLUSAO (1 = na sombra). O sampler devolve 1 = iluminado, entao
    // inverte. Cada chamada ja custa 4 texels filtrados bilinearmente pela
    // unidade de textura, pelo preco de um.
    float ShadowTap(int cascade, vec2 uv, float refZ)
    {
        return 1.0 - texture(u_ShadowMapCSM, vec4(uv, float(cascade), refZ));
    }

    // PCF 3x3 em uma cascade específica. Com o tap ja filtrado, os 9 pontos
    // cobrem na pratica 4x4 texels com peso bilinear — antes eram 9 decisoes
    // duras de 0 ou 1.
    float PCFShadow(int cascade, vec3 projCoords, float bias)
    {
        float shadow = 0.0;
        vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMapCSM, 0).xy);
        float refZ = projCoords.z - bias;
        for (int x = -1; x <= 1; x++)
            for (int y = -1; y <= 1; y++)
                shadow += ShadowTap(cascade,
                                    projCoords.xy + vec2(x, y) * texelSize, refZ);
        return shadow / 9.0;
    }

    // ═════════════════════════════════════════════════════════════════════
    //  CSM_BLEND_V1 — amostra UMA cascade
    //
    //  Extraido para funcao porque agora ele e chamado DUAS vezes na faixa de
    //  transicao (esta cascade e a proxima), para poder misturar as duas.
    // ═════════════════════════════════════════════════════════════════════
    // ═════════════════════════════════════════════════════════════════════
    //  PCSS_V1 — PENUMBRA QUE CRESCE COM A DISTANCIA
    //
    //  ── O QUE ESTAVA FALTANDO ──────────────────────────────────────────
    //
    //  O PCF de raio fixo borra a sombra IGUALMENTE em todo lugar. Sombra de
    //  sol de verdade nao e assim: no ponto em que o pe encosta no chao ela e
    //  quase perfeitamente nitida, e vai abrindo conforme o objeto se afasta
    //  da superficie. E o sinal mais forte de "isto esta apoiado ali" que uma
    //  imagem ao ar livre tem — e a ausencia dele e boa parte do que o olho
    //  acusa como "sombra de jogo".
    //
    //  A causa fisica e o TAMANHO ANGULAR do sol (0.53 grau). Uma fonte
    //  pontual daria borda dura a qualquer distancia.
    //
    //  ── COMO E CALCULADO, EM TRES PASSOS ────────────────────────────────
    //
    //  1. BUSCA DE BLOQUEADOR: amostra o mapa numa vizinhanca e tira a
    //     profundidade MEDIA do que esta na frente deste ponto.
    //  2. LARGURA DA PENUMBRA: a distancia entre bloqueador e receptor,
    //     convertida para metros pelo DepthRange da cascade, vezes o tamanho
    //     angular do sol.
    //  3. PCF com esse raio, em texeis desta cascade.
    //
    //  Sem bloqueador nenhum, sai cedo com "iluminado" — e o caso da maior
    //  parte da tela, entao o custo medio fica bem abaixo do pior caso.
    // ═════════════════════════════════════════════════════════════════════
    float PCSSShadow(int cascade, vec3 projCoords, float bias)
    {
        vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMapCSM, 0).xy);

        // SHADOW_ROTDISK_V1 — o disco inteiro gira por pixel. O MESMO angulo
        // serve a busca e ao filtro (girar duas vezes nao acrescenta nada e
        // custaria outro seno).
        float ang = ShadowDitherAngle(gl_FragCoord.xy);
        float cs = cos(ang), sn = sin(ang);
        mat2 rot = mat2(cs, -sn, sn, cs);

        // Raio da busca: proporcional ao maior borrao que este sol pode
        // produzir, limitado pelo teto. Buscar em toda a tela seria correto e
        // inviavel.
        float searchTexels = max(u_MaxPenumbraTexels * 0.5, 2.0);

        float refZ = projCoords.z - bias;

        float blockerSum = 0.0;
        int   blockers   = 0;

        for (int i = 0; i < 16; i++)
        {
            // Profundidade CRUA — sampler sem comparacao e sem filtro. Aqui o
            // valor e lido como numero (a que distancia esta o bloqueador),
            // entao interpolar seria erro (ver SHADOW_HWPCF_V1).
            vec2 uv = projCoords.xy + (rot * kPoisson16[i]) * searchTexels * texelSize;
            float d = texture(u_ShadowMapRaw, vec3(uv, float(cascade))).r;
            if (d < refZ)
            {
                blockerSum += d;
                blockers++;
            }
        }

        if (blockers == 0) return 0.0;   // nada na frente: iluminado

        float avgBlocker = blockerSum / float(blockers);

        // Distancia REAL, em metros, entre quem projeta e quem recebe.
        float gapMeters = (projCoords.z - avgBlocker) * u_CascadeDepthRange[cascade];

        // Largura da penumbra pelo tamanho angular da fonte, convertida para
        // texeis desta cascade.
        float penumbraMeters = gapMeters * u_SunAngularRadius;
        float radiusTexels = penumbraMeters / max(u_CascadeTexelWorld[cascade], 1e-6);

        // Piso de 1 texel: abaixo disso nao ha o que filtrar, e a borda ficaria
        // serrilhada em vez de nitida.
        radiusTexels = clamp(radiusTexels, 1.0, u_MaxPenumbraTexels);

        float shadow = 0.0;
        for (int i = 0; i < 16; i++)
        {
            vec2 uv = projCoords.xy + (rot * kPoisson16[i]) * radiusTexels * texelSize;
            shadow += ShadowTap(cascade, uv, refZ);
        }
        return shadow / 16.0;
    }

    float SampleCascade(int cascade, vec3 fragPos, vec3 normal, vec3 lightDir)
    {
        // ── SHADOW_ACNE_V1 — NORMAL OFFSET ────────────────────────────────
        //
        // Desloca o PONTO DE AMOSTRAGEM ao longo da normal antes de projetar
        // na luz, em vez de mexer na profundidade comparada.
        //
        // A acne acontece porque um texel do shadow map cobre uma area do
        // mundo, e dentro dessa area a superficie sobe e desce — metade dela
        // fica acima da profundidade gravada e se auto-sombreia. Empurrar o
        // ponto para FORA da superficie por cerca de um texel tira a amostra
        // dessa zona de duvida.
        //
        // Por que e melhor que bias de profundidade: bias grande o bastante
        // para matar a acne descola a sombra do pe do objeto (peter-panning).
        // O deslocamento por normal e LATERAL — nao mexe em quem esta na
        // frente de quem, entao o contato continua no lugar.
        //
        // A escala vem do tamanho REAL do texel desta cascade: o mesmo valor
        // fixo seria pouco perto e demais longe. E cresce em superficie
        // rasante, que e onde a area coberta por texel e maior.
        float ndl = clamp(dot(normal, lightDir), 0.0, 1.0);
        float offsetScale = u_CascadeTexelWorld[cascade] * 1.5 * (2.0 - ndl);
        vec3  offsetPos = fragPos + normal * offsetScale;

        vec4 fragPosLS  = u_LightSpaceMatrixCSM[cascade] * vec4(offsetPos, 1.0);
        vec3 projCoords = fragPosLS.xyz / fragPosLS.w * 0.5 + 0.5;
        if (projCoords.z > 1.0) return 0.0;

        // ═════════════════════════════════════════════════════════════════
        //  SHADOW_BIAS_METERS_V1 — O BIAS PASSA A TER UNIDADE
        //
        //  ── O QUE ESTAVA ERRADO ────────────────────────────────────────
        //
        //  projCoords.z e uma FRACAO do intervalo de profundidade desta
        //  cascade — e esse intervalo e completamente diferente em cada uma.
        //  Medido na configuracao real (2048px, lambda 0.75, zMult 5):
        //
        //    Shadow Distance  50 -> cascade 0 cobre  27 m, cascade 3  367 m
        //    Shadow Distance 200 -> cascade 0 cobre 100 m, cascade 3 1474 m
        //
        //  O mesmo u_ShadowBias = 0.005 valia, portanto, 14 cm na cascade 0 e
        //  1,8 METRO na cascade 3 — e ao trocar o Shadow Distance de 50 para
        //  200 todos esses numeros multiplicavam por ~4 de uma vez.
        //
        //  O `biasScale = cascade*0.5+1` era um chute para compensar: dava
        //  2,5x entre a primeira e a ultima cascade, quando a razao real e
        //  ~15x. E por isso que UM slider de Shadow Bias nunca conseguia
        //  ficar certo nas 4 cascades ao mesmo tempo, e por isso que mexer no
        //  Shadow Distance obrigava a re-ajustar o bias.
        //
        //  ── COMO FICA ──────────────────────────────────────────────────
        //
        //  O bias e calculado em METROS e so no fim convertido para a escala
        //  desta cascade. Duas parcelas:
        //
        //  1. TEXEL x INCLINACAO. Dentro de um texel a superficie sobe ou
        //     desce, e quanto mais rasante a luz, mais ela sobe. A variacao
        //     maxima e o tamanho do texel vezes a tangente do angulo — que e
        //     a quantidade EXATA que precisa ser compensada. Como isso ja
        //     usa o texel REAL da cascade, a parcela se ajusta sozinha em
        //     todas as 4 e em qualquer Shadow Distance.
        //
        //  2. u_ShadowBias, agora um piso constante EM METROS, para a
        //     precisao do proprio depth buffer. E o unico numero que sobra
        //     para o artista, e ele passa a significar sempre a mesma coisa.
        //
        //  A tangente e limitada em 4 (~76 graus): sem teto, luz rasante
        //  manda o bias para o infinito e a sombra descola do objeto.
        // ═════════════════════════════════════════════════════════════════
        float slopeTan = min(sqrt(1.0 - ndl * ndl) / max(ndl, 0.15), 4.0);
        float biasMeters = u_ShadowBias
                         + u_CascadeTexelWorld[cascade] * (0.5 + slopeTan);

        float bias = biasMeters / max(u_CascadeDepthRange[cascade], 1e-4);

        // PCSS_V1 — SunAngularRadius em 0 volta ao PCF de raio fixo, que e
        // mais barato. E o interruptor, e ele vive no Inspector da luz.
        if (u_SunAngularRadius > 0.0)
            return PCSSShadow(cascade, projCoords, bias);

        return PCFShadow(cascade, projCoords, bias);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  CONTACT_SHADOW_V1 — A SOMBRA QUE O SHADOW MAP NAO CONSEGUE TER
    //
    //  ── O PROBLEMA QUE ELA RESOLVE ───────────────────────────────────────
    //
    //  Todo shadow map tem uma resolucao, e nela um texel cobre centimetros
    //  (perto) a dezenas de centimetros (longe). Contato menor que um texel
    //  simplesmente NAO EXISTE no mapa: o pe encostando no chao, o vinco onde
    //  a caixa toca a parede, a base de um poste. O olho le isso como o objeto
    //  FLUTUANDO — e nenhum ajuste de bias, cascade ou penumbra conserta,
    //  porque a informacao nunca foi gravada.
    //
    //  ── COMO FUNCIONA ────────────────────────────────────────────────────
    //
    //  Em vez de consultar um mapa, marcha um raio curto do fragmento EM
    //  DIRECAO AO SOL, projeta cada passo na tela e pergunta ao G-Buffer de
    //  posicao o que esta visivel ali. Se a superficie visivel esta NA FRENTE
    //  do ponto marchado, alguma coisa bloqueia o sol — e essa alguma coisa
    //  tem tamanho de pixel, nao de texel de shadow map.
    //
    //  E o `Contact Shadow Length` da Unreal, e o motivo de existir la e o
    //  mesmo: complementar o shadow map na escala em que ele nao alcanca.
    //
    //  ── OS TRES CUIDADOS QUE FAZEM A DIFERENCA ───────────────────────────
    //
    //  1. JANELA DE ESPESSURA. Se a superficie visivel esta MUITO mais perto,
    //     ela nao e um bloqueador — e outro objeto na frente, sem relacao. Sem
    //     essa janela aparece uma auréola escura em volta de tudo, que e o
    //     defeito classico de sombra em espaco de tela.
    //
    //  2. DITHER. Sem jitter, 12 passos fixos viram 12 degraus visiveis. Com
    //     jitter o mesmo erro vira granulado, que o TAA integra e some.
    //
    //  3. FUNDO. Posicao (0,0,0) no G-Buffer e ceu, nao geometria na origem
    //     do mundo — o mesmo cuidado do SSAO_ROBUST_V1.
    // ═════════════════════════════════════════════════════════════════════
    float ContactShadow(vec3 fragPos, vec3 N, vec3 L)
    {
        if (u_ContactShadowLength <= 0.0) return 0.0;

        const int kSteps = 12;
        float stepLen = u_ContactShadowLength / float(kSteps);

        // Fracao pseudo-aleatoria por pixel, reaproveitando o ruido do disco
        // de sombra (2*pi -> 0..1).
        float jitter = fract(ShadowDitherAngle(gl_FragCoord.xy) * 0.15915494);

        // Sai da propria superficie antes de comecar, senao o primeiro passo
        // acha o proprio fragmento e tudo fica sombreado.
        vec3 origin = fragPos + N * (stepLen * 0.5);

        // Janela de espessura: bloqueador plausivel esta ENTRE estes dois.
        float minDiff = stepLen * 0.25;
        float maxDiff = stepLen * 4.0;

        for (int i = 1; i <= kSteps; ++i)
        {
            vec3 p = origin + L * (stepLen * (float(i) + jitter));

            vec4 clip = u_ViewProjection * vec4(p, 1.0);
            if (clip.w <= 0.0) break;                       // atras da camera
            vec2 ndc = clip.xy / clip.w;
            if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0) break; // saiu da tela
            vec2 uv = ndc * 0.5 + 0.5;

            vec3 surf = texture(u_Position, uv).xyz;
            if (dot(surf, surf) < 1e-6) continue;            // ceu

            float diff = length(p - u_CameraPosition)
                       - length(surf - u_CameraPosition);

            if (diff > minDiff && diff < maxDiff) return 1.0;
        }
        return 0.0;
    }

    float ShadowCalculation(vec3 fragPos, vec3 normal, vec3 lightDir)
    {
        // ── CSM — seleciona cascade pela profundidade view space ───────────
        if (u_CascadeCount > 0)
        {
            vec4 fragViewPos = u_View * vec4(fragPos, 1.0);
            float depth = abs(fragViewPos.z);

            int cascade = u_CascadeCount - 1;
            for (int i = 0; i < u_CascadeCount; ++i)
            {
                if (depth < u_CascadeSplitDepths[i])
                {
                    cascade = i;
                    break;
                }
            }

            float shadow = SampleCascade(cascade, fragPos, normal, lightDir);

            // ═══════════════════════════════════════════════════════════════
            //  CSM_BLEND_V1 — A METADE QUE FALTAVA
            //
            //  O CSM_STABLE_V1 acabou com a sombra NADANDO (esfera + snap de
            //  texel). O que sobrou foi a EMENDA entre cascades: a escolha era
            //  um corte seco, e cada cascade tem um tamanho de texel diferente.
            //  Na linha exata do split, a sombra mudava de nitidez e de
            //  posicao de um pixel para o vizinho — e como essa linha e uma
            //  DISTANCIA DA CAMERA, ela varre o chao conforme voce anda. O
            //  olho le isso como "a sombra se mexeu".
            //
            //  E por isso que aumentar o Shadow Distance de 50 para 200
            //  "melhorou um pouco" sem resolver: empurrou os splits para
            //  longe, entao a emenda passou a cruzar o chao mais longe de
            //  voce — mas ela continua la, e agora com menos resolucao.
            //
            //  Aqui as duas cascades sao amostradas na faixa final de cada uma
            //  e misturadas. A emenda deixa de ser uma linha e vira uma
            //  transicao que o olho nao acha.
            //
            //  Custa um segundo PCF 3x3 SOMENTE nos ~12% finais de cada
            //  cascade — o resto da tela continua com uma amostra so.
            // ═══════════════════════════════════════════════════════════════
            const float kBlendFraction = 0.12;

            if (cascade < u_CascadeCount - 1)
            {
                float splitEnd   = u_CascadeSplitDepths[cascade];
                float splitBegin = (cascade == 0) ? 0.0 : u_CascadeSplitDepths[cascade - 1];
                float range      = max(splitEnd - splitBegin, 1e-4);

                float bandStart = splitEnd - range * kBlendFraction;

                if (depth > bandStart)
                {
                    float t = clamp((depth - bandStart) / max(splitEnd - bandStart, 1e-4),
                                    0.0, 1.0);
                    float next = SampleCascade(cascade + 1, fragPos, normal, lightDir);
                    shadow = mix(shadow, next, t);
                }
            }

            return shadow;
        }

        // ── Fallback: shadow map simples ───────────────────────────────────
        vec4 fragPosLS  = u_LightSpaceMatrix * vec4(fragPos, 1.0);
        vec3 projCoords = fragPosLS.xyz / fragPosLS.w * 0.5 + 0.5;
        if (projCoords.z > 1.0) return 0.0;

        float bias = max(u_ShadowBias * (1.0 - dot(normal, lightDir)), u_ShadowBias);
        float shadow = 0.0;
        vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
        for (int x = -2; x <= 2; x++)
            for (int y = -2; y <= 2; y++)
            {
                float pcf = texture(u_ShadowMap,
                    projCoords.xy + vec2(x, y) * texelSize).r;
                shadow += projCoords.z - bias > pcf ? 1.0 : 0.0;
            }
        return shadow / 25.0;
    }

    // Quanto o fragmento está "dentro" do volume i: 0.0 fora, 1.0 bem
    // dentro. Usa a SDF (distância assinada) da caixa — negativa dentro,
    // positiva fora — e esmaece ao longo de BlendDistance PARA DENTRO da
    // caixa: a face da caixa fica no vão da porta/janela e a luz externa
    // "vaza" suavemente por BlendDistance metros pro interior.
    float InteriorMask(vec3 fragPos, int i)
    {
        vec3 local = (u_InteriorWorldToLocal[i] * vec4(fragPos, 1.0)).xyz;
        vec3 d = abs(local) - u_InteriorHalfExtents[i];
        float dist = length(max(d, vec3(0.0))) + min(max(d.x, max(d.y, d.z)), 0.0);
        return 1.0 - smoothstep(-u_InteriorBlend[i], 0.0, dist);
    }

    // Calcula quanto da luz externa sobrevive neste fragmento — separado
    // em luz direta (sol) e ambient/IBL. Vários volumes sobrepostos
    // combinam pelo mínimo (o mais escuro vence).
    void InteriorFactors(vec3 fragPos, out float directFactor, out float ambientFactor)
    {
        directFactor  = 1.0;
        ambientFactor = 1.0;
        for (int i = 0; i < u_NumInteriorVolumes; i++)
        {
            float survive = 1.0 - InteriorMask(fragPos, i) * u_InteriorIntensity[i];
            if ((u_InteriorAffect[i] & 1) != 0) directFactor  = min(directFactor,  survive);
            if ((u_InteriorAffect[i] & 2) != 0) ambientFactor = min(ambientFactor, survive);
        }
    }

    // Avalia TODOS os Probe Volumes no fragmento — peso combinado (0..1,
    // com feather), irradiância SH L1 e visibilidade do céu como MÉDIA
    // PONDERADA pelos pesos (sobreposições fazem crossfade natural), e a
    // oclusão de sol por volume (Occlusion Probes). Reconstrução com a
    // convolução cosseno padrão (A0=pi, A1=2pi/3), dividida por PI —
    // mesma semântica do irradiance map do IBL.
    void EvalProbeVolumes(vec3 fragPos, vec3 N,
        out float weight, out vec3 irradiance, out float skyVis, out float sunOcc)
    {
        weight = 0.0; irradiance = vec3(0.0); skyVis = 1.0; sunOcc = 1.0;
        float total = 0.0;
        vec3  accIrr = vec3(0.0);
        float accSky = 0.0;
        float accOcc = 0.0;

        for (int i = 0; i < u_NumProbeVolumes; i++)
        {
            vec3 local = (u_ProbeWorldToLocal[i] * vec4(fragPos, 1.0)).xyz;
            vec3 d = abs(local) - u_ProbeHalfExtents[i];
            float dist = length(max(d, vec3(0.0))) + min(max(d.x, max(d.y, d.z)), 0.0);
            float w = 1.0 - smoothstep(-u_ProbeFeather[i], 0.0, dist);
            if (w <= 0.0) continue;

            // Probes nos CENTROS das células → texel centers → trilinear
            // do sampler é a interpolação entre probes, de graça
            vec3 uvw = clamp(local / (2.0 * u_ProbeHalfExtents[i]) + 0.5, 0.0, 1.0);

            vec4 sh0  = texture(u_ProbeSH0[i],  uvw);
            vec3 sh1x = texture(u_ProbeSH1X[i], uvw).rgb;
            vec3 sh1y = texture(u_ProbeSH1Y[i], uvw).rgb;
            vec3 sh1z = texture(u_ProbeSH1Z[i], uvw).rgb;

            const float Y00 = 0.282095;
            const float Y1  = 0.488603;
            const float A0  = 3.14159265;
            const float A1  = 2.09439510;

            vec3 E = Y00 * A0 * sh0.rgb
                   + Y1  * A1 * (sh1x * N.x + sh1y * N.y + sh1z * N.z);
            vec3 irr = max(E, vec3(0.0)) / PI * u_ProbeIntensity[i];

            // Occlusion Probes: skyVis geométrica oclui o sol direto.
            // O x2 corrige o exterior (chão come metade do céu).
            float occ = (u_ProbeOccludeSun[i] == 1)
                ? min(sh0.a * 2.0, 1.0) : 1.0;

            accIrr += irr * w;
            accSky += sh0.a * w;
            accOcc += occ * w;
            total  += w;
        }

        if (total > 0.0)
        {
            irradiance = accIrr / total;
            skyVis     = accSky / total;
            sunOcc     = accOcc / total;
            weight     = min(total, 1.0);
        }
    }

    // Reflexo local — combina as Reflection Probes cuja caixa contém o
    // fragmento (média ponderada; loop de índice dynamically uniform).
    // Box projection: intersecta o raio refletido com a caixa em espaço
    // LOCAL e sampleia o cubemap na direção hit→pontoDeCaptura — a
    // inversa da rotação é a transposta da 3x3 (WorldToLocal não tem
    // escala), então tudo custa uma mat3 extra.
    void EvalReflectionProbes(vec3 fragPos, vec3 R, float roughness,
        out float weight, out vec3 specular)
    {
        weight = 0.0; specular = vec3(0.0);
        float total = 0.0;

        for (int i = 0; i < u_NumReflProbes; i++)
        {
            mat3 rot = mat3(u_ReflWorldToLocal[i]);
            vec3 lp = (u_ReflWorldToLocal[i] * vec4(fragPos, 1.0)).xyz;

            vec3 d = abs(lp) - u_ReflHalfExtents[i];
            float dist = length(max(d, vec3(0.0))) + min(max(d.x, max(d.y, d.z)), 0.0);
            float w = 1.0 - smoothstep(-u_ReflFeather[i], 0.0, dist);
            if (w <= 0.0) continue;

            vec3 dir = R;
            if (u_ReflBoxProj[i] == 1)
            {
                vec3 lr = rot * R;
                // interseção raio-caixa (slab method, saída)
                vec3 t1 = ( u_ReflHalfExtents[i] - lp) / lr;
                vec3 t2 = (-u_ReflHalfExtents[i] - lp) / lr;
                vec3 tmax = max(t1, t2);
                float t = min(min(tmax.x, tmax.y), tmax.z);
                vec3 lProbe = (u_ReflWorldToLocal[i] * vec4(u_ReflPosition[i], 1.0)).xyz;
                dir = transpose(rot) * ((lp + lr * t) - lProbe);
            }

            // 5 mips GGX → lod máximo 4.0 (mesma escala do IBL global)
            vec3 c = textureLod(u_ReflCube[i], dir, roughness * 4.0).rgb
                   * u_ReflIntensity[i];
            specular += c * w;
            total += w;
        }

        if (total > 0.0)
        {
            specular /= total;
            weight = min(total, 1.0);
        }
    }

    // Sombra omnidirecional com PCF de 4 taps: compara a distância real
    // do fragmento até a luz contra a gravada no cubemap (backfaces —
    // o cull front do depth pass já empurra a superfície de comparação
    // pra dentro do objeto, o bias só cobre o resto).
    float PointShadow(int layer, float bias, vec3 lightPos, float radius, vec3 fragPos)
    {
        vec3 fragToLight = fragPos - lightPos;
        float dist = length(fragToLight);
        if (dist >= radius) return 0.0;

        vec3 dir = normalize(fragToLight);
        const vec3 offs[4] = vec3[](
            vec3( 1,  1,  0), vec3(-1,  1, 0),
            vec3( 1, -1,  0), vec3(-1, -1, 0));
        const float diskRadius = 0.012;

        float shadow = 0.0;
        for (int k = 0; k < 4; k++)
        {
            float closest = texture(u_PointShadowArray,
                vec4(dir + offs[k] * diskRadius, float(layer))).r * radius;
            shadow += (dist - bias > closest) ? 1.0 : 0.0;
        }
        return shadow * 0.25;
    }

    // SHADING_MODEL_V1 — `isToon`/`toonSteps` entraram na assinatura para que
    // uma luz de ponto sobre um material toon tambem caia em bandas. Sem isso,
    // o personagem ficaria celula sob o sol e PBR liso sob um poste — o mesmo
    // material com duas aparencias, dependendo de qual luz o alcanca.
    vec3 CalcPointLight(PointLight pl, vec3 fragPos, vec3 N, vec3 V,
                        vec3 albedo, float metallic, float roughness, vec3 F0,
                        bool isToon, float toonSteps)
    {
        vec3  L    = normalize(pl.Position - fragPos);
        vec3  H    = normalize(V + L);
        float dist = length(pl.Position - fragPos);

        // Atenuação física com smooth falloff no radius
        float att  = clamp(1.0 - (dist / pl.Radius), 0.0, 1.0);
        att        = att * att;

        // Atenuação do cone (Spot Light) — theta é o cosseno do ângulo
        // entre a direção da luz (apontando PARA o fragmento, por isso o
        // -L) e o eixo do cone. Fora do OuterCutoff = 0 (escuro); dentro
        // do InnerCutoff = 1 (intensidade máxima); entre os dois, suaviza.
        if (pl.IsSpot == 1)
        {
            // theta: 1.0 = fragmento diretamente na direção do cone,
            //        <OuterCutoff = fora do cone.
            float theta   = dot(-L, normalize(pl.Direction));
            // smoothstep: 0 quando theta < OuterCutoff, 1 quando
            // theta > InnerCutoff, transição suave entre os dois.
            // Mais robusto que a divisão por epsilon — funciona mesmo
            // quando Inner == Outer (corte duro).
            float coneAtt = smoothstep(pl.OuterCutoff, pl.InnerCutoff, theta);
            att *= coneAtt;
        }

        vec3 radiance = pl.Color * pl.Intensity * att;

        // SHADING_MODEL_V1 — mesma regra do sol: quantiza a LUZ (aqui, o
        // NdotL ja atenuado pela distancia e pelo cone), nunca a cor.
        if (isToon)
        {
            float lit  = max(dot(N, L), 0.0) * att;
            float band = floor(clamp(lit, 0.0, 1.0) * toonSteps + 0.5) / toonSteps;

            float specCut  = mix(0.995, 0.75, roughness);
            float toonSpec = step(specCut, pow(max(dot(N, H), 0.0), 64.0))
                           * (1.0 - roughness) * band;

            // `pl.Color * pl.Intensity` e nao `radiance`: a atenuacao ja
            // entrou no `band`, e aplica-la duas vezes apagaria a luz.
            return (albedo * band + vec3(toonSpec)) * pl.Color * pl.Intensity;
        }

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        vec3  numerator   = NDF * G * F;
        float NdotL       = max(dot(N, L), 0.0);
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3  specular    = numerator / denominator;

        return (kD * albedo / PI + specular) * radiance * NdotL;
    }

    void main()
    {
        vec3 fragPos = texture(u_Position, v_TexCoord).rgb;
        vec3 N       = normalize(texture(u_Normal, v_TexCoord).rgb);

        vec4  albedoM   = texture(u_Albedo, v_TexCoord);
        vec3  albedo    = albedoM.rgb;
        float metallic  = albedoM.a;

        // SHADING_MODEL_V1 — o texel do G-Buffer 3 carrega QUATRO coisas:
        //   .r roughness   .g ambient occlusion
        //   .b id do shading model   .a bandas do toon
        // Os dois ultimos eram canais alocados e sem uso. Ver a nota longa em
        // axe/material/material_cooked.hpp.
        vec4  pbr       = texture(u_PBR, v_TexCoord);
        float roughness = pbr.r;
        float matAO     = pbr.g;

        // O +0.5 nao e decoracao: sem ele, um id 2 que voltasse do UNORM como
        // 1.9999 viraria 1 — o material seria Unlit em vez de Toon.
        int   shadingModel = int(pbr.b * 255.0 + 0.5);
        float toonSteps    = max(floor(pbr.a * 16.0 + 0.5), 2.0);

        float ao = matAO;
        if (u_HasSSAO == 1)
            ao *= texture(u_SSAO, v_TexCoord).r;

        // Modo debug — mostra só a textura de oclusão em cinza
        // Se SSAO não está disponível, mostra vermelho como indicador
        if (u_SSAODebug == 1)
        {
            if (u_HasSSAO == 1)
            {
                float ssaoVal = texture(u_SSAO, v_TexCoord).r;
                FragColor = vec4(vec3(ssaoVal), 1.0);
            }
            else
            {
                FragColor = vec4(1.0, 0.0, 0.0, 1.0); // vermelho = SSAO não disponível
            }
            return;
        }

        // ── SHADING_MODEL_V1 — UNLIT ─────────────────────────────────────────
        //
        // Sai ANTES de tudo: nem sombra, nem ambient, nem IBL, nem probes.
        // Unlit quer dizer que o albedo E a cor final — e a base de cartoon
        // chapado, de VFX, de interface no mundo e de qualquer material que
        // precise ter exatamente a cor que o artista pintou.
        //
        // Aqui em cima, e nao no fim com um mix, porque assim o pixel Unlit
        // custa quase nada: pula o Cook-Torrance, o loop de point lights, a
        // avaliacao de probes e a de reflection probes.
        if (shadingModel == 1)
        {
            FragColor = vec4(albedo + texture(u_Emissive, v_TexCoord).rgb, 1.0);
            return;
        }

        vec3 V = normalize(u_CameraPosition - fragPos);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        // SHADING_MODEL_V1 — atalho de leitura para o resto do main.
        bool isToon = (shadingModel == 2);

        // Interior Volumes — quanto da luz externa (sol + ambient/IBL)
        // chega neste fragmento. 1.0 = fragmento fora de qualquer volume.
        float interiorDirect  = 1.0;
        float interiorAmbient = 1.0;
        if (u_NumInteriorVolumes > 0)
            InteriorFactors(fragPos, interiorDirect, interiorAmbient);

        // Light Probes — peso, irradiância, visibilidade do céu e oclusão
        // de sol (Occlusion Probes) combinados de todos os volumes
        float probeW = 0.0;
        vec3  probeIrr = vec3(0.0);
        float probeSkyVis = 1.0;
        float probeOcc = 1.0;
        EvalProbeVolumes(fragPos, N, probeW, probeIrr, probeSkyVis, probeOcc);
        // ═════════════════════════════════════════════════════════════════
        //  PROBE_SUN_AUTHORITY_V1 — CADA SISTEMA NO SEU LUGAR
        //
        //  ── O SINTOMA ─────────────────────────────────────────────────────
        //
        //  O personagem "pulava" do escuro para o claro ao andar, e a fronteira
        //  entre sombra e luz parecia um PLANO liso que nao correspondia a
        //  objeto nenhum.
        //
        //  ── A CAUSA ───────────────────────────────────────────────────────
        //
        //  probeOcc vem de sh0.a — a visibilidade GEOMETRICA do ceu bakeada em
        //  cada probe — e estava multiplicando o SOL DIRETO em todo pixel
        //  dentro do volume. So que o grid e ESPARSO (6x3x6 sobre a caixa
        //  inteira): entre uma probe e a vizinha ha metros. O sol entao acendia
        //  e apagava na resolucao do GRID, com interpolacao trilinear — o que
        //  na tela vira exatamente aquilo: um degrade largo e liso, sem relacao
        //  com a geometria, e um personagem que muda de iluminacao aos saltos.
        //
        //  E pior: isso DISPUTA com o shadow map. Onde o shadow map diz
        //  "iluminado" e a probe diz "ocluido", aparece uma sombra que nao e a
        //  sombra de nada.
        //
        //  ── COMO A UNREAL FAZ ─────────────────────────────────────────────
        //
        //  A Unreal NAO usa probes para ocluir sol direto. La a oclusao do sol
        //  e sempre do shadow map (CSM por pixel, continuo) e, mais longe,
        //  Distance Field Shadows. O volumetric lightmap da INDIRETA e so isso.
        //  Occlusion Probes e uma ideia da Unity, e la ela existe para luz
        //  BAKEADA que nao tem sombra em tempo real nenhuma.
        //
        //  ── A CORRECAO ────────────────────────────────────────────────────
        //
        //  A oclusao por probe passa a agir SO onde o shadow map nao alcanca —
        //  alem do Shadow Distance. Dentro do alcance, quem manda no sol e o
        //  shadow map, que e por pixel e continuo. A troca e suave nos ultimos
        //  20% da distancia, entao nao ha linha nova.
        //
        //  Interior fechado dentro do alcance nao perde nada: quem trata esse
        //  caso e o Interior Volume (interiorDirect), que e explicito.
        // ═════════════════════════════════════════════════════════════════
        float shadowAuthority = 0.0;
        if (u_HasShadowMap == 1 && u_CascadeCount > 0)
        {
            float viewDepth = abs((u_View * vec4(fragPos, 1.0)).z);
            float far = max(u_CascadeSplitDepths[u_CascadeCount - 1], 1e-3);
            shadowAuthority = 1.0 - smoothstep(far * 0.8, far, viewDepth);
        }

        float probeSunOcc = mix(mix(1.0, probeOcc, probeW), 1.0, shadowAuthority);

        // --- Luz direcional ---
        vec3  Lo     = vec3(0.0);
        float shadow = 0.0; // fora do if para ser acessível no cálculo de ambient
        if (u_HasLight == 1)
        {
            vec3 L = normalize(-u_LightDirection);
            vec3 H = normalize(V + L);

            vec3 radiance = u_LightColor * u_LightIntensity;

            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

            vec3  numerator   = NDF * G * F;
            float NdotL       = max(dot(N, L), 0.0);
            float NdotV       = max(dot(N, V), 0.0);
            float denominator = 4.0 * NdotV * NdotL + 0.0001;
            vec3  specular    = numerator / denominator;

            if (u_HasShadowMap == 1)
                shadow = ShadowCalculation(fragPos, N, L);

            // CONTACT_SHADOW_V1 — complementa, nao substitui. Pulado onde a
            // sombra do mapa ja e total (nao ha o que escurecer) e onde a
            // face nao ve o sol (NdotL 0) — as duas condicoes cobrem a maior
            // parte da tela, entao o custo medio fica bem abaixo do pior caso.
            if (shadow < 0.999 && NdotL > 0.0)
                shadow = max(shadow, ContactShadow(fragPos, N, L));

            // Cookie — projeta fragPos no plano perpendicular à direção da
            // luz (paralela, então sem perspectiva — só tiling por escala)
            vec3 dirCookieTint = vec3(1.0);
            if (u_HasDirCookie == 1)
            {
                float cu = dot(fragPos, u_DirCookieRight) / u_DirCookieScale;
                float cv = dot(fragPos, u_DirCookieUp) / u_DirCookieScale;
                dirCookieTint = texture(u_DirCookie, fract(vec2(cu, cv))).rgb;
            }

            // ── SHADING_MODEL_V1 — TOON ──────────────────────────────────────
            //
            // A quantizacao acontece no termo de LUZ (NdotL ja multiplicado
            // pela sombra e pelas oclusoes), e nao na cor final. Quantizar a
            // cor produziria posterizacao — degraus na textura e no ambient
            // tambem. Quantizar a luz produz CELULA: a textura passa inteira,
            // so a iluminacao dela e que anda em degraus. E a diferenca entre
            // parecer estilizado e parecer com pouca profundidade de cor.
            //
            // Note que sombra, Interior Volumes e Occlusion Probes entram
            // ANTES do degrau — o toon herda de graca todo o sistema de
            // sombras que ja existe, em vez de precisar do seu proprio.
            if (isToon)
            {
                float lit  = NdotL * (1.0 - shadow) * interiorDirect * probeSunOcc;
                float band = floor(clamp(lit, 0.0, 1.0) * toonSteps + 0.5) / toonSteps;

                // Especular de corte duro. O limiar vem do ROUGHNESS: liso =
                // brilho pequeno e concentrado, aspero = maior e mais suave.
                // Reusa um parametro que o artista ja mexe, em vez de gastar o
                // ultimo canal livre do G-Buffer com um slider novo.
                float specCut  = mix(0.995, 0.75, roughness);
                float toonSpec = step(specCut, pow(max(dot(N, H), 0.0), 64.0))
                               * (1.0 - roughness) * band;

                // `* band` no especular de proposito: brilho especular dentro
                // da banda de sombra e o erro classico do toon caseiro.
                Lo += (albedo * band + vec3(toonSpec)) * radiance * dirCookieTint;
            }
            else
            {
                // interiorDirect: dentro de um Interior Volume, o sol não
                // entra — independente do shadow map cobrir ou não o teto.
                Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadow) * mix(1.0, ao, 0.5) * dirCookieTint * interiorDirect * probeSunOcc;
            }
        }

        // --- Point lights --- (independente da direcional)
        for (int i = 0; i < u_NumPointLights; i++)
        {
            vec3 contribution = CalcPointLight(u_PointLights[i], fragPos, N, V, albedo, metallic, roughness, F0, isToon, toonSteps);

            // Sombra da luz — só quando ela ganhou uma camada neste frame
            if (u_PointLights[i].ShadowLayer >= 0)
                contribution *= 1.0 - PointShadow(
                    u_PointLights[i].ShadowLayer,
                    u_PointLights[i].ShadowBias,
                    u_PointLights[i].Position,
                    u_PointLights[i].Radius,
                    fragPos);

            // Cookie — só Spot Light com CookieIndex válido. Projeção
            // cônica (perspectiva): divide pelo cosseno do ângulo em
            // relação ao eixo, igual a uma divisão de perspectiva real.
            if (u_PointLights[i].IsSpot == 1 && u_PointLights[i].CookieIndex >= 0)
            {
                vec3 toFrag = normalize(fragPos - u_PointLights[i].Position);
                vec3 dir = normalize(u_PointLights[i].Direction);
                float cosAngle = dot(toFrag, dir);

                if (cosAngle > 0.0001)
                {
                    vec3 perp = toFrag - dir * cosAngle;
                    vec2 cuv = vec2(dot(perp, u_PointLights[i].Right),
                                     dot(perp, u_PointLights[i].Up))
                               / (cosAngle * max(u_PointLights[i].TanOuterAngle, 0.0001));
                    cuv = cuv * 0.5 + 0.5;

                    if (cuv.x >= 0.0 && cuv.x <= 1.0 && cuv.y >= 0.0 && cuv.y <= 1.0)
                    {
                        // Índices sempre constantes (0/1/2/3) — evita
                        // indexação dinâmica de array de sampler, que não
                        // é garantida em todo hardware/driver.
                        vec3 cookieTint = vec3(1.0);
                        if (u_PointLights[i].CookieIndex == 0) cookieTint = texture(u_PointCookies[0], cuv).rgb;
                        else if (u_PointLights[i].CookieIndex == 1) cookieTint = texture(u_PointCookies[1], cuv).rgb;
                        else if (u_PointLights[i].CookieIndex == 2) cookieTint = texture(u_PointCookies[2], cuv).rgb;
                        else if (u_PointLights[i].CookieIndex == 3) cookieTint = texture(u_PointCookies[3], cuv).rgb;
                        contribution *= cookieTint;
                    }
                }
            }

            Lo += contribution;
        }

        // --- Ambient / IBL --- (independente da direcional)
        // u_AmbientShadowFactor: 0=ambient bloqueado em sombra (interiores),
        //                        1=ambient livre (céu aberto, padrão)
        // Quando shadow=1 (totalmente na sombra) e factor=0: ambient reduz ao mínimo.
        float shadowedAmbient = mix(1.0 - shadow * 0.85, 1.0, u_AmbientShadowFactor);

        vec3 ambient;
        if (u_HasIBL == 1)
        {
            vec3 F_amb  = FresnelSchlick(max(dot(N, V), 0.0), F0);
            vec3 kD_amb = (1.0 - F_amb) * (1.0 - metallic);

            // Difuso: dentro do Probe Volume, a irradiância bakeada das
            // probes SUBSTITUI o irradiance map global do céu — é aqui que
            // interiores escurecem sozinhos e recebem o bounce colorido
            // do sol nas paredes.
            vec3 irradiance  = texture(u_IrradianceMap, N).rgb;
            irradiance = mix(irradiance, probeIrr, probeW);
            vec3 diffuse_ibl = irradiance * albedo;

            vec3 R = reflect(-V, N);
            vec3 prefilteredColor = textureLod(u_PrefilteredMap, R, roughness * 4.0).rgb;
            vec2 brdf = texture(u_BRDFLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
            vec3 specular_ibl = prefilteredColor * (F_amb * brdf.x + brdf.y);

            // Especular: continua vindo do prefiltered map do céu (as
            // probes L1 não têm detalhe angular pra reflexo), mas atenuado
            // pela visibilidade do céu da probe — dentro de uma sala, o
            // reflexo do céu não existe.
            float skyAtten = mix(1.0, probeSkyVis, probeW);
            specular_ibl *= skyAtten;

            // Reflection Probes — dentro da caixa de influência, o reflexo
            // LOCAL (paredes da sala, com box projection) substitui o do
            // céu. A mesma aproximação de BRDF do envmap global se aplica.
            float reflW = 0.0;
            vec3  reflSpec = vec3(0.0);
            EvalReflectionProbes(fragPos, R, roughness, reflW, reflSpec);
            if (reflW > 0.0)
            {
                reflSpec = reflSpec * (F_amb * brdf.x + brdf.y);
                specular_ibl = mix(specular_ibl, reflSpec, reflW);
            }

            vec3 ibl = (kD_amb * diffuse_ibl + specular_ibl) * ao * u_IBLIntensity;
            vec3 flatAmbient = u_AmbientStrength * albedo * ao * skyAtten;
            // SKY_LIGHT_V1 — a tinta do Sky Light multiplica TODO o ambiente
            // (IBL e chapado), porque os dois representam a mesma coisa: luz
            // que vem do ceu.
            ambient = (ibl + flatAmbient) * shadowedAmbient * u_SkyLightColor;
        }
        else
        {
            ambient = u_AmbientStrength * (u_HasLight == 1 ? u_LightColor : vec3(1.0)) * albedo * ao * shadowedAmbient * u_SkyLightColor;
            // Sem IBL: as probes substituem o ambient constante inteiro
            ambient = mix(ambient, probeIrr * albedo * ao, probeW);
        }

        // Interior Volumes — o ambient/IBL vem do céu; dentro de uma sala
        // fechada ele não existe. Este é o termo que causava o leaking.
        ambient *= interiorAmbient;

        vec3 color = ambient + Lo;

        // ═════════════════════════════════════════════════════════════════
        //  SKY_SUN_GATE_V1b — O PISO DE 2% ERA UM PISO DE VERDADE
        //
        //  Aqui havia `max(color, albedo * 0.02 * interiorAmbient)` com o
        //  0.02 CRAVADO. Um piso constante significa que a cena NUNCA fica
        //  preta: apagado o sol, apagado o Sky Light, apagado o HDRI, cada
        //  superficie continuava devolvendo 2% do proprio albedo. Era por
        //  isso que "de noite os objetos ainda aparecem".
        //
        //  Um piso desses e mentira: ele nao representa luz nenhuma da cena,
        //  so evita preto puro. Agora o valor vem da CPU e ja chega
        //  multiplicado pelo mesmo portao do sol e pelo estado do Sky Light,
        //  entao ele acompanha a luz que EXISTE — e vai a zero quando nao ha
        //  nenhuma, que e o comportamento pedido.
        // ═════════════════════════════════════════════════════════════════
        color = max(color, albedo * u_AmbientFloor * interiorAmbient);

        // Emissive — somado direto, sem ser afetado por luz/sombra/AO,
        // assim como no caminho forward (preview do material).
        color += texture(u_Emissive, v_TexCoord).rgb;

        FragColor = vec4(color, 1.0);
    }
)";

    void OpenGLLightingPass::Initialize()
    {
        // Carimbo de versao — conferir NO LOG antes de investigar sombreamento.
        AXE_CORE_INFO("PCSS_V1: penumbra por tamanho angular do sol + SHADOW_ACNE_V1 (normal offset)");

        try
        {
            m_Shader = Shader::Create(s_QuadVert, s_LightingFrag);
            SetupQuad();
            m_Initialized = true;
            //AXE_CORE_INFO("OpenGLLightingPass initialized");
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("OpenGLLightingPass shader error: {}", e.what());
        }
    }

    void OpenGLLightingPass::RecompileShader()
    {
        // Só o shader — o quad (VAO/VBO) não precisa ser refeito, a
        // geometria nunca muda. Substituir o shared_ptr libera o shader
        // antigo automaticamente (OpenGLShader::~OpenGLShader já chama
        // glDeleteProgram), então não vaza recurso de GPU.
        try
        {
            auto newShader = Shader::Create(s_QuadVert, s_LightingFrag);
            if (newShader)
            {
                m_Shader = newShader;
                AXE_CORE_INFO("OpenGLLightingPass: shader recompilado.");
            }
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("OpenGLLightingPass::RecompileShader falhou: {}", e.what());
        }
    }

    void OpenGLLightingPass::SetupQuad()
    {
        float verts[] = {
            -1.f,  1.f,  0.f, 1.f,
            -1.f, -1.f,  0.f, 0.f,
             1.f, -1.f,  1.f, 0.f,
            -1.f,  1.f,  0.f, 1.f,
             1.f, -1.f,  1.f, 0.f,
             1.f,  1.f,  1.f, 1.f,
        };
        glCreateVertexArrays(1, &m_QuadVAO);
        glCreateBuffers(1, &m_QuadVBO);
        glNamedBufferStorage(m_QuadVBO, sizeof(verts), verts, 0);
        glVertexArrayVertexBuffer(m_QuadVAO, 0, m_QuadVBO, 0, 4 * sizeof(float));
        glEnableVertexArrayAttrib(m_QuadVAO, 0);
        glVertexArrayAttribFormat(m_QuadVAO, 0, 2, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(m_QuadVAO, 0, 0);
        glEnableVertexArrayAttrib(m_QuadVAO, 1);
        glVertexArrayAttribFormat(m_QuadVAO, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
        glVertexArrayAttribBinding(m_QuadVAO, 1, 0);
    }

    void OpenGLLightingPass::Execute(const GBuffer& gbuffer,
        uint32_t ssaoTextureID,
        uint32_t shadowMapID,
        const glm::mat4& lightSpaceMatrix,
        const CascadedShadowPass* csm,
        const glm::mat4& view,
        const glm::vec3& cameraPosition,
        const DirectionalLight* light,
        const SceneEnvironment* environment,
        const std::vector<PointLight>& pointLights,
        const std::vector<InteriorVolumeData>& interiorVolumes,
        const std::vector<ProbeVolumeData>& probeVolumes,
        const std::vector<ReflectionProbeData>& reflectionProbes,
        uint32_t pointShadowArrayID)
    {
        if (!m_Shader || !m_Initialized)
        {
            AXE_CORE_ERROR("LightingPass: não inicializado!");
            return;
        }

        // ── DIAGNÓSTICO TEMPORÁRIO — remover depois ──
        //{
        //    static double s_LastLogTime = 0.0;
        //    double now = glfwGetTime();
        //    if (now - s_LastLogTime > 1.0)
        //    {
        //        s_LastLogTime = now;
        //        AXE_CORE_INFO("[DIAG-LP] gbuffer={}x{} albedoID={} posID={} hasLight={} numPointLights={}",
        //            gbuffer.GetWidth(), gbuffer.GetHeight(), gbuffer.GetAlbedoID(), gbuffer.GetPositionID(),
        //            (light != nullptr), (int)pointLights.size());

        //        if (light)
        //            AXE_CORE_INFO("[DIAG-LP] DirLight color=({:.2f},{:.2f},{:.2f}) intensity={:.2f} ambient={:.2f}",
        //                light->Color.x, light->Color.y, light->Color.z, light->Intensity, light->AmbientStrength);

        //        for (size_t i = 0; i < pointLights.size(); i++)
        //        {
        //            const auto& pl = pointLights[i];
        //            AXE_CORE_INFO("[DIAG-LP] PointLight[{}] color=({:.2f},{:.2f},{:.2f}) intensity={:.2f} pos=({:.2f},{:.2f},{:.2f}) radius={:.2f} isSpot={} hasLightMaterial={}",
        //                i, pl.Color.x, pl.Color.y, pl.Color.z, pl.Intensity,
        //                pl.Position.x, pl.Position.y, pl.Position.z, pl.Radius,
        //                pl.IsSpot, (pl.LightMaterialShader != nullptr));
        //            if (pl.IsSpot)
        //                AXE_CORE_INFO("[DIAG-LP]   -> dir=({:.3f},{:.3f},{:.3f}) innerAngle={:.1f} outerAngle={:.1f}",
        //                    pl.Direction.x, pl.Direction.y, pl.Direction.z,
        //                    pl.InnerConeAngle, pl.OuterConeAngle);
        //        }
        //    }
        //}

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE); // preserva o depth copiado pelo BlitDepth para o skybox
        glBindVertexArray(m_QuadVAO);
        m_Shader->Bind();

        // G-Buffer — slots 0, 1, 2, 3
        glBindTextureUnit(0, gbuffer.GetPositionID());
        glBindTextureUnit(1, gbuffer.GetNormalID());
        glBindTextureUnit(2, gbuffer.GetAlbedoID());
        glBindTextureUnit(3, gbuffer.GetPBRID());
        m_Shader->SetInt("u_Position", 0);
        m_Shader->SetInt("u_Normal", 1);
        m_Shader->SetInt("u_Albedo", 2);
        m_Shader->SetInt("u_PBR", 3);

        // Emissive — slot 9 (0-3 = G-Buffer base, 4 = SSAO, 5 = shadow map,
        // 6-8 = IBL — ver binds abaixo)
        glBindTextureUnit(9, gbuffer.GetEmissiveID());
        m_Shader->SetInt("u_Emissive", 9);

        // ── DIAGNÓSTICO TEMPORÁRIO — remover depois ──
        //{
        //    static double s_LastLogTime2 = 0.0;
        //    double now2 = glfwGetTime();
        //    if (now2 - s_LastLogTime2 > 1.0)
        //    {
        //        s_LastLogTime2 = now2;
        //        AXE_CORE_INFO("[DIAG-SHADOW] ssaoTextureID={} shadowMapID={} hasSSAO={} hasShadow={}",
        //            ssaoTextureID, shadowMapID, (ssaoTextureID != 0), (shadowMapID != 0));
        //    }
        //}

        // SSAO — slot 4
        if (ssaoTextureID != 0)
        {
            glBindTextureUnit(4, ssaoTextureID);
            m_Shader->SetInt("u_SSAO", 4);
            m_Shader->SetInt("u_HasSSAO", 1);
        }
        else
        {
            m_Shader->SetInt("u_HasSSAO", 0);
        }
        m_Shader->SetInt("u_SSAODebug", m_SSAODebug ? 1 : 0);

        // ── SHADOW_HWPCF_V1 — as unidades dos samplers de sombra ─────────
        //
        // Fixadas em 11/12 SEMPRE, inclusive quando nao ha CSM. Uniform de
        // sampler comeca em ZERO, e a unidade 0 tem o G-Buffer de posicao —
        // um sampler2DArrayShadow apontando para uma textura 2D de cor e
        // incompatibilidade de tipo, que e comportamento indefinido mesmo
        // quando o codigo nunca amostra (u_CascadeCount = 0).
        m_Shader->SetInt("u_ShadowMapCSM", 11);
        m_Shader->SetInt("u_ShadowMapRaw", 12);

        // Shadow — slot 5 (legacy) + slots 11/12 (CSM array)
        if (csm && csm->IsInitialized())
        {
            // ── SHADOW_HWPCF_V1 — a MESMA textura em duas unidades ───────
            //
            // 11 com o sampler de comparacao (PCF de hardware, filtrado),
            // 12 com o sampler cru (busca de bloqueador do PCSS). O modo de
            // comparacao e parametro da textura, e sampler object e a unica
            // forma de ter os dois no mesmo frame sem duplicar o mapa.
            //
            // Os samplers sao DESBINDADOS no fim do Execute: eles ficam
            // presos a unidade ate serem trocados, e vazariam para qualquer
            // passe seguinte que use as unidades 11/12.
            glBindTextureUnit(11, csm->GetDepthArrayID());
            glBindTextureUnit(12, csm->GetDepthArrayID());
            glBindSampler(11, csm->GetCompareSamplerID());
            glBindSampler(12, csm->GetRawSamplerID());
            m_Shader->SetInt("u_CascadeCount", csm->GetCascadeCount());

            const auto& cascades = csm->GetCascades();
            for (int i = 0; i < csm->GetCascadeCount(); ++i)
            {
                std::string matKey = "u_LightSpaceMatrixCSM[" + std::to_string(i) + "]";
                std::string splitKey = "u_CascadeSplitDepths[" + std::to_string(i) + "]";
                m_Shader->SetMat4(matKey.c_str(), glm::value_ptr(cascades[i].LightSpaceMatrix));
                m_Shader->SetFloat(splitKey.c_str(), cascades[i].SplitDepth);

                // SHADOW_ACNE_V1 — metros por texel desta cascade.
                std::string texelKey = "u_CascadeTexelWorld[" + std::to_string(i) + "]";
                m_Shader->SetFloat(texelKey.c_str(), cascades[i].TexelWorldSize);

                // PCSS_V1
                std::string rangeKey = "u_CascadeDepthRange[" + std::to_string(i) + "]";
                m_Shader->SetFloat(rangeKey.c_str(), cascades[i].DepthRange);
            }
            // ── PCSS_V1 — o tamanho angular da fonte ─────────────────────
            //
            // Enviado como a TANGENTE DA METADE do angulo, que e o fator que
            // multiplica a distancia para dar a largura da penumbra. Converter
            // aqui, e nao no shader, evita repetir trigonometria por pixel.
            // ── CLOUDS_SOFTEN_SUN_V1 ─────────────────────────────────
            //
            // Tamanho angular EFETIVO do sol: o valor autoral e o de CEU
            // LIMPO, e a cobertura de nuvens o alarga ate ~35 graus (fonte
            // difusa de dia encoberto). Curva ao QUADRADO porque nuvem rala
            // quase nao muda a sombra — so ceu bem fechado e que a dissolve.
            //
            // 35 e nao 90: acima disso o PCSS pediria um raio de penumbra que
            // o teto de MaxPenumbraTexels ja corta — trabalho jogado fora.
            float sunDeg = light ? light->SunAngularDegrees : 0.0f;
            if (light && m_HasSkyLight && m_SkyLight.Enabled &&
                m_SkyLight.ProceduralSky && m_SkyLight.CloudsSoftenSun)
            {
                const float c = glm::clamp(m_SkyLight.CloudCoverage, 0.0f, 1.0f);
                sunDeg = glm::mix(sunDeg, 35.0f, c * c);
            }
            const float sunTan = (sunDeg > 0.0f)
                ? std::tan(glm::radians(sunDeg) * 0.5f) * 2.0f
                : 0.0f;

            m_Shader->SetFloat("u_SunAngularRadius", sunTan);
            m_Shader->SetFloat("u_MaxPenumbraTexels",
                light ? glm::max(light->MaxPenumbraTexels, 1.0f) : 16.0f);

            m_Shader->SetInt("u_HasShadowMap", 1);
        }
        else if (shadowMapID != 0)
        {
            // Fallback: shadow map simples
            glBindTextureUnit(5, shadowMapID);
            m_Shader->SetInt("u_ShadowMap", 5);
            m_Shader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(lightSpaceMatrix));
            m_Shader->SetInt("u_CascadeCount", 0);
            m_Shader->SetInt("u_HasShadowMap", 1);
        }
        else
        {
            m_Shader->SetInt("u_HasShadowMap", 0);
            m_Shader->SetInt("u_CascadeCount", 0);
        }

        // ═════════════════════════════════════════════════════════════════
        //  SKY_LIGHT_V1 — QUEM MANDA NO AMBIENTE
        //
        //  Os tres valores de ambiente (intensidade do IBL, quanto a sombra
        //  bloqueia o ambiente, e o ambiente chapado) sao resolvidos AQUI,
        //  num ponto so, antes de qualquer coisa da luz direcional. Assim o
        //  bloco da luz abaixo nao volta a decidir sobre ambiente — que era
        //  exatamente o acoplamento que esta rodada desfaz.
        //
        //  Precedencia:
        //    1. Sky Light da cena, se existir (o caminho normal daqui pra
        //       frente — a migracao do SceneSerializer garante que toda cena
        //       aberta ganha um);
        //    2. campos LEGADOS do DirectionalLight, se nao houver Sky Light
        //       mas houver sol — cena vinda de qualquer caminho que escape da
        //       migracao continua com a mesma aparencia em vez de escurecer;
        //    3. nada dos dois: ambiente ZERO. E o caso "cena sem sol e sem
        //       sky light", e escuro e a resposta certa.
        //
        //  Repare que o Sky Light NAO inventa luz: ele escala o cubemap
        //  capturado do ceu, e esse ceu obedece ao sol desde o
        //  SKYLIGHT_CHAIN_V1. Sky Light em 1.0 com o sol apagado continua
        //  dando escuro, que e o comportamento fisico.
        // ═════════════════════════════════════════════════════════════════
        {
            float     iblIntensity = 0.0f;
            float     ambientShadow = 1.0f;
            float     flatAmbient = 0.0f;
            glm::vec3 skyTint(1.0f);

            if (m_HasSkyLight)
            {
                if (m_SkyLight.Enabled)
                {
                    iblIntensity = m_SkyLight.Intensity;
                    ambientShadow = m_SkyLight.ShadowFactor;
                    flatAmbient = m_SkyLight.ConstantAmbient;
                    skyTint = m_SkyLight.Color;
                }
                // Enabled=false cai nos zeros acima de proposito: desligar o
                // Sky Light tem que apagar o ambiente INTEIRO, inclusive o
                // chapado. E assim que se ve quanto da imagem e o sol.
            }
            else if (light)
            {
                iblIntensity = light->IBLIntensity;
                ambientShadow = light->AmbientShadowFactor;
                flatAmbient = light->AmbientStrength;
            }

            // ═════════════════════════════════════════════════════════════
            //  SKY_SUN_GATE_V1 — SEM SOL NÃO HÁ DIA
            //
            //  Eu tinha defendido que um HDRI é fonte de luz própria e por
            //  isso continuava iluminando sem Luz Direcional. Está errado no
            //  que importa: um HDRI de céu diurno é uma FOTOGRAFIA de luz do
            //  sol espalhada pela atmosfera. Apagado o sol, aquela luz não
            //  existe mais — a foto continua existindo, a luz não.
            //
            //  Então o ambiente INTEIRO (IBL e chapado, procedural ou HDRI)
            //  passa por este portão. É o que faz "apaguei o sol" significar
            //  escuro, que é o comportamento esperado de qualquer engine.
            //
            //  A curva satura em k=0.15, muito mais rápido que a do céu
            //  (k=0.5), de propósito:
            //     I = 0     -> 0.00   (apagado é apagado)
            //     I = 0.15  -> 0.63
            //     I = 0.5   -> 0.96
            //     I >= 1    -> ~1.00  (qualquer cena iluminada: intacta)
            //  Assim ele só morde quando o sol está de fato apagado ou quase,
            //  e quase não soma com o escurecimento que o céu procedural já
            //  faz por conta própria.
            //
            //  Sem Sky Light na cena (caminho legado) o portão também vale:
            //  o default do desenho é depender do sol.
            // ═════════════════════════════════════════════════════════════
            const bool sunGated = !m_HasSkyLight || m_SkyLight.SunDependent;
            if (sunGated)
            {
                const float sunI = light ? std::max(light->Intensity, 0.0f) : 0.0f;
                const float gate = 1.0f - std::exp(-sunI / 0.15f);
                iblIntensity *= gate;
                flatAmbient *= gate;
            }

            m_Shader->SetFloat("u_IBLIntensity", iblIntensity);
            m_Shader->SetFloat("u_AmbientShadowFactor", ambientShadow);
            m_Shader->SetFloat("u_AmbientStrength", flatAmbient);
            m_Shader->SetFloat3("u_SkyLightColor", skyTint);

            // Piso minimo do ambiente. Era 0.02 cravado NO SHADER; agora
            // acompanha a luz que existe: some junto com o sol, e some de vez
            // com o Sky Light desligado.
            float ambientFloor = 0.02f;
            if (m_HasSkyLight && !m_SkyLight.Enabled) ambientFloor = 0.0f;
            if (sunGated)
            {
                const float sunI = light ? std::max(light->Intensity, 0.0f) : 0.0f;
                ambientFloor *= 1.0f - std::exp(-sunI / 0.15f);
            }
            m_Shader->SetFloat("u_AmbientFloor", ambientFloor);
        }

        // Luz direcional
        if (light)
        {
            m_Shader->SetInt("u_HasLight", 1);
            m_Shader->SetFloat3("u_LightDirection", light->Direction);
            // LightMaterialResult é 1.0 (neutro) quando não há Light
            // Material attachado, então essa multiplicação é segura mesmo
            // sem nenhum grafo configurado.
            m_Shader->SetFloat3("u_LightColor", light->Color * light->LightMaterialResult);
            // CLOUDS_SOFTEN_SUN_V1 — nuvem tambem TIRA luz direta (parte vira
            // difusa e volta pelo ceu, que o Sky Light ja captura). Ate 45% a
            // menos com cobertura total, e LINEAR de proposito: perda de
            // direta comeca com pouca nuvem, diferente do alargamento da
            // sombra, que so vem com ceu fechado.
            float directDim = 1.0f;
            if (m_HasSkyLight && m_SkyLight.Enabled &&
                m_SkyLight.ProceduralSky && m_SkyLight.CloudsSoftenSun)
                directDim = 1.0f - 0.45f * glm::clamp(m_SkyLight.CloudCoverage, 0.0f, 1.0f);

            m_Shader->SetFloat("u_LightIntensity", light->Intensity * directDim);
            m_Shader->SetFloat("u_ShadowBias", light->ShadowBias);
            m_Shader->SetFloat("u_ContactShadowLength",
                std::max(light->ContactShadowLength, 0.0f));   // CONTACT_SHADOW_V1
            // SKY_LIGHT_V1 — u_IBLIntensity / u_AmbientStrength /
            // u_AmbientShadowFactor NAO sao mais decididos aqui: sairam para o
            // bloco do Sky Light, acima. Deixa-los aqui sobrescreveria o Sky
            // Light com os valores legados da luz e o desacoplamento seria
            // apenas aparente.

            // Cookie — slot 10. Right/Up: base ortonormal perpendicular à
            // direção da luz, pra projetar fragPos num plano 2D.
            if (light->CookieTexture && light->CookieTexture->IsLoaded())
            {
                glm::vec3 dir = glm::length(light->Direction) > 0.0001f
                    ? glm::normalize(light->Direction) : glm::vec3(0, -1, 0);
                glm::vec3 up = (fabsf(dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 right = glm::normalize(glm::cross(dir, up));
                up = glm::normalize(glm::cross(right, dir));

                light->CookieTexture->Bind(10);
                m_Shader->SetInt("u_DirCookie", 10);
                m_Shader->SetInt("u_HasDirCookie", 1);
                m_Shader->SetFloat3("u_DirCookieRight", right);
                m_Shader->SetFloat3("u_DirCookieUp", up);
                m_Shader->SetFloat("u_DirCookieScale", std::max(light->CookieScale, 0.01f));
            }
            else m_Shader->SetInt("u_HasDirCookie", 0);
        }
        else
        {
            m_Shader->SetInt("u_HasLight", 0);
            // SKY_LIGHT_V1 — aqui havia um u_IBLIntensity = 1.0f FORCADO:
            // apagar a luz direcional colocava o ambiente no MAXIMO, que e o
            // contrario do esperado e era parte do "apaguei o sol e a cena
            // continua clara". O ambiente agora e do Sky Light, resolvido
            // acima; sem Sky Light e sem sol ele e zero.
            m_Shader->SetFloat("u_ShadowBias", 0.02f);
            m_Shader->SetFloat("u_ContactShadowLength", 0.0f);   // sem sol, sem contato
            m_Shader->SetInt("u_HasDirCookie", 0);
        }

        m_Shader->SetFloat3("u_CameraPosition", cameraPosition);
        m_Shader->SetMat4("u_View", glm::value_ptr(view));

        // CONTACT_SHADOW_V1 — a view-projection deste frame.
        const glm::mat4 viewProj = m_Projection * view;
        m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(viewProj));

        // IBL — slots 6, 7, 8
        //
        // SKY_IBL_V1 — a fonte deixou de ser "o Skybox" e passou a ser
        // IBLSource(): o cubemap do ceu procedural quando ele existe, senao o
        // HDRI. Ver a nota em SceneEnvironment.
        const CubemapTexture* ibl = environment ? environment->IBLSource() : nullptr;
        if (ibl)
        {
            ibl->BindIrradiance(6);
            ibl->BindPrefiltered(7);
            ibl->BindBRDFLut(8);
            m_Shader->SetInt("u_IrradianceMap", 6);
            m_Shader->SetInt("u_PrefilteredMap", 7);
            m_Shader->SetInt("u_BRDFLut", 8);
            m_Shader->SetInt("u_HasIBL", 1);
        }
        else m_Shader->SetInt("u_HasIBL", 0);

        // Point Lights
        int numLights = (int)std::min(pointLights.size(), (size_t)16);
        m_Shader->SetInt("u_NumPointLights", numLights);

        // Cookies de Point Light — slots 12, 13, 14, 15 (limite de 4
        // simultâneas; a partir da 5ª, a luz funciona normal sem padrão).
        // NÃO usar o slot 11: é o do CSM (sampler2DArray) — bindar um
        // sampler2D no mesmo unit é comportamento indefinido e quebrava
        // a sombra em cascata sempre que uma cookie estava ativa.
        int nextCookieSlot = 0;
        const int cookieTextureUnits[4] = { 12, 13, 14, 15 };

        for (int i = 0; i < numLights; i++)
        {
            const auto& pl = pointLights[i];
            std::string base = "u_PointLights[" + std::to_string(i) + "]";
            m_Shader->SetFloat3(base + ".Position", pl.Position);
            m_Shader->SetFloat3(base + ".Color", pl.Color);
            m_Shader->SetFloat(base + ".Intensity", pl.Intensity);
            m_Shader->SetFloat(base + ".Radius", pl.Radius);

            m_Shader->SetInt(base + ".IsSpot", pl.IsSpot ? 1 : 0);
            m_Shader->SetFloat3(base + ".Direction", pl.Direction);
            // cos() pré-calculado aqui — mais barato que recalcular por
            // pixel no fragment shader, já que o ângulo é o mesmo para
            // todos os fragmentos afetados por esta luz no frame.
            m_Shader->SetFloat(base + ".InnerCutoff", cosf(glm::radians(pl.InnerConeAngle)));
            m_Shader->SetFloat(base + ".OuterCutoff", cosf(glm::radians(pl.OuterConeAngle)));

            m_Shader->SetInt(base + ".ShadowLayer", pl.ShadowLayer);
            m_Shader->SetFloat(base + ".ShadowBias", pl.ShadowBias);

            // Cookie — base ortonormal (Right/Up) perpendicular à direção,
            // mesma usada no gizmo do cone, e TanOuterAngle pra normalizar
            // o offset projetado pra -1..1 na borda do cone.
            m_Shader->SetFloat(base + ".TanOuterAngle", tanf(glm::radians(pl.OuterConeAngle)));

            int cookieIndex = -1;
            if (pl.IsSpot && pl.CookieTexture && pl.CookieTexture->IsLoaded() && nextCookieSlot < 4)
            {
                glm::vec3 dir = glm::length(pl.Direction) > 0.0001f
                    ? glm::normalize(pl.Direction) : glm::vec3(0, -1, 0);
                glm::vec3 up = (fabsf(dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 right = glm::normalize(glm::cross(dir, up));
                up = glm::normalize(glm::cross(right, dir));

                m_Shader->SetFloat3(base + ".Right", right);
                m_Shader->SetFloat3(base + ".Up", up);

                cookieIndex = nextCookieSlot;
                int unit = cookieTextureUnits[nextCookieSlot];
                pl.CookieTexture->Bind(unit);
                m_Shader->SetInt("u_PointCookies[" + std::to_string(nextCookieSlot) + "]", unit);
                nextCookieSlot++;
            }
            m_Shader->SetInt(base + ".CookieIndex", cookieIndex);
        }

        // Interior Volumes — máximo de 8 (limite dos arrays de uniform;
        // acima disso os volumes extras são simplesmente ignorados)
        int numVolumes = (int)std::min(interiorVolumes.size(), (size_t)8);
        m_Shader->SetInt("u_NumInteriorVolumes", numVolumes);
        for (int i = 0; i < numVolumes; i++)
        {
            const auto& iv = interiorVolumes[i];
            std::string idx = "[" + std::to_string(i) + "]";
            m_Shader->SetMat4("u_InteriorWorldToLocal" + idx, glm::value_ptr(iv.WorldToLocal));
            m_Shader->SetFloat3("u_InteriorHalfExtents" + idx, iv.HalfExtents);
            m_Shader->SetFloat("u_InteriorIntensity" + idx, iv.Intensity);
            m_Shader->SetFloat("u_InteriorBlend" + idx, iv.BlendDistance);
            int affect = (iv.AffectDirect ? 1 : 0) | (iv.AffectAmbient ? 2 : 0);
            m_Shader->SetInt("u_InteriorAffect" + idx, affect);
        }

        // Sombras de Point Light — cube map array no unit 28 (o mapa de
        // units está no comentário logo abaixo). O uniform é setado
        // sempre; a textura só binda quando existe — nenhum fragmento
        // sampleia sem ShadowLayer >= 0, então unit "vazio" nunca é lido.
        if (pointShadowArrayID != 0)
            glBindTextureUnit(28, pointShadowArrayID);
        m_Shader->SetInt("u_PointShadowArray", 28);

        // Light Probes — 2 grids x 4 samplers (units 16-23) + Reflection
        // Probes — 4 cubemaps (units 24-27). NOTA de texture units: o
        // mínimo garantido pela spec é 16 por stage (0-15, já ocupados);
        // desktop AMD/NVIDIA expõe 32 (a RX 580 inclusive), então 16-27 é
        // seguro nos alvos do AXE. Se um dia rodar em GL de mínimo
        // estrito, este é o primeiro lugar pra revisitar.
        {
            int uploaded = 0;
            for (size_t i = 0; i < probeVolumes.size() && uploaded < 2; i++)
            {
                const auto& pv = probeVolumes[i];
                if (!pv.Grid || !pv.Grid->IsValid()) continue;

                std::string idx = "[" + std::to_string(uploaded) + "]";
                int base = 16 + uploaded * 4;
                pv.Grid->SH0->Bind(base + 0);
                pv.Grid->SH1X->Bind(base + 1);
                pv.Grid->SH1Y->Bind(base + 2);
                pv.Grid->SH1Z->Bind(base + 3);
                m_Shader->SetInt("u_ProbeSH0" + idx, base + 0);
                m_Shader->SetInt("u_ProbeSH1X" + idx, base + 1);
                m_Shader->SetInt("u_ProbeSH1Y" + idx, base + 2);
                m_Shader->SetInt("u_ProbeSH1Z" + idx, base + 3);
                m_Shader->SetMat4("u_ProbeWorldToLocal" + idx, glm::value_ptr(pv.WorldToLocal));
                m_Shader->SetFloat3("u_ProbeHalfExtents" + idx, pv.HalfExtents);
                m_Shader->SetFloat("u_ProbeIntensity" + idx, pv.Intensity);
                m_Shader->SetFloat("u_ProbeFeather" + idx, std::max(pv.Feather, 0.0001f));
                m_Shader->SetInt("u_ProbeOccludeSun" + idx, pv.OccludeSunlight ? 1 : 0);
                uploaded++;
            }
            m_Shader->SetInt("u_NumProbeVolumes", uploaded);

            // Reflection Probes — o GetPrefilteredID() é o id OPACO do
            // cubemap pré-filtrado (mesmo contrato do ssaoTextureID);
            // esta camada é OpenGL, então binda direto.
            int rUp = 0;
            for (size_t i = 0; i < reflectionProbes.size() && rUp < 4; i++)
            {
                const auto& rp = reflectionProbes[i];
                if (!rp.Capture || !rp.Capture->IsValid()) continue;

                std::string idx = "[" + std::to_string(rUp) + "]";
                int unit = 24 + rUp;
                glBindTextureUnit(unit, rp.Capture->GetPrefilteredID());
                m_Shader->SetInt("u_ReflCube" + idx, unit);
                m_Shader->SetMat4("u_ReflWorldToLocal" + idx, glm::value_ptr(rp.WorldToLocal));
                m_Shader->SetFloat3("u_ReflHalfExtents" + idx, rp.HalfExtents);
                m_Shader->SetFloat3("u_ReflPosition" + idx, rp.Position);
                m_Shader->SetFloat("u_ReflIntensity" + idx, rp.Intensity);
                m_Shader->SetFloat("u_ReflFeather" + idx, std::max(rp.Feather, 0.0001f));
                m_Shader->SetInt("u_ReflBoxProj" + idx, rp.BoxProjection ? 1 : 0);
                rUp++;
            }
            m_Shader->SetInt("u_NumReflProbes", rUp);
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Restaura estado
        //
        // SHADOW_HWPCF_V1 — sampler object fica preso a UNIDADE, nao ao
        // programa nem a textura: sem soltar aqui, o proximo passe que usasse
        // as unidades 11/12 herdaria comparacao de profundidade ligada e leria
        // a propria textura como 0/1. Mesma regra do glPolygonOffset do shadow
        // pass — quem clobbera estado global, restaura.
        glBindSampler(11, 0);
        glBindSampler(12, 0);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindVertexArray(0);
        glUseProgram(0);
    }
} // namespace axe