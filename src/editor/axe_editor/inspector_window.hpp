#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/material/material.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/scene/transform.hpp"
#include "axe/asset/asset_database.hpp"
#include <entt/entt.hpp>

namespace axe
{

    class AXE_API InspectorWindow
    {
    public:
        void SetContext(EditorContext* context);
        void Draw();

    private:
        void DrawTransform(Transform& transform);
        void DrawMaterial(entt::entity entity);   // slot do asset + drag and drop
        void DrawMaterialParams(Material& mat);   // parâmetros editáveis
        void DrawTextureSlot(const char* label,
            std::shared_ptr<Texture2D>& tex,
            std::string& uuid);
        void DrawLight(DirectionalLight& light);

        EditorContext* m_Context = nullptr;
    };

} // namespace axe