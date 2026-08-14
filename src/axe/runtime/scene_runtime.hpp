#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include "axe/physics/physics_world.hpp"
#include "axe/script/script_world.hpp"
#include "axe/particles/particle_world.hpp"
#include "axe/audio/audio_world.hpp"
#include "axe/animation/animation_world.hpp"
#include "axe/graphics/game_camera.hpp"

#include <entt/entt.hpp>
#include <string>

namespace axe
{
    class Scene;
    class Window;

    // ── SceneRuntime ─────────────────────────────────────────────────────────
    //
    // Dono dos cinco mundos de simulacao e da batuta que os rege.
    //
    // POR QUE ESTA CLASSE EXISTE (SR1):
    //
    //   Ate aqui os mundos eram membros do EditorLayer, e o EditorLayer::
    //   OnUpdate era quem os chamava na ordem certa. Isso funciona enquanto
    //   so existe o editor — e para de funcionar no dia em que houver um
    //   `game.exe`, porque a orquestracao do jogo estaria dentro de uma
    //   classe que o jogo nao linka.
    //
    //   Extrair para ca nao e reorganizacao cosmetica: o Play do editor passa
    //   a rodar EXATAMENTE o mesmo objeto que o jogo empacotado vai rodar. O
    //   caminho de runtime deixa de ser um caminho que so seria exercitado no
    //   dia do empacotamento, e passa a ser testado todo dia, de graca.
    //
    // O QUE NAO ENTRA AQUI:
    //
    //   SceneSnapshot (Capture/Restore) e servico de EDITOR — existe para
    //   desfazer o Play, que e conceito de editor. Um jogo nunca desfaz o
    //   Play. Mesma coisa para selecao, gizmos, CommandHistory, captura de
    //   cursor e flags de ImGui. Tudo isso fica no EditorLayer.
    //
    //   Tambem nao entra ProjectManager. O runtime nao sabe o que e um
    //   `.axeproject`: o GameMode ativo chega por INJECAO, via StartConfig.
    //   Hoje quem injeta e o editor (lendo o ProjectManager); amanha e o
    //   manifesto de empacotamento (B3 do PACKAGING_READINESS). A fronteira
    //   ja fica pronta para os dois.
    //
    // REGRA DE OURO PARA FEATURE NOVA:
    //
    //   Sistema novo que precisa tickar por frame se pendura AQUI, nunca no
    //   EditorLayer. Vale em especial para a GUI/HUD do jogo, que e a
    //   proxima feature planejada.
    class AXE_API SceneRuntime
    {
    public:
        SceneRuntime() = default;

        // SR1b — rede de seguranca de ultima linha.
        //
        // Os callbacks do Jolt vivem no PhysicsSystem, que e SINGLETON:
        // sobrevive a este objeto. Eles capturam `this` e a `Scene`. Um
        // SceneRuntime destruido sem OnStop deixa duas referencias mortas
        // registradas num singleton — e o estouro so acontece na proxima
        // colisao, longe da causa.
        //
        // O destrutor nao substitui o OnStop (nao para scripts nem audio);
        // ele so garante que o cenario acima nao exista.
        ~SceneRuntime();

        // Nao copiavel nem movivel de proposito: os callbacks registrados no
        // PhysicsSystem apontam para ESTE endereco. Uma copia teria os
        // mundos duplicados com o singleton apontando para o original; um
        // move deixaria o singleton apontando para a casca vazia. Os dois
        // casos sao silenciosos ate a primeira colisao.
        SceneRuntime(const SceneRuntime&) = delete;
        SceneRuntime& operator=(const SceneRuntime&) = delete;
        SceneRuntime(SceneRuntime&&) = delete;
        SceneRuntime& operator=(SceneRuntime&&) = delete;

        // ── Servicos de processo ─────────────────────────────────────────
        //
        // Input e Audio sao singletons de processo, nao de cena: vivem do
        // boot ao shutdown e atravessam qualquer numero de Play/Stop.
        //
        // Estavam no EditorLayer::OnAttach / OnDetach, e o KNOWN_LIMITATIONS
        // ja registrava a falta de um ponto de init standalone para o
        // AudioEngine. Este e o ponto. O `main` do jogo chama estes dois e
        // nada mais precisa saber sobre inicializacao de subsistema.
        //
        // A PhysicsSystem NAO entra aqui de proposito: ela se inicializa
        // sozinha no PhysicsWorld::OnSceneStart, e mudar isso seria alterar
        // comportamento em um patch cuja unica funcao e mover codigo.
        static void InitializeServices(Window* window);
        static void ShutdownServices();

        // ── Start ────────────────────────────────────────────────────────
        struct StartConfig
        {
            // UUID do GameMode ativo. Vazio = sem GameMode; nenhum pawn e
            // resolvido e a camera fica com o que a cena disser.
            std::string GameModeUUID;

            // Window de onde a GameCamera le mouse/teclado. Pode ser o
            // mesmo ponteiro passado ao InitializeServices.
            Window* InputWindow = nullptr;
        };

        struct StartResult
        {
            // Entidade resolvida como DefaultPawn do GameMode, ou null.
            entt::entity PlayerEntity{ entt::null };

            // true se a cena tinha ao menos um CameraComponent. O editor usa
            // isto para decidir se cai no fallback de posicionar a
            // GameCamera onde a camera do viewport estava — decisao que e
            // dele, nao do runtime (o jogo nao tem camera de viewport).
            bool HasSceneCamera = false;
        };

