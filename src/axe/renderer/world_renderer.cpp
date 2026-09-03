#include "axe/renderer/world_renderer.hpp"

#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/audio/audio_engine.hpp"
#include "axe/log/log.hpp"

#include <cmath>
#include <algorithm>

// world_renderer.cpp — sem includes diretos de OpenGL, e sem imgui.
//
// A ausencia de imgui aqui nao e coincidencia: e a razao de o arquivo
// existir. Um include de imgui neste .cpp reabre o bloqueio que o SR2
// fechou.

namespace
{
    // Copia do helper do viewport_renderer.cpp.
    //
    // Duplicado de proposito, e nao promovido a header compartilhado: a
    // regra do projeto e promover so depois de dois usos REAIS, e este e o
    // segundo — mas os dois vao convergir quando o caminho de editor passar
    // a delegar (ver notas do SR2). Promover agora criaria um header que
    // seria desfeito em seguida.
    float WR_Smoothstep(float edge0, float edge1, float x)
    {
        float t = std::max(0.f, std::min(1.f, (x - edge0) / (edge1 - edge0)));
        return t * t * (3.f - 2.f * t);
    }
}

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    void WorldRenderer::Initialize()
    {
        m_SceneRenderer = std::make_unique<SceneRenderer>();
        m_SkyboxRenderer.Initialize();

        FramebufferSpecification hdrSpec;
        hdrSpec.Width = 1280;
        hdrSpec.Height = 720;
        hdrSpec.Attachments = {
            FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::DEPTH32F,
        };
        m_HDRFramebuffer = Framebuffer::Create(hdrSpec);

        m_PostProcess = PostProcessPass::Create();
        m_PostProcess->Initialize(1280, 720);

        m_SceneRenderer->InitializeDeferredPasses(1280, 720);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void WorldRenderer::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0) return;
        if (m_HDRFramebuffer)
            m_HDRFramebuffer->Resize(width, height);
        if (m_PostProcess && m_PostProcess->IsInitialized())
            m_PostProcess->Resize(width, height);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void WorldRenderer::SyncEnvironment(Scene& scene, SceneEnvironment* env,
        float timeSeconds)
    {
        auto& registry = scene.GetRegistry();

        // ANTES de bindar o framebuffer: LoadHDRI muda viewport e FBO
        // internamente, e precisa rodar com estado limpo.
        for (auto entity : registry.view<EnvironmentComponent>())
        {
            auto& ec = registry.get<EnvironmentComponent>(entity);
            if (env)
            {
                env->SkyboxRotation = ec.SkyboxRotation;

                // SKY_OFF_V1 — ver o comentario no viewport_renderer.
                if (!ec.UseHDRI)
                {
                    env->Skybox.reset();
                    env->SkyboxPath.clear();
                }
                else if (!ec.HDRIPath.empty() && ec.HDRIPath != env->SkyboxPath)
                    env->LoadHDRI(ec.HDRIPath);
            }
            break;
        }

        // ── Ceu Procedural + Time of Day — o dono e o SKY LIGHT ───────────
        // SKY_OWNS_SKY_V1 — ver o comentário equivalente no viewport_renderer.
        SkyLight* skyData = nullptr;
        for (auto se : registry.view<SkyLightComponent>())
        {
            auto& sc = registry.get<SkyLightComponent>(se);
            if (sc.Data) { skyData = sc.Data.get(); break; }
        }

        DirectionalLight* sunData = nullptr;
        for (auto le : registry.view<LightComponent>())
        {
            auto& lc = registry.get<LightComponent>(le);
            if (lc.Data) { sunData = lc.Data.get(); break; }
        }

        // SKY_DEADLOCK_FIX_V1 — ver o comentario no viewport_renderer.
        SceneRenderer::ApplySkyFrame(m_SkyboxRenderer, skyData, sunData,
            timeSeconds - m_LastTimeSeconds);

        m_LastTimeSeconds = timeSeconds;
    }

    // ─────────────────────────────────────────────────────────────────────────
    void WorldRenderer::RenderToFramebuffer(Framebuffer& target,
        const FrameParams& params)
    {
        RenderInternal(&target, params);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void WorldRenderer::RenderToScreen(const FrameParams& params)
    {
        RenderInternal(nullptr, params);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void WorldRenderer::RenderInternal(Framebuffer* target, const FrameParams& params)
    {
        if (!m_HDRFramebuffer || !m_PostProcess || !m_SceneRenderer)
        {
            AXE_CORE_ERROR("WorldRenderer: Initialize() nao foi chamado.");
            return;
        }

        const std::uint32_t width = params.Width;
        const std::uint32_t height = params.Height;
        if (width == 0 || height == 0) return;

        // ── 1. Resize do HDR e do post-process ───────────────────────────
        auto& hdrSpec = m_HDRFramebuffer->GetSpecification();
        if (hdrSpec.Width != width || hdrSpec.Height != height)
            m_HDRFramebuffer->Resize(width, height);

        if (!m_PostProcess->IsInitialized())
            m_PostProcess->Initialize(width, height);
        else
            m_PostProcess->Resize(width, height);

        // ── 2. Alvo e modo do SceneRenderer ──────────────────────────────
        //
        // Deferred sempre ligado: nao existe "preview mode" no jogo. Era o
        // unico ponto onde o ramo de GameCamera divergia do de editor.
        m_SceneRenderer->SetTargetFramebuffer(m_HDRFramebuffer->GetRendererID());
        m_SceneRenderer->SetDeferredEnabled(true);
        m_SceneRenderer->SetDeferredSupported(true);

        // ── 3. Environment / ceu procedural (antes do bind) ──────────────
        if (params.WorldScene)
            SyncEnvironment(*params.WorldScene, params.Environment, params.TimeSeconds);

        // ── 4. Binda o HDR e limpa ───────────────────────────────────────
        m_HDRFramebuffer->Bind();
        RenderCommand::SetColorWrite(true);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetViewport(0, 0, width, height);
        RenderCommand::SetClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        RenderCommand::Clear();

        // ── 5. PostProcessComponent da cena ──────────────────────────────
        if (params.WorldScene)
        {
            auto& registry = params.WorldScene->GetRegistry();
            for (auto entity : registry.view<PostProcessComponent>())
            {
                auto& pp = registry.get<PostProcessComponent>(entity);
                m_PostProcessSettings = pp.Settings;
                m_SceneRenderer->SetSSAOSettings(pp.SSAO);
                m_SceneRenderer->SetFogSettings(pp.Settings.Fog);
                m_TAASettings = pp.Settings.TAA;
                m_SSRSettings = pp.Settings.SSR;
                break;
            }
        }

        if (params.Environment)
            m_SceneRenderer->SetEnvironment(params.Environment);

        // ── 6. Skybox ────────────────────────────────────────────────────
        //
        // GetSkyboxView remove a translacao da view (o ceu fica
        // "infinitamente distante", nao anda junto com a camera) e aplica a
        // rotacao configurada.
        // SKYGATE_FIX_V1 — ver o comentario no viewport_renderer. Este e o
        // caminho do JOGO EMPACOTADO: sem esta correcao, um jogo que usa so o
        // ceu procedural rodava com o fundo preto.
        if (params.Environment &&
            (params.Environment->HasSkybox() || m_SkyboxRenderer.IsProceduralSky()))
        {
            if (params.Environment->HasSkybox())
                m_SkyboxRenderer.SetCubemap(params.Environment->Skybox);
            m_SceneRenderer->SetSkyboxRenderer(
                &m_SkyboxRenderer,
                params.Environment->GetSkyboxView(params.View),
                params.Projection);
        }
        else
        {
            m_SceneRenderer->SetSkyboxRenderer(nullptr, {}, {});
        }

        // ── 7. Cena ──────────────────────────────────────────────────────
        //
        // ATENCAO A ESTA SEPARACAO — e a unica sutileza real deste arquivo.
        //
        // O jitter do TAA vale APENAS para o RenderScene. SSR, TAA resolve e
        // visualizacao de som usam a projecao LIMPA. No ViewportRenderer isso
        // era invisivel porque cada bloco recalculava
        // `GetProjectionMatrix(aspect)` do zero, sempre sem jitter; aqui, com
        // a matriz vindo pronta por parametro, reaproveitar a variavel
        // jitterada seria facil — e daria TAA com invVP errado (ghosting) e
        // SSR com reflexo tremendo.
        const glm::mat4 view = params.View;
        const glm::mat4 projClean = params.Projection;
        glm::mat4 projJittered = projClean;

        if (params.WorldScene)
        {
            // TAA: jitter na projection ANTES do render.
            if (m_TAASettings.Enabled)
            {
                if (!m_TAAPass)
                {
                    m_TAAPass = TAAPass::Create();
                    m_TAAPass->Initialize(width, height);
                }
                m_TAAPass->BeginFrame(projClean * view);
                projJittered = m_SceneRenderer->BeginTAAFrame(
                    projClean, projClean * view, width, height);
            }

            // selectedEntity = entt::null: outline de selecao e do editor.
            m_SceneRenderer->RenderScene(
                *params.WorldScene,
                view, projJittered,
                params.EyePosition,
                entt::null,
                width, height);
        }

        m_HDRFramebuffer->Unbind();

        std::uint32_t finalColorID = m_HDRFramebuffer->GetColorAttachmentRendererID();

        // ── 8. SSR ───────────────────────────────────────────────────────
        //
        // Antes do TAA, para ser estabilizado por ele. Precisa do GBuffer.
        if (m_SSRSettings.Enabled)
        {
            if (!m_SSRPass)
            {
                m_SSRPass = SSRPass::Create();
                m_SSRPass->Initialize(width, height);
            }
            else
            {
                m_SSRPass->Resize(width, height);
            }

            if (m_SSRPass && m_SSRPass->IsInitialized())
            {
                std::uint32_t ssrResult = m_SSRPass->Execute(
                    m_SceneRenderer->GetGBuffer(), finalColorID,
                    projClean, view, m_SSRSettings, width, height);
                if (ssrResult != 0) finalColorID = ssrResult;
            }
        }

        // ── 9. TAA resolve ───────────────────────────────────────────────
        if (m_TAASettings.Enabled)
        {
            if (!m_TAAPass)
            {
                m_TAAPass = TAAPass::Create();
                m_TAAPass->Initialize(width, height);
            }
            else
            {
                m_TAAPass->Resize(width, height);
            }

            if (m_TAAPass && m_TAAPass->IsInitialized())
            {
                const glm::mat4 fullVP = projClean * view;
                const glm::mat4 invVP = glm::inverse(fullVP);
                const glm::vec2 jitter = m_TAAPass->GetCurrentJitter();
                const glm::mat4 prevVP = m_TAAPass->GetPrevViewProj();

                const std::uint32_t depthID = m_SceneRenderer->GetGBufferDepthID();

                std::uint32_t resolved = m_TAAPass->Execute(
                    finalColorID, depthID,
                    invVP, prevVP, jitter,
                    m_TAASettings, width, height);
                if (resolved != 0) finalColorID = resolved;
            }
        }

        // ── 10. Post-process, ja no framebuffer final ────────────────────
        // Alvo nulo = tela. O `Unbind` de qualquer framebuffer binda o 0, e e
        // isso que o jogo quer; usamos o HDR so por ser o que esta a mao.
        if (target)
            target->Bind();
        else
            m_HDRFramebuffer->Unbind();

        RenderCommand::SetViewport(0, 0, width, height);
        // POSTPROCESS_GBUFFER_V1 / _SKY_V1 — entrega ao passe, ANTES do Execute,
        // o G-Buffer (normal, posicao, shading model) e os dados de camera e
        // sol. Sem isto o material de efeito so enxerga a cor.
        if (m_SceneRenderer) m_SceneRenderer->PublishSceneBuffersTo(*m_PostProcess);

        m_PostProcess->Execute(finalColorID, m_PostProcessSettings);

        // ── 11. Visualizacao de som ──────────────────────────────────────
        //
        // Acessibilidade, nao gizmo: existe PRA aparecer no jogo. Depth
        // desligado de proposito — o pulso tem que ser visivel ATRAVES da
        // parede, que e o caso de uso (saber do inimigo do outro lado).
        if (params.ShowSoundVisualization)
        {
            RenderCommand::SetDepthTest(false);
            RenderCommand::SetBlend(true);
            RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha,
                RendererAPI::BlendFactor::OneMinusSrcAlpha);

            m_SoundVisualization.Render(AudioEngine::GetActiveSounds(),
                view, projClean, params.EyePosition);

            RenderCommand::SetBlend(false);
            RenderCommand::SetDepthTest(true);
        }

        // ── AQUI entra o HUD, quando existir ─────────────────────────────
        //
        // Depois do post-process e ja em espaco de tela, no framebuffer
        // final. Nao antes: o HUD nao deve ser afetado por tone mapping,
        // bloom ou TAA.

        // Na tela nao ha o que desligar — ja estamos no framebuffer 0.
        if (target)
            target->Unbind();
    }

} // namespace axe