#include "project_selector_layer.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>
#include <cstring>
#include <filesystem>

#ifdef AXE_PLATFORM_WINDOWS
#include <windows.h>
#include <ShlObj.h>
#endif

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
		ImGui::SetNextWindowSize(ImVec2(700, 450), ImGuiCond_Always);

		ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoDocking;

		ImGui::Begin("Axe Engine - Selecionar Projeto", nullptr, flags);

		//Abas
		if (ImGui::BeginTabBar("##tabs"))
		{
			if (ImGui::BeginTabItem("Projetos Recentes"))
			{
				m_ActiveTab = Tab::Recent;
				DrawRecentProjects();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Novo Projeto"))
			{
				m_ActiveTab = Tab::New;
				DrawNewProject();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Abrir Projeto"))
			{
				m_ActiveTab = Tab::Open;
				DrawOpenProject();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void ProjectSelectorLayer::DrawRecentProjects()
	{
		const auto& recents = ProjectManager::Get().GetRecentProjects();
		if (recents.empty())
		{
			ImGui::TextDisabled("Nenhum projeto recente.");
			return;
		}

		ImGui::Text("Projetos recentes: ");
		ImGui::Separator();

		for (const auto& path : recents)
		{
			std::filesystem::path p(path);
			std::string name = p.stem().string();
			bool exists = std::filesystem::exists(p);

			if (!exists)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

			bool clicked = ImGui::Selectable(name.c_str());

			if (!exists)
			{
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Arquivo não encontrado : %s", path.c_str());

			}
			else
			{
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", path.c_str());

			}

			if (clicked && exists)
			{
				if (ProjectManager::Get().OpenProject(p))
				{
					m_ShouldClose = true;
					m_Callback(p);
				}
			}

		}
	}
		void ProjectSelectorLayer::DrawNewProject()
		{
			ImGui::Text("Nome do Projeto.");
			ImGui::InputText("##name", m_ProjectName, sizeof(m_ProjectName));

			ImGui::Spacing();
			ImGui::Text("Folder:");
			ImGui::InputText("##path", m_ProjectPath, sizeof(m_ProjectPath));
			ImGui::SameLine();

			if(ImGui::Button("..."))
			{ 
				std::string selected;
				if (TrySelectFolder(selected))
					std::strncpy(m_ProjectPath, selected.c_str(), sizeof(m_ProjectPath) - 1);

			}

			//Preview do caminho final
			std::filesystem::path finalPath =
				std::filesystem::path(m_ProjectPath) / m_ProjectName;

			ImGui::TextDisabled("Será criado em: %s", finalPath.string().c_str());

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (!m_ErrorMessage.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
				ImGui::TextWrapped("%s", m_ErrorMessage.c_str());
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}

			bool nameValid = std::strlen(m_ProjectName) > 0;
			bool pathValid = std::strlen(m_ProjectPath) > 0;

			if (!nameValid || !pathValid)
				ImGui::BeginDisabled();

			if (ImGui::Button("Criar PRojeto", ImVec2(150, 35)))
			{
				m_ErrorMessage.clear();
				std::filesystem::path root(m_ProjectPath);

				if (ProjectManager::Get().NewProject(m_ProjectName, root))
				{
					auto projectFile = root / m_ProjectName / (std::string(m_ProjectName) + ".axeproject");
					m_ShouldClose = true;
					m_Callback(projectFile);
				}
				else
				{
					m_ErrorMessage = "Falaha ao criar projet, Verifique se a pasta já existe.";
				}
			}
			if (!nameValid || !pathValid)
				ImGui::EndDisabled();
		}

		void ProjectSelectorLayer::DrawOpenProject()
		{
			ImGui::Text("Selecione um arquivo .axeproject:");
			ImGui::Spacing();

			static char openPath[512] = "";
			ImGui::InputText("##openpath", openPath, sizeof(openPath));
			ImGui::SameLine();

			if (ImGui::Button("Procurar"))
			{
#ifdef AXE_WINDOW_PLATFORM
				OPENFILENAME ofn;
				char szFile[512] = { 0 };
				ZeroMemory(&ofn, sizeof(ofn));
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = nullptr;
				ofn.lpstrFile = szFile;
				ofn.nMaxFile = sizeof(szFile);
				ofn.lpstrFilter = "AXE Project\0*.axeproject\0";
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

				if (GetOpenFileNameA(&ofn))
					std::strncpy(openPath, szFile, sizeof(openPath) - 1);
#endif // AXE_WINDOW_PLATFORM
			}

			ImGui::Spacing();
			bool pathValid = std::strlen(openPath) > 0 && std::filesystem::exists(openPath);

			if (!pathValid)
				ImGui::BeginDisabled();

			if (ImGui::Button("Abrir", ImVec2(150, 35)))
			{
				if (ProjectManager::Get().OpenProject(openPath))
					m_ShouldClose = true;
					m_Callback(std::filesystem::path(openPath));
			}

			if (!pathValid)
				ImGui::EndDisabled();
		}

		bool ProjectSelectorLayer::TrySelectFolder(std::string& outPath)
		{
#ifdef AXE_PLATFORM_WINOWS
			BROWSEINFOA bi = { 0 };
			bi.lpszTitle = "Selecione a pasta do projeto";
			bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
			LPITEMIDLIST pidl = SHBrowseForFolder(&bi);

			if (pidl)
			{
				char path[MAX_PATH];
				if (SHGetPathFromIDListA(pidl, path))
				{
					outPath = path;
					CoTaskMemFree(pidl);
					return true;
				}
				CoTaskMemFree(pidl)
			}

#endif

			return false;

		}
	
}//namespace axe