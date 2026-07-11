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

    void SceneRenderer::Render(const RenderQueue& queue,
        const glm::mat4& viewProjection,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        uint32_t width, uint32_t height)
    {
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

        RenderShadowPass(queue, cameraPosition, view, projection);

        if (m_DeferredEnabled && m_DeferredSupported && m_TargetFBO != 0)
            RenderDeferred(queue, viewProjection, view, projection, cameraPosition, width, height);
        else
            RenderForward(queue, viewProjection, view, cameraPosition);
    }

    // =============================================================================
    // Shadow pass — opera sobre RenderQueue
    // =============================================================================

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
                    if (dc.Mesh) m_CSMPass->DrawMesh(*dc.Mesh, dc.Transform);
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
                if (dc.Mesh) m_ShadowPass->DrawMesh(*dc.Mesh, dc.Transform);
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

    void SceneRenderer::RenderEntity(const Scene& scene, entt::entity entity,
        const glm::mat4& parentTransform, entt::entity selectedEntity,
        const DirectionalLight* light)
    {
        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        if (!registry.valid(entity)) return;
        if (registry.any_of<PostProcessComponent, InteriorVolumeComponent, ProbeVolumeComponent, LightComponent>(entity)) return;
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
        if (registry.any_of<PostProcessComponent, InteriorVolumeComponent, ProbeVolumeComponent, LightComponent, FolderComponent>(entity)) return;

        auto* tc = registry.try_get<TransformComponent>(entity);
        auto* mc = registry.try_get<MeshComponent>(entity);
        auto* mat = registry.try_get<MaterialComponent>(entity);

        if (mc && mc->Data && tc)
            m_GeometryPass->DrawMesh(*mc->Data, tc->Data.GetMatrix(), mat ? mat->Data.get() : nullptr);

        auto* rel = registry.try_get<RelationshipComponent>(entity);
        if (rel) for (auto child : rel->Children) GeometryPassEntity(scene, child);
    }

} // namespace axe