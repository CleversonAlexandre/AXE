#pragma once
#include "axe/core/types.hpp"
#include "render_queue.hpp"
#include "axe/scene/scene.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/graphics/renderer/line_renderer.hpp"
#include "axe/graphics/renderer/mesh_renderer.hpp"
#include "axe/graphics/renderer/shadow_map_pass.hpp"
#include "axe/graphics/renderer/gbuffer.hpp"
#include "axe/graphics/renderer/geometry_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"
#include "axe/graphics/renderer/lighting_pass.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/renderer/skybox_renderer.hpp"
#include "axe/graphics/renderer/outline_renderer.hpp"

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
            entt::entity selectedEntity);

        void SetEnvironment(const SceneEnvironment* env)
        {
            m_Environment = env;
            m_MeshRenderer.SetEnvironment(env);
        }

        void SetSSAOSettings(const SSAOSettings& s) { m_SSAOSettings = s; }
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
        OutlineRenderer m_OutlineRenderer;

        std::shared_ptr<ShadowMapPass> m_ShadowPass;
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
            const glm::vec3& cameraPosition = glm::vec3(0.0f));
        void RenderForward(const RenderQueue& queue,
            const glm::mat4& viewProjection,
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