#include "skeletal_mesh_asset.hpp"
#include "skeletal_mesh_loader.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{
	using json = nlohmann::json;

	namespace fs = std::filesystem;

	std::shared_ptr<SkeletalMeshAsset> SkeletalMeshAsset::Create(const std::string& name,
		const fs::path& sourceFile)
	{
		auto asset = std::make_shared<SkeletalMeshAsset>();
		asset->m_Name = name;
		asset->m_SourceFile = sourceFile;
		return asset;
	}

	fs::path SkeletalMeshAsset::ResolvePath(const fs::path& p) const
	{
		if (p.empty() || p.is_absolute())
			return p;

		// Relativo -> a partir da pasta do .axeskel.
		if (!m_FilePath.empty())
			return fs::weakly_canonical(m_FilePath.parent_path() / p);

		return p;
	}

	fs::path SkeletalMeshAsset::RelativizePath(const fs::path& p) const
	{
		if (p.empty() || m_FilePath.empty())
			return p;

		std::error_code ec;
		fs::path rel = fs::relative(p, m_FilePath.parent_path(), ec);

		// Se relative() falhou (drives diferentes, por exemplo), guarda o
		// absoluto — feio, mas funcional, e melhor do que gravar um caminho
		// quebrado.
		if (ec || rel.empty())
			return p;

		return rel;
	}

	bool SkeletalMeshAsset::Resolve()
	{
		if (m_Resolved)
			return true;

		const fs::path source = ResolvePath(m_SourceFile);

		if (source.empty() || !fs::exists(source))
		{
			AXE_CORE_ERROR("SkeletalMeshAsset '{}': arquivo de origem '{}' nao encontrado.",
				m_Name, source.string());
			return false;
		}

		SkeletalAsset imported = SkeletalMeshLoader::Load(source.string());

		if (!imported.IsValid())
		{
			AXE_CORE_ERROR("SkeletalMeshAsset '{}': falha ao importar '{}'.",
				m_Name, source.string());
			return false;
		}

		m_Mesh = imported.MeshData;
		m_Skeleton = imported.SkeletonData;

		// Clipes embutidos no próprio arquivo do personagem (comum em glTF).
		m_Clips = imported.Clips;

		// Clipes de arquivos separados (o fluxo Mixamo).
		for (const auto& entry : m_Animations)
		{
			const fs::path animPath = ResolvePath(entry.SourceFile);

			if (!fs::exists(animPath))
			{
				AXE_CORE_WARN("SkeletalMeshAsset '{}': animacao '{}' nao encontrada em '{}' — ignorada.",
					m_Name, entry.Name, animPath.string());
				continue;
			}

			// Religamento por NOME de osso. Os índices de bone de um arquivo
			// de animação NUNCA batem com os do personagem — é por isso que
			// isto funciona com os FBX separados da Mixamo.
			auto clips = SkeletalMeshLoader::LoadClips(animPath.string(), *m_Skeleton);

			for (auto& c : clips)
			{
				if (!c)
					continue;

				// O nome do JSON manda: o usuário pode ter renomeado o clipe,
				// e "mixamo.com" (nome que a Mixamo põe em TODO clipe) não
				// ajuda ninguém a distinguir idle de run.
				if (!entry.Name.empty())
					c->SetName(entry.Name);

				m_Clips.push_back(c);
			}
		}

		m_Resolved = true;

		AXE_CORE_INFO("SkeletalMeshAsset '{}': {} bones, {} clipe(s).",
			m_Name, m_Skeleton->GetBoneCount(), m_Clips.size());

		return true;
	}

	int SkeletalMeshAsset::AddAnimation(const fs::path& file)
	{
		if (!Resolve())
			return 0;

		if (!fs::exists(file))
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: animacao '{}' nao encontrada.", file.string());
			return 0;
		}

		auto clips = SkeletalMeshLoader::LoadClips(file.string(), *m_Skeleton);

		if (clips.empty())
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: '{}' nao trouxe nenhum clipe compativel com o esqueleto '{}'. "
				"Os nomes dos ossos batem? (Mixamo usa 'mixamorig:*' em todos os arquivos)",
				file.string(), m_Skeleton->GetName());
			return 0;
		}

		int added = 0;

		for (auto& c : clips)
		{
			if (!c)
				continue;

			AnimEntry entry;

			// Nome do ARQUIVO, não o nome interno do clipe. A Mixamo chama
			// todo clipe de "mixamo.com" — se usássemos isso, o dropdown
			// ficaria com cinco entradas idênticas.
			entry.Name = file.stem().string();
			entry.SourceFile = RelativizePath(file);

			// Colisão de nome (dois arquivos com o mesmo stem, ou um FBX com
			// vários clipes dentro): desambigua com sufixo.
			if (added > 0)
				entry.Name += "_" + std::to_string(added);

			c->SetName(entry.Name);

			m_Animations.push_back(entry);
			m_Clips.push_back(c);

			++added;
		}

		return added;
	}

	void SkeletalMeshAsset::RemoveAnimation(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_Animations.size()))
			return;

		m_Animations.erase(m_Animations.begin() + index);

		// Força reimportação: os índices de m_Clips não batem mais com os de
		// m_Animations (clipes embutidos no personagem vêm antes). Recalcular
		// do zero é barato — o SkeletalMeshLoader cacheia por caminho — e
		// evita um bug de índice deslocado bem difícil de rastrear.
		m_Resolved = false;
		m_Clips.clear();
		Resolve();
	}

	bool SkeletalMeshAsset::Save(const fs::path& filepath)
	{
		// m_FilePath tem que estar setado ANTES de relativizar — os caminhos
		// são relativos à pasta do .axeskel.
		m_FilePath = filepath;

		json root;
		root["type"] = "SkeletalMesh";
		root["version"] = "1.0";
		root["name"] = m_Name;
		root["source"] = RelativizePath(ResolvePath(m_SourceFile)).generic_string();

		json anims = json::array();

		for (const auto& a : m_Animations)
		{
			json j;
			j["name"] = a.Name;
			j["source"] = RelativizePath(ResolvePath(a.SourceFile)).generic_string();
			anims.push_back(j);
		}

		root["animations"] = anims;

		fs::create_directories(filepath.parent_path());

		std::ofstream file(filepath);
		if (!file.is_open())
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: falha ao salvar '{}'", filepath.string());
			return false;
		}

		file << root.dump(4);

		AXE_CORE_INFO("SkeletalMeshAsset: salvo em '{}'", filepath.string());
		return true;
	}

	bool SkeletalMeshAsset::Save()
	{
		if (m_FilePath.empty())
		{
			AXE_CORE_ERROR("SkeletalMeshAsset '{}': sem caminho — use Save(path).", m_Name);
			return false;
		}

		return Save(m_FilePath);
	}

	std::shared_ptr<SkeletalMeshAsset> SkeletalMeshAsset::LoadFromFile(const fs::path& filepath)
	{
		if (!fs::exists(filepath))
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: '{}' nao encontrado.", filepath.string());
			return nullptr;
		}

		std::ifstream file(filepath);

		json root;
		try
		{
			root = json::parse(file);
		}
		catch (const std::exception& e)
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: '{}' invalido: {}", filepath.string(), e.what());
			return nullptr;
		}

		auto asset = std::make_shared<SkeletalMeshAsset>();

		asset->m_FilePath = filepath;
		asset->m_Name = root.value("name", filepath.stem().string());
		asset->m_SourceFile = fs::path(root.value("source", std::string{}));

		if (root.contains("animations") && root["animations"].is_array())
		{
			for (const auto& j : root["animations"])
			{
				AnimEntry entry;
				entry.Name = j.value("name", std::string{});
				entry.SourceFile = fs::path(j.value("source", std::string{}));

				if (!entry.SourceFile.empty())
					asset->m_Animations.push_back(entry);
			}
		}

		// NÃO resolve aqui: importar um FBX é caro, e o asset browser lista
		// dezenas de assets sem precisar da geometria de nenhum. Quem
		// realmente for usar chama Resolve().
		return asset;
	}

} // namespace axe
