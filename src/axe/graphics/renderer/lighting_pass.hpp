#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "gbuffer.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/lighting/interior_volume.hpp"
#include "axe/lighting/probe_volume.hpp"
#include "axe/lighting/reflection_probe.hpp"
#include "axe/scene/scene_environment.hpp"
#include "cascaded_shadow_pass.hpp"
#include <memory>
#include <cstdint>
#include <vector>

namespace axe
{
    class AXE_API LightingPass
    {
    public:
        virtual ~LightingPass() = default;

        virtual void Initialize() = 0;
        virtual void RecompileShader() {}

        virtual void Execute(const GBuffer& gbuffer,
            uint32_t ssaoTextureID,
            uint32_t shadowMapID,
            const glm::mat4& lightSpaceMatrix,
            const CascadedShadowPass* csm,
            const glm::mat4& view,
            const glm::vec3& cameraPosition,
            const DirectionalLight* light,
            const SceneEnvironment* environment,
            const std::vector<PointLight>& pointLights = {},
            const std::vector<InteriorVolumeData>& interiorVolumes = {},
            const std::vector<ProbeVolumeData>& probeVolumes = {},
            const std::vector<ReflectionProbeData>& reflectionProbes = {},
            uint32_t pointShadowArrayID = 0) = 0;

        virtual bool IsInitialized() const = 0;
        virtual void SetSSAODebug(bool debug) {}

        // ── SKY_LIGHT_V1 ─────────────────────────────────────────────────
        //
        // Entra por SETTER e nao por parametro do Execute — mesmo precedente
        // do SetSSAODebug/SetSSAOSettings. O Execute ja tem 14 parametros e
        // e chamado de mais de um lugar; cada novo parametro ali e mais uma
        // chance de um chamador ficar para tras em silencio.
        //
        // Copiado por VALOR de proposito: o SkyLight vive num shared_ptr do
        // componente, e guardar ponteiro entre frames seria guardar algo que
        // pode morrer com a entidade.
        //
        // present=false significa "a cena nao tem Sky Light" — o pass volta
        // aos campos legados do DirectionalLight em vez de escurecer.
        virtual void SetSkyLight(const SkyLight& sky, bool present) {}

        // ── CONTACT_SHADOW_V1 ────────────────────────────────────────────
        //
        // O Execute recebe a VIEW mas nunca recebeu a PROJECTION — ate agora
        // nenhum passe do lighting precisava projetar nada de volta na tela.
        // O contact shadow precisa: ele marcha no mundo e pergunta ao
        // G-Buffer o que esta visivel em cada passo.
        //
        // Entra por setter pelo mesmo motivo do SetSkyLight: o Execute ja tem
        // 14 parametros e e chamado de mais de um lugar.
        virtual void SetProjection(const glm::mat4& projection) {}

        static std::shared_ptr<LightingPass> Create();
    };
}