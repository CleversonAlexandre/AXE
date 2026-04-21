#pragma once
#include "hierarchy_window.hpp"
#include "viewport_window.hpp"
#include "inspector_window.hpp"
#include "editor_ui.hpp"
#include "asset_browser.hpp"

//#include "axe/core/types.hpp"

#include <imgui.h>

namespace axe
{
	class EditorUI
	{
	public:
		void Draw();

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
	};
}


