#include "axe/log/log.hpp"
#include "triangle_renderer.hpp"

#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/texture.hpp"



namespace axe
{
    TriangleRenderer::TriangleRenderer()
    {
        // Vertex format: position (3 floats) + color (3 floats)
        const float vertices[] =
        {
            // position          // color
             0.0f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,  // top - red
            -0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,  // bottom-left - green
             0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f   // bottom-right - blue
        };

        const std::uint32_t indices[] = { 0, 1, 2 };

        const std::string vertexSrc = R"(
            #version 460 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Color;

            out vec3 v_Color;

            void main()
            {
                v_Color = a_Color;
                gl_Position = vec4(a_Position, 1.0);
            }
        )";

        const std::string fragmentSrc = R"(
            #version 460 core
            in vec3 v_Color;
            layout(location = 0) out vec4 color;

            void main()
            {
                color = vec4(v_Color, 1.0);
            }
        )";

        m_Shader = Shader::Create(vertexSrc, fragmentSrc);

        auto vb = VertexBuffer::Create(vertices, sizeof(vertices));
        auto ib = IndexBuffer::Create(indices, 3);

        // Create buffer layout
        BufferLayout layout = {
            { ShaderDataType::Float3, sizeof(float) * 3, false },  // position
            { ShaderDataType::Float3, sizeof(float) * 3, false }   // color
        };

        m_VertexArray = VertexArray::Create();
        m_VertexArray->AddVertexBuffer(vb, layout);
        m_VertexArray->SetIndexBuffer(ib);


        PipelineSpecification spec;
        spec.Shader = m_Shader;
        spec.DepthTest = false; // triângulo 2D
        spec.Blend = false;

        m_Pipeline = Pipeline::Create(spec);


        m_Texture = Texture2D::Create(1, 1);

        AXE_CORE_INFO("TriangleRenderer created");
    }

    void TriangleRenderer::Render()
    {

        m_Pipeline->Bind();
        m_VertexArray->Bind();
        m_Texture->Bind(0);

        RenderCommand::DrawIndexed(m_VertexArray);

    }
}