#include "opengl_volumetric_fog_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/renderer/gbuffer.hpp"
#include "axe/graphics/texture3d.hpp"
#include "axe/graphics/texture.hpp"
// VOLUME_DOMAIN_V1 — resolucao do material de fog (UUID -> shader + samplers)
#include "axe/material/material_shader_cache.hpp"
#include "axe/material/material_cooked.hpp"
#include "axe/log/log.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

// GL permitido aqui — está na camada opengl/
#include <glad/glad.h>

namespace axe
{
    static constexpr int kMaxFogLights = 8;

    static const char* kFogVS = R"(
#version 460 core
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;
out vec2 v_UV;
void main() { gl_Position = vec4(a_Pos, 0.0, 1.0); v_UV = a_UV; }
)";

    static const char* kFogFS = R"(
#version 460 core
in vec2 v_UV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Depth;
uniform sampler2D u_SceneColor;
uniform mat4  u_InvViewProj;
uniform vec3  u_CameraPos;
uniform vec3  u_FogColor;
uniform float u_Density;
uniform float u_HeightBase;
uniform float u_HeightFalloff;
uniform float u_ScatterStrength;
uniform float u_AmbientStrength;
uniform float u_FogStart;
uniform float u_FogEnd;
uniform int   u_Steps;
uniform float u_StepJitter;
uniform float u_Time;
uniform int   u_NumLights;
uniform vec3  u_LightPos[8];
uniform vec3  u_LightColor[8];
uniform float u_LightIntensity[8];
uniform float u_LightRadius[8];
// Spot light support — cone check no volume de fog
uniform int   u_LightIsSpot[8];
uniform vec3  u_LightDir[8];      // direção do cone (normalizada)
uniform float u_LightOuterCut[8]; // cos(OuterConeAngle), pré-calculado

// ── Interiores/Probes — bloqueio de luz externa no fog ─────────────────
// O termo ambiente do fog (u_FogColor * u_AmbientStrength) representa a
// luz do CÉU espalhada no volume — dentro de uma sala fechada ela não
// existe. Sem este bloqueio, o fog interno brilhava com a "névoa do
// exterior", o último vazamento de luz externa em interiores. As point
// lights do fog NÃO são afetadas: fog interno iluminado pelas luzes da
// sala é exatamente o comportamento desejado.
uniform int   u_NumInteriorVolumes;
uniform mat4  u_InteriorWorldToLocal[8];
uniform vec3  u_InteriorHalfExtents[8];
uniform float u_InteriorIntensity[8];
uniform float u_InteriorBlend[8];
uniform int   u_InteriorAffect[8]; // bit 1 = ambient (mesmo pacote do lighting)

uniform int       u_NumProbeVolumes;      // até 2 (mesmo limite do lighting)
uniform mat4      u_ProbeWorldToLocal[2];
uniform vec3      u_ProbeHalfExtents[2];
uniform float     u_ProbeFeather[2];
uniform sampler3D u_ProbeSH0[2]; // .a = visibilidade do céu (geométrica)

// ── VOLUME_SUN_V2 — a luz direcional dentro do volume ──────────────────
// u_SunDirection aponta PARA ONDE A LUZ VAI (mesma convenção do
// u_LightDirection do lighting pass). u_SunIntensity 0 desliga o termo.
uniform vec3  u_SunDirection;
uniform vec3  u_SunColor;
uniform float u_SunIntensity;

// A cascata, na unidade 13 — a mesma escolha e a mesma razão do
// FORWARD_SHADOW_V1 no MeshRenderer: 11 e 12 podem estar com sampler
// object preso pelo lighting pass, e sampler object com
// GL_TEXTURE_COMPARE_MODE sobre amostragem crua é indefinido.
uniform sampler2DArray u_FogShadowArray;
uniform mat4  u_FogShadowView;
uniform mat4  u_FogCascadeMatrix[4];
uniform float u_FogCascadeSplit[4];
uniform int   u_FogCascadeCount;

vec3 ReconstructWorld(vec2 uv, float d)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 world = u_InvViewProj * ndc;
    return world.xyz / world.w;
}

float HeightDensity(float y)
{
    return exp(-max(0.0, y - u_HeightBase) * u_HeightFalloff);
}

