#include "light_material_evaluator.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/graphics/render_command.hpp"

namespace axe
{
    void LightMaterialEvaluator::Initialize()
    {
        if (m_Initialized) return;

        // Quad simples cobrindo a NDC inteira (-1..1) — o shader gerado por
        // CompileLightFunction usa a_Position.xy direto como clip-space,
        // sem nenhuma transformação de câmera/mundo (ver comentário no
        // header e em MaterialCompiler::CompileLightFunction).
        const float vertices[] = {
            -1.0f, -1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f,
        };
        const uint32_t indices[] = { 0, 1, 2, 2, 3, 0 };

        auto vb = VertexBuffer::Create(vertices, sizeof(vertices));
        auto ib = IndexBuffer::Create(indices, 6);

        BufferLayout layout = {
            { ShaderDataType::Float3, sizeof(float) * 3, false }, // position
        };

        m_VertexArray = VertexArray::Create();
        m_VertexArray->AddVertexBuffer(vb, layout);
        m_VertexArray->SetIndexBuffer(ib);

        // Framebuffer de 1x1 — só precisamos de UM resultado (a cor da
        // luz neste frame), não uma imagem.
        FramebufferSpecification spec;
        spec.Width = 1;
        spec.Height = 1;
        spec.Attachments = { FramebufferTextureFormat::RGBA8 };
        m_Framebuffer = Framebuffer::Create(spec);

        m_Initialized = true;
    }

    glm::vec3 LightMaterialEvaluator::Evaluate(const std::shared_ptr<Shader>& shader,
        const std::map<std::string, std::shared_ptr<Texture2D>>& samplers,
        float time, const glm::vec3& cameraPosition)
    {
        if (!m_Initialized || !shader || !m_Framebuffer || !m_VertexArray)
            return glm::vec3(1.0f);

        // Salva o estado real da GPU ANTES de usar o framebuffer de 1x1 —
        // sem isso, o viewport e o framebuffer ativo ficam "vazando" pro
        // resto do frame depois que esta função termina. Foi exatamente
        // esse bug que fazia a cena toda ficar escura no modo Play: o
        // viewport ficava travado em 1x1 pixel pro resto do frame inteiro,
        // já que nada mais re-setava ele de volta no caminho do Play.
        // Agora a consulta passa pela abstração (sem glGetIntegerv cru).
        uint32_t previousFBO = RenderCommand::GetBoundFramebuffer();
        RendererAPI::Viewport previousViewport = RenderCommand::GetViewport();

        m_Framebuffer->Bind();
        RenderCommand::SetViewport(0, 0, 1, 1);
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetBlend(false);

        shader->Bind();
        shader->SetFloat("u_Time", time);
        shader->SetFloat3("u_CameraPosition", cameraPosition);

        int unit = 0;
        for (auto& [name, tex] : samplers)
        {
            if (!tex) continue;
            tex->Bind(unit);
            shader->SetInt(name, unit);
            unit++;
        }

        m_VertexArray->Bind();
        RenderCommand::DrawIndexed(m_VertexArray);
        m_VertexArray->Unbind();
        shader->Unbind();

        // ReadPixel já devolve RGBA empacotado num uint32 (R no byte mais
        // baixo) — mesmo método usado pelo picking de entidades, só que
        // aqui interpretamos os bytes como cor em vez de ID.
        uint32_t packed = m_Framebuffer->ReadPixel(0, 0);
        glm::vec3 color(
            (packed & 0xFF) / 255.0f,
            ((packed >> 8) & 0xFF) / 255.0f,
            ((packed >> 16) & 0xFF) / 255.0f);

        // Restaura tudo exatamente como estava antes — framebuffer ativo
        // E viewport. Framebuffer::Unbind() sempre volta pro framebuffer 0
        // (a tela), não pro que estava ativo antes (ex: o HDR framebuffer
        // da cena) — por isso restauramos o FBO salvo direto pela abstração,
        // em vez de confiar em Unbind().
        RenderCommand::BindFramebuffer(previousFBO);
        RenderCommand::SetViewport(previousViewport.x, previousViewport.y,
            previousViewport.width, previousViewport.height);
        RenderCommand::SetDepthTest(true);

        return color;
    }

    glm::vec3 LightMaterialEvaluator::EvaluateAverage(
        const std::shared_ptr<Shader>& shader,
        const std::map<std::string, std::shared_ptr<Texture2D>>& samplers)
    {
        if (!m_Initialized || !shader || !m_VertexArray)
            return glm::vec3(0.0f);

        constexpr uint32_t kRes = 8;

        if (!m_AvgFramebuffer)
        {
            FramebufferSpecification spec;
            spec.Width = kRes;
            spec.Height = kRes;
            spec.Attachments = { FramebufferTextureFormat::RGBA8 };
            m_AvgFramebuffer = Framebuffer::Create(spec);
        }
        if (!m_AvgFramebuffer) return glm::vec3(0.0f);

        // Mesmo protocolo de save/restore do Evaluate() — quem clobbera
        // estado, restaura estado
        uint32_t previousFBO = RenderCommand::GetBoundFramebuffer();
        RendererAPI::Viewport previousViewport = RenderCommand::GetViewport();

        m_AvgFramebuffer->Bind();
        RenderCommand::SetViewport(0, 0, kRes, kRes);
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetBlend(false);

        shader->Bind();
        shader->SetFloat("u_Time", 0.0f); // média é atemporal
        shader->SetFloat3("u_CameraPosition", glm::vec3(0.0f));

        int unit = 0;
        for (auto& [name, tex] : samplers)
        {
            if (!tex) continue;
            tex->Bind(unit);
            shader->SetInt(name, unit);
            unit++;
        }

        m_VertexArray->Bind();
        RenderCommand::DrawIndexed(m_VertexArray);
        m_VertexArray->Unbind();
        shader->Unbind();

        // Média dos 64 texels — o shader gravou emissive/8, desfazemos
        // a escala aqui (readback LDR virando range 0..8)
        glm::vec3 sum(0.0f);
        for (uint32_t y = 0; y < kRes; y++)
            for (uint32_t x = 0; x < kRes; x++)
            {
                uint32_t packed = m_AvgFramebuffer->ReadPixel(x, y);
                sum += glm::vec3(
                    (packed & 0xFF) / 255.0f,
                    ((packed >> 8) & 0xFF) / 255.0f,
                    ((packed >> 16) & 0xFF) / 255.0f);
            }

        RenderCommand::BindFramebuffer(previousFBO);
        RenderCommand::SetViewport(previousViewport.x, previousViewport.y,
            previousViewport.width, previousViewport.height);
        RenderCommand::SetDepthTest(true);

        return (sum / float(kRes * kRes)) * 8.0f;
    }

} // namespace axe