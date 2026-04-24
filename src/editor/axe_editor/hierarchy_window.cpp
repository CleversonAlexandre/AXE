#include "hierarchy_window.hpp"
#include <imgui.h>
#include <string>

#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_objects.hpp"

namespace axe
{


	void HierarchyWindow::Draw()
	{
		if (!ImGui::Begin("Hierarchy"))
		{
			ImGui::End();
			return;
		}
		if (!m_ViewportRenderer)
		{
			ImGui::Text("ViewportRenderer not available");
			ImGui::End();
			return;
		}
		Scene* scene = m_ViewportRenderer->GetScene();

		if (!scene)
		{
			ImGui::Text("No scene loaded");
			ImGui::End();
			return;
		}

		for (auto& object : scene->GetObjects())
		{
			bool selected = m_ViewportRenderer->GetSelectedObjectID() == object.ID;
			std::string label = object.Name + "##" + std::to_string(object.ID);

			if (ImGui::Selectable(label.c_str(), selected))
			{
				m_ViewportRenderer->SelectObject(object.ID);
			}
		}

		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
		{
			m_ViewportRenderer->ClearSelection();
		}

		ImGui::End();
	}
}