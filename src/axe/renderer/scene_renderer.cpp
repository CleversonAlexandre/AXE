#include "scene_renderer.hpp"
#include "scene_collector.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include <chrono>
#include "axe/scene/components.hpp"
#include "axe/graphics/renderer/outline_renderer.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/renderer/skybox_renderer.hpp"
#include "axe/log/log.hpp"
#include "axe/material/material.hpp"
#include "axe/core/time.hpp"
#include <algorithm>
#include <limits>

namespace axe
{
    namespace
    {
        // ═════════════════════════════════════════════════════════════════════
        //  TRANSLUCENT_NOCAST_V1 — translucido nao entra no shadow map
        //
        //  ── O DEFEITO ───────────────────────────────────────────────────────
        //
        //  Os dois passes de sombra desenhavam a fila INTEIRA, sem olhar o
        //  material — ao contrario do scene height pass e do geometry pass, que
        //  sempre filtraram translucido. A agua ia para o shadow map junto com
        //  o resto.
        //
        //  Isso passou despercebido enquanto o passe forward nao LIA sombra
        //  nenhuma (ver FORWARD_SHADOW_V1). No instante em que passou a ler, o
        //  defeito virou a coisa mais visivel da tela:
        //
        //    o CASTER e a malha SEM deslocamento — o shadow pass tem vertex
        //    shader proprio, fixo, que nao conhece World Position Offset;
        //    o RECEIVER e a mesma malha COM deslocamento.
        //
        //  Entao toda crista que sobe fica na frente do plano chapado do mapa,
        //  e todo vale que desce fica ATRAS dele — em sombra de si mesmo. O
        //  resultado sao manchas escuras grandes, de borda poligonal e
        //  pontilhada, que mudam de forma quando o sol gira. Exatamente o que o
        //  Clever fotografou.
        //
        //  ── POR QUE FILTRAR E A RESPOSTA CERTA, E NAO UM REMENDO ────────────
        //
        //  Superficie translucida nao projeta sombra opaca: seria errado a agua
        //  escurecer o fundo do mar como se fosse pedra. Sombra de translucido
        //  e um recurso separado e mais caro (mapa colorido), e nenhuma engine
        //  o liga por padrao. Filtrar aqui alinha os tres passes que consomem a
        //  fila com o mesmo criterio que os outros dois ja usavam.
        //
        //  Masked continua projetando: `IsTransparent` e false para ele (ver
        //  MaterialCompiler), e recorte de folhagem PRECISA de sombra.
        //
        //  ── CORRECAO DE UMA AFIRMACAO MINHA ────────────────────────────────
        //
        //  Na entrega do WPO_V1 ficou escrito que a sombra nao acompanhar o
        //  deslocamento era "irrelevante para agua, porque translucido nao
        //  projeta sombra". A premissa estava errada: projetava sim, porque
        //  ninguem filtrava. A limitacao documentada la continua valendo para
        //  malha OPACA com WPO (folhagem com vento), que segue projetando a
        //  silhueta parada.
        // ═════════════════════════════════════════════════════════════════════
        bool ShadowCasterSkipped(const MeshDrawCall& dc)
        {
            return dc.Material && dc.Material->IsTransparent;
        }
    }

    SceneRenderer::SceneRenderer() {}

    glm::mat4 SceneRenderer::BeginTAAFrame(const glm::mat4& projection,
        const glm::mat4& viewProjection,
        uint32_t width, uint32_t height)
    {
        if (!m_TAASettings.Enabled) return projection;

        if (!m_TAAPass) m_TAAPass = TAAPass::Create();
        if (!m_TAAPass->IsInitialized()) m_TAAPass->Initialize(width, height);

        m_TAAPass->BeginFrame(viewProjection);

        // Aplica offset de jitter na coluna [2] da projection matrix
        glm::vec2 jitter = m_TAAPass->GetCurrentJitter();
        glm::mat4 jitteredProj = projection;
        jitteredProj[2][0] += jitter.x * 2.f / (float)width;
        jitteredProj[2][1] += jitter.y * 2.f / (float)height;
        return jitteredProj;
    }

    // =============================================================================
    // API principal — recebe RenderQueue já montada, sem saber nada de Scene/entt
    // =============================================================================

