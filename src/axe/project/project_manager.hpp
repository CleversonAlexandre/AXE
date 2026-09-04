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

		// ═══════════════════════════════════════════════════════════════════
		//  PROJECT_NEW_V2 — a validacao de destino, num lugar so
		//
		//  ── POR QUE ELA MORA AQUI ─────────────────────────────────────────
		//
		//  "Posso criar um projeto com este nome nesta pasta?" e uma pergunta
		//  sobre PROJETO, nao sobre interface. Ela e feita hoje pelo launcher
		//  e pelo menu File do editor, e sera feita por qualquer coisa que
		//  venha a criar projeto. Deixar cada um responder do seu jeito e como
		//  a rotina de material acabou espalhada em seis copias, cada uma
		//  errando de um jeito diferente.
		//
		//  O NewProject continua recusando pasta existente — sobrescrever o
		//  trabalho de alguem seria imperdoavel. O que faltava era poder
		//  PERGUNTAR antes, para a interface oferecer a saida certa em vez de
		//  um "falhou, verifique se a pasta ja existe" depois do clique.
		// ═══════════════════════════════════════════════════════════════════
		enum class NewProjectCheck
		{
			Ok,                // pode criar
			NoPath,            // pasta base vazia
			InvalidName,       // nome vazio ou com caractere que o disco recusa
			ExistingProject,   // ja ha um .axeproject ali — oferecer abrir
			OccupiedFolder,    // pasta existe sem projeto — oferecer outro nome
		};

		static NewProjectCheck CheckNewProject(const std::string& name,
			const std::filesystem::path& folder,
			std::filesystem::path& outRoot,
			std::filesystem::path& outProjectFile);

		// Primeiro nome livre a partir de `base`: "Meujogo", "Meujogo_2", ...
		static std::string SuggestFreeName(const std::string& base,
			const std::filesystem::path& folder);

		// Abre um projeto existente a partir do axe.project.
		//
		// `recordAsRecent` decide se este open vira "ultimo projeto" e entra na
		// lista de recentes — preferencias do EDITOR, gravadas em
		// %APPDATA%/AXEEngine/editor_prefs.json.
		//
		// O JOGO passa `false`, e isso NAO e detalhe. O prefs e um arquivo so,
		// por maquina, compartilhado por qualquer executavel que use esta
		// classe. Com o default, rodar o jogo empacotado gravava a copia
		// EMPACOTADA do projeto como "ultimo projeto", e o editor abria nela no
		// boot seguinte — trabalhando na pasta de saida do build, com o Package
		// recusando o destino ("the output folder is inside the project
		// folder"), ate alguem reabrir o projeto de verdade na mao.
		bool OpenProject(const std::filesystem::path& projectFile,
			bool recordAsRecent = true);

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