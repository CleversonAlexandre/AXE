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
		Material
	};

	// Converte extensão para tipo
	static AssetType AssetTypeFromExtension(const std::string& ext)
	{
		if (ext == ".obj" || ext == ".gltf" || ext == ".glb")return AssetType::Mesh;
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")return AssetType::Texture;
		if (ext == ".axescene")                              return AssetType::Scene;
		if (ext == ".wav" || ext == ".mp3")                  return AssetType::Audio;
		if (ext == ".lua")                                   return AssetType::Script;
		if (ext == ".axemat")                                return AssetType::Material;
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
		return AssetType::Unknown;
	}

	// Um asset registrado no database
	struct AssetRecord
	{
		std::string           UUID;
		std::filesystem::path FilePath;   // caminho absoluto
		AssetType             Type = AssetType::Unknown;
		std::string           Name;       // nome sem extensão

		bool IsValid() const { return !UUID.empty() && !FilePath.empty(); }
	};
	

} // namespace axe