    // =============================================================================
    // Skin Cache — o coração do sistema de animação no renderer.
    //
    // Roda ANTES de qualquer pass. Para cada personagem:
    //   1. pega (ou cria) os buffers de GPU daquela entidade
    //   2. sobe a palette de bones deste frame
    //   3. despacha o compute, que escreve os vértices deformados
    //   4. empurra um MeshDrawCall NORMAL apontando pra mesh deformada
    //
    // Depois do passo 4, o personagem é indistinguível de uma parede pro
    // resto do renderer. Sombra, G-Buffer, forward, outline e picking o
    // pegam de graça, sem shader novo e sem o MaterialCompiler mudar.
    // =============================================================================
    void SceneRenderer::ResolveSkinnedMeshes(RenderQueue& queue)
    {
        if (queue.SkinnedMeshes.empty())
            return;

        if (!m_SkinningPass)
            m_SkinningPass = SkinningPass::Create();

        const bool computeOK = m_SkinningPass->IsInitialized() || m_SkinningPass->Initialize();

        for (const auto& sdc : queue.SkinnedMeshes)
        {
            if (!sdc.Mesh)
                continue;

            auto it = m_SkinCache.find(sdc.InstanceID);
            if (it == m_SkinCache.end() || it->second->GetSource() != sdc.Mesh)
            {
                // Entidade nova, ou trocou de asset em runtime — realoca.
                auto shared = std::const_pointer_cast<SkinnedMesh>(
                    sdc.Mesh->shared_from_this());

                it = m_SkinCache.insert_or_assign(sdc.InstanceID,
                    std::make_shared<SkinnedMeshInstance>(shared)).first;
            }

            auto& instance = it->second;

            const bool hasPalette = sdc.BonePalette && !sdc.BonePalette->empty();

            if (computeOK && hasPalette)
            {
                instance->UploadPalette(*sdc.BonePalette);

                m_SkinningPass->Execute(*sdc.Mesh,
                    *instance->GetBoneBuffer(),
                    *instance->GetOutputBuffer(),
                    instance->GetVertexCount());
            }
            else if (!instance->HasPalette())
            {
                // Sem compute (GPU velha) ou sem palette ainda: o buffer de
                // saída está com lixo. Pular o draw call é melhor do que
                // desenhar triângulos aleatórios pela cena.
                continue;
            }

            MeshDrawCall dc;
            dc.Mesh = &instance->GetDeformedMesh();
            dc.Material = sdc.Material;
            dc.Transform = sdc.Transform;
            dc.Selected = sdc.Selected;

            queue.Meshes.push_back(dc);

            // Outline: registrado SÓ agora, com a mesh já deformada. Se
            // fosse registrado no SceneCollector, o contorno seguiria a
            // T-pose enquanto o corpo anima.
            if (dc.Selected)
            {
                queue.SelectedID = sdc.InstanceID;
                queue.SelectedMesh = dc.Mesh;
                queue.SelectedTransform = dc.Transform;
            }
        }

        // UMA barreira pra todos os personagens — ver SkinningPass::Flush.
        // Depois disto, os vértices deformados estão visíveis pros draws.
        if (computeOK)
            m_SkinningPass->Flush();
    }

    void SceneRenderer::Render(RenderQueue& queue,
        const glm::mat4& viewProjection,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        uint32_t width, uint32_t height)
    {
        // PRIMEIRA COISA do frame: deformar os personagens. Tudo abaixo
        // depende de queue.Meshes já conter as meshes skinned resolvidas.
        ResolveSkinnedMeshes(queue);

        // Acumula tempo pra u_Time no shader de material de partícula.
        // std::chrono aqui é só pra não precisar invadir a assinatura de
        // Render() com um parâmetro extra — zero dependência de GL.
        {
            using clock = std::chrono::steady_clock;
            static auto s_Last = clock::now();
            auto now = clock::now();
            float dt = std::chrono::duration<float>(now - s_Last).count();
            s_Last = now;
            dt = glm::clamp(dt, 0.0f, 0.1f); // clamp pra evitar salto no primeiro frame
            m_ParticleRenderer.Tick(dt);
            m_RibbonRenderer.Tick(dt);
            if (m_SkyboxRenderer) m_SkyboxRenderer->Tick(dt);
        }

        // ── SKY_IBL_V1 — o ceu procedural vira luz de ambiente ───────────────
        //
        // AQUI, e nao mais para baixo, por uma razao dura: a captura troca o
        // framebuffer e o viewport (renderiza 6 faces num FBO proprio). Rodar
        // isso depois de qualquer bind do frame deixaria o resto do frame
        // desenhando no alvo errado — e a mesma razao pela qual o LoadHDRI ja
        // e chamado antes de tudo, la no ViewportRenderer.
        //
        // Nao custa por frame: o UpdateSkyIBL so trabalha quando o sol, ou um
        // parametro do ceu, mudou o bastante. Ver SkyboxRenderer::
        // SkyIBLNeedsRebake.
        if (m_SkyboxRenderer && m_Environment)
        {
            m_SkyboxRenderer->UpdateSkyIBL();
            m_Environment->SkyIBL = m_SkyboxRenderer->GetSkyIBL();
        }

        RenderShadowPass(queue, cameraPosition, view, projection);

        // SCENE_HEIGHT_V1 — aqui, e nao dentro do RenderDeferred: e uma render
        // auxiliar da cena inteira, e os dois caminhos abaixo a consomem.
        RenderSceneHeightPass(queue, cameraPosition);

        if (m_DeferredEnabled && m_DeferredSupported && m_TargetFBO != 0)
            RenderDeferred(queue, viewProjection, view, projection, cameraPosition, width, height);
        else
            RenderForward(queue, viewProjection, view, cameraPosition);
    }

    // =============================================================================
    // Shadow pass — opera sobre RenderQueue
    // =============================================================================

