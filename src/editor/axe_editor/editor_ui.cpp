#include "axe_editor/asset/asset_viewer_window.hpp"   // ASSET_VIEWER_V1
#include "editor_ui.hpp"
#include "axe/audio/audio_engine.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/project/project.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/scene/game_mode_asset.hpp"
#include "file_dialog.hpp"
#include "editor_app.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include "axe/log/log.hpp"
namespace axe
{
	void EditorUI::Draw()
	{
		BeginDockspace();

		m_AssetBowserWindow.Draw();
		m_HierarchyWindow.Draw();
		m_InspectorWindow.Draw();
		m_ViewportWindow.Draw();
		m_MaterialEditorWindow.Draw();
		m_ParticleEditorWindow.Draw();
		m_SoundCueEditorWindow.Draw();
		m_AssetViewerWindow.Draw();   // ASSET_VIEWER_V1
		DrawAudioMixer();

		// Input Settings — carrega o InputConfig.json do projeto atual na
		// primeira vez que detecta o projeto (ou troca de projeto). Evita
		// recarregar do disco todo frame.
		if (ProjectManager::Get().HasProject())
		{
			// m_LoadedInputProjectRoot e MEMBRO — ver a nota no header. Era um
			// static local, e por isso reabrir o MESMO projeto (File > Open
			// Project, que recria o EditorLayer inteiro) deixava a janela nova
			// sem caminho: "Nenhum projeto carregado".
			auto curRoot = ProjectManager::Get().GetCurrent().RootPath;
			if (curRoot != m_LoadedInputProjectRoot)
			{
				m_LoadedInputProjectRoot = curRoot;
				m_InputSettingsWindow.SetProjectPath(curRoot);
			}
		}
		m_InputSettingsWindow.Draw();
		m_AssetReportWindow.Draw();

		// Painel de Environment — flutuante, abrível pelo menu View
		if (m_ShowEnvironment && OnDrawEnvironment)
		{
			ImGui::SetNextWindowSize(ImVec2(320, 180), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Environment", &m_ShowEnvironment))
				OnDrawEnvironment();
			ImGui::End();
		}



		//if (m_SequencerWindow.m_IsOpen && OnDrawSequencer)
		//{
		//	if (ImGui::Begin("Sequencer", &m_SequencerWindow.m_IsOpen))
		//	{
		//		OnDrawSequencer();
		//		m_SequencerWindow.Open();
		//	}
		//	ImGui::End();
		//}
		//else
		//{
		//	m_SequencerWindow.Close();
		//}
		//

		// ── Game Mode Editor ──────────────────────────────────────────────────
		if (m_ShowGameMode && ProjectManager::Get().HasProject())
		{
			const std::string& gmUUID = ProjectManager::Get().GetCurrent().ActiveGameModeUUID;

			ImGui::SetNextWindowSize(ImVec2(400, 260), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Game Mode", &m_ShowGameMode))
			{
				ImGui::SeparatorText("Game Mode Ativo");

				if (gmUUID.empty())
				{
					ImGui::TextDisabled("Nenhum. Duplo clique em um .axegamemode no Asset Browser.");
				}
				else
				{
					const AssetRecord* gmRec = AssetDatabase::Get().GetByUUID(gmUUID);
					std::string gmName = gmRec ? gmRec->Name : "(removido)";
					ImGui::Text("%s", gmName.c_str());
					ImGui::SameLine();
					if (ImGui::SmallButton("X##cleargm"))
					{
						ProjectManager::Get().GetCurrent().ActiveGameModeUUID = "";
						ProjectManager::Get().SaveProject();
					}

					if (gmRec)
					{
						auto gmAsset = GameModeAsset::LoadFromFile(gmRec->FilePath);
						if (gmAsset)
						{
							ImGui::Spacing();
							ImGui::SeparatorText("Pawn Padrão");

							std::string pawnLabel = "Nenhum";
							if (!gmAsset->DefaultPawnScriptUUID.empty())
							{
								const AssetRecord* pr = AssetDatabase::Get().GetByUUID(gmAsset->DefaultPawnScriptUUID);
								pawnLabel = pr ? (pr->Name + "  [" + pr->ScriptClassType + "]") : "(removido)";
							}
							ImGui::LabelText("Script##pawn", "%s", pawnLabel.c_str());
							ImGui::SameLine();
							if (ImGui::Button("Escolher##pawn"))
								ImGui::OpenPopup("##PawnPickGM");

							if (ImGui::BeginPopup("##PawnPickGM"))
							{
								ImGui::SeparatorText("Scripts disponíveis");
								for (auto& [uuid, rec] : AssetDatabase::Get().GetAll())
								{
									if (rec.Type != AssetType::Script) continue;
									std::string lbl = rec.Name;
									if (!rec.ScriptClassType.empty()) lbl += "  [" + rec.ScriptClassType + "]";
									if (ImGui::Selectable(lbl.c_str()))
									{
										gmAsset->DefaultPawnScriptUUID = uuid;
										gmAsset->Save(gmRec->FilePath);
									}
								}
								ImGui::EndPopup();
							}

							ImGui::Spacing();
							ImGui::TextDisabled("Configure a câmera no SpringArmComponent + CameraComponent do pawn.");
						}
					}
				}
			}
			ImGui::End();
		}

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

#ifdef ImGuiWindowFlags_NoDocking
		windowFlags |= ImGuiWindowFlags_NoDocking;
#endif


		//Pega o tamanho e posição da janela d sistema operacional

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		//Sem padding e sem borda - a janela host é invisivel
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));


		//Abre a janela host 
		ImGui::Begin("Dockspace Host", nullptr, windowFlags);
		//ImGui::Begin("Dockspace Host");

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
	void EditorUI::SetContext(EditorContext* context)
	{
		m_HierarchyWindow.SetContext(context);
		m_InspectorWindow.SetContext(context);
		m_AssetBowserWindow.SetContext(context);
		m_MaterialEditorWindow.SetContext(context);
		m_ParticleEditorWindow.SetContext(context);
		m_SoundCueEditorWindow.SetContext(context);
	}

	void EditorUI::SetViewportRenderer(ViewportRenderer* renderer)
	{
		m_ViewportRenderer = renderer;
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
				if (ImGui::MenuItem("Nova Cena", "Ctrl+N"))
				{
					if (OnNewScene) OnNewScene();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Desfazer", "Ctrl+Z",
					false, OnCanUndo && OnCanUndo()))
				{
					if (OnUndo) OnUndo();
				}

				if (ImGui::MenuItem("Refazer", "Ctrl+Y",
					false, OnCanRedo && OnCanRedo()))
				{
					if (OnRedo) OnRedo();
				}

				ImGui::Separator();

				bool playing = IsPlaying && IsPlaying();

				if (playing)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					ImGui::TextDisabled("  (Stop para salvar/carregar)");
					ImGui::PopStyleColor();
				}

