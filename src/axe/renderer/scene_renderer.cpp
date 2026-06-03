#include "scene_renderer.hpp"
#include "scene_collector.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/scene/components.hpp"
#include "axe/graphics/renderer/outline_renderer.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/renderer/skybox_renderer.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>

namespace axe
{

    SceneRenderer::SceneRenderer() {}

    // =============================================================================
    // API principal — recebe RenderQueue já montada, sem saber nada de Scene/entt
    // =============================================================================

    void SceneRenderer::Render(const RenderQueue& queue,
        const glm::mat4& viewProjection,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        uint32_t width, uint32_t height)
    {
        RenderShadowPass(queue, cameraPosition);

        if (m_DeferredEnabled && m_DeferredSupported && m_TargetFBO != 0)
            RenderDeferred(queue, viewProjection, view, projection, cameraPosition, width, height);
        else
            RenderForward(queue, viewProjection, cameraPosition);
    }

    // =============================================================================
    // Shadow pass — opera sobre RenderQueue
    // =============================================================================

    void SceneRenderer::RenderShadowPass(const RenderQueue& queue,
        const glm::vec3& cameraPosition)
    {
        if (!queue.Light || !queue.Light->CastShadows) return;

        if (!m_ShadowPass) m_ShadowPass = ShadowMapPass::Create();
        if (!m_ShadowPass->IsInitialized()) m_ShadowPass->Initialize(4096);

        auto lsm = ShadowMapPass::CalcLightSpaceMatrix(
            queue.Light->Direction, queue.Light->ShadowDistance, cameraPosition);

        m_ShadowPass->Begin(lsm);
        for (auto& dc : queue.Meshes)
            if (dc.Mesh) m_ShadowPass->DrawMesh(*dc.Mesh, dc.Transform);
        m_ShadowPass->End();
    }

    // =============================================================================
    // Forward pass — opera sobre RenderQueue
    // =============================================================================

    void SceneRenderer::RenderForward(const RenderQueue& queue,
        const glm::mat4& viewProjection,
        const glm::vec3& cameraPosition)
    {
        m_MeshRenderer.SetEnvironment(m_Environment);
        if (m_ShadowPass)
            m_MeshRenderer.SetShadowMap(
                m_ShadowPass->GetDepthMapID(),
                m_ShadowPass->GetLightSpaceMatrix());

        if (m_SkyboxRenderer)
        {
            RenderCommand::SetDepthTest(false);
            RenderCommand::SetCullFace(false);
            m_SkyboxRenderer->Render(m_SkyboxView, m_SkyboxProjection);
            RenderCommand::SetDepthTest(true);
            RenderCommand::SetCullFace(true);
        }

        m_MeshRenderer.Begin(viewProjection, cameraPosition);
        for (auto& dc : queue.Meshes)
            if (dc.Mesh) m_MeshRenderer.DrawMesh(*dc.Mesh, dc.Transform, dc.Material, queue.Light);
        m_MeshRenderer.End();
    }

    // =============================================================================
    // Deferred pass — opera sobre RenderQueue
    // =============================================================================

    void SceneRenderer::RenderDeferred(const RenderQueue& queue,
        const glm::mat4& viewProjection,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        uint32_t width, uint32_t height)
    {
        // --- Resize ---
        if (width != m_Width || height != m_Height)
        {
            m_Width = width;
            m_Height = height;
            m_GBuffer.Resize(width, height);
            if (m_SSAOPass) m_SSAOPass->Resize(width, height);
        }

        // --- 1. Geometry Pass ---
        m_GeometryPass->Begin(m_GBuffer, viewProjection, cameraPosition);
        for (auto& dc : queue.Meshes)
            if (dc.Mesh) m_GeometryPass->DrawMesh(*dc.Mesh, dc.Transform, dc.Material);
        m_GeometryPass->End();

        // --- 2. SSAO ---
        uint32_t ssaoID = 0;
        if (m_SSAOSettings.Enabled && m_SSAOPass && m_SSAOPass->IsInitialized())
        {
            m_SSAOPass->Execute(m_GBuffer, projection, view, m_SSAOSettings);
            ssaoID = m_SSAOPass->GetOcclusionTextureID();
        }
        if (m_LightingPass) m_LightingPass->SetSSAODebug(m_SSAOSettings.Debug);

        // --- 3. Lighting Pass ---
        RenderCommand::BindFramebuffer(m_TargetFBO);
        RenderCommand::SetViewport(0, 0, width, height);

        RenderCommand::BlitDepth(m_GBuffer.GetFramebufferID(), m_TargetFBO, width, height);

        uint32_t  shadowID = m_ShadowPass ? m_ShadowPass->GetDepthMapID() : 0;
        glm::mat4 lsm = m_ShadowPass ? m_ShadowPass->GetLightSpaceMatrix() : glm::mat4(1.0f);

        m_LightingPass->Execute(m_GBuffer, ssaoID, shadowID, lsm,
            cameraPosition, queue.Light, m_Environment, queue.PointLights);

        // --- 4. Skybox ---
        // O LightingPass agora preserva o depth do BlitDepth (depthMask=false).
        // O skybox usa LessEqual — aparece onde depth=1.0 (céu) e é bloqueado
        // pela geometria onde depth < 1.0.
        if (m_SkyboxRenderer)
            m_SkyboxRenderer->RenderDeferred(m_SkyboxView, m_SkyboxProjection);

        // --- 5. Outline ---
        if (queue.SelectedMesh && queue.SelectedID != UINT32_MAX)
        {
            RenderCommand::SetDepthTest(true);
            RenderCommand::SetDepthWrite(true);
            RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::Less);
            RenderCommand::SetColorWrite(true);
            RenderCommand::SetCullFace(false);
            RenderCommand::SetStencilTest(false);
            RenderCommand::SetStencilWrite(0xFF);

            m_OutlineRenderer.Begin(viewProjection);
            m_OutlineRenderer.DrawOutline(*queue.SelectedMesh, queue.SelectedTransform,
                { 1.0f, 0.0f, 0.0f, 1.0f }, 1.03f);
            m_OutlineRenderer.End();
        }