    // =============================================================================
    // SCENE_HEIGHT_V1 — mapa de topo da cena
    //
    //  Uma render ortografica de cima para baixo, centrada na camera, gravando
    //  a altura da geometria; depois um jump flood transforma isso em "onde
    //  esta a geometria mais proxima". Ver scene_height_pass.hpp para o porque.
    //
    //  Transparentes ficam de fora, pela mesma razao do bake de probe: agua nao
    //  e margem de si mesma, e vidro nao e chao.
    // =============================================================================
    void SceneRenderer::RenderSceneHeightPass(const RenderQueue& queue,
        const glm::vec3& cameraPosition)
    {
        // ── SCENE_HEIGHT_V6 — quem pede o mapa e a propria superficie ────────
        //
        //  Varre a fila atras dos draw calls cujo material usa o node Scene
        //  Height. Se nao ha nenhum, o passe NAO RODA — nao ha o que medir, e
        //  uma cena sem agua nao paga por isto.
        //
        //  A altura da fatia sai do transform desses draw calls: e a altura da
        //  propria superficie que vai ler o mapa. Nao ha nada para configurar,
        //  e nao ha campo de "agua" em painel nenhum.
        //
        //  Com mais de uma superficie em alturas diferentes fica valendo a mais
        //  BAIXA, e as outras leem um mapa fatiado no lugar errado. Um mapa por
        //  altura distinta e a evolucao natural daqui — mas custa um jump flood
        //  por altura, entao so quando alguma cena precisar de verdade.
        bool  wantsHeight = false;
        float sliceY = 0.0f;

        for (const auto& dc : queue.Meshes)
        {
            if (!dc.Material || !dc.Material->UsesSceneHeight) continue;

            const float y = dc.Transform[3].y;
            if (!wantsHeight || y < sliceY) sliceY = y;
            wantsHeight = true;
        }

        if (!wantsHeight)
        {
            // Sem zerar as fontes, o MeshRenderer continuaria mandando a
            // textura do ultimo frame em que o passe rodou.
            m_MeshRenderer.SetSceneHeightSource(0, 0, glm::mat4(1.0f));
            return;
        }

        if (!m_SceneHeightPass) m_SceneHeightPass = SceneHeightPass::Create();
        if (!m_SceneHeightPass) return;

        if (!m_SceneHeightPass->IsInitialized())
            m_SceneHeightPass->Initialize(k_SceneHeightResolution);

        if (!m_SceneHeightPass->IsInitialized())
        {
            m_MeshRenderer.SetSceneHeightSource(0, 0, glm::mat4(1.0f));
            return;
        }

        const glm::mat4 topDown = SceneHeightPass::CalcTopDownMatrix(
            cameraPosition, k_SceneHeightExtent, k_SceneHeightResolution);

        // Antes do Begin: a fatia e lida ja no desenho, para contar as
        // superficies que ficam acima dela.
        m_SceneHeightPass->SetSlice(sliceY);

        m_SceneHeightPass->Begin(topDown);

        for (const auto& dc : queue.Meshes)
        {
            if (!dc.Mesh) continue;
            if (dc.Material && dc.Material->IsTransparent) continue;
            m_SceneHeightPass->DrawMesh(*dc.Mesh, dc.Transform);
        }

        m_SceneHeightPass->End();

        m_MeshRenderer.SetSceneHeightSource(
            m_SceneHeightPass->GetHeightMapID(),
            m_SceneHeightPass->GetSeedMapID(),
            m_SceneHeightPass->GetMatrix());
    }

    void SceneRenderer::RenderShadowPass(const RenderQueue& queue,
        const glm::vec3& cameraPosition,
        const glm::mat4& view,
        const glm::mat4& projection)
    {
        if (!queue.Light || !queue.Light->CastShadows) return;

        if (m_UseCSM)
        {
            // ── Cascaded Shadow Maps ──────────────────────────────────────
            if (!m_CSMPass) m_CSMPass = CascadedShadowPass::Create();
            if (!m_CSMPass->IsInitialized()) m_CSMPass->Initialize(2048);

            // Extrai near/far da projeção (assume perspective padrão)
            // near = C/(A-1), far = C/(A+1) onde A=proj[2][2], C=proj[3][2]
            float A = projection[2][2];
            float B = projection[3][2];
            float camNear = B / (A - 1.0f);
            float camFar = B / (A + 1.0f);
            // Limita o far das sombras pra não desperdiçar resolução
            camFar = glm::min(camFar, queue.Light->ShadowDistance > 0.f
                ? queue.Light->ShadowDistance : 100.0f);

            m_CSMPass->ComputeCascades(queue.Light->Direction, view, projection,
                camNear, camFar);

            for (int c = 0; c < m_CSMPass->GetCascadeCount(); ++c)
            {
                m_CSMPass->Begin(c);
                for (auto& dc : queue.Meshes)
                {
                    if (!dc.Mesh) continue;
                    if (ShadowCasterSkipped(dc)) continue;   // TRANSLUCENT_NOCAST_V1
                    m_CSMPass->DrawMesh(*dc.Mesh, dc.Transform);
                }
                m_CSMPass->End();
            }
        }
        else
        {
            // ── Shadow map simples (legado) ───────────────────────────────
            if (!m_ShadowPass) m_ShadowPass = ShadowMapPass::Create();
            if (!m_ShadowPass->IsInitialized()) m_ShadowPass->Initialize(4096);

            auto lsm = ShadowMapPass::CalcLightSpaceMatrix(
                queue.Light->Direction, queue.Light->ShadowDistance, cameraPosition);

            m_ShadowPass->Begin(lsm);
            for (auto& dc : queue.Meshes)
            {
                if (!dc.Mesh) continue;
                if (ShadowCasterSkipped(dc)) continue;   // TRANSLUCENT_NOCAST_V1
                m_ShadowPass->DrawMesh(*dc.Mesh, dc.Transform);
            }
            m_ShadowPass->End();
        }
    }

    // =============================================================================
    // Forward pass — opera sobre RenderQueue
    // =============================================================================

