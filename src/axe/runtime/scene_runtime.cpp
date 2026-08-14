#include "axe/runtime/scene_runtime.hpp"

#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/scene/game_mode_asset.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/physics/physics_system.hpp"
#include "axe/audio/audio_engine.hpp"
#include "axe/script/script_base.hpp"
#include "axe/script/script_component.hpp"
#include "axe/input/input.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    SceneRuntime* SceneRuntime::s_PhysicsCallbackOwner = nullptr;

    // ─────────────────────────────────────────────────────────────────────────
    SceneRuntime::~SceneRuntime()
    {
        // Ver a nota no header. Nao para scripts nem audio — para isso
        // existe o OnStop, e quem morre em Play deveria te-lo chamado. Isto
        // aqui so impede que um singleton fique com ponteiro para um objeto
        // que acabou de deixar de existir.
        UninstallPhysicsCallbacks();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void SceneRuntime::InstallPhysicsCallbacks(Scene& scene)
    {
        // Os callbacks recebem entt::entity como uint32_t, via UserData do
        // body.
        //
        // Capturam `this` e `&scene`. A Scene nao e recriada entre Play e
        // Stop (o snapshot repoe o registry, nao troca o objeto), mas E
        // recriada ao abrir outra cena ou outro projeto — por isso o
        // Uninstall existe e por isso o destrutor o chama.
        PhysicsSystem::Get().SetCollisionCallback(
            [this, &scene](uint32_t a, uint32_t b)
            {
                m_ScriptWorld.DispatchCollision(scene,
                    (entt::entity)a, (entt::entity)b);
            });

        PhysicsSystem::Get().SetTriggerCallbacks(
            [this, &scene](uint32_t a, uint32_t b)
            {
                // Para triggers: chama OnCollision em ambas as partes
                // (comportamento padrao — personagem encosta no trigger)
                m_ScriptWorld.DispatchCollision(scene,
                    (entt::entity)a, (entt::entity)b);
                m_ScriptWorld.DispatchTriggerEnter(scene,
                    (entt::entity)a, (entt::entity)b);
            },
            [this, &scene](uint32_t a, uint32_t b)
            {
                m_ScriptWorld.DispatchTriggerExit(scene,
                    (entt::entity)a, (entt::entity)b);
            });

        s_PhysicsCallbackOwner = this;
    }

    // ─────────────────────────────────────────────────────────────────────────
    void SceneRuntime::UninstallPhysicsCallbacks()
    {
        // So desinstala o que E seu. Ver a nota no header sobre por que a
        // checagem de dono nao e paranoia.
        if (s_PhysicsCallbackOwner != this)
            return;

        PhysicsSystem::Get().SetCollisionCallback(nullptr);
        PhysicsSystem::Get().SetTriggerCallbacks(nullptr, nullptr);
        s_PhysicsCallbackOwner = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    void SceneRuntime::InitializeServices(Window* window)
    {
        // Input primeiro: o AudioEngine nao depende dele, mas um script que
        // rode no primeiro frame depende dos dois, e a ordem "input antes de
        // tudo" e a mesma que o editor sempre usou.
        Input::Init(window);

        // Audio sobe no boot, e nao no primeiro som tocado. Abrir o device
        // custa alguns milissegundos: pagar isso aqui e invisivel; pagar no
        // primeiro tiro e um engasgo no meio do jogo.
        AudioEngine::Init();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void SceneRuntime::ShutdownServices()
    {
        // Antes do device morrer: se sobrar voice viva, ela ainda segura o
        // PCM de um clip e o backend fecharia com dado em uso.
        AudioEngine::Shutdown();
    }

    // ─────────────────────────────────────────────────────────────────────────
    SceneRuntime::ListenerPose SceneRuntime::PoseFromView(const glm::vec3& position,
        const glm::mat4& view)
    {
        ListenerPose pose;
        pose.Position = position;

        const glm::mat3 r = glm::transpose(glm::mat3(view));
        pose.Forward = -r[2];
        pose.Up = r[1];
        return pose;
    }

    // ─────────────────────────────────────────────────────────────────────────
    SceneRuntime::StartResult SceneRuntime::OnStart(Scene& scene, const StartConfig& cfg)
    {
        StartResult result;

        // ── 1. Callbacks do Jolt -> ScriptWorld ──────────────────────────
        //
        // ANTES de qualquer OnSceneStart, para que os bodies ja criados
        // sejam cobertos.
        InstallPhysicsCallbacks(scene);

        // ── 2/3. Fisica e scripts ────────────────────────────────────────
        m_PhysicsWorld.OnSceneStart(scene);
        m_ScriptWorld.SetActiveCamera(&m_GameCamera);
        m_ScriptWorld.OnSceneStart(scene);

        // ── 4. Audio ─────────────────────────────────────────────────────
        //
        // DEPOIS do Capture do editor, sempre. Ver a nota no header.
        m_AudioWorld.OnScenePlay(scene);

        // ── 5. GameMode -> DefaultPawn -> GameCamera ─────────────────────
        m_PlayerEntity = entt::null;
        auto& registry = scene.GetRegistry();

        if (!cfg.GameModeUUID.empty())
        {
            const AssetRecord* gmRec = AssetDatabase::Get().GetByUUID(cfg.GameModeUUID);
            if (gmRec)
            {
                auto gmAsset = GameModeAsset::LoadFromFile(gmRec->FilePath);
                if (gmAsset && gmAsset->HasDefaultPawn())
                {
                    registry.view<ScriptComponent>().each([&](entt::entity e, ScriptComponent& sc)
                        {
                            if (m_PlayerEntity != entt::null) return;
                            const AssetRecord* rec = AssetDatabase::Get().GetByPath(sc.ScriptAssetPath);
                            if (rec && rec->UUID == gmAsset->DefaultPawnScriptUUID)
                                m_PlayerEntity = e;
                        });

                    if (m_PlayerEntity != entt::null)
                    {
                        auto* sa = registry.try_get<SpringArmComponent>(m_PlayerEntity);
                        auto* tc = registry.try_get<TransformComponent>(m_PlayerEntity);

                        if (sa)
                        {
                            m_GameCamera.CameraMode = GameCamera::Mode::ThirdPerson;
                            m_GameCamera.TPDistance = sa->Length;
                            m_GameCamera.TPHeight = sa->HeightOffset;
                            m_GameCamera.TPLagSpeed = sa->LagSpeed;
                            m_GameCamera.TPMouseRotates = sa->MouseRotates;
                        }
                        if (auto* cam = registry.try_get<CameraComponent>(m_PlayerEntity))
                        {
                            m_GameCamera.Fov = cam->Fov;
                            m_GameCamera.NearClip = cam->NearClip;
                            m_GameCamera.FarClip = cam->FarClip;
                            m_GameCamera.Sensitivity = cam->Sensitivity;
                        }
                        if (tc)
                        {
                            glm::vec3 startPos = tc->Data.Position +
                                glm::vec3(0, m_GameCamera.TPHeight, m_GameCamera.TPDistance);
                            m_GameCamera.Reset(startPos, -90.0f, -10.0f);
                            m_GameCamera.SetTarget(&tc->Data.Position);
                        }
                        AXE_CORE_INFO("GameMode: pawn encontrado (entity {}), camera third person.",
                            (uint32_t)m_PlayerEntity);
                    }
                }
            }
        }

        // ── 6. Camera primaria da cena ───────────────────────────────────
        //
        // Vem DEPOIS do pawn de proposito: uma camera marcada IsPrimary na
        // cena e uma escolha explicita do autor do nivel, e vence o default
        // do GameMode.
        for (auto entity : registry.view<CameraComponent>())
        {
            result.HasSceneCamera = true;

            auto& cam = registry.get<CameraComponent>(entity);
            if (cam.IsPrimary)
            {
                m_GameCamera.Fov = cam.Fov;
                m_GameCamera.NearClip = cam.NearClip;
                m_GameCamera.FarClip = cam.FarClip;
                m_GameCamera.MoveSpeed = cam.MoveSpeed;
                m_GameCamera.Sensitivity = cam.Sensitivity;
                if (auto* tc = registry.try_get<TransformComponent>(entity))
                    m_GameCamera.Reset(tc->Data.Position, tc->Data.Rotation.y, tc->Data.Rotation.x);
                break;
            }
        }

        m_GameCamera.m_FirstMouse = true;

        result.PlayerEntity = m_PlayerEntity;
        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────
    void SceneRuntime::OnUpdate(Scene& scene, float deltaTime, const TickContext& ctx)
    {
        const bool inPlay = (ctx.Mode == TickMode::Play);
        const bool paused = (ctx.Mode == TickMode::Paused);

        // ── Particulas e animacao ────────────────────────────────────────
        //
        // Tickam em Play E em EditPreview; congelam no Pause.
        //
        // allowDestroy so em Play: em Edit o ParticleWorld nunca pode
        // destruir entidades — seria apagar trabalho do usuario.
        //
        // AnimationWorld com inPlay=false nao avanca o tempo, mas continua
        // calculando a palette: e o que mostra o personagem na bind pose em
        // vez de colapsado na origem.
        if (!paused)
        {
            const glm::vec3 lodPos = ctx.LodCameraPosition
                ? *ctx.LodCameraPosition
                : m_GameCamera.GetPosition();

            m_ParticleWorld.OnUpdate(scene, deltaTime, inPlay, lodPos);
            m_AnimationWorld.OnUpdate(scene, deltaTime, inPlay);
        }

        // ── Simulacao de jogo ────────────────────────────────────────────
        if (inPlay)
        {
            // O alvo da camera e re-apontado por frame porque o
            // TransformComponent do pawn pode ter sido realocado pelo entt
            // (um emplace em qualquer entidade invalida ponteiros do pool).
            if (m_PlayerEntity != entt::null)
            {
                if (auto* tc = scene.GetRegistry().try_get<TransformComponent>(m_PlayerEntity))
                    m_GameCamera.SetTarget(&tc->Data.Position);
            }

            m_GameCamera.OnUpdate(deltaTime, ctx.InputWindow);
            m_ScriptWorld.OnSceneUpdate(scene, deltaTime);
            m_PhysicsWorld.OnUpdate(scene, deltaTime);
            ScriptBase::TickScreenMessages(deltaTime);
        }

        // ── Audio ────────────────────────────────────────────────────────
        //
        // Fora do bloco de Play: o AudioWorld tambem roda em Edit para
        // alimentar o listener (preview do Animation Editor) e para servir o
        // botao Play do Inspector. Ele proprio decide o que fica mudo, via
        // `inPlay`.
        ListenerPose listener;
        if (ctx.ListenerOverride)
            listener = *ctx.ListenerOverride;
        else
            listener = PoseFromView(m_GameCamera.GetPosition(), m_GameCamera.GetViewMatrix());

        m_AudioWorld.OnUpdate(scene, deltaTime, inPlay, paused,
            listener.Position, listener.Forward, listener.Up);

        // Recolhe as voices que terminaram. Depois do AudioWorld: uma voice
        // criada neste frame nao pode ser recolhida antes de tocar.
        AudioEngine::Update(deltaTime);
    }

    // ─────────────────────────────────────────────────────────────────────────
    void SceneRuntime::OnStop(Scene& scene)
    {
        m_ScriptWorld.OnSceneStop(scene);
        m_ParticleWorld.OnSceneStop(scene);

        // Stop mata todo som do jogo. Sem isso, um loop disparado em Play
        // continuaria tocando por cima do editor — e so sumiria fechando o
        // programa.
        //
        // O AudioWorld zera tambem os handles nos componentes; o StopAll
        // cobre o que nao pertence a fonte nenhuma (one-shot de notify em
        // voo no momento do Stop).
        m_AudioWorld.OnSceneStop(scene);
        AudioEngine::StopAll();

        m_GameCamera.CameraMode = GameCamera::Mode::FreeFly;
        m_GameCamera.ClearTarget();
        m_PlayerEntity = entt::null;

        m_PhysicsWorld.OnSceneStop(scene);
        ScriptBase::ClearScreenMessages();

        // Desliga os callbacks do Jolt.
        //
        // Nao existia no EditorLayer, e ali passava despercebido: as lambdas
        // capturavam `this` de um EditorLayer que vivia tanto quanto o
        // programa. Aqui elas capturam tambem a `Scene`, e o editor recria a
        // Scene ao abrir outro projeto ou outra cena — um callback vivo
        // apontando para a cena antiga seria use-after-free na primeira
        // colisao do Play seguinte.
        //
        // POR ULTIMO, e nao no inicio: o PhysicsWorld::OnSceneStop logo
        // acima remove os bodies, e o Jolt pode emitir contatos durante essa
        // remocao. Desconectar antes engoliria esses eventos em silencio —
        // e o OnSceneStop de um script deixaria de ver a ultima colisao.
        // Este e o comportamento que o EditorLayer sempre teve.
        UninstallPhysicsCallbacks();
    }

} // namespace axe