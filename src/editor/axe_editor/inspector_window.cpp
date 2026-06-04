#include "inspector_window.hpp"
#include "axe/utils/glm_config.hpp"
#include <imgui.h>
#include <cstdint>
#include <algorithm>

#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/scene/scene_serializer.hpp"

#include "asset/asset_picker.hpp"
#include "axe/material/material_compiler.hpp"
#include "material_editor_window.hpp"


namespace axe
{
	static std::unique_ptr<MaterialGraph> s_CachedGraph;
	static std::string s_CachedGraphUUID;
	static bool s_GraphCacheDirty = false;

	static void InvalidateGraphCache() { s_CachedGraphUUID = ""; }

	static void SaveCachedGraph(const std::string& assetUUID,
		entt::registry* registry = nullptr,
		entt::entity entity = entt::null)
	{
		if (!s_CachedGraph) return;
		const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
		if (!record) return;

		auto graphPath = record->FilePath;
		graphPath.replace_extension(".axegraph");

		nlohmann::json originalJ;
		{
			std::ifstream fileIn(graphPath);
			if (fileIn.is_open())
				try { originalJ = nlohmann::json::parse(fileIn); }
			catch (...) {}
		}

		nlohmann::json newJ = s_CachedGraph->Serialize();

		if (originalJ.contains("nodes") && newJ.contains("nodes"))
		{
			auto& origNodes = originalJ["nodes"];
			auto& newNodes = newJ["nodes"];
			for (int i = 0; i < (int)newNodes.size() && i < (int)origNodes.size(); i++)
				if (origNodes[i].contains("pos_x"))
				{
					newNodes[i]["pos_x"] = origNodes[i]["pos_x"];
					newNodes[i]["pos_y"] = origNodes[i]["pos_y"];
				}
		}

		std::ofstream fileOut(graphPath);
		if (fileOut.is_open())
			fileOut << newJ.dump(4);

		auto result = MaterialCompiler::Compile(s_CachedGraph.get());
		if (result.Success)
		{
			try
			{
				auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
				if (shader && registry && registry->valid(entity))
				{
					auto* mc = registry->try_get<MaterialComponent>(entity);
					if (mc && mc->Data)
						mc->Data->SetShader(shader);
				}
			}
			catch (...) {}
		}
		MaterialEditorWindow::MarkNeedsReload();
	}

	static std::string FindOutputPinLabel(MaterialGraph* graph, Node* sourceNode)
	{
		for (auto& outPin : sourceNode->Outputs)
			for (auto& link : graph->GetLinks())
			{
				if (link.StartPin != outPin.ID) continue;
				for (auto& destNode : graph->GetNodes())
					for (auto& inPin : destNode->Inputs)
					{
						if (inPin.ID != link.EndPin) continue;
						if (destNode->Name == "Material Output") return inPin.Name;
						std::string label = FindOutputPinLabel(graph, destNode.get());
						if (!label.empty()) return label;
					}
			}
		return "";
	}

	void InspectorWindow::SetContext(EditorContext* context) { m_Context = context; }

	void InspectorWindow::Draw()
	{
		if (!ImGui::Begin("Inspector")) { ImGui::End(); return; }

		if (!m_Context || !m_Context->HasSelection())
		{
			ImGui::TextDisabled("Nenhum objeto selecionado.");
			ImGui::End();
			return;
		}

		auto& registry = m_Context->ActiveScene->GetRegistry();
		entt::entity entity = m_Context->SelectedEntity;

		if (auto* name = registry.try_get<NameComponent>(entity))
		{
			char nameBuffer[256];
			std::memset(nameBuffer, 0, sizeof(nameBuffer));
			std::strncpy(nameBuffer, name->Name.c_str(), sizeof(nameBuffer) - 1);
			if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
				name->Name = nameBuffer;
		}

		ImGui::Separator();

		if (auto* transform = registry.try_get<TransformComponent>(entity))
			DrawTransform(transform->Data);

		if (auto* light = registry.try_get<LightComponent>(entity))
		{
			if (light->Data) DrawLight(*light->Data);
		}
		else if (auto* pp = registry.try_get<PostProcessComponent>(entity))
			DrawPostProcess(*pp);
		else if (auto* ec = registry.try_get<EnvironmentComponent>(entity))
			DrawEnvironment(*ec);
		else if (auto* cam = registry.try_get<CameraComponent>(entity))
			DrawCamera(*cam);
		else if (auto* folder = registry.try_get<FolderComponent>(entity))
			DrawFolder(*folder);
		else if (registry.any_of<PointLightComponent>(entity)) {}
		else if (registry.any_of<MaterialComponent>(entity))
			DrawMaterial(entity);
		else
		{
			ImGui::Separator();
			DrawMaterial(entity);
		}

		if (auto* plc = registry.try_get<PointLightComponent>(entity))
			if (plc->Data) DrawPointLight(*plc->Data);

		ImGui::End();
	}

