#pragma once
#include "axe/core/types.hpp"
#include <string>
#include <filesystem>

namespace axe
{

	struct AXE_API Project
	{
		std::string Name = "StartScene";
		std::string Version = "1.0.0";
		std::string EngineVersion = "0.1.0";
		std::string StartScene = "";

		std::filesystem::path ProjectPath;  // caminho do axe.project
		std::filesystem::path RootPath;     // pasta raiz do projeto
		std::filesystem::path AssetsPath;   // pasta Assets/

		bool IsValid() const { return !Name.empty() && !RootPath.empty(); }
	};

} // namespace axe