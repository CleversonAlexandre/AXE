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
    class IndexBuffer;

    // Desenha partículas como billboards (quads virados pra câmera).
    // Sem instancing (a RHI não expõe) — monta um VB dinâmico com 4
    // vértices por partícula e billboarda no vertex shader usando os
    // eixos right/up da câmera. Soft particles via falloff radial no
    // fragment (não precisa de textura no MVP). Roda no forward, depois
    // do lighting, com depth-test ligado (geometria occlui) e
    // depth-write desligado.
    class AXE_API ParticleRenderer
    {
    public:
        void Render(const std::vector<ParticleBatch>& batches,
            const glm::mat4& viewProjection,
            const glm::mat4& view);

    private:
        void EnsureInitialized();
        void EnsureCapacity(uint32_t particleCount);

        std::shared_ptr<Shader>       m_Shader;
        std::shared_ptr<VertexArray>  m_VAO;
        std::shared_ptr<VertexBuffer> m_VBO;
        std::shared_ptr<IndexBuffer>  m_IBO;

        uint32_t           m_Capacity = 0;   // em partículas
        std::vector<float> m_Scratch;        // buffer CPU reaproveitado
    };

} // namespace axe