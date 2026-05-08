#pragma once
#include "axe/layers/layer.hpp"
#include <string>
#include <filesystem>
#include <functional>

namespace axe
{
class ProjectSelectorLayer : public Layer
{
public:
	//Callback chamado quando o projeto é selecionado
	using ProjectSelectedCallback = std::function<void(const std::filesystem::path&)>;

	ProjectSelectorLayer(ProjectSelectedCallback callback):
		Layer("ProjectSelectirLayer"), m_Callback(callback) {}

	void OnAttach() override;
	void OnRender() override;

private:
	void DrawNewProject();
	void DrawOpenProject();
	void DrawRecentProjects();

	bool TrySelectFolder(std::string& outPath);

	ProjectSelectedCallback m_Callback;

	//Estado do formulario de novo projeto
	char m_ProjectName[256] = "Meujogo";
	char m_ProjectPath[512] = "";
	std::string m_ErrorMessage;

	//Aba ativa 
	enum class Tab {Recent, New, Open};
	Tab m_ActiveTab = Tab::Recent;	

	bool m_ShouldClose = false;
};

}//namespace axe
