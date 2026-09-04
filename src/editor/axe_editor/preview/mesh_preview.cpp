#include "mesh_preview.hpp"

#include "axe_editor/viewport_renderer.hpp"
#include "axe_editor/ui/view_gizmo.hpp"

#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/log/log.hpp"

#include <algorithm>
#include <cmath>

namespace axe::ui
{
    MeshPreview::MeshPreview() = default;

    // Fora de linha de proposito: o header so tem declaracao adiantada de
    // Scene, ViewportRenderer, Framebuffer e SceneEnvironment, e o unique_ptr
    // precisa do tipo COMPLETO para destruir. Com o destrutor implicito no
    // header, todo arquivo que incluisse este preview teria de incluir os
    // quatro — que e justamente o acoplamento que a declaracao adiantada
    // existe para evitar.
    MeshPreview::~MeshPreview() = default;

    void MeshPreview::Initialize()
    {
        if (m_Ready) return;

        FramebufferSpecification spec;
        spec.Width = 512;
        spec.Height = 512;
        spec.Attachments = {
            FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::DEPTH32F,
        };
        // Este alvo e APRESENTADO como imagem no ImGui — sem filtro linear,
        // qualquer tamanho que nao seja o nativo sai serrilhado.
        spec.LinearFilter = true;
        m_Framebuffer = Framebuffer::Create(spec);

        m_Renderer = std::make_unique<ViewportRenderer>();
        m_Renderer->Initialize();
        m_Renderer->SetPickingEnabled(false);
        m_Renderer->SetPreviewMode(true);

        // Forward, e nao deferred. Um G-Buffer inteiro para desenhar UMA malha
        // num painel de 512 px custa mais memoria de video que o preview todo
        // vale — e e o mesmo motivo pelo qual os outros quatro previews da
        // engine fazem exatamente isto.
        if (auto* sr = m_Renderer->GetSceneRenderer())
        {
            sr->SetDeferredEnabled(false);
            sr->SetDeferredSupported(false);
        }

        m_Scene = std::make_unique<Scene>();
        auto& registry = m_Scene->GetRegistry();

        m_Entity = m_Scene->CreateEntity("PreviewMesh");

        // Luz propria. O preview NAO herda a iluminacao da cena aberta: o
        // objetivo aqui e ver o asset, e nao ver como ele fica sob a luz de
        // uma cena que pode estar de madrugada — depois de todo o trabalho de
        // fazer "sem sol, sem luz" valer de verdade, um preview que herdasse a
        // cena mostraria o asset no breu, corretamente e inutilmente.
        auto lightEntity = m_Scene->CreateEntity("PreviewLight");
        auto& lc = registry.emplace<LightComponent>(lightEntity);
        lc.Data = std::make_shared<DirectionalLight>();
        lc.Data->Direction = glm::vec3(-0.4f, -1.0f, -0.6f);
        lc.Data->Color = glm::vec3(1.0f);
        lc.Data->Intensity = 3.0f;
        lc.Data->AmbientStrength = 0.3f;

        // Material padrao cinza. Malha de FBX chega aqui sem material util com
        // frequencia, e um preview preto nao responde nenhuma pergunta.
        m_DefaultMaterial = std::make_shared<Material>(nullptr, "AssetPreview");

        m_Renderer->SetScene(m_Scene.get());

        m_Environment = std::make_unique<SceneEnvironment>();
        m_Environment->LoadHDRI("resources/quarry_04_puresky_2k.hdr");
        m_Renderer->SetEnvironment(m_Environment.get());
        if (auto* sr = m_Renderer->GetSceneRenderer())
            sr->SetEnvironment(m_Environment.get());

        m_Ready = true;
    }

