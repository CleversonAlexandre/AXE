#include "project_selector_layer.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/log/log.hpp"
#include "axe_editor/file_dialog.hpp"   // PROJECT_NEW_V2b
#include "axe_editor/ui/editor_icons.hpp"
#include "axe_editor/ui/editor_widgets.hpp"

#include <imgui.h>
#include <cstring>
#include <filesystem>

// PROJECT_NEW_V2b — o Win32 saiu daqui.
//
// Este arquivo abria dois dialogos do sistema por conta propria. Os dois
// tinham o mesmo defeito (sem janela dona) e o mesmo problema de acento
// (API ANSI). Agora os dois passam pelo FileDialog, que e o unico lugar do
// editor que fala com o shell do Windows.

namespace axe
{
	void ProjectSelectorLayer::OnAttach()
	{
		//Carega preferencias - ultimo projeto, recentes
		ProjectManager::Get().LoadPreferences();

		//preence path padrão com Documentos do usuario
		const char* userProfile = std::getenv("USERPROFILE");
		if (userProfile)
		{
			std::filesystem::path docs = std::filesystem::path(userProfile) / "Documents" / "AXEProjects";
			std::strncpy(m_ProjectPath, docs.string().c_str(), sizeof(m_ProjectPath) - 1);
		}

		//Se tem rrecentes, começa na aba de recentes
		//Se não tem, começa em New Project
		if (ProjectManager::Get().GetRecentProjects().empty())
			m_ActiveTab = Tab::New;
	}

	void ProjectSelectorLayer::OnRender()
	{
		if (m_ShouldClose)
			return;

		//Janela centralizada, sem decoração
		ImGuiIO& io = ImGui::GetIO();
		ImVec2 center(io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5f);
		ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		// 760x520: a lista de recentes agora mostra o caminho embaixo do nome,
		// e a aba de novo projeto ganhou a faixa de estado do destino. Com 450
		// de altura as duas cortavam.
		ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_Always);

		ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoDocking;

		ImGui::Begin("Axe Engine - Selecionar Projeto", nullptr, flags);

