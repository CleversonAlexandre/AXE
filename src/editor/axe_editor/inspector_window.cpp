#include "inspector_window.hpp"
#include "axe/utils/glm_config.hpp"
#include <imgui.h>
#include <cstdint>
#include <algorithm>

#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"
#include "axe/material/material_asset.hpp"


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
		else if (registry.any_of<MaterialComponent>(entity))
		{
			DrawMaterial(entity); // ← passa entity, não o material
		}
		else if (!registry.any_of<LightComponent>(entity))
		{
			ImGui::Separator();
			// Objeto sem material — mostra slot vazio para arrastar
			DrawMaterial(entity);
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


	void InspectorWindow::DrawTextureSlot(const char* label,
		std::shared_ptr<Texture2D>& tex, std::string& uuid)
	{
		ImGui::PushID(label);

		ImVec2 size(48, 48);

		if (tex && tex->IsLoaded())
		{
			ImGui::Image(
				(ImTextureID)(uintptr_t)tex->GetRendererID(),
				size, ImVec2(0, 1), ImVec2(1, 0)
			);
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
			ImGui::Button("##empty", size);
			ImGui::PopStyleColor();
		}

		// Drag and drop
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
			{
				std::string droppedUUID = (const char*)payload->Data;
				const AssetRecord* record = AssetDatabase::Get().GetByUUID(droppedUUID);

				if (record && record->Type == AssetType::Texture)
				{
					tex = Texture2D::Create(record->FilePath.string());
					uuid = droppedUUID;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::Text("%s", label);

		if (tex && tex->IsLoaded())
		{
			ImGui::TextDisabled("%dx%d", tex->GetWidth(), tex->GetHeight());
			if (ImGui::SmallButton("X"))
			{
				tex = nullptr;
				uuid = "";
			}
		}
		else
		{
			ImGui::TextDisabled("Nenhuma");
		}

		ImGui::EndGroup();
		ImGui::PopID();
		ImGui::Spacing();
	}


	void InspectorWindow::DrawMaterial(entt::entity entity)
	{
		auto& registry = m_Context->ActiveScene->GetRegistry();
		auto* mc = registry.try_get<MaterialComponent>(entity);

		ImGui::Separator();
		ImGui::Text("Material");
		ImGui::Spacing();

		// Slot do asset .axemat
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

		std::string slotLabel = "Nenhum Material";
		if (mc && !mc->MaterialAssetUUID.empty())
		{
			const AssetRecord* record = AssetDatabase::Get().GetByUUID(mc->MaterialAssetUUID);
			if (record) slotLabel = record->Name;
		}
		else if (mc && mc->Data)
		{
			slotLabel = mc->Data->GetName();
		}

		ImGui::Button(slotLabel.c_str(), ImVec2(-1, 32));
		ImGui::PopStyleColor();

		// Aceita .axemat via drag and drop
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
			{
				std::string uuid = (const char*)payload->Data;
				const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);

				if (record && record->Type == AssetType::Material)
				{
					auto matAsset = MaterialAsset::LoadFromFile(record->FilePath);
					if (matAsset)
					{
						if (!mc)
						{
							registry.emplace<MaterialComponent>(entity,
								matAsset->GetMaterial());
							mc = registry.try_get<MaterialComponent>(entity);
						}
						else
						{
							mc->Data = matAsset->GetMaterial();
						}
						mc->MaterialAssetUUID = uuid;
						AXE_EDITOR_INFO("Material '{}' aplicado.", record->Name);
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::Spacing();

		// Mostra parâmetros do material se tiver
		if (mc && mc->Data)
			DrawMaterialParams(*mc->Data);
	}

	void InspectorWindow::DrawMaterialParams(Material& mat)
	{
		bool usePBR = mat.UsePBR;
		if (ImGui::Checkbox("PBR", &usePBR))
			mat.UsePBR = usePBR;

		ImGui::Separator();

		if (!mat.UsePBR)
		{
			ImGui::ColorEdit4("Cor", glm::value_ptr(mat.Color));
			ImGui::DragFloat("Specular", &mat.SpecularStrength, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Shininess", &mat.Shininess, 1.0f, 1.0f, 256.0f);
			return;
		}

		ImGui::DragFloat("Metallic", &mat.Metallic, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Roughness", &mat.Roughness, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("AO", &mat.AO, 0.01f, 0.0f, 1.0f);

		ImGui::Separator();
		ImGui::Text("Texturas:");
		ImGui::Spacing();

		DrawTextureSlot("Albedo", mat.AlbedoMap, mat.AlbedoUUID);
		DrawTextureSlot("Normal", mat.NormalMap, mat.NormalUUID);
		DrawTextureSlot("Roughness", mat.RoughnessMap, mat.RoughnessUUID);
		DrawTextureSlot("Metallic", mat.MetallicMap, mat.MetallicUUID);
		DrawTextureSlot("AO", mat.AOMap, mat.AOUUID);
	}

} // namespace axe