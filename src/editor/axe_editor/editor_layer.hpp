#pragma once

#include "axe/layers/layer.hpp"
#include "editor_ui.hpp"
#include "file_dialog.hpp"
#include "axe/core/command_history.hpp"

// SR1 — os cinco mundos de simulacao deixaram de ser membros desta classe e
// passaram a viver dentro do SceneRuntime, que este header inclui. As
// inclusoes individuais de physics_world / particle_world / audio_world /
// animation_world / script_world sairam daqui: quem precisa delas e o
// runtime, nao o editor.
#include "axe/runtime/scene_runtime.hpp"


#include "editor/axe_editor/viewport_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/mesh/mesh_factory.hpp"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>

#include "axe/graphics/editor_camera.hpp"
#include "editor_context.hpp"

#include "editor/axe_editor/import/mesh_loader.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/asset/asset_database.hpp"
#include "editor/axe_editor/script/script_asset.hpp"
#include "axe/script/script_component.hpp"
#include "axe/input/input.hpp"
#include "axe/scene/game_mode_asset.hpp"
#include "axe/project/project_manager.hpp"
#include <filesystem>

#include "axe/scene/scene_serializer.hpp"
#include "editor_icon_library.hpp"

#include "axe/events/key_event.hpp"
#include "axe/input/key_codes.hpp"

#include "editor/axe_editor/material/material_editor_window.hpp"
#include "node_graph/material_graph.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

#include "material_thumbnail_renderer.hpp"
#include "mesh_thumbnail_renderer.hpp"

#include "axe/scene/scene_snapshot.hpp"

#include "axe_editor/animation/sequencer/sequencer_window.hpp"

namespace axe
{
    //class SequencerWindow;

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;

        // FRAME_SELECTED_V1 — enquadra a camera do viewport na entidade
        // selecionada (tecla F, como na Unreal e no Blender).
        void FrameSelectedEntity();
        void OnRender() override;
        void OnEvent(Event& e) override;


    private:
        // ── Estados do editor ──────────────────────────────────────────────────
        enum class EditorState { Edit, Play, Pause };
        EditorState m_EditorState = EditorState::Edit;

        // ── Cena ──────────────────────────────────────────────────────────────
        std::unique_ptr<Scene>  m_Scene;
        std::string             m_CurrentScenePath;
        // Snapshot de Play/Stop — CLONE DE MEMORIA, nao JSON.
        //
        // Antes era uma std::string com a cena serializada. O round-trip por
        // JSON descartava em silencio tudo que o SceneSerializer nao
        // conhecesse — e o dado perdido era o da EDICAO, feito antes do Play.
        SceneSnapshot           m_SceneSnapshot;
        bool                    m_SceneLoaded = false;
        SceneEnvironment        m_Environment;
        SequencerWindow         m_SequencerWindow;

        // ── Renderer / UI ─────────────────────────────────────────────────────
        std::unique_ptr<axe::ViewportRenderer> m_ViewportRenderer;
        std::unique_ptr<EditorUI>              m_EditorUI;
        EditorContext                          m_Context;
        CommandHistory                         m_CommandHistory;
        MaterialThumbnailRenderer              m_ThumbnailRenderer;
        // SC23 — miniaturas de malha/esqueleto/animacao/script no Asset Browser.
        MeshThumbnailRenderer                  m_MeshThumbnails;

        // ── Runtime ───────────────────────────────────────────────────────────
        //
        // SR1 — aqui moravam PhysicsWorld, ScriptWorld, ParticleWorld,
        // AudioWorld, AnimationWorld, m_PlayerEntity e m_GameCamera, todos
        // como membros diretos desta classe.
        //
        // Sete membros de estado de JOGO dentro de uma classe de EDITOR. O
        // custo disso nao era de organizacao: era que a ordem de update, o
        // ciclo start/stop e a resolucao de GameMode viviam num objeto que um
        // `game.exe` nao linkaria nunca. Cada sistema novo pendurado aqui era
        // mais um no a desatar no dia do empacotamento.
        //
        // Agora o Play do editor roda EXATAMENTE o objeto que o jogo vai
        // rodar. Ver a nota longa no topo de scene_runtime.hpp.
        //
        // O que ficou deste lado, e por que: o SceneSnapshot logo acima. Ele
        // existe para DESFAZER o Play — conceito que so o editor tem.
        SceneRuntime m_Runtime;

