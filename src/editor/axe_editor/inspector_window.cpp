#include "inspector_window.hpp"
#include "axe/utils/glm_config.hpp"
#include <imgui.h>
#include <cstdint>
#include <algorithm>

namespace axe
{

	void InspectorWindow::SetContext(EditorContext* context)
	{
		m_Context = context;
	}

	void InspectorWindow::Draw()
	{
		if (!ImGui::Begin("Inspector"))
		{
			ImGui::End();
			return;
		}

		if (!m_Context || !m_Context->HasSelection())
		{
			ImGui::TextDisabled("Nenhum objeto selecionado.");
			ImGui::End();
			return;
		}

		auto& registry = m_Context->ActiveScene->GetRegistry();
		entt::entity entity = m_Context->SelectedEntity;

		// Nome editável
		if (auto* name = registry.try_get<NameComponent>(entity))
		{
			char nameBuffer[256];
			std::memset(nameBuffer, 0, sizeof(nameBuffer));
			std::strncpy(nameBuffer, name->Name.c_str(), sizeof(nameBuffer) - 1);
			if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
				name->Name = nameBuffer;
		}

		ImGui::Separator();

		// Transform
		if (auto* transform = registry.try_get<TransformComponent>(entity))
			DrawTransform(transform->Data);

		// Luz
		if (auto* light = registry.try_get<LightComponent>(entity))
		{
			if (light->Data)
				DrawLight(*light->Data);
		}
		// Material
		else if (auto* material = registry.try_get<MaterialComponent>(entity))
		{
			if (material->Data)
				DrawMaterial(*material->Data);
		}
		else if (!registry.any_of<LightComponent>(entity))
		{
			ImGui::Separator();
			ImGui::TextDisabled("Material: padrão");
		}

		ImGui::End();
	}

	void InspectorWindow::DrawLight(DirectionalLight& light)
	{
		ImGui::Separator();
		ImGui::Text("Luz Direcional");

		ImGui::DragFloat3("Direção", glm::value_ptr(light.Direction), 0.01f, -1.0f, 1.0f);
		ImGui::ColorEdit3("Cor", glm::value_ptr(light.Color));
		ImGui::DragFloat("Intensidade", &light.Intensity, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("Ambient", &light.AmbientStrength, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Specular", &light.SpecularStrength, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Shininess", &light.Shininess, 1.0f, 1.0f, 256.0f);
	}

	void InspectorWindow::DrawTransform(Transform& t)
	{
		ImGui::Text("Transform");

		bool changed = false;

		if (ImGui::DragFloat3("Position", &t.Position.x, 0.1f))
			changed = true;

		glm::vec3 rotationDegrees = glm::degrees(t.Rotation);
		if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotationDegrees), 0.5f))
		{
			t.Rotation = glm::radians(rotationDegrees);
			changed = true;
		}

		glm::vec3 scaleCopy = t.Scale;
		if (ImGui::DragFloat3("Scale", &scaleCopy.x, 0.05f))
		{
			t.Scale.x = std::max(scaleCopy.x, 0.001f);
			t.Scale.y = std::max(scaleCopy.y, 0.001f);
			t.Scale.z = std::max(scaleCopy.z, 0.001f);
			changed = true;
		}

		if (changed)
		{
			t.UseWorldMatrix = false;
			t.WorldMatrix = t.GetMatrix();
		}
	}

	void InspectorWindow::DrawMaterial(Material& material)
	{
		ImGui::Separator();
		ImGui::Text("Material - %s", material.GetName().c_str());
		ImGui::ColorEdit4("Color", glm::value_ptr(material.Color));
	}

} // namespace axe