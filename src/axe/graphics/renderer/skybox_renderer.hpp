#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include <memory>
#include <glm/glm.hpp>

namespace axe
{
    class Shader;
    class VertexArray;

    class AXE_API SkyboxRenderer
    {
    public:
        void Initialize();
        bool HasCubemap() const { return m_Cubemap != nullptr; }
        void SetCubemap(std::shared_ptr<CubemapTexture> cubemap) { m_Cubemap = cubemap; }

        // ── Céu procedural ───────────────────────────────────────────────────
        void SetProceduralSky(bool enabled) { m_UseProceduralSky = enabled; }
        bool IsProceduralSky() const { return m_UseProceduralSky; }

        void SetSunDirection(const glm::vec3& dir) { m_SunDirection = glm::normalize(dir); }
        void SetTurbidity(float t) { m_Turbidity = t; }
        void SetCloudCoverage(float c) { m_CloudCoverage = c; }
        void SetCloudSpeed(float s) { m_CloudSpeed = s; }
        void SetCloudColor(const glm::vec3& c) { m_CloudColor = c; }
        void SetNightColor(const glm::vec3& c) { m_NightColor = c; }

        // ── SKYLIGHT_CHAIN_V1 — o ceu passa a OBEDECER ao sol ────────────
        //
        // Ate aqui o ceu procedural so recebia a DIRECAO do sol. A cor e a
        // intensidade nunca chegavam, entao o ceu era permanentemente
        // meio-dia: desligar o sol (ou apagar a luz direcional) nao mudava
        // NADA no ceu, e como o ceu e quem gera o IBL desde o SKY_IBL_V1, a
        // cena continuava iluminada por um sol que nao existia mais.
        //
        // Este e o elo que faltava para a cadeia que a Unreal tem:
        //   sol -> atmosfera -> captura do ceu -> luz de ambiente
        // Com ele, zerar ou apagar o sol escurece o ceu, e o ceu escuro
        // escurece a cena — sozinho, sem nenhum caso especial.
        void SetSunColor(const glm::vec3& c) { m_SunColor = c; }
        void SetSunIntensity(float i) { m_SunIntensity = (i > 0.0f) ? i : 0.0f; }

        void Tick(float dt) { m_Time += dt; }

        void Render(const glm::mat4& view, const glm::mat4& projection);
        void RenderDeferred(const glm::mat4& view, const glm::mat4& projection);

        // ═══════════════════════════════════════════════════════════════════
        //  SKY_IBL_V1 — O CEU PASSA A ILUMINAR A CENA
        //
        //  O PROBLEMA. O ceu procedural era so DESENHO. A luz de ambiente da
        //  cena so existia quando havia um HDRI de arquivo (SceneEnvironment::
        //  HasIBL exigia um cubemap vindo de disco); sem ele, o
        //  u_HasIBL do lighting pass caia em 0 e o ambiente virava a constante
        //  u_AmbientStrength.
        //
        //  Consequencia exata, e a queixa que originou isto: mover o sol
        //  mudava o termo direto no objeto e mudava o desenho do ceu, mas o
        //  ENTORNO nao acompanhava — porque o entorno era um numero, nao o
        //  ceu. E mesmo com HDRI era errado de outro jeito: o irradiance era
        //  gerado UMA vez, no load, entao o ciclo dia/noite movia o sol sem
        //  que a luz do ambiente mudasse junto.
        //
        //  A SOLUCAO. Renderizar o proprio ceu num cubemap e passar pela
        //  convolucao de IBL que a engine JA tem (GenerateIrradianceMap /
        //  GeneratePrefilteredMap, em opengl_cubemap.cpp). Nenhum sistema
        //  novo, nenhuma copia do shader do ceu: a captura chama o DrawSky
        //  daqui.
        //
        //  QUANDO REBAKEIA. Nao por frame — quando o resultado mudaria: sol
        //  girou mais que ~1 grau, ou turbidez/cobertura/cor de nuvem/cor de
        //  noite mudaram. Parado, custa zero.
        // ═══════════════════════════════════════════════════════════════════

        // Chamado UMA vez por frame pelo SceneRenderer, antes de qualquer
        // bind de framebuffer do frame (a captura troca FBO e viewport).
        void UpdateSkyIBL();

        // nullptr quando o ceu procedural esta desligado — nesse caso quem
        // manda e o HDRI, e o SceneEnvironment cai de volta nele sozinho.
        std::shared_ptr<CubemapTexture> GetSkyIBL() const
        {
            return m_UseProceduralSky ? m_SkyIBL : nullptr;
        }

        // Forca o proximo UpdateSkyIBL a rebakear. Util quando algo fora
        // destes parametros muda o ceu.
        void InvalidateSkyIBL() { m_SkyIBLValid = false; }

    private:
        void DrawSky(const glm::mat4& view, const glm::mat4& proj);
        bool SkyIBLNeedsRebake() const;

        std::shared_ptr<Shader>         m_Shader;      // HDRI
        std::shared_ptr<Shader>         m_ProcShader;  // procedural
        std::shared_ptr<VertexArray>    m_VertexArray;
        std::shared_ptr<CubemapTexture> m_Cubemap;

        bool      m_UseProceduralSky = false;
        glm::vec3 m_SunDirection{ 0.5f, 0.5f, 0.5f };
        float     m_Turbidity = 2.5f;
        float     m_CloudCoverage = 0.5f;
        float     m_CloudSpeed = 0.02f;
        glm::vec3 m_CloudColor{ 1.f, 1.f, 1.f };
        glm::vec3 m_NightColor{ 0.01f, 0.01f, 0.03f };

        // SKYLIGHT_CHAIN_V1 — cor e intensidade do sol que ilumina este ceu.
        glm::vec3 m_SunColor{ 1.f, 1.f, 1.f };
        float     m_SunIntensity = 1.0f;

        float     m_Time = 0.f;

        // ── SKY_IBL_V1 — estado do cubemap de iluminacao ─────────────────────
        //
        // 64 por face de proposito. A irradiancia difusa e ultra-baixa
        // frequencia — a convolucao cosseno joga fora qualquer detalhe acima
        // disso — e o prefiltered ja e reamostrado para 128 com 5 mips. Subir
        // este numero custa tempo de rebake e nao muda a imagem.
        static constexpr uint32_t kSkyIBLFaceSize = 64;

        std::shared_ptr<CubemapTexture> m_SkyIBL;

        // Parametros com que o m_SkyIBL foi assado. Comparados em
        // SkyIBLNeedsRebake — e isto que faz o rebake ser raro em vez de por
        // frame. As NUVENS ficam de fora do teste de propriedade: elas andam
        // com u_Time todo frame, e rebakear por causa delas seria rebakear
        // sempre; a cobertura entra, o deslocamento nao.
        bool      m_SkyIBLValid = false;
        glm::vec3 m_BakedSunDir{ 0.0f };
        float     m_BakedTurbidity = -1.0f;
        float     m_BakedCloudCoverage = -1.0f;
        glm::vec3 m_BakedCloudColor{ -1.0f };
        glm::vec3 m_BakedNightColor{ -1.0f };

        // SKYLIGHT_CHAIN_V1 — OBRIGATORIO estarem no teste de rebake. Sem
        // isto, baixar a intensidade do sol escureceria o ceu NA TELA e
        // deixaria o cubemap de iluminacao com o ceu antigo e claro: a cena
        // continuaria iluminada por um ceu que ninguem mais ve. E o pior tipo
        // de bug, porque a imagem fica plausivel.
        glm::vec3 m_BakedSunColor{ -1.0f };
        float     m_BakedSunIntensity = -1.0f;
    };

} // namespace axe