#include "opengl_taa_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>

namespace axe
{
    // ── Shaders ───────────────────────────────────────────────────────────────

    static const char* kVS = R"(
#version 460 core
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;
out vec2 v_UV;
void main() { gl_Position = vec4(a_Pos, 0.0, 1.0); v_UV = a_UV; }
)";

    static const char* kFS = R"(
#version 460 core
in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_Current;      // frame atual (HDR)
uniform sampler2D u_History;      // frame anterior resolvido
uniform sampler2D u_Depth;        // depth buffer do GBuffer
uniform mat4  u_InvViewProj;      // inverso da VP atual
uniform mat4  u_PrevViewProj;     // VP do frame anterior
uniform float u_ResolutionX;      // largura do viewport
uniform float u_ResolutionY;      // altura do viewport
uniform float u_JitterX;          // offset de jitter X
uniform float u_JitterY;          // offset de jitter Y
uniform float u_BlendFactor;
uniform float u_EmissiveLumMin;
uniform float u_EmissiveLumMax;
uniform float u_EmissiveBlendMax;
uniform float u_TemporalSensitivity;
uniform int   u_Sharpen;
uniform float u_SharpenAmount;

// YCoCg — espaço de cor mais adequado pra neighborhood clamping
vec3 RGBToYCoCg(vec3 c) {
    float co = c.r - c.b;
    float tmp = c.b + co * 0.5;
    float cg = c.g - tmp;
    return vec3(tmp + cg * 0.5, co, cg);
}
vec3 YCoCgToRGB(vec3 c) {
    float tmp = c.x - c.z * 0.5;
    float g = c.z + tmp;
    float b = tmp - c.y * 0.5;
    return max(vec3(0.0), vec3(b + c.y, g, b));
}