    void SceneRenderer::RenderForward(const RenderQueue& queue,
        const glm::mat4& viewProjection,
        const glm::mat4& view,
        const glm::vec3& cameraPosition)
    {
        m_MeshRenderer.SetEnvironment(m_Environment);
        if (m_ShadowPass)
            m_MeshRenderer.SetShadowMap(
                m_ShadowPass->GetDepthMapID(),
                m_ShadowPass->GetLightSpaceMatrix());

        // FORWARD_SHADOW_V1 — DESLIGA, pela mesma razao do bloco logo abaixo:
        // este e o forward puro dos previews, sem passe de sombra rodando. Sem
        // o zero explicito, o id de membro sobreviveria entre frames e o
        // preview amostraria a cascata do frame anterior do viewport.
        m_MeshRenderer.SetCascadedShadow(nullptr, glm::mat4(1.0f));

        // ── SCENE_DEPTH_SURFACE_V1 — DESLIGA explicitamente aqui ────────────
        //
        // Este e o caminho FORWARD PURO (deferred desligado): os previews do
        // Material Editor, do Script Editor e do Anim Graph. Nao ha G-Buffer
        // preenchido, entao nao ha "cena atras" para ler.
        //
        // O zero e explicito e nao redundante: o m_ScenePositionID e membro e
        // sobrevive entre frames. Sem esta linha, um SceneRenderer que rodasse
        // os dois caminhos ficaria com o id da ULTIMA vez que o deferred rodou
        // e amostraria uma textura velha — que aparece como sujeira que muda
        // sozinha, o tipo de defeito que nao se liga a causa.
        m_MeshRenderer.SetSceneDepthSource(0, 1, 1);

        if (m_SkyboxRenderer)
        {
            RenderCommand::SetDepthTest(false);
            RenderCommand::SetCullFace(false);
            m_SkyboxRenderer->Render(m_SkyboxView, m_SkyboxProjection);
            RenderCommand::SetDepthTest(true);
            RenderCommand::SetCullFace(true);
        }


        m_MeshRenderer.Begin(viewProjection, cameraPosition);
        for (auto& dc : queue.Meshes)
            if (dc.Mesh) m_MeshRenderer.DrawMesh(*dc.Mesh, dc.Transform, dc.Material, queue.Light,
                dc.Material && dc.Material->IsTransparent);
        m_MeshRenderer.End();

        // --- Partículas (billboards) — faltava no forward; só rodava no
        // deferred. Sem isso, qualquer preview que força forward (Material
        // Editor, Particle Editor) nunca desenhava partícula nenhuma, mesmo
        // com a simulação rodando normalmente (CPU-side) por trás.
        if (!queue.ParticleBatches.empty())
            m_ParticleRenderer.Render(queue.ParticleBatches, viewProjection, view);

        if (!queue.RibbonBatches.empty())
            m_RibbonRenderer.Render(queue.RibbonBatches, viewProjection, cameraPosition);
    }

    // =============================================================================
    // Deferred pass — opera sobre RenderQueue
    // =============================================================================

