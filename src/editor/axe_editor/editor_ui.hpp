#pragma once
#include "hierarchy_window.hpp"
#include "viewport_window.hpp"
#include "inspector_window.hpp"
#include "editor_ui.hpp"
#include "asset_browser.hpp"

#include "editor_context.hpp"
#include "material_editor_window.hpp"
#include <imgui.h>

namespace axe
{
	class ViewportRenderer;

	class EditorUI
	{
	public:
		void SetViewportRenderer(ViewportRenderer* renderer);
		void SetContext(EditorContext* context);
		void Draw();
		
		ViewportWindow* GetViewport() { return &m_ViewportWindow; }
		AssetBrowser* GetAssetBrowser() { return &m_AssetBowserWindow; }
		MaterialEditorWindow m_MaterialEditorWindow;
	private:

		void BeginDockspace();
		void EndDockspace();
		void DrawMenuBar();
		void BuildDefaultLayout(ImGuiID dockspaceId);


		HierarchyWindow m_HierarchyWindow;
		ViewportWindow  m_ViewportWindow;
		InspectorWindow m_InspectorWindow;
		AssetBrowser m_AssetBowserWindow;
		

		bool m_ShowHierarchy = true;
		bool m_ShowViewport = true;
		bool m_ShowInspector = true;
		bool m_ShowAssetBrowser = true;

		ViewportRenderer* m_ViewportRenderer = nullptr;
	};
}


