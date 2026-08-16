// main.cpp — ponto de entrada do JOGO.
//
// ── O QUE ESTE ARQUIVO PROVA ─────────────────────────────────────────────────
//
// O SR1 extraiu o `SceneRuntime` e o SR2 o `WorldRenderer`, os dois com o mesmo
// argumento: o Play do editor passaria a rodar EXATAMENTE o objeto que o jogo
// vai rodar. Ate agora isso era so um argumento — nenhum executavel fora do
// editor tinha exercitado nenhum dos dois.
//
// Este arquivo e a verificacao. Se o jogo abre e o personagem anima, a extracao
// estava certa. Se falta alguma coisa, ela aparece aqui, curta e legivel, em vez
// de aparecer no meio do empacotamento.
//
// ── O QUE ELE NAO E ──────────────────────────────────────────────────────────
//
// Nao ha HUD, menu, tela de carregamento ou tratamento de fim de jogo. Nao e
// descuido: cada coisa a mais e uma variavel a mais quando algo nao funcionar, e
// este executavel existe para responder UMA pergunta.
//
// ── FRONTEIRA ────────────────────────────────────────────────────────────────
//
// Nada aqui inclui imgui, assimp ou qualquer header de `src/editor/`. Se um dia
// um include desses aparecer neste arquivo, o empacotamento voltou a quebrar —
// e este comentario e o aviso.

#include "axe/axe_window/window.hpp"
#include "axe/graphics/graphics_device.hpp"
#include "axe/runtime/scene_runtime.hpp"
#include "axe/renderer/world_renderer.hpp"

#include "axe/scene/scene.hpp"
#include "axe/scene/scene_serializer.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/input/input.hpp"
#include "axe/input/key_codes.hpp"
#include "axe/input/input_mapping.hpp"
#include "axe/events/application_event.hpp"
#include "axe/core/time.hpp"
#include "axe/log/log.hpp"

#include <filesystem>
#include <string>
#include <memory>

namespace fs = std::filesystem;

namespace
{
    // ── Onde esta o projeto ──────────────────────────────────────────────────
    //
    // Dois usuarios em fases diferentes, cada um querendo o oposto:
    //
    //   voce, agora     — rodar contra a pasta de desenvolvimento sem copiar
    //                     nada. Sem argumento, seria preciso duplicar os assets
    //                     a cada teste, e ai o teste nao seria do projeto real.
    //
    //   o jogador, depois — duplo clique, tudo ao lado do executavel.
    //
    // O argumento vence QUANDO VALIDO; senao, a pasta do executavel. Aceitar o
    // argumento sem validar significaria que arrastar um arquivo em cima do exe
    // desviaria o carregamento em silencio.
    fs::path FindProjectFile(int argc, char** argv, const fs::path& exeDir)
    {
        auto firstProjectIn = [](const fs::path& dir) -> fs::path
            {
                std::error_code ec;

                if (!fs::is_directory(dir, ec))
                    return {};

                for (const auto& e : fs::directory_iterator(dir, ec))
                    if (e.is_regular_file(ec) && e.path().extension() == ".axeproject")
                        return e.path();

                return {};
            };

        if (argc > 1)
        {
            const fs::path arg = argv[1];
            std::error_code ec;

            // O argumento pode ser o .axeproject direto ou a pasta que o contem.
            if (fs::is_regular_file(arg, ec) && arg.extension() == ".axeproject")
                return arg;

            if (const fs::path found = firstProjectIn(arg); !found.empty())
                return found;

            AXE_CORE_WARN("Game: '{}' has no .axeproject - falling back to the "
                "executable folder.", arg.string());
        }

        return firstProjectIn(exeDir);
    }
}

