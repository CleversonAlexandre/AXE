#pragma once
#include "axe/core/types.hpp"
#include <cmath>   // SKY_OWNS_SKY_V1 — fmod/asin/atan2 do ciclo dia/noite
#include "render_queue.hpp"
#include "axe/scene/scene.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/graphics/renderer/line_renderer.hpp"
#include "axe/graphics/renderer/mesh_renderer.hpp"
#include "axe/graphics/renderer/shadow_map_pass.hpp"
#include "axe/graphics/renderer/scene_height_pass.hpp"   // SCENE_HEIGHT_V1
#include "axe/graphics/renderer/cascaded_shadow_pass.hpp"
#include "axe/graphics/renderer/gbuffer.hpp"
#include "axe/graphics/renderer/probe_bake_pass.hpp"
#include "axe/graphics/renderer/point_shadow_pass.hpp"
#include "axe/graphics/renderer/geometry_pass.hpp"
#include "axe/graphics/renderer/skinning_pass.hpp"
#include "axe/animation/skinned_mesh_instance.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"
#include "axe/graphics/renderer/lighting_pass.hpp"
#include "axe/graphics/renderer/post_process_pass.hpp"   // POSTPROCESS_GBUFFER_V1
#include "axe/lighting/directional_light.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/renderer/skybox_renderer.hpp"
#include "axe/graphics/renderer/outline_renderer.hpp"
#include "axe/graphics/renderer/particle_renderer.hpp"
#include "axe/graphics/renderer/ribbon_renderer.hpp"
#include "axe/renderer/volumetric_fog_pass.hpp"
#include "axe/graphics/renderer/taa_pass.hpp"
#include <unordered_map>

namespace axe
{
    class AXE_API SceneRenderer
    {
    public:
        SceneRenderer();

        // --- API principal: recebe RenderQueue já montada ---
        //
        // NÃO-CONST de propósito: o skin cache roda aqui dentro e ADICIONA
        // os personagens deformados em queue.Meshes, pra que todos os
        // passes seguintes os tratem como mesh estática comum.
        void Render(RenderQueue& queue,
            const glm::mat4& viewProjection,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            uint32_t width, uint32_t height);

        // --- Compat: monta RenderQueue internamente a partir da Scene ---
        // Mantido para não quebrar o ViewportRenderer durante a transição
        void RenderScene(const Scene& scene, const EditorCamera& camera,
            entt::entity selectedEntity);
        void RenderScene(const Scene& scene,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            entt::entity selectedEntity,
            uint32_t width = 0,
            uint32_t height = 0);

        // Força o shader do Lighting Pass a recompilar do zero — ver
        // comentário em LightingPass::RecompileShader (no header base)
        // pro contexto completo do porquê isso existe.
        void RecompileLightingShader()
        {
            if (m_LightingPass) m_LightingPass->RecompileShader();
        }

        void SetEnvironment(const SceneEnvironment* env)
        {
            m_Environment = env;
            m_MeshRenderer.SetEnvironment(env);
        }

        void SetSSAOSettings(const SSAOSettings& s) { m_SSAOSettings = s; }

        void SetFogSettings(const VolumetricFogSettings& s) { m_FogSettings = s; }
        void SetTAASettings(const TAASettings& s) { m_TAASettings = s; }

        // Configura o céu procedural no SkyboxRenderer
        void SetProceduralSky(bool enabled,
            const glm::vec3& sunDir,
            float turbidity,
            float cloudCoverage,
            float cloudSpeed,
            const glm::vec3& cloudColor,
            const glm::vec3& nightColor,
            // ── SKYLIGHT_CHAIN_V1 ─────────────────────────────────────────
            // A COR e a INTENSIDADE do sol, que ate agora nunca chegavam ao
            // ceu. Sem elas o ceu era sempre meio-dia, e o sol so existia
            // como luz direta — "cosmetico" para todo o resto do ambiente.
            //
            // Default (1,1,1) e 1.0 para nao quebrar chamador que ainda nao
            // passe; os chamadores reais passam o valor da luz.
            const glm::vec3& sunColor = glm::vec3(1.0f),
            float sunIntensity = 1.0f)
        {
            if (!m_SkyboxRenderer) return;
            m_SkyboxRenderer->SetProceduralSky(enabled);
            m_SkyboxRenderer->SetSunDirection(sunDir);
            m_SkyboxRenderer->SetTurbidity(turbidity);
            m_SkyboxRenderer->SetCloudCoverage(cloudCoverage);
            m_SkyboxRenderer->SetCloudSpeed(cloudSpeed);
            m_SkyboxRenderer->SetCloudColor(cloudColor);
            m_SkyboxRenderer->SetNightColor(nightColor);
            m_SkyboxRenderer->SetSunColor(sunColor);
            m_SkyboxRenderer->SetSunIntensity(sunIntensity);
        }

