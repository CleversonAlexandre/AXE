#include "inspector_window.hpp"
#include "axe/utils/glm_config.hpp"
#include <imgui.h>
#include <cstdint>
#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>

#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/scene/scene_serializer.hpp"
#include "axe/script/script_component.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/project/project_manager.hpp"

#include "asset/asset_picker.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include "editor/axe_editor/material/material_editor_window.hpp"


namespace axe
{
	static std::unique_ptr<MaterialGraph> s_CachedGraph;
	static std::string s_CachedGraphUUID;
	static bool s_GraphCacheDirty = false;

	// Slot de "Light Material" — compartilhado entre PointLight e
	// DirectionalLight (mesmos nomes de campo: LightMaterialUUID/Shader/
	// Samplers), pra não duplicar a mesma lógica duas vezes. Compila via
	// MaterialCompiler::CompileLightFunctionFromFile (domínio Light
	// Function: só o pin Emissive importa) — ver comentário em PointLight.
	template<typename TLight>
	static void DrawLightMaterialSlot(TLight& light)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Light Material (grafo controla a cor da luz)");

		bool changed = AssetPicker::Draw("Light Material", light.LightMaterialUUID,
			{ AssetType::Material },
			[&](const AssetRecord& record)
			{
				std::shared_ptr<Shader> shader;
				std::map<std::string, std::shared_ptr<Texture2D>> samplers;
				if (MaterialCompiler::CompileLightFunctionFromFile(record.FilePath, shader, samplers))
				{
					light.LightMaterialShader = shader;
					light.LightMaterialSamplers = samplers;
					light.LightMaterialUUID = record.UUID;
				}
			});

		if (changed && light.LightMaterialUUID.empty())
		{
			// Limpo via botão "X" do próprio AssetPicker (não passa pelo
			// callback onSelect, que só dispara numa seleção nova)
			light.LightMaterialShader = nullptr;
			light.LightMaterialSamplers.clear();
		}

