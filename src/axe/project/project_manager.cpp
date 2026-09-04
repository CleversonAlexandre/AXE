#include "project_manager.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>

#include "axe/asset/asset_database.hpp"
#include "axe/script/script_paths.hpp"
#include "axe/mesh/primitive_uuid.hpp"

namespace axe
{


	using json = nlohmann::json;

	ProjectManager& ProjectManager::Get()
	{
		static ProjectManager instance;
		return instance;
	}

	// PROJECT_NEW_V2 — ver a nota no header.
	ProjectManager::NewProjectCheck ProjectManager::CheckNewProject(
		const std::string& name,
		const std::filesystem::path& folder,
		std::filesystem::path& outRoot,
		std::filesystem::path& outProjectFile)
	{
		if (folder.empty()) return NewProjectCheck::NoPath;

		// A barra entra na lista de proibidos junto com os caracteres que o
		// Windows recusa: um nome com barra criaria uma SUBPASTA em vez do
		// projeto, e o erro so apareceria depois, no lugar errado.
		if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos)
			return NewProjectCheck::InvalidName;

		outRoot = folder / name;
		outProjectFile = outRoot / (name + ".axeproject");

		std::error_code ec;

		if (!std::filesystem::exists(outRoot, ec))
			return NewProjectCheck::Ok;

		// Pasta ocupada por um PROJETO e pasta ocupada por qualquer coisa sao
		// situacoes diferentes: numa o trabalho existe e o que se quer e
		// abri-lo; na outra e so um nome tomado.
		if (std::filesystem::exists(outProjectFile, ec))
			return NewProjectCheck::ExistingProject;

