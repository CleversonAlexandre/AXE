#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include <entt/entt.hpp>
#include <imgui.h>

namespace axe
{

	class AXE_API HierarchyWindow
	{
	public:
		HierarchyWindow();
		void SetContext(EditorContext* context);
		void Draw();
		
		void CreateObject(const std::string& name, const std::string& primitiveUUID);

	private:
		void DrawObjectNode(entt::entity entity);
		void DrawContextMenuEmpty();
		void DrawContextMenuObject(entt::entity entity);
		void HandleKeyboardShortcuts();
		
		void CreateLight();
		void DeleteSelected();
		void DuplicateSelected();

		EditorContext* m_Context = nullptr;
	};

} // namespace axe