        RenderCommand::ResetState();
    }

    // =============================================================================
    // Compat — RenderScene legados constroem RenderQueue internamente
    // =============================================================================

    void SceneRenderer::RenderScene(const Scene& scene,
        const EditorCamera& camera,
        entt::entity selectedEntity)
    {
        const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
        const glm::mat4 projection = camera.GetProjectionMatrix();
        const glm::vec3 cameraPosition = camera.GetPosition();
        const glm::mat4 view = camera.GetViewMatrix();
        uint32_t width = (uint32_t)camera.GetViewportWidth();
        uint32_t height = (uint32_t)camera.GetViewportHeight();

        auto queue = SceneCollector::Collect(scene, (uint32_t)selectedEntity);
        Render(queue, viewProjection, view, projection, cameraPosition, width, height);
    }

    void SceneRenderer::RenderScene(const Scene& scene,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        entt::entity selectedEntity)
    {
        glm::mat4 viewProjection = projection * view;
        auto queue = SceneCollector::Collect(scene, (uint32_t)selectedEntity);
        Render(queue, viewProjection, view, projection, cameraPosition, 0, 0);
    }

    // =============================================================================
    // InitializeDeferredPasses
    // =============================================================================

    void SceneRenderer::InitializeDeferredPasses(uint32_t width, uint32_t height)
    {
        m_GeometryPass = GeometryPass::Create();
        m_GeometryPass->Initialize();

        m_LightingPass = LightingPass::Create();
        m_LightingPass->Initialize();

        m_SSAOPass = SSAOPass::Create();
        m_SSAOPass->Initialize(width, height);

        m_GBuffer.Initialize(width, height);

        m_Width = width;
        m_Height = height;
    }

    // =============================================================================
    // Compat legacy helpers (usados pelo preview/forward legado)
    // =============================================================================

    void SceneRenderer::RenderShadowPassLegacy(const Scene& scene,
        const DirectionalLight* light, const glm::vec3& cameraPosition)
    {
        if (!light || !light->CastShadows) return;
        if (!m_ShadowPass) m_ShadowPass = ShadowMapPass::Create();
        if (!m_ShadowPass->IsInitialized()) m_ShadowPass->Initialize(4096);

        auto lsm = ShadowMapPass::CalcLightSpaceMatrix(
            light->Direction, light->ShadowDistance, cameraPosition);
        m_ShadowPass->Begin(lsm);

        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        std::function<void(entt::entity)> renderDepth = [&](entt::entity entity)
            {
                if (!registry.valid(entity)) return;
                if (registry.any_of<LightComponent, PostProcessComponent, FolderComponent>(entity)) return;
                auto* tc = registry.try_get<TransformComponent>(entity);
                auto* mc = registry.try_get<MeshComponent>(entity);
                if (mc && mc->Data && tc) m_ShadowPass->DrawMesh(*mc->Data, tc->Data.GetMatrix());
                auto* rel = registry.try_get<RelationshipComponent>(entity);
                if (rel) for (auto child : rel->Children) renderDepth(child);
            };
        for (auto entity : const_cast<Scene&>(scene).GetRootEntities()) renderDepth(entity);
        m_ShadowPass->End();
    }

    void SceneRenderer::RenderEntity(const Scene& scene, entt::entity entity,
        const glm::mat4& parentTransform, entt::entity selectedEntity,
        const DirectionalLight* light)
    {
        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        if (!registry.valid(entity)) return;
        if (registry.any_of<PostProcessComponent, LightComponent>(entity)) return;
        if (registry.any_of<FolderComponent>(entity))
        {
            auto* rel = registry.try_get<RelationshipComponent>(entity);
            if (rel) for (auto child : rel->Children)
                RenderEntity(scene, child, glm::mat4(1.0f), selectedEntity, light);
            return;
        }

        auto* tc = registry.try_get<TransformComponent>(entity);
        auto* mc = registry.try_get<MeshComponent>(entity);
        auto* mat = registry.try_get<MaterialComponent>(entity);

        glm::mat4 worldTransform = tc ? tc->Data.GetMatrix() : glm::mat4(1.0f);

        if (mc && mc->Data)
            m_MeshRenderer.DrawMesh(*mc->Data, worldTransform, mat ? mat->Data.get() : nullptr, light);
        else
            m_CubeRenderer.DrawCube(worldTransform, entity == selectedEntity);

        auto* rel = registry.try_get<RelationshipComponent>(entity);
        if (rel) for (auto child : rel->Children)
            RenderEntity(scene, child, glm::mat4(1.0f), selectedEntity, light);
    }

    void SceneRenderer::GeometryPassEntity(const Scene& scene, entt::entity entity)
    {
        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        if (!registry.valid(entity)) return;
        if (registry.any_of<PostProcessComponent, LightComponent, FolderComponent>(entity)) return;

        auto* tc = registry.try_get<TransformComponent>(entity);
        auto* mc = registry.try_get<MeshComponent>(entity);
        auto* mat = registry.try_get<MaterialComponent>(entity);

        if (mc && mc->Data && tc)
            m_GeometryPass->DrawMesh(*mc->Data, tc->Data.GetMatrix(), mat ? mat->Data.get() : nullptr);

        auto* rel = registry.try_get<RelationshipComponent>(entity);
        if (rel) for (auto child : rel->Children) GeometryPassEntity(scene, child);
    }

} // namespace axe