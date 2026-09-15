#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/lighting/interior_volume.hpp"
#include "axe/lighting/probe_volume.hpp"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace axe
{
    class GBuffer;

    // VOLUME_SUN_V2 — declarado, nao incluido: `cascaded_shadow_pass.hpp` mora
    // em graphics/renderer/ e este header e da camada renderer/, que ja evita
    // incluir de graphics (o GBuffer logo acima segue a mesma regra). Quem
    // desempacota a cascata e a implementacao OpenGL, que pode incluir.
    class CascadedShadowPass;

    // Configurações do fog — editáveis via PostProcessComponent no Inspector.
    // Sem GL aqui — só dados puros.
    struct AXE_API VolumetricFogSettings
    {
        bool      Enabled = false;
        glm::vec3 FogColor{ 0.6f, 0.7f, 0.8f };
        float     Density = 0.04f;
        float     HeightBase = 0.0f;
        float     HeightFalloff = 0.15f;
        float     ScatterStrength = 0.6f;
        float     AmbientStrength = 0.15f;
        float     FogStart = 2.0f;
        float     FogEnd = 80.0f;
        int       Steps = 12;
        float     StepJitter = 0.5f;

        // ── VOLUME_DOMAIN_V1 ─────────────────────────────────────────────────
        //
        // UUID de um material de dominio Volume. Vazio (o default) = o fog
        // embutido de sempre, exatamente como antes desta rodada.
        //
        // Com material, o passe troca APENAS o shader: os campos acima
        // continuam valendo e chegam ao grafo como uniforms. Isso e de
        // proposito — Density, FogColor e a queda por altura viram os valores
        // de FALLBACK dos pinos que o autor deixar soltos, entao um material
        // recem-criado nao apaga o ajuste que ja estava na cena.
        //
        // String, e nao ponteiro para shader: o dono desta struct e o
        // PostProcessComponent, que e dado puro e serializavel. Resolver UUID
        // -> shader e tarefa do passe, na camada que pode falar com GL.
        std::string MaterialUUID;
    };

    // Interface abstrata — sem nenhum include de GL/GLFW.
    // A implementação OpenGL fica em opengl/opengl_volumetric_fog_pass.
    class AXE_API VolumetricFogPass
    {
    public:
        virtual ~VolumetricFogPass() = default;

        virtual void Initialize() = 0;
        virtual bool IsInitialized() const = 0;

        // ── VOLUME_SUN_V2 — o sol e a sombra dele DENTRO do volume ───────────
        //
        //  Ate a V1 o fog tinha dois termos de luz: o ambiente (a cor do ceu
        //  espalhada) e o inscattering das point lights. A luz DIRECIONAL nunca
        //  entrou na conta — numa cena iluminada so pelo sol, como a maioria
        //  das cenas de exterior, a nevoa ficava iluminada por um termo chapado
        //  e sem direcao, e por isso lia como veu e nao como volume.
        //
        //  Sao SETTERS, e nao parametros do Execute, pelo mesmo motivo do
        //  PostProcessPass::SetSceneBuffers: quem nao tem sol ou nao tem
        //  cascata simplesmente nao chama, e o termo se desliga sozinho.
        //  A assinatura do SetCascadedShadow e IDENTICA a do MeshRenderer de
        //  proposito — e a mesma informacao, desempacotada do mesmo jeito, e
        //  divergir as duas seria convite a sombra do fog e a da agua
        //  discordarem sobre onde esta a sombra.
        //
        //  `direction` aponta PARA ONDE A LUZ VAI (mesma convencao do
        //  u_LightDirection do lighting pass e do SunDirection do post
        //  process). Intensity 0 desliga o termo.
        virtual void SetSun(const glm::vec3& direction,
            const glm::vec3& color,
            float intensity) = 0;

        virtual void SetCascadedShadow(const CascadedShadowPass* csm,
            const glm::mat4& view) = 0;

        virtual void Execute(
            const GBuffer& gbuffer,
            const VolumetricFogSettings& settings,
            const glm::mat4& invViewProj,
            const glm::vec3& cameraPosition,
            const std::vector<PointLight>& pointLights,
            float                        time,
            uint32_t                     width,
            uint32_t                     height,
            const std::vector<InteriorVolumeData>& interiorVolumes = {},
            const std::vector<ProbeVolumeData>& probeVolumes = {}) = 0;

        // Factory — retorna a implementação correta para a API ativa
        static std::shared_ptr<VolumetricFogPass> Create();
    };

} // namespace axe