        // ── SKYLIGHT_CHAIN_V1 — so a luz que acende o ceu ────────────────────
        //
        // Existe separado do SetProceduralSky por causa do caso "a cena NAO tem
        // luz direcional". Nesse caso nao ha de onde ler ProceduralSky,
        // turbidez, nuvens — esses campos moram no proprio DirectionalLight —,
        // entao chamar o SetProceduralSky completo obrigaria a INVENTAR um
        // estado de ceu, e isso apagaria o HDRI de quem ilumina por arquivo.
        //
        // Este setter toca so no sol: o ceu procedural continua ligado ou
        // desligado como estava, e quem ilumina por HDRI nao e afetado —
        // um HDRI e uma fonte que o usuario colocou de proposito e nao depende
        // de nenhum sol.
        void SetSunLighting(const glm::vec3& sunColor, float sunIntensity)
        {
            if (!m_SkyboxRenderer) return;
            m_SkyboxRenderer->SetSunColor(sunColor);
            m_SkyboxRenderer->SetSunIntensity(sunIntensity);
        }

        // ═════════════════════════════════════════════════════════════════
        //  SKY_OWNS_SKY_V1 — UM CAMINHO SÓ PARA CONFIGURAR O CÉU
        //
        //  Esta lógica existia DUAS VEZES, copiada quase idêntica no
        //  viewport_renderer (editor + play) e no world_renderer (jogo
        //  empacotado). Duas cópias de regra é como o Post Process Volume
        //  divergiu, e aqui seria pior: o céu do editor e o do jogo
        //  passariam a discordar sem erro nenhum aparecer.
        //
        //  Recebe PONTEIROS já resolvidos, e não o registry, de propósito:
        //  assim este header não precisa incluir components.hpp (um hub
        //  pesado, incluído por dezenas de TUs) e a busca dos componentes
        //  fica de fora — ela é trivial e não é onde mora o risco.
        //
        //  nullptr é caso normal, não erro:
        //    sky   == nullptr -> a cena não tem Sky Light; sem dono do céu,
        //                        o procedural fica desligado.
        //    light == nullptr -> não há sol. O céu procedural CONTINUA
        //                        ligado, mas com radiância 0 — ou seja,
        //                        noite. É exatamente isto que faz "apaguei
        //                        o sol" virar escuro em vez de cair no HDRI.
        // ═════════════════════════════════════════════════════════════════
        //  ── SKY_DEADLOCK_FIX_V1 — POR QUE E STATIC E RECEBE O SKYBOX ────
        //
        //  A primeira versao era metodo de instancia e escrevia no
        //  m_SkyboxRenderer do SceneRenderer, saindo cedo se ele fosse nulo.
        //  Isso criou um IMPASSE CIRCULAR:
        //
        //    o ponteiro so e preenchido pelo SetSkyboxRenderer, que os
        //    renderers chamam DENTRO do teste `HasSkybox() ||
        //    IsProceduralSky()`  ->  sem HDRI, esse teste depende de
        //    IsProceduralSky()  ->  que so vira true quando o ApplySkyFrame
        //    roda  ->  que saia cedo porque o ponteiro estava nulo.
        //
        //  Resultado: com o HDRI desligado o ceu procedural NUNCA acendia no
        //  editor — o estado que destrava o teste era justamente o que o
        //  teste impedia de existir.
        //
        //  Recebendo o SkyboxRenderer que o CHAMADOR possui (viewport e world
        //  renderer tem um como membro), a configuracao do ceu deixa de
        //  depender de qualquer ordem: ela sempre acontece, e o teste passa a
        //  ler um estado que ja foi escrito neste frame.
        static void ApplySkyFrame(SkyboxRenderer& skybox,
            SkyLight* sky, DirectionalLight* light, float deltaSeconds)
        {
            // Aplica no skybox recebido — nunca no membro.
            auto configure = [&skybox](bool enabled, const glm::vec3& sunDir,
                float turbidity, float cloudCoverage, float cloudSpeed,
                const glm::vec3& cloudColor, const glm::vec3& nightColor,
                const glm::vec3& sunColor, float sunIntensity)
                {
                    skybox.SetProceduralSky(enabled);
                    skybox.SetSunDirection(sunDir);
                    skybox.SetTurbidity(turbidity);
                    skybox.SetCloudCoverage(cloudCoverage);
                    skybox.SetCloudSpeed(cloudSpeed);
                    skybox.SetCloudColor(cloudColor);
                    skybox.SetNightColor(nightColor);
                    skybox.SetSunColor(sunColor);
                    skybox.SetSunIntensity(sunIntensity);
                };

            if (!sky || !sky->ProceduralSky)
            {
                // Sem céu procedural quem manda é o HDRI do Environment (se
                // houver). O sol ainda é publicado: o HDRI não o usa, mas
                // deixar valor velho aqui seria estado escondido.
                configure(false, { 0, 1, 0 }, 2.5f, 0.4f, 0.015f,
                    { 1, 1, 1 }, { 0.005f, 0.005f, 0.02f },
                    light ? light->Color : glm::vec3(1.0f),
                    light ? light->Intensity : 0.0f);
                return;
            }

            // Direção do sol: do ciclo dia/noite quando ligado, senão da luz.
            // Sem luz e sem ciclo, aponta para cima — a direção não importa
            // porque a radiância vai ser 0 de qualquer forma.
            glm::vec3 sunDir = light
                ? glm::normalize(-light->Direction)
                : glm::vec3(0.0f, 1.0f, 0.0f);

            if (sky->TimeOfDayEnabled)
            {
                float dt = deltaSeconds;
                if (dt < 0.0f || dt > 0.5f) dt = 0.016f;

                sky->Hour = std::fmod(sky->Hour + dt * (sky->DaySpeed / 3600.0f), 24.0f);
                if (sky->Hour < 0.0f) sky->Hour += 24.0f;

                // Ângulo horário: 0 ao meio-dia, pi/2 ao pôr do sol.
                const float kPi = 3.14159265f;
                float hourAngle = (sky->Hour - 12.0f) * (kPi / 12.0f);
                float latRad = sky->SunLatitude * (kPi / 180.0f);
                float elevation = std::asin(std::cos(latRad) * std::cos(hourAngle));
                float azimuth = std::atan2(std::sin(hourAngle),
                    std::cos(hourAngle) * std::sin(latRad));

                sunDir = glm::normalize(glm::vec3(
                    std::cos(elevation) * std::sin(azimuth),
                    std::sin(elevation),
                    std::cos(elevation) * std::cos(azimuth)));

                // O relógio é do céu, mas quem ILUMINA continua sendo o sol:
                // o ciclo escreve na luz direcional quando existe uma. Sem
                // luz na cena, o relógio segue correndo e o céu fica de noite
                // o tempo todo — que é o certo, já que não há sol.
                if (light)
                {
                    light->Direction = -sunDir;
                    float elev = glm::clamp(sunDir.y, 0.0f, 1.0f);
                    float sunsetF = glm::clamp((elev - 0.0f) / 0.3f, 0.0f, 1.0f);
                    sunsetF = sunsetF * sunsetF * (3.0f - 2.0f * sunsetF); // smoothstep
                    light->Color = glm::mix(
                        glm::vec3(1.0f, 0.42f, 0.08f),
                        glm::vec3(1.0f, 0.93f, 0.88f), sunsetF);
                    light->Intensity = elev * 8.0f;
                }
            }

            configure(true, sunDir,
                sky->Turbidity, sky->CloudCoverage, sky->CloudSpeed,
                sky->CloudColor, sky->NightColor,
                light ? light->Color : glm::vec3(1.0f),
                light ? light->Intensity : 0.0f);   // sem sol => céu de noite
        }
        // Deve ser chamado ANTES de Render() a cada frame pra aplicar jitter.
        // Retorna a projection matrix com jitter aplicado.
        glm::mat4 BeginTAAFrame(const glm::mat4& projection,
            const glm::mat4& viewProjection,
            uint32_t width, uint32_t height);

