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
	};

} // namespace axe