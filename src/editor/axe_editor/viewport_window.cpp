#include "viewport_window.hpp"

#include <imgui.h>

namespace axe
{
	void ViewportWindow::Draw()
	{
		if (!ImGui::Begin("Viewport")) 
		{
			ImGui::End();
			return;
		}

		ImVec2 size = ImGui::GetContentRegionAvail();

		ImGui::Text("Scene viewport placeholder");
		ImGui::Separator();
		ImGui::Text("Available size: %.1f, x %.1f", size.x, size.y);

		ImGui::Dummy(size);

		ImGui::End();
	}
}