void main()
{
    vec2 texel = 1.0 / vec2(u_ResolutionX, u_ResolutionY);

    // ── Amostras vizinhas pra neighborhood clamping ────────────────────────
    // Usamos cruz 5-tap + 4 cantos = padrão 3×3
    vec3 c00 = RGBToYCoCg(texture(u_Current, v_UV + texel * vec2(-1,-1)).rgb);
    vec3 c10 = RGBToYCoCg(texture(u_Current, v_UV + texel * vec2( 0,-1)).rgb);
    vec3 c20 = RGBToYCoCg(texture(u_Current, v_UV + texel * vec2( 1,-1)).rgb);
    vec3 c01 = RGBToYCoCg(texture(u_Current, v_UV + texel * vec2(-1, 0)).rgb);
    vec3 cCC = RGBToYCoCg(texture(u_Current, v_UV).rgb);
    vec3 c21 = RGBToYCoCg(texture(u_Current, v_UV + texel * vec2( 1, 0)).rgb);
    vec3 c02 = RGBToYCoCg(texture(u_Current, v_UV + texel * vec2(-1, 1)).rgb);
    vec3 c12 = RGBToYCoCg(texture(u_Current, v_UV + texel * vec2( 0, 1)).rgb);
    vec3 c22 = RGBToYCoCg(texture(u_Current, v_UV + texel * vec2( 1, 1)).rgb);

    vec3 nMin = min(min(min(c00,c10),min(c20,c01)),min(min(cCC,c21),min(c02,min(c12,c22))));
    vec3 nMax = max(max(max(c00,c10),max(c20,c01)),max(max(cCC,c21),max(c02,max(c12,c22))));

    // ── Reprojection via depth ─────────────────────────────────────────────
    float depth = texture(u_Depth, v_UV).r;

    // Reconstrói posição mundial (desfaz jitter no UV)
    vec2 uvNoJitter = v_UV - vec2(u_JitterX, u_JitterY) * 0.5 / vec2(u_ResolutionX, u_ResolutionY);
    vec4 ndc = vec4(uvNoJitter * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = u_InvViewProj * ndc;
    world /= world.w;

    // Projeta no frame anterior
    vec4 prevNDC = u_PrevViewProj * vec4(world.xyz, 1.0);
    prevNDC /= prevNDC.w;
    vec2 prevUV = prevNDC.xy * 0.5 + 0.5;

    // ── Blend current + history ─────────────────────────────────────────────
    float blend = u_BlendFactor;

    // Sem histórico válido (fora de tela ou primeiro frame) → usa só current
    bool outOfBounds = any(lessThan(prevUV, vec2(0.0))) ||
                       any(greaterThan(prevUV, vec2(1.0)));
    if (outOfBounds || depth >= 0.9999)
        blend = 1.0;

    vec3 histYCoCg = RGBToYCoCg(texture(u_History, prevUV).rgb);

    // Neighborhood clamping — clipa a história para o AABBox do frame atual
    // Previne ghosting em bordas e movimento rápido
    vec3 histClamped = clamp(histYCoCg, nMin, nMax);

    // ── Blend adaptativo — luminância + variância temporal ─────────────────
    // O problema com emissivos animados via Sine: todos os pixels da luminária
    // mudam juntos → variância ESPACIAL baixa → blend padrão não detecta.
    // Solução: pixels brilhantes são provavelmente emissivos — usam mais
    // do frame atual pra seguir a animação fielmente.
    if (blend < 1.0)
    {
        vec3 currentRGB = YCoCgToRGB(cCC);
        vec3 histRGB    = YCoCgToRGB(histClamped);

        // Luminância do frame atual (emissivos têm lum alta)
        float lumCurrent = dot(currentRGB, vec3(0.299, 0.587, 0.114));

        // Variância temporal — quanto a cor mudou desde o frame anterior
        float temporalDiff = length(currentRGB - histRGB);

        // Boost por luminância: superfícies brilhantes seguem animação mais rápido.
        // smoothstep(0.2, 1.2): começa a boostar acima de lum 0.2, máximo em 1.2
        float emissiveBoost = smoothstep(u_EmissiveLumMin, u_EmissiveLumMax, lumCurrent);
        float temporalBoost = clamp(temporalDiff * u_TemporalSensitivity, 0.0, 1.0);
        float signal = max(emissiveBoost, temporalBoost);
        blend = mix(blend, u_EmissiveBlendMax, signal);
    }

    vec3 result = mix(histClamped, cCC, blend);
    result = YCoCgToRGB(result);

    // ── Sharpening opcional (passa de realce após acumulação) ──────────────
    if (u_Sharpen != 0)
    {
        // Kernel de sharpening leve: center - avg(vizinhos_cruz)
        vec3 cross4 = (texture(u_Current, v_UV + texel*vec2( 0,-1)).rgb +
                       texture(u_Current, v_UV + texel*vec2( 0, 1)).rgb +
                       texture(u_Current, v_UV + texel*vec2(-1, 0)).rgb +
                       texture(u_Current, v_UV + texel*vec2( 1, 0)).rgb) * 0.25;
        vec3 sharp  = texture(u_Current, v_UV).rgb - cross4;
        result += sharp * u_SharpenAmount;
    }

    FragColor = vec4(max(result, vec3(0.0)), 1.0);
}
)";

    // ── Halton sequence ───────────────────────────────────────────────────────

    float OpenGLTAAPass::Halton(int index, int base)
    {
        float result = 0.f, f = 1.f;
        int i = index;
        while (i > 0) { f /= base; result += f * (i % base); i /= base; }
        return result;
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    OpenGLTAAPass::~OpenGLTAAPass()
    {
        DestroyTextures();
    }

    void OpenGLTAAPass::CreateTextures(uint32_t w, uint32_t h)
    {
        DestroyTextures();
        m_Width = w; m_Height = h;

        glGenTextures(2, m_HistoryTex);
        glGenFramebuffers(2, m_HistoryFBO);
        for (int i = 0; i < 2; ++i)
        {
            glBindTexture(GL_TEXTURE_2D, m_HistoryTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glBindFramebuffer(GL_FRAMEBUFFER, m_HistoryFBO[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, m_HistoryTex[i], 0);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLTAAPass::DestroyTextures()
    {
        if (m_HistoryTex[0]) { glDeleteTextures(2, m_HistoryTex); m_HistoryTex[0] = m_HistoryTex[1] = 0; }
        if (m_HistoryFBO[0]) { glDeleteFramebuffers(2, m_HistoryFBO); m_HistoryFBO[0] = m_HistoryFBO[1] = 0; }
    }

    void OpenGLTAAPass::Initialize(uint32_t w, uint32_t h)
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

        CreateTextures(w, h);

        // Limpa history com preto para o primeiro frame
        for (int i = 0; i < 2; ++i)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_HistoryFBO[i]);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        AXE_CORE_INFO("OpenGLTAAPass: inicializado {}x{}.", w, h);
    }

    void OpenGLTAAPass::Resize(uint32_t w, uint32_t h)
    {
        if (w == m_Width && h == m_Height) return;
        CreateTextures(w, h);
    }

    // ── BeginFrame — jitter + store prevVP ────────────────────────────────────

    void OpenGLTAAPass::BeginFrame(const glm::mat4& viewProj)
    {
        m_PrevViewProj = m_CurrentViewProj;
        m_CurrentViewProj = viewProj;

        // Halton 2/3, sequência de 16 amostras (boa cobertura do pixel)
        int idx = (m_FrameIndex % 16) + 1;
        m_Jitter = glm::vec2(Halton(idx, 2) - 0.5f, Halton(idx, 3) - 0.5f);
        ++m_FrameIndex;
    }

    // ── Execute — TAA resolve ─────────────────────────────────────────────────

    uint32_t OpenGLTAAPass::Execute(
        uint32_t hdrColorID, uint32_t depthID,
        const glm::mat4& invViewProj, const glm::mat4& prevViewProj,
        const glm::vec2& jitter,
        const TAASettings& settings,
        uint32_t width, uint32_t height)
    {
        if (!m_Shader || !m_QuadVAO) return hdrColorID;
        if (width != m_Width || height != m_Height) Resize(width, height);

        // Escreve no buffer que NÃO é o histórico atual (ping-pong)
        int writeIdx = 1 - m_CurrentHistory;
        int readIdx = m_CurrentHistory;

        glBindFramebuffer(GL_FRAMEBUFFER, m_HistoryFBO[writeIdx]);
        glViewport(0, 0, width, height);

        m_Shader->Bind();
        m_Shader->SetInt("u_Current", 0);
        m_Shader->SetInt("u_History", 1);
        m_Shader->SetInt("u_Depth", 2);
        m_Shader->SetMat4("u_InvViewProj", glm::value_ptr(invViewProj));
        m_Shader->SetMat4("u_PrevViewProj", glm::value_ptr(prevViewProj));
        m_Shader->SetFloat("u_ResolutionX", (float)width);
        m_Shader->SetFloat("u_ResolutionY", (float)height);
        m_Shader->SetFloat("u_JitterX", jitter.x);
        m_Shader->SetFloat("u_JitterY", jitter.y);
        m_Shader->SetFloat("u_BlendFactor", settings.BlendFactor);
        m_Shader->SetFloat("u_EmissiveLumMin", settings.EmissiveLumMin);
        m_Shader->SetFloat("u_EmissiveLumMax", settings.EmissiveLumMax);
        m_Shader->SetFloat("u_EmissiveBlendMax", settings.EmissiveBlendMax);
        m_Shader->SetFloat("u_TemporalSensitivity", settings.TemporalSensitivity);
        m_Shader->SetInt("u_Sharpen", settings.Sharpen ? 1 : 0);
        m_Shader->SetFloat("u_SharpenAmount", settings.SharpenAmount);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hdrColorID);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_HistoryTex[readIdx]);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, depthID);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        m_QuadVAO->Bind();
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        m_QuadVAO->Unbind();

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);

        m_Shader->Unbind();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Avança ping-pong
        m_CurrentHistory = writeIdx;

        // Retorna a textura TAA resolvida (usada pelo post-process em vez do HDR bruto)
        return m_HistoryTex[writeIdx];
    }

} // namespace axe