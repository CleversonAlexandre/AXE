#pragma once
#include "axe/core/types.hpp"
#include <string>
#include <filesystem>

namespace axe
{

	enum class AssetType
	{
		Unknown,
		Mesh,       // .obj, .gltf, .glb
		Texture,    // .png, .jpg, .jpeg
		Scene,      // .axescene
		Audio,      // .wav, .mp3
		Script,      // .lua (futuro)
		Material,
		GameMode,    // .axegamemode
		ParticleSystem // .axepart
	};

	// Converte extensão para tipo
	static AssetType AssetTypeFromExtension(const std::string& ext)
	{
		if (ext == ".obj" || ext == ".gltf" || ext == ".glb")return AssetType::Mesh;
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")return AssetType::Texture;
		if (ext == ".axescene")                              return AssetType::Scene;
		if (ext == ".wav" || ext == ".mp3")                  return AssetType::Audio;
		if (ext == ".lua" || ext == ".axescript")           return AssetType::Script;
		if (ext == ".axemat")                                return AssetType::Material;
		if (ext == ".axegamemode")                           return AssetType::GameMode;
		if (ext == ".axepart")                                return AssetType::ParticleSystem;
		return AssetType::Unknown;
	}

	static std::string AssetTypeToString(AssetType type)
	{
		switch (type)
		{
		case AssetType::Mesh:		return "Mesh";
		case AssetType::Texture:	return "Texture";
		case AssetType::Scene:		return "Scene";
		case AssetType::Audio:		return "Audio";
		case AssetType::Script:		return "Script";
		case AssetType::Material:	return "Material";
		case AssetType::GameMode:	return "GameMode";
		case AssetType::ParticleSystem: return "ParticleSystem";

		default:					return "Unknown";
		}
	}

	static AssetType AssetTypeFromString(const std::string& str)
	{
		if (str == "Mesh")    return AssetType::Mesh;
		if (str == "Texture") return AssetType::Texture;
		if (str == "Scene")   return AssetType::Scene;
		if (str == "Audio")   return AssetType::Audio;
		if (str == "Script")  return AssetType::Script;
		if (str == "Material")  return AssetType::Material;
		if (str == "GameMode")  return AssetType::GameMode;
		if (str == "ParticleSystem") return AssetType::ParticleSystem;
		return AssetType::Unknown;
	}

	// Um asset registrado no database
	struct AssetRecord
	{
		std::string           UUID;
		std::filesystem::path FilePath;
		AssetType             Type = AssetType::Unknown;
		std::string           Name;
		std::string           VirtualFolder = ""; // pasta virtual no browser

		// Apenas para Type == Script: subtipo lido do .axescript
		// Valores: "Entity", "Agent", "Character", "StaticObject", "Trigger"
		std::string           ScriptClassType;

		bool IsValid() const { return !UUID.empty() && !FilePath.empty(); }
	};

	// Pasta virtual do asset browser — pode ter cor e subpastas
	struct VirtualFolderDef
	{
		std::string Name;
		std::string Parent = "";  // "" = raiz
		uint32_t    Color = 0xFFFFFFFF; // RGBA
		bool        Expanded = true;
	};


} // namespace axe