        // Acesso ao depth do GBuffer para o TAA pass no viewport renderer
        uint32_t GetGBufferDepthID() const { return m_GBuffer.GetDepthID(); }

        // Acesso ao GBuffer completo para o SSR pass
        const GBuffer& GetGBuffer() const { return m_GBuffer; }

        // POSTPROCESS_GBUFFER_V1 — entrega ao passe de post process as texturas
        // que o material de efeito pode ler. Fica aqui porque o G-Buffer e
        // deste renderer; quem chama e o dono do passe (viewport/world).
        void PublishSceneBuffersTo(PostProcessPass& pass) const
        {
            pass.SetSceneBuffers(m_GBuffer.GetPositionID(),
                m_GBuffer.GetNormalID(),
                m_GBuffer.GetPBRID());

            // POSTPROCESS_SKY_V1 — junto, e nao num segundo Publish: quem
            // chama nao tem por que saber que sao duas coisas, e uma das duas
            // esquecida seria um efeito meio quebrado em vez de um erro.
            pass.SetCameraParams(m_PostProcessCamera);
        }
        void SetTargetFramebuffer(uint32_t fboID) { m_TargetFBO = fboID; }
        uint32_t GetTargetFBO() const { return m_TargetFBO; }
        bool IsDeferredEnabled() const { return m_DeferredEnabled; }
        bool IsDeferredSupported() const { return m_DeferredSupported; }
        void SetDeferredEnabled(bool enabled) { m_DeferredEnabled = enabled; }
        void SetDeferredSupported(bool supported) { m_DeferredSupported = supported; }

