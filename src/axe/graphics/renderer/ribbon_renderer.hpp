#pragma once
#include "axe/core/types.hpp"
#include "axe/renderer/render_queue.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace axe
{
    class Shader;
    class VertexArray;
    class VertexBuffer;

    // Renderiza ribbons/trails conectando partículas em sequência como
    // uma fita contínua (triangle strip). A geometria é construída no CPU
    // a cada frame — sem instancing. Layout: position(3)+uv(2)+color(4)=9f.
    class AXE_API RibbonRenderer
    {
    public:
        void Tick(float dt) { m_Time += dt; }

        void Render(const std::vector<RibbonBatch>& batches,
            const glm::mat4& viewProjection,
            const glm::vec3& cameraPosition);

    private:
        void EnsureInitialized();
        void EnsureCapacity(uint32_t vertexCount);

        std::shared_ptr<Shader>       m_DefaultShader;
        std::shared_ptr<VertexArray>  m_VAO;
        std::shared_ptr<VertexBuffer> m_VBO;

        uint32_t           m_Capacity = 0;
        std::vector<float> m_Scratch;
        float              m_Time = 0.f;
    };

} // namespace axe