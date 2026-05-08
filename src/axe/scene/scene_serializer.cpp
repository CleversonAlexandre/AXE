#include "scene_serializer.hpp"
#include "components.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/mesh_loader.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{

	using json = nlohmann::json;

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
					components["Material"]["color"] = { c->Data->Color.r, c->Data->Color.g, c->Data->Color.b, c->Data->Color.a };
					components["Material"]["specular_strength"] = c->Data->SpecularStrength;
					components["Material"]["shininess"] = c->Data->Shininess;
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
		if (!file.is_open())
			return false;

		json root;
		try { root = json::parse(file); }
		catch (const json::exception& e)
		{
			AXE_CORE_ERROR("SceneSerializer: erro ao parsear '{}': {}", filepath.string(), e.what());
			return false;
		}

		auto& registry = scene.GetRegistry();

		for (const auto& e : root["entities"])
		{
			entt::entity entity = scene.CreateEntity("Entity");
			const auto& components = e["components"];

			// NameComponent
			if (components.contains("Name"))
			{
				auto* c = registry.try_get<NameComponent>(entity);
				if (c) c->Name = components["Name"]["name"];
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
				{
					// Primitiva — gera proceduralmente
					mc.Data = MeshFactory::CreateByUUID(uuid);
				}
				else
				{
					// Asset de arquivo — busca no AssetDatabase
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
					if (record && std::filesystem::exists(record->FilePath))
					{
						auto loaded = MeshLoader::Load(record->FilePath.string());
						mc.Data = loaded.MeshData;
					}
					else
					{
						AXE_CORE_WARN("SceneSerializer: asset '{}' não encontrado.", uuid);
					}
				}
			}

			// MaterialComponent
			if (components.contains("Material"))
			{
				auto& t = components["Material"];
				auto  mat = std::make_shared<Material>(nullptr, "Material");
				mat->Color = { t["color"][0], t["color"][1], t["color"][2], t["color"][3] };
				mat->SpecularStrength = t["specular_strength"];
				mat->Shininess = t["shininess"];
				registry.emplace<MaterialComponent>(entity, mat);
			}

			// LightComponent
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

			if (auto* c = registry.try_get<MaterialComponent>(entity))
			{
				if (c->Data)
				{
					components["Material"]["color"] = { c->Data->Color.r, c->Data->Color.g, c->Data->Color.b, c->Data->Color.a };
					components["Material"]["specular_strength"] = c->Data->SpecularStrength;
					components["Material"]["shininess"] = c->Data->Shininess;
				}
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

				if (components.contains("Material"))
				{
					auto& t = components["Material"];
					auto  mat = std::make_shared<Material>(nullptr, "Material");
					mat->Color = { t["color"][0], t["color"][1], t["color"][2], t["color"][3] };
					mat->SpecularStrength = t["specular_strength"];
					mat->Shininess = t["shininess"];
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