		if (light.LightMaterialShader)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Recompilar"))
			{
				const AssetRecord* record = AssetDatabase::Get().GetByUUID(light.LightMaterialUUID);
				if (record)
				{
					std::shared_ptr<Shader> shader;
					std::map<std::string, std::shared_ptr<Texture2D>> samplers;
					if (MaterialCompiler::CompileLightFunctionFromFile(record->FilePath, shader, samplers))
					{
						light.LightMaterialShader = shader;
						light.LightMaterialSamplers = samplers;
					}
				}
			}
			ImGui::TextDisabled("Multiplica a Cor da luz (cinza = brilho, cor = tingimento)");
		}
	}

	static bool DrawComponentHeader(const char* label, entt::entity entity, int componentIdx, bool* outRemove)
	{
		*outRemove = false;
		ImGui::Separator();
		ImGui::Spacing();

		float available = ImGui::GetContentRegionAvail().x;

		// ID único: inclui entity e componente no label para evitar conflito
		std::string headerId = std::string(label) + "##hdr_" +
			std::to_string((uint32_t)entity) + "_" + std::to_string(componentIdx);
		std::string btnId = "x##x_" +
			std::to_string((uint32_t)entity) + "_" + std::to_string(componentIdx);

		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.32f, 1.0f));
		bool open = ImGui::CollapsingHeader(headerId.c_str(),
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
		ImGui::PopStyleColor(2);

		ImGui::SameLine(available - 18.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
		if (ImGui::SmallButton(btnId.c_str()))
			*outRemove = true;
		ImGui::PopStyleColor(3);

		return open;
	}

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
		// Executa remoção pendente ANTES de qualquer ImGui — frame limpo
		if (m_PendingRemove != PendingRemove::None && m_Context)
		{
			auto& reg = m_Context->ActiveScene->GetRegistry();
			if (reg.valid(m_PendingRemoveEntity))
			{
				switch (m_PendingRemove)
				{
				case PendingRemove::Rigidbody:           reg.remove<RigidbodyComponent>(m_PendingRemoveEntity);           break;
				case PendingRemove::Collider:            reg.remove<ColliderComponent>(m_PendingRemoveEntity);            break;
				case PendingRemove::CharacterController: reg.remove<CharacterControllerComponent>(m_PendingRemoveEntity); break;
				case PendingRemove::Script:              reg.remove<ScriptComponent>(m_PendingRemoveEntity);              break;
				default: break;
				}
			}
			m_PendingRemove = PendingRemove::None;
			m_PendingRemoveEntity = entt::null;
		}

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
		else if (auto* sa = registry.try_get<SpringArmComponent>(entity))
		{
			bool removeArm = false;
			bool openArm = DrawComponentHeader("Spring Arm", entity, 4, &removeArm);
			if (removeArm) { registry.remove<SpringArmComponent>(entity); return; }
			if (openArm) DrawSpringArm(*sa);
		}
		else if (auto* cam = registry.try_get<CameraComponent>(entity))
			DrawCamera(*cam);
		else if (auto* folder = registry.try_get<FolderComponent>(entity))
			DrawFolder(*folder);
		else if (registry.any_of<PointLightComponent>(entity)) {}
		else if (registry.any_of<MaterialComponent>(entity))
			DrawMaterial(entity);
		else if (registry.any_of<ParticleSystemComponent>(entity))
		{
			// Entity de partícula pura — não exibe slot de Material aqui;
			// o Material do mesh (se existir) aparece só se houver MeshComponent
			// junto. Evita mostrar um slot inútil e confuso.
		}
		else
		{
			ImGui::Separator();
			DrawMaterial(entity);
		}

		if (auto* plc = registry.try_get<PointLightComponent>(entity))
			if (plc->Data)
			{
				glm::vec3 rotation = glm::vec3(0.0f);
				if (auto* tc = registry.try_get<TransformComponent>(entity))
					rotation = tc->Data.Rotation;
				DrawPointLight(*plc->Data, rotation);
			}

		// Física — podem coexistir com outros componentes
		if (registry.any_of<RigidbodyComponent>(entity))
			DrawRigidbody(entity, registry);
		if (registry.any_of<ColliderComponent>(entity))
			DrawCollider(entity, registry);
		if (registry.any_of<CharacterControllerComponent>(entity))
			DrawCharacterController(entity, registry);

		// Script
		if (registry.any_of<ScriptComponent>(entity))
		{
			auto& sc = registry.get<ScriptComponent>(entity);
			bool remove = false;
			bool open = DrawComponentHeader("Script", entity, 3, &remove);
			if (remove) { m_PendingRemove = PendingRemove::Script; m_PendingRemoveEntity = entity; }
			else if (open)
			{
				ImGui::Text("Script: %s", sc.ScriptName.c_str());
				ImGui::Spacing();

				float btnW = ImGui::GetContentRegionAvail().x;
				if (ImGui::Button("  Editar Script  ", ImVec2(btnW, 0)))
				{
					if (m_OnOpenScript)
						m_OnOpenScript(entity, &sc, &registry);
				}

				if (sc.IsCompiled)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
					ImGui::TextUnformatted("  Script compilado");
					ImGui::PopStyleColor();
				}
			}
		}

		// Particle System
		DrawParticleSystem(entity);

		// Botão Add Component
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		float btnWidth = ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnWidth * 0.15f);

		ImGui::PushID((uint32_t)entity);
		if (ImGui::Button("+ Adicionar Componente", ImVec2(btnWidth * 0.7f, 0)))
			ImGui::OpenPopup("add_comp");

		if (ImGui::BeginPopup("add_comp"))
		{
			ImGui::TextDisabled("Fisica");
			ImGui::Separator();

			bool hasRb = registry.any_of<RigidbodyComponent>(entity);
			bool hasCol = registry.any_of<ColliderComponent>(entity);
			bool hasCc = registry.any_of<CharacterControllerComponent>(entity);

			if (hasRb)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Rigidbody (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Rigidbody"))
			{
				registry.emplace<RigidbodyComponent>(entity);
				ImGui::CloseCurrentPopup();
			}

			if (hasCol)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Collider (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Collider"))
			{
				auto& col = registry.emplace<ColliderComponent>(entity);

				// Detecta shape pelo nome do mesh
				if (auto* mc = registry.try_get<MeshComponent>(entity))
				{
					std::string uuid = mc->AssetUUID;
					std::string name = "";
					if (auto* nc = registry.try_get<NameComponent>(entity))
						name = nc->Name;

					// Converte para lowercase para comparação
					std::string lower = name;
					std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

					if (lower.find("sphere") != std::string::npos ||
						lower.find("ball") != std::string::npos ||
						lower.find("esfera") != std::string::npos)
					{
						col.Shape = ColliderShape::Sphere;
						// Usa o maior eixo do scale como raio
						if (auto* tc = registry.try_get<TransformComponent>(entity))
							col.Radius = glm::compMax(tc->Data.Scale) * 0.5f;
						else
							col.Radius = 1.0f;
					}
					else if (lower.find("capsule") != std::string::npos ||
						lower.find("capsula") != std::string::npos)
					{
						col.Shape = ColliderShape::Capsule;
						col.Height = 1.8f;
						col.CapsuleRadius = 0.3f;
					}
					// else: Box por padrão
				}
				ImGui::CloseCurrentPopup();
			}

			if (hasCc)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Character Controller (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Character Controller"))
			{
				registry.emplace<CharacterControllerComponent>(entity);
				ImGui::CloseCurrentPopup();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Renderizacao");
			ImGui::Separator();

			if (registry.any_of<MaterialComponent>(entity))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Material (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Material"))
			{
				registry.emplace<MaterialComponent>(entity);
				ImGui::CloseCurrentPopup();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Script");
			ImGui::Separator();

			if (registry.any_of<ScriptComponent>(entity))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Script (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Script"))
			{
				auto& sc = registry.emplace<ScriptComponent>(entity);
				if (auto* nc = registry.try_get<NameComponent>(entity))
					sc.ScriptName = nc->Name + "Script";
				ImGui::CloseCurrentPopup();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Efeitos");
			ImGui::Separator();

			if (registry.any_of<ParticleSystemComponent>(entity))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Particle System (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Particle System"))
			{
				registry.emplace<ParticleSystemComponent>(entity);
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
		ImGui::PopID();

		ImGui::End();
	}

	void InspectorWindow::DrawSpringArm(SpringArmComponent& sa)
	{
		ImGui::DragFloat("Comprimento", &sa.Length, 0.1f, 0.5f, 50.0f, "%.1f m");
		ImGui::DragFloat("Altura", &sa.HeightOffset, 0.1f, 0.0f, 20.0f, "%.1f m");
		ImGui::DragFloat3("Socket Offset", &sa.SocketOffset.x, 0.05f, -10.f, 10.f, "%.2f");
		ImGui::DragFloat("Suavização", &sa.LagSpeed, 0.1f, 0.5f, 30.0f, "%.1f");
		ImGui::Checkbox("Lag de câmera", &sa.EnableCameraLag);
		ImGui::Checkbox("Mouse rotaciona", &sa.MouseRotates);
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
		ImGui::TextDisabled("Céu Procedural e Time of Day estão");
		ImGui::TextDisabled("no componente Directional Light.");
	}

	void InspectorWindow::DrawPointLight(PointLight& light, const glm::vec3& rotationEuler)
	{
		ImGui::Separator();
		ImGui::Text("Point Light");
		ImGui::TextDisabled("Posicione pelo Transform");
		ImGui::ColorEdit3("Cor", glm::value_ptr(light.Color));
		ImGui::DragFloat("Intensidade", &light.Intensity, 0.1f, 0.0f, 100.0f);
		ImGui::DragFloat("Radius", &light.Radius, 0.1f, 0.1f, 200.0f);

		ImGui::Separator();
		ImGui::Checkbox("Spot Light (cone)", &light.IsSpot);
		if (light.IsSpot)
		{
			ImGui::TextDisabled("Direção segue a Rotation do Transform —");
			ImGui::TextDisabled("rotacione o objeto pra apontar o cone.");
			glm::vec3 dir = ComputeSpotDirection(rotationEuler); // valor real, ao vivo
			ImGui::BeginDisabled();
			ImGui::DragFloat3("Direção (calculada)", glm::value_ptr(dir), 0.01f);
			ImGui::EndDisabled();
			ImGui::DragFloat("Ângulo Interno", &light.InnerConeAngle, 0.5f, 0.0f, light.OuterConeAngle);
			ImGui::DragFloat("Ângulo Externo", &light.OuterConeAngle, 0.5f, light.InnerConeAngle, 89.0f);

			ImGui::Spacing();
			ImGui::TextDisabled("Cookie (textura projetada através do cone)");
			DrawTextureSlot("Cookie", light.CookieTexture, light.CookieTextureUUID);
		}

		DrawLightMaterialSlot(light);
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
		ImGui::SliderFloat("Ambient nas Sombras", &light.AmbientShadowFactor, 0.0f, 1.0f);
		ImGui::TextDisabled("  0=interior escuro  1=ambient livre (exterior)");

		ImGui::Separator();
		ImGui::TextDisabled("Sombras");
		ImGui::Checkbox("Cast Shadows", &light.CastShadows);
		if (light.CastShadows)
		{
			ImGui::DragFloat("Shadow Distance", &light.ShadowDistance, 1.0f, 2.0f, 200.0f);
			ImGui::TextDisabled("  Menor = mais preciso (indoor: 8-15, outdoor: 50-100)");
			ImGui::DragFloat("Shadow Bias", &light.ShadowBias, 0.0001f, 0.0001f, 0.05f);
		}

		ImGui::Separator();
		ImGui::TextDisabled("Cookie (textura projetada, com tiling)");
		DrawTextureSlot("Cookie", light.CookieTexture, light.CookieTextureUUID);
		if (light.CookieTexture)
			ImGui::DragFloat("Escala (tamanho do tile)", &light.CookieScale, 0.1f, 0.1f, 200.0f);

		DrawLightMaterialSlot(light);

		// ── Céu Procedural ────────────────────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Céu Procedural");
		ImGui::Checkbox("Ativar Céu Procedural##procsky", &light.ProceduralSky);
		if (light.ProceduralSky)
		{
			ImGui::TextDisabled("  O sol segue a direção desta luz.");
			ImGui::DragFloat("Turbidez", &light.Turbidity, 0.1f, 1.0f, 10.0f);
			ImGui::TextDisabled("  1=limpo  10=poluido/nublado");
			ImGui::DragFloat("Cobertura Nuvens", &light.CloudCoverage, 0.02f, 0.0f, 1.0f);
			ImGui::DragFloat("Velocidade Nuvens", &light.CloudSpeed, 0.001f, 0.0f, 0.2f);
			ImGui::ColorEdit3("Cor Nuvens", &light.CloudColor.x);
			ImGui::ColorEdit3("Cor Noite", &light.NightColor.x);

			// ── Time of Day ─────────────────────────────────────────────────
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Ciclo Dia/Noite");
			ImGui::Checkbox("Ativar Time of Day##tod", &light.TimeOfDayEnabled);
			if (light.TimeOfDayEnabled)
			{
				ImGui::SliderFloat("Hora##tod", &light.Hour, 0.0f, 24.0f);
				ImGui::DragFloat("Velocidade##tod", &light.DaySpeed, 1.0f, 0.1f, 3600.0f);
				ImGui::TextDisabled("  1=tempo real  60=1min/seg  3600=1h/seg");
				ImGui::DragFloat("Latitude##tod", &light.SunLatitude, 1.0f, -90.f, 90.f);
				int h = (int)light.Hour;
				int m = (int)((light.Hour - h) * 60.0f);
				ImGui::Text("  Hora atual: %02d:%02d", h, m);
				ImGui::TextDisabled("  Direcao, Cor e Intensidade sao");
				ImGui::TextDisabled("  automaticas com Time of Day ativo.");
			}
		}
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

	// Particle System — referência a um ParticleSystemAsset (.axepart),
	// mesmo modelo do Material: o componente só guarda o UUID + um cache
	// runtime (Data); os parâmetros vivem no asset, editados no Particle
	// Editor (duplo clique no asset browser, ou botão "Editar" abaixo).
	void InspectorWindow::DrawParticleSystem(entt::entity entity)
	{
		auto& registry = m_Context->ActiveScene->GetRegistry();
		auto* ps = registry.try_get<ParticleSystemComponent>(entity);
		if (!ps) return;

		ImGui::Separator();
		if (!ImGui::CollapsingHeader("Particle System", ImGuiTreeNodeFlags_DefaultOpen)) return;

		std::string uuid = ps->ParticleAssetUUID;

		if (AssetPicker::Draw("System", uuid, { AssetType::ParticleSystem },
			[&](const AssetRecord& record)
			{
				auto asset = ParticleSystemAsset::LoadFromFile(record.FilePath);
				if (!asset) return;
				ps->Data = asset;
				ps->ParticleAssetUUID = record.UUID;
				ps->EmitterRuntimes.assign(asset->Emitters.size(), ParticleEmitterRuntime{});
			}))
		{
			if (uuid.empty()) { ps->Data = nullptr; ps->ParticleAssetUUID.clear(); }
			else ps->ParticleAssetUUID = uuid;
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("New##particle_new"))
		{
			if (ProjectManager::Get().HasProject())
			{
				auto dir = ProjectManager::Get().GetCurrent().AssetsPath / "Particles";
				auto path = dir / "NewParticleSystem.axepart";
				int i = 1;
				while (std::filesystem::exists(path))
					path = dir / ("NewParticleSystem_" + std::to_string(i++) + ".axepart");

				auto asset = ParticleSystemAsset::Create(path.stem().string());
				asset->Save(path);
				auto newUuid = AssetDatabase::Get().Register(path.string());
				AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

				ps->Data = asset;
				ps->ParticleAssetUUID = newUuid;
				ps->EmitterRuntimes.assign(asset->Emitters.size(), ParticleEmitterRuntime{});
			}
		}

		ImGui::Checkbox("Playing", &ps->Playing);

		if (ps->Data && m_OnOpenParticleSystem)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Edit##particle_edit"))
				m_OnOpenParticleSystem(ps->Data);
		}

		if (ps->Data)
		{
			int alive = 0, total = 0;
			for (auto& rt : ps->EmitterRuntimes)
			{
				for (auto& p : rt.Particles) if (p.Alive) ++alive;
				total += (int)rt.Particles.size();
			}
			ImGui::TextDisabled("Alive: %d / %d (Max defined in asset)", alive, total);
		}
		else
		{
			ImGui::TextDisabled("No Particle System assigned.");
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

		// ── TAA ──────────────────────────────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("TAA (Temporal Anti-Aliasing)");
		ImGui::Checkbox("TAA Ativo", &pp.Settings.TAA.Enabled);
		if (pp.Settings.TAA.Enabled)
		{
			ImGui::Separator();
			ImGui::TextDisabled("Base");
			ImGui::DragFloat("Blend Factor", &pp.Settings.TAA.BlendFactor, 0.005f, 0.02f, 0.5f);
			ImGui::TextDisabled("  0.05=suave/ghost  0.15=rapido/ruido");

			ImGui::Separator();
			ImGui::TextDisabled("Emissivos / Animados");
			ImGui::DragFloat("Lum Min", &pp.Settings.TAA.EmissiveLumMin, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Lum Max", &pp.Settings.TAA.EmissiveLumMax, 0.01f, 0.1f, 3.0f);
			ImGui::DragFloat("Blend Max Emissivo", &pp.Settings.TAA.EmissiveBlendMax, 0.01f, 0.1f, 1.0f);
			ImGui::DragFloat("Sensib. Temporal", &pp.Settings.TAA.TemporalSensitivity, 0.1f, 0.0f, 10.0f);
			ImGui::TextDisabled("  Lum Min/Max: range de luminancia para detectar emissivo");
			ImGui::TextDisabled("  Blend Max: o quanto emissivos ignoram o historico");
			ImGui::TextDisabled("  Sensib. Temporal: reatividade a mudancas de cor");

			ImGui::Separator();
			ImGui::Checkbox("Sharpening", &pp.Settings.TAA.Sharpen);
			if (pp.Settings.TAA.Sharpen)
				ImGui::DragFloat("Sharpen Amount", &pp.Settings.TAA.SharpenAmount, 0.05f, 0.f, 1.f);
		}

		// ── SSR — Screen Space Reflections ─────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("SSR (Screen Space Reflections)");
		ImGui::Checkbox("SSR Ativo", &pp.Settings.SSR.Enabled);
		if (pp.Settings.SSR.Enabled)
		{
			ImGui::DragFloat("Max Distance", &pp.Settings.SSR.MaxDistance, 0.5f, 1.f, 100.f);
			ImGui::SliderInt("Max Steps", &pp.Settings.SSR.MaxSteps, 8, 128);
			ImGui::SliderInt("Binary Refine", &pp.Settings.SSR.BinaryRefine, 0, 10);
			ImGui::DragFloat("Thickness", &pp.Settings.SSR.Thickness, 0.05f, 0.05f, 5.f);
			ImGui::DragFloat("Max Roughness", &pp.Settings.SSR.MaxRoughness, 0.02f, 0.f, 1.f);
			ImGui::DragFloat("Intensity", &pp.Settings.SSR.Intensity, 0.05f, 0.f, 2.f);
			ImGui::DragFloat("Edge Fade", &pp.Settings.SSR.EdgeFade, 0.01f, 0.f, 0.5f);
			ImGui::TextDisabled("  Superficies lisas (baixa roughness) refletem a cena.");
		}

		// ── Volumetric Fog ────────────────────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Volumetric Fog");
		ImGui::Checkbox("Fog Ativo", &pp.Settings.Fog.Enabled);
		if (pp.Settings.Fog.Enabled)
		{
			ImGui::ColorEdit3("Cor do Fog", &pp.Settings.Fog.FogColor.x);
			ImGui::DragFloat("Densidade", &pp.Settings.Fog.Density, 0.001f, 0.0f, 1.0f);
			ImGui::DragFloat("Scatter Strength", &pp.Settings.Fog.ScatterStrength, 0.01f, 0.0f, 2.0f);
			ImGui::DragFloat("Ambient Strength", &pp.Settings.Fog.AmbientStrength, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Fog Start", &pp.Settings.Fog.FogStart, 0.5f, 0.0f, 50.0f);
			ImGui::DragFloat("Fog End", &pp.Settings.Fog.FogEnd, 1.0f, 1.0f, 500.0f);
			ImGui::DragFloat("Height Base", &pp.Settings.Fog.HeightBase, 0.1f, -20.0f, 20.0f);
			ImGui::DragFloat("Height Falloff", &pp.Settings.Fog.HeightFalloff, 0.01f, 0.0f, 2.0f);
			ImGui::SliderInt("Ray Steps", &pp.Settings.Fog.Steps, 4, 32);
			ImGui::DragFloat("Jitter", &pp.Settings.Fog.StepJitter, 0.01f, 0.0f, 1.0f);
			ImGui::TextDisabled("Ponto de luz ilumina o volume (scatter).");
		}
	}

	// ==================== Física ====================

		// Helper — header de componente com botão X para remover
	void InspectorWindow::DrawRigidbody(entt::entity entity, entt::registry& registry)
	{
		auto* rb = registry.try_get<RigidbodyComponent>(entity);
		if (!rb) return;

		bool remove = false;
		bool open = DrawComponentHeader("Rigidbody", entity, 0, &remove);
		if (remove) { m_PendingRemove = PendingRemove::Rigidbody; m_PendingRemoveEntity = entity; return; }
		if (!open) return;

		const char* types[] = { "Static", "Dynamic", "Kinematic" };
		int typeIdx = (int)rb->Type;
		if (ImGui::Combo("Tipo", &typeIdx, types, 3))
			rb->Type = (BodyType)typeIdx;

		if (rb->Type != BodyType::Static)
		{
			ImGui::DragFloat("Massa", &rb->Mass, 0.1f, 0.01f, 1000.0f);
			ImGui::DragFloat("Friccao", &rb->Friction, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Restitution", &rb->Restitution, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Linear Damp", &rb->LinearDamping, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Angular Damp", &rb->AngularDamping, 0.01f, 0.0f, 1.0f);
			ImGui::Checkbox("Usar Gravidade", &rb->UseGravity);
			ImGui::Spacing();
			ImGui::TextDisabled("Travar Rotacao:");
			ImGui::SameLine(); ImGui::Checkbox("X##rx", &rb->LockRotX);
			ImGui::SameLine(); ImGui::Checkbox("Y##ry", &rb->LockRotY);
			ImGui::SameLine(); ImGui::Checkbox("Z##rz", &rb->LockRotZ);
		}
	}

	void InspectorWindow::DrawCollider(entt::entity entity, entt::registry& registry)
	{
		auto* col = registry.try_get<ColliderComponent>(entity);
		if (!col) return;

		bool remove = false;
		bool open = DrawComponentHeader("Collider", entity, 1, &remove);
		if (remove) { m_PendingRemove = PendingRemove::Collider; m_PendingRemoveEntity = entity; return; }
		if (!open) return;

		const char* shapes[] = { "Box", "Sphere", "Capsule", "Mesh (Static)", "Convex Hull" };
		int shapeIdx = (int)col->Shape;
		if (ImGui::Combo("Forma", &shapeIdx, shapes, 5))
			col->Shape = (ColliderShape)shapeIdx;

		// Dica de uso
		auto* rb = registry.try_get<RigidbodyComponent>(entity);
		if (rb && rb->Type != BodyType::Static &&
			(col->Shape == ColliderShape::Box ||
				col->Shape == ColliderShape::Sphere ||
				col->Shape == ColliderShape::Capsule ||
				col->Shape == ColliderShape::ConvexHull))
		{
			// ok — shape compatível com dynamic
		}
		else if (col->Shape == ColliderShape::Mesh && rb && rb->Type != BodyType::Static)
		{
			// já mostrado abaixo
		}
		else if (rb && rb->Type != BodyType::Static && col->Shape == ColliderShape::Box)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::TextDisabled("Dica: use Convex Hull para formato exato dinamico");
			ImGui::PopStyleColor();
		}

		ImGui::Checkbox("Is Trigger", &col->IsTrigger);
		ImGui::Checkbox("Debug Wireframe", &col->ShowDebug);
		ImGui::DragFloat3("Offset", &col->Offset.x, 0.01f);

		switch (col->Shape)
		{
		case ColliderShape::Box:
			ImGui::DragFloat3("Half Extent", &col->HalfExtent.x, 0.01f, 0.01f, 100.0f);
			break;
		case ColliderShape::Sphere:
			ImGui::DragFloat("Raio", &col->Radius, 0.01f, 0.01f, 100.0f);
			break;
		case ColliderShape::Capsule:
			ImGui::DragFloat("Altura", &col->Height, 0.01f, 0.1f, 10.0f);
			ImGui::DragFloat("Raio Capsule", &col->CapsuleRadius, 0.01f, 0.01f, 5.0f);
			break;
		case ColliderShape::Mesh:
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.1f, 1.0f));
			ImGui::TextWrapped("Mesh exato do objeto. Use apenas com Rigidbody Static.");
			ImGui::PopStyleColor();
			// Avisa se Rigidbody for Dynamic
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity))
				if (rb->Type != BodyType::Static)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
					ImGui::TextWrapped("AVISO: Mesh Collider requer Rigidbody Static!");
					ImGui::PopStyleColor();
				}
			break;
		case ColliderShape::ConvexHull:
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.1f, 1.0f));
			ImGui::TextWrapped("Convex Hull do mesh. Funciona com Dynamic e Kinematic.");
			ImGui::PopStyleColor();
			break;
		}
	}

	void InspectorWindow::DrawCharacterController(entt::entity entity, entt::registry& registry)
	{
		auto* cc = registry.try_get<CharacterControllerComponent>(entity);
		if (!cc) return;

		bool remove = false;
		bool open = DrawComponentHeader("Character Controller", entity, 2, &remove);
		if (remove) { m_PendingRemove = PendingRemove::CharacterController; m_PendingRemoveEntity = entity; return; }
		if (!open) return;

		ImGui::DragFloat("Altura", &cc->Height, 0.01f, 0.5f, 5.0f);
		ImGui::DragFloat("Raio", &cc->Radius, 0.01f, 0.1f, 2.0f);
		ImGui::DragFloat("Max Slope", &cc->MaxSlopeAngle, 0.5f, 0.0f, 89.0f);
		ImGui::DragFloat("Step Height", &cc->StepHeight, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Max Speed", &cc->MaxSpeed, 0.1f, 0.0f, 50.0f);
		ImGui::DragFloat("Jump Force", &cc->JumpForce, 0.1f, 0.0f, 50.0f);

		if (cc->IsCreated)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Grounded: %s", cc->IsGrounded ? "Sim" : "Nao");
		}
	}

} // namespace axe