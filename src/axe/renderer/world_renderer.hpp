#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include "axe/renderer/scene_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/skybox_renderer.hpp"
#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/renderer/taa_pass.hpp"
#include "axe/graphics/renderer/ssr_pass.hpp"
#include "axe/graphics/renderer/sound_visualization_renderer.hpp"
#include "axe/scene/scene_environment.hpp"

#include <memory>

namespace axe
{
    class Scene;

    // ── WorldRenderer ────────────────────────────────────────────────────────
    //
    // Monta UM frame de jogo, do zero ao framebuffer final.
    //
    // POR QUE ESTA CLASSE EXISTE (SR2):
    //
    //   O `SceneRenderer` sempre foi limpo — nenhum imgui na arvore de
    //   includes dele. Mas quem sabia MONTAR um frame era o
    //   `ViewportRenderer`, que inclui <imgui.h> e <ImGuizmo.h> e carrega
    //   junto gizmo, picking, grid, collider debug e ghost de drag & drop.
    //
    //   O ramo `if (m_GameCamera)` daquele arquivo nao e editor: e o frame do
    //   jogo. Sem esta classe, o `game.exe` teria duas saidas, ambas ruins —
    //   duplicar ~150 linhas de montagem de frame (que divergiriam), ou linkar
    //   o ViewportRenderer inteiro e arrastar ImGuizmo para dentro do jogo.
    //
    //   E o mesmo padrao do B1, um andar abaixo: orquestracao de jogo dentro
    //   de uma classe de editor.
    //
    // O QUE ELE NAO FAZ, E NAO DEVE PASSAR A FAZER:
    //
    //   Grid, picking, gizmo, wireframe de collider, raio de point light e
    //   ghost de drag & drop. Todos sao ferramenta de autoria e continuam no
    //   `ViewportRenderer`. Se um deles aparecer aqui, a fronteira quebrou.
    //
    //   A visualizacao de som E excecao, e de proposito: e recurso de
    //   acessibilidade, existe PRA aparecer no jogo. Por isso esta aqui,
    //   atras de uma flag, e nao no lado do editor.
    //
    // ONDE O HUD VAI ENTRAR:
    //
    //   Depois do post-process, junto da visualizacao de som — ou seja, no
    //   framebuffer final, ja em espaco de tela. E aqui, e nao no
    //   `ViewportRenderer`, porque o HUD tem que existir no jogo empacotado.
    class AXE_API WorldRenderer
    {
    public:
        // Exige contexto OpenGL vivo: cria framebuffer, passes e shaders.
        void Initialize();

        void Resize(std::uint32_t width, std::uint32_t height);

        struct FrameParams
        {
            Scene* WorldScene = nullptr;
            SceneEnvironment* Environment = nullptr;

            // View/projection ja resolvidas pelo chamador.
            //
            // Recebe matrizes em vez de uma camera de proposito: assim o
            // WorldRenderer nao precisa saber se por tras dela ha uma
            // GameCamera, uma camera de cinematica ou uma camera de replay.
            glm::mat4 View{ 1.0f };
            glm::mat4 Projection{ 1.0f };
            glm::vec3 EyePosition{ 0.0f };

            std::uint32_t Width = 0;
            std::uint32_t Height = 0;

            // Relogio da engine. Alimenta o ciclo de Time of Day; o
            // WorldRenderer deriva o dt internamente.
            float TimeSeconds = 0.0f;

            // Acessibilidade — ver a nota no topo da classe.
            bool ShowSoundVisualization = false;
        };

        // Sequencia, identica a que o ViewportRenderer executava no ramo de
        // GameCamera (a ordem nao e negociavel; cada passo depende do
        // anterior):
        //
        //   resize HDR/post -> sync de environment e ceu procedural ->
        //   bind HDR + clear -> le PostProcessComponent -> skybox ->
        //   [TAA jitter] -> RenderScene -> unbind ->
        //   SSR -> TAA resolve -> post-process no alvo ->
        //   [visualizacao de som]
        void RenderToFramebuffer(Framebuffer& target, const FrameParams& params);

        // Mesma sequencia, mas o post-process final sai na TELA.
        //
        // Existe porque o jogo nao tem para onde renderizar senao o backbuffer,
        // e nao ha objeto `Framebuffer` que o represente — `Unbind()` e o que
        // binda o 0.
        //
        // A alternativa seria o jogo criar um framebuffer e copiar para a tela
        // depois: uma passada de tela cheia por frame, para nada.
        void RenderToScreen(const FrameParams& params);

        SceneRenderer* GetSceneRenderer() { return m_SceneRenderer.get(); }

    private:
        // Sincroniza HDRI/rotacao do skybox e roda o ciclo de Time of Day.
        //
        // ATENCAO — este passo ESCREVE no LightComponent da cena (Hour,
        // Direction, Color, Intensity). Isso e simulacao morando no
        // renderer, e a rigor pertence a um sistema de tempo do
        // `SceneRuntime`. Ficou aqui porque e exatamente como o
        // ViewportRenderer sempre fez, e mudar semantica no mesmo patch em
        // que se move codigo e a receita para um bug que ninguem consegue
        // atribuir. Registrado como divida; ver as notas do SR2.
        void SyncEnvironment(Scene& scene, SceneEnvironment* env, float timeSeconds);

        // O corpo de verdade dos dois pontos de entrada acima.
        //
        // `target` nulo = backbuffer. Um unico caminho, para que o frame do
        // editor em Play e o frame do jogo nao possam divergir — que e a razao
        // de esta classe existir.
        void RenderInternal(Framebuffer* target, const FrameParams& params);

        std::unique_ptr<SceneRenderer>   m_SceneRenderer;
        SkyboxRenderer                   m_SkyboxRenderer;
        SoundVisualizationRenderer       m_SoundVisualization;

        std::shared_ptr<Framebuffer>     m_HDRFramebuffer;
        std::shared_ptr<PostProcessPass> m_PostProcess;
        std::shared_ptr<TAAPass>         m_TAAPass;
        std::shared_ptr<SSRPass>         m_SSRPass;

        TAASettings          m_TAASettings;
        SSRSettings          m_SSRSettings;
        PostProcessSettings  m_PostProcessSettings;

        float m_LastTimeSeconds = 0.0f;
    };

} // namespace axe