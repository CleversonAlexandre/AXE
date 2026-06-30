#include "axe/graphics/renderer/particle_renderer.hpp"

#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace axe
{
    // 11 floats por vértice: center(3) + corner(2) + color(4) + size(1) + rotation(1)
    static constexpr uint32_t kFloatsPerVertex = 11;
    static constexpr uint32_t kVertsPerParticle = 4;
    static constexpr uint32_t kIndicesPerParticle = 6;

    void ParticleRenderer::EnsureInitialized()
    {
        if (m_Shader) return;

        const std::string vertexSrc = R"(
            #version 460 core
            layout(location = 0) in vec3  a_Center;
            layout(location = 1) in vec2  a_Corner;
            layout(location = 2) in vec4  a_Color;
            layout(location = 3) in float a_Size;
            layout(location = 4) in float a_Rotation;

            uniform mat4 u_ViewProjection;
            uniform vec3 u_CameraRight;
            uniform vec3 u_CameraUp;

            out vec2 v_UV;
            out vec4 v_Color;

            void main()
            {
                float c = cos(a_Rotation);
                float s = sin(a_Rotation);
                vec2  rc = vec2(a_Corner.x * c - a_Corner.y * s,
                                a_Corner.x * s + a_Corner.y * c);

                vec3 worldPos = a_Center
                    + (u_CameraRight * rc.x + u_CameraUp * rc.y) * a_Size;

                gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
                v_UV    = a_Corner + vec2(0.5);   // 0..1
                v_Color = a_Color;
            }
        )";

        const std::string fragmentSrc = R"(
            #version 460 core
            in vec2 v_UV;
            in vec4 v_Color;
            layout(location = 0) out vec4 FragColor;

            void main()
            {
                // Falloff radial suave: 1 no centro, 0 na borda do quad.
                float d     = length(v_UV - vec2(0.5));
                float alpha = smoothstep(0.5, 0.0, d);
                FragColor   = vec4(v_Color.rgb, v_Color.a * alpha);
            }
        )";

        m_Shader = Shader::Create(vertexSrc, fragmentSrc);
        m_VAO = VertexArray::Create();
    }

    void ParticleRenderer::EnsureCapacity(uint32_t particleCount)
    {
        if (particleCount <= m_Capacity && m_VBO && m_IBO) return;

        m_Capacity = particleCount;

        // VAO recriado junto (AddVertexBuffer só acrescenta; não substitui)
        m_VAO = VertexArray::Create();

        // VB dinâmico vazio (preenchido por SetData a cada frame)
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
        };
        m_VAO->AddVertexBuffer(m_VBO, layout);

        // IB estático com o padrão de quad repetido (0,1,2, 2,3,0)
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
        // Maior lote determina a capacidade dos buffers
        uint32_t maxBatch = 0;
        for (const auto& b : batches)
            maxBatch = std::max(maxBatch, (uint32_t)b.Instances.size());
        if (maxBatch == 0) return;

        EnsureInitialized();
        EnsureCapacity(maxBatch);

        // Eixos da câmera em world space (linhas da matriz de view)
        const glm::vec3 camRight{ view[0][0], view[1][0], view[2][0] };
        const glm::vec3 camUp{ view[0][1], view[1][1], view[2][1] };

        // Estado: depth-test ON (geometria occlui), depth-write OFF,
        // blend ON, sem cull (billboard é de uma face só mesmo).
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetBlend(true);
        RenderCommand::SetCullFace(false);

        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(viewProjection));
        m_Shader->SetFloat3("u_CameraRight", camRight);
        m_Shader->SetFloat3("u_CameraUp", camUp);
        m_VAO->Bind();

        static const glm::vec2 corners[4] =
        {
            { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f }
        };

        for (const auto& batch : batches)
        {
            if (batch.Instances.empty()) continue;

            if (batch.BlendMode == 1) // additive
                RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha,
                    RendererAPI::BlendFactor::One);
            else                       // alpha
                RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha,
                    RendererAPI::BlendFactor::OneMinusSrcAlpha);

            // Monta os 4 vértices de cada partícula no buffer CPU
            m_Scratch.clear();
            m_Scratch.reserve(batch.Instances.size() * kVertsPerParticle * kFloatsPerVertex);
            for (const auto& p : batch.Instances)
            {
                for (int c = 0; c < 4; ++c)
                {
                    m_Scratch.push_back(p.Position.x);
                    m_Scratch.push_back(p.Position.y);
                    m_Scratch.push_back(p.Position.z);
                    m_Scratch.push_back(corners[c].x);
                    m_Scratch.push_back(corners[c].y);
                    m_Scratch.push_back(p.Color.r);
                    m_Scratch.push_back(p.Color.g);
                    m_Scratch.push_back(p.Color.b);
                    m_Scratch.push_back(p.Color.a);
                    m_Scratch.push_back(p.Size);
                    m_Scratch.push_back(p.Rotation);
                }
            }

            m_VBO->SetData(m_Scratch.data(), (uint32_t)(m_Scratch.size() * sizeof(float)));
            RenderCommand::DrawIndexedCount((uint32_t)batch.Instances.size() * kIndicesPerParticle);
        }

        // Restaura estado padrão
        RenderCommand::SetBlend(false);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha,
            RendererAPI::BlendFactor::OneMinusSrcAlpha);
    }

} // namespace axe