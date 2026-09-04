#include "axe/runtime/scene_runtime.hpp"

#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/scene/game_mode_asset.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/material/material_shader_cache.hpp"
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

        // ── 0. Cache de shader de material ───────────────────────────────
        //
        // Zerado a cada Play. O cache serve aos spawns de tempo de jogo
        // (sub-emissor, FX de AnimNotify) e, no editor, um material editado
        // entre um Play e outro precisa aparecer no proximo — sem isto, a
        // sessao anterior mandaria na atual. No jogo isso roda uma vez, com o
        // cache ja vazio, e nao custa nada.
        MaterialShaderCache::Clear();

        // ── 1. Callbacks do Jolt -> ScriptWorld ──────────────────────────
        //
        // ANTES de qualquer OnSceneStart, para que os bodies ja criados
        // sejam cobertos.
        InstallPhysicsCallbacks(scene);

        // ── 2. Fisica ────────────────────────────────────────────────────
        m_PhysicsWorld.OnSceneStart(scene);

        // ── 3. Cutscenes, ANTES dos scripts ──────────────────────────────
        //
        // A ordem entre estes dois nao e indiferente, e o motivo e afiado:
        // SequenceWorld::OnSceneStart LIMPA as instancias. Um script cujo
        // OnStart dispara a cutscene de abertura criaria a instancia, e a
        // limpeza logo em seguida a jogaria fora — a cutscene sumiria antes
        // do primeiro frame, sem erro nenhum.
        //
        // Carrega os `.axeseq` e dispara os que tem PlayOnStart. As tracks de
        // Event so entregam durante o OnUpdate, entao nada aqui precisa que as
        // DLLs de script ja estejam carregadas.
        m_CameraCutActive = false;
        m_CameraCutSavedFov = m_GameCamera.Fov;
        m_SequenceWorld.OnSceneStart(scene, &m_PhysicsWorld);

        // ── 4. Scripts ───────────────────────────────────────────────────
        m_ScriptWorld.SetActiveCamera(&m_GameCamera);

        // ANTES do OnSceneStart: e ele que monta o ScriptContext de cada
        // script, e um OnStart que dispara a cutscene de abertura precisa do
        // ponteiro ja la.
        m_ScriptWorld.SetSequenceWorld(&m_SequenceWorld);

        m_ScriptWorld.OnSceneStart(scene);

        // ── 5. Audio ─────────────────────────────────────────────────────
        //
        // DEPOIS do Capture do editor, sempre. Ver a nota no header.
        m_AudioWorld.OnScenePlay(scene);

        // ── 6. GameMode -> DefaultPawn -> GameCamera ─────────────────────
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

                            // ── BP_CAMERA_V1 ────────────────────────────────
                            //
                            // Tres campos do SpringArm nunca chegavam a
                            // GameCamera: SocketOffset, EnableCameraLag e
                            // (porque nao existia) o modo travado. Eles eram
                            // editaveis no Inspector e no Script Editor, e o
                            // jogo simplesmente os ignorava.
                            m_GameCamera.TPSocketOffset = sa->SocketOffset;
                            m_GameCamera.TPEnableLag = sa->EnableCameraLag;
                            m_GameCamera.TPLockRotation = (sa->Mode == CameraRigMode::Fixed);
                            m_GameCamera.TPLockYaw = sa->FixedYaw;
                            m_GameCamera.TPLockPitch = sa->FixedPitch;
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
                            // BP_CAMERA_V1 — o Play nascia SEMPRE em (-90, -10),
                            // ignorando o braco. Num braco travado isso valia um
                            // frame de camera no lugar errado antes do lerp
                            // corrigir; com lag desligado, valia a cena inteira
                            // comecando torta.
                            const float yaw0 = m_GameCamera.TPLockRotation
                                ? m_GameCamera.TPLockYaw : -90.0f;
                            const float pitch0 = m_GameCamera.TPLockRotation
                                ? m_GameCamera.TPLockPitch : -10.0f;

                            const glm::vec3 dir0 =
                                GameCamera::ForwardFromYawPitch(yaw0, pitch0);

                            glm::vec3 startPos = tc->Data.Position
                                + glm::vec3(0, m_GameCamera.TPHeight, 0)
                                - dir0 * m_GameCamera.TPDistance;

                            m_GameCamera.Reset(startPos, yaw0, pitch0);
                            m_GameCamera.SetTarget(&tc->Data.Position);
                        }
                        AXE_CORE_INFO("GameMode: pawn encontrado (entity {}), camera third person "
                            "[BP_CAMERA_V1 modo={} yaw={:.1f} pitch={:.1f} socket=({:.2f},{:.2f},{:.2f})].",
                            (uint32_t)m_PlayerEntity,
                            m_GameCamera.TPLockRotation ? "Fixed" : "Orbit",
                            m_GameCamera.TPLockYaw, m_GameCamera.TPLockPitch,
                            m_GameCamera.TPSocketOffset.x,
                            m_GameCamera.TPSocketOffset.y,
                            m_GameCamera.TPSocketOffset.z);
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
                {
                    // ── RADIANOS NO TRANSFORM, GRAUS NA GameCamera ───────────
                    //
                    // `Transform::Rotation` e radianos: `GetMatrix` faz
                    // `glm::quat(Rotation)`, que interpreta assim. Ja o
                    // `GameCamera::Reset` recebe GRAUS — o `CalcForward` dele
                    // chama `glm::radians(yaw)` internamente.
                    //
                    // Sem a conversao, uma camera girada 90 graus (1.5708 rad)
                    // entrava como 1.57 GRAUS: ela apontava quase exatamente
                    // para +X, independente de como o autor a tinha posicionado.
                    // Como o angulo pequeno e proximo de zero, o defeito passava
                    // por "a camera nasce olhando para o lado errado" em vez de
                    // parecer um erro de unidade.
                    //
                    // O editor_layer ja convertia no caminho irmao (o fallback
                    // pela camera do viewport, com `glm::degrees` nos dois
                    // argumentos). Era so este que estava fora.
                    m_GameCamera.Reset(tc->Data.Position,
                        glm::degrees(tc->Data.Rotation.y),
                        glm::degrees(tc->Data.Rotation.x));
                }
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

            // ── Cutscenes, POR ULTIMO ────────────────────────────────────
            //
            // Uma cutscene existe para SOBREPOR o comportamento normal. Se ela
            // rodasse antes, o AnimGraph voltaria a andar com o personagem no
            // meio da cena, a fisica empurraria a porta que ela abriu, e o
            // mouse mexeria a camera durante o plano. Quem escreve por ultimo
            // vence — e aqui isso e a regra, nao um acidente de ordem.
            m_SequenceWorld.OnUpdate(scene, deltaTime, &m_ScriptWorld);

            // ── A CAMERA DA CUTSCENE ─────────────────────────────────────
            //
            // Depois do m_GameCamera.OnUpdate pelo mesmo motivo: durante o
            // plano, quem enquadra e a sequence, nao o jogador.
            const entt::entity camE = m_SequenceWorld.GetActiveCameraEntity();

            if (camE != entt::null && scene.GetRegistry().valid(camE))
            {
                auto& reg = scene.GetRegistry();

                if (!m_CameraCutActive)
                {
                    m_CameraCutSavedFov = m_GameCamera.Fov;
                    m_CameraCutActive = true;
                }

                if (const auto* tc = reg.try_get<TransformComponent>(camE))
                {
                    // RADIANOS no Transform, GRAUS na GameCamera. Mesma
                    // fronteira do OnStart, e o mesmo motivo pelo qual ela e
                    // atravessada em um lugar so.
                    m_GameCamera.Reset(tc->Data.Position,
                        glm::degrees(tc->Data.Rotation.y),
                        glm::degrees(tc->Data.Rotation.x));
                }

                // O FOV vai junto: enquadramento visto com FOV errado nao e o
                // enquadramento.
                if (const auto* cc = reg.try_get<CameraComponent>(camE))
                {
                    m_GameCamera.Fov = cc->Fov;
                    m_GameCamera.NearClip = cc->NearClip;
                    m_GameCamera.FarClip = cc->FarClip;
                }
            }
            else if (m_CameraCutActive)
            {
                m_GameCamera.Fov = m_CameraCutSavedFov;
                m_CameraCutActive = false;
            }
        }

        // ═════════════════════════════════════════════════════════════════
        //  SOCKET_LAG_V1 — a arma escorregando da mao ao correr
        //
        //  ── O DEFEITO ─────────────────────────────────────────────────────
        //
        //  O AnimationWorld compoe a matriz do anexo de socket no fim do
        //  proprio OnUpdate — que roda ANTES dos scripts, da fisica e da
        //  sequence. Esses tres MOVEM o personagem. Ou seja: a arma era
        //  posicionada usando o lugar onde o personagem ESTAVA, e desenhada no
        //  frame em que ele ja estava adiante.
        //
        //  Parado, os dois lugares coincidem e nada aparece. Correndo, a
        //  diferenca e `velocidade x dt` — e por isso o escorregao cresce com
        //  a velocidade, que foi exatamente o relato.
        //
        //  ── POR QUE AQUI, E POR QUE DE NOVO ───────────────────────────────
        //
        //  Aqui e o ultimo ponto do frame em que alguem ainda mexe no
        //  personagem. A pose ja esta pronta desde o AnimationWorld e nao muda;
        //  o que mudou foi so a transform de mundo dele. Recompor custa uma
        //  varredura num view que costuma ter uma ou duas entidades.
        //
        //  A chamada de dentro do OnUpdate NAO foi removida: cinco previews do
        //  editor criam um AnimationWorld proprio e nunca chegam ate aqui.
        //  Tirar de la para "nao repetir" quebraria os cinco de uma vez.
        // ═════════════════════════════════════════════════════════════════
        if (!paused)
            m_AnimationWorld.UpdateSocketAttachments(scene);

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
        // ANTES dos scripts: o SequenceWorld devolve os TransformComponent que
        // a cutscene moveu, e no editor o Stop e seguido de um Ctrl+S possivel.
        // Uma porta que ficou aberta no ultimo frame da cutscene seria gravada
        // como a posicao autorada dela.
        m_SequenceWorld.OnSceneStop(scene);

        if (m_CameraCutActive)
        {
            m_GameCamera.Fov = m_CameraCutSavedFov;
            m_CameraCutActive = false;
        }

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