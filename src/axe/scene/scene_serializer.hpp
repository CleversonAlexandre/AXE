#pragma once
#pragma once
#include "axe/core/types.hpp"
#include "scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include <filesystem>
#include <string>

namespace axe
{

	class AXE_API SceneSerializer
	{
	public:
		// Salva a cena em formato .axescene
		static bool Serialize(const Scene& scene, const std::filesystem::path& filepath,
			const SceneEnvironment* env = nullptr);

		// Carrega uma cena de um .axescene
		static bool Deserialize(const std::filesystem::path& filepath, Scene& scene,
			SceneEnvironment* env = nullptr);

		static std::string SerializeToString(const Scene& scene);
		static bool DeserializeFromString(const std::string& data, Scene& scene);

		// Serialização por entity — para undo/redo de criar/deletar
		static std::string  SerializeEntity(entt::entity entity, const Scene& scene);
		static entt::entity DeserializeEntity(const std::string& data, Scene& scene);

		// Restaura um grupo de entities (ex: pasta + filhos) reconstruindo a hierarquia
		// Retorna a entity raiz (primeira da lista)
		static entt::entity DeserializeEntities(const std::vector<std::string>& snapshots, Scene& scene);

		using MaterialRecompileCallback =
			std::function<void(const std::string& assetUUID, Material* material)>;

		static void SetMaterialRecompileCallback(MaterialRecompileCallback cb)
		{
			s_MaterialRecompileCallback = cb;
		}

		static MaterialRecompileCallback GetMaterialRecompileCallback()
		{
			return s_MaterialRecompileCallback;
		}

		static MaterialRecompileCallback s_MaterialRecompileCallback;

	};

} // namespace axe