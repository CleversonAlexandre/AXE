#include "ribbon_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/texture.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace axe
{
    // Vertex layout: position(3) + uv(2) + color(4) = 9 floats/vertex
    // 2 verts por ponto (esquerda e direita da fita)
    // Desenhado como GL_TRIANGLE_STRIP
    static constexpr uint32_t kFloatsPerVert = 9;

    static const char* kRibbonVS = R"(
#version 460 core
layout(location = 0) in vec3  a_Position;
layout(location = 1) in vec2  a_UV;
layout(location = 2) in vec4  a_Color;

uniform mat4 u_ViewProjection;

out vec2  v_UV;
out vec4  v_Color;

void main()
{
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
    v_UV    = a_UV;
    v_Color = a_Color;
}
)";

    static const char* kRibbonFS = R"(
#version 460 core
in vec2  v_UV;
in vec4  v_Color;

uniform sampler2D u_Tex;
uniform int       u_HasTex;

layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 col = v_Color;
    if (u_HasTex != 0)
        col *= texture(u_Tex, v_UV);
    // Fade nas bordas laterais (suaviza a fita)
    float edgeFade = smoothstep(0.0, 0.1, v_UV.y) * smoothstep(1.0, 0.9, v_UV.y);
    col.a *= edgeFade;
    if (col.a < 0.001) discard;
    FragColor = col;
}
)";

    void RibbonRenderer::EnsureInitialized()
    {
        if (m_DefaultShader) return;
        m_DefaultShader = Shader::Create(kRibbonVS, kRibbonFS);
        m_VAO = VertexArray::Create();
    }

    void RibbonRenderer::EnsureCapacity(uint32_t vertexCount)
    {
        if (vertexCount <= m_Capacity && m_VBO) return;
        m_Capacity = vertexCount + 64; // margem
        m_VAO = VertexArray::Create();
        m_VBO = VertexBuffer::Create(nullptr,
            m_Capacity * kFloatsPerVert * sizeof(float));

        BufferLayout layout = {
            { ShaderDataType::Float3, sizeof(float) * 3, false }, // position
            { ShaderDataType::Float2, sizeof(float) * 2, false }, // uv
            { ShaderDataType::Float4, sizeof(float) * 4, false }, // color
        };
        m_VAO->AddVertexBuffer(m_VBO, layout);
    }

    void RibbonRenderer::Render(
        const std::vector<RibbonBatch>& batches,
        const glm::mat4& viewProjection,
        const glm::vec3& cameraPosition)
    {
        if (batches.empty()) return;
        EnsureInitialized();

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetBlend(true);
        RenderCommand::SetCullFace(false);

        for (const auto& batch : batches)
        {
            const auto& pts = batch.Points;
            if (pts.size() < 2) continue;

            const int N = (int)pts.size();

            // Acumula comprimento total pra UV normalizado
            std::vector<float> cumLen(N, 0.f);
            for (int i = 1; i < N; ++i)
                cumLen[i] = cumLen[i - 1] + glm::length(pts[i].Position - pts[i - 1].Position);
            float totalLen = cumLen[N - 1];
            if (totalLen < 1e-5f) continue;

            // Constrói triangle strip: 2 verts por ponto (left, right)
            // Total = N * 2 verts
            EnsureCapacity(N * 2);
            m_Scratch.clear();
            m_Scratch.reserve(N * 2 * kFloatsPerVert);

            auto pushVert = [&](const glm::vec3& pos, float u, float v,
                const glm::vec4& col)
                {
                    m_Scratch.push_back(pos.x); m_Scratch.push_back(pos.y); m_Scratch.push_back(pos.z);
                    m_Scratch.push_back(u);     m_Scratch.push_back(v);
                    m_Scratch.push_back(col.r); m_Scratch.push_back(col.g);
                    m_Scratch.push_back(col.b); m_Scratch.push_back(col.a);
                };

            for (int i = 0; i < N; ++i)
            {
                const auto& pt = pts[i];

                // Direção do segmento: usa o vizinho pra calcular
                glm::vec3 segDir(0.f, 1.f, 0.f);
                if (i < N - 1)
                    segDir = pts[i + 1].Position - pt.Position;
                else if (i > 0)
                    segDir = pt.Position - pts[i - 1].Position;
                if (glm::length(segDir) > 1e-5f)
                    segDir = glm::normalize(segDir);

                // Right = perpendicular ao segmento E voltado pra câmera
                glm::vec3 toCamera = glm::normalize(cameraPosition - pt.Position);
                glm::vec3 right = glm::cross(segDir, toCamera);
                if (glm::length(right) < 1e-5f) // degenerado
                    right = glm::cross(segDir, glm::vec3(0.f, 1.f, 0.f));
                right = glm::normalize(right);

                float u = cumLen[i] / totalLen;
                float hw = pt.Width * 0.5f; // half-width

                pushVert(pt.Position - right * hw, u, 0.f, pt.Color); // esquerda
                pushVert(pt.Position + right * hw, u, 1.f, pt.Color); // direita
            }

            m_VBO->SetData(m_Scratch.data(),
                (uint32_t)(m_Scratch.size() * sizeof(float)));

            Shader* shader = batch.OverrideShader
                ? batch.OverrideShader.get()
                : m_DefaultShader.get();

            if (batch.BlendMode == 1)
                RenderCommand::SetBlendFunc(
                    RendererAPI::BlendFactor::SrcAlpha,
                    RendererAPI::BlendFactor::One);
            else
                RenderCommand::SetBlendFunc(
                    RendererAPI::BlendFactor::SrcAlpha,
                    RendererAPI::BlendFactor::OneMinusSrcAlpha);

            shader->Bind();
            shader->SetMat4("u_ViewProjection", glm::value_ptr(viewProjection));
            shader->SetFloat("u_Time", m_Time);

            int samplerSlot = 0;
            shader->SetInt("u_HasTex", batch.OverrideSamplers.empty() ? 0 : 1);
            for (auto& [name, tex] : batch.OverrideSamplers)
            {
                if (!tex) continue;
                tex->Bind(samplerSlot);
                shader->SetInt(name.c_str(), samplerSlot++);
            }

            m_VAO->Bind();
            // Triangle strip: N*2 verts → (N*2 - 2) tris
            RenderCommand::DrawArraysStrip(N * 2);
            m_VAO->Unbind();
            shader->Unbind();
        }

        RenderCommand::SetBlend(false);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetBlendFunc(
            RendererAPI::BlendFactor::SrcAlpha,
            RendererAPI::BlendFactor::OneMinusSrcAlpha);
    }

} // namespace axe