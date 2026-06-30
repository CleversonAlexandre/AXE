#pragma once

#include "axe/layers/layer.hpp"
#include "editor_ui.hpp"
#include "file_dialog.hpp"
#include "axe/core/command_history.hpp"
#include "axe/physics/physics_world.hpp"
#include "axe/particles/particle_world.hpp"


#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/mesh/mesh_factory.hpp"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>

#include "axe/graphics/editor_camera.hpp"
#include "editor_context.hpp"

#include "axe/mesh/mesh_loader.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_world.hpp"
#include "axe/script/script_component.hpp"
#include "axe/input/input.hpp"
#include "axe/scene/game_mode_asset.hpp"
#include "axe/project/project_manager.hpp"
#include <filesystem>

#include "axe/scene/scene_serializer.hpp"
#include "editor_icon_library.hpp"

#include "axe/graphics/game_camera.hpp"
#include "axe/events/key_event.hpp"
#include "axe/input/key_codes.hpp"

#include "editor/axe_editor/material/material_editor_window.hpp"
#include "node_graph/material_graph.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

#include "material_thumbnail_renderer.hpp"

namespace axe
{
    class EditorLayer : public Layer
    {
    public:
        EditorLayer();

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnRender() override;
        void OnEvent(Event& e) override;

    private:
        // ── Estados do editor ──────────────────────────────────────────────────
        enum class EditorState { Edit, Play, Pause };
        EditorState m_EditorState = EditorState::Edit;

        // ── Cena ──────────────────────────────────────────────────────────────
        std::unique_ptr<Scene>  m_Scene;
        std::string             m_CurrentScenePath;
        std::string             m_SceneSnapshot;
        bool                    m_SceneLoaded = false;
        SceneEnvironment        m_Environment;

        // ── Renderer / UI ─────────────────────────────────────────────────────
        std::unique_ptr<axe::ViewportRenderer> m_ViewportRenderer;
        std::unique_ptr<EditorUI>              m_EditorUI;
        EditorContext                          m_Context;
        CommandHistory                         m_CommandHistory;
        MaterialThumbnailRenderer              m_ThumbnailRenderer;

        // ── Física / Scripts ──────────────────────────────────────────────────
        PhysicsWorld  m_PhysicsWorld;
        ScriptWorld   m_ScriptWorld;
        ParticleWorld m_ParticleWorld;
        entt::entity  m_PlayerEntity = entt::null;

        // ── Câmera de jogo ────────────────────────────────────────────────────
        GameCamera m_GameCamera;

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
        void EnsureEnvironmentComponent();
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