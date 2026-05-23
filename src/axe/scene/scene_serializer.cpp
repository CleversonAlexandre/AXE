#include "scene_serializer.hpp"
#include "components.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/mesh_loader.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <imgui.h>

#include "editor/axe_editor/node_graph/material_graph.hpp"
namespace axe
{

	using json = nlohmann::json;

	SceneSerializer::MaterialRecompileCallback SceneSerializer::s_MaterialRecompileCallback = nullptr;

	bool SceneSerializer::Serialize(const Scene& scene, const std::filesystem::path& filepath)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		json root;
		root["scene"]["name"] = filepath.stem().string();
		root["scene"]["version"] = "1.0";

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
					components["Light"]["specular"] = c->Data->SpecularStrength;
					components["Light"]["shininess"] = c->Data->Shininess;
				}
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

	bool SceneSerializer::Deserialize(const std::filesystem::path& filepath, Scene& scene)
	{
		if (!std::filesystem::exists(filepath))
		{
			AXE_CORE_ERROR("SceneSerializer: arquivo '{}' não encontrado.", filepath.string());
			return false;
		}

		std::ifstream file(filepath);
		if (!file.is_open()) return false;

		json root;
		try { root = json::parse(file); }
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

					// Recompila o shader
					if (s_MaterialRecompileCallback && !mc.MaterialAssetUUID.empty())
					{
						auto shader = s_MaterialRecompileCallback(mc.MaterialAssetUUID);
						if (shader) mat->SetShader(shader);
					}
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
				light->SpecularStrength = t["specular"];
				light->Shininess = t["shininess"];
				registry.emplace<LightComponent>(entity, light);
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

		AXE_CORE_INFO("SceneSerializer: cena carregada de '{}'", filepath.string());
		return true;
	}

	std::string SceneSerializer::SerializeToString(const Scene& scene)
	{
		// Reutiliza a lógica existente mas escreve para string
		// Cria um path temporário em memória — usamos stringstream
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

				registry.emplace<MaterialComponent>(entity, mat);
			}

			if (auto* c = registry.try_get<LightComponent>(entity))
			{
				if (c->Data)
				{
					components["Light"]["direction"] = { c->Data->Direction.x,  c->Data->Direction.y,  c->Data->Direction.z };
					components["Light"]["color"] = { c->Data->Color.x,      c->Data->Color.y,      c->Data->Color.z };
					components["Light"]["intensity"] = c->Data->Intensity;
					components["Light"]["ambient"] = c->Data->AmbientStrength;
					components["Light"]["specular"] = c->Data->SpecularStrength;
					components["Light"]["shininess"] = c->Data->Shininess;
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

			// Reutiliza a lógica do Deserialize
			auto& registry = scene.GetRegistry();

			for (const auto& e : root["entities"])
			{
				entt::entity entity = scene.CreateEntity("Entity");
				const auto& components = e["components"];

				if (components.contains("Name"))
				{
					auto* c = registry.try_get<NameComponent>(entity);
					if (c) c->Name = components["Name"]["name"];
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

					registry.emplace<MaterialComponent>(entity, mat);
				}

				if (components.contains("Light"))
				{
					auto& t = components["Light"];
					auto  light = std::make_shared<DirectionalLight>();
					light->Direction = { t["direction"][0], t["direction"][1], t["direction"][2] };
					light->Color = { t["color"][0],     t["color"][1],     t["color"][2] };
					light->Intensity = t["intensity"];
					light->AmbientStrength = t["ambient"];
					light->SpecularStrength = t["specular"];
					light->Shininess = t["shininess"];
					registry.emplace<LightComponent>(entity, light);
				}
			}

			return true;
		}
		catch (const json::exception& e)
		{
			AXE_CORE_ERROR("SceneSerializer: erro ao deserializar snapshot: {}", e.what());
			return false;
		}
	}


} // namespace axe