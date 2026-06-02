#pragma once
#include "axe/graphics/renderer/geometry_pass.hpp"

namespace axe
{
    class Shader;

    class OpenGLGeometryPass final : public GeometryPass
    {
    public:
        void Initialize()                                    override;
        void Begin(GBuffer& gbuffer,
            const glm::mat4& viewProjection,
            const glm::vec3& cameraPosition) override;
        void DrawMesh(const Mesh& mesh,
            const glm::mat4& model,
            const Material* material = nullptr)     override;
        void End()                                           override;

        bool IsInitialized() const override { return m_Initialized; }

    private:
        std::shared_ptr<Shader> m_Shader;
        bool      m_Initialized = false;
        glm::mat4 m_ViewProjection{ 1.0f };
        glm::vec3 m_CameraPosition{ 0.0f };
    };
}