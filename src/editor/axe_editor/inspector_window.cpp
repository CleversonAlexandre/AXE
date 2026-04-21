#include "inspector_window.hpp"

#include <imgui.h>

namespace axe
{
	void InspectorWindow::Draw()
	{
		if (!ImGui::Begin("Inspector"))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("Nothing selected");
		ImGui::Separator();
		ImGui::Text("Name: <none>");
		ImGui::Text("Transform: <none>");
		ImGui::Text("Mesh: <none>");

		ImGui::End();
	}
}