#include "inspector_window.hpp"

#include <imgui.h>
#include <cstdint>

#include "axe/utils/glm_config.hpp"

#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/scene/scene_objects.hpp"
namespace axe
{
	void InspectorWindow::Draw()
	{
		if (!ImGui::Begin("Inspector"))
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

		SceneObject* selectedObject = m_ViewportRenderer->GetSelectedObject();

		if (!selectedObject)
		{
			ImGui::Text("Nothing selected");
			ImGui::End();
			return;
		}

		char nameBuffer[256];
		std::memset(nameBuffer, 0, sizeof(nameBuffer));
		std::strncpy(nameBuffer, selectedObject->Name.c_str(), sizeof(nameBuffer) - 1);

		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
		{
			selectedObject->Name = nameBuffer;
		}

		ImGui::Separator();

		auto& transform = selectedObject->TransformData;

		ImGui::DragFloat3("Position", &transform.Position.x, 0.1f);

		glm::vec3 rotationDegrees = glm::degrees(transform.Rotation);
		if (ImGui::DragFloat3("Rotation", &rotationDegrees.x, 0.5f))
		{
			transform.Rotation = glm::radians(rotationDegrees);
		}

		ImGui::DragFloat3("Scale", &transform.Scale.x, 0.05f);

		ImGui::End();
	}
}