	void InspectorWindow::DrawCamera(CameraComponent& cam)
	{
		ImGui::Separator();
		ImGui::Text("Câmera");
		ImGui::Checkbox("Câmera Principal", &cam.IsPrimary);
		ImGui::DragFloat("FOV", &cam.Fov, 0.5f, 10.0f, 170.0f);
		ImGui::DragFloat("Near Clip", &cam.NearClip, 0.01f, 0.001f, 10.0f);
		ImGui::DragFloat("Far Clip", &cam.FarClip, 1.0f, 10.0f, 10000.0f);
		ImGui::Separator();
		ImGui::TextDisabled("Controles (modo Play)");
		ImGui::DragFloat("Velocidade", &cam.MoveSpeed, 0.1f, 0.1f, 100.0f);
		ImGui::DragFloat("Sensibilidade", &cam.Sensitivity, 0.01f, 0.01f, 5.0f);
	}

	void InspectorWindow::DrawFolder(FolderComponent& folder)
	{
		ImGui::Separator();
		ImGui::Text("Pasta");
		float col[4] = { folder.Color.x, folder.Color.y, folder.Color.z, folder.Color.w };
		if (ImGui::ColorEdit4("Cor", col))
			folder.Color = { col[0], col[1], col[2], col[3] };
		ImGui::TextDisabled("Arraste objetos para dentro desta pasta na hierarchy.");
	}

	void InspectorWindow::DrawEnvironment(EnvironmentComponent& ec)
	{
		ImGui::Separator();
		ImGui::Text("Environment");
		ImGui::TextDisabled("HDRI:");
		ImGui::SameLine();
		std::string shortPath = ec.HDRIPath.empty() ? "Nenhum" :
			std::filesystem::path(ec.HDRIPath).filename().string();
		ImGui::Text("%s", shortPath.c_str());
		if (ImGui::Button("Carregar HDRI..."))
		{
			auto path = FileDialog::Open("HDR Image\0*.hdr;*.exr\0All Files\0*.*\0", "Selecionar HDRI", "hdr");
			if (!path.empty()) ec.HDRIPath = path.string();
		}
		ImGui::Separator();
		ImGui::DragFloat("Rotação Skybox", &ec.SkyboxRotation, 1.0f, -360.0f, 360.0f);
	}

	void InspectorWindow::DrawPointLight(PointLight& light)
	{
		ImGui::Separator();
		ImGui::Text("Point Light");
		ImGui::TextDisabled("Posicione pelo Transform");
		ImGui::ColorEdit3("Cor", glm::value_ptr(light.Color));
		ImGui::DragFloat("Intensidade", &light.Intensity, 0.1f, 0.0f, 100.0f);
		ImGui::DragFloat("Radius", &light.Radius, 0.1f, 0.1f, 200.0f);
	}

	void InspectorWindow::DrawLight(DirectionalLight& light)
	{
		ImGui::Separator();
		ImGui::Text("Luz Direcional");
		ImGui::DragFloat3("Direção", glm::value_ptr(light.Direction), 0.01f, -1.0f, 1.0f);
		ImGui::ColorEdit3("Cor", glm::value_ptr(light.Color));
		ImGui::DragFloat("Intensidade", &light.Intensity, 0.01f, 0.0f, 10.0f);
		ImGui::Separator();
		ImGui::TextDisabled("Luz Indireta");
		ImGui::DragFloat("IBL Intensity", &light.IBLIntensity, 0.01f, 0.0f, 5.0f);
		ImGui::DragFloat("Ambient Flat", &light.AmbientStrength, 0.01f, 0.0f, 1.0f);
	}