				if (ImGui::MenuItem("Abrir Cena...", "Ctrl+O", false, !playing))
				{
					auto path = FileDialog::Open(
						"AXE Scene\0*.axescene\0All Files\0*.*\0",
						"Abrir Cena",
						"axescene");
					if (!path.empty() && OnOpenScene)
						OnOpenScene(path.string());
				}

				if (ImGui::MenuItem("Salvar Cena", "Ctrl+S", false, !playing))
				{
					if (OnSaveScene) OnSaveScene("");
				}

				if (ImGui::MenuItem("Salvar Cena Como...", "Ctrl+Shift+S", false, !playing))
				{
					auto path = FileDialog::Save(
						"AXE Scene\0*.axescene\0All Files\0*.*\0",
						"Salvar Cena",
						"axescene");
					if (!path.empty() && OnSaveScene)
						OnSaveScene(path.string());
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Salvar Projeto", nullptr, false, !playing))
				{
					if (OnSaveProject) OnSaveProject();
				}

				if (ImGui::MenuItem("Abrir Projeto...", nullptr, false, !playing))
				{
					auto path = FileDialog::Open(
						"AXE Project\0*.axeproject\0All Files\0*.*\0",
						"Abrir Projeto",
						"axeproject");
					if (!path.empty() && OnOpenProject)
						OnOpenProject(path.string());
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Sair", "Alt+F4"))
					EditorApp::Get().Close();

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
				ImGui::MenuItem("Viewport", nullptr, &m_ShowViewport);
				ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
				ImGui::MenuItem("Asset Browser", nullptr, &m_ShowAssetBrowser);
				ImGui::Separator();
				ImGui::MenuItem("Environment", nullptr, &m_ShowEnvironment);
				ImGui::Separator();
				ImGui::MenuItem("Game Mode", nullptr, &m_ShowGameMode);

				ImGui::Separator();
				bool seqOpen = m_SequencerWindow.IsOpen();
				if (ImGui::MenuItem("Sequencer", nullptr, &seqOpen)) {
					if (seqOpen) m_SequencerWindow.Open();
					else         m_SequencerWindow.Close();
				}


				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Project"))
			{
				if (ImGui::MenuItem("Input Settings"))
					m_InputSettingsWindow.Open();

				// B3.2 — em Project, junto do Input Settings: e informacao
				// sobre o PROJETO inteiro, nao sobre a cena aberta.
				if (ImGui::MenuItem("Asset Report"))
					m_AssetReportWindow.Open();

				ImGui::MenuItem("Audio Mixer", nullptr, &m_ShowAudioMixer);
				ImGui::EndMenu();
			}
			//m_SequencerWindow.OnImGuiRender();
			ImGui::EndMenuBar();
		}
	}

