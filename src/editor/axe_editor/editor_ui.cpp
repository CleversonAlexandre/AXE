#include "editor_ui.hpp"
#include <imgui.h>
#include <imgui_internal.h>
namespace axe
{
	void EditorUI::Draw()
	{
		BeginDockspace();

		m_AssetBowserWindow.Draw();
		m_HierarchyWindow.Draw();
		m_InspectorWindow.Draw();
		m_ViewportWindow.Draw();

		EndDockspace();
	}

	void EditorUI::BeginDockspace()
	{
		// Flags da janela host — ela precisa ser invisível e cobrir tudo
		ImGuiWindowFlags windowFlags =
			ImGuiWindowFlags_MenuBar |  // tem menu bar
			ImGuiWindowFlags_NoDocking |  // ela mesma não pode ser ancorada
			ImGuiWindowFlags_NoTitleBar |  // sem título
			ImGuiWindowFlags_NoCollapse |  // não pode ser minimizada
			ImGuiWindowFlags_NoResize |  // não pode ser redimensionada
			ImGuiWindowFlags_NoMove |  // não pode ser movida
			ImGuiWindowFlags_NoBringToFrontOnFocus | // não sobe ao clicar
			ImGuiWindowFlags_NoNavFocus;          // não recebe foco de navegação


		//Pega o tamanho e posição da janela d sistema operacional

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		//Sem padding e sem borda - a janela host é invisivel
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));


		//Abre a janela host 
		ImGui::Begin("Dockspace Host", nullptr, windowFlags);

		ImGui::PopStyleVar(3); // restaura os 3 estilos que feitos o push

		//Cria o DockSpace dentro da janela host
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
			ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f),
				ImGuiDockNodeFlags_None);

			//Na primeira execução, define i layout inical
			BuildDefaultLayout(dockspaceId);
		}
		DrawMenuBar();

	}

	void EditorUI::EndDockspace()
	{
		ImGui::End(); // fecha a janela host
	}

	void EditorUI::DrawMenuBar() 
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("New Scene", "Ctrl+N")) {}
				if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {}
				if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Exit", "Alt+F4"))
				{
					// futuramente: EditorApp::Get().Close();
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
				ImGui::MenuItem("Viewport", nullptr, &m_ShowViewport);
				ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
				ImGui::MenuItem("Asset Browser", nullptr, &m_ShowAssetBrowser);
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}
	}

	void EditorUI::BuildDefaultLayout(ImGuiID dockspaceId)
	{
		// Só executa uma vez — na primeira vez que o programa roda
		// O ImGui salva o layout no imgui.ini e restaura nas próximas vezes
		static bool layoutBuilt = false;
		if (layoutBuilt) return;
			layoutBuilt = true;

			//Limpa qualquer layout existente e começa do zero
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);


		// Divide o espaço principal
	   // O ID do nó restante após cada split vira o espaço que sobrou
		ImGuiID dockMain = dockspaceId;
		ImGuiID dockLeft = 0;
		ImGuiID dockRight = 0;
		ImGuiID dockBottom = 0;
		ImGuiID dockCenter = 0;

		// 1. Separa o painel esquerdo (Hierarchy) — 15% da largura
		ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.15f,
			&dockLeft, &dockMain);

		// 2. Separa o painel direito (Inspector) — 20% do que sobrou
		ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f,
			&dockRight, &dockCenter);

		// 3. Separa o painel inferior (Asset Browser) — 25% do centro
		ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f,
			&dockBottom, &dockCenter);

		// 4. Encaixa cada janela no nó correto
		// O nome aqui precisa ser EXATAMENTE igual ao que você passa no ImGui::Begin()
		ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
		ImGui::DockBuilderDockWindow("Viewport", dockCenter);
		ImGui::DockBuilderDockWindow("Inspector", dockRight);
		ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);

		ImGui::DockBuilderFinish(dockspaceId);

	}
		
		
		
	
}