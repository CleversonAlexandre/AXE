#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "gbuffer.hpp"
#include <memory>

namespace axe
{
    class Mesh;
    class Material;
    class Shader;

    class AXE_API GeometryPass
    {
    public:
        virtual ~GeometryPass() = default;

        virtual void Initialize() = 0;
        virtual void Begin(GBuffer& gbuffer,
            const glm::mat4& viewProjection,
            const glm::vec3& cameraPosition) = 0;
        virtual void DrawMesh(const Mesh& mesh,
            const glm::mat4& model,
            const Material* material = nullptr) = 0;
        virtual void End() = 0;

        virtual bool IsInitialized() const = 0;

        static std::shared_ptr<GeometryPass> Create();
    };
}