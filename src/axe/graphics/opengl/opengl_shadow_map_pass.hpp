#pragma once
#include "axe/graphics/renderer/shadow_map_pass.hpp"
#include <cstdint>

namespace axe
{
    class Shader;

    class OpenGLShadowMapPass : public ShadowMapPass
    {
    public:
        OpenGLShadowMapPass() = default;
        ~OpenGLShadowMapPass() override;

        void Initialize(uint32_t resolution = 2048) override;
        void Begin(const glm::mat4& lightSpaceMatrix)        override;
        void DrawMesh(const Mesh& mesh, const glm::mat4& model) override;
        void End()                                           override;

        uint32_t         GetDepthMapID()       const override { return m_DepthMapID; }
        const glm::mat4& GetLightSpaceMatrix() const override { return m_LightSpaceMatrix; }
        bool             IsInitialized()       const override { return m_Initialized; }

    private:
        uint32_t  m_FBO = 0;
        uint32_t  m_DepthMapID = 0;
        uint32_t  m_Resolution = 2048;
        glm::mat4 m_LightSpaceMatrix{ 1.0f };
        bool      m_Initialized = false;
        int       m_SavedFBO = 0;
        int       m_SavedViewport[4] = {};

        std::shared_ptr<Shader> m_DepthShader;
    };
}