        // Ordem interna, preservada do EditorLayer::EnterPlay:
        //
        //   1. callbacks do Jolt -> ScriptWorld  (ANTES do OnSceneStart, para
        //      que os bodies ja criados sejam cobertos)
        //   2. PhysicsWorld::OnSceneStart
        //   3. ScriptWorld: injeta camera, depois OnSceneStart
        //   4. AudioWorld::OnScenePlay
        //   5. resolve DefaultPawn e configura a GameCamera
        //
        // CUIDADO DE CHAMADA: no editor, o SceneSnapshot::Capture tem que
        // acontecer ANTES desta funcao. O passo 4 dispara as fontes com
        // PlayOnStart, e o snapshot precisa guardar a cena sem nenhuma voice
        // viva — senao o Restore devolve handles de voices ja mortas.
        StartResult OnStart(Scene& scene, const StartConfig& cfg);

        // ── Tick ─────────────────────────────────────────────────────────
        enum class TickMode
        {
            // Jogo rodando. Unico modo que o `game.exe` usa.
            Play,

            // Editor em Edit: particulas e animacao tickam como preview ao
            // vivo, script e fisica nao rodam, audio da cena fica mudo.
            EditPreview,

            // Editor em Pause: particulas e animacao congelam, audio pausa.
            Paused,
        };

        struct ListenerPose
        {
            glm::vec3 Position{ 0.0f };
            glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
            glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
        };

        struct TickContext
        {
            TickMode Mode = TickMode::Play;

            // De onde a GameCamera le input. So usado em Play.
            Window* InputWindow = nullptr;

            // Pose de fallback do listener quando a cena nao tem
            // AudioListenerComponent. Nulo = deriva da GameCamera.
            //
            // O editor injeta a camera do viewport em Edit/Pause: o ouvido
            // do usuario esta onde ele esta olhando, nao onde a camera de
            // jogo parou.
            const ListenerPose* ListenerOverride = nullptr;

            // Posicao usada no LOD de particula. Nulo = GameCamera.
            const glm::vec3* LodCameraPosition = nullptr;
        };

        // Ordem do frame — a mesma que estava documentada no EditorLayer, e
        // que agora e imposta aqui:
        //
        //   Particle -> Animation -> [Play] GameCamera -> [Play] Script
        //     -> [Play] Physics -> Audio -> reap de voices
        //
        //   Animacao ANTES do render porque o SceneCollector le a BonePalette
        //   que este update acabou de escrever; um frame de atraso aparece
        //   como tremida sutil em movimento rapido.
        //
        //   Audio POR ULTIMO porque uma fonte 3D presa a um personagem le a
        //   posicao do transform: rodar antes da fisica daria panning do
        //   frame anterior. O render nao depende do audio.
        void OnUpdate(Scene& scene, float deltaTime, const TickContext& ctx);

        // ── Stop ─────────────────────────────────────────────────────────
        //
        // Ordem preservada do EditorLayer::EnterEdit. O AudioEngine::StopAll
        // depois do AudioWorld::OnSceneStop cobre o que nao pertence a fonte
        // nenhuma — one-shot de notify em voo no instante do Stop.
        //
        // A AnimationWorld nao tem OnSceneStop e nao ganha um aqui: os FX de
        // notify que ela spawna morrem com o restore do snapshot.
        void OnStop(Scene& scene);

        // ── Acesso ───────────────────────────────────────────────────────
        //
        // A GameCamera mora aqui porque e estado de JOGO, nao de editor: o
        // jogo empacotado precisa de uma e nao tem EditorLayer para hospeda-la.
        // O editor a le para alimentar o ViewportRenderer e para capturar o
        // cursor.
        GameCamera& GetGameCamera() { return m_GameCamera; }
        const GameCamera& GetGameCamera() const { return m_GameCamera; }

        entt::entity GetPlayerEntity() const { return m_PlayerEntity; }

        // Exposto porque despachar evento para uma entidade e necessidade
        // legitima de fora do tick (ferramenta de editor, debug). Os outros
        // quatro mundos NAO sao expostos: quem os chamar de fora estaria
        // recriando exatamente a orquestracao paralela que este patch veio
        // eliminar.
        ScriptWorld& GetScriptWorld() { return m_ScriptWorld; }

        // Deriva pose de listener de uma view matrix.
        //
        // A view matrix ja e a inversa da pose, entao transpor a parte
        // rotacional devolve os eixos do mundo. Publico e estatico porque o
        // editor precisa da MESMA conta para a camera do viewport, e duas
        // copias divergiriam.
        static ListenerPose PoseFromView(const glm::vec3& position,
            const glm::mat4& view);

    private:
        PhysicsWorld   m_PhysicsWorld;
        ScriptWorld    m_ScriptWorld;
        ParticleWorld  m_ParticleWorld;
        AudioWorld     m_AudioWorld;
        AnimationWorld m_AnimationWorld;

        GameCamera     m_GameCamera;
        entt::entity   m_PlayerEntity{ entt::null };

        // Instala/desinstala os callbacks do Jolt, com dono explicito.
        //
        // O "dono" existe porque o PhysicsSystem e singleton e o registro
        // e global: sem ele, um SceneRuntime secundario sendo destruido
        // arrancaria os callbacks do runtime ATIVO — e nenhuma colisao
        // seria despachada dali em diante, sem erro nenhum no log.
        //
        // Hoje so ha um SceneRuntime, entao isto e prevencao. Mas janelas
        // de preview ja tem cena e mundos proprios; o dia em que uma delas
        // quiser fisica, o bug estaria pronto e seria dificil de achar.
        void InstallPhysicsCallbacks(Scene& scene);
        void UninstallPhysicsCallbacks();

        static SceneRuntime* s_PhysicsCallbackOwner;
    };

} // namespace axe