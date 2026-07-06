#include "skybox_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/log/log.hpp"
#include "axe/utils/glm_config.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
namespace axe
{

    // ── Shader HDRI (original) ────────────────────────────────────────────────
    static const char* s_SkyboxVertSrc = R"(
    #version 460 core
    layout(location = 0) in vec3 a_Position;
    out vec3 v_TexCoords;
    uniform mat4 u_Projection;
    uniform mat4 u_View;
    void main()
    {
        v_TexCoords = a_Position;
        vec4 pos    = u_Projection * mat4(mat3(u_View)) * vec4(a_Position, 1.0);
        gl_Position = pos.xyww;
    }
)";

    static const char* s_SkyboxFragSrc = R"(
    #version 460 core
    out vec4 FragColor;
    in vec3 v_TexCoords;
    uniform samplerCube u_Skybox;
    void main()
    {
        vec3 dir = vec3(-v_TexCoords.x, v_TexCoords.y, v_TexCoords.z);
        vec3 color = texture(u_Skybox, dir).rgb;
        FragColor = vec4(color, 1.0);
    }
)";

    // ── Shader Procedural (Rayleigh + Mie + Nuvens) ───────────────────────────
    static const char* s_ProcSkyFragSrc = R"(
#version 460 core
out vec4 FragColor;
in  vec3 v_TexCoords;

uniform vec3  u_SunDir;
uniform float u_Turbidity;
uniform float u_CloudCoverage;
uniform float u_CloudSpeed;
uniform vec3  u_CloudColor;
uniform vec3  u_NightColor;
uniform float u_Time;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0 - 2.0*f);
    return mix(mix(hash(i), hash(i+vec2(1,0)), f.x),
               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * noise(p); p *= 2.1; a *= 0.5; }
    return v;
}

vec3 skyAtmosphere(vec3 viewDir, vec3 sunDir) {
    float cosTheta = clamp(viewDir.y, 0.001, 1.0);
    float cosGamma = clamp(dot(viewDir, sunDir), -1.0, 1.0);
    float gamma    = acos(cosGamma);
    float sunTheta = acos(clamp(sunDir.y, -1.0, 1.0));
    float t = u_Turbidity;

    // Coeficientes de Preetham simplificados
    float Ax = -0.0193*t - 0.2592, Bx = -0.0665*t + 0.0008, Cx = -0.0004*t + 0.2125;
    float Dx = -0.0641*t - 0.8989, Ex = -0.0033*t + 0.0452;
    float Ay = -0.0167*t - 0.2608, By = -0.0950*t + 0.0092, Cy = -0.0079*t + 0.2102;
    float Dy = -0.0441*t - 1.6537, Ey = -0.0109*t + 0.0529;

    float perezX = (1.0 + Ax*exp(Bx/max(cosTheta,0.001))) * (1.0 + Cx*exp(Dx*gamma) + Ex*cosGamma*cosGamma);
    float perezY = (1.0 + Ay*exp(By/max(cosTheta,0.001))) * (1.0 + Cy*exp(Dy*gamma) + Ey*cosGamma*cosGamma);

    float normX = (1.0 + Ax*exp(Bx)) * (1.0 + Cx*exp(Dx*sunTheta) + Ex);
    float normY = (1.0 + Ay*exp(By)) * (1.0 + Cy*exp(Dy*sunTheta) + Ey);

    float fx = max(normX, 0.001);
    float fy = max(normY, 0.001);

    vec3 zenith = vec3(0.25, 0.45, 0.9);
    vec3 horizon = vec3(0.65, 0.75, 0.95);
    vec3 base = mix(horizon, zenith, pow(max(viewDir.y, 0.0), 0.35));

    vec3 sky = base * clamp(vec3(perezX/fx, perezX/fx, perezY/fy) * 0.25, 0.0, 1.5);

    float elev = clamp(sunDir.y, 0.0, 1.0);
    vec3 sunsetTint = mix(vec3(1.0, 0.35, 0.05), vec3(1.0, 0.92, 0.85), smoothstep(0.0, 0.25, elev));
    sky *= sunsetTint;
    return sky;
}

