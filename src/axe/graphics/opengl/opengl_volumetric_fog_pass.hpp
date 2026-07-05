#pragma once
#include "axe/renderer/volumetric_fog_pass.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
    class Shader;
    class VertexArray;
    class VertexBuffer;

    class OpenGLVolumetricFogPass final : public VolumetricFogPass
    {
    public:
        OpenGLVolumetricFogPass() = default;
        ~OpenGLVolumetricFogPass() = default;

        void Initialize() override;
        bool IsInitialized() const override { return m_Shader != nullptr; }

        void Execute(
            const GBuffer& gbuffer,
            const VolumetricFogSettings& settings,
            const glm::mat4& invViewProj,
            const glm::vec3& cameraPosition,
            const std::vector<PointLight>& pointLights,
            float                        time,
            uint32_t                     width,
            uint32_t                     height) override;

    private:
        void EnsureSceneColorTex(uint32_t width, uint32_t height);

        std::shared_ptr<Shader>       m_Shader;
        std::shared_ptr<VertexArray>  m_QuadVAO;
        std::shared_ptr<VertexBuffer> m_QuadVBO;
        uint32_t m_SceneColorTex = 0;
        uint32_t m_LastW = 0, m_LastH = 0;
    };

} // namespace axe