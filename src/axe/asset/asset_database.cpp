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

			// ── A EXTENSAO TEM A PALAVRA FINAL SOBRE O TIPO ──────────────────
			//
			// O Type gravado no .axemeta pode estar velho: um .axeanim
			// registrado antes de AssetType::AnimGraph existir tem "Unknown" ou
			// "Mesh" aqui. Confiar nesse valor faz o engine tratar o JSON como
			// modelo e manda-lo pro Assimp ("No suitable reader found").
			//
			// A extensao do arquivo no disco nao mente. Quando ela discorda do
			// meta, ela vence — e isso conserta os assets antigos sozinho, sem o
			// usuario ter que reimportar nada.
			{
				// metaPath e "<asset>.axemeta"; tirar o .axemeta devolve o
				// caminho do asset, e dele a extensao real. (out.FilePath so e
				// preenchido mais abaixo, entao nao da pra usa-lo aqui.)
				std::filesystem::path assetPath = metaPath;
				assetPath.replace_extension();   // remove ".axemeta"

				const AssetType byExt = AssetTypeFromExtension(assetPath.extension().string());

				if (byExt != AssetType::Unknown && byExt != out.Type)
					out.Type = byExt;
			}
			out.Name = j.value("name", "");
			out.ScriptClassType = j.value("script_class_type", "");

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
		if (!record.ScriptClassType.empty())
			j["script_class_type"] = record.ScriptClassType;

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
		{
			// Já registrado — mas atualiza ScriptClassType se ainda não foi lido
			auto& existing = m_Records[it->second];
			if (existing.Type == AssetType::Script && existing.ScriptClassType.empty())
			{
				try {
					std::ifstream jf(filepath);
					if (jf.is_open()) {
						nlohmann::json j = nlohmann::json::parse(jf, nullptr, false);
						if (!j.is_discarded() && j.contains("class_type"))
							existing.ScriptClassType = j["class_type"].get<std::string>();
					}
				}
				catch (...) {}
				if (!existing.ScriptClassType.empty())
					WriteMeta(existing);
			}
			return it->second;
		}

		// Verifica se tem .axemeta existente
		AssetRecord record;
		auto metaPath = GetMetaPath(filepath);

		if (ReadMeta(metaPath, record))
		{
			// Defesa extra: se esse UUID já está em uso por um registro vivo
			// apontando para OUTRO arquivo que ainda existe no disco, o .axemeta
			// lido aqui é órfão (sobrou de um rename/move anterior). Reaproveitar
			// esse UUID corromperia o asset original — gera um UUID novo.
			auto existingIt = m_Records.find(record.UUID);
			bool staleMeta = existingIt != m_Records.end()
				&& std::filesystem::absolute(existingIt->second.FilePath) != std::filesystem::absolute(filepath)
				&& std::filesystem::exists(existingIt->second.FilePath);

			if (staleMeta)
			{
				AXE_CORE_WARN("AssetDatabase: .axemeta órfão detectado em '{}' (UUID já usado por '{}'). Gerando novo UUID.",
					filepath.string(), existingIt->second.FilePath.string());

				std::error_code ec;
				std::filesystem::remove(metaPath, ec);

				record = AssetRecord{};
				record.UUID = GenerateUUID();
				record.FilePath = std::filesystem::absolute(filepath);
				record.Type = AssetTypeFromExtension(filepath.extension().string());
				record.Name = filepath.stem().string();
				WriteMeta(record);
			}
			else
			{
				// Meta existe — usa UUID existente
				record.FilePath = std::filesystem::absolute(filepath);
				record.Name = filepath.stem().string();
			}
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

		// Para scripts: lê o class_type do JSON do .axescript
		// e persiste no .axemeta para não precisar reler na próxima inicialização
		if (record.Type == AssetType::Script && record.ScriptClassType.empty())
		{
			try {
				std::ifstream jf(filepath);
				if (jf.is_open()) {
					nlohmann::json j = nlohmann::json::parse(jf, nullptr, false);
					if (!j.is_discarded() && j.contains("class_type"))
						record.ScriptClassType = j["class_type"].get<std::string>();
				}
			}
			catch (...) {}
			// Atualiza o .axemeta para persistir o class_type
			if (!record.ScriptClassType.empty())
				WriteMeta(record);
		}

		m_Records[record.UUID] = record;
		m_PathIndex[absPath] = record.UUID;

		return record.UUID;
	}

	bool AssetDatabase::UpdatePath(const std::string& uuid, const std::filesystem::path& newPath,
		const std::string& newName)
	{
		auto it = m_Records.find(uuid);
		if (it == m_Records.end())
			return false;

		AssetRecord& record = it->second;

		// Remove a entrada antiga do índice de caminhos — crítico para não
		// deixar o caminho antigo "fantasma" registrado em memória.
		std::string oldAbsPath = std::filesystem::absolute(record.FilePath).string();
		m_PathIndex.erase(oldAbsPath);

		// Remove o .axemeta antigo (o novo será escrito no novo caminho)
		auto oldMeta = GetMetaPath(record.FilePath);
		std::error_code ec;
		if (std::filesystem::exists(oldMeta))
			std::filesystem::remove(oldMeta, ec);

		record.FilePath = std::filesystem::absolute(newPath);
		if (!newName.empty())
			record.Name = newName;

		std::string newAbsPath = record.FilePath.string();
		m_PathIndex[newAbsPath] = uuid;

		WriteMeta(record);
		return true;
	}

	bool AssetDatabase::Unregister(const std::string& uuid)
	{
		auto it = m_Records.find(uuid);
		if (it == m_Records.end())
			return false;

		std::string absPath = std::filesystem::absolute(it->second.FilePath).string();
		m_PathIndex.erase(absPath);
		m_Records.erase(it);
		return true;
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
		json j = json::array();
		for (const auto& [uuid, record] : m_Records)
		{
			json entry;
			entry["uuid"] = record.UUID;
			// ── PKG1: caminho RELATIVO a raiz do projeto ─────────────────
			//
			// Era absoluto (`C:\\Users\\Clever\\...`), com o mesmo defeito que o
			// ScriptComponent tinha: mover a pasta do projeto, abrir em outra
			// maquina ou EMPACOTAR invalidava o indice inteiro.
			//
			// Relativo tambem e o que torna o manifesto do jogo possivel: o
			// pacote e uma pasta com outra raiz, e os caminhos precisam
			// funcionar la sem reescrita.
			//
			// Fora da raiz (asset referenciado de outro lugar do disco) fica
			// absoluto — nao ha relativo que faca sentido, e forcar um
			// produziria um caminho que sobe pastas ate a raiz do volume.
			{
				std::error_code ec;
				const auto rel = std::filesystem::relative(record.FilePath, projectRoot, ec);

				// `..` no primeiro componente = o asset esta FORA da raiz.
				// Comparar o componente inteiro, e nao o prefixo da string:
				// uma pasta chamada "..coisa" nao deve ser confundida com
				// subir de nivel.
				const bool escapes = !rel.empty()
					&& rel.begin() != rel.end()
					&& rel.begin()->string() == "..";

				const bool inside = !ec && !rel.empty() && !escapes;

				entry["path"] = inside ? rel.generic_string() : record.FilePath.string();
			}
			entry["type"] = AssetTypeToString(record.Type);
			entry["name"] = record.Name;
			entry["virtual_folder"] = record.VirtualFolder;
			if (!record.ScriptClassType.empty())
				entry["script_class_type"] = record.ScriptClassType;
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
				// PKG1 — relativo e resolvido contra a raiz; absoluto (indice
				// gravado antes desta mudanca) continua valendo. Um indice
				// antigo e migrado no proximo Save, sem passo manual.
				{
					std::filesystem::path p = entry.value("path", "");

					record.FilePath = p.is_absolute()
						? p
						: std::filesystem::weakly_canonical(projectRoot / p);
				}
				record.Type = AssetTypeFromString(entry.value("type", "Unknown"));

				// A EXTENSAO NO DISCO E A VERDADE.
				//
				// O "type" gravado no indice e derivado e envelhece: um asset
				// registrado ANTES de um tipo existir (.axeskel, .axeanim) fica
				// com Unknown/Mesh pra sempre, e some de todo filtro por tipo
				// (foi o AssetPicker do script abrindo vazio). O load do
				// .axemeta ja fazia esta correcao; o indice nao fazia.
				{
					const AssetType byExt =
						AssetTypeFromExtension(record.FilePath.extension().string());

					if (byExt != AssetType::Unknown && byExt != record.Type)
					{
						AXE_CORE_INFO("AssetDatabase [ASSETTYPE_FIX_V1]: '{}' estava como {} no indice — corrigido para {} pela extensao.",
							record.FilePath.filename().string(),
							AssetTypeToString(record.Type), AssetTypeToString(byExt));

						record.Type = byExt;
					}
				}

				record.Name = entry.value("name", "");
				record.VirtualFolder = entry.value("virtual_folder", "");
				record.ScriptClassType = entry.value("script_class_type", "");

				// Migração: se é script mas class_type não está no índice,
				// lê direto do .axescript (só ocorre na primeira run após o update)
				if (record.Type == AssetType::Script && record.ScriptClassType.empty()
					&& std::filesystem::exists(record.FilePath))
				{
					try {
						std::ifstream jf(record.FilePath);
						if (jf.is_open()) {
							nlohmann::json jscript = nlohmann::json::parse(jf, nullptr, false);
							if (!jscript.is_discarded() && jscript.contains("class_type"))
								record.ScriptClassType = jscript["class_type"].get<std::string>();
						}
					}
					catch (...) {}
				}

				if (!record.UUID.empty() && std::filesystem::exists(record.FilePath))
				{
					std::string absPath = record.FilePath.string();
					m_Records[record.UUID] = record;
					m_PathIndex[absPath] = record.UUID;
				}
			}

			//AXE_CORE_INFO("AssetDatabase: {} assets carregados.", m_Records.size());
		}
		catch (const json::exception& e)
		{
			//AXE_CORE_WARN("AssetDatabase: erro ao carregar índice: {}", e.what());
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
				//AXE_CORE_INFO("AssetDatabase: primitiva registrada '{}' → {}", name, uuid);
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



	// ─────────────────────────────────────────────────────────────────────────
	//  Assets fora da raiz do projeto (PKG2)
	// ─────────────────────────────────────────────────────────────────────────

	namespace
	{
		// Arquivos gerados que acompanham um asset. Mesma lista do
		// AssetDependencyGraph, e pela mesma razao: nenhum deles esta no
		// AssetDatabase (a extensao nao e um tipo de asset), entao quem move o
		// asset e o unico que pode leva-los junto.
		//
		// Deixar um `.axemesh` para tras nao daria erro — o runtime cairia no
		// fonte e recozinharia. Deixar o `.axemeta` para tras SIM: o UUID mora
		// nele, e sem ele um Scan futuro gera outro UUID para o mesmo arquivo,
		// e toda referencia existente aponta para o vazio.
		const char* kSatelliteExtensions[] =
		{
			".axemesh", ".axeskelbin", ".axeclipbin", ".axegraph", ".axeshader",
		};

		bool IsInside(const std::filesystem::path& p, const std::filesystem::path& root)
		{
			std::error_code ec;
			const auto rel = std::filesystem::relative(p, root, ec);

			if (ec || rel.empty())
				return false;

			return rel.begin()->string() != "..";
		}
	}

	std::vector<const AssetRecord*> AssetDatabase::ExternalAssets(
		const std::filesystem::path& projectRoot) const
	{
		std::vector<const AssetRecord*> out;

		std::error_code ec;
		const auto root = std::filesystem::weakly_canonical(projectRoot, ec);

		for (const auto& [uuid, rec] : m_Records)
		{
			if (rec.FilePath.empty())
				continue;

			// Primitivas do engine (Cube, Sphere...) nao tem arquivo no disco.
			if (!std::filesystem::exists(rec.FilePath, ec))
				continue;

			if (!IsInside(rec.FilePath, root))
				out.push_back(&rec);
		}

		return out;
	}

	AssetDatabase::ImportExternalResult AssetDatabase::ImportExternalAssets(
		const std::filesystem::path& projectRoot, const std::string& subfolder)
	{
		ImportExternalResult res;

		std::error_code ec;

		const auto root = std::filesystem::weakly_canonical(projectRoot, ec);
		const auto destDir = root / "Assets" / subfolder;

		std::filesystem::create_directories(destDir, ec);

		if (ec)
		{
			res.Failures.push_back("could not create " + destDir.string()
				+ ": " + ec.message());
			return res;
		}

		// Copia a lista ANTES de mexer: ExternalAssets devolve ponteiros para
		// dentro de m_Records, e UpdatePath altera esse mapa. Iterar sobre os
		// ponteiros enquanto o mapa muda seria ler memoria realocada.
		std::vector<std::pair<std::string, std::filesystem::path>> toMove;

		for (const AssetRecord* rec : ExternalAssets(root))
			toMove.emplace_back(rec->UUID, rec->FilePath);

		for (const auto& [uuid, src] : toMove)
		{
			std::filesystem::path dst = destDir / src.filename();

			// Nome ja ocupado por OUTRO asset: desambigua em vez de
			// sobrescrever. Dois arquivos de pastas diferentes podem ter o
			// mesmo nome, e perder um deles em silencio seria pior que um nome
			// feio.
			if (std::filesystem::exists(dst, ec))
			{
				const AssetRecord* occupant = GetByPath(dst);

				if (!occupant || occupant->UUID != uuid)
				{
					const std::string stem = src.stem().string();
					const std::string ext = src.extension().string();

					for (int i = 1; i < 1000; ++i)
					{
						dst = destDir / (stem + "_" + std::to_string(i) + ext);

						if (!std::filesystem::exists(dst, ec))
							break;
					}
				}
			}

			std::filesystem::copy_file(src, dst,
				std::filesystem::copy_options::overwrite_existing, ec);

			if (ec)
			{
				res.Failures.push_back(src.filename().string() + ": " + ec.message());
				ec.clear();
				continue;
			}

			// ── Satelites ────────────────────────────────────────────────────
			//
			// O `.axemeta` ANEXA a extensao ("a.fbx.axemeta"); os cozidos
			// SUBSTITUEM ("a.axemesh"). A diferenca importa: tratar os dois
			// igual deixaria o .axemeta para tras, e com ele o UUID.
			{
				const std::filesystem::path metaSrc(src.string() + ".axemeta");

				if (std::filesystem::exists(metaSrc, ec))
				{
					std::filesystem::copy_file(metaSrc,
						std::filesystem::path(dst.string() + ".axemeta"),
						std::filesystem::copy_options::overwrite_existing, ec);
					ec.clear();
				}

				for (const char* ext : kSatelliteExtensions)
				{
					std::filesystem::path satSrc = src;
					satSrc.replace_extension(ext);

					if (!std::filesystem::exists(satSrc, ec))
						continue;

					std::filesystem::path satDst = dst;
					satDst.replace_extension(ext);

					std::filesystem::copy_file(satSrc, satDst,
						std::filesystem::copy_options::overwrite_existing, ec);
					ec.clear();
				}
			}

			// UpdatePath preserva o UUID e reescreve o .axemeta no destino, e e
			// isso que faz toda referencia existente continuar valendo.
			if (!UpdatePath(uuid, dst))
			{
				res.Failures.push_back(src.filename().string()
					+ ": copied, but the index could not be updated");
				continue;
			}

			++res.Imported;

			AXE_CORE_INFO("AssetDatabase: imported '{}' into the project.",
				dst.filename().string());
		}

		if (res.Imported)
			Save(root);

		return res;
	}

} // namespace axe