#pragma once
#include "axe/core/types.hpp"
#include "render_queue.hpp"
#include "axe/scene/scene.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/graphics/renderer/line_renderer.hpp"
#include "axe/graphics/renderer/mesh_renderer.hpp"
#include "axe/graphics/renderer/shadow_map_pass.hpp"
#include "axe/graphics/renderer/cascaded_shadow_pass.hpp"
#include "axe/graphics/renderer/gbuffer.hpp"
#include "axe/graphics/renderer/geometry_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"
#include "axe/graphics/renderer/lighting_pass.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/renderer/skybox_renderer.hpp"
#include "axe/graphics/renderer/outline_renderer.hpp"
#include "axe/graphics/renderer/particle_renderer.hpp"
#include "axe/graphics/renderer/ribbon_renderer.hpp"
#include "axe/renderer/volumetric_fog_pass.hpp"
#include "axe/graphics/renderer/taa_pass.hpp"

namespace axe
{
    class AXE_API SceneRenderer
    {
    public:
        SceneRenderer();

        // --- API principal: recebe RenderQueue já montada ---
        void Render(const RenderQueue& queue,
            const glm::mat4& viewProjection,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            uint32_t width, uint32_t height);

        // --- Compat: monta RenderQueue internamente a partir da Scene ---
        // Mantido para não quebrar o ViewportRenderer durante a transição
        void RenderScene(const Scene& scene, const EditorCamera& camera,
            entt::entity selectedEntity);
        void RenderScene(const Scene& scene,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            entt::entity selectedEntity,
            uint32_t width = 0,
            uint32_t height = 0);

        // Força o shader do Lighting Pass a recompilar do zero — ver
        // comentário em LightingPass::RecompileShader (no header base)
        // pro contexto completo do porquê isso existe.
        void RecompileLightingShader()
        {
            if (m_LightingPass) m_LightingPass->RecompileShader();
        }

        void SetEnvironment(const SceneEnvironment* env)
        {
            m_Environment = env;
            m_MeshRenderer.SetEnvironment(env);
        }

        void SetSSAOSettings(const SSAOSettings& s) { m_SSAOSettings = s; }
        void SetFogSettings(const VolumetricFogSettings& s) { m_FogSettings = s; }
        void SetTAASettings(const TAASettings& s) { m_TAASettings = s; }

        // Configura o céu procedural no SkyboxRenderer
        void SetProceduralSky(bool enabled,
            const glm::vec3& sunDir,
            float turbidity,
            float cloudCoverage,
            float cloudSpeed,
            const glm::vec3& cloudColor,
            const glm::vec3& nightColor)
        {
            if (!m_SkyboxRenderer) return;
            m_SkyboxRenderer->SetProceduralSky(enabled);
            m_SkyboxRenderer->SetSunDirection(sunDir);
            m_SkyboxRenderer->SetTurbidity(turbidity);
            m_SkyboxRenderer->SetCloudCoverage(cloudCoverage);
            m_SkyboxRenderer->SetCloudSpeed(cloudSpeed);
            m_SkyboxRenderer->SetCloudColor(cloudColor);
            m_SkyboxRenderer->SetNightColor(nightColor);
        }
        // Deve ser chamado ANTES de Render() a cada frame pra aplicar jitter.
        // Retorna a projection matrix com jitter aplicado.
        glm::mat4 BeginTAAFrame(const glm::mat4& projection,
            const glm::mat4& viewProjection,
            uint32_t width, uint32_t height);

        // Acesso ao depth do GBuffer para o TAA pass no viewport renderer
        uint32_t GetGBufferDepthID() const { return m_GBuffer.GetDepthID(); }

        // Acesso ao GBuffer completo para o SSR pass
        const GBuffer& GetGBuffer() const { return m_GBuffer; }
        void SetTargetFramebuffer(uint32_t fboID) { m_TargetFBO = fboID; }
        uint32_t GetTargetFBO() const { return m_TargetFBO; }
        bool IsDeferredEnabled() const { return m_DeferredEnabled; }
        bool IsDeferredSupported() const { return m_DeferredSupported; }
        void SetDeferredEnabled(bool enabled) { m_DeferredEnabled = enabled; }
        void SetDeferredSupported(bool supported) { m_DeferredSupported = supported; }

        MeshRenderer& GetMeshRenderer() { return m_MeshRenderer; }

        void SetSkyboxRenderer(SkyboxRenderer* skybox,
            const glm::mat4& view,
            const glm::mat4& projection)
        {
            m_SkyboxRenderer = skybox;
            m_SkyboxView = view;
            m_SkyboxProjection = projection;
        }

        void InitializeDeferredPasses(uint32_t width, uint32_t height);
        void SetEnvironment(const SceneEnvironment* env, bool dummy) {} // overload compat

    private:
        CubeRenderer    m_CubeRenderer;
        MeshRenderer    m_MeshRenderer;
        LineRenderer    m_LineRenderer;
        OutlineRenderer  m_OutlineRenderer;
        ParticleRenderer m_ParticleRenderer;
        RibbonRenderer   m_RibbonRenderer;
        std::shared_ptr<VolumetricFogPass> m_FogPass;
        VolumetricFogSettings m_FogSettings;
        std::shared_ptr<TAAPass> m_TAAPass;
        TAASettings              m_TAASettings;

        std::shared_ptr<ShadowMapPass> m_ShadowPass;
        std::shared_ptr<CascadedShadowPass> m_CSMPass;
        bool m_UseCSM = true; // usa CSM ao invés do shadow map simples
        GBuffer                        m_GBuffer;
        std::shared_ptr<GeometryPass>  m_GeometryPass;
        std::shared_ptr<SSAOPass>      m_SSAOPass;
        std::shared_ptr<LightingPass>  m_LightingPass;

        SSAOSettings            m_SSAOSettings;
        const SceneEnvironment* m_Environment = nullptr;
        uint32_t                m_TargetFBO = 0;
        bool                    m_DeferredEnabled = false;
        bool                    m_DeferredSupported = true;
        uint32_t                m_Width = 0;
        uint32_t                m_Height = 0;

        SkyboxRenderer* m_SkyboxRenderer = nullptr;
        glm::mat4       m_SkyboxView{ 1.0f };
        glm::mat4       m_SkyboxProjection{ 1.0f };

        // Passes internos
        void RenderShadowPass(const RenderQueue& queue,
            const glm::vec3& cameraPosition,
            const glm::mat4& view,
            const glm::mat4& projection);
        void RenderForward(const RenderQueue& queue,
            const glm::mat4& viewProjection,
            const glm::mat4& view,
            const glm::vec3& cameraPosition);
        void RenderDeferred(const RenderQueue& queue,
            const glm::mat4& viewProjection,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            uint32_t width, uint32_t height);

        // Compat helpers — usados pelos RenderScene legados
        void RenderShadowPassLegacy(const Scene& scene, const DirectionalLight* light,
            const glm::vec3& cameraPosition = glm::vec3(0.0f));
        void RenderEntity(const Scene& scene, entt::entity entity,
            const glm::mat4& parentTransform,
            entt::entity selectedEntity,
            const DirectionalLight* light);
        void GeometryPassEntity(const Scene& scene, entt::entity entity);
    };
}