#pragma once
#include "axe/core/types.hpp"
#include "project.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <memory>

namespace axe
{

	class AXE_API ProjectManager
	{
	public:
		// Singleton — só existe um projeto ativo por vez
		static ProjectManager& Get();

		// Cria um novo projeto na pasta indicada
		bool NewProject(const std::string& name, const std::filesystem::path& path);

		// Abre um projeto existente a partir do axe.project
		bool OpenProject(const std::filesystem::path& projectFile);

		// Projeto atual
		bool        HasProject() const { return m_CurrentProject != nullptr; }
		const Project& GetCurrent() const { return *m_CurrentProject; }
		Project& GetCurrent() { return *m_CurrentProject; }

		// Preferências do editor
		bool                            HasLastProject() const;
		std::filesystem::path           GetLastProjectPath() const;
		const std::vector<std::string>& GetRecentProjects() const { return m_RecentProjects; }

		// Salva preferências (último projeto aberto)
		void SavePreferences();
		void LoadPreferences();

		bool HasStartScene() const;
		void SaveProject();

		// Marca uma cena como a cena de abertura do projeto.
		//
		// Guarda o caminho RELATIVO a RootPath — gravar absoluto
		// ("C:/Users/...") funcionaria so nesta maquina e quebraria o projeto
		// em qualquer outra. Salva o .axeproj na hora.
		//
		// Chamado sempre que uma cena e aberta ou salva: a ultima cena com
		// que voce trabalhou passa a ser a que abre sozinha no proximo boot.
		void SetStartScene(const std::filesystem::path& sceneAbsolutePath);


		std::filesystem::path GetStartScenePath() const
		{
			return m_CurrentProject->RootPath / m_CurrentProject->StartScene;
		}

	private:
		ProjectManager() = default;

		void CreateProjectStructure(const std::filesystem::path& root);
		bool WriteProjectFile(const Project& project);
		bool ReadProjectFile(const std::filesystem::path& path, Project& out);

		std::filesystem::path GetPreferencesPath() const;

		std::unique_ptr<Project>     m_CurrentProject;
		std::vector<std::string>     m_RecentProjects;
		std::string                  m_LastProjectPath;
	};

} // namespace axe