    void SceneRenderer::RenderDeferred(const RenderQueue& queue,
        const glm::mat4& viewProjection,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        uint32_t width, uint32_t height)
    {
        // ── POSTPROCESS_SKY_V1 — o que o efeito de tela pode saber do frame ──
        //
        // Capturado AQUI porque este e o unico ponto onde view, projection,
        // posicao da camera e luz direcional estao juntas. A inversa da
        // view-projection e o que permite a um material de post process
        // converter pixel em RAIO DO MUNDO — sem isso nao ha como estilizar o
        // ceu no grafo, so tratar a tela como imagem 2D.
        {
            m_PostProcessCamera.InvViewProjection = glm::inverse(projection * view);
            m_PostProcessCamera.CameraPosition = cameraPosition;

            // Time::Elapsed e a fonte de tempo UNICA da engine (ver
            // core/time.hpp). Usar outro relogio aqui faria um efeito de tela
            // animado e uma particula animada correrem em tempos diferentes.
            m_PostProcessCamera.TimeSeconds = Time::Elapsed();

            if (queue.Light)
            {
                // Direcao apontando PARA onde a luz vai (mesma convencao do
                // u_LightDirection do lighting pass), para nao haver dois
                // sentidos de "direcao do sol" na engine.
                m_PostProcessCamera.SunDirection = glm::normalize(queue.Light->Direction);
                m_PostProcessCamera.SunColor = queue.Light->Color;
                m_PostProcessCamera.SunIntensity = queue.Light->Intensity;
            }
        }

        // --- Probe bake on demand ---
        // Pedido enfileirado pelo SceneCollector (botão "Bake" ou load de
        // cena). Executa AQUI porque o SceneRenderer é quem tem o contexto
        // gráfico — operação offline, bloqueante, aceitável no editor.
        // Escrever via Target é seguro: não há mudança estrutural no
        // registry entre o Collect e o Render do mesmo frame (mesma
        // garantia dos ponteiros de Mesh/Material da queue).
        if (!queue.ProbeBakes.empty() || !queue.ReflectionBakes.empty())
        {
            if (!m_ProbeBakePass)
            {
                m_ProbeBakePass = ProbeBakePass::Create();
                m_ProbeBakePass->Initialize();
            }
            if (m_ProbeBakePass->IsInitialized())
            {
                // GI volumes primeiro — os reflection probes capturados
                // logo abaixo podem usar o grid recém-bakeado como
                // ambiente, ficando consistentes no mesmo frame.
                for (const auto& req : queue.ProbeBakes)
                    if (req.Target)
                        *req.Target = m_ProbeBakePass->Bake(queue, m_Environment, req);

                for (const auto& req : queue.ReflectionBakes)
                    if (req.Target)
                    {
                        // Ambiente da captura = o grid de GI mais próximo
                        // do PONTO DE CAPTURA (não o primeiro da lista) —
                        // com multi-volume, a probe da sala B deve refletir
                        // com o GI da sala B.
                        const ProbeVolumeData* gi = nullptr;
                        float best = std::numeric_limits<float>::max();
                        for (const auto& pv : queue.ProbeVolumes)
                        {
                            glm::vec3 local = glm::vec3(pv.WorldToLocal * glm::vec4(req.Position, 1.0f));
                            glm::vec3 d = glm::abs(local) - pv.HalfExtents;
                            float dist = glm::length(glm::max(d, glm::vec3(0.0f)))
                                + glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
                            if (dist < best) { best = dist; gi = &pv; }
                        }
                        *req.Target = m_ProbeBakePass->CaptureReflection(
                            queue, m_Environment, req, gi);
                    }
            }
        }

        // --- Seleção de volumes por proximidade da câmera ---
        // Uma cena de mundo aberto pode ter DEZENAS de volumes (um por
        // sala/área); o shader recebe poucos por frame (2 grids de GI,
        // 4 reflections, 8 interiores). A regra de seleção é a mesma dos
        // grandes engines: volumes que CONTÊM a câmera primeiro (SDF
        // negativa), depois os mais próximos. Cópias locais são baratas
        // (mat4 + shared_ptr por volume) e a queue permanece const.
        auto boxDistance = [&](const glm::mat4& worldToLocal, const glm::vec3& he)
            {
                glm::vec3 local = glm::vec3(worldToLocal * glm::vec4(cameraPosition, 1.0f));
                glm::vec3 d = glm::abs(local) - he;
                return glm::length(glm::max(d, glm::vec3(0.0f)))
                    + glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
            };

        auto probeSel = queue.ProbeVolumes;
        std::stable_sort(probeSel.begin(), probeSel.end(),
            [&](const ProbeVolumeData& a, const ProbeVolumeData& b)
            { return boxDistance(a.WorldToLocal, a.HalfExtents)
            < boxDistance(b.WorldToLocal, b.HalfExtents); });

        auto reflSel = queue.ReflectionProbes;
        std::stable_sort(reflSel.begin(), reflSel.end(),
            [&](const ReflectionProbeData& a, const ReflectionProbeData& b)
            { return boxDistance(a.WorldToLocal, a.HalfExtents)
            < boxDistance(b.WorldToLocal, b.HalfExtents); });

        auto interiorSel = queue.InteriorVolumes;
        std::stable_sort(interiorSel.begin(), interiorSel.end(),
            [&](const InteriorVolumeData& a, const InteriorVolumeData& b)
            { return boxDistance(a.WorldToLocal, a.HalfExtents)
            < boxDistance(b.WorldToLocal, b.HalfExtents); });

        // --- Sombras de Point/Spot Light ---
        // Entre as luzes marcadas com CastShadows, as
        // PointShadowPass::kMaxShadowLights (4) mais próximas da câmera
        // ganham uma camada do cube map array e re-renderizam a cena em
        // profundidade (6 faces, depth-only). A cópia local carrega o
        // ShadowLayer atribuído até o lighting — a queue permanece const.
        auto lightsSel = queue.PointLights;
        uint32_t pointShadowID = 0;
        {
            std::vector<size_t> candidates;
            for (size_t i = 0; i < lightsSel.size(); i++)
                if (lightsSel[i].CastShadows) candidates.push_back(i);

            std::stable_sort(candidates.begin(), candidates.end(),
                [&](size_t a, size_t b)
                {
                    float da = glm::distance(lightsSel[a].Position, cameraPosition) - lightsSel[a].Radius;
                    float db = glm::distance(lightsSel[b].Position, cameraPosition) - lightsSel[b].Radius;
                    return da < db;
                });

            int layer = 0;
            for (size_t idx : candidates)
            {
                if (layer >= PointShadowPass::kMaxShadowLights) break;

                if (!m_PointShadowPass)
                {
                    m_PointShadowPass = PointShadowPass::Create();
                    m_PointShadowPass->Initialize(512);
                }
                if (!m_PointShadowPass->IsInitialized()) break;

                m_PointShadowPass->RenderLightShadow(queue, layer,
                    lightsSel[idx].Position, lightsSel[idx].Radius);
                lightsSel[idx].ShadowLayer = layer++;
            }

            if (m_PointShadowPass && m_PointShadowPass->IsInitialized() && layer > 0)
                pointShadowID = m_PointShadowPass->GetTextureID();
        }

        // --- Resize ---
        if (width != m_Width || height != m_Height)
        {
            m_Width = width;
            m_Height = height;
            m_GBuffer.Resize(width, height);
            if (m_SSAOPass) m_SSAOPass->Resize(width, height);
        }

        // --- 1. Geometry Pass ---
        // Materiais transparentes (vidro, etc.) NÃO entram no G-Buffer —
        // o G-Buffer só guarda um fragmento por pixel (o mais próximo da
        // câmera), então não tem como "ver através" dele. Eles são
        // desenhados depois, num forward pass separado (passo 4.5).
        m_GeometryPass->Begin(m_GBuffer, viewProjection, cameraPosition);
        for (auto& dc : queue.Meshes)
            if (dc.Mesh && !(dc.Material && dc.Material->IsTransparent))
                m_GeometryPass->DrawMesh(*dc.Mesh, dc.Transform, dc.Material);
        m_GeometryPass->End();

        // --- 2. SSAO ---
        uint32_t ssaoID = 0;
        if (m_SSAOSettings.Enabled && m_SSAOPass && m_SSAOPass->IsInitialized())
        {
            m_SSAOPass->Execute(m_GBuffer, projection, view, m_SSAOSettings);
            ssaoID = m_SSAOPass->GetOcclusionTextureID();
        }
        if (m_LightingPass) m_LightingPass->SetSSAODebug(m_SSAOSettings.Debug);

        // --- 3. Lighting Pass ---
        RenderCommand::BindFramebuffer(m_TargetFBO);
        RenderCommand::SetViewport(0, 0, width, height);

        RenderCommand::BlitDepth(m_GBuffer.GetFramebufferID(), m_TargetFBO, width, height);

        uint32_t  shadowID = m_ShadowPass ? m_ShadowPass->GetDepthMapID() : 0;
        glm::mat4 lsm = m_ShadowPass ? m_ShadowPass->GetLightSpaceMatrix() : glm::mat4(1.0f);
        const CascadedShadowPass* csm = (m_UseCSM && m_CSMPass && m_CSMPass->IsInitialized())
            ? m_CSMPass.get() : nullptr;

        // SKY_LIGHT_V1 — por setter, nao por parametro do Execute (que ja tem
        // 14 e e chamado de mais de um lugar). Enviado TODO frame porque o
        // usuario pode ligar/desligar o Sky Light no Inspector a qualquer hora.
        if (queue.Sky) m_LightingPass->SetSkyLight(*queue.Sky, true);
        else           m_LightingPass->SetSkyLight(SkyLight{}, false);

        // CONTACT_SHADOW_V1 — a projecao deste frame, para o raio em espaco
        // de tela saber onde cada passo cai.
        m_LightingPass->SetProjection(projection);

        m_LightingPass->Execute(m_GBuffer, ssaoID, shadowID, lsm, csm,
            view, cameraPosition, queue.Light, m_Environment, lightsSel,
            interiorSel,
            probeSel, reflSel, pointShadowID);

        // --- 4. Skybox ---
        // O LightingPass agora preserva o depth do BlitDepth (depthMask=false).
        // O skybox usa LessEqual — aparece onde depth=1.0 (céu) e é bloqueado
        // pela geometria onde depth < 1.0.
        if (m_SkyboxRenderer)
            m_SkyboxRenderer->RenderDeferred(m_SkyboxView, m_SkyboxProjection);

        // --- 4.5. Forward pass de materiais transparentes ---
        // Desenhados em cima do resultado do deferred (que já está em
        // m_TargetFBO, com o depth do passe opaco preservado): depth-test
        // ligado (continuam sendo ocluídos pela geometria opaca), mas
        // depth-write desligado (não se ocluem incorretamente entre si) e
        // blend habilitado. Ordenados de trás pra frente — essencial pro
        // blend ficar visualmente correto quando há vários objetos
        // transparentes sobrepostos (ex: vários tubos de vidro).
        {
            std::vector<const MeshDrawCall*> transparent;
            for (auto& dc : queue.Meshes)
                if (dc.Mesh && dc.Material && dc.Material->IsTransparent)
                    transparent.push_back(&dc);

            if (!transparent.empty())
            {
                std::sort(transparent.begin(), transparent.end(),
                    [&cameraPosition](const MeshDrawCall* a, const MeshDrawCall* b)
                    {
                        glm::vec3 posA = glm::vec3(a->Transform[3]);
                        glm::vec3 posB = glm::vec3(b->Transform[3]);
                        float distA = glm::length(posA - cameraPosition);
                        float distB = glm::length(posB - cameraPosition);
                        return distA > distB; // mais distante primeiro
                    });

                m_MeshRenderer.SetEnvironment(m_Environment);
                if (m_ShadowPass)
                    m_MeshRenderer.SetShadowMap(
                        m_ShadowPass->GetDepthMapID(),
                        m_ShadowPass->GetLightSpaceMatrix());

                // ── FORWARD_SHADOW_V1 ───────────────────────────────────
                //
                // O `csm` logo acima ja e nulo quando as cascatas estao
                // desligadas, entao esta chamada cobre os dois casos.
                //
                // Repare no contraste com a linha de cima: o SetShadowMap
                // esta atras de `if (m_ShadowPass)`, e com m_UseCSM ligado
                // (o padrao) esse passe NUNCA e criado — era por isso que o
                // translucido nao tinha sombra nenhuma. Aqui a chamada e
                // incondicional de proposito.
                m_MeshRenderer.SetCascadedShadow(csm, view);

                // ── SCENE_DEPTH_SURFACE_V1 ──────────────────────────────
                //
                // Neste ponto o G-Buffer ja foi resolvido (passo 4) e NAO
                // esta mais ligado para escrita — o alvo agora e o
                // m_TargetFBO. Entao o attachment de posicao pode ser lido
                // como textura comum, sem o ciclo leitura/escrita que
                // impede a mesma coisa no caminho opaco.
                //
                // E isto que da profundidade de lamina d'agua: o material le
                // a distancia ate o fundo e decide cor e espuma a partir dela.
                m_MeshRenderer.SetSceneDepthSource(
                    m_GBuffer.GetPositionID(),
                    m_GBuffer.GetWidth(),
                    m_GBuffer.GetHeight());

                m_MeshRenderer.Begin(viewProjection, cameraPosition);
                for (auto* dc : transparent)
                    m_MeshRenderer.DrawMesh(*dc->Mesh, dc->Transform, dc->Material,
                        queue.Light, /*transparent*/ true);
                m_MeshRenderer.End();
            }
        }

        // --- 4.6. Partículas (billboards) ---
        // Em cima do deferred (depth da cena já no m_TargetFBO via BlitDepth):
        // depth-test occlui contra a geometria, depth-write off, blend on.
        if (!queue.ParticleBatches.empty())
            m_ParticleRenderer.Render(queue.ParticleBatches, viewProjection, view);
        if (!queue.RibbonBatches.empty())
            m_RibbonRenderer.Render(queue.RibbonBatches, viewProjection, cameraPosition);

        // --- 4.7. Volumetric Fog (screen-space) ---
        if (m_FogSettings.Enabled)
        {
            if (!m_FogPass)
                m_FogPass = VolumetricFogPass::Create();
            if (m_FogPass && !m_FogPass->IsInitialized())
                m_FogPass->Initialize();
            if (m_FogPass && m_FogPass->IsInitialized())
            {
                // ── VOLUME_SUN_V2 ────────────────────────────────────────
                //
                // As duas informacoes ja estavam AQUI, a poucas linhas de
                // distancia: `queue.Light` e a direcional que o lighting
                // pass usa, e `csm` e a mesma cascata que o passe 4.5
                // acabou de publicar no MeshRenderer para a agua ter
                // sombra. O fog so nunca as tinha pedido.
                //
                // Sem luz direcional na cena, SetSun com intensidade 0
                // desliga o termo — e nao um `if` em volta da chamada, que
                // deixaria o estado ANTERIOR vivo no passe entre frames.
                if (queue.Light)
                    m_FogPass->SetSun(queue.Light->Direction,
                        queue.Light->Color,
                        queue.Light->Intensity);
                else
                    m_FogPass->SetSun(glm::vec3(0.0f, -1.0f, 0.0f),
                        glm::vec3(1.0f), 0.0f);

                // Idem: chamada INCONDICIONAL. csm nulo zera a contagem
                // dentro do passe. Foi exatamente o `if` em volta do
                // SetShadowMap que deixou o translucido sem sombra por
                // rodadas.
                m_FogPass->SetCascadedShadow(csm, view);

                glm::mat4 invViewProj = glm::inverse(viewProjection);
                m_FogPass->Execute(m_GBuffer, m_FogSettings, invViewProj,
                    cameraPosition, lightsSel, Time::Elapsed(), width, height,
                    interiorSel,
                    probeSel);
            }
        }

        // --- 4.8. TAA Resolve ---
        // Integrado no ViewportRenderer que tem acesso ao HDR color attachment.
        // SceneRenderer só aplica o jitter na projection via BeginTAAFrame().

        // --- 5. Outline ---
        if (queue.SelectedMesh && queue.SelectedID != UINT32_MAX)
        {
            RenderCommand::SetDepthTest(true);
            RenderCommand::SetDepthWrite(true);
            RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::Less);
            RenderCommand::SetColorWrite(true);
            RenderCommand::SetCullFace(false);
            RenderCommand::SetStencilTest(false);
            RenderCommand::SetStencilWrite(0xFF);

            m_OutlineRenderer.Begin(viewProjection);
            m_OutlineRenderer.DrawOutline(*queue.SelectedMesh, queue.SelectedTransform,
                { 1.0f, 0.0f, 0.0f, 1.0f }, 1.03f);
            m_OutlineRenderer.End();
        }