int main(int argc, char** argv)
{
    axe::InfoLog::Init();

    // ── Carimbo de build ─────────────────────────────────────────────────────
    //
    // PRIMEIRA linha do log, antes de qualquer outra coisa.
    //
    // O `game` NAO e dependencia de build do `editor` na solution: compilar ou
    // rodar o editor (F5, ou Build no projeto editor) reconstroi `axe.dll` e
    // `editor.exe` e NAO toca no `game.exe`. O empacotador entao copia um
    // executavel velho ao lado de uma DLL nova — e como quase toda a engine
    // vive na DLL, o pacote FUNCIONA, so que sem as mudancas que estavam no
    // `main.cpp`. Foi exatamente o que aconteceu com a captura do mouse do
    // PKG5: material e iluminacao (axe.dll) corrigidos, camera (game.exe)
    // parada, e nada no log explicando a contradicao.
    //
    // __DATE__/__TIME__ sao do momento da COMPILACAO deste arquivo. Se a data
    // nao bate com o seu ultimo build, o executavel e velho — e a duvida
    // acaba na primeira linha do log, em vez de virar uma sessao de
    // investigacao.
    AXE_CORE_INFO("Game: build {} {}", __DATE__, __TIME__);

    const fs::path exeDir = fs::path(argv[0]).parent_path();
    const fs::path projectFile = FindProjectFile(argc, argv, exeDir);

    if (projectFile.empty())
    {
        // Dizer ONDE procurou. Sem isso, quem nao escreveu este codigo nao
        // sabe se errou a pasta ou se o arquivo sumiu.
        AXE_CORE_ERROR("Game: no .axeproject found in '{}'. Pass the project "
            "folder as an argument, or put the game next to one.",
            (argc > 1 ? std::string(argv[1]) : exeDir.string()));
        return 1;
    }

    // SEMPRE logado. Sem isto, "por que abriu a cena errada" vira meia hora de
    // investigacao — e com duas fontes possiveis de projeto, isso acontece.
    AXE_CORE_INFO("Game: loading project '{}'", projectFile.string());

    // ── Janela e contexto grafico ────────────────────────────────────────────
    std::unique_ptr<axe::Window> window(axe::Window::Create());

    if (!window || !window->GetNativeWindow())
    {
        AXE_CORE_ERROR("Game: failed to create the window.");
        return 1;
    }

    axe::GraphicsDevice graphics;

    if (!graphics.Initialize(window.get()))
    {
        AXE_CORE_ERROR("Game: failed to initialize the graphics device.");
        return 1;
    }

    graphics.SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    bool running = true;

    window->SetEventCallback([&running](axe::Event& e)
        {
            if (e.GetEventType() == axe::EventType::WindowClose)
                running = false;
        });

    // ── Servicos ─────────────────────────────────────────────────────────────
    //
    // UMA chamada. E o ponto de init standalone que o KNOWN_LIMITATIONS pedia
    // para o AudioEngine e que o SR1 criou — este e o primeiro consumidor real
    // dele fora do editor.
    axe::SceneRuntime::InitializeServices(window.get());

    // ── Projeto e assets ─────────────────────────────────────────────────────
    //
    // `recordAsRecent = false` — o jogo NAO mexe nas preferencias do editor.
    //
    // Elas vivem num arquivo unico por maquina
    // (%APPDATA%/AXEEngine/editor_prefs.json), compartilhado por qualquer
    // executavel que use o ProjectManager. Com o default `true`, cada vez que
    // este jogo rodava ele gravava a copia EMPACOTADA do projeto como "ultimo
    // projeto" — e o editor, no boot seguinte, abria a pasta de build em vez
    // do projeto de trabalho. O sintoma nao apontava para ca: o Package
    // recusava o destino ("the output folder is inside the project folder"),
    // porque a saida do build era, naquele momento, a raiz do projeto aberto.
    //
    // O jogador tambem nao tem "projetos recentes" — nem esta lista deveria
    // existir para ele.
    if (!axe::ProjectManager::Get().OpenProject(projectFile, /*recordAsRecent*/ false))
    {
        AXE_CORE_ERROR("Game: failed to open the project.");
        return 1;
    }

    const axe::Project& project = axe::ProjectManager::Get().GetCurrent();

    // Por enquanto o jogo le a pasta de assets do projeto, como o editor. E o
    // que o B3 vai substituir por um manifesto — e e de proposito que isso
    // fique para depois: uma variavel de cada vez.
    axe::AssetDatabase::Get().Load(project.RootPath);

    // ── Mapeamentos de input ─────────────────────────────────────────────────
    //
    // Os bindings vivem em `<projeto>/InputConfig.json`, na RAIZ — fora de
    // `Assets/`, como as DLLs de script.
    //
    // No editor quem carrega isso e a janela de Input Settings, no momento em
    // que o projeto abre. O jogo nao tem essa janela, e sem esta chamada ele
    // sobe com ZERO mapeamentos: os scripts rodam, o pawn existe, e nada
    // responde ao teclado. Foi o sintoma do segundo build.
    //
    // A ausencia do arquivo e AVISO e nao erro: um projeto pode legitimamente
    // nao ter input configurado ainda. Mas o jogo precisa dizer isso, porque o
    // sintoma (controles mortos) nao aponta para um arquivo faltando.
    {
        const fs::path inputCfg = project.RootPath / "InputConfig.json";

        std::error_code iec;

        if (fs::exists(inputCfg, iec))
        {
            if (axe::InputMappingConfig::Get().Load(inputCfg))
                AXE_CORE_INFO("Game: input mappings loaded.");
            else
                AXE_CORE_WARN("Game: could not read '{}' - controls will not respond.",
                    inputCfg.string());
        }
        else
        {
            AXE_CORE_WARN("Game: no InputConfig.json in the project root - "
                "controls will not respond.");
        }
    }

    if (project.StartScene.empty())
    {
        AXE_CORE_ERROR("Game: the project has no start scene set.");
        return 1;
    }

    axe::Scene scene;
    axe::SceneEnvironment environment;

    const fs::path scenePath = project.RootPath / project.StartScene;

    if (!axe::SceneSerializer::Deserialize(scenePath, scene, &environment))
    {
        AXE_CORE_ERROR("Game: failed to load the start scene '{}'.", scenePath.string());
        return 1;
    }

    AXE_CORE_INFO("Game: start scene '{}' loaded.", project.StartScene);

    // ── Runtime e renderer ───────────────────────────────────────────────────
    axe::SceneRuntime runtime;
    axe::WorldRenderer worldRenderer;

    worldRenderer.Initialize();

    axe::SceneRuntime::StartConfig startCfg;
    startCfg.GameModeUUID = project.ActiveGameModeUUID;
    startCfg.InputWindow = window.get();

    runtime.OnStart(scene, startCfg);

    // ── Captura do mouse ─────────────────────────────────────────────────────
    //
    // `GameCamera::OnUpdate` comeca com `if (!MouseCaptured || !window) return;`
    // — sem esta flag ela nao le o mouse E NAO SEGUE O ALVO, porque o follow
    // acontece depois daquele return. Foi o sintoma do terceiro build: o
    // personagem andava com WASD, a camera apontava para ele e ficava parada.
    //
    // No editor quem liga isso e o `EnterPlay`. O jogo nao tem Play — ele JA
    // esta em play desde o primeiro frame, e por isso liga aqui.
    //
    // `CaptureCursor` esconde e prende o ponteiro: sem isso, mirar levaria o
    // cursor para fora da janela e o clique cairia no que estivesse atras.
    runtime.GetGameCamera().MouseCaptured = true;
    runtime.GetGameCamera().m_FirstMouse = true;   // evita o salto do 1o delta
    window->CaptureCursor(true);

    // ── Diagnostico da camera ────────────────────────────────────────────────
    //
    // Tres perguntas que estavamos respondendo por adivinhacao, respondidas de
    // uma vez, no log, todo boot:
    //
    //   captured=false          → esta linha nao rodou (executavel velho)
    //   mode=FreeFly            → o GameMode nao resolveu o pawn/spring arm
    //   target=false            → ThirdPerson sem alvo: cai no FreeFly
    //   mouseRotates=false      → o Spring Arm do pawn tem MouseRotates
    //                             desligado, e a camera IGNORA o mouse de
    //                             proposito (ver UpdateThirdPerson)
    //   sensitivity=0           → le o mouse e nao gira: delta x 0 = 0
    {
        const axe::GameCamera& cam = runtime.GetGameCamera();

        AXE_CORE_INFO("Game: camera captured={} mode={} target={} "
            "mouseRotates={} sensitivity={} yaw={} pitch={}",
            cam.MouseCaptured,
            cam.CameraMode == axe::GameCamera::Mode::ThirdPerson ? "ThirdPerson" : "FreeFly",
            cam.HasTarget(),
            cam.TPMouseRotates,
            cam.Sensitivity,
            cam.GetYaw(),
            cam.GetPitch());
    }

    // ── Loop ─────────────────────────────────────────────────────────────────
    //
    // Mesma sequencia do EditorApp::Run, sem as layers e sem o ImGui.
    float lastFrameTime = window->GetTime();

    while (running)
    {
        const float now = window->GetTime();
        axe::Time::SetElapsed(now);          // fonte de tempo unica da engine
        const float deltaTime = now - lastFrameTime;
        lastFrameTime = now;

        window->PollEvents();
        axe::Input::Update(deltaTime);

        // PROVISORIO — Esc fecha.
        //
        // Um jogo de verdade decide o que Esc faz; aqui ele existe porque este
        // executavel vai ser aberto e fechado dezenas de vezes durante os
        // testes, e exigir Alt+F4 em todas atrapalha exatamente a fase em que
        // ele e util. Sai quando houver menu.
        if (window->IsKeyDown((int)axe::Key::Escape))
        {
            // Solta o cursor ANTES de fechar. Sem isto, um crash ou um
            // fechamento no meio do frame deixaria o ponteiro preso e
            // invisivel no desktop.
            window->CaptureCursor(false);
            running = false;
        }

        // ── O objeto do SR1 ──────────────────────────────────────────────────
        axe::SceneRuntime::TickContext tick;
        tick.Mode = axe::SceneRuntime::TickMode::Play;
        tick.InputWindow = window.get();

        runtime.OnUpdate(scene, deltaTime, tick);

        // ── O objeto do SR2 ──────────────────────────────────────────────────
        int fbWidth = 0, fbHeight = 0;
        window->GetFramebufferSize(fbWidth, fbHeight);

        // Janela minimizada: pular o frame.
        //
        // Sem isto, o resize do framebuffer para 0x0 e a divisao do aspect
        // ratio por zero acontecem toda vez que alguem minimiza — e o jogo
        // morre num lugar sem relacao aparente com a causa.
        if (fbWidth > 0 && fbHeight > 0)
        {
            graphics.BeginFrame();

            axe::WorldRenderer::FrameParams frame;
            frame.WorldScene = &scene;
            frame.Environment = &environment;

            const axe::GameCamera& cam = runtime.GetGameCamera();
            const float aspect = (float)fbWidth / (float)fbHeight;

            frame.View = cam.GetViewMatrix();
            frame.Projection = cam.GetProjectionMatrix(aspect);
            frame.EyePosition = cam.GetPosition();
            frame.Width = (std::uint32_t)fbWidth;
            frame.Height = (std::uint32_t)fbHeight;
            frame.TimeSeconds = now;
            frame.ShowSoundVisualization = false;

            worldRenderer.RenderToScreen(frame);

            graphics.EndFrame();
        }

        window->SwapBuffers();
    }

    // ── Desligamento ─────────────────────────────────────────────────────────
    //
    // OnStop antes do ShutdownServices: e ele quem mata as voices e descarrega
    // os scripts. Fechar o device de audio com voice viva significa fechar com
    // dado em uso — a mesma razao pela qual o EditorLayer::OnDetach faz nesta
    // ordem.
    runtime.OnStop(scene);
    axe::SceneRuntime::ShutdownServices();

    graphics.Shutdown();

    return 0;
}