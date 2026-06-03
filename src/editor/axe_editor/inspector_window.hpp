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

#include "node_graph/material_graph.hpp"
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

        EditorContext* m_Context = nullptr;

        GraphLoadCallback m_GraphLoadCallback;
        std::unique_ptr<MaterialGraph> m_CachedGraph;
        std::string m_CachedGraphUUID;
    };

} // namespace axe