float LightAttenuation(float dist, float radius)
{
    float d = dist / max(radius, 0.001);
    return max(0.0, 1.0 - d * d);
}

// ── VOLUME_SUN_V2 — quanto do sol chega a ESTE ponto do ar ─────────────
//
// Devolve 1.0 no sol, 0.0 na sombra. É isto que desenha o raio de luz: o
// que separa a coluna iluminada da escura não é a névoa, é o mapa de
// sombra amostrado ao longo do raio.
//
// UMA amostra por passo, e não o PCF 3x3 do forward. Aqui a média já
// acontece de graça: são 12 a 32 passos por pixel, e o jitter desloca
// cada raio, então o ruído se dissolve na integral. Um PCF 3x3 custaria
// 9x isso para suavizar algo que o próprio ray march já suaviza.
//
// Sem deslocamento por normal: um ponto no ar não tem normal. O bias
// constante pequeno é só contra o auto-sombreamento na primeira amostra
// rente ao chão.
//
// ── VOLUME_SUN_V2b — A PAREDE DE LUZ ──────────────────────────────────
//
// A V2 fazia `return 1.0` seco fora do tronco da cascata. O raciocínio
// estava certo pela metade: 1.0 é mesmo o único valor que não INVENTA
// sombra onde o mapa acaba. Só que num ray march ele inventa o oposto —
// uma PAREDE DE LUZ.
//
// A conta que denuncia: as cascatas cobrem `ShadowDistance` (50 m no
// default), e o Fog End pode ir a 200. Da distância de sombra em diante,
// TODA amostra cai fora e volta "totalmente iluminada", de uma vez. E o
// tronco de uma cascata é uma CAIXA, então a silhueta dela na cena é uma
// RETA — uma emenda dura atravessando o chão, que varre conforme se anda.
//
// Num passe de superfície isso nunca apareceu porque a última cascata
// cobre tudo que se vê. O ray march é o primeiro consumidor que vai
// ALÉM da distância de sombra, e por isso é o primeiro a enxergar.
//
// O conserto não é escolher outro valor de borda — é não ter borda:
// a sombra desaparece por RAMPA, na borda do tronco (em UV) e na cauda
// da última cascata (em metros). Fora da cobertura continua iluminado,
// como tem de ser; o que sumiu foi o degrau.
float SunLightFactor(vec3 pos)
{
    if (u_FogCascadeCount <= 0) return 1.0;

    float depth = abs((u_FogShadowView * vec4(pos, 1.0)).z);
    int cascade = u_FogCascadeCount - 1;
    for (int i = 0; i < 4; ++i)
    {
        if (i >= u_FogCascadeCount) break;
        if (depth < u_FogCascadeSplit[i]) { cascade = i; break; }
    }

    vec4 lp = u_FogCascadeMatrix[cascade] * vec4(pos, 1.0);
    vec3 proj = (lp.xyz / lp.w) * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;

    // Quanto este ponto está DENTRO do tronco: 0 na borda, 1 no miolo.
    vec2  b    = min(proj.xy, vec2(1.0) - proj.xy);
    float edge = smoothstep(0.0, 0.06, min(b.x, b.y));

    // E quanto falta para o fim da última cascata, em metros. Os 20%
    // finais são a rampa — larga o bastante para a emenda não se ler,
    // curta o bastante para não comer sombra útil.
    float far  = max(u_FogCascadeSplit[u_FogCascadeCount - 1], 0.001);
    float tail = 1.0 - smoothstep(far * 0.8, far, depth);

    float d   = texture(u_FogShadowArray, vec3(proj.xy, float(cascade))).r;
    float lit = (proj.z - 0.0015 > d) ? 0.0 : 1.0;

    return mix(1.0, lit, edge * tail);
}

