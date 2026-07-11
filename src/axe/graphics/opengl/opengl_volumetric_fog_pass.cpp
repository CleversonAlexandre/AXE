#include "opengl_volumetric_fog_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/renderer/gbuffer.hpp"
#include "axe/graphics/texture3d.hpp"
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

    // Nada a fazer no fundo (depth = 1.0 no skybox)
    if (depth >= 0.9999) { FragColor = vec4(sceneColor, 1.0); return; }

    vec3  worldPos  = ReconstructWorld(v_UV, depth);
    vec3  rayDir    = worldPos - u_CameraPos;
    float rayLen    = length(rayDir);
    vec3  rayDirN   = rayDir / rayLen;

    float startDist = u_FogStart;
    float endDist   = min(rayLen, u_FogEnd);
    if (endDist <= startDist) { FragColor = vec4(sceneColor, 1.0); return; }

    float stepSize = (endDist - startDist) / float(u_Steps);
    // Jitter pra suavizar banding sem TAA
    float jitter = fract(sin(dot(v_UV, vec2(12.9898, 78.233)) + u_Time * 0.07) * 43758.5453);

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

        m_Shader->Bind();
        m_Shader->SetInt("u_SceneColor", 0);
        m_Shader->SetInt("u_Depth", 1);
        m_Shader->SetMat4("u_InvViewProj", glm::value_ptr(invViewProj));
        m_Shader->SetFloat3("u_CameraPos", cameraPosition);
        m_Shader->SetFloat3("u_FogColor", settings.FogColor);
        m_Shader->SetFloat("u_Density", settings.Density);
        m_Shader->SetFloat("u_HeightBase", settings.HeightBase);
        m_Shader->SetFloat("u_HeightFalloff", settings.HeightFalloff);
        m_Shader->SetFloat("u_ScatterStrength", settings.ScatterStrength);
        m_Shader->SetFloat("u_AmbientStrength", settings.AmbientStrength);
        m_Shader->SetFloat("u_FogStart", settings.FogStart);
        m_Shader->SetFloat("u_FogEnd", settings.FogEnd);
        m_Shader->SetInt("u_Steps", settings.Steps);
        m_Shader->SetFloat("u_StepJitter", settings.StepJitter);
        m_Shader->SetFloat("u_Time", time);

        int numLights = (int)std::min((int)pointLights.size(), kMaxFogLights);
        m_Shader->SetInt("u_NumLights", numLights);
        for (int i = 0; i < numLights; ++i)
        {
            m_Shader->SetFloat3(("u_LightPos[" + std::to_string(i) + "]").c_str(), pointLights[i].Position);
            m_Shader->SetFloat3(("u_LightColor[" + std::to_string(i) + "]").c_str(), pointLights[i].Color);
            m_Shader->SetFloat(("u_LightIntensity[" + std::to_string(i) + "]").c_str(), pointLights[i].Intensity);
            m_Shader->SetFloat(("u_LightRadius[" + std::to_string(i) + "]").c_str(), pointLights[i].Radius);
            // Spot light — cone check no volume de fog
            bool isSpot = pointLights[i].IsSpot;
            m_Shader->SetInt(("u_LightIsSpot[" + std::to_string(i) + "]").c_str(), isSpot ? 1 : 0);
            if (isSpot)
            {
                m_Shader->SetFloat3(("u_LightDir[" + std::to_string(i) + "]").c_str(),
                    glm::normalize(pointLights[i].Direction));
                m_Shader->SetFloat(("u_LightOuterCut[" + std::to_string(i) + "]").c_str(),
                    std::cos(glm::radians(pointLights[i].OuterConeAngle)));
            }
            else
            {
                m_Shader->SetInt(("u_LightIsSpot[" + std::to_string(i) + "]").c_str(), 0);
            }
        }

        // Interiores — mesmos dados do lighting pass, mesma SDF
        int numVolumes = (int)std::min(interiorVolumes.size(), (size_t)8);
        m_Shader->SetInt("u_NumInteriorVolumes", numVolumes);
        for (int i = 0; i < numVolumes; ++i)
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
                m_Shader->SetInt("u_ProbeSH0" + idx, 2 + uploaded);
                m_Shader->SetMat4("u_ProbeWorldToLocal" + idx, glm::value_ptr(pv.WorldToLocal));
                m_Shader->SetFloat3("u_ProbeHalfExtents" + idx, pv.HalfExtents);
                m_Shader->SetFloat("u_ProbeFeather" + idx, std::max(pv.Feather, 0.0001f));
                uploaded++;
            }
            m_Shader->SetInt("u_NumProbeVolumes", uploaded);
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

        m_Shader->Unbind();
    }

} // namespace axe