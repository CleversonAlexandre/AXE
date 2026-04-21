#include "hierarchy_window.hpp"
#include <imgui.h>

namespace axe
{
	void HierarchyWindow::Draw()
	{
		if (!ImGui::Begin("Hierarchy"))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("No scene loaded");
		ImGui::Separator();
		ImGui::BulletText("Entity 1");
		ImGui::BulletText("Entity 2");
		ImGui::BulletText("Entity 3");

		ImGui::End();
	}
}