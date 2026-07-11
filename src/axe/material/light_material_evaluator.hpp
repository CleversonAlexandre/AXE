#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <map>
#include <string>

namespace axe
{
    class Shader;
    class VertexArray;
    class Framebuffer;
    class Texture2D;

    // Avalia um shader de domínio "Light Function" (compilado por
    // MaterialCompiler::CompileLightFunction) uma vez por frame, num
    // framebuffer de 1x1 pixel, e lê o resultado de volta — assim, uma
    // Point Light (ou Directional Light) pode ter sua cor/intensidade
    // controlada por um grafo de nodes real (Time, Sine, Noise, etc.),
    // compilado pra GLSL de verdade, sem precisar desenhar nada na tela
    // nem recompilar o Lighting Pass inteiro.
    //
    // Custo: cada Evaluate() faz um glReadPixels síncrono (força a CPU
    // esperar a GPU terminar aquele 1 pixel) — barato isoladamente, mas
    // se você tiver MUITAS luzes com material attachado ao mesmo tempo,
    // pode somar. Para o uso típico (algumas luzes decorativas/dinâmicas
    // por cena) não é um problema perceptível.
    class AXE_API LightMaterialEvaluator
    {
    public:
        void Initialize();
        bool IsInitialized() const { return m_Initialized; }

        // Avalia o shader pro frame atual e retorna a cor resultante (RGB,
        // 0..1 — vem de um framebuffer RGBA8, então é quantizado em 256
        // níveis por canal, suficiente pra flicker/cor sem serrilhado visível)
        // Avalia o shader num grid 8x8 (o VS do shader deve espalhar UVs
        // pelo quad — ver MaterialCompiler::CompileEmissiveAverage) e
        // devolve a MÉDIA dos 64 texels. O shader escreve valor/8 (o FBO
        // é LDR); esta função multiplica de volta — cobre emissive até 8x
        // com precisão de ~0.03, de sobra pra média de GI.
        glm::vec3 EvaluateAverage(const std::shared_ptr<Shader>& shader,
            const std::map<std::string, std::shared_ptr<Texture2D>>& samplers);

        glm::vec3 Evaluate(const std::shared_ptr<Shader>& shader,
            const std::map<std::string, std::shared_ptr<Texture2D>>& samplers,
            float time, const glm::vec3& cameraPosition);

    private:
        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::shared_ptr<Framebuffer> m_AvgFramebuffer; // 8x8, lazy
        bool m_Initialized = false;
    };

} // namespace axe