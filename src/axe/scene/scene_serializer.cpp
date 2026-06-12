#include "scene_serializer.hpp"
#include "components.hpp"
#include "axe/script/script_component.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/mesh_loader.hpp"
#include "axe/log/log.hpp"
#include "axe/lighting/point_light.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <imgui.h>
#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include "axe/scene/scene_environment.hpp"

namespace axe
{

	using json = nlohmann::json;

	SceneSerializer::MaterialRecompileCallback SceneSerializer::s_MaterialRecompileCallback = nullptr;

	bool SceneSerializer::Serialize(const Scene& scene, const std::filesystem::path& filepath,
		const SceneEnvironment* env)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		json root;
		root["scene"]["name"] = filepath.stem().string();
		root["scene"]["version"] = "1.0";

		if (env)
		{
			root["scene"]["environment"]["hdri_path"] = env->SkyboxPath;
			root["scene"]["environment"]["skybox_rotation"] = env->SkyboxRotation;
		}

		json entities = json::array();

		auto view = registry.view<entt::entity>();
		for (auto entity : registry.storage<entt::entity>())
		{
			if (!registry.valid(entity))
				continue;

			json e;
			e["id"] = (uint32_t)entity;

			json components;

			// FolderComponent
			if (registry.any_of<FolderComponent>(entity))
			{
				auto& folder = registry.get<FolderComponent>(entity);
				components["Folder"]["color"] = {
					folder.Color.x, folder.Color.y,
					folder.Color.z, folder.Color.w
				};
			}

			// NameComponent
			if (auto* c = registry.try_get<NameComponent>(entity))
				components["Name"]["name"] = c->Name;

			// TransformComponent
			if (auto* c = registry.try_get<TransformComponent>(entity))
			{
				components["Transform"]["position"] = { c->Data.Position.x, c->Data.Position.y, c->Data.Position.z };
				components["Transform"]["rotation"] = { c->Data.Rotation.x, c->Data.Rotation.y, c->Data.Rotation.z };
				components["Transform"]["scale"] = { c->Data.Scale.x,    c->Data.Scale.y,    c->Data.Scale.z };
			}

			// MeshComponent
			if (auto* c = registry.try_get<MeshComponent>(entity))
			{
				components["Mesh"]["uuid"] = c->AssetUUID;
			}


			// MaterialComponent
			if (auto* c = registry.try_get<MaterialComponent>(entity))
			{
				if (c->Data)
				{
					components["Material"]["material_asset_uuid"] = c->MaterialAssetUUID;
					components["Material"]["color"] = { c->Data->Color.r, c->Data->Color.g, c->Data->Color.b, c->Data->Color.a };
					components["Material"]["specular_strength"] = c->Data->SpecularStrength;
					components["Material"]["shininess"] = c->Data->Shininess;
					components["Material"]["metallic"] = c->Data->Metallic;
					components["Material"]["roughness"] = c->Data->Roughness;
					components["Material"]["ao"] = c->Data->AO;
					components["Material"]["use_pbr"] = c->Data->UsePBR;

					// UUIDs das texturas
					components["Material"]["albedo_uuid"] = c->Data->AlbedoUUID;
					components["Material"]["normal_uuid"] = c->Data->NormalUUID;
					components["Material"]["roughness_uuid"] = c->Data->RoughnessUUID;
					components["Material"]["metallic_uuid"] = c->Data->MetallicUUID;
					components["Material"]["ao_uuid"] = c->Data->AOUUID;
				}
			}

			// LightComponent
			if (auto* c = registry.try_get<LightComponent>(entity))
			{
				if (c->Data)
				{
					components["Light"]["direction"] = { c->Data->Direction.x,  c->Data->Direction.y,  c->Data->Direction.z };
					components["Light"]["color"] = { c->Data->Color.x,      c->Data->Color.y,      c->Data->Color.z };
					components["Light"]["intensity"] = c->Data->Intensity;
					components["Light"]["ambient"] = c->Data->AmbientStrength;
					components["Light"]["ibl_intensity"] = c->Data->IBLIntensity;
					components["Light"]["specular"] = c->Data->SpecularStrength;
					components["Light"]["shininess"] = c->Data->Shininess;
				}
			}
			// PointLightComponent
			if (auto* c = registry.try_get<PointLightComponent>(entity))
			{
				if (c->Data)
				{
					components["PointLight"]["position"] = { c->Data->Position.x, c->Data->Position.y, c->Data->Position.z };
					components["PointLight"]["color"] = { c->Data->Color.x,    c->Data->Color.y,    c->Data->Color.z };
					components["PointLight"]["intensity"] = c->Data->Intensity;
					components["PointLight"]["radius"] = c->Data->Radius;
				}
			}
			// PostProcessComponent
			if (auto* c = registry.try_get<PostProcessComponent>(entity))
			{
				components["PostProcess"]["is_global"] = c->IsGlobal;
				components["PostProcess"]["exposure"] = c->Settings.Exposure;
				components["PostProcess"]["bloom_enabled"] = c->Settings.BloomEnabled;
				components["PostProcess"]["bloom_threshold"] = c->Settings.BloomThreshold;
				components["PostProcess"]["bloom_intensity"] = c->Settings.BloomIntensity;
				components["PostProcess"]["bloom_blur_passes"] = c->Settings.BloomBlurPasses;
				components["PostProcess"]["ssao_enabled"] = c->SSAO.Enabled;
				components["PostProcess"]["ssao_radius"] = c->SSAO.Radius;
				components["PostProcess"]["ssao_bias"] = c->SSAO.Bias;
				components["PostProcess"]["ssao_power"] = c->SSAO.Power;
				components["PostProcess"]["ssao_kernel"] = c->SSAO.KernelSize;
			}

			// Física
			if (auto* c = registry.try_get<RigidbodyComponent>(entity))
			{
				components["Rigidbody"]["type"] = (int)c->Type;
				components["Rigidbody"]["mass"] = c->Mass;
				components["Rigidbody"]["friction"] = c->Friction;
				components["Rigidbody"]["restitution"] = c->Restitution;
				components["Rigidbody"]["linear_damping"] = c->LinearDamping;
				components["Rigidbody"]["angular_damping"] = c->AngularDamping;
				components["Rigidbody"]["use_gravity"] = c->UseGravity;
				components["Rigidbody"]["lock_rot_x"] = c->LockRotX;
				components["Rigidbody"]["lock_rot_y"] = c->LockRotY;
				components["Rigidbody"]["lock_rot_z"] = c->LockRotZ;
			}

			if (auto* c = registry.try_get<ColliderComponent>(entity))
			{
				components["Collider"]["shape"] = (int)c->Shape;
				components["Collider"]["offset"] = { c->Offset.x,      c->Offset.y,      c->Offset.z };
				components["Collider"]["half_extent"] = { c->HalfExtent.x,  c->HalfExtent.y,  c->HalfExtent.z };
				components["Collider"]["radius"] = c->Radius;
				components["Collider"]["height"] = c->Height;
				components["Collider"]["capsule_radius"] = c->CapsuleRadius;
				components["Collider"]["is_trigger"] = c->IsTrigger;
			}

			if (auto* c = registry.try_get<CharacterControllerComponent>(entity))
			{
				components["CharacterController"]["height"] = c->Height;
				components["CharacterController"]["radius"] = c->Radius;
				components["CharacterController"]["max_slope"] = c->MaxSlopeAngle;
				components["CharacterController"]["step_height"] = c->StepHeight;
				components["CharacterController"]["max_speed"] = c->MaxSpeed;
				components["CharacterController"]["jump_force"] = c->JumpForce;
			}

			// RelationshipComponent
			if (auto* rel = registry.try_get<RelationshipComponent>(entity))
			{
				if (rel->Parent != entt::null)
					components["Relationship"]["parent"] = (uint32_t)rel->Parent;

				if (!rel->Children.empty())
				{
					json children = json::array();
					for (auto child : rel->Children)
						children.push_back((uint32_t)child);
					components["Relationship"]["children"] = children;
				}
			}


			e["components"] = components;
			entities.push_back(e);
		}

