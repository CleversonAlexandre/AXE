#include "opengl_ssr_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/renderer/gbuffer.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

namespace axe
{
    static const char* kVS = R"(
#version 460 core
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;
out vec2 v_UV;
void main() { gl_Position = vec4(a_Pos, 0.0, 1.0); v_UV = a_UV; }
)";

    // SSR em VIEW SPACE — position/normal já vêm em view space do GBuffer.
    // Ray march linear + refinamento binário no ponto de interseção.
    static const char* kFS = R"(
#version 460 core
in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_SceneColor;  // cor da cena (resultado do lighting)
uniform sampler2D u_Position;    // posição view space (GBuffer 0)
uniform sampler2D u_Normal;      // normal view space (GBuffer 1)
uniform sampler2D u_PBR;         // r=roughness, g=ao (GBuffer 3)
uniform mat4  u_Projection;      // matriz de projeção (view→clip)
uniform float u_MaxDistance;
uniform int   u_MaxSteps;
uniform int   u_BinaryRefine;
uniform float u_Thickness;
uniform float u_MaxRoughness;
uniform float u_Intensity;
uniform float u_EdgeFade;

// Projeta um ponto view space para UV de tela [0,1]
vec2 ViewToUV(vec3 viewPos)
{
    vec4 clip = u_Projection * vec4(viewPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    return ndc.xy * 0.5 + 0.5;
}

void main()
{
    vec3  sceneColor = texture(u_SceneColor, v_UV).rgb;
    float roughness  = texture(u_PBR, v_UV).r;

    // Superfícies ásperas não geram reflexão nítida — early out
    if (roughness > u_MaxRoughness)
    {
        FragColor = vec4(sceneColor, 1.0);
        return;
    }

    vec3 viewPos    = texture(u_Position, v_UV).xyz;
    vec3 viewNormal = normalize(texture(u_Normal, v_UV).xyz);

    // Skybox / fundo (posição zerada) → sem reflexão
    if (length(viewPos) < 0.001)
    {
        FragColor = vec4(sceneColor, 1.0);
        return;
    }

    // Direção de reflexão do vetor câmera→ponto sobre a normal
    vec3 viewDir     = normalize(viewPos);
    vec3 reflectDir  = normalize(reflect(viewDir, viewNormal));

    // ── Ray march linear em view space ──────────────────────────────────────
    float stepSize = u_MaxDistance / float(u_MaxSteps);
    vec3  rayPos   = viewPos;
    vec3  hitUV    = vec3(0.0);
    bool  hit      = false;

    for (int i = 0; i < u_MaxSteps; ++i)
    {
        rayPos += reflectDir * stepSize;

        // Projeta o ponto atual do raio para tela
        vec2 uv = ViewToUV(rayPos);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        // Compara profundidade do raio com a geometria naquele pixel
        vec3  sampledPos = texture(u_Position, uv).xyz;
        float depthDiff  = rayPos.z - sampledPos.z;

        // rayPos.z é mais negativo (mais longe) que a superfície = passou por trás
        if (depthDiff < 0.0 && depthDiff > -u_Thickness)
        {
            hit = true;
            hitUV = vec3(uv, 1.0);

            // ── Refinamento binário ──────────────────────────────────────
            vec3 refineStart = rayPos - reflectDir * stepSize;
            vec3 refineEnd   = rayPos;
            for (int j = 0; j < u_BinaryRefine; ++j)
            {
                vec3 mid = (refineStart + refineEnd) * 0.5;
                vec2 midUV = ViewToUV(mid);
                vec3 midSampled = texture(u_Position, midUV).xyz;
                float midDiff = mid.z - midSampled.z;
                if (midDiff < 0.0) refineEnd = mid;
                else               refineStart = mid;
                hitUV.xy = ViewToUV((refineStart + refineEnd) * 0.5);
            }
            break;
        }
    }

    if (!hit)
    {
        FragColor = vec4(sceneColor, 1.0);
        return;
    }

    // ── Reflexão encontrada — sample da cor + fades ─────────────────────────
    vec3 reflectedColor = texture(u_SceneColor, hitUV.xy).rgb;

    // Edge fade — reflexões perto da borda da tela somem suavemente
    vec2 edge = smoothstep(vec2(0.0), vec2(u_EdgeFade), hitUV.xy) *
                smoothstep(vec2(0.0), vec2(u_EdgeFade), 1.0 - hitUV.xy);
    float edgeFade = edge.x * edge.y;

    // Roughness fade — superfícies quase no limite refletem mais fraco
    float roughFade = 1.0 - smoothstep(0.0, u_MaxRoughness, roughness);

    // Fresnel — reflexão mais forte em ângulos rasantes
    float fresnel = pow(1.0 - max(dot(-viewDir, viewNormal), 0.0), 3.0);
    fresnel = mix(0.2, 1.0, fresnel);

    float reflectStrength = edgeFade * roughFade * fresnel * u_Intensity;

    vec3 finalColor = mix(sceneColor, reflectedColor, reflectStrength);
    FragColor = vec4(finalColor, 1.0);
}
)";

    OpenGLSSRPass::~OpenGLSSRPass() { DestroyTargets(); }

    void OpenGLSSRPass::CreateTargets(uint32_t w, uint32_t h)
    {
        DestroyTargets();
        m_Width = w; m_Height = h;

        glGenTextures(1, &m_OutputTex);
        glBindTexture(GL_TEXTURE_2D, m_OutputTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &m_OutputFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, m_OutputTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void OpenGLSSRPass::DestroyTargets()
    {
        if (m_OutputTex) { glDeleteTextures(1, &m_OutputTex); m_OutputTex = 0; }
        if (m_OutputFBO) { glDeleteFramebuffers(1, &m_OutputFBO); m_OutputFBO = 0; }
    }

    void OpenGLSSRPass::Initialize(uint32_t w, uint32_t h)
    {
        m_Shader = Shader::Create(kVS, kFS);

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

        CreateTargets(w, h);
        AXE_CORE_INFO("OpenGLSSRPass: inicializado {}x{}.", w, h);
    }

    void OpenGLSSRPass::Resize(uint32_t w, uint32_t h)
    {
        if (w == m_Width && h == m_Height) return;
        CreateTargets(w, h);
    }

    uint32_t OpenGLSSRPass::Execute(
        const GBuffer& gbuffer, uint32_t sceneColorID,
        const glm::mat4& projection, const glm::mat4& view,
        const SSRSettings& settings,
        uint32_t width, uint32_t height)
    {
        if (!m_Shader || !m_QuadVAO) return sceneColorID;
        if (width != m_Width || height != m_Height) Resize(width, height);

        glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
        glViewport(0, 0, width, height);

        m_Shader->Bind();
        m_Shader->SetInt("u_SceneColor", 0);
        m_Shader->SetInt("u_Position", 1);
        m_Shader->SetInt("u_Normal", 2);
        m_Shader->SetInt("u_PBR", 3);
        m_Shader->SetMat4("u_Projection", glm::value_ptr(projection));
        m_Shader->SetFloat("u_MaxDistance", settings.MaxDistance);
        m_Shader->SetInt("u_MaxSteps", settings.MaxSteps);
        m_Shader->SetInt("u_BinaryRefine", settings.BinaryRefine);
        m_Shader->SetFloat("u_Thickness", settings.Thickness);
        m_Shader->SetFloat("u_MaxRoughness", settings.MaxRoughness);
        m_Shader->SetFloat("u_Intensity", settings.Intensity);
        m_Shader->SetFloat("u_EdgeFade", settings.EdgeFade);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneColorID);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gbuffer.GetPositionID());
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gbuffer.GetNormalID());
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, gbuffer.GetPBRID());

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        m_QuadVAO->Bind();
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        m_QuadVAO->Unbind();

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);

        for (int i = 0; i < 4; ++i)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        m_Shader->Unbind();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return m_OutputTex;
    }

} // namespace axe