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

		// UUID do .axegamemode ativo — vazio = sem GameMode
		std::string ActiveGameModeUUID;

		std::filesystem::path ProjectPath;
		std::filesystem::path RootPath;
		std::filesystem::path AssetsPath;

		bool IsValid() const { return !Name.empty() && !RootPath.empty(); }
	};

} // namespace axe