	void EditorUI::BuildDefaultLayout(ImGuiID dockspaceId)
	{
		// Só executa uma vez POR EditorUI — o ImGui salva o layout no
		// imgui.ini e restaura nas próximas vezes. Membro e não static pelo
		// mesmo motivo do m_LoadedInputProjectRoot: o reopen de projeto
		// destrói e recria este objeto.
		if (m_DefaultLayoutBuilt) return;
		m_DefaultLayoutBuilt = true;

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
		//ImGui::DockBuilderDockWindow("Material Editor", dockBottom);

		ImGui::DockBuilderFinish(dockspaceId);

	}







	// ─────────────────────────────────────────────────────────────────────────
	//  Audio Mixer
	//
	//  Um slider por bus. E o que um menu de opcoes do JOGO vai chamar por
	//  baixo — aqui serve para ouvir a mixagem enquanto se edita, sem ter que
	//  construir a tela de opcoes antes.
	//
	//  Os valores NAO sao salvos no projeto, de proposito: mixagem de sessao e
	//  ferramenta de escuta, nao autoria. Volume que deve persistir e decisao
	//  do jogo e mora nas preferencias dele.
	// ─────────────────────────────────────────────────────────────────────────
	void EditorUI::DrawAudioMixer()
	{
		if (!m_ShowAudioMixer)
			return;

		ImGui::SetNextWindowSize(ImVec2(300, 230), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Audio Mixer", &m_ShowAudioMixer))
		{
			ImGui::End();
			return;
		}

		if (!AudioEngine::IsInitialized())
		{
			ImGui::TextDisabled("Audio nao inicializado.");
			ImGui::End();
			return;
		}

		for (int i = 0; i < (int)AudioBus::Count; ++i)
		{
			const AudioBus bus = (AudioBus)i;

			float v = AudioEngine::GetBusVolume(bus);

			ImGui::PushID(i);

			if (ImGui::SliderFloat(AudioBusToString(bus), &v, 0.0f, 1.5f, "%.2f"))
				AudioEngine::SetBusVolume(bus, v);

			ImGui::PopID();

			// Master separado dos demais: ele nao e um grupo irmao, e sim o
			// volume final por onde todos passam.
			if (bus == AudioBus::Master)
				ImGui::Separator();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// ── Vozes ────────────────────────────────────────────────────────
		//
		// O medidor existe para o limite nao ser magico. Sem ele, "meu som
		// sumiu" e um misterio; com ele, voce ve o contador colado no teto e
		// sabe exatamente o que aconteceu.
		const int active = AudioEngine::GetActiveVoiceCount();
		const int maxV = AudioEngine::GetMaxVoices();

		ImGui::Text("Vozes: %d / %d", active, maxV);

		if (maxV > 0)
		{
			const float frac = (float)active / (float)maxV;

			ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
				frac > 0.9f ? ImVec4(0.90f, 0.35f, 0.30f, 1.0f)
				: frac > 0.6f ? ImVec4(0.90f, 0.70f, 0.25f, 1.0f)
				: ImVec4(0.35f, 0.70f, 0.45f, 1.0f));

			ImGui::ProgressBar(frac, ImVec2(-1.0f, 6.0f), "");
			ImGui::PopStyleColor();
		}

		int cap = maxV;

		if (ImGui::SliderInt("Teto", &cap, 0, 128))
			AudioEngine::SetMaxVoices(cap);

		ImGui::TextDisabled("Zero desliga o limite.");

		ImGui::Spacing();
		ImGui::TextDisabled("Sessao apenas — nao e salvo no projeto.");

		ImGui::End();
	}


}