		//Abas
		if (ImGui::BeginTabBar("##tabs"))
		{
			if (ImGui::BeginTabItem(ICON_LIST "  Recentes"))
			{
				m_ActiveTab = Tab::Recent;
				DrawRecentProjects();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_PLUS "  Novo projeto"))
			{
				m_ActiveTab = Tab::New;
				DrawNewProject();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_FOLDER_OPEN "  Abrir"))
			{
				m_ActiveTab = Tab::Open;
				DrawOpenProject();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	// PROJECT_NEW_V2 — DELEGA. A regra de "posso criar aqui?" mora no
	// ProjectManager, e o menu File do editor faz a mesma pergunta no mesmo
	// lugar. Duas implementacoes divergiriam no primeiro caso de borda.
	ProjectSelectorLayer::TargetState ProjectSelectorLayer::EvaluateTarget(
		std::filesystem::path& outRoot,
		std::filesystem::path& outProjectFile) const
	{
		const auto check = ProjectManager::CheckNewProject(
			m_ProjectName, std::filesystem::path(m_ProjectPath),
			outRoot, outProjectFile);

		switch (check)
		{
		case ProjectManager::NewProjectCheck::Ok:              return TargetState::Free;
		case ProjectManager::NewProjectCheck::NoPath:          return TargetState::NoPath;
		case ProjectManager::NewProjectCheck::InvalidName:     return TargetState::InvalidName;
		case ProjectManager::NewProjectCheck::ExistingProject: return TargetState::ExistingProject;
		case ProjectManager::NewProjectCheck::OccupiedFolder:  return TargetState::OccupiedFolder;
		}

		return TargetState::InvalidName;
	}

	std::string ProjectSelectorLayer::SuggestFreeName() const
	{
		return ProjectManager::SuggestFreeName(m_ProjectName,
			std::filesystem::path(m_ProjectPath));
	}

	void ProjectSelectorLayer::DrawSectionTitle(const char* icon, const char* label) const
	{
		ui::SectionHeader(icon, label, ui::Accent::Neutral);
	}

	void ProjectSelectorLayer::DrawRecentProjects()
	{
		const auto& recents = ProjectManager::Get().GetRecentProjects();

		if (recents.empty())
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Nenhum projeto recente.");
			ImGui::TextDisabled("Crie um na aba \"Novo projeto\" ou abra um .axeproject.");
			return;
		}

		DrawSectionTitle(ICON_LIST, "Projetos recentes");

		// O CAMINHO fica visivel, e nao so no tooltip. Dois projetos chamados
		// "Teste" em pastas diferentes eram indistinguiveis nesta lista — e a
		// unica forma de saber qual era qual era passar o mouse em cada um.
		for (std::size_t i = 0; i < recents.size(); ++i)
		{
			const std::string& path = recents[i];
			const std::filesystem::path p(path);

			std::error_code ec;
			const bool exists = std::filesystem::exists(p, ec);

			ImGui::PushID((int)i);

			const float rowH = ImGui::GetTextLineHeightWithSpacing() * 2.0f;

			if (!exists)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.0f));

			const bool clicked = ImGui::Selectable("##row", false,
				ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowH));

			// Desenhado POR CIMA do Selectable: duas linhas de texto dentro de
			// um item so, para o clique valer na linha inteira.
			const ImVec2 rowMin = ImGui::GetItemRectMin();
			ImDrawList* dl = ImGui::GetWindowDrawList();

			const std::string name = p.stem().string();

			dl->AddText(ImVec2(rowMin.x + 8.0f, rowMin.y + 2.0f),
				ImGui::GetColorU32(ImGuiCol_Text), name.c_str());

			dl->AddText(ImVec2(rowMin.x + 8.0f,
				rowMin.y + ImGui::GetTextLineHeight() + 2.0f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled),
				exists ? path.c_str() : (path + "   (nao encontrado)").c_str());

			if (!exists)
				ImGui::PopStyleColor();

			if (ImGui::IsItemHovered() && !exists)
				ImGui::SetTooltip("A pasta foi movida, renomeada ou apagada.");

			if (clicked && exists)
			{
				if (ProjectManager::Get().OpenProject(p))
				{
					m_ShouldClose = true;
					m_Callback(p);
					ImGui::PopID();
					return;   // a lista morreu junto com o layer
				}
			}

			ImGui::PopID();
		}

	}

	void ProjectSelectorLayer::DrawNewProject()
	{
		DrawSectionTitle(ICON_FILE, "Nome do projeto");

		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##name", m_ProjectName, sizeof(m_ProjectName));

		ImGui::Spacing();

		// ═══════════════════════════════════════════════════════════════════
		//  PROJECT_NEW_V2d — dizer QUE PASTA e essa
		//
		//  O rotulo era so "Pasta", e o campo trazia
		//  ".../Documents/AXEProjects". Nada dizia se ali se escolhia a pasta
		//  DO projeto ou a pasta ONDE ele nasce — e a diferenca importa: no
		//  primeiro caso o usuario tem de cria-la antes, no segundo nao.
		//
		//  Ele nao tem de criar nada: o nome vira a pasta, e a engine a cria.
		//  Duas palavras a mais no rotulo evitam a duvida inteira.
		// ═══════════════════════════════════════════════════════════════════
		DrawSectionTitle(ICON_FOLDER_OPEN, "Onde criar (a pasta-mae)");

		const float btnW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(-btnW);
		ImGui::InputText("##path", m_ProjectPath, sizeof(m_ProjectPath));
		ImGui::SameLine();

		if (ui::IconButton(ICON_FOLDER_OPEN, "Escolher a pasta onde o projeto sera criado"))
		{
			std::string selected;
			if (TrySelectFolder(selected))
				std::strncpy(m_ProjectPath, selected.c_str(), sizeof(m_ProjectPath) - 1);
		}

		std::filesystem::path root, projectFile;
		const TargetState state = EvaluateTarget(root, projectFile);

		ImGui::Spacing();

		// ── O que vai acontecer, dito ANTES do clique ──────────────────────
		//
		// Cada estado tem sua cor e sua saida. O caso que motivou tudo isto —
		// pasta ja ocupada — deixou de ser um beco: ou se abre o que esta la,
		// ou se aceita um nome livre, sem sair do launcher.
		switch (state)
		{
		case TargetState::Free:
			ImGui::TextDisabled("Sera criado em: %s", root.string().c_str());
			break;

		case TargetState::NoPath:
			ImGui::TextDisabled("Escolha a pasta onde o projeto vai morar.");
			break;

		case TargetState::InvalidName:
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
				"Nome vazio ou com caractere que o disco nao aceita.");
			ImGui::TextDisabled("Fora: \\ / : * ? \" < > |");
			break;

		case TargetState::ExistingProject:
			ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
				"Ja existe um projeto AXE nessa pasta.");
			ImGui::TextDisabled("%s", root.string().c_str());
			ImGui::Spacing();

			if (ui::AccentButton(ICON_FOLDER_OPEN "  Abrir esse projeto",
				ui::Accent::Primary,
				"Abre o projeto que ja esta nessa pasta, sem criar nada",
				ImVec2(220, 0)))
			{
				if (ProjectManager::Get().OpenProject(projectFile))
				{
					m_ShouldClose = true;
					m_Callback(projectFile);
				}
			}

			ImGui::SameLine();

			if (ui::AccentButton(ICON_PLUS "  Usar outro nome", ui::Accent::Neutral,
				"Preenche o campo com o primeiro nome livre", ImVec2(180, 0)))
			{
				const std::string free = SuggestFreeName();
				std::strncpy(m_ProjectName, free.c_str(), sizeof(m_ProjectName) - 1);
			}
			break;

		case TargetState::OccupiedFolder:
			ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
				"Ja existe uma pasta com esse nome (sem projeto dentro).");
			ImGui::TextDisabled("%s", root.string().c_str());
			ImGui::TextDisabled("Criar aqui poderia misturar arquivos, entao nao e feito.");
			ImGui::Spacing();

			if (ui::AccentButton(ICON_PLUS "  Usar outro nome", ui::Accent::Primary,
				"Preenche o campo com o primeiro nome livre", ImVec2(220, 0)))
			{
				const std::string free = SuggestFreeName();
				std::strncpy(m_ProjectName, free.c_str(), sizeof(m_ProjectName) - 1);
			}
			break;
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (!m_ErrorMessage.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			ImGui::TextWrapped("%s", m_ErrorMessage.c_str());
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		ImGui::BeginDisabled(state != TargetState::Free);

		if (ui::AccentButton(ICON_PLUS "  Criar projeto", ui::Accent::Add,
			"Cria a estrutura de pastas e abre o projeto novo", ImVec2(200, 38)))
		{
			m_ErrorMessage.clear();

			if (ProjectManager::Get().NewProject(m_ProjectName,
				std::filesystem::path(m_ProjectPath)))
			{
				m_ShouldClose = true;
				m_Callback(projectFile);
			}
			else
			{
				// Chegar aqui virou raro: o estado ja foi validado acima. Sobra
				// o que a validacao nao ve — permissao negada, disco cheio,
				// caminho longo demais.
				m_ErrorMessage =
					"Nao foi possivel criar o projeto. Verifique se ha permissao "
					"de escrita nessa pasta e se o caminho nao e longo demais.";
			}
		}

		ImGui::EndDisabled();
	}

	void ProjectSelectorLayer::DrawOpenProject()
	{
		DrawSectionTitle(ICON_FOLDER_OPEN, "Abrir um projeto existente");

		static char openPath[512] = "";

		const float btnW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(-btnW);
		ImGui::InputText("##openpath", openPath, sizeof(openPath));
		ImGui::SameLine();

		if (ui::IconButton(ICON_MAGNIFYING_GLASS, "Procurar um arquivo .axeproject"))
		{
			// PROJECT_NEW_V2b — a TERCEIRA porta com o mesmo defeito.
			//
			// Este bloco montava uma OPENFILENAMEA a mao, com hwndOwner nulo e
			// em ANSI: dialogo sem dono (pode nascer atras da janela e parecer
			// travamento) e caminho com acento corrompido. O FileDialog::Open
			// ja resolve os dois desde sempre — este era o unico lugar do
			// editor que nao o usava.
			const auto picked = FileDialog::Open(
				"AXE Project\0*.axeproject\0All Files\0*.*\0",
				"Abrir Projeto", "axeproject");

			if (!picked.empty())
				std::strncpy(openPath, picked.string().c_str(), sizeof(openPath) - 1);
		}

		ImGui::Spacing();

		std::error_code ec;
		const bool pathValid = std::strlen(openPath) > 0
			&& std::filesystem::exists(openPath, ec);

		// Dizer POR QUE o botao esta apagado. Um botao cinza sem explicacao faz
		// o usuario clicar tres vezes antes de desconfiar do campo acima.
		if (std::strlen(openPath) == 0)
			ImGui::TextDisabled("Escolha um arquivo .axeproject para continuar.");
		else if (!pathValid)
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
				"Esse caminho nao existe.");

		ImGui::Spacing();
		ImGui::BeginDisabled(!pathValid);

		if (ui::AccentButton(ICON_FOLDER_OPEN "  Abrir", ui::Accent::Primary,
			nullptr, ImVec2(160, 38)))
		{
			// BUGFIX: faltavam as chaves aqui — só "m_ShouldClose = true;"
			// fazia parte do if; "m_Callback(...)" rodava SEMPRE, mesmo
			// se OpenProject falhasse (chamava o callback com um projeto
			// que nem abriu de verdade).
			if (ProjectManager::Get().OpenProject(openPath))
			{
				m_ShouldClose = true;
				m_Callback(std::filesystem::path(openPath));
			}
		}

		ImGui::EndDisabled();
	}

	// ═══════════════════════════════════════════════════════════════════════
	//  PROJECT_NEW_V2b — DELEGA, e por um motivo concreto
	//
	//  Aqui havia uma copia propria do SHBrowseForFolder, em ANSI, sem janela
	//  dona e com BIF_NEWDIALOGSTYLE. Ela tem o MESMO defeito que fez o
	//  seletor do menu File travar: dialogo modal sem dono nasce ATRAS da
	//  janela da engine, e o programa fica bloqueado esperando algo que o
	//  usuario nao ve — a engine continua desenhando, e por isso parece um
	//  carregamento infinito em vez de um dialogo escondido.
	//
	//  Consertar so um dos dois deixaria o outro travando. E como sao duas
	//  portas para a MESMA acao ("escolher a pasta do projeto"), o usuario
	//  encontraria uma que funciona e outra que congela, sem entender por que.
	//
	//  A versao ANSI ainda tinha um problema proprio: caminho com acento —
	//  quase garantido em "Documentos" — volta corrompido por ela.
	// ═══════════════════════════════════════════════════════════════════════
	bool ProjectSelectorLayer::TrySelectFolder(std::string& outPath)
	{
		const auto picked = FileDialog::PickFolder("Selecione a pasta do projeto");

		if (picked.empty())
			return false;   // cancelou

		outPath = picked.string();
		return true;
	}

}//namespace axe