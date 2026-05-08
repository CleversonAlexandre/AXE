#pragma once
#include "axe/core/types.hpp"

#include "editor_context.hpp"
#include <imgui.h>

namespace axe
{
	class AXE_API HierarchyPanel
	{
	public:
		HierarchyPanel();
		void SetContext(EditorContext* context);
		void OnRender();

	private:
		void DrawObjectNode(SceneObject& obj);

		EditorContext* m_Context = nullptr;
	};
} // namespace axe