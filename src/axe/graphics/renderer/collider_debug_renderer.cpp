#include "collider_debug_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/log/log.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>
#include <algorithm> // std::max no PushCapsule
#include "axe/physics/physics_components.hpp"

namespace axe
{

    static const char* s_Vert = R"(
    #version 460 core
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec4 a_Color;
    uniform mat4 u_VP;
    out vec4 v_Color;
    void main() { v_Color = a_Color; gl_Position = u_VP * vec4(a_Position, 1.0); }
)";

    static const char* s_Frag = R"(
    #version 460 core
    in  vec4 v_Color;
    out vec4 FragColor;
    void main() { FragColor = v_Color; }
)";

    void ColliderDebugRenderer::Initialize()
    {
        if (m_Initialized) return;
        m_Shader = Shader::Create(s_Vert, s_Frag);
        m_Initialized = true;
        AXE_CORE_INFO("[CAPSULE_DRAW_V1] ColliderDebugRenderer: capsula desenhada com ALTURA TOTAL (igual a fisica).");
    }

    // Adiciona dois pontos de linha com a cor definida
    static void PushLine(std::vector<float>& v, glm::vec3 a, glm::vec3 b, glm::vec4 col)
    {
        v.insert(v.end(), { a.x, a.y, a.z, col.r, col.g, col.b, col.a });
        v.insert(v.end(), { b.x, b.y, b.z, col.r, col.g, col.b, col.a });
    }

    void ColliderDebugRenderer::PushBox(std::vector<float>& verts,
        const glm::mat4& model, const glm::vec3& h)
    {
        // 8 cantos do box
        glm::vec3 corners[8] = {
            {-h.x,-h.y,-h.z}, { h.x,-h.y,-h.z}, { h.x, h.y,-h.z}, {-h.x, h.y,-h.z},
            {-h.x,-h.y, h.z}, { h.x,-h.y, h.z}, { h.x, h.y, h.z}, {-h.x, h.y, h.z},
        };

        // Transforma pela model matrix
        glm::vec3 w[8];
        for (int i = 0; i < 8; i++)
            w[i] = glm::vec3(model * glm::vec4(corners[i], 1.0f));

        // 12 arestas
        int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0}, // face traseira
            {4,5},{5,6},{6,7},{7,4}, // face frontal
            {0,4},{1,5},{2,6},{3,7}  // laterais
        };
        for (auto& e : edges)
            PushLine(verts, w[e[0]], w[e[1]], m_Color);
    }

    void ColliderDebugRenderer::PushSphere(std::vector<float>& verts,
        const glm::mat4& model, float radius, const glm::vec4& color)
    {
        const int segs = 24;
        glm::vec3 center = glm::vec3(model[3]);
        float r = radius;

        auto circle = [&](auto getPoint)
            {
                for (int i = 0; i < segs; i++)
                {
                    float a0 = (float)i / segs * glm::two_pi<float>();
                    float a1 = (float)(i + 1) / segs * glm::two_pi<float>();
                    PushLine(verts, getPoint(a0), getPoint(a1), color);
                }
            };

        // 3 círculos nos 3 planos
        circle([&](float a) { return center + glm::vec3(r * cosf(a), r * sinf(a), 0); });
        circle([&](float a) { return center + glm::vec3(r * cosf(a), 0, r * sinf(a)); });
        circle([&](float a) { return center + glm::vec3(0, r * cosf(a), r * sinf(a)); });
    }

    void ColliderDebugRenderer::PushCapsule(std::vector<float>& verts,
        const glm::mat4& model, float radius, float height)
    {
        const int segs = 16;

        // `height` e a altura TOTAL da capsula (base do hemisferio inferior
        // ate o topo do superior) — a MESMA convencao da fisica: tanto o
        // CharacterController (PhysicsWorld::CreateCharacter) quanto o
        // Collider Capsule (CreateShape) criam a CapsuleShape do Jolt com
        // halfHeight = height*0.5 - radius. A versao antiga usava `height`
        // como altura so do CILINDRO e desenhava os hemisferios ALEM dela:
        // a capsula desenhada media Height + 2*Radius — meio metro a mais
        // em cada ponta num personagem padrao. Era por isso que alinhar o
        // wireframe no chao exigia um Capsule Offset que afundava o
        // personagem no Play: o desenho e a fisica discordavam do tamanho.
        float halfH = std::max(height * 0.5f - radius, 0.0f);
        glm::vec3 center = glm::vec3(model[3]);

        // Círculo do meio (cilindro)
        for (int i = 0; i < segs; i++)
        {
            float a0 = (float)i / segs * glm::two_pi<float>();
            float a1 = (float)(i + 1) / segs * glm::two_pi<float>();
            glm::vec3 p0 = center + glm::vec3(radius * cosf(a0), halfH, radius * sinf(a0));
            glm::vec3 p1 = center + glm::vec3(radius * cosf(a1), halfH, radius * sinf(a1));
            glm::vec3 p2 = center + glm::vec3(radius * cosf(a0), -halfH, radius * sinf(a0));
            glm::vec3 p3 = center + glm::vec3(radius * cosf(a1), -halfH, radius * sinf(a1));
            PushLine(verts, p0, p1, m_Color);
            PushLine(verts, p2, p3, m_Color);
        }

        // Linhas verticais
        for (int i = 0; i < 4; i++)
        {
            float a = (float)i / 4 * glm::two_pi<float>();
            glm::vec3 top = center + glm::vec3(radius * cosf(a), halfH, radius * sinf(a));
            glm::vec3 bot = center + glm::vec3(radius * cosf(a), -halfH, radius * sinf(a));
            PushLine(verts, top, bot, m_Color);
        }

        // Semi-círculos dos hemisférios
        for (int i = 0; i < segs / 2; i++)
        {
            float a0 = (float)i / (segs / 2) * glm::pi<float>();
            float a1 = (float)(i + 1) / (segs / 2) * glm::pi<float>();
            // Hemisfério superior
            glm::vec3 t0 = center + glm::vec3(radius * cosf(a0), halfH + radius * sinf(a0), 0);
            glm::vec3 t1 = center + glm::vec3(radius * cosf(a1), halfH + radius * sinf(a1), 0);
            PushLine(verts, t0, t1, m_Color);
            // Hemisfério inferior
            glm::vec3 b0 = center + glm::vec3(radius * cosf(a0), -halfH - radius * sinf(a0), 0);
            glm::vec3 b1 = center + glm::vec3(radius * cosf(a1), -halfH - radius * sinf(a1), 0);
            PushLine(verts, b0, b1, m_Color);
        }
    }

    void ColliderDebugRenderer::UploadAndDraw(const std::vector<float>& verts,
        const glm::mat4& vp)
    {
        if (verts.empty()) return;

        // Upload dinâmico — recria o buffer a cada frame
        auto vb = VertexBuffer::Create(
            verts.data(),
            (uint32_t)(verts.size() * sizeof(float)));

        BufferLayout layout = {
            { ShaderDataType::Float3, sizeof(float) * 3 }, // position
            { ShaderDataType::Float4, sizeof(float) * 4 }  // color
        };

        auto va = VertexArray::Create();
        va->AddVertexBuffer(vb, layout);

        m_Shader->Bind();
        m_Shader->SetMat4("u_VP", glm::value_ptr(vp));

        RenderCommand::SetBlend(true);
        RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha, RendererAPI::BlendFactor::OneMinusSrcAlpha);
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthWrite(false);

        va->Bind();
        RenderCommand::DrawLines(va, (uint32_t)(verts.size() / 7));
        va->Unbind();

        m_Shader->Unbind();

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetBlend(false);
    }

    void ColliderDebugRenderer::Render(const Scene& scene,
        const glm::mat4& view, const glm::mat4& projection)
    {
        if (!m_Initialized) return;

        glm::mat4 vp = projection * view;
        std::vector<float> verts;

        auto& registry = const_cast<Scene&>(scene).GetRegistry();

        registry.view<ColliderComponent, TransformComponent>().each(
            [&](entt::entity e, ColliderComponent& col, TransformComponent& tc)
            {
                if (!col.ShowDebug) return; // só desenha se ativado

                glm::vec3 scale = tc.Data.Scale;

                // Model matrix base do objeto
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, tc.Data.Position);
                model = glm::rotate(model, tc.Data.Rotation.y, glm::vec3(0, 1, 0));
                model = glm::rotate(model, tc.Data.Rotation.x, glm::vec3(1, 0, 0));
                model = glm::rotate(model, tc.Data.Rotation.z, glm::vec3(0, 0, 1));

                // Aplica offset do collider (em espaço local do objeto, escalado)
                if (glm::length(col.Offset) > 0.0001f)
                    model = glm::translate(model, col.Offset * scale);

                switch (col.Shape)
                {
                case ColliderShape::Box:
                    PushBox(verts, model, col.HalfExtent * scale);
                    break;
                case ColliderShape::Sphere:
                    PushSphere(verts, model, col.Radius * glm::compMax(scale), m_Color);
                    break;
                case ColliderShape::Capsule:
                    PushCapsule(verts, model,
                        col.CapsuleRadius * glm::compMax(glm::vec2(scale.x, scale.z)),
                        col.Height * scale.y);
                    break;
                case ColliderShape::Mesh:
                case ColliderShape::ConvexHull:
                {
                    // Desenha wireframe das arestas do mesh
                    auto* mc = const_cast<Scene&>(scene).GetRegistry().try_get<MeshComponent>(e);
                    if (!mc || !mc->Data) break;
                    const auto& meshVerts = mc->Data->GetVertices();
                    const auto& meshIndices = mc->Data->GetIndices();
                    for (size_t i = 0; i + 2 < meshIndices.size(); i += 3)
                    {
                        glm::vec3 p0 = glm::vec3(model * glm::vec4(meshVerts[meshIndices[i]].Position * scale, 1.0f));
                        glm::vec3 p1 = glm::vec3(model * glm::vec4(meshVerts[meshIndices[i + 1]].Position * scale, 1.0f));
                        glm::vec3 p2 = glm::vec3(model * glm::vec4(meshVerts[meshIndices[i + 2]].Position * scale, 1.0f));
                        auto push = [&](glm::vec3 a, glm::vec3 b) {
                            verts.insert(verts.end(), { a.x,a.y,a.z, m_Color.r,m_Color.g,m_Color.b,m_Color.a });
                            verts.insert(verts.end(), { b.x,b.y,b.z, m_Color.r,m_Color.g,m_Color.b,m_Color.a });
                            };
                        push(p0, p1); push(p1, p2); push(p2, p0);
                    }
                    break;
                }
                }
            });

        // ── Cápsula do CharacterController ───────────────────────────────
        //
        // Mesma cápsula que a física usa: origem nos PÉS (transform), corpo
        // subindo Height. Desenhada aqui para que ajustar Height/Radius no
        // Inspector seja um ato visual, e não tentativa e erro no Play.
        registry.view<CharacterControllerComponent, TransformComponent>().each(
            [&](entt::entity e, CharacterControllerComponent& cc, TransformComponent& tc)
            {
                if (!cc.ShowDebug) return;

                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, tc.Data.Position);
                model = glm::rotate(model, tc.Data.Rotation.y, glm::vec3(0, 1, 0));

                // A cápsula da física fica meia altura ACIMA da origem (ver
                // PhysicsWorld::CreateCharacter) — o desenho segue a mesma
                // regra, senão o wireframe mentiria sobre onde ela está.
                model = glm::translate(model, glm::vec3(
                    cc.CapsuleOffset.x,
                    cc.Height * 0.5f + cc.CapsuleOffset.y,
                    cc.CapsuleOffset.z));

                // SEM escala do transform, de propósito: Height e Radius do
                // CharacterController são METROS ABSOLUTOS (o Jolt cria a
                // cápsula com esses valores crus). Multiplicar pela escala do
                // personagem — 0.015 num Mixamo — desenhava uma cápsula de
                // 1,6 cm dentro do pé: "ativei e não vejo nada".
                PushCapsule(verts, model, cc.Radius, cc.Height);
            });

        UploadAndDraw(verts, vp);
    }

    void ColliderDebugRenderer::PushCone(std::vector<float>& verts, const glm::vec3& apex,
        const glm::vec3& direction, float length, float halfAngleDegrees, const glm::vec4& color)
    {
        // Constrói uma base ortonormal (right/up) perpendicular à direção
        // do cone, pra desenhar o círculo da "boca" do cone nessa base.
        glm::vec3 dir = glm::length(direction) > 0.0001f ? glm::normalize(direction) : glm::vec3(0, -1, 0);
        glm::vec3 up = (fabsf(dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 right = glm::normalize(glm::cross(dir, up));
        up = glm::normalize(glm::cross(right, dir));

        float radius = length * tanf(glm::radians(halfAngleDegrees));
        glm::vec3 baseCenter = apex + dir * length;

        const int segs = 24;
        auto pointOnCircle = [&](float a)
            {
                return baseCenter + (right * cosf(a) + up * sinf(a)) * radius;
            };

        // Círculo da boca do cone
        for (int i = 0; i < segs; i++)
        {
            float a0 = (float)i / segs * glm::two_pi<float>();
            float a1 = (float)(i + 1) / segs * glm::two_pi<float>();
            PushLine(verts, pointOnCircle(a0), pointOnCircle(a1), color);
        }

        // Linhas do ápice até 8 pontos da borda — dão a sensação de "leque"
        // do cone, igual ao gizmo de Spot Light de outras engines
        for (int i = 0; i < 8; i++)
        {
            float a = (float)i / 8 * glm::two_pi<float>();
            PushLine(verts, apex, pointOnCircle(a), color);
        }
    }

    void ColliderDebugRenderer::RenderLights(const Scene& scene,
        const glm::mat4& view, const glm::mat4& projection)
    {
        if (!m_Initialized) return;

        glm::mat4 vp = projection * view;
        std::vector<float> verts;

        auto& registry = const_cast<Scene&>(scene).GetRegistry();

        // Só Point Light tem "raio de alcance" — Directional Light é
        // paralela/infinita, não faz sentido um gizmo de raio pra ela.
        registry.view<PointLightComponent>().each(
            [&](entt::entity e, PointLightComponent& plc)
            {
                if (!plc.Data) return;

                // Posição sincronizada com o TransformComponent, igual ao
                // SceneCollector faz pra renderização real da luz.
                glm::vec3 position = plc.Data->Position;
                glm::vec3 direction = plc.Data->Direction;
                if (auto* tc = registry.try_get<TransformComponent>(e))
                {
                    position = tc->Data.Position;
                    // Mesmo cálculo do SceneCollector — o gizmo precisa
                    // bater exatamente com a direção real da luz, senão o
                    // cone desenhado mente sobre pra onde a luz aponta.
                    direction = ComputeSpotDirection(tc->Data.Rotation);
                }

                if (plc.Data->IsSpot)
                {
                    // Spot Light — mostra o cone (ângulo externo) em vez da
                    // esfera, já que o raio de alcance só se aplica dentro
                    // do feixe, não em todas as direções.
                    PushCone(verts, position, direction,
                        plc.Data->Radius, plc.Data->OuterConeAngle, m_LightColor);
                }
                else
                {
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
                    PushSphere(verts, model, plc.Data->Radius, m_LightColor);
                }
            });

        // Interior Volumes — caixa wireframe ciano. Mesma decomposição de
        // escala do SceneCollector: a matriz do PushBox fica só com
        // rotação+translação e a escala vira half extents, pro gizmo bater
        // EXATAMENTE com o volume que o lighting pass usa.
        const glm::vec4 volumeColor{ 0.2f, 0.9f, 1.0f, 0.9f };
        registry.view<InteriorVolumeComponent>().each(
            [&](entt::entity e, InteriorVolumeComponent& ivc)
            {
                if (!ivc.Data.Enabled) return;

                glm::mat4 world = scene.GetWorldTransform(e);
                glm::vec3 sc(
                    glm::length(glm::vec3(world[0])),
                    glm::length(glm::vec3(world[1])),
                    glm::length(glm::vec3(world[2])));
                sc = glm::max(sc, glm::vec3(0.0001f));

                glm::mat4 rt = world;
                rt[0] = glm::vec4(glm::vec3(world[0]) / sc.x, 0.0f);
                rt[1] = glm::vec4(glm::vec3(world[1]) / sc.y, 0.0f);
                rt[2] = glm::vec4(glm::vec3(world[2]) / sc.z, 0.0f);

                // PushBox usa m_Color internamente — seta a cor do volume
                // e restaura depois, sem mexer na assinatura existente.
                glm::vec4 prev = m_Color;
                m_Color = volumeColor;
                PushBox(verts, rt, sc * 0.5f);
                m_Color = prev;
            });

        // Probe Volumes — caixa wireframe verde-limão + (opcional) uma
        // cruzinha em cada probe, na MESMA posição usada no bake (centro
        // das células), pro artista ver exatamente o que foi capturado.
        const glm::vec4 probeBoxColor{ 0.55f, 1.0f, 0.25f, 0.9f };
        registry.view<ProbeVolumeComponent>().each(
            [&](entt::entity e, ProbeVolumeComponent& pvc)
            {
                if (!pvc.Settings.Enabled) return;

                glm::mat4 world = scene.GetWorldTransform(e);
                glm::vec3 sc(
                    glm::length(glm::vec3(world[0])),
                    glm::length(glm::vec3(world[1])),
                    glm::length(glm::vec3(world[2])));
                sc = glm::max(sc, glm::vec3(0.0001f));

                glm::mat4 rt = world;
                rt[0] = glm::vec4(glm::vec3(world[0]) / sc.x, 0.0f);
                rt[1] = glm::vec4(glm::vec3(world[1]) / sc.y, 0.0f);
                rt[2] = glm::vec4(glm::vec3(world[2]) / sc.z, 0.0f);

                glm::vec4 prev = m_Color;
                m_Color = probeBoxColor;
                PushBox(verts, rt, sc * 0.5f);
                m_Color = prev;

                if (pvc.Settings.ShowProbes)
                {
                    glm::ivec3 res = glm::clamp(pvc.Settings.Resolution,
                        glm::ivec3(2), glm::ivec3(16));
                    glm::vec3 he = sc * 0.5f;
                    glm::vec3 step = 2.0f * he / glm::vec3(res);
                    const float r = 0.08f; // meia-cruz

                    for (int z = 0; z < res.z; z++)
                        for (int y = 0; y < res.y; y++)
                            for (int x = 0; x < res.x; x++)
                            {
                                glm::vec3 local = -he + (glm::vec3(x, y, z) + 0.5f) * step;
                                glm::vec3 p = glm::vec3(rt * glm::vec4(local, 1.0f));
                                PushLine(verts, p - glm::vec3(r, 0, 0), p + glm::vec3(r, 0, 0), probeBoxColor);
                                PushLine(verts, p - glm::vec3(0, r, 0), p + glm::vec3(0, r, 0), probeBoxColor);
                                PushLine(verts, p - glm::vec3(0, 0, r), p + glm::vec3(0, 0, r), probeBoxColor);
                            }
                }
            });

        // Reflection Probes — caixa wireframe magenta (influência/box
        // projection) + cruz no ponto de captura (posição do transform)
        const glm::vec4 reflColor{ 1.0f, 0.35f, 0.9f, 0.9f };
        registry.view<ReflectionProbeComponent>().each(
            [&](entt::entity e, ReflectionProbeComponent& rpc)
            {
                if (!rpc.Settings.Enabled) return;

                glm::mat4 world = scene.GetWorldTransform(e);
                glm::vec3 sc(
                    glm::length(glm::vec3(world[0])),
                    glm::length(glm::vec3(world[1])),
                    glm::length(glm::vec3(world[2])));
                sc = glm::max(sc, glm::vec3(0.0001f));

                glm::mat4 rt = world;
                rt[0] = glm::vec4(glm::vec3(world[0]) / sc.x, 0.0f);
                rt[1] = glm::vec4(glm::vec3(world[1]) / sc.y, 0.0f);
                rt[2] = glm::vec4(glm::vec3(world[2]) / sc.z, 0.0f);

                glm::vec4 prev = m_Color;
                m_Color = reflColor;
                PushBox(verts, rt, sc * 0.5f);
                m_Color = prev;

                glm::vec3 p = glm::vec3(rt[3]);
                const float r = 0.2f;
                PushLine(verts, p - glm::vec3(r, 0, 0), p + glm::vec3(r, 0, 0), reflColor);
                PushLine(verts, p - glm::vec3(0, r, 0), p + glm::vec3(0, r, 0), reflColor);
                PushLine(verts, p - glm::vec3(0, 0, r), p + glm::vec3(0, 0, r), reflColor);
            });

        UploadAndDraw(verts, vp);
    }

} // namespace axe