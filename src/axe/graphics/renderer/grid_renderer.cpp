#include "grid_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/log/log.hpp"
#include "axe/utils/glm_config.hpp"

namespace axe
{

    static const char* s_GridVert = R"(
    #version 460 core
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec4 a_Color;
    uniform mat4 u_ViewProjection;
    out vec4 v_Color;
    void main()
    {
        v_Color     = a_Color;
        gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
    }
)";

    static const char* s_GridFrag = R"(
    #version 460 core
    in  vec4 v_Color;
    out vec4 FragColor;
    void main()
    {
        FragColor = v_Color;
    }
)";

    void GridRenderer::Initialize(int halfSize)
    {
        // Recria sempre — garante que mudanças nas cores sejam aplicadas
        m_Initialized = false;
        m_VertexArray.reset();
        m_Shader.reset();

        m_Shader = Shader::Create(s_GridVert, s_GridFrag);

        // Stride: vec3 pos (12 bytes) + vec4 color (16 bytes) = 28 bytes per vertex
        std::vector<float> verts;
        verts.reserve(halfSize * 4 * 2 * 7);

        auto pushLine = [&](float x1, float z1, float x2, float z2, bool main)
            {
                // Valores em espaço linear — após ACES tone mapping ficam ~cinza médio
                // 0.04 linear ≈ cinza escuro após ACES; 0.08 ≈ cinza médio
                float g = main ? 0.08f : 0.04f;
                float a = main ? 0.90f : 0.60f;
                verts.insert(verts.end(), { x1, 0.f, z1, g, g, g, a });
                verts.insert(verts.end(), { x2, 0.f, z2, g, g, g, a });
            };

        for (int i = -halfSize; i <= halfSize; i++)
        {
            bool isMain = (i % 10 == 0);
            float fi = (float)i;
            float fh = (float)halfSize;
            pushLine(-fh, fi, fh, fi, isMain);
            pushLine(fi, -fh, fi, fh, isMain);
        }

        m_VertexCount = (uint32_t)(verts.size() / 7);

        auto vb = VertexBuffer::Create(
            verts.data(),
            (uint32_t)(verts.size() * sizeof(float))
        );

        // BufferElement(ShaderDataType, size_in_bytes)
        BufferLayout layout = {
            { ShaderDataType::Float3, sizeof(float) * 3 }, // a_Position
            { ShaderDataType::Float4, sizeof(float) * 4 }  // a_Color
        };

        m_VertexArray = VertexArray::Create();
        m_VertexArray->AddVertexBuffer(vb, layout);

        m_Initialized = true;
        //AXE_CORE_INFO("GridRenderer: inicializado ({} linhas).", m_VertexCount / 2);
    }

    void GridRenderer::Render(const glm::mat4& view, const glm::mat4& projection)
    {
        if (!m_Initialized || !m_Shader || !m_VertexArray) return;

        glm::mat4 vp = projection * view;

        RenderCommand::SetBlend(true);
        RenderCommand::SetBlendFunc(0x0302, 0x0303); // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(false); // lê depth mas não escreve — não sobrepõe objetos

        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(vp));

        m_VertexArray->Bind();
        RenderCommand::DrawLines(m_VertexArray, m_VertexCount);
        m_VertexArray->Unbind();

        m_Shader->Unbind();

        // Restaura estado
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetBlend(false);
    }

} // namespace axe