#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>

namespace axe
{
    class Mesh;

    class AXE_API ShadowMapPass
    {
    public:
        virtual ~ShadowMapPass() = default;

        virtual void Initialize(uint32_t resolution = 2048) = 0;

        virtual void Begin(const glm::mat4& lightSpaceMatrix) = 0;
        virtual void DrawMesh(const Mesh& mesh, const glm::mat4& model) = 0;
        virtual void End() = 0;

        virtual uint32_t          GetDepthMapID()        const = 0;
        virtual const glm::mat4& GetLightSpaceMatrix()  const = 0;
        virtual bool              IsInitialized()        const = 0;

        // Factory — igual ao Shader::Create
        static std::shared_ptr<ShadowMapPass> Create();

        // Utilitário matemático — sem OpenGL, pode ficar aqui
        static glm::mat4 CalcLightSpaceMatrix(
            const glm::vec3& direction,
            float distance = 20.0f);
    };
}