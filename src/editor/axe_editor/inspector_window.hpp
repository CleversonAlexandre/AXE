#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/material/material.hpp"
#include "axe/scene/transform.hpp"
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
		void DrawMaterial(Material& material);
		void DrawLight(DirectionalLight& light);

		EditorContext* m_Context = nullptr;
	};

} // namespace axe