        // ── FPS ───────────────────────────────────────────────────────────────
        float m_DeltaTime = 0.0f;
        float m_FPS = 0.0f;
        float m_FPSAccumulator = 0.0f;
        int   m_FPSSamples = 0;

        // ── Input state ───────────────────────────────────────────────────────
        bool m_EscWasPressed = false;

        // ── Métodos privados ──────────────────────────────────────────────────
        void DrawPlayToolbar();
        void HandleSceneInput();
        void HandleViewportCameraInput();
        // ── ENV_RESPECT_DELETE_V1 ────────────────────────────────────────
        //
        //  `createIfMissing` false = so NORMALIZA o que ja existe (tira o
        //  FolderComponent, devolve o EnvironmentComponent a uma entidade
        //  "Enviroment" que o perdeu) e nunca cria do zero.
        //
        //  Existe porque esta funcao era chamada em quatro pontos com dois
        //  significados diferentes espremidos num so. Nos dois pontos onde a
        //  cena esta NASCENDO (cena nova, bootstrap do projeto) criar e o
        //  certo. Nos outros dois — ao CARREGAR do disco e ao voltar do Play —
        //  a cena ja e a intencao do autor, e criar ali fazia apagar a
        //  entidade "Enviroment" nunca pegar: ela voltava no proximo reload, e
        //  voltava tambem em todo Stop, desfazendo o snapshot que a tinha
        //  removido corretamente.
        void EnsureEnvironmentComponent(bool createIfMissing = true);

        // Cria a entidade do personagem a partir de um asset .axeskel.
        //
        // Existe como METODO, e nao inline nos callbacks, porque ha DOIS
        // caminhos que instanciam asset: o duplo-clique no Asset Browser e o
        // arrasto pra viewport. Eles sao lambdas separadas, e eu havia
        // implementado o SkeletalMesh so em uma — o arrasto caía no fallback
        // de mesh estatica e mandava o .axeskel (JSON) pro Assimp.
        //
        // Com um metodo unico, os dois caminhos nao podem mais divergir.
        void SpawnSkeletalMesh(const AssetRecord& record, const std::string& uuid);

        // Reaplica os componentes de um .axescript em TODAS as instancias
        // dele que ja estao na cena — o "compilar Blueprint" da Unreal.
        // Devolve quantas entidades foram atualizadas.
        int SyncScriptInstances(const std::filesystem::path& scriptPath);

        // SC46 — reconcilia as entidades-filhas de anexo a socket de UMA
        // instancia com o que o Blueprint pede. Casamento por nome de socket;
        // cria o que falta, remove o que saiu, preserva o que ja existe.
        void SyncSocketAttachments(entt::entity root, ScriptAsset& scriptAsset);

        // FBX/DAE/GLTF de animacao -> abre o Animation Editor no clipe.
        // Chamado por TODOS os caminhos de abrir/instanciar asset.
        // true = abriu (o chamador deve retornar).
        bool TryOpenAnimationFile(const AssetRecord& record);
        void SaveScene();
        void LoadScene();
        void EnterPlay();

        // Recompila TODOS os materiais usados na cena, de uma vez —
        // mesma ideia da Unreal compilando shaders do projeto inteiro
        // antes de rodar. Workaround pra um bug ainda não totalmente
        // isolado onde a iluminação no Play só aplica certo depois de
        // QUALQUER material ser recompilado manualmente; chamado
        // automaticamente ao entrar em Play (ver EnterPlay).
        void RecompileAllMaterials();
        void EnterPause();
        void EnterEdit();
        void InstantiateScriptAsset(const std::filesystem::path& scriptPath,
            const std::string& assetUUID);




    };

} // namespace axe