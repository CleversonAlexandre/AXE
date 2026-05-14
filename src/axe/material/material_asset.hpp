#pragma once
#include "axe/core/types.hpp"
#include "material.hpp"
#include <filesystem>
#include <string>
#include <memory>

namespace axe
{
	class AXE_API MaterialAsset
	{
	public:
		MaterialAsset() = default;

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		//Material runtime - criado a partir dos dados do asset
		std::shared_ptr<Material> GetMaterial() const { return m_Material; }
		void SetMaterial(std::shared_ptr<Material> mat) { m_Material = mat; }

		//Serialização
		bool Save(const std::filesystem::path& filepath) const;
		bool Load(const std::filesystem::path& filepath);

		//Cria um Material vazio
		static std::shared_ptr<MaterialAsset> Create(const std::string& name = "NewMaterial");

		//Carrega arquivo
		static std::shared_ptr<MaterialAsset> LoadFromFile(const std::filesystem::path& filepath);

		const std::filesystem::path& GetFilePath() const { return m_FilePath; }

	private:
		std::string m_Name = "NewMaterial";
		std::filesystem::path m_FilePath;
		std::shared_ptr<Material> m_Material;
	};

}//namespace axe