// Henyey-Greenstein: a fase que faz a névoa acender quando você olha NA
// DIREÇÃO do sol e apagar de costas para ele.
//
// ── O 1/4pi NÃO É DECORAÇÃO ────────────────────────────────────────────
//
// Sem ele a fase vale 10.0 no pico (g = 0.6). Multiplicada por uma
// intensidade de sol típica de exterior (7 a 8) e pelo Scatter Strength,
// dá algo em torno de 43 em HDR: qualquer tone mapping devolve BRANCO
// PURO ao olhar na direção do sol. Com a normalização o pico vai a 0.80,
// e a mesma cena entrega ~3.4 — halo forte, sem estourar.
//
// 1/4pi é o que torna a fase uma DISTRIBUIÇÃO: integrada sobre a esfera
// ela soma 1, ou seja, redistribui a luz que existe em vez de inventar
// luz nova. Foi medido antes de escrever esta linha, não estimado.
//
// g = 0.6 e não 0.85: o 0.85 da fase de Mie das point lights concentra
// 232:1 entre frente e costas, e a névoa sumiria assim que o jogador
// virasse de costas para o sol. O que sustenta a névoa nessa direção é o
// termo AMBIENTE, que é justamente a luz do céu — a separação é essa.
//
// O termo das point lights ficou como estava, sem normalizar, de
// propósito: mudá-lo alteraria a imagem de cena já ajustada, e a
// atenuação por raio já segura a magnitude lá.
float SunPhase(float cosTheta)
{
    const float g = 0.6;
    const float kInv4Pi = 0.07957747;
    float denom = 1.0 + g * g - 2.0 * g * cosTheta;
    return kInv4Pi * (1.0 - g * g) / max(pow(max(denom, 0.0001), 1.5), 0.0001);
}

// Quanto da luz EXTERNA (céu) sobrevive neste ponto do volume — combina
// Interior Volumes (SDF de caixa, mesma matemática do lighting pass) e a
// visibilidade do céu das probes. 1.0 ao ar livre, 0.0 no fundo de uma
// sala fechada. Custa 1 SDF por volume + 1 sample 3D por passo do ray
// march — barato (12 passos default).
float ExternalLightFactor(vec3 pos)
{
    float f = 1.0;

    for (int i = 0; i < u_NumInteriorVolumes; ++i)
    {
        if ((u_InteriorAffect[i] & 2) == 0) continue;
        vec3 local = (u_InteriorWorldToLocal[i] * vec4(pos, 1.0)).xyz;
        vec3 d = abs(local) - u_InteriorHalfExtents[i];
        float dist = length(max(d, vec3(0.0))) + min(max(d.x, max(d.y, d.z)), 0.0);
        float inside = 1.0 - smoothstep(-u_InteriorBlend[i], 0.0, dist);
        f = min(f, 1.0 - inside * u_InteriorIntensity[i]);
    }

    // Loop de índice dynamically uniform (bound é uniform) — indexar o
    // array de sampler assim é legal no GL 4.6
    for (int i = 0; i < u_NumProbeVolumes; ++i)
    {
        vec3 local = (u_ProbeWorldToLocal[i] * vec4(pos, 1.0)).xyz;
        vec3 d = abs(local) - u_ProbeHalfExtents[i];
        float dist = length(max(d, vec3(0.0))) + min(max(d.x, max(d.y, d.z)), 0.0);
        float w = 1.0 - smoothstep(-u_ProbeFeather[i], 0.0, dist);
        if (w > 0.0)
        {
            vec3 uvw = clamp(local / (2.0 * u_ProbeHalfExtents[i]) + 0.5, 0.0, 1.0);
            float skyVis = texture(u_ProbeSH0[i], uvw).a;
            // Mesmo remap x2 da oclusão de sol: exterior em chão plano
            // enxerga ~50% do céu (o chão come o hemisfério de baixo)
            f = min(f, mix(1.0, min(skyVis * 2.0, 1.0), w));
        }
    }

    return f;
}