	void InspectorWindow::DrawTransform(Transform& t)
	{
		ImGui::Text("Transform");
		bool changed = false;
		if (ImGui::DragFloat3("Position", &t.Position.x, 0.1f)) changed = true;
		glm::vec3 rotDeg = glm::degrees(t.Rotation);
		if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotDeg), 0.5f))
		{
			t.Rotation = glm::radians(rotDeg);
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
		if (changed) { t.UseWorldMatrix = false; t.WorldMatrix = t.GetMatrix(); }
	}

	void InspectorWindow::DrawTextureSlot(const char* label,
		std::shared_ptr<Texture2D>& tex, std::string& uuid)
	{
		ImGui::PushID(label);
		ImVec2 size(48, 48);
		if (tex && tex->IsLoaded())
			ImGui::Image((ImTextureID)(uintptr_t)tex->GetRendererID(), size, ImVec2(0, 1), ImVec2(1, 0));
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
			ImGui::Button("##empty", size);
			ImGui::PopStyleColor();
		}
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
			if (ImGui::SmallButton("X")) { tex = nullptr; uuid = ""; }
		}
		else ImGui::TextDisabled("Nenhuma");
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

		std::string uuid = mc ? mc->MaterialAssetUUID : "";

		if (AssetPicker::Draw("Material", uuid, { AssetType::Material },
			[&](const AssetRecord& record)
			{
				auto matAsset = MaterialAsset::LoadFromFile(record.FilePath);
				if (!matAsset) return;
				auto material = matAsset->GetMaterial();
				if (SceneSerializer::GetMaterialRecompileCallback())
					SceneSerializer::GetMaterialRecompileCallback()(record.UUID, material.get());
				if (!mc)
				{
					auto& comp = registry.emplace<MaterialComponent>(entity, material);
					comp.MaterialAssetUUID = record.UUID;
					mc = registry.try_get<MaterialComponent>(entity);
				}
				else
				{
					mc->Data = material;
					mc->MaterialAssetUUID = record.UUID;
				}
			}))
		{
			if (uuid.empty() && mc) { registry.remove<MaterialComponent>(entity); mc = nullptr; }
			else if (!uuid.empty() && mc) mc->MaterialAssetUUID = uuid;
		}

		ImGui::Spacing();
		if (mc && mc->Data)
		{
			if (!mc->MaterialAssetUUID.empty())
				DrawMaterialGraphParams(mc->MaterialAssetUUID, registry, entity);
			else
				DrawMaterialParams(*mc->Data);
		}
	}

	void InspectorWindow::DrawMaterialParams(Material& mat)
	{
		bool usePBR = mat.UsePBR;
		if (ImGui::Checkbox("PBR", &usePBR)) mat.UsePBR = usePBR;
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
		AssetPicker::Draw("Albedo", mat.AlbedoUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.AlbedoMap = Texture2D::Create(r.FilePath.string()); mat.AlbedoUUID = r.UUID; });
		AssetPicker::Draw("Normal", mat.NormalUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.NormalMap = Texture2D::Create(r.FilePath.string()); mat.NormalUUID = r.UUID; });
		AssetPicker::Draw("Roughness", mat.RoughnessUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.RoughnessMap = Texture2D::Create(r.FilePath.string()); mat.RoughnessUUID = r.UUID; });
		AssetPicker::Draw("Metallic", mat.MetallicUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.MetallicMap = Texture2D::Create(r.FilePath.string()); mat.MetallicUUID = r.UUID; });
		AssetPicker::Draw("AO", mat.AOUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.AOMap = Texture2D::Create(r.FilePath.string()); mat.AOUUID = r.UUID; });
	}

	void InspectorWindow::DrawMaterialGraphParams(const std::string& assetUUID,
		entt::registry& registry, entt::entity entity)
	{
		if (s_GraphCacheDirty) { s_CachedGraphUUID = ""; s_GraphCacheDirty = false; }
		if (assetUUID.empty()) return;

		if (s_CachedGraphUUID != assetUUID)
		{
			s_CachedGraphUUID = assetUUID;
			s_CachedGraph.reset();
			const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
			if (!record) return;
			auto graphPath = record->FilePath;
			graphPath.replace_extension(".axegraph");
			if (!std::filesystem::exists(graphPath)) return;
			std::ifstream file(graphPath);
			try
			{
				nlohmann::json j = nlohmann::json::parse(file);
				s_CachedGraph = std::make_unique<MaterialGraph>();
				s_CachedGraph->Deserialize(j);
				s_CachedGraph->BuildNodes();
			}
			catch (...) { s_CachedGraph.reset(); return; }
		}

		if (!s_CachedGraph || s_CachedGraph->GetNodes().empty()) return;

		Node* outputNode = nullptr;
		for (auto& node : s_CachedGraph->GetNodes())
			if (node->Name == "Material Output") { outputNode = node.get(); break; }
		if (!outputNode) return;

		ImGui::Separator();
		ImGui::Text("Parâmetros:");
		ImGui::Spacing();

		bool anyEditable = false;
		for (auto& node : s_CachedGraph->GetNodes())
		{
			if (node->Name == "Material Output" || node->Type == NodeType::Comment) continue;
			ImGui::PushID(node->ID.Get());

			if (node->Name == "Float" && node->IsConstant)
			{
				std::string pinLabel = FindOutputPinLabel(s_CachedGraph.get(), node.get());
				if (!pinLabel.empty())
				{
					anyEditable = true;
					ImGui::TextDisabled("%s [Float]", pinLabel.c_str());
					ImGui::SetNextItemWidth(-1);
					if (ImGui::DragFloat(("##f" + std::to_string(node->ID.Get())).c_str(), &node->Value.FloatVal, 0.01f, 0.0f, 1.0f))
						SaveCachedGraph(assetUUID, &registry, entity);
				}
			}
			else if (node->Name == "Color" && node->IsConstant)
			{
				std::string pinLabel = FindOutputPinLabel(s_CachedGraph.get(), node.get());
				if (!pinLabel.empty())
				{
					anyEditable = true;
					ImGui::TextDisabled("%s [Color]", pinLabel.c_str());
					ImGui::SetNextItemWidth(-1);
					if (ImGui::ColorEdit4(("##c" + std::to_string(node->ID.Get())).c_str(), &node->Value.Vec4Val.x))
						SaveCachedGraph(assetUUID, &registry, entity);
				}
			}
			else if (node->Name == "Texture Sample")
			{
				std::string pinLabel = FindOutputPinLabel(s_CachedGraph.get(), node.get());
				if (!pinLabel.empty())
				{
					anyEditable = true;
					ImGui::TextDisabled("%s [Texture]", pinLabel.c_str());
					AssetPicker::Draw((pinLabel + "_" + std::to_string(node->ID.Get())).c_str(),
						node->Value.TextureUUID, { AssetType::Texture },
						[&](const AssetRecord& record)
						{
							node->Value.TextureVal = Texture2D::Create(record.FilePath.string());
							node->Value.TextureUUID = record.UUID;
							SaveCachedGraph(assetUUID, &registry, entity);
						});
				}
			}
			ImGui::PopID();
		}

		if (!anyEditable)
		{
			ImGui::TextDisabled("Nenhum parâmetro editável.");
			ImGui::TextDisabled("Adicione nodes Float, Color ou Texture Sample no graph.");
		}
	}

	void InspectorWindow::MarkGraphCacheDirty() { s_GraphCacheDirty = true; }

	void InspectorWindow::DrawPostProcess(PostProcessComponent& pp)
	{
		ImGui::Separator();
		ImGui::Text("Post Process Volume");
		ImGui::Checkbox("Global", &pp.IsGlobal);

		ImGui::Separator();
		ImGui::Text("Tone Mapping");
		ImGui::DragFloat("Exposure", &pp.Settings.Exposure, 0.01f, 0.1f, 10.0f);
		const char* toneModes[] = { "Reinhard", "ACES" };
		ImGui::Combo("Tone Map", &pp.Settings.ToneMapMode, toneModes, 2);

		ImGui::Separator();
		ImGui::Text("Bloom");
		ImGui::Checkbox("Bloom Ativo", &pp.Settings.BloomEnabled);
		if (pp.Settings.BloomEnabled)
		{
			ImGui::DragFloat("Threshold", &pp.Settings.BloomThreshold, 0.01f, 0.0f, 3.0f);
			ImGui::DragFloat("Intensidade", &pp.Settings.BloomIntensity, 0.01f, 0.0f, 3.0f);
			ImGui::SliderInt("Blur Passes", &pp.Settings.BloomBlurPasses, 1, 10);
		}

		ImGui::Separator();
		ImGui::Text("SSAO");
		ImGui::Checkbox("SSAO Ativo", &pp.SSAO.Enabled);
		if (pp.SSAO.Enabled)
		{
			ImGui::DragFloat("Radius", &pp.SSAO.Radius, 0.01f, 0.01f, 2.0f);
			ImGui::DragFloat("Bias", &pp.SSAO.Bias, 0.001f, 0.0f, 0.1f);
			ImGui::DragFloat("Power", &pp.SSAO.Power, 0.1f, 0.5f, 8.0f);
			ImGui::SliderInt("Kernel Size", &pp.SSAO.KernelSize, 8, 64);
			ImGui::Checkbox("Debug (mostra oclusão)", &pp.SSAO.Debug);
		}
	}

} // namespace axe