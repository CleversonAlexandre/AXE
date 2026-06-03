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