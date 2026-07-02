#include "axe/graphics/renderer/particle_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/texture.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace axe
{
    // Layout: center(3) + corner(2) + color(4) + size(1) + rotation(1) + age01(1) + velocity(3) = 15
    static constexpr uint32_t kFloatsPerVertex = 15;
    static constexpr uint32_t kVertsPerParticle = 4;
    static constexpr uint32_t kIndicesPerParticle = 6;

    void ParticleRenderer::EnsureInitialized()
    {
        if (m_DefaultShader) return;

        // Vertex shader com suporte a velocity-stretch.
        // Quando u_StretchAmount > 0 e a partícula tem velocidade, o billboard
        // é esticado na direção do movimento em vez de usar rotação normal.
        // u_StretchAmount = 0 → comportamento idêntico ao billboard padrão.
        const std::string vertexSrc = R"(
#version 460 core
layout(location = 0) in vec3  a_Center;
layout(location = 1) in vec2  a_Corner;
layout(location = 2) in vec4  a_Color;
layout(location = 3) in float a_Size;
layout(location = 4) in float a_Rotation;
layout(location = 5) in float a_Age01;
layout(location = 6) in vec3  a_Velocity;

uniform mat4 u_ViewProjection;
uniform vec3 u_CameraRight;
uniform vec3 u_CameraUp;
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
        // Velocity-aligned stretch:
        // X (corner.x) → camera right   (largura do billboard)
        // Y (corner.y) → dir. velocidade (comprimento esticado)
        vec3 stretchDir = normalize(a_Velocity);
        float stretchFactor = 1.0 + u_StretchAmount * speed;

        worldPos = a_Center
            + u_CameraRight * a_Corner.x * a_Size
            + stretchDir    * a_Corner.y * a_Size * stretchFactor;
    }
    else
    {
        // Billboard normal com rotação
        float c = cos(a_Rotation);
        float s = sin(a_Rotation);
        vec2 rc = vec2(a_Corner.x * c - a_Corner.y * s,
                       a_Corner.x * s + a_Corner.y * c);
        worldPos = a_Center
            + (u_CameraRight * rc.x + u_CameraUp * rc.y) * a_Size;
    }

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
    v_UV    = a_Corner + vec2(0.5);
    v_Color = a_Color;
    v_Age01 = a_Age01;
}
)";

        const std::string fragmentSrc = R"(
#version 460 core
in vec2  v_UV;
in vec4  v_Color;
in float v_Age01;
layout(location = 0) out vec4 FragColor;

