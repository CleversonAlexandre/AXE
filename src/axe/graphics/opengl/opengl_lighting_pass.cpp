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
    uniform sampler2DArray u_ShadowMapCSM;         // texture array com 4 cascades
    uniform sampler2D      u_ShadowMap;            // shadow map simples (fallback)
    uniform mat4  u_LightSpaceMatrixCSM[4];        // matrizes por cascade
    uniform float u_CascadeSplitDepths[4];         // splits em view space (z negativo)
    uniform int   u_CascadeCount;                  // 0 = sem CSM
    uniform mat4  u_LightSpaceMatrix;
    uniform int   u_HasShadowMap;
    uniform mat4  u_View;
    uniform float u_ShadowBias;

    // Luz direcional
    uniform vec3  u_LightDirection;
    uniform vec3  u_LightColor;
    uniform float u_LightIntensity;
    uniform float u_AmbientStrength;
    uniform float u_AmbientShadowFactor; // 0=ambient bloqueado por sombra, 1=ambient livre
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

    // PCF 3x3 em uma cascade específica
    float PCFShadow(sampler2DArray shadowArray, int cascade, vec3 projCoords, float bias)
    {
        float shadow = 0.0;
        vec2 texelSize = 1.0 / textureSize(shadowArray, 0).xy;
        for (int x = -1; x <= 1; x++)
            for (int y = -1; y <= 1; y++)
            {
                float pcf = texture(shadowArray,
                    vec3(projCoords.xy + vec2(x, y) * texelSize, float(cascade))).r;
                shadow += projCoords.z - bias > pcf ? 1.0 : 0.0;
            }
        return shadow / 9.0;
    }

    float ShadowCalculation(vec3 fragPos, vec3 normal, vec3 lightDir)
    {
        // ── CSM — seleciona cascade pela profundidade view space ───────────
        if (u_CascadeCount > 0)
        {
            // Converte fragPos (world) para view space para selecionar cascade por profundidade
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

            vec4 fragPosLS  = u_LightSpaceMatrixCSM[cascade] * vec4(fragPos, 1.0);
            vec3 projCoords = fragPosLS.xyz / fragPosLS.w * 0.5 + 0.5;
            if (projCoords.z > 1.0) return 0.0;

            // Bias adaptativo por cascade — cascades longe precisam de mais bias
            float biasScale = max(1.0, float(cascade) * 0.5 + 1.0);
            float bias = max(u_ShadowBias * biasScale * (1.0 - dot(normal, lightDir)), u_ShadowBias * biasScale);

            return PCFShadow(u_ShadowMapCSM, cascade, projCoords, bias);
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

    vec3 CalcPointLight(PointLight pl, vec3 fragPos, vec3 N, vec3 V,
                        vec3 albedo, float metallic, float roughness, vec3 F0)
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
        vec2  pbr       = texture(u_PBR, v_TexCoord).rg;
        float roughness = pbr.r;
        float matAO     = pbr.g;

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

        vec3 V = normalize(u_CameraPosition - fragPos);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

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
        float probeSunOcc = mix(1.0, probeOcc, probeW);

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

            // Cookie — projeta fragPos no plano perpendicular à direção da
            // luz (paralela, então sem perspectiva — só tiling por escala)
            vec3 dirCookieTint = vec3(1.0);
            if (u_HasDirCookie == 1)
            {
                float cu = dot(fragPos, u_DirCookieRight) / u_DirCookieScale;
                float cv = dot(fragPos, u_DirCookieUp) / u_DirCookieScale;
                dirCookieTint = texture(u_DirCookie, fract(vec2(cu, cv))).rgb;
            }

            // interiorDirect: dentro de um Interior Volume, o sol não
            // entra — independente do shadow map cobrir ou não o teto.
            Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadow) * mix(1.0, ao, 0.5) * dirCookieTint * interiorDirect * probeSunOcc;
        }

        // --- Point lights --- (independente da direcional)
        for (int i = 0; i < u_NumPointLights; i++)
        {
            vec3 contribution = CalcPointLight(u_PointLights[i], fragPos, N, V, albedo, metallic, roughness, F0);

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
            ambient = (ibl + flatAmbient) * shadowedAmbient;
        }
        else
        {
            ambient = u_AmbientStrength * (u_HasLight == 1 ? u_LightColor : vec3(1.0)) * albedo * ao * shadowedAmbient;
            // Sem IBL: as probes substituem o ambient constante inteiro
            ambient = mix(ambient, probeIrr * albedo * ao, probeW);
        }

        // Interior Volumes — o ambient/IBL vem do céu; dentro de uma sala
        // fechada ele não existe. Este é o termo que causava o leaking.
        ambient *= interiorAmbient;

        vec3 color = ambient + Lo;
        // O piso mínimo (2% do albedo) também é "luz de fora" — dentro de
        // um interior ele acompanha o fator, senão a sala nunca escurece
        // de verdade.
        color = max(color, albedo * 0.02 * interiorAmbient);

        // Emissive — somado direto, sem ser afetado por luz/sombra/AO,
        // assim como no caminho forward (preview do material).
        color += texture(u_Emissive, v_TexCoord).rgb;

        FragColor = vec4(color, 1.0);
    }
)";

    void OpenGLLightingPass::Initialize()
    {
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

        // Shadow — slot 5 (legacy) + slot 11 (CSM array)
        if (csm && csm->IsInitialized())
        {
            // CSM — texture array com todas as cascades
            glBindTextureUnit(11, csm->GetDepthArrayID());
            m_Shader->SetInt("u_ShadowMapCSM", 11);
            m_Shader->SetInt("u_CascadeCount", csm->GetCascadeCount());

            const auto& cascades = csm->GetCascades();
            for (int i = 0; i < csm->GetCascadeCount(); ++i)
            {
                std::string matKey = "u_LightSpaceMatrixCSM[" + std::to_string(i) + "]";
                std::string splitKey = "u_CascadeSplitDepths[" + std::to_string(i) + "]";
                m_Shader->SetMat4(matKey.c_str(), glm::value_ptr(cascades[i].LightSpaceMatrix));
                m_Shader->SetFloat(splitKey.c_str(), cascades[i].SplitDepth);
            }
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

        // Luz direcional
        if (light)
        {
            m_Shader->SetInt("u_HasLight", 1);
            m_Shader->SetFloat3("u_LightDirection", light->Direction);
            // LightMaterialResult é 1.0 (neutro) quando não há Light
            // Material attachado, então essa multiplicação é segura mesmo
            // sem nenhum grafo configurado.
            m_Shader->SetFloat3("u_LightColor", light->Color * light->LightMaterialResult);
            m_Shader->SetFloat("u_LightIntensity", light->Intensity);
            m_Shader->SetFloat("u_AmbientStrength", light->AmbientStrength);
            m_Shader->SetFloat("u_AmbientShadowFactor", light->AmbientShadowFactor);
            m_Shader->SetFloat("u_ShadowBias", light->ShadowBias);
            m_Shader->SetFloat("u_IBLIntensity", light->IBLIntensity);

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
            m_Shader->SetFloat("u_IBLIntensity", 1.0f);
            m_Shader->SetFloat("u_AmbientStrength", 0.0f);
            m_Shader->SetFloat("u_AmbientShadowFactor", 1.0f);
            m_Shader->SetFloat("u_ShadowBias", 0.005f);
            m_Shader->SetInt("u_HasDirCookie", 0);
        }

        m_Shader->SetFloat3("u_CameraPosition", cameraPosition);
        m_Shader->SetMat4("u_View", glm::value_ptr(view));

        // IBL — slots 6, 7, 8
        if (environment && environment->HasIBL())
        {
            environment->Skybox->BindIrradiance(6);
            environment->Skybox->BindPrefiltered(7);
            environment->Skybox->BindBRDFLut(8);
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
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindVertexArray(0);
        glUseProgram(0);
    }
} // namespace axe