		root["entities"] = entities;

		// Cria as pastas se não existirem
		std::filesystem::create_directories(filepath.parent_path());

		std::ofstream file(filepath);
		if (!file.is_open())
		{
			AXE_CORE_ERROR("SceneSerializer: falha ao abrir '{}' para escrita.", filepath.string());
			return false;
		}

		file << root.dump(4);
		AXE_CORE_INFO("SceneSerializer: cena salva em '{}'", filepath.string());
		return true;
	}

	bool SceneSerializer::Deserialize(const std::filesystem::path& filepath, Scene& scene,
		SceneEnvironment* env)
	{
		if (!std::filesystem::exists(filepath))
		{
			AXE_CORE_ERROR("SceneSerializer: arquivo '{}' não encontrado.", filepath.string());
			return false;
		}

		std::ifstream file(filepath);
		if (!file.is_open()) return false;

		json root;
		try {
			root = json::parse(file);

			if (env && root["scene"].contains("environment"))
			{
				auto& envJson = root["scene"]["environment"];
				std::string hdriPath = envJson.value("hdri_path", "");
				env->SkyboxRotation = envJson.value("skybox_rotation", 0.0f);
				if (!hdriPath.empty() && std::filesystem::exists(hdriPath))
					env->LoadHDRI(hdriPath);
			}
		}

		catch (const json::exception& e)
		{
			AXE_CORE_ERROR("SceneSerializer: erro ao parsear '{}': {}", filepath.string(), e.what());
			return false;
		}

		auto& registry = scene.GetRegistry();

		// Mapa de ID antigo → entity nova
		std::unordered_map<uint32_t, entt::entity> idMap;

		// --- Passo 1: cria entities e componentes ---
		for (const auto& e : root["entities"])
		{
			uint32_t oldID = e["id"];
			const auto& components = e["components"];

			entt::entity entity;

			// Pasta não tem TransformComponent
			if (components.contains("Folder"))
				entity = scene.CreateFolder("Entity");
			else
				entity = scene.CreateEntity("Entity");

			idMap[oldID] = entity;

			// NameComponent
			if (components.contains("Name"))
			{
				auto* c = registry.try_get<NameComponent>(entity);
				if (c) c->Name = components["Name"]["name"];
			}

			// FolderComponent — atualiza cor
			if (components.contains("Folder"))
			{
				auto* f = registry.try_get<FolderComponent>(entity);
				if (f)
				{
					auto& t = components["Folder"]["color"];
					f->Color = ImVec4(t[0], t[1], t[2], t[3]);
				}
			}

			// TransformComponent
			if (components.contains("Transform"))
			{
				auto& c = registry.get_or_emplace<TransformComponent>(entity);
				auto& t = components["Transform"];
				c.Data.Position = { t["position"][0], t["position"][1], t["position"][2] };
				c.Data.Rotation = { t["rotation"][0], t["rotation"][1], t["rotation"][2] };
				c.Data.Scale = { t["scale"][0],    t["scale"][1],    t["scale"][2] };
			}

			// MeshComponent
			if (components.contains("Mesh"))
			{
				std::string uuid = components["Mesh"]["uuid"];
				auto& mc = registry.emplace<MeshComponent>(entity);
				mc.AssetUUID = uuid;

				if (MeshFactory::IsPrimitive(uuid))
					mc.Data = MeshFactory::CreateByUUID(uuid);
				else
				{
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
					if (record && std::filesystem::exists(record->FilePath))
					{
						auto loaded = MeshLoader::Load(record->FilePath.string());
						mc.Data = loaded.MeshData;
					}
					else
						AXE_CORE_WARN("SceneSerializer: asset '{}' não encontrado.", uuid);
				}
			}

			// MaterialComponent
			if (components.contains("Material"))
			{
				auto& t = components["Material"];

				auto mat = std::make_shared<Material>(nullptr, "Material");

				mat->Color = { t["color"][0], t["color"][1], t["color"][2], t["color"][3] };
				mat->SpecularStrength = t["specular_strength"];
				mat->Shininess = t["shininess"];

				// PBR
				if (t.contains("metallic"))  mat->Metallic = t["metallic"];
				if (t.contains("roughness")) mat->Roughness = t["roughness"];
				if (t.contains("ao"))        mat->AO = t["ao"];
				if (t.contains("use_pbr"))   mat->UsePBR = t["use_pbr"];

				// Texturas — carrega pelo UUID
				auto LoadTex = [&](const std::string& key, std::string& uuid,
					std::shared_ptr<Texture2D>& tex)
					{
						if (!t.contains(key)) return;
						uuid = t[key].get<std::string>();
						if (uuid.empty()) return;

						const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
						if (record && std::filesystem::exists(record->FilePath))
							tex = Texture2D::Create(record->FilePath.string());
						else
							AXE_CORE_WARN("SceneSerializer: textura '{}' não encontrada.", uuid);
					};

				LoadTex("albedo_uuid", mat->AlbedoUUID, mat->AlbedoMap);
				LoadTex("normal_uuid", mat->NormalUUID, mat->NormalMap);
				LoadTex("roughness_uuid", mat->RoughnessUUID, mat->RoughnessMap);
				LoadTex("metallic_uuid", mat->MetallicUUID, mat->MetallicMap);
				LoadTex("ao_uuid", mat->AOUUID, mat->AOMap);

				auto mc = MaterialComponent{ mat };
				// Carrega e recompila o shader do grafo se tiver UUID do asset
				if (t.contains("material_asset_uuid"))
				{
					mc.MaterialAssetUUID = t["material_asset_uuid"].get<std::string>();

					// Recompila o shader a partir do .axegraph
					if (s_MaterialRecompileCallback && !mc.MaterialAssetUUID.empty())
						s_MaterialRecompileCallback(mc.MaterialAssetUUID, mat.get());
				}


				registry.emplace<MaterialComponent>(entity, mc);
			}

			// LightComponent
			if (components.contains("Light"))
			{
				auto& t = components["Light"];
				auto light = std::make_shared<DirectionalLight>();
				light->Direction = { t["direction"][0], t["direction"][1], t["direction"][2] };
				light->Color = { t["color"][0],     t["color"][1],     t["color"][2] };
				light->Intensity = t["intensity"];
				light->AmbientStrength = t["ambient"];
				light->IBLIntensity = t.value("ibl_intensity", 1.0f);
				light->SpecularStrength = t["specular"];
				light->Shininess = t["shininess"];
				registry.emplace<LightComponent>(entity, light);
			}

			// PointLightComponent
			if (components.contains("PointLight"))
			{
				auto& t = components["PointLight"];
				auto pl = std::make_shared<PointLight>();
				pl->Position = { t["position"][0], t["position"][1], t["position"][2] };
				pl->Color = { t["color"][0],    t["color"][1],    t["color"][2] };
				pl->Intensity = t["intensity"];
				pl->Radius = t["radius"];
				registry.emplace<PointLightComponent>(entity, pl);
			}

			// PostProcessComponent
			if (components.contains("PostProcess"))
			{
				auto& t = components["PostProcess"];
				PostProcessComponent pp;
				pp.IsGlobal = t["is_global"];
				pp.Settings.Exposure = t["exposure"];
				pp.Settings.BloomEnabled = t["bloom_enabled"];
				pp.Settings.BloomThreshold = t["bloom_threshold"];
				pp.Settings.BloomIntensity = t["bloom_intensity"];
				pp.Settings.BloomBlurPasses = t["bloom_blur_passes"];
				pp.SSAO.Enabled = t.value("ssao_enabled", false);
				pp.SSAO.Radius = t.value("ssao_radius", 0.5f);
				pp.SSAO.Bias = t.value("ssao_bias", 0.025f);
				pp.SSAO.Power = t.value("ssao_power", 2.0f);
				pp.SSAO.KernelSize = t.value("ssao_kernel", 64);
				registry.emplace<PostProcessComponent>(entity, pp);
			}

			if (components.contains("Rigidbody"))
			{
				auto& t = components["Rigidbody"];
				RigidbodyComponent rb;
				rb.Type = (BodyType)t.value("type", 0);
				rb.Mass = t.value("mass", 1.0f);
				rb.Friction = t.value("friction", 0.5f);
				rb.Restitution = t.value("restitution", 0.0f);
				rb.LinearDamping = t.value("linear_damping", 0.05f);
				rb.AngularDamping = t.value("angular_damping", 0.05f);
				rb.UseGravity = t.value("use_gravity", true);
				rb.LockRotX = t.value("lock_rot_x", false);
				rb.LockRotY = t.value("lock_rot_y", false);
				rb.LockRotZ = t.value("lock_rot_z", false);
				registry.emplace<RigidbodyComponent>(entity, rb);
			}

			if (components.contains("Collider"))
			{
				auto& t = components["Collider"];
				ColliderComponent col;
				col.Shape = (ColliderShape)t.value("shape", 0);
				col.Offset = { t["offset"][0],      t["offset"][1],      t["offset"][2] };
				col.HalfExtent = { t["half_extent"][0], t["half_extent"][1], t["half_extent"][2] };
				col.Radius = t.value("radius", 0.5f);
				col.Height = t.value("height", 1.8f);
				col.CapsuleRadius = t.value("capsule_radius", 0.3f);
				col.IsTrigger = t.value("is_trigger", false);
				registry.emplace<ColliderComponent>(entity, col);
			}

			if (components.contains("CharacterController"))
			{
				auto& t = components["CharacterController"];
				CharacterControllerComponent cc;
				cc.Height = t.value("height", 1.8f);
				cc.Radius = t.value("radius", 0.3f);
				cc.MaxSlopeAngle = t.value("max_slope", 45.0f);
				cc.StepHeight = t.value("step_height", 0.3f);
				cc.MaxSpeed = t.value("max_speed", 5.0f);
				cc.JumpForce = t.value("jump_force", 5.0f);
				registry.emplace<CharacterControllerComponent>(entity, cc);
			}
		}



		// --- Passo 2: reconstrói hierarquia ---
		for (const auto& e : root["entities"])
		{
			uint32_t oldID = e["id"];
			if (!idMap.count(oldID)) continue;

			entt::entity entity = idMap[oldID];
			const auto& components = e["components"];

			if (components.contains("Relationship"))
			{
				auto& rel = components["Relationship"];
				if (rel.contains("parent"))
				{
					uint32_t oldParent = rel["parent"];
					if (idMap.count(oldParent))
						scene.SetParent(entity, idMap[oldParent]);
				}
			}
		}

		//AXE_CORE_INFO("SceneSerializer: cena carregada de '{}'", filepath.string());
		return true;
	}

	std::string SceneSerializer::SerializeToString(const Scene& scene)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		json root;
		root["scene"]["name"] = "snapshot";
		root["scene"]["version"] = "1.0";

		json entities = json::array();

		for (auto entity : registry.storage<entt::entity>())
		{
			if (!registry.valid(entity)) continue;

			json e;
			e["id"] = (uint32_t)entity;
			json components;

			// FolderComponent
			if (registry.any_of<FolderComponent>(entity))
			{
				auto& folder = registry.get<FolderComponent>(entity);
				components["Folder"]["color"] = {
					folder.Color.x, folder.Color.y,
					folder.Color.z, folder.Color.w };
			}

			if (auto* c = registry.try_get<NameComponent>(entity))
				components["Name"]["name"] = c->Name;

			if (auto* c = registry.try_get<TransformComponent>(entity))
			{
				components["Transform"]["position"] = { c->Data.Position.x, c->Data.Position.y, c->Data.Position.z };
				components["Transform"]["rotation"] = { c->Data.Rotation.x, c->Data.Rotation.y, c->Data.Rotation.z };
				components["Transform"]["scale"] = { c->Data.Scale.x,    c->Data.Scale.y,    c->Data.Scale.z };
			}

			if (auto* c = registry.try_get<MeshComponent>(entity))
				components["Mesh"]["uuid"] = c->AssetUUID;

			if (auto* c = registry.try_get<MaterialComponent>(entity))
			{
				if (c->Data)
				{
					components["Material"]["material_asset_uuid"] = c->MaterialAssetUUID;
					components["Material"]["color"] = { c->Data->Color.r, c->Data->Color.g, c->Data->Color.b, c->Data->Color.a };
					components["Material"]["specular_strength"] = c->Data->SpecularStrength;
					components["Material"]["shininess"] = c->Data->Shininess;
					components["Material"]["metallic"] = c->Data->Metallic;
					components["Material"]["roughness"] = c->Data->Roughness;
					components["Material"]["ao"] = c->Data->AO;
					components["Material"]["use_pbr"] = c->Data->UsePBR;
					components["Material"]["albedo_uuid"] = c->Data->AlbedoUUID;
					components["Material"]["normal_uuid"] = c->Data->NormalUUID;
					components["Material"]["roughness_uuid"] = c->Data->RoughnessUUID;
					components["Material"]["metallic_uuid"] = c->Data->MetallicUUID;
					components["Material"]["ao_uuid"] = c->Data->AOUUID;
				}
			}

			if (auto* c = registry.try_get<LightComponent>(entity))
			{
				if (c->Data)
				{
					components["Light"]["direction"] = { c->Data->Direction.x, c->Data->Direction.y, c->Data->Direction.z };
					components["Light"]["color"] = { c->Data->Color.x,     c->Data->Color.y,     c->Data->Color.z };
					components["Light"]["intensity"] = c->Data->Intensity;
					components["Light"]["ambient"] = c->Data->AmbientStrength;
					components["Light"]["ibl_intensity"] = c->Data->IBLIntensity;
					components["Light"]["specular"] = c->Data->SpecularStrength;
					components["Light"]["shininess"] = c->Data->Shininess;
				}
			}

			if (auto* c = registry.try_get<PointLightComponent>(entity))
			{
				if (c->Data)
				{
					components["PointLight"]["position"] = { c->Data->Position.x, c->Data->Position.y, c->Data->Position.z };
					components["PointLight"]["color"] = { c->Data->Color.x,    c->Data->Color.y,    c->Data->Color.z };
					components["PointLight"]["intensity"] = c->Data->Intensity;
					components["PointLight"]["radius"] = c->Data->Radius;
				}
			}

			if (auto* c = registry.try_get<PostProcessComponent>(entity))
			{
				components["PostProcess"]["is_global"] = c->IsGlobal;
				components["PostProcess"]["exposure"] = c->Settings.Exposure;
				components["PostProcess"]["bloom_enabled"] = c->Settings.BloomEnabled;
				components["PostProcess"]["bloom_threshold"] = c->Settings.BloomThreshold;
				components["PostProcess"]["bloom_intensity"] = c->Settings.BloomIntensity;
				components["PostProcess"]["bloom_blur_passes"] = c->Settings.BloomBlurPasses;
				components["PostProcess"]["ssao_enabled"] = c->SSAO.Enabled;
				components["PostProcess"]["ssao_radius"] = c->SSAO.Radius;
				components["PostProcess"]["ssao_bias"] = c->SSAO.Bias;
				components["PostProcess"]["ssao_power"] = c->SSAO.Power;
				components["PostProcess"]["ssao_kernel"] = c->SSAO.KernelSize;
			}

			if (auto* c = registry.try_get<CameraComponent>(entity))
			{
				components["Camera"]["fov"] = c->Fov;
				components["Camera"]["near"] = c->NearClip;
				components["Camera"]["far"] = c->FarClip;
				components["Camera"]["move_speed"] = c->MoveSpeed;
				components["Camera"]["sensitivity"] = c->Sensitivity;
				components["Camera"]["is_primary"] = c->IsPrimary;
			}

			if (auto* c = registry.try_get<RigidbodyComponent>(entity))
			{
				components["Rigidbody"]["type"] = (int)c->Type;
				components["Rigidbody"]["mass"] = c->Mass;
				components["Rigidbody"]["friction"] = c->Friction;
				components["Rigidbody"]["restitution"] = c->Restitution;
				components["Rigidbody"]["linear_damping"] = c->LinearDamping;
				components["Rigidbody"]["angular_damping"] = c->AngularDamping;
				components["Rigidbody"]["use_gravity"] = c->UseGravity;
				components["Rigidbody"]["lock_rot_x"] = c->LockRotX;
				components["Rigidbody"]["lock_rot_y"] = c->LockRotY;
				components["Rigidbody"]["lock_rot_z"] = c->LockRotZ;
			}

			if (auto* c = registry.try_get<ColliderComponent>(entity))
			{
				components["Collider"]["shape"] = (int)c->Shape;
				components["Collider"]["offset"] = { c->Offset.x, c->Offset.y, c->Offset.z };
				components["Collider"]["half_extent"] = { c->HalfExtent.x, c->HalfExtent.y, c->HalfExtent.z };
				components["Collider"]["radius"] = c->Radius;
				components["Collider"]["height"] = c->Height;
				components["Collider"]["capsule_radius"] = c->CapsuleRadius;
				components["Collider"]["is_trigger"] = c->IsTrigger;
			}

			if (auto* c = registry.try_get<CharacterControllerComponent>(entity))
			{
				components["CharacterController"]["height"] = c->Height;
				components["CharacterController"]["radius"] = c->Radius;
				components["CharacterController"]["max_slope"] = c->MaxSlopeAngle;
				components["CharacterController"]["step_height"] = c->StepHeight;
				components["CharacterController"]["max_speed"] = c->MaxSpeed;
				components["CharacterController"]["jump_force"] = c->JumpForce;
			}


			if (auto* sa = registry.try_get<SpringArmComponent>(entity))
			{
				components["SpringArm"]["length"] = sa->Length;
				components["SpringArm"]["height_offset"] = sa->HeightOffset;
				components["SpringArm"]["socket_offset"] = { sa->SocketOffset.x, sa->SocketOffset.y, sa->SocketOffset.z };
				components["SpringArm"]["lag_speed"] = sa->LagSpeed;
				components["SpringArm"]["enable_lag"] = sa->EnableCameraLag;
				components["SpringArm"]["mouse_rotates"] = sa->MouseRotates;
			}

			if (auto* sc = registry.try_get<ScriptComponent>(entity))
			{
				components["Script"]["asset_path"] = sc->ScriptAssetPath;
				components["Script"]["dll_path"] = sc->DllPath;
				components["Script"]["name"] = sc->ScriptName;
				components["Script"]["compiled"] = sc->IsCompiled;
			}

			// RelationshipComponent — essencial para restaurar hierarquia
			if (auto* rel = registry.try_get<RelationshipComponent>(entity))
			{
				if (rel->Parent != entt::null)
					components["Relationship"]["parent"] = (uint32_t)rel->Parent;
				if (!rel->Children.empty())
				{
					json children = json::array();
					for (auto child : rel->Children)
						children.push_back((uint32_t)child);
					components["Relationship"]["children"] = children;
				}
			}

			e["components"] = components;
			entities.push_back(e);
		}

		root["entities"] = entities;
		return root.dump();
	}


	bool SceneSerializer::DeserializeFromString(const std::string& data, Scene& scene)
	{
		try
		{
			json root = json::parse(data);
			auto& registry = scene.GetRegistry();

			// Mapa de ID antigo -> entity nova
			std::unordered_map<uint32_t, entt::entity> idMap;

			// Passo 1: cria entities e componentes
			for (const auto& e : root["entities"])
			{
				uint32_t oldID = e["id"];
				const auto& components = e["components"];

				entt::entity entity;
				if (components.contains("Folder"))
					entity = scene.CreateFolder("Folder");
				else
					entity = scene.CreateEntity("Entity");

				idMap[oldID] = entity;

				if (components.contains("Name"))
					if (auto* c = registry.try_get<NameComponent>(entity))
						c->Name = components["Name"]["name"];

				if (components.contains("Folder"))
					if (auto* f = registry.try_get<FolderComponent>(entity))
					{
						auto& col = components["Folder"]["color"];
						f->Color = ImVec4(col[0], col[1], col[2], col[3]);
					}

				if (components.contains("Transform"))
				{
					auto& c = registry.get_or_emplace<TransformComponent>(entity);
					auto& t = components["Transform"];
					c.Data.Position = { t["position"][0], t["position"][1], t["position"][2] };
					c.Data.Rotation = { t["rotation"][0], t["rotation"][1], t["rotation"][2] };
					c.Data.Scale = { t["scale"][0],    t["scale"][1],    t["scale"][2] };
				}

				if (components.contains("Mesh"))
				{
					std::string uuid = components["Mesh"]["uuid"];
					auto& mc = registry.emplace<MeshComponent>(entity);
					mc.AssetUUID = uuid;
					if (MeshFactory::IsPrimitive(uuid))
						mc.Data = MeshFactory::CreateByUUID(uuid);
					else
					{
						const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
						if (record && std::filesystem::exists(record->FilePath))
						{
							auto loaded = MeshLoader::Load(record->FilePath.string());
							mc.Data = loaded.MeshData;
						}
					}
				}

				if (components.contains("Material"))
				{
					auto& t = components["Material"];
					auto mat = std::make_shared<Material>(nullptr, "Material");
					mat->Color = { t["color"][0], t["color"][1], t["color"][2], t["color"][3] };
					mat->SpecularStrength = t["specular_strength"];
					mat->Shininess = t["shininess"];
					if (t.contains("metallic"))  mat->Metallic = t["metallic"];
					if (t.contains("roughness")) mat->Roughness = t["roughness"];
					if (t.contains("ao"))        mat->AO = t["ao"];
					if (t.contains("use_pbr"))   mat->UsePBR = t["use_pbr"];

					auto LoadTex = [&](const std::string& key, std::string& uuid, std::shared_ptr<Texture2D>& tex)
						{
							if (!t.contains(key)) return;
							uuid = t[key].get<std::string>();
							if (uuid.empty()) return;
							const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
							if (record && std::filesystem::exists(record->FilePath))
								tex = Texture2D::Create(record->FilePath.string());
						};
					LoadTex("albedo_uuid", mat->AlbedoUUID, mat->AlbedoMap);
					LoadTex("normal_uuid", mat->NormalUUID, mat->NormalMap);
					LoadTex("roughness_uuid", mat->RoughnessUUID, mat->RoughnessMap);
					LoadTex("metallic_uuid", mat->MetallicUUID, mat->MetallicMap);
					LoadTex("ao_uuid", mat->AOUUID, mat->AOMap);

					auto mc = MaterialComponent{ mat };
					if (t.contains("material_asset_uuid"))
					{
						mc.MaterialAssetUUID = t["material_asset_uuid"].get<std::string>();
						if (s_MaterialRecompileCallback && !mc.MaterialAssetUUID.empty())
							s_MaterialRecompileCallback(mc.MaterialAssetUUID, mat.get());
					}
					registry.emplace<MaterialComponent>(entity, mc);
				}

				if (components.contains("Light"))
				{
					auto& t = components["Light"];
					auto light = std::make_shared<DirectionalLight>();
					light->Direction = { t["direction"][0], t["direction"][1], t["direction"][2] };
					light->Color = { t["color"][0],     t["color"][1],     t["color"][2] };
					light->Intensity = t["intensity"];
					light->AmbientStrength = t["ambient"];
					light->IBLIntensity = t.value("ibl_intensity", 1.0f);
					light->SpecularStrength = t["specular"];
					light->Shininess = t["shininess"];
					registry.emplace<LightComponent>(entity, light);
				}

				if (components.contains("PointLight"))
				{
					auto& t = components["PointLight"];
					auto pl = std::make_shared<PointLight>();
					pl->Position = { t["position"][0], t["position"][1], t["position"][2] };
					pl->Color = { t["color"][0],    t["color"][1],    t["color"][2] };
					pl->Intensity = t["intensity"];
					pl->Radius = t["radius"];
					registry.emplace<PointLightComponent>(entity, pl);
				}

				if (components.contains("PostProcess"))
				{
					auto& t = components["PostProcess"];
					PostProcessComponent pp;
					pp.IsGlobal = t["is_global"];
					pp.Settings.Exposure = t["exposure"];
					pp.Settings.BloomEnabled = t["bloom_enabled"];
					pp.Settings.BloomThreshold = t["bloom_threshold"];
					pp.Settings.BloomIntensity = t["bloom_intensity"];
					pp.Settings.BloomBlurPasses = t["bloom_blur_passes"];
					pp.SSAO.Enabled = t.value("ssao_enabled", false);
					pp.SSAO.Radius = t.value("ssao_radius", 0.5f);
					pp.SSAO.Bias = t.value("ssao_bias", 0.025f);
					pp.SSAO.Power = t.value("ssao_power", 2.0f);
					pp.SSAO.KernelSize = t.value("ssao_kernel", 64);
					registry.emplace<PostProcessComponent>(entity, pp);
				}

				if (components.contains("Camera"))
				{
					auto& t = components["Camera"];
					CameraComponent cam;
					cam.Fov = t.value("fov", 60.0f);
					cam.NearClip = t.value("near", 0.1f);
					cam.FarClip = t.value("far", 1000.0f);
					cam.MoveSpeed = t.value("move_speed", 5.0f);
					cam.Sensitivity = t.value("sensitivity", 0.1f);
					cam.IsPrimary = t.value("is_primary", true);
					registry.emplace<CameraComponent>(entity, cam);
				}
				if (components.contains("SpringArm"))
				{
					auto& t = components["SpringArm"];
					SpringArmComponent sa;
					sa.Length = t.value("length", 5.0f);
					sa.HeightOffset = t.value("height_offset", 0.0f);
					sa.LagSpeed = t.value("lag_speed", 8.0f);
					sa.EnableCameraLag = t.value("enable_lag", true);
					sa.MouseRotates = t.value("mouse_rotates", true);
					if (t.contains("socket_offset") && t["socket_offset"].size() == 3)
						sa.SocketOffset = { t["socket_offset"][0], t["socket_offset"][1], t["socket_offset"][2] };
					registry.emplace<SpringArmComponent>(entity, sa);
				}

				if (components.contains("Script"))
				{
					auto& t = components["Script"];
					ScriptComponent sc;
					sc.ScriptAssetPath = t.value("asset_path", "");
					sc.DllPath = t.value("dll_path", "");
					sc.ScriptName = t.value("name", "");
					sc.IsCompiled = t.value("compiled", false);
					registry.emplace<ScriptComponent>(entity, sc);
				}



				if (components.contains("Rigidbody"))
				{
					auto& t = components["Rigidbody"];
					RigidbodyComponent rb;
					rb.Type = (BodyType)t.value("type", 0);
					rb.Mass = t.value("mass", 1.0f);
					rb.Friction = t.value("friction", 0.5f);
					rb.Restitution = t.value("restitution", 0.0f);
					rb.LinearDamping = t.value("linear_damping", 0.05f);
					rb.AngularDamping = t.value("angular_damping", 0.05f);
					rb.UseGravity = t.value("use_gravity", true);
					rb.LockRotX = t.value("lock_rot_x", false);
					rb.LockRotY = t.value("lock_rot_y", false);
					rb.LockRotZ = t.value("lock_rot_z", false);
					registry.emplace<RigidbodyComponent>(entity, rb);
				}

				if (components.contains("Collider"))
				{
					auto& t = components["Collider"];
					ColliderComponent col;
					col.Shape = (ColliderShape)t.value("shape", 0);
					col.Offset = { t["offset"][0],      t["offset"][1],      t["offset"][2] };
					col.HalfExtent = { t["half_extent"][0], t["half_extent"][1], t["half_extent"][2] };
					col.Radius = t.value("radius", 0.5f);
					col.Height = t.value("height", 1.8f);
					col.CapsuleRadius = t.value("capsule_radius", 0.3f);
					col.IsTrigger = t.value("is_trigger", false);
					registry.emplace<ColliderComponent>(entity, col);
				}

				if (components.contains("CharacterController"))
				{
					auto& t = components["CharacterController"];
					CharacterControllerComponent cc;
					cc.Height = t.value("height", 1.8f);
					cc.Radius = t.value("radius", 0.3f);
					cc.MaxSlopeAngle = t.value("max_slope", 45.0f);
					cc.StepHeight = t.value("step_height", 0.3f);
					cc.MaxSpeed = t.value("max_speed", 5.0f);
					cc.JumpForce = t.value("jump_force", 5.0f);
					registry.emplace<CharacterControllerComponent>(entity, cc);
				}
			}

			// Passo 2: reconstrói hierarquia
			for (const auto& e : root["entities"])
			{
				uint32_t oldID = e["id"];
				if (!idMap.count(oldID)) continue;
				entt::entity entity = idMap[oldID];
				const auto& components = e["components"];

				if (components.contains("Relationship"))
				{
					auto& rel = components["Relationship"];
					if (rel.contains("parent"))
					{
						uint32_t oldParent = rel["parent"];
						if (idMap.count(oldParent))
							scene.SetParent(entity, idMap[oldParent]);
					}
				}
			}

			AXE_CORE_INFO("SceneSerializer: snapshot restaurado.");
			return true;
		}
		catch (const json::exception& e)
		{
			AXE_CORE_ERROR("SceneSerializer: erro ao deserializar snapshot: {}", e.what());
			return false;
		}
	}


	std::string SceneSerializer::SerializeEntity(entt::entity entity, const Scene& scene)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();
		if (!registry.valid(entity)) return "";

		json e;
		e["id"] = (uint32_t)entity;
		json components;

		if (registry.any_of<FolderComponent>(entity))
		{
			auto& folder = registry.get<FolderComponent>(entity);
			components["Folder"]["color"] = {
				folder.Color.x, folder.Color.y, folder.Color.z, folder.Color.w };
		}

		if (auto* c = registry.try_get<NameComponent>(entity))
			components["Name"]["name"] = c->Name;

		if (auto* c = registry.try_get<TransformComponent>(entity))
		{
			components["Transform"]["position"] = { c->Data.Position.x, c->Data.Position.y, c->Data.Position.z };
			components["Transform"]["rotation"] = { c->Data.Rotation.x, c->Data.Rotation.y, c->Data.Rotation.z };
			components["Transform"]["scale"] = { c->Data.Scale.x,    c->Data.Scale.y,    c->Data.Scale.z };
		}

		if (auto* c = registry.try_get<MeshComponent>(entity))
			components["Mesh"]["uuid"] = c->AssetUUID;

		if (auto* c = registry.try_get<MaterialComponent>(entity))
		{
			if (c->Data)
			{
				components["Material"]["material_asset_uuid"] = c->MaterialAssetUUID;
				components["Material"]["color"] = { c->Data->Color.r, c->Data->Color.g, c->Data->Color.b, c->Data->Color.a };
				components["Material"]["specular_strength"] = c->Data->SpecularStrength;
				components["Material"]["shininess"] = c->Data->Shininess;
				components["Material"]["metallic"] = c->Data->Metallic;
				components["Material"]["roughness"] = c->Data->Roughness;
				components["Material"]["ao"] = c->Data->AO;
				components["Material"]["use_pbr"] = c->Data->UsePBR;
				components["Material"]["albedo_uuid"] = c->Data->AlbedoUUID;
				components["Material"]["normal_uuid"] = c->Data->NormalUUID;
				components["Material"]["roughness_uuid"] = c->Data->RoughnessUUID;
				components["Material"]["metallic_uuid"] = c->Data->MetallicUUID;
				components["Material"]["ao_uuid"] = c->Data->AOUUID;
			}
		}

		if (auto* c = registry.try_get<LightComponent>(entity))
		{
			if (c->Data)
			{
				components["Light"]["direction"] = { c->Data->Direction.x, c->Data->Direction.y, c->Data->Direction.z };
				components["Light"]["color"] = { c->Data->Color.x,     c->Data->Color.y,     c->Data->Color.z };
				components["Light"]["intensity"] = c->Data->Intensity;
				components["Light"]["ambient"] = c->Data->AmbientStrength;
				components["Light"]["ibl_intensity"] = c->Data->IBLIntensity;
				components["Light"]["specular"] = c->Data->SpecularStrength;
				components["Light"]["shininess"] = c->Data->Shininess;
			}
		}

		if (auto* c = registry.try_get<PointLightComponent>(entity))
		{
			if (c->Data)
			{
				components["PointLight"]["position"] = { c->Data->Position.x, c->Data->Position.y, c->Data->Position.z };
				components["PointLight"]["color"] = { c->Data->Color.x,    c->Data->Color.y,    c->Data->Color.z };
				components["PointLight"]["intensity"] = c->Data->Intensity;
				components["PointLight"]["radius"] = c->Data->Radius;
			}
		}

		if (auto* c = registry.try_get<PostProcessComponent>(entity))
		{
			components["PostProcess"]["is_global"] = c->IsGlobal;
			components["PostProcess"]["exposure"] = c->Settings.Exposure;
			components["PostProcess"]["bloom_enabled"] = c->Settings.BloomEnabled;
			components["PostProcess"]["bloom_threshold"] = c->Settings.BloomThreshold;
			components["PostProcess"]["bloom_intensity"] = c->Settings.BloomIntensity;
			components["PostProcess"]["bloom_blur_passes"] = c->Settings.BloomBlurPasses;
			components["PostProcess"]["ssao_enabled"] = c->SSAO.Enabled;
			components["PostProcess"]["ssao_radius"] = c->SSAO.Radius;
			components["PostProcess"]["ssao_bias"] = c->SSAO.Bias;
			components["PostProcess"]["ssao_power"] = c->SSAO.Power;
			components["PostProcess"]["ssao_kernel"] = c->SSAO.KernelSize;
		}

		if (auto* c = registry.try_get<CameraComponent>(entity))
		{
			components["Camera"]["fov"] = c->Fov;
			components["Camera"]["near"] = c->NearClip;
			components["Camera"]["far"] = c->FarClip;
			components["Camera"]["move_speed"] = c->MoveSpeed;
			components["Camera"]["sensitivity"] = c->Sensitivity;
			components["Camera"]["is_primary"] = c->IsPrimary;
		}

		// RelationshipComponent — salva IDs para reconstruir hierarquia no undo
		if (auto* rel = registry.try_get<RelationshipComponent>(entity))
		{
			if (rel->Parent != entt::null)
				components["Relationship"]["parent"] = (uint32_t)rel->Parent;
			if (!rel->Children.empty())
			{
				json children = json::array();
				for (auto child : rel->Children)
					children.push_back((uint32_t)child);
				components["Relationship"]["children"] = children;
			}
		}

		e["components"] = components;
		return e.dump();
	}

	entt::entity SceneSerializer::DeserializeEntity(const std::string& data, Scene& scene)
	{
		if (data.empty()) return entt::null;

		try
		{
			json e = json::parse(data);
			auto& registry = scene.GetRegistry();
			const auto& components = e["components"];

			entt::entity entity;
			if (components.contains("Folder"))
				entity = scene.CreateFolder("Folder");
			else
				entity = scene.CreateEntity("Entity");

			if (components.contains("Name"))
				if (auto* c = registry.try_get<NameComponent>(entity))
					c->Name = components["Name"]["name"];

			if (components.contains("Folder"))
				if (auto* f = registry.try_get<FolderComponent>(entity))
				{
					auto& t = components["Folder"]["color"];
					f->Color = ImVec4(t[0], t[1], t[2], t[3]);
				}

			if (components.contains("Transform"))
			{
				auto& c = registry.get_or_emplace<TransformComponent>(entity);
				auto& t = components["Transform"];
				c.Data.Position = { t["position"][0], t["position"][1], t["position"][2] };
				c.Data.Rotation = { t["rotation"][0], t["rotation"][1], t["rotation"][2] };
				c.Data.Scale = { t["scale"][0],    t["scale"][1],    t["scale"][2] };
			}

			if (components.contains("Mesh"))
			{
				std::string uuid = components["Mesh"]["uuid"];
				auto& mc = registry.emplace<MeshComponent>(entity);
				mc.AssetUUID = uuid;
				if (MeshFactory::IsPrimitive(uuid))
					mc.Data = MeshFactory::CreateByUUID(uuid);
				else
				{
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
					if (record && std::filesystem::exists(record->FilePath))
					{
						auto loaded = MeshLoader::Load(record->FilePath.string());
						mc.Data = loaded.MeshData;
					}
				}
			}

			if (components.contains("Material"))
			{
				auto& t = components["Material"];
				auto mat = std::make_shared<Material>(nullptr, "Material");
				mat->Color = { t["color"][0], t["color"][1], t["color"][2], t["color"][3] };
				mat->SpecularStrength = t["specular_strength"];
				mat->Shininess = t["shininess"];
				if (t.contains("metallic"))  mat->Metallic = t["metallic"];
				if (t.contains("roughness")) mat->Roughness = t["roughness"];
				if (t.contains("ao"))        mat->AO = t["ao"];
				if (t.contains("use_pbr"))   mat->UsePBR = t["use_pbr"];

				auto mc = MaterialComponent{ mat };
				if (t.contains("material_asset_uuid"))
				{
					mc.MaterialAssetUUID = t["material_asset_uuid"].get<std::string>();
					if (s_MaterialRecompileCallback && !mc.MaterialAssetUUID.empty())
						s_MaterialRecompileCallback(mc.MaterialAssetUUID, mat.get());
				}
				registry.emplace<MaterialComponent>(entity, mc);
			}

			if (components.contains("Light"))
			{
				auto& t = components["Light"];
				auto light = std::make_shared<DirectionalLight>();
				light->Direction = { t["direction"][0], t["direction"][1], t["direction"][2] };
				light->Color = { t["color"][0],     t["color"][1],     t["color"][2] };
				light->Intensity = t["intensity"];
				light->AmbientStrength = t["ambient"];
				light->IBLIntensity = t.value("ibl_intensity", 1.0f);
				light->SpecularStrength = t["specular"];
				light->Shininess = t["shininess"];
				registry.emplace<LightComponent>(entity, light);
			}

			if (components.contains("PointLight"))
			{
				auto& t = components["PointLight"];
				auto pl = std::make_shared<PointLight>();
				pl->Position = { t["position"][0], t["position"][1], t["position"][2] };
				pl->Color = { t["color"][0],    t["color"][1],    t["color"][2] };
				pl->Intensity = t["intensity"];
				pl->Radius = t["radius"];
				registry.emplace<PointLightComponent>(entity, pl);
			}

			if (components.contains("PostProcess"))
			{
				auto& t = components["PostProcess"];
				PostProcessComponent pp;
				pp.IsGlobal = t["is_global"];
				pp.Settings.Exposure = t["exposure"];
				pp.Settings.BloomEnabled = t["bloom_enabled"];
				pp.Settings.BloomThreshold = t["bloom_threshold"];
				pp.Settings.BloomIntensity = t["bloom_intensity"];
				pp.Settings.BloomBlurPasses = t["bloom_blur_passes"];
				pp.SSAO.Enabled = t.value("ssao_enabled", false);
				pp.SSAO.Radius = t.value("ssao_radius", 0.5f);
				pp.SSAO.Bias = t.value("ssao_bias", 0.025f);
				pp.SSAO.Power = t.value("ssao_power", 2.0f);
				pp.SSAO.KernelSize = t.value("ssao_kernel", 64);
				registry.emplace<PostProcessComponent>(entity, pp);
			}

			if (components.contains("Camera"))
			{
				auto& t = components["Camera"];
				CameraComponent cam;
				cam.Fov = t.value("fov", 60.0f);
				cam.NearClip = t.value("near", 0.1f);
				cam.FarClip = t.value("far", 1000.0f);
				cam.MoveSpeed = t.value("move_speed", 5.0f);
				cam.Sensitivity = t.value("sensitivity", 0.1f);
				cam.IsPrimary = t.value("is_primary", true);
				registry.emplace<CameraComponent>(entity, cam);
			}

			return entity;
		}
		catch (const json::exception& ex)
		{
			AXE_CORE_ERROR("SceneSerializer::DeserializeEntity falhou: {}", ex.what());
			return entt::null;
		}
	}

	entt::entity SceneSerializer::DeserializeEntities(
		const std::vector<std::string>& snapshots, Scene& scene)
	{
		if (snapshots.empty()) return entt::null;

		auto& registry = scene.GetRegistry();

		// Mapa de ID antigo → entity nova
		std::unordered_map<uint32_t, entt::entity> idMap;

		// Passo 1 — cria todas as entities sem hierarquia
		for (const auto& snap : snapshots)
		{
			if (snap.empty()) continue;
			try
			{
				json e = json::parse(snap);
				uint32_t oldID = e["id"];
				const auto& components = e["components"];

				entt::entity entity;
				if (components.contains("Folder"))
					entity = scene.CreateFolder("Folder");
				else
					entity = scene.CreateEntity("Entity");

				idMap[oldID] = entity;

				if (components.contains("Name"))
					if (auto* c = registry.try_get<NameComponent>(entity))
						c->Name = components["Name"]["name"];

				if (components.contains("Folder"))
					if (auto* f = registry.try_get<FolderComponent>(entity))
					{
						auto& t = components["Folder"]["color"];
						f->Color = ImVec4(t[0], t[1], t[2], t[3]);
					}

				if (components.contains("Transform"))
				{
					auto& c = registry.get_or_emplace<TransformComponent>(entity);
					auto& t = components["Transform"];
					c.Data.Position = { t["position"][0], t["position"][1], t["position"][2] };
					c.Data.Rotation = { t["rotation"][0], t["rotation"][1], t["rotation"][2] };
					c.Data.Scale = { t["scale"][0],    t["scale"][1],    t["scale"][2] };
				}

				if (components.contains("Mesh"))
				{
					std::string uuid = components["Mesh"]["uuid"];
					auto& mc = registry.emplace<MeshComponent>(entity);
					mc.AssetUUID = uuid;
					if (MeshFactory::IsPrimitive(uuid))
						mc.Data = MeshFactory::CreateByUUID(uuid);
					else
					{
						const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
						if (record && std::filesystem::exists(record->FilePath))
						{
							auto loaded = MeshLoader::Load(record->FilePath.string());
							mc.Data = loaded.MeshData;
						}
					}
				}

				if (components.contains("Material"))
				{
					auto& t = components["Material"];
					auto  mat = std::make_shared<Material>(nullptr, "Material");
					mat->Color = { t["color"][0], t["color"][1], t["color"][2], t["color"][3] };
					mat->SpecularStrength = t["specular_strength"];
					mat->Shininess = t["shininess"];
					if (t.contains("metallic"))  mat->Metallic = t["metallic"];
					if (t.contains("roughness")) mat->Roughness = t["roughness"];
					if (t.contains("ao"))        mat->AO = t["ao"];
					if (t.contains("use_pbr"))   mat->UsePBR = t["use_pbr"];
					auto mc = MaterialComponent{ mat };
					if (t.contains("material_asset_uuid"))
					{
						mc.MaterialAssetUUID = t["material_asset_uuid"].get<std::string>();
						if (s_MaterialRecompileCallback && !mc.MaterialAssetUUID.empty())
							s_MaterialRecompileCallback(mc.MaterialAssetUUID, mat.get());
					}
					registry.emplace<MaterialComponent>(entity, mc);
				}

				if (components.contains("PointLight"))
				{
					auto& t = components["PointLight"];
					auto pl = std::make_shared<PointLight>();
					pl->Position = { t["position"][0], t["position"][1], t["position"][2] };
					pl->Color = { t["color"][0],    t["color"][1],    t["color"][2] };
					pl->Intensity = t["intensity"];
					pl->Radius = t["radius"];
					registry.emplace<PointLightComponent>(entity, pl);
				}
			}
			catch (...) {}
		}

		// Passo 2 — reconstrói hierarquia com IDs mapeados
		for (const auto& snap : snapshots)
		{
			if (snap.empty()) continue;
			try
			{
				json e = json::parse(snap);
				uint32_t oldID = e["id"];
				if (!idMap.count(oldID)) continue;

				entt::entity entity = idMap[oldID];
				const auto& components = e["components"];

				if (components.contains("Relationship"))
				{
					auto& rel = components["Relationship"];
					// Só seta parent se o pai está no mesmo grupo restaurado
					if (rel.contains("parent"))
					{
						uint32_t oldParent = rel["parent"];
						if (idMap.count(oldParent))
							scene.SetParent(entity, idMap[oldParent]);
					}
				}
			}
			catch (...) {}
		}

		// Retorna a primeira entity (raiz do grupo)
		if (!idMap.empty())
		{
			try
			{
				json first = json::parse(snapshots[0]);
				uint32_t firstID = first["id"];
				if (idMap.count(firstID))
					return idMap[firstID];
			}
			catch (...) {}
		}

		return entt::null;
	}

} // namespace axe