    void MeshPreview::SetMesh(const std::shared_ptr<Mesh>& mesh,
        const std::shared_ptr<Material>& material)
    {
        Initialize();
        if (!m_Scene || m_Entity == entt::null) return;

        auto& registry = m_Scene->GetRegistry();

        if (!mesh)
        {
            registry.remove<MeshComponent>(m_Entity);
            return;
        }

        auto& mc = registry.get_or_emplace<MeshComponent>(m_Entity);
        mc.Data = mesh;
        mc.AssetUUID.clear();

        auto& matc = registry.get_or_emplace<MaterialComponent>(m_Entity);
        matc.Data = material ? material : m_DefaultMaterial;
        matc.MaterialAssetUUID.clear();

        // Transform identidade. A malha e mostrada COMO ELA E — se a escala
        // do asset estiver errada, o preview tem de mostrar errado, senao ele
        // esconde justamente o defeito que o painel existe para revelar.
        auto& tc = registry.get_or_emplace<TransformComponent>(m_Entity);
        tc.Data.Position = glm::vec3(0.0f);
        tc.Data.Rotation = glm::vec3(0.0f);
        tc.Data.Scale = glm::vec3(1.0f);
    }

    void MeshPreview::SetCollider(const ColliderComponent& collider)
    {
        Initialize();
        if (!m_Scene || m_Entity == entt::null) return;

        auto& c = m_Scene->GetRegistry().get_or_emplace<ColliderComponent>(m_Entity);
        c = collider;
        c.ShowDebug = true;

        // Quem liga a flag no renderer e o Render, a cada frame, a partir de
        // ShowColliderWire. Force-la aqui faria o collider reaparecer sozinho
        // toda vez que as settings mudassem, por cima da escolha do usuario.
    }

    void MeshPreview::ClearCollider()
    {
        if (!m_Scene || m_Entity == entt::null) return;
        m_Scene->GetRegistry().remove<ColliderComponent>(m_Entity);
    }

    void MeshPreview::Clear()
    {
        if (!m_Scene || m_Entity == entt::null) return;
        m_Scene->GetRegistry().remove<MeshComponent>(m_Entity);
    }

    void MeshPreview::FrameBounds(const glm::vec3& boundsMin, const glm::vec3& boundsMax)
    {
        if (!m_Renderer || !m_Renderer->m_Camera) return;

        const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
        const glm::vec3 size = boundsMax - boundsMin;

        // Raio da ESFERA que contem a caixa, e nao a maior aresta: enquadrar
        // pela aresta deixa os cantos de um objeto alongado fora de quadro
        // quando a camera olha na diagonal — que e a vista padrao aqui.
        const float radius = std::max(glm::length(size) * 0.5f, 0.001f);

        // 2.5 raios e a distancia que deixa o objeto ocupando a maior parte do
        // quadro com folga para a grade aparecer em volta. Fixo, e nao
        // derivado do FOV, porque o FOV deste preview nao muda.
        const float distance = radius * 2.5f;

        // Tres quartos de frente, ligeiramente de cima: a vista que revela
        // profundidade e a base do objeto ao mesmo tempo. De frente reta
        // esconderia se o pivo esta enterrado no chao — que e uma das coisas
        // que se vem conferir aqui.
        m_Renderer->m_Camera->SetOrbit(center, distance,
            glm::radians(35.0f), glm::radians(20.0f));
    }

    void MeshPreview::Render()
    {
        if (!m_Ready || !m_Renderer || !m_Framebuffer || !m_Scene) return;

        m_Renderer->ShowGrid = ShowGrid;

        // O collider CONTINUA na cena quando o wireframe esta desligado: o que
        // se apaga e o desenho, nao a configuracao. Remover o componente aqui
        // faria o botao de visibilidade virar um botao de apagar.
        m_Renderer->ShowColliders = ShowColliderWire;

        // Segunda linha de defesa contra deferred, igual a dos outros
        // previews: se algo externo mexer no SceneRenderer entre frames, o
        // sintoma seria um preview preto sem erro nenhum.
        if (auto* sr = m_Renderer->GetSceneRenderer())
        {
            sr->SetDeferredEnabled(false);
            sr->SetDeferredSupported(false);
            sr->SetEnvironment(m_Environment.get());
        }
        m_Renderer->SetEnvironment(m_Environment.get());

        if (m_PendingSize.x > 0.0f && m_PendingSize.y > 0.0f)
        {
            const std::uint32_t pw = (std::uint32_t)m_PendingSize.x;
            const std::uint32_t ph = (std::uint32_t)m_PendingSize.y;
            m_PendingSize = ImVec2(0.0f, 0.0f);

            if (pw != (std::uint32_t)m_Size.x || ph != (std::uint32_t)m_Size.y)
            {
                m_Size = ImVec2((float)pw, (float)ph);
                m_Framebuffer->Resize(pw, ph);
                if (m_Renderer->m_Camera)
                    m_Renderer->m_Camera->SetAspectRatio((float)pw / (float)ph);
            }
        }

        std::uint32_t width = (std::uint32_t)m_Size.x;
        std::uint32_t height = (std::uint32_t)m_Size.y;
        if (width == 0 || height == 0) { width = 512; height = 512; }

        m_Renderer->SetScene(m_Scene.get());
        m_Renderer->RenderToFramebuffer(*m_Framebuffer, width, height, 0.0f);
    }

