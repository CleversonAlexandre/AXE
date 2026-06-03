#include "skybox_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/log/log.hpp"
#include "axe/utils/glm_config.hpp"
#include <glad/glad.h>
namespace axe
{

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
        // Espelha X para corrigir orientação após fix do winding order
        vec3 dir = vec3(-v_TexCoords.x, v_TexCoords.y, v_TexCoords.z);
        vec3 color = texture(u_Skybox, dir).rgb;
        // Sem tone mapping aqui — o PostProcess já aplica ACES/Reinhard
        FragColor = vec4(color, 1.0);
    }
)";

    // Cubo unitário — 6 faces, 4 vértices cada
    static float s_Vertices[] = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,
        -1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1, 1,
         1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1, 1,
        -1,-1,-1,  1,-1,-1,  1,-1, 1, -1,-1, 1,
        -1, 1,-1,  1, 1,-1,  1, 1, 1, -1, 1, 1
    };

    static uint32_t s_Indices[] = {
         0, 1, 2,  2, 3, 0,
         4, 5, 6,  6, 7, 4,
         8, 9,10, 10,11, 8,
        12,13,14, 14,15,12,
        16,17,18, 18,19,16,
        20,21,22, 22,23,20
    };

    void SkyboxRenderer::Initialize()
    {
        m_Shader = Shader::Create(s_SkyboxVertSrc, s_SkyboxFragSrc);

        // Cria VertexArray via abstração — sem glad
        m_VertexArray = VertexArray::Create();

        auto vb = VertexBuffer::Create(
            s_Vertices,
            static_cast<uint32_t>(sizeof(s_Vertices))
        );

        BufferLayout layout = {
            { ShaderDataType::Float3, sizeof(float) * 3 }
        };

        m_VertexArray->AddVertexBuffer(vb, layout);
        m_VertexArray->AddVertexBuffer(vb, layout);

        auto ib = IndexBuffer::Create(s_Indices, 36);
        m_VertexArray->SetIndexBuffer(ib);
    }

    void SkyboxRenderer::Render(const glm::mat4& view, const glm::mat4& projection)
    {
        if (!HasCubemap()) return;

        // Sem depth test e sem cull — skybox visto de dentro
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetCullFace(false);

        m_Shader->Bind();
        m_Shader->SetMat4("u_View", glm::value_ptr(view));
        m_Shader->SetMat4("u_Projection", glm::value_ptr(projection));
        m_Shader->SetInt("u_Skybox", 0);

        m_Cubemap->Bind(0);

        m_VertexArray->Bind();
        RenderCommand::DrawIndexedCount(36);

        RenderCommand::SetCullFace(true);
        RenderCommand::SetDepthTest(true);
    }

    void SkyboxRenderer::RenderDeferred(const glm::mat4& view, const glm::mat4& projection)
    {


        if (!HasCubemap()) return;

        // ✅ Depth test ON com LEQUAL — skybox aparece atrás da geometria
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::LessEqual);
        RenderCommand::SetCullFace(false);

        m_Shader->Bind();
        m_Shader->SetMat4("u_View", glm::value_ptr(view));
        m_Shader->SetMat4("u_Projection", glm::value_ptr(projection));
        m_Shader->SetInt("u_Skybox", 0);
        m_Cubemap->Bind(0);
        m_VertexArray->Bind();
        RenderCommand::DrawIndexedCount(36);

        RenderCommand::SetCullFace(true);
        RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::Less);
    }

} // namespace axe