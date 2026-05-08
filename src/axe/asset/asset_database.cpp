#include "asset_database.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include "axe/mesh/primitive_uuid.hpp"

namespace axe
{

	using json = nlohmann::json;

	AssetDatabase& AssetDatabase::Get()
	{
		static AssetDatabase instance;
		return instance;
	}

	std::string AssetDatabase::GenerateUUID() const
	{
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

		std::stringstream ss;
		ss << std::hex << std::setfill('0');
		ss << std::setw(8) << dis(gen) << "-";
		ss << std::setw(4) << (dis(gen) & 0xFFFF) << "-";
		ss << std::setw(4) << ((dis(gen) & 0x0FFF) | 0x4000) << "-";
		ss << std::setw(4) << ((dis(gen) & 0x3FFF) | 0x8000) << "-";
		ss << std::setw(8) << dis(gen);
		ss << std::setw(4) << (dis(gen) & 0xFFFF);
		return ss.str();
	}

	std::filesystem::path AssetDatabase::GetMetaPath(const std::filesystem::path& assetPath) const
	{
		return std::filesystem::path(assetPath.string() + ".axemeta");
	}

	bool AssetDatabase::ReadMeta(const std::filesystem::path& metaPath, AssetRecord& out) const
	{
		if (!std::filesystem::exists(metaPath))
			return false;

		std::ifstream file(metaPath);
		if (!file.is_open())
			return false;

		try
		{
			json j = json::parse(file);
			out.UUID = j.value("uuid", "");
			out.Type = AssetTypeFromString(j.value("type", "Unknown"));
			out.Name = j.value("name", "");

			// Caminho absoluto salvo no meta
			std::string path = j.value("path", "");
			if (!path.empty())
				out.FilePath = path;
		}
		catch (const json::exception& e)
		{
			AXE_CORE_WARN("AssetDatabase: erro ao ler meta '{}': {}", metaPath.string(), e.what());
			return false;
		}

		return !out.UUID.empty();
	}

	void AssetDatabase::WriteMeta(const AssetRecord& record) const
	{
		auto metaPath = GetMetaPath(record.FilePath);

		json j;
		j["uuid"] = record.UUID;
		j["type"] = AssetTypeToString(record.Type);
		j["name"] = record.Name;
		j["path"] = record.FilePath.string();

		std::ofstream file(metaPath);
		if (file.is_open())
			file << j.dump(4);
	}

	std::string AssetDatabase::Register(const std::filesystem::path& filepath)
	{
		// Verifica se já está registrado pelo caminho
		std::string absPath = std::filesystem::absolute(filepath).string();
		auto it = m_PathIndex.find(absPath);
		if (it != m_PathIndex.end())
			return it->second; // já registrado

		// Verifica se tem .axemeta existente
		AssetRecord record;
		auto metaPath = GetMetaPath(filepath);

		if (ReadMeta(metaPath, record))
		{
			// Meta existe — usa UUID existente
			record.FilePath = std::filesystem::absolute(filepath);
			record.Name = filepath.stem().string();
		}
		else
		{
			// Novo asset — gera UUID e cria meta
			record.UUID = GenerateUUID();
			record.FilePath = std::filesystem::absolute(filepath);
			record.Type = AssetTypeFromExtension(filepath.extension().string());
			record.Name = filepath.stem().string();

			WriteMeta(record);
			AXE_CORE_INFO("AssetDatabase: registrado '{}' → {}", record.Name, record.UUID);
		}

		m_Records[record.UUID] = record;
		m_PathIndex[absPath] = record.UUID;

		return record.UUID;
	}