        MeshRenderer& GetMeshRenderer() { return m_MeshRenderer; }

        void SetSkyboxRenderer(SkyboxRenderer* skybox,
            const glm::mat4& view,
            const glm::mat4& projection)
        {
            m_SkyboxRenderer = skybox;
            m_SkyboxView = view;
            m_SkyboxProjection = projection;
        }

        void InitializeDeferredPasses(uint32_t width, uint32_t height);
        void SetEnvironment(const SceneEnvironment* env, bool dummy) {} // overload compat

    private:
        // ── Skin Cache ────────────────────────────────────────────────────
        // Roda o compute de skinning em cada SkinnedDrawCall e injeta o
        // resultado em queue.Meshes como MeshDrawCall normal.
        void ResolveSkinnedMeshes(RenderQueue& queue);

        std::shared_ptr<SkinningPass> m_SkinningPass;

        // Buffers de GPU por personagem, chaveados pelo ID da entidade.
        // Persiste entre frames: realocar o VBO de saída todo frame seria
        // um stall de driver garantido.
        std::unordered_map<uint32_t, std::shared_ptr<SkinnedMeshInstance>> m_SkinCache;

        CubeRenderer    m_CubeRenderer;
        MeshRenderer    m_MeshRenderer;
        LineRenderer    m_LineRenderer;
        OutlineRenderer  m_OutlineRenderer;
        ParticleRenderer m_ParticleRenderer;
        RibbonRenderer   m_RibbonRenderer;
        std::shared_ptr<VolumetricFogPass> m_FogPass;
        VolumetricFogSettings m_FogSettings;
        std::shared_ptr<TAAPass> m_TAAPass;
        TAASettings              m_TAASettings;