		return NewProjectCheck::OccupiedFolder;
	}

	std::string ProjectManager::SuggestFreeName(const std::string& base,
		const std::filesystem::path& folder)
	{
		std::error_code ec;

		// Para em 999: se chegou ali, ou ha algo errado com o caminho, ou o
		// problema nao e mais o nome do projeto.
		for (int i = 2; i < 1000; ++i)
		{
			const std::string candidate = base + "_" + std::to_string(i);
			if (!std::filesystem::exists(folder / candidate, ec))
				return candidate;
		}

		return base;
	}

	bool ProjectManager::NewProject(const std::string& name, const std::filesystem::path& path)
	{
		// Cria a pasta raiz do projeto
		std::filesystem::path root = path / name;

		if (std::filesystem::exists(root))
		{
			AXE_CORE_WARN("ProjectManager: pasta '{}' já existe.", root.string());
			return false;
		}

		// Cria estrutura de pastas
		CreateProjectStructure(root);




		// Popula o projeto
		auto project = std::make_unique<Project>();
		project->Name = name;
		project->RootPath = root;
		project->AssetsPath = root / "Assets";
		project->ProjectPath = root / (name + ".axeproject");

		// Novo projeto — faz scan inicial (pasta vazia, mas já pronto)
		AssetDatabase::Get().Clear();
		AssetDatabase::Get().Scan(project->AssetsPath);
		AssetDatabase::Get().Save(project->RootPath);

		// Escreve o arquivo .axeproject
		if (!WriteProjectFile(*project))
		{
			//AXE_CORE_ERROR("ProjectManager: falha ao escrever arquivo do projeto.");
			return false;
		}

		m_CurrentProject = std::move(project);

		// Salva como último projeto
		m_LastProjectPath = m_CurrentProject->ProjectPath.string();
		m_RecentProjects.insert(m_RecentProjects.begin(), m_LastProjectPath);
		SavePreferences();

		//AXE_CORE_INFO("ProjectManager: projeto '{}' criado em '{}'", name, root.string());
		return true;
	}

	bool ProjectManager::OpenProject(const std::filesystem::path& projectFile,
		bool recordAsRecent)
	{
		if (!std::filesystem::exists(projectFile))
		{
			AXE_CORE_ERROR("ProjectManager: arquivo '{}' não encontrado.", projectFile.string());
			return false;
		}

		auto project = std::make_unique<Project>();
		if (!ReadProjectFile(projectFile, *project))
		{
			AXE_CORE_ERROR("ProjectManager: falha ao ler arquivo do projeto.");
			return false;
		}

		project->ProjectPath = projectFile;
		project->RootPath = projectFile.parent_path();
		project->AssetsPath = project->RootPath / "Assets";

		m_CurrentProject = std::move(project);

		// Atualiza preferências — SO quando quem abriu quer ser lembrado.
		// Ver a nota no header: o jogo empacotado passa false, senao ele
		// sequestra o "ultimo projeto" do editor apontando para a pasta de
		// build.
		if (recordAsRecent)
		{
			m_LastProjectPath = projectFile.string();
			auto it = std::find(m_RecentProjects.begin(), m_RecentProjects.end(), m_LastProjectPath);
			if (it != m_RecentProjects.end())
				m_RecentProjects.erase(it);
			m_RecentProjects.insert(m_RecentProjects.begin(), m_LastProjectPath);

			// Mantém só os 10 mais recentes
			if (m_RecentProjects.size() > 10)
				m_RecentProjects.resize(10);

			SavePreferences();
		}

		AssetDatabase::Get().Clear();
		AssetDatabase::Get().Load(m_CurrentProject->RootPath);

		// SC4 — limpeza de artefatos de script orfaos. Feita AQUI, e nao em
		// qualquer outro momento, porque este e o unico ponto em que o
		// AssetDatabase acabou de ser carregado: antes disso a pergunta "este
		// UUID ainda existe?" nao tem resposta confiavel e apagar seria chute.
		ScriptPaths::SweepOrphans();

		//AXE_CORE_INFO("ProjectManager: projeto '{}' aberto.", m_CurrentProject->Name);
		return true;
	}

	bool ProjectManager::HasLastProject() const
	{
		return !m_LastProjectPath.empty() &&
			std::filesystem::exists(m_LastProjectPath);
	}

	std::filesystem::path ProjectManager::GetLastProjectPath() const
	{
		return m_LastProjectPath;
	}

	void ProjectManager::CreateProjectStructure(const std::filesystem::path& root)
	{
		// Cria pastas padrão
		std::filesystem::create_directories(root / "Assets" / "Scenes");
		std::filesystem::create_directories(root / "Assets" / "Meshes");
		std::filesystem::create_directories(root / "Assets" / "Textures");
		std::filesystem::create_directories(root / "Assets" / "Materials");
		std::filesystem::create_directories(root / "Assets" / "Scripts");
		std::filesystem::create_directories(root / "Assets" / "Audio");

		// SC4 — destino dos artefatos gerados pelo compilador de script. As duas
		// sao 100% regeneraveis a partir de Assets/: entram no .gitignore do
		// projeto sem perda nenhuma, e apagar as duas so custa um Compilar.
		std::filesystem::create_directories(root / "Intermediate" / "Scripts");
		std::filesystem::create_directories(root / "Binaries" / "Scripts");

		AXE_CORE_INFO("ProjectManager: estrutura criada em '{}'", root.string());
	}

	bool ProjectManager::WriteProjectFile(const Project& project)
	{
		json j;
		j["name"] = project.Name;
		j["version"] = project.Version;
		j["engine_version"] = project.EngineVersion;
		j["start_scene"] = project.StartScene;
		j["assets_path"] = "Assets";
		j["active_gamemode_uuid"] = project.ActiveGameModeUUID;

		std::ofstream file(project.ProjectPath);
		if (!file.is_open())
			return false;

		file << j.dump(4); // 4 espaços de indentação
		return true;
	}

	bool ProjectManager::ReadProjectFile(const std::filesystem::path& path, Project& out)
	{
		std::ifstream file(path);
		if (!file.is_open())
			return false;

		try
		{
			json j = json::parse(file);
			out.Name = j.value("name", "Untitled");
			out.Version = j.value("version", "1.0.0");
			out.EngineVersion = j.value("engine_version", "0.1.0");
			out.StartScene = j.value("start_scene", "");
			out.ActiveGameModeUUID = j.value("active_gamemode_uuid", "");
		}
		catch (const json::exception& e)
		{
			AXE_CORE_ERROR("ProjectManager: erro ao parsear JSON: {}", e.what());
			return false;
		}

		return true;
	}

	std::filesystem::path ProjectManager::GetPreferencesPath() const
	{
		// %APPDATA%/AXEEngine/editor_prefs.json
		const char* appdata = std::getenv("APPDATA");
		if (!appdata) appdata = ".";

		std::filesystem::path prefsDir = std::filesystem::path(appdata) / "AXEEngine";
		std::filesystem::create_directories(prefsDir);
		return prefsDir / "editor_prefs.json";
	}

	void ProjectManager::SavePreferences()
	{
		json j;
		j["last_project"] = m_LastProjectPath;
		j["recent_projects"] = m_RecentProjects;

		std::ofstream file(GetPreferencesPath());
		if (file.is_open())
			file << j.dump(4);
	}

	void ProjectManager::LoadPreferences()
	{
		std::filesystem::path prefsPath = GetPreferencesPath();
		if (!std::filesystem::exists(prefsPath))
			return;

		std::ifstream file(prefsPath);
		if (!file.is_open())
			return;

		try
		{
			json j = json::parse(file);
			m_LastProjectPath = j.value("last_project", "");
			m_RecentProjects = j.value("recent_projects", std::vector<std::string>{});
		}
		catch (const json::exception& e)
		{
			AXE_CORE_WARN("ProjectManager: erro ao ler preferências: {}", e.what());
		}
	}

	void ProjectManager::SetStartScene(const std::filesystem::path& sceneAbsolutePath)
	{
		if (!m_CurrentProject || sceneAbsolutePath.empty())
			return;

		std::error_code ec;
		std::filesystem::path rel =
			std::filesystem::relative(sceneAbsolutePath, m_CurrentProject->RootPath, ec);

		// Cena fora da arvore do projeto (outro drive, pasta avulsa): guardar
		// o relativo daria "../../..". Nesse caso o absoluto e o mal menor —
		// funciona aqui, e o HasStartScene() valida a existencia no boot.
		const std::string value = (ec || rel.empty())
			? sceneAbsolutePath.generic_string()
			: rel.generic_string();

		if (m_CurrentProject->StartScene == value)
			return;   // nada mudou, nao reescreve o .axeproj

		m_CurrentProject->StartScene = value;
		SaveProject();

		AXE_CORE_INFO("ProjectManager: cena de abertura definida como '{}'.", value);
	}

	bool ProjectManager::HasStartScene() const
	{
		if (!m_CurrentProject) return false;
		if (m_CurrentProject->StartScene.empty()) return false;

		auto path = m_CurrentProject->RootPath / m_CurrentProject->StartScene;
		return std::filesystem::exists(path);
	}
	void ProjectManager::SaveProject()
	{
		if (!m_CurrentProject) return;
		WriteProjectFile(*m_CurrentProject);
	}


} // namespace axe