	void AssetDatabase::Scan(const std::filesystem::path& directory)
	{
		if (!std::filesystem::exists(directory))
			return;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
		{
			if (!entry.is_regular_file())
				continue;

			// Ignora arquivos .axemeta — são metadados, não assets
			if (entry.path().extension() == ".axemeta")
				continue;

			// Ignora tipos desconhecidos
			AssetType type = AssetTypeFromExtension(entry.path().extension().string());
			if (type == AssetType::Unknown)
				continue;

			Register(entry.path());
		}

		AXE_CORE_INFO("AssetDatabase: scan concluído — {} assets registrados.", m_Records.size());
	}

	const AssetRecord* AssetDatabase::GetByUUID(const std::string& uuid) const
	{
		auto it = m_Records.find(uuid);
		if (it != m_Records.end())
			return &it->second;
		return nullptr;
	}

	const AssetRecord* AssetDatabase::GetByPath(const std::filesystem::path& path) const
	{
		std::string absPath = std::filesystem::absolute(path).string();
		auto it = m_PathIndex.find(absPath);
		if (it != m_PathIndex.end())
			return GetByUUID(it->second);
		return nullptr;
	}

	std::vector<const AssetRecord*> AssetDatabase::GetAllOfType(AssetType type) const
	{
		std::vector<const AssetRecord*> result;
		for (const auto& [uuid, record] : m_Records)
			if (record.Type == type)
				result.push_back(&record);
		return result;
	}

	void AssetDatabase::Save(const std::filesystem::path& projectRoot)
	{
		// Salva índice completo em axe_assets.json
		json j = json::array();
		for (const auto& [uuid, record] : m_Records)
		{
			json entry;
			entry["uuid"] = record.UUID;
			entry["path"] = record.FilePath.string();
			entry["type"] = AssetTypeToString(record.Type);
			entry["name"] = record.Name;
			j.push_back(entry);
		}

		std::filesystem::path indexPath = projectRoot / "axe_assets.json";
		std::ofstream file(indexPath);
		if (file.is_open())
			file << j.dump(4);
	}

	void AssetDatabase::Load(const std::filesystem::path& projectRoot)
	{		

		std::filesystem::path indexPath = projectRoot / "axe_assets.json";
		if (!std::filesystem::exists(indexPath))
		{
			// Primeira vez — faz scan
			Scan(projectRoot / "Assets");
			Save(projectRoot);
			return;
		}

		std::ifstream file(indexPath);
		if (!file.is_open())
			return;

		try
		{
			json j = json::parse(file);
			for (const auto& entry : j)
			{
				AssetRecord record;
				record.UUID = entry.value("uuid", "");
				record.FilePath = entry.value("path", "");
				record.Type = AssetTypeFromString(entry.value("type", "Unknown"));
				record.Name = entry.value("name", "");

				if (!record.UUID.empty() && std::filesystem::exists(record.FilePath))
				{
					std::string absPath = record.FilePath.string();
					m_Records[record.UUID] = record;
					m_PathIndex[absPath] = record.UUID;
				}
			}

			AXE_CORE_INFO("AssetDatabase: {} assets carregados.", m_Records.size());
		}
		catch (const json::exception& e)
		{
			AXE_CORE_WARN("AssetDatabase: erro ao carregar índice: {}", e.what());
			// Fallback — faz scan
			Scan(projectRoot / "Assets");
		}
	}

	void AssetDatabase::RegisterPrimitives()
	{
		auto reg = [this](const char* uuid, const char* name)
			{
				AssetRecord r;
				r.UUID = uuid;
				r.Name = name;
				r.Type = AssetType::Mesh;
				m_Records[r.UUID] = r;
				AXE_CORE_INFO("AssetDatabase: primitiva registrada '{}' → {}", name, uuid);
			};

		reg(PrimitiveUUID::Cube, "Cube");
		reg(PrimitiveUUID::Sphere, "Sphere");
		reg(PrimitiveUUID::Plane, "Plane");
		reg(PrimitiveUUID::Cylinder, "Cylinder");
	}

	void AssetDatabase::Clear()
	{
		m_Records.clear();
		m_PathIndex.clear();
		RegisterPrimitives(); 
	}


} // namespace axe