        // ── SCENE_HEIGHT_V1 ──────────────────────────────────────────────
        //
        //  Roda uma vez por frame, ao lado do passe de sombra e pelo mesmo
        //  motivo: e uma render auxiliar da cena inteira, que os dois caminhos
        //  (deferred e forward) consomem depois.
        //
        //  A extensao e a resolucao ficam aqui, e nao numa struct de settings
        //  do editor, porque nesta rodada nada ainda os edita — quando forem
        //  expostos, o lugar natural e o PostProcessComponent, junto de SSAO e
        //  Fog, que e por onde o Inspector ja conversa com o renderer.
        std::shared_ptr<SceneHeightPass> m_SceneHeightPass;

        // ── SCENE_HEIGHT_V6 — qualidade do mapa, e nao ajuste de cena ────────
        //
        //  Os dois juntos dao o TAMANHO DO TEXEL, que e a granulacao da linha
        //  da praia: 24 m a 1024 da 2,3 cm. Os 60 m antigos davam 5,9 cm, e uma
        //  faixa de espuma de meio metro cabia em nove texels — dai o serrilhado.
        //
        //  Ficam aqui, e nao num painel, pelo mesmo motivo que a resolucao da
        //  shadow map fica: e escolha de renderer, nao de cena. Quando a engine
        //  tiver um lugar de Qualidade de Render, os dois vao junto com ela.
        static constexpr float    k_SceneHeightExtent = 24.0f;
        static constexpr uint32_t k_SceneHeightResolution = 1024;

        std::shared_ptr<ShadowMapPass> m_ShadowPass;
        std::shared_ptr<CascadedShadowPass> m_CSMPass;
        bool m_UseCSM = true; // usa CSM ao invés do shadow map simples
        GBuffer                        m_GBuffer;
        std::shared_ptr<GeometryPass>  m_GeometryPass;
        std::shared_ptr<SSAOPass>      m_SSAOPass;
        std::shared_ptr<LightingPass>  m_LightingPass;

        // Bake de Light Probes — criado sob demanda (lazy) no primeiro
        // pedido de bake; a maioria das cenas nunca aloca os recursos.
        std::shared_ptr<ProbeBakePass> m_ProbeBakePass;

        // Sombras de Point/Spot Light — lazy: só aloca o cube map array
        // quando a primeira luz com CastShadows aparece na cena.
        std::shared_ptr<PointShadowPass> m_PointShadowPass;

        SSAOSettings            m_SSAOSettings;
        const SceneEnvironment* m_Environment = nullptr;
        uint32_t                m_TargetFBO = 0;
        bool                    m_DeferredEnabled = false;
        bool                    m_DeferredSupported = true;
        uint32_t                m_Width = 0;
        uint32_t                m_Height = 0;

        SkyboxRenderer* m_SkyboxRenderer = nullptr;
        glm::mat4       m_SkyboxView{ 1.0f };
        glm::mat4       m_SkyboxProjection{ 1.0f };

        // POSTPROCESS_SKY_V1 — capturado no RenderDeferred, que e onde a view,
        // a projection, a posicao da camera e a luz direcional estao todas na
        // mao ao mesmo tempo. Guardar aqui evita passar seis parametros pelo
        // viewport_renderer e pelo world_renderer.
        PostProcessPass::CameraParams m_PostProcessCamera;

        // Passes internos
        void RenderShadowPass(const RenderQueue& queue,
            const glm::vec3& cameraPosition,
            const glm::mat4& view,
            const glm::mat4& projection);

        // SCENE_HEIGHT_V1 — a render ortografica de topo.
        void RenderSceneHeightPass(const RenderQueue& queue,
            const glm::vec3& cameraPosition);
        void RenderForward(const RenderQueue& queue,
            const glm::mat4& viewProjection,
            const glm::mat4& view,
            const glm::vec3& cameraPosition);
        void RenderDeferred(const RenderQueue& queue,
            const glm::mat4& viewProjection,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            uint32_t width, uint32_t height);

        // Compat helpers — usados pelos RenderScene legados
        void RenderShadowPassLegacy(const Scene& scene, const DirectionalLight* light,
            const glm::vec3& cameraPosition = glm::vec3(0.0f));
        void RenderEntity(const Scene& scene, entt::entity entity,
            const glm::mat4& parentTransform,
            entt::entity selectedEntity,
            const DirectionalLight* light);
        void GeometryPassEntity(const Scene& scene, entt::entity entity);
    };
}