void main()
{
    float depth = texture(u_Depth, v_UV).r;
    vec3  sceneColor = texture(u_SceneColor, v_UV).rgb;

    // ── VOLUME_SKY_V3 — "sem opaco atrás" NÃO é "sem névoa" ────────────
    //
    // Aqui havia um `return` quando depth >= 0.9999, com o comentário
    // "nada a fazer no fundo, que é o skybox". A premissa estava errada de
    // duas maneiras, e a segunda é a cara:
    //
    // 1. O CÉU tem névoa. Um raio que sai rasante ao horizonte atravessa
    //    quilômetros de ar; é exatamente por isso que o horizonte
    //    embranquece na vida real. Desistir dele impede para sempre o
    //    efeito que mais se quer de fog: o horizonte dissolvendo.
    //
    // 2. E o que chega aqui como "fundo" NÃO é só o céu. Este passe lê a
    //    profundidade do G-BUFFER, que só tem geometria OPACA — e a
    //    pipeline translúcida tem `DepthWrite = false`. Então TODA
    //    superfície translúcida chega com depth 1.0: uma água que ocupa a
    //    tela inteira era classificada como céu e pulada. Numa cena de mar
    //    aberto, o fog volumétrico só encostava nas pedras.
    //
    // Marchar até u_FogEnd nesses pixels é a resposta certa: o meio existe
    // independentemente de haver algo atrás dele para pintar.
    //
    // A reconstrução usa 0.9999 e não 1.0 de propósito — no plano distante
    // exato o `w` da inversa degenera e a direção do raio sai lixo. 0.9999
    // dá a MESMA direção sem chegar na singularidade, e a distância vem do
    // u_FogEnd de qualquer jeito.
    //
    // CUSTO: pixel de céu deixou de ser grátis. Numa tela com metade de
    // céu, o passe quase dobra. Ray Steps é o botão.
    bool  isBackground = depth >= 0.9999;

    vec3  worldPos  = ReconstructWorld(v_UV, isBackground ? 0.9999 : depth);
    vec3  rayDir    = worldPos - u_CameraPos;
    float rayLen    = length(rayDir);
    vec3  rayDirN   = rayDir / max(rayLen, 1e-6);

    float startDist = u_FogStart;
    float endDist   = isBackground ? u_FogEnd : min(rayLen, u_FogEnd);
    if (endDist <= startDist) { FragColor = vec4(sceneColor, 1.0); return; }

    float stepSize = (endDist - startDist) / float(u_Steps);
    // Jitter pra suavizar banding sem TAA.
    //
    // VOLUME_DOMAIN_V1 — o `* u_StepJitter` no fim ENTROU nesta rodada. A
    // uniform era declarada, enviada pelo Execute e exposta no Inspector como
    // "Jitter", e nunca era lida: mexer no controle nao fazia nada. Achado ao
    // escrever o ray march gerado, que tinha de reproduzir este aqui — a
    // reproducao e que denuncia a linha morta.
    float jitter = fract(sin(dot(v_UV, vec2(12.9898, 78.233)) + u_Time * 0.07) * 43758.5453)
                 * u_StepJitter;

    vec3  fogAccum      = vec3(0.0);
    float transmittance = 1.0;

    for (int i = 0; i < u_Steps; ++i)
    {
        float t   = startDist + (float(i) + jitter) * stepSize;
        vec3  pos = u_CameraPos + rayDirN * t;

        float localDensity = u_Density * HeightDensity(pos.y);
        float stepTrans    = exp(-localDensity * stepSize);
        float stepWeight   = transmittance * (1.0 - stepTrans);

        // Ambient fog — atenuado pela luz externa disponível NESTE ponto
        // do raio: o godray/névoa do céu morre ao cruzar a porta da sala,
        // por amostra, exatamente como a luz de superfície faz por pixel.
        fogAccum += u_FogColor * u_AmbientStrength * stepWeight * ExternalLightFactor(pos);

        // ── VOLUME_SUN_V2 — inscattering do SOL ───────────────────────
        //
        // O termo que faltava. Sem ele, numa cena de exterior iluminada
        // só pela direcional, a névoa inteira dependia do ambiente
        // chapado — e u_ScatterStrength não fazia nada, porque só
        // multiplicava as point lights, que muitas cenas não têm.
        if (u_SunIntensity > 0.0)
        {
            float cosSun = dot(rayDirN, -u_SunDirection);
            fogAccum += u_SunColor * u_SunIntensity * u_ScatterStrength
                      * SunPhase(cosSun) * stepWeight * SunLightFactor(pos);
        }

        // Inscattering das point lights
        for (int li = 0; li < u_NumLights && li < 8; ++li)
        {
            vec3  toLight = u_LightPos[li] - pos;
            float dist    = length(toLight);
            float atten   = LightAttenuation(dist, u_LightRadius[li]);
            if (atten <= 0.001) continue;

            // Spot light — só scatter dentro do cone.
            // Fora do cone a luz não ilumina o volume (sem halo).
            if (u_LightIsSpot[li] == 1)
            {
                vec3  toLightN  = toLight / dist;
                float cosAngle  = dot(-toLightN, u_LightDir[li]);
                // Transição suave na borda externa do cone
                float spotAtt   = smoothstep(u_LightOuterCut[li] - 0.05,
                                             u_LightOuterCut[li], cosAngle);
                if (spotAtt <= 0.001) continue;
                atten *= spotAtt;
            }

            // Fase de Mie simplificada (forward scatter)
            float cosTheta = dot(rayDirN, toLight / dist);
            float mie      = (1.0 - 0.85 * 0.85) /
                             pow(1.0 + 0.85 * 0.85 - 2.0 * 0.85 * cosTheta, 1.5);
            mie = max(0.0, mie);

            fogAccum += u_LightColor[li] * u_LightIntensity[li] * atten
                       * u_ScatterStrength * mie * stepWeight;
        }

        transmittance *= stepTrans;
        if (transmittance < 0.01) break;
    }

    // Compositing: fog sobre a cena
    vec3 finalColor = sceneColor * transmittance + fogAccum;
    FragColor = vec4(finalColor, 1.0);
}
)";

    void OpenGLVolumetricFogPass::Initialize()
    {
        m_Shader = Shader::Create(kFogVS, kFogFS);

        static float quad[] = {
           -1.f,-1.f, 0.f,0.f,  1.f,-1.f, 1.f,0.f,
            1.f, 1.f, 1.f,1.f, -1.f, 1.f, 0.f,1.f
        };
        BufferLayout layout = {
            { ShaderDataType::Float2, sizeof(float) * 2, false },
            { ShaderDataType::Float2, sizeof(float) * 2, false }
        };
        m_QuadVBO = VertexBuffer::Create(quad, sizeof(quad));
        m_QuadVAO = VertexArray::Create();
        m_QuadVAO->AddVertexBuffer(m_QuadVBO, layout);

        AXE_CORE_INFO("OpenGLVolumetricFogPass: inicializado.");
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  VOLUME_DOMAIN_V1 — o shader do MATERIAL no lugar do embutido
    //
    //  Falhar aqui NAO e fatal, e essa e a escolha de desenho: material
    //  ausente, `.axeshader` de outro dominio, pacote sem o cozido — qualquer
    //  um deles devolve nullptr e o passe segue com o fog embutido. A cena sai
    //  como saia antes, e nao preta.
    //
    //  O cache por (UUID, geracao) e o que faz o botao Compile do Material
    //  Editor ter efeito imediato: ele chama MaterialShaderCache::Invalidate, a
    //  geracao sobe, e este cache se rende no proximo frame. Sem isso o autor
    //  recompilaria o fog e continuaria vendo o shader anterior ate reabrir a
    //  cena.
    // ═════════════════════════════════════════════════════════════════════════
    std::shared_ptr<Shader> OpenGLVolumetricFogPass::ResolveVolumeShader(const std::string& uuid)
    {
        if (uuid.empty())
        {
            m_VolumeShader.reset();
            m_VolumeSamplers.clear();
            m_VolumeUUID.clear();
            m_VolumeGen = 0;
            return nullptr;
        }

        const std::uint64_t gen = MaterialShaderCache::Generation();
        if (uuid == m_VolumeUUID && gen == m_VolumeGen) return m_VolumeShader;

        m_VolumeUUID = uuid;
        m_VolumeGen = gen;
        m_VolumeShader.reset();
        m_VolumeSamplers.clear();

        if (!MaterialShaderCache::Resolve(uuid, CookedMaterialDomain::Volume,
            m_VolumeShader, m_VolumeSamplers))
        {
            AXE_CORE_WARN("VolumetricFog: material '{}' nao resolveu — fog embutido.", uuid);
            m_VolumeShader.reset();
        }

        return m_VolumeShader;
    }

    // ── VOLUME_SUN_V2 ────────────────────────────────────────────────────────
    void OpenGLVolumetricFogPass::SetSun(const glm::vec3& direction,
        const glm::vec3& color, float intensity)
    {
        // normalize de vetor nulo e NaN, e NaN aqui apagaria o fog inteiro sem
        // erro nenhum. Direcao degenerada = sem sol.
        const float len = glm::length(direction);
        if (len < 1e-6f || intensity <= 0.0f)
        {
            m_SunIntensity = 0.0f;
            return;
        }

        m_SunDirection = direction / len;
        m_SunColor = color;
        m_SunIntensity = intensity;
    }

    // Corpo IDENTICO ao MeshRenderer::SetCascadedShadow de proposito — a agua e
    // o fog tem de concordar sobre onde esta a sombra, e duas leituras
    // diferentes da mesma cascata divergiriam na primeira mudanca de uma delas.
    void OpenGLVolumetricFogPass::SetCascadedShadow(const CascadedShadowPass* csm,
        const glm::mat4& view)
    {
        if (!csm || !csm->IsInitialized() || csm->GetCascadeCount() <= 0)
        {
            m_CascadeCount = 0;
            m_CascadeArrayID = 0;
            return;
        }

        m_CascadeArrayID = csm->GetDepthArrayID();
        m_CascadeView = view;
        m_CascadeCount = csm->GetCascadeCount();
        if (m_CascadeCount > AXE_SHADOW_CASCADES) m_CascadeCount = AXE_SHADOW_CASCADES;

        const auto& cascades = csm->GetCascades();
        for (int i = 0; i < m_CascadeCount; ++i)
        {
            m_CascadeMatrices[i] = cascades[i].LightSpaceMatrix;
            m_CascadeSplits[i] = cascades[i].SplitDepth;
            m_CascadeTexels[i] = cascades[i].TexelWorldSize;
        }
    }

    void OpenGLVolumetricFogPass::EnsureSceneColorTex(uint32_t w, uint32_t h)
    {
        if (m_SceneColorTex == 0)
        {
            glGenTextures(1, &m_SceneColorTex);
            glBindTexture(GL_TEXTURE_2D, m_SceneColorTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (w != m_LastW || h != m_LastH)
        {
            m_LastW = w; m_LastH = h;
            glBindTexture(GL_TEXTURE_2D, m_SceneColorTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        // Copia o framebuffer atual para a textura de cena
        glBindTexture(GL_TEXTURE_2D, m_SceneColorTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void OpenGLVolumetricFogPass::Execute(
        const GBuffer& gbuffer,
        const VolumetricFogSettings& settings,
        const glm::mat4& invViewProj,
        const glm::vec3& cameraPosition,
        const std::vector<PointLight>& pointLights,
        float                        time,
        uint32_t                     width,
        uint32_t                     height,
        const std::vector<InteriorVolumeData>& interiorVolumes,
        const std::vector<ProbeVolumeData>& probeVolumes)
    {
        if (!m_Shader || !m_QuadVAO) return;

        EnsureSceneColorTex(width, height);

        // ── VOLUME_DOMAIN_V1 — a UNICA escolha nova deste Execute ────────────
        //
        // Tudo abaixo desta linha continua igual, e de proposito: o shader do
        // material obedece ao mesmo contrato de uniforms do embutido, entao a
        // troca e de UM ponteiro. Se o envio de uniforms tivesse de saber qual
        // dos dois esta ligado, o contrato ja teria sido quebrado.
        auto volumeShader = ResolveVolumeShader(settings.MaterialUUID);
        const std::shared_ptr<Shader>& shader = volumeShader ? volumeShader : m_Shader;

        shader->Bind();
        shader->SetInt("u_SceneColor", 0);
        shader->SetInt("u_Depth", 1);
        shader->SetMat4("u_InvViewProj", glm::value_ptr(invViewProj));
        shader->SetFloat3("u_CameraPos", cameraPosition);
        // As duas que so o shader gerado le. No embutido nao existem, e uniform
        // inexistente devolve location -1: chamada sem efeito, sem erro.
        //
        // u_CameraPosition carrega o MESMO valor de u_CameraPos. Os nodes do
        // grafo (Camera Vector, Pixel Depth, Camera Position) emitem aquele
        // nome nos outros cinco dominios; renomear qualquer um dos dois lados
        // quebraria o outro.
        shader->SetFloat3("u_CameraPosition", cameraPosition);
        shader->SetFloat2("u_ScreenSize", glm::vec2((float)width, (float)height));
        shader->SetFloat3("u_FogColor", settings.FogColor);
        shader->SetFloat("u_Density", settings.Density);
        shader->SetFloat("u_HeightBase", settings.HeightBase);
        shader->SetFloat("u_HeightFalloff", settings.HeightFalloff);
        shader->SetFloat("u_ScatterStrength", settings.ScatterStrength);
        shader->SetFloat("u_AmbientStrength", settings.AmbientStrength);
        shader->SetFloat("u_FogStart", settings.FogStart);
        shader->SetFloat("u_FogEnd", settings.FogEnd);
        // VOLUME_DOMAIN_V1 — o teto de 64 casa com o `for (i < 64)` do ray
        // march GERADO, que usa limite constante para o driver poder otimizar
        // o laco. O slider do Inspector ja para em 32; o clamp aqui e para uma
        // cena com valor gravado a mao nao fazer os dois shaders divergirem em
        // numero de passos — divergencia que so apareceria como "o fog muda ao
        // trocar o material", que e o pior formato de defeito para diagnosticar.
        shader->SetInt("u_Steps", std::clamp(settings.Steps, 1, 64));
        shader->SetFloat("u_StepJitter", settings.StepJitter);
        shader->SetFloat("u_Time", time);

        int numLights = (int)std::min((int)pointLights.size(), kMaxFogLights);
        shader->SetInt("u_NumLights", numLights);
        for (int i = 0; i < numLights; ++i)
        {
            shader->SetFloat3(("u_LightPos[" + std::to_string(i) + "]").c_str(), pointLights[i].Position);
            shader->SetFloat3(("u_LightColor[" + std::to_string(i) + "]").c_str(), pointLights[i].Color);
            shader->SetFloat(("u_LightIntensity[" + std::to_string(i) + "]").c_str(), pointLights[i].Intensity);
            shader->SetFloat(("u_LightRadius[" + std::to_string(i) + "]").c_str(), pointLights[i].Radius);
            // Spot light — cone check no volume de fog
            bool isSpot = pointLights[i].IsSpot;
            shader->SetInt(("u_LightIsSpot[" + std::to_string(i) + "]").c_str(), isSpot ? 1 : 0);
            if (isSpot)
            {
                shader->SetFloat3(("u_LightDir[" + std::to_string(i) + "]").c_str(),
                    glm::normalize(pointLights[i].Direction));
                shader->SetFloat(("u_LightOuterCut[" + std::to_string(i) + "]").c_str(),
                    std::cos(glm::radians(pointLights[i].OuterConeAngle)));
            }
            else
            {
                shader->SetInt(("u_LightIsSpot[" + std::to_string(i) + "]").c_str(), 0);
            }
        }

        // Interiores — mesmos dados do lighting pass, mesma SDF
        int numVolumes = (int)std::min(interiorVolumes.size(), (size_t)8);
        shader->SetInt("u_NumInteriorVolumes", numVolumes);
        for (int i = 0; i < numVolumes; ++i)
        {
            const auto& iv = interiorVolumes[i];
            std::string idx = "[" + std::to_string(i) + "]";
            shader->SetMat4("u_InteriorWorldToLocal" + idx, glm::value_ptr(iv.WorldToLocal));
            shader->SetFloat3("u_InteriorHalfExtents" + idx, iv.HalfExtents);
            shader->SetFloat("u_InteriorIntensity" + idx, iv.Intensity);
            shader->SetFloat("u_InteriorBlend" + idx, iv.BlendDistance);
            int affect = (iv.AffectDirect ? 1 : 0) | (iv.AffectAmbient ? 2 : 0);
            shader->SetInt("u_InteriorAffect" + idx, affect);
        }

        // Probe skyVis — só a SH0 (o alpha) de cada volume, nos units
        // 2-3 (0=cena, 1=depth). Mesmo limite de 2 volumes do lighting.
        {
            int uploaded = 0;
            for (size_t i = 0; i < probeVolumes.size() && uploaded < 2; i++)
            {
                const auto& pv = probeVolumes[i];
                if (!pv.Grid || !pv.Grid->IsValid()) continue;
                std::string idx = "[" + std::to_string(uploaded) + "]";
                pv.Grid->SH0->Bind(2 + uploaded);
                shader->SetInt("u_ProbeSH0" + idx, 2 + uploaded);
                shader->SetMat4("u_ProbeWorldToLocal" + idx, glm::value_ptr(pv.WorldToLocal));
                shader->SetFloat3("u_ProbeHalfExtents" + idx, pv.HalfExtents);
                shader->SetFloat("u_ProbeFeather" + idx, std::max(pv.Feather, 0.0001f));
                uploaded++;
            }
            shader->SetInt("u_NumProbeVolumes", uploaded);
        }

        // ── VOLUME_SUN_V2 — sol e cascata ────────────────────────────────────
        shader->SetFloat3("u_SunDirection", m_SunDirection);
        shader->SetFloat3("u_SunColor", m_SunColor);
        shader->SetFloat("u_SunIntensity", m_SunIntensity);

        if (m_CascadeCount > 0 && m_CascadeArrayID != 0)
        {
            // Unidade 13: mesma escolha e mesma razao do MeshRenderer. O
            // lighting pass prende sampler object nas unidades 11 e 12, e
            // sampler object com comparacao ligada sobre amostragem crua e
            // comportamento indefinido. A 13 nao e tocada por passe nenhum.
            RenderCommand::BindTextureUnit(13, m_CascadeArrayID);
            shader->SetInt("u_FogShadowArray", 13);
            shader->SetInt("u_FogCascadeCount", m_CascadeCount);
            shader->SetMat4("u_FogShadowView", glm::value_ptr(m_CascadeView));

            for (int i = 0; i < m_CascadeCount; ++i)
            {
                const std::string idx = "[" + std::to_string(i) + "]";
                shader->SetMat4("u_FogCascadeMatrix" + idx,
                    glm::value_ptr(m_CascadeMatrices[i]));
                shader->SetFloat("u_FogCascadeSplit" + idx, m_CascadeSplits[i]);
            }
        }
        else
        {
            // Obrigatorio ENVIAR o desligamento: uniform nao enviada vale zero
            // em silencio, e zero aqui seria "cascata 0 valida", que amostraria
            // a unidade 13 com o que estivesse nela.
            shader->SetInt("u_FogCascadeCount", 0);
        }

        // VOLUME_DOMAIN_V1 — as texturas do grafo, a partir da unidade 4.
        //
        // 0 = cena, 1 = profundidade, 2 e 3 = as SH0 das probes, 13 a cascata.
        // O CompileVolume numera os samplers do grafo (u_VolTex_0,
        // u_VolTex_1...) na MESMA ordem em que o mapa e percorrido aqui — os
        // dois lados leem um std::map, ordenado por nome. Nao havendo material,
        // o mapa esta vazio e nada disto roda.
        //
        // Teto em 11 e nao em 16: 11 e 12 podem estar com sampler object preso
        // pelo lighting pass, e 13 e da cascata.
        {
            int unit = 4;
            for (auto& [name, tex] : m_VolumeSamplers)
            {
                if (!tex) continue;
                if (unit >= 11) break;
                tex->Bind(unit);
                shader->SetInt(name, unit);
                ++unit;
            }
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_SceneColorTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gbuffer.GetDepthID());

        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetBlend(false);

        m_QuadVAO->Bind();
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        m_QuadVAO->Unbind();

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);

        // VOLUME_SUN_V2 — solta a cascata. Via RenderCommand, como o bind:
        // glBindTextureUnit e agnostico de alvo e nao mexe na unidade ATIVA,
        // entao nao ha estado de glActiveTexture para restaurar depois.
        if (m_CascadeCount > 0 && m_CascadeArrayID != 0)
            RenderCommand::BindTextureUnit(13, 0);

        shader->Unbind();
    }

} // namespace axe