void main() {
    vec3 dir = normalize(v_TexCoords);
    float sunElev  = u_SunDir.y;
    float dayFactor  = smoothstep(-0.1, 0.15, sunElev);
    float dawnFactor = smoothstep(-0.2, 0.0, sunElev) - smoothstep(0.0, 0.3, sunElev);

    // Atmosfera
    vec3 sky = u_NightColor;
    if (dir.y > -0.05) {
        vec3 vd = normalize(vec3(dir.x, max(dir.y, 0.01), dir.z));
        sky = mix(u_NightColor, skyAtmosphere(vd, u_SunDir), dayFactor);
    }

    // Tinte de amanhecer/entardecer no horizonte
    float horiz = smoothstep(0.25, 0.0, abs(dir.y));
    sky += vec3(1.0, 0.35, 0.05) * dawnFactor * horiz * 0.4;

    // Disco solar + corona
    if (sunElev > -0.15) {
        float cosA = dot(dir, u_SunDir);
        float disk  = smoothstep(0.9995, 0.9997, cosA);
        float corona = pow(max(cosA, 0.0), 128.0) * 0.6;
        float glow   = pow(max(cosA, 0.0), 6.0) * 0.12 * dayFactor;
        vec3 sc = mix(vec3(1.0, 0.55, 0.15), vec3(1.5, 1.4, 1.2), clamp(sunElev*3.0, 0.0, 1.0));
        sky += sc * (disk * 3.0 + corona + glow);
    }

    // Lua (noite)
    if (sunElev < 0.1) {
        vec3 moonDir = normalize(-u_SunDir + vec3(0.0, 0.3, 0.0));
        float cosM = dot(dir, moonDir);
        float moon = smoothstep(0.9997, 1.0, cosM) * clamp(-sunElev*2.0, 0.0, 1.0);
        sky += vec3(0.88, 0.92, 1.0) * moon * 0.8;
        // Estrelas
        float starN = hash(floor(dir.xy * 300.0 + dir.z * 80.0));
        float star = smoothstep(0.985, 1.0, starN) * clamp(-sunElev * 3.0, 0.0, 1.0) * max(dir.y, 0.0);
        sky += vec3(0.85, 0.9, 1.0) * star;
    }

    // Nuvens
    if (dir.y > 0.04 && u_CloudCoverage > 0.01) {
        vec2 cuv = dir.xz / (dir.y + 0.08) * 0.35;
        cuv += vec2(u_Time * u_CloudSpeed, u_Time * u_CloudSpeed * 0.6);
        float cloud = fbm(cuv);
        cloud = smoothstep(1.0 - u_CloudCoverage, 1.0, cloud);
        float lit = mix(0.65, 1.3, clamp(dot(dir, u_SunDir), 0.0, 1.0));
        float sunsetC = smoothstep(0.15, 0.0, sunElev) * smoothstep(-0.2, 0.0, sunElev);
        vec3 cc = mix(u_CloudColor * lit, vec3(1.0, 0.45, 0.15) * lit, sunsetC * 0.65);
        cc *= (0.5 + 0.5 * dayFactor);
        float blend = cloud * dayFactor * smoothstep(0.04, 0.18, dir.y);
        sky = mix(sky, cc, blend);
    }

    // Horizonte suave (fade para horizonte/neblina)
    float hFade = smoothstep(0.0, -0.12, dir.y);
    vec3 fogColor = vec3(0.55, 0.62, 0.7) * dayFactor + u_NightColor * (1.0 - dayFactor);
    sky = mix(sky, fogColor, hFade * 0.5);

    FragColor = vec4(max(sky, vec3(0.0)), 1.0);
}
)";

    static float s_Vertices[] = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,
        -1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1, 1,
         1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1, 1,
        -1,-1,-1,  1,-1,-1,  1,-1, 1, -1,-1, 1,
        -1, 1,-1,  1, 1,-1,  1, 1, 1, -1, 1, 1
    };
    static uint32_t s_Indices[] = {
         0, 1, 2,  2, 3, 0,   4, 5, 6,  6, 7, 4,
         8, 9,10, 10,11, 8,  12,13,14, 14,15,12,
        16,17,18, 18,19,16,  20,21,22, 22,23,20
    };

    void SkyboxRenderer::Initialize()
    {
        m_Shader = Shader::Create(s_SkyboxVertSrc, s_SkyboxFragSrc);
        m_ProcShader = Shader::Create(s_SkyboxVertSrc, s_ProcSkyFragSrc);

        m_VertexArray = VertexArray::Create();
        auto vb = VertexBuffer::Create(s_Vertices, sizeof(s_Vertices));
        BufferLayout layout = { { ShaderDataType::Float3, sizeof(float) * 3 } };
        m_VertexArray->AddVertexBuffer(vb, layout);
        m_VertexArray->AddVertexBuffer(vb, layout);
        auto ib = IndexBuffer::Create(s_Indices, 36);
        m_VertexArray->SetIndexBuffer(ib);
    }

    // ── Render interno — compartilhado por Render e RenderDeferred ────────────
    void SkyboxRenderer::DrawSky(const glm::mat4& view, const glm::mat4& proj)
    {
        if (m_UseProceduralSky)
        {
            if (!m_ProcShader) return;
            m_ProcShader->Bind();
            m_ProcShader->SetMat4("u_View", glm::value_ptr(view));
            m_ProcShader->SetMat4("u_Projection", glm::value_ptr(proj));
            m_ProcShader->SetFloat3("u_SunDir", m_SunDirection);
            m_ProcShader->SetFloat("u_Turbidity", m_Turbidity);
            m_ProcShader->SetFloat("u_CloudCoverage", m_CloudCoverage);
            m_ProcShader->SetFloat("u_CloudSpeed", m_CloudSpeed);
            m_ProcShader->SetFloat3("u_CloudColor", m_CloudColor);
            m_ProcShader->SetFloat3("u_NightColor", m_NightColor);
            m_ProcShader->SetFloat("u_Time", m_Time);
            m_ProcShader->Bind();
        }
        else
        {
            if (!HasCubemap()) return;
            m_Shader->Bind();
            m_Shader->SetMat4("u_View", glm::value_ptr(view));
            m_Shader->SetMat4("u_Projection", glm::value_ptr(proj));
            m_Shader->SetInt("u_Skybox", 0);
            m_Cubemap->Bind(0);
        }

        m_VertexArray->Bind();
        RenderCommand::DrawIndexedCount(36);
    }

    void SkyboxRenderer::Render(const glm::mat4& view, const glm::mat4& projection)
    {
        if (!m_UseProceduralSky && !HasCubemap()) return;
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetCullFace(false);
        DrawSky(view, projection);
        RenderCommand::SetCullFace(true);
        RenderCommand::SetDepthTest(true);
    }

    void SkyboxRenderer::RenderDeferred(const glm::mat4& view, const glm::mat4& projection)
    {
        if (!m_UseProceduralSky && !HasCubemap()) return;
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::LessEqual);
        RenderCommand::SetCullFace(false);
        DrawSky(view, projection);
        RenderCommand::SetCullFace(true);
        RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::Less);
    }

} // namespace axe