        // ── Linhas de debug (esqueleto) ──────────────────────────────────
        //
        // Por ULTIMO e com o depth test DESLIGADO: os ossos ficam DENTRO da
        // malha, entao com depth test eles seriam ocluidos pela propria pele
        // e voce nao veria nada. Desenhar por cima e o comportamento certo
        // pra uma ferramenta de diagnostico.
        if (!queue.DebugLines.empty())
        {
            RenderCommand::SetDepthTest(false);
            RenderCommand::SetDepthWrite(false);
            RenderCommand::SetColorWrite(true);
            RenderCommand::SetCullFace(false);
            RenderCommand::SetStencilTest(false);

            m_LineRenderer.Begin(viewProjection);

            for (const auto& l : queue.DebugLines)
                m_LineRenderer.DrawLine(l.A, l.B, l.Color);

            m_LineRenderer.End();
        }

        RenderCommand::ResetState();
    }

    // =============================================================================
    // Compat — RenderScene legados constroem RenderQueue internamente
    // =============================================================================

    void SceneRenderer::RenderScene(const Scene& scene,
        const EditorCamera& camera,
        entt::entity selectedEntity)
    {
        // Só extrai os dados da câmera e delega pra sobrecarga unificada
        // abaixo — não existe mais lógica própria de coleta/render aqui.
        // Antes essa função tinha sua PRÓPRIA chamada de
        // SceneCollector::Collect + Render, e a sobrecarga da GameCamera
        // tinha a SUA, separada — foi exatamente essa duplicação que
        // deixou os dois caminhos divergirem (um recebia o tamanho real
        // do viewport, o outro não). Delegando assim, só existe UM lugar
        // de verdade fazendo o trabalho — Editor e Play sempre passam
        // pelo mesmo código, ficando impossível divergir de novo.
        const glm::mat4 projection = camera.GetProjectionMatrix();
        const glm::vec3 cameraPosition = camera.GetPosition();
        const glm::mat4 view = camera.GetViewMatrix();
        uint32_t width = (uint32_t)camera.GetViewportWidth();
        uint32_t height = (uint32_t)camera.GetViewportHeight();

        RenderScene(scene, view, projection, cameraPosition, selectedEntity, width, height);
    }

    void SceneRenderer::RenderScene(const Scene& scene,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        entt::entity selectedEntity,
        uint32_t width,
        uint32_t height)
    {
        glm::mat4 viewProjection = projection * view;
        auto queue = SceneCollector::Collect(scene, (uint32_t)selectedEntity, cameraPosition);

        // Antes este caminho (usado pela GameCamera no modo Play) sempre
        // usava m_Width/m_Height — o último tamanho conhecido de QUALQUER
        // chamada anterior, geralmente vindo do modo Edit (já que é o
        // outro overload, com EditorCamera, que de fato atualiza esses
        // valores). Se o viewport real no momento do Play tivesse um
        // tamanho diferente do último frame em Edit, o G-Buffer ficava
        // com dimensões erradas — causando os objetos perderem a
        // textura/cor correta (sample incorreto no Lighting Pass).
        // Agora usa o tamanho real passado pelo ViewportRenderer; só cai
        // no fallback antigo se ninguém passar nada (width/height = 0),
        // por segurança/compatibilidade.
        uint32_t w = (width > 0) ? width : m_Width;
        uint32_t h = (height > 0) ? height : m_Height;

        Render(queue, viewProjection, view, projection, cameraPosition, w, h);
    }

    // =============================================================================
    // InitializeDeferredPasses
    // =============================================================================

    void SceneRenderer::InitializeDeferredPasses(uint32_t width, uint32_t height)
    {
        m_GeometryPass = GeometryPass::Create();
        m_GeometryPass->Initialize();

        m_LightingPass = LightingPass::Create();
        m_LightingPass->Initialize();

        m_SSAOPass = SSAOPass::Create();
        m_SSAOPass->Initialize(width, height);

        m_GBuffer.Initialize(width, height);

        m_Width = width;
        m_Height = height;
    }

    // =============================================================================
    // Compat legacy helpers (usados pelo preview/forward legado)
    // =============================================================================

    void SceneRenderer::RenderShadowPassLegacy(const Scene& scene,
        const DirectionalLight* light, const glm::vec3& cameraPosition)
    {
        if (!light || !light->CastShadows) return;
        if (!m_ShadowPass) m_ShadowPass = ShadowMapPass::Create();
        if (!m_ShadowPass->IsInitialized()) m_ShadowPass->Initialize(4096);

        auto lsm = ShadowMapPass::CalcLightSpaceMatrix(
            light->Direction, light->ShadowDistance, cameraPosition);
        m_ShadowPass->Begin(lsm);

        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        std::function<void(entt::entity)> renderDepth = [&](entt::entity entity)
            {
                if (!registry.valid(entity)) return;
                if (registry.any_of<LightComponent, PostProcessComponent, InteriorVolumeComponent, ProbeVolumeComponent, FolderComponent>(entity)) return;
                auto* tc = registry.try_get<TransformComponent>(entity);
                auto* mc = registry.try_get<MeshComponent>(entity);
                if (mc && mc->Data && tc) m_ShadowPass->DrawMesh(*mc->Data, tc->Data.GetMatrix());
                auto* rel = registry.try_get<RelationshipComponent>(entity);
                if (rel) for (auto child : rel->Children) renderDepth(child);
            };
        for (auto entity : const_cast<Scene&>(scene).GetRootEntities()) renderDepth(entity);
        m_ShadowPass->End();
    }

    // SKY_CUBE_FIX_V1 — a UNICA lista de "entidade que nao e geometria".
    //
    // Sao componentes cuja entidade existe para configurar o mundo, nao para
    // aparecer nele: luz, sky light, volumes. Sem esta funcao a lista vivia
    // duplicada no RenderEntity e no GeometryPassEntity, e bastava esquecer
    // de um lado para o objeto virar um cubo solido no meio da cena.
    static bool IsNonVisualEntity(entt::registry& registry, entt::entity entity)
    {
        return registry.any_of<
            LightComponent,
            SkyLightComponent,      // <- faltava, e era a causa do cubo
            PostProcessComponent,
            InteriorVolumeComponent,
            ProbeVolumeComponent>(entity);
    }

    void SceneRenderer::RenderEntity(const Scene& scene, entt::entity entity,
        const glm::mat4& parentTransform, entt::entity selectedEntity,
        const DirectionalLight* light)
    {
        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        if (!registry.valid(entity)) return;
        // ═════════════════════════════════════════════════════════════════
        //  SKY_CUBE_FIX_V1 — O CUBO MISTERIOSO
        //
        //  Logo abaixo, entidade SEM MeshComponent cai no `else` e vira um
        //  CUBO SOLIDO desenhado no mundo (placeholder para objeto vazio).
        //  Isto aqui e a lista do que NAO deve receber esse tratamento — e
        //  ela nao tinha o SkyLightComponent.
        //
        //  Resultado: a propria entidade Sky Light era desenhada como um cubo
        //  opaco no meio da cena. E o "cubo com o ceu" que aparecia ao dar
        //  zoom out: nao era o skybox (esse tem a translacao removida e nao
        //  da para sair de dentro dele) — era este placeholder, refletindo o
        //  ambiente. Com Cor de Nuvem vermelha ele apareceu vermelho, que foi
        //  a prova.
        //
        //  MESMA FAMILIA de bug de outras duas listas que ja morderam nesta
        //  frente: a do any_of do Inspector e a do AllComponents do
        //  SceneSnapshot. Lista de tipos escrita a mao, em mais de um lugar,
        //  que o compilador nao confere. Por isso agora e UMA funcao,
        //  chamada pelos dois passes.
        // ═════════════════════════════════════════════════════════════════
        if (IsNonVisualEntity(registry, entity)) return;
        if (registry.any_of<FolderComponent>(entity))
        {
            auto* rel = registry.try_get<RelationshipComponent>(entity);
            if (rel) for (auto child : rel->Children)
                RenderEntity(scene, child, glm::mat4(1.0f), selectedEntity, light);
            return;
        }

        auto* tc = registry.try_get<TransformComponent>(entity);
        auto* mc = registry.try_get<MeshComponent>(entity);
        auto* mat = registry.try_get<MaterialComponent>(entity);

        glm::mat4 worldTransform = tc ? tc->Data.GetMatrix() : glm::mat4(1.0f);

        if (mc && mc->Data)
            m_MeshRenderer.DrawMesh(*mc->Data, worldTransform, mat ? mat->Data.get() : nullptr, light);
        else
            m_CubeRenderer.DrawCube(worldTransform, entity == selectedEntity);

        auto* rel = registry.try_get<RelationshipComponent>(entity);
        if (rel) for (auto child : rel->Children)
            RenderEntity(scene, child, glm::mat4(1.0f), selectedEntity, light);
    }

    void SceneRenderer::GeometryPassEntity(const Scene& scene, entt::entity entity)
    {
        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        if (!registry.valid(entity)) return;
        // SKY_CUBE_FIX_V1 — mesma lista do RenderEntity, agora numa funcao so.
        if (IsNonVisualEntity(registry, entity) || registry.any_of<FolderComponent>(entity)) return;

        auto* tc = registry.try_get<TransformComponent>(entity);
        auto* mc = registry.try_get<MeshComponent>(entity);
        auto* mat = registry.try_get<MaterialComponent>(entity);

        if (mc && mc->Data && tc)
            m_GeometryPass->DrawMesh(*mc->Data, tc->Data.GetMatrix(), mat ? mat->Data.get() : nullptr);

        auto* rel = registry.try_get<RelationshipComponent>(entity);
        if (rel) for (auto child : rel->Children) GeometryPassEntity(scene, child);
    }

} // namespace axe