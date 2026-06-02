#include "outline_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/log/log.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace axe
{
    static const char* s_OutlineVert = R"(
        #version 460 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;

        uniform mat4  u_ViewProjection;
        uniform mat4  u_Model;
        uniform float u_Scale;

        void main()
        {
            vec3 expanded = a_Position + normalize(a_Normal) * (u_Scale - 1.0);
            gl_Position = u_ViewProjection * u_Model * vec4(expanded, 1.0);
        }
    )";

    static const char* s_OutlineFrag = R"(
        #version 460 core
        out vec4 FragColor;
        uniform vec4 u_Color;
        void main()
        {
            FragColor = u_Color;
        }
    )";

    OutlineRenderer::OutlineRenderer()
    {
        m_Shader = Shader::Create(s_OutlineVert, s_OutlineFrag);
    }

    void OutlineRenderer::Begin(const glm::mat4& viewProjection)
    {
        m_ViewProjection = viewProjection;
    }

    void OutlineRenderer::DrawOutline(const Mesh& mesh, const glm::mat4& model,
        const glm::vec4& color, float scale)
    {
        mesh.GetVertexArray()->Bind();

        // --- Pass 1: Escreve o depth do mesh ORIGINAL no framebuffer atual ---
        // Isso garante que o mesh expandido (Pass 2) seja bloqueado onde o
        // objeto real existe, deixando visível apenas a borda exterior.
        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(m_ViewProjection));
        m_Shader->SetMat4("u_Model", glm::value_ptr(model));
        m_Shader->SetFloat4("u_Color", { 0, 0, 0, 0 }); // cor irrelevante
        m_Shader->SetFloat("u_Scale", 1.0f);            // sem expansão

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::Less);
        RenderCommand::SetDepthWrite(true);
        // Desativa escrita de cor — só queremos atualizar o depth buffer
        RenderCommand::SetColorWrite(false);

        RenderCommand::SetCullFace(true);
        RenderCommand::SetCullMode(false); // cull back (normal)
        RenderCommand::DrawIndexedCount(mesh.GetIndexCount());

        // Reativa escrita de cor para o Pass 2
        RenderCommand::SetColorWrite(true);

        // --- Pass 2: Renderiza mesh expandido — só borda fica visível ---
        m_Shader->SetFloat4("u_Color", color);
        m_Shader->SetFloat("u_Scale", scale);

        // LessEqual: o mesh expandido aparece onde o depth é >= ao original
        // Com o depth do Pass 1 já escrito, a frente do objeto bloqueia o outline
        RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::LessEqual);
        RenderCommand::SetCullMode(true); // cull front — só back faces do mesh expandido
        RenderCommand::DrawIndexedCount(mesh.GetIndexCount());

        // Restaura estado
        RenderCommand::SetCullMode(false);
        RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::Less);
    }

    void OutlineRenderer::End()
    {
        RenderCommand::SetCullFace(false);
        RenderCommand::ResetState();
    }

} // namespace axe