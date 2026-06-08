#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/material/material.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/scene/transform.hpp"
#include "axe/asset/asset_database.hpp"
#include "file_dialog.hpp"
#include <entt/entt.hpp>
#include <functional>

#include "node_graph/material_graph.hpp"
#include "axe/script/script_component.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include "axe/graphics/renderer/post_process_pass.hpp"
namespace axe
{

    class AXE_API InspectorWindow
    {
    public:
        void SetContext(EditorContext* context);
        void Draw();

        using GraphLoadCallback = std::function<MaterialGraph* (const std::string& assetUUID)>;
        void SetGraphLoadCallback(GraphLoadCallback cb) { m_GraphLoadCallback = cb; }

        void DrawMaterialGraphParams(const std::string& assetUUID,
            entt::registry& registry,
            entt::entity entity);

        void DrawPostProcess(PostProcessComponent& pp);

        static void MarkGraphCacheDirty();

        // Callback para abrir o editor de script
        std::function<void(entt::entity, ScriptComponent*)> m_OnOpenScript;
    private:
        void DrawTransform(Transform& transform);
        void DrawMaterial(entt::entity entity);
        void DrawMaterialParams(Material& mat);
        void DrawTextureSlot(const char* label,
            std::shared_ptr<Texture2D>& tex,
            std::string& uuid);
        void DrawLight(DirectionalLight& light);
        void DrawPointLight(PointLight& light);
        void DrawEnvironment(EnvironmentComponent& ec);
        void DrawFolder(FolderComponent& folder);
        void DrawCamera(CameraComponent& cam);
        void DrawRigidbody(entt::entity entity, entt::registry& registry);
        void DrawCollider(entt::entity entity, entt::registry& registry);
        void DrawCharacterController(entt::entity entity, entt::registry& registry);

        EditorContext* m_Context = nullptr;

        // Remoção pendente — executa no início do próximo frame
        enum class PendingRemove { None, Rigidbody, Collider, CharacterController, Script };
        PendingRemove  m_PendingRemove = PendingRemove::None;
        entt::entity   m_PendingRemoveEntity = entt::null;


        GraphLoadCallback m_GraphLoadCallback;
        std::unique_ptr<MaterialGraph> m_CachedGraph;
        std::string m_CachedGraphUUID;
    };

    
} // namespace axe