void main()
{
    float d     = length(v_UV - vec2(0.5));
    float alpha = smoothstep(0.5, 0.0, d);
    FragColor   = vec4(v_Color.rgb, v_Color.a * alpha);
}
)";

        m_DefaultShader = Shader::Create(vertexSrc, fragmentSrc);
        m_VAO = VertexArray::Create();
    }

    void ParticleRenderer::EnsureCapacity(uint32_t particleCount)
    {
        if (particleCount <= m_Capacity && m_VBO && m_IBO) return;

        m_Capacity = particleCount;
        m_VAO = VertexArray::Create();

        const uint32_t vboBytes =
            m_Capacity * kVertsPerParticle * kFloatsPerVertex * sizeof(float);
        m_VBO = VertexBuffer::Create(nullptr, vboBytes);

        BufferLayout layout =
        {
            { ShaderDataType::Float3, sizeof(float) * 3, false }, // a_Center
            { ShaderDataType::Float2, sizeof(float) * 2, false }, // a_Corner
            { ShaderDataType::Float4, sizeof(float) * 4, false }, // a_Color
            { ShaderDataType::Float,  sizeof(float) * 1, false }, // a_Size
            { ShaderDataType::Float,  sizeof(float) * 1, false }, // a_Rotation
            { ShaderDataType::Float,  sizeof(float) * 1, false }, // a_Age01
            { ShaderDataType::Float3, sizeof(float) * 3, false }, // a_Velocity
        };
        m_VAO->AddVertexBuffer(m_VBO, layout);

        std::vector<uint32_t> indices(m_Capacity * kIndicesPerParticle);
        for (uint32_t i = 0; i < m_Capacity; ++i)
        {
            const uint32_t base = i * kVertsPerParticle;
            const uint32_t o = i * kIndicesPerParticle;
            indices[o + 0] = base + 0;
            indices[o + 1] = base + 1;
            indices[o + 2] = base + 2;
            indices[o + 3] = base + 2;
            indices[o + 4] = base + 3;
            indices[o + 5] = base + 0;
        }
        m_IBO = IndexBuffer::Create(indices.data(), (uint32_t)indices.size());
        m_VAO->SetIndexBuffer(m_IBO);
    }

    void ParticleRenderer::Render(const std::vector<ParticleBatch>& batches,
        const glm::mat4& viewProjection,
        const glm::mat4& view)
    {
        uint32_t maxBatch = 0;
        for (const auto& b : batches)
            maxBatch = std::max(maxBatch, (uint32_t)b.Instances.size());
        if (maxBatch == 0) return;

        EnsureInitialized();
        EnsureCapacity(maxBatch);

        const glm::vec3 camRight{ view[0][0], view[1][0], view[2][0] };
        const glm::vec3 camUp{ view[0][1], view[1][1], view[2][1] };

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetBlend(true);
        RenderCommand::SetCullFace(false);

        m_VAO->Bind();

        static const glm::vec2 corners[4] =
        {
            { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f }
        };

        for (const auto& batch : batches)
        {
            if (batch.Instances.empty()) continue;

            Shader* shader = batch.OverrideShader
                ? batch.OverrideShader.get()
                : m_DefaultShader.get();

            if (batch.BlendMode == 1)
                RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha,
                    RendererAPI::BlendFactor::One);
            else
                RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha,
                    RendererAPI::BlendFactor::OneMinusSrcAlpha);

            shader->Bind();
            shader->SetMat4("u_ViewProjection", glm::value_ptr(viewProjection));
            shader->SetFloat3("u_CameraRight", camRight);
            shader->SetFloat3("u_CameraUp", camUp);
            shader->SetFloat("u_Time", m_Time);
            shader->SetFloat("u_StretchAmount", batch.StretchAmount);
            // Flipbook — só o override shader usa; o default ignora uniforms desconhecidos
            shader->SetInt("u_FlipbookCols", batch.FlipbookEnabled ? batch.FlipbookCols : 1);
            shader->SetInt("u_FlipbookRows", batch.FlipbookEnabled ? batch.FlipbookRows : 1);
            shader->SetFloat("u_FlipbookCycles", batch.FlipbookEnabled ? batch.FlipbookCycles : 1.0f);

            int samplerSlot = 0;
            for (auto& [name, tex] : batch.OverrideSamplers)
            {
                if (!tex) continue;
                tex->Bind(samplerSlot);
                shader->SetInt(name, samplerSlot);
                ++samplerSlot;
            }

            m_Scratch.clear();
            m_Scratch.reserve(batch.Instances.size() * kVertsPerParticle * kFloatsPerVertex);
            for (const auto& p : batch.Instances)
            {
                for (int c = 0; c < 4; ++c)
                {
                    // center (3)
                    m_Scratch.push_back(p.Position.x);
                    m_Scratch.push_back(p.Position.y);
                    m_Scratch.push_back(p.Position.z);
                    // corner (2)
                    m_Scratch.push_back(corners[c].x);
                    m_Scratch.push_back(corners[c].y);
                    // color (4)
                    m_Scratch.push_back(p.Color.r);
                    m_Scratch.push_back(p.Color.g);
                    m_Scratch.push_back(p.Color.b);
                    m_Scratch.push_back(p.Color.a);
                    // size (1)
                    m_Scratch.push_back(p.Size);
                    // rotation (1)
                    m_Scratch.push_back(p.Rotation);
                    // age01 (1)
                    m_Scratch.push_back(p.Age01);
                    // velocity (3)
                    m_Scratch.push_back(p.Velocity.x);
                    m_Scratch.push_back(p.Velocity.y);
                    m_Scratch.push_back(p.Velocity.z);
                }
            }

            m_VBO->SetData(m_Scratch.data(), (uint32_t)(m_Scratch.size() * sizeof(float)));
            RenderCommand::DrawIndexedCount((uint32_t)batch.Instances.size() * kIndicesPerParticle);

            shader->Unbind();
        }

        RenderCommand::SetBlend(false);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha,
            RendererAPI::BlendFactor::OneMinusSrcAlpha);
    }

} // namespace axe