#pragma once
#include "axe/core/types.hpp"
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
#include <entt/entt.hpp>
#include "axe/lighting/directional_light.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/renderer/skybox_renderer.hpp"

namespace axe
{
    class AXE_API SceneRenderer
    {
    public:
        SceneRenderer();
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

        // false = forward (atual), true = deferred + SSAO
        void SetDeferredEnabled(bool enabled) { m_DeferredEnabled = enabled; }

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

        void SetDeferredSupported(bool supported) { m_DeferredSupported = supported; }

    private:
        // Forward — mantido intacto
        CubeRenderer m_CubeRenderer;
        MeshRenderer m_MeshRenderer;
        LineRenderer m_LineRenderer;
        std::shared_ptr<ShadowMapPass> m_ShadowPass;

        // Deferred
        GBuffer                        m_GBuffer;
        std::shared_ptr<GeometryPass>  m_GeometryPass;
        std::shared_ptr<SSAOPass>      m_SSAOPass;
        std::shared_ptr<LightingPass>  m_LightingPass;

        SSAOSettings            m_SSAOSettings;
        const SceneEnvironment* m_Environment = nullptr;
        uint32_t                m_TargetFBO = 0;
        bool                    m_DeferredEnabled = false;
        uint32_t                m_Width = 0;
        uint32_t                m_Height = 0;

        // Skybox — passado pelo ViewportRenderer
        SkyboxRenderer* m_SkyboxRenderer = nullptr;
        glm::mat4       m_SkyboxView{ 1.0f };
        glm::mat4       m_SkyboxProjection{ 1.0f };

        void RenderShadowPass(const Scene& scene, const DirectionalLight* light);
        void RenderEntity(const Scene& scene, entt::entity entity,
            const glm::mat4& parentTransform,
            entt::entity selectedEntity,
            const DirectionalLight* light);
        void GeometryPassEntity(const Scene& scene, entt::entity entity);
        void RenderDeferredScene(const Scene& scene,
            const glm::mat4& viewProjection,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            entt::entity selectedEntity,
            const DirectionalLight* light,
            uint32_t width, uint32_t height);

        bool m_DeferredSupported = true;

    };

    
}