    void MeshPreview::Draw(const ImVec2& size, bool showGizmoTools)
    {
        Initialize();
        if (!m_Framebuffer) return;

        const float w = std::max(size.x, 16.0f);
        const float h = std::max(size.y, 16.0f);

        // So ANOTA — quem aplica e o Render, no comeco do proximo frame.
        if (std::abs(w - m_Size.x) > 1.0f || std::abs(h - m_Size.y) > 1.0f)
            m_PendingSize = ImVec2(w, h);

        const ImVec2 boundsMin = ImGui::GetCursorScreenPos();
        const ImVec2 boundsMax = ImVec2(boundsMin.x + w, boundsMin.y + h);

        // AddImage e nao ImGui::Image: o mesmo motivo do painel de textura —
        // as sobrecargas de ImGui::Image mudaram entre versoes do ImGui, e a
        // draw list e estavel.
        const ImTextureID texID = (ImTextureID)(std::uintptr_t)
            m_Framebuffer->GetColorAttachmentRendererID();

        if (texID != (ImTextureID)0)
        {
            ImGui::GetWindowDrawList()->AddImage(texID, boundsMin, boundsMax,
                ImVec2(0, 1), ImVec2(1, 0));
        }

        // InvisibleButton no lugar de Dummy: hover e clique so existem sobre um
        // item, e a imagem foi desenhada direto na draw list, que nao cria um.
        ImGui::InvisibleButton("##meshpreview", ImVec2(w, h),
            ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonRight |
            ImGuiButtonFlags_MouseButtonMiddle);
        m_Hovered = ImGui::IsItemHovered();

        if (m_Renderer && m_Renderer->m_Camera)
            ui::DrawViewGizmo(*m_Renderer->m_Camera, boundsMin, boundsMax, showGizmoTools);

        HandleInput(boundsMin, boundsMax);
    }

    void MeshPreview::HandleInput(const ImVec2&, const ImVec2&)
    {
        if (!m_Renderer) return;

        const ImVec2 mousePos = ImGui::GetMousePos();

        // Delta guardado POR INSTANCIA, e nao num static de funcao como nos
        // previews antigos. Com static, dois previews abertos ao mesmo tempo
        // dividiriam a mesma "ultima posicao" e um saltaria ao receber o mouse
        // de volta do outro.
        if (!m_HaveLastMouse)
        {
            m_LastMousePos = mousePos;
            m_HaveLastMouse = true;
        }

        const ImVec2 delta(mousePos.x - m_LastMousePos.x, mousePos.y - m_LastMousePos.y);
        m_LastMousePos = mousePos;

        if (!m_Hovered) return;

        // O gizmo tem prioridade: sem isto, arrastar num eixo tambem orbitaria
        // pelo caminho normal e o movimento sairia dobrado.
        if (ui::ViewGizmoCapturesMouse()) return;

        const ImGuiIO& io = ImGui::GetIO();

        // Roda sem Alt — e o que todo visualizador de asset faz, e aqui nao ha
        // objeto para selecionar nem gizmo de transformacao competindo pelo
        // clique, entao a exigencia de Alt do viewport nao se justifica.
        if (io.MouseWheel != 0.0f)
            m_Renderer->OnMouseZoom(io.MouseWheel);

        const glm::vec2 d(delta.x * 0.003f, delta.y * 0.003f);

        // Alt+esquerdo mantido para quem ja tem o dedo treinado no viewport;
        // botao direito orbita direto, que e o gesto natural num preview.
        if (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_Renderer->OnMouseRotate(d);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            m_Renderer->OnMouseRotate(d);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            m_Renderer->OnMousePan(d);
    }
}
