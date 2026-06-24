// material_preview.cpp
// Cena/câmera/framebuffer de preview 3D do material (a esfera renderizada à
// direita do node graph) e o input orbital (Alt + mouse) da câmera de preview.

#include "material_editor_window.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include <imgui.h>

namespace axe
{

    void MaterialEditorWindow::InitializePreview()
    {
        // Framebuffer do preview
        FramebufferSpecification spec;
        spec.Width = 512;
        spec.Height = 512;
        spec.Attachments = {
            FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::DEPTH32F,
        };
        m_PreviewFramebuffer = Framebuffer::Create(spec);

        // Renderer dedicado ao preview — desacoplado do renderer do editor
        m_PreviewRenderer = std::make_unique<ViewportRenderer>();
        m_PreviewRenderer->Initialize();
        m_PreviewRenderer->SetPickingEnabled(false);
        m_PreviewRenderer->SetPreviewMode(true);

        // Força forward imediatamente após Initialize — garante que mesmo que
        // SetPreviewMode seja chamado tarde ou revertido, o SceneRenderer já
        // começa com deferred desabilitado permanentemente para este renderer.
        if (m_PreviewRenderer->GetSceneRenderer())
        {
            m_PreviewRenderer->GetSceneRenderer()->SetDeferredEnabled(false);
            m_PreviewRenderer->GetSceneRenderer()->SetDeferredSupported(false);
        }

        // Cena isolada com a esfera de preview
        m_PreviewScene = std::make_unique<Scene>();
        m_PreviewEntity = m_PreviewScene->CreateEntity("PreviewSphere");

        auto& registry = m_PreviewScene->GetRegistry();

        // Escala padrão
        if (registry.all_of<TransformComponent>(m_PreviewEntity))
        {
            auto& tc = registry.get<TransformComponent>(m_PreviewEntity);
            tc.Data.Scale = glm::vec3(1.0f);
        }

        auto lightEntity = m_PreviewScene->CreateEntity("PreviewLight");
        auto& lc = registry.emplace<LightComponent>(lightEntity);
        lc.Data = std::make_shared<DirectionalLight>();
        lc.Data->Direction = glm::vec3(0.0f, -1.0f, -1.0f);
        lc.Data->Color = glm::vec3(1.0f, 1.0f, 1.0f);
        lc.Data->Intensity = 3.0f;
        lc.Data->AmbientStrength = 0.3f;

        // Mesh de preview — usa CreateSphere() diretamente para evitar
        // dependência do AssetDatabase (o UUID é apenas para referência)
        auto mesh = MeshFactory::CreateSphere();
        auto& mc = registry.emplace<MeshComponent>(m_PreviewEntity);
        mc.Data = mesh;
        mc.AssetUUID = PrimitiveUUID::Sphere;

        // O MaterialComponent é adicionado em OpenMaterial(),
        // quando m_Material estiver disponível


        m_PreviewRenderer->SetScene(m_PreviewScene.get());

        // Ambiente HDRI
        m_PreviewEnvironment = std::make_unique<SceneEnvironment>();
        m_PreviewEnvironment->LoadHDRI("resources/quarry_04_puresky_2k.hdr");
        m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());

        if (m_PreviewRenderer->GetSceneRenderer())
            m_PreviewRenderer->GetSceneRenderer()->SetEnvironment(m_PreviewEnvironment.get());
    }

    // -------------------------------------------------------------------------
    // Render (chamado ANTES do ImGui, no EditorLayer::OnRender)
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::RenderPreview()
    {
        if (!m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene) return;

        // Segunda linha de defesa — garante forward mesmo que algo externo
        // tenha alterado o SceneRenderer entre frames.
        if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
        {
            sr->SetDeferredEnabled(false);
            sr->SetDeferredSupported(false);
        }

        m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());
        if (m_PreviewEnvironment && m_PreviewRenderer->GetSceneRenderer())
            m_PreviewRenderer->GetSceneRenderer()->SetEnvironment(m_PreviewEnvironment.get());

        uint32_t width = (uint32_t)m_PreviewSize.x;
        uint32_t height = (uint32_t)m_PreviewSize.y;
        if (width == 0 || height == 0) { width = 512; height = 512; }

        m_PreviewRenderer->SetScene(m_PreviewScene.get());
        m_PreviewRenderer->RenderToFramebuffer(*m_PreviewFramebuffer, width, height, 0.0f);
    }

    // -------------------------------------------------------------------------
    // Janela de preview (textura renderizada + input orbital)
    // -------------------------------------------------------------------------

    void MaterialEditorWindow::DrawPreviewWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        if (ImGui::Begin("Material Preview", nullptr,
            ImGuiWindowFlags_NoScrollbar)) // | ImGuiWindowFlags_NoMove))
        {
            m_PreviewHovered = ImGui::IsWindowHovered();
            m_PreviewFocused = ImGui::IsWindowFocused();

            ImVec2 viewportSize = ImGui::GetContentRegionAvail();
            uint32_t width = static_cast<uint32_t>(viewportSize.x);
            uint32_t height = static_cast<uint32_t>(viewportSize.y);

            // Redimensiona framebuffer se o tamanho mudou
            if (width > 0 && height > 0 &&
                (std::abs((float)width - m_PreviewSize.x) > 1.0f ||
                    std::abs((float)height - m_PreviewSize.y) > 1.0f))
            {
                m_PreviewSize = ImVec2((float)width, (float)height);
                m_PreviewFramebuffer->Resize(width, height);

                if (m_PreviewRenderer && m_PreviewRenderer->m_Camera)
                    m_PreviewRenderer->m_Camera->SetAspectRatio((float)width / (float)height);
            }

            m_PreviewBoundsMin = ImGui::GetCursorScreenPos();
            m_PreviewBoundsMax = ImVec2(
                m_PreviewBoundsMin.x + viewportSize.x,
                m_PreviewBoundsMin.y + viewportSize.y);

            // Exibe a textura renderizada
            ImTextureID textureID = (ImTextureID)(uintptr_t)
                m_PreviewFramebuffer->GetColorAttachmentRendererID();
            if (textureID != (ImTextureID)0)
                ImGui::Image(textureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));

            HandlePreviewInput();
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }


    void MaterialEditorWindow::HandlePreviewInput()
    {
        if (!m_PreviewHovered) return;

        ImGuiIO& io = ImGui::GetIO();

        ImVec2 mousePos = ImGui::GetMousePos();
        static ImVec2 lastMousePos = mousePos;
        m_PreviewMouseDelta = ImVec2(
            mousePos.x - lastMousePos.x,
            mousePos.y - lastMousePos.y);
        lastMousePos = mousePos;

        // Controles da câmera apenas com Alt pressionado
        const bool alt = io.KeyAlt;
        if (!alt) return;

        glm::vec2 delta(m_PreviewMouseDelta.x, m_PreviewMouseDelta.y);
        delta *= 0.003f;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_PreviewRenderer->OnMouseRotate(delta);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            m_PreviewRenderer->OnMousePan(delta);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            m_PreviewRenderer->OnMouseZoom(delta.y * 10.0f);

        if (io.MouseWheel != 0.0f)
            m_PreviewRenderer->OnMouseZoom(io.MouseWheel);
    }

} // namespace axe