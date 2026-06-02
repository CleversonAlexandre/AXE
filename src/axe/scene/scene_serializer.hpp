#pragma once
#pragma once
#include "axe/core/types.hpp"
#include "scene.hpp"
#include <filesystem>
#include <string>

namespace axe
{

	class AXE_API SceneSerializer
	{
	public:
		// Salva a cena em formato .axescene
		static bool Serialize(const Scene& scene, const std::filesystem::path& filepath);

		// Carrega uma cena de um .axescene
		static bool Deserialize(const std::filesystem::path& filepath, Scene& scene);

		static std::string SerializeToString(const Scene& scene);
		static bool DeserializeFromString(const std::string& data, Scene& scene);

		// Callback recebe o UUID do asset e o Material* já criado.
		// Responsabilidade: compilar o .axegraph e setar os shaders (forward + geometry)
		// diretamente no material, além de preencher as texturas.
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