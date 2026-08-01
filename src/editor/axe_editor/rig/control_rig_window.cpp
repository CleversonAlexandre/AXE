#include "control_rig_window.hpp"
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "axe/log/log.hpp"

#include <imgui_internal.h>
#include <ImGuizmo.h>

namespace axe
{
	void ControlRigWindow::OpenForAsset(const std::shared_ptr<ControlRigAsset>& rig,
		const std::shared_ptr<SkeletalMeshAsset>& skeleton)
	{
		if (!rig)
			return;

		// Carimbo de versao. Se esta linha aparecer no console ao abrir, os
		// arquivos do CONTROLRIG_V1 estao em uso.
		AXE_EDITOR_INFO("Control Rig editor - CONTROLRIG_V1");

		m_Asset = rig;
		m_Skeleton = skeleton;

		m_Open = true;
		m_Dirty = false;

		m_SelectedElement = -1;
		m_SelectedNode = -1;
		m_Renaming = -1;

		m_Filter[0] = '\0';

		// Historico e POR ASSET: abrir outro rig nao pode deixar comandos que
		// restaurariam o estado do anterior.
		m_History.Clear();
		m_PendingUndo = false;
		m_Baseline.reset();

		// O contexto do node-editor guarda posicoes por ID de no. Abrir OUTRO
		// rig sem zerar faria os nos nascerem onde estavam os do rig anterior.
		m_NeedsContextReset = true;
		m_NodePositionsLoaded = false;

		// A copia do preview e do rig ANTERIOR. Sem zerar, o primeiro frame do
		// rig novo desenharia o esqueleto do rig velho.
		InvalidatePreviewHierarchy();

		// Ossos novos no esqueleto entram agora. Um personagem reimportado com
		// dedos, por exemplo, apareceria sem eles ate alguem reparar.
		if (m_Skeleton && m_Skeleton->GetSkeleton())
		{
			const int added = m_Asset->SyncNewBones(*m_Skeleton->GetSkeleton());

			if (added > 0)
				m_Dirty = true;
		}

		m_Baseline = std::make_shared<RigSnapshot>(CaptureState());

		AXE_EDITOR_INFO("Control Rig '{}': {} elementos, {} nos.",
			m_Asset->GetName(),
			m_Asset->GetHierarchy().Size(),
			m_Asset->GetGraph().GetNodes().size());
	}

	void ControlRigWindow::Close()
	{
		m_Open = false;
		m_Asset.reset();
		m_Skeleton.reset();
	}

	bool ControlRigWindow::SaveAsset()
	{
		if (!m_Asset)
			return false;

		if (!m_Asset->Save())
		{
			AXE_EDITOR_ERROR("Control Rig: falha ao salvar '{}'.", m_Asset->GetName());
			return false;
		}

		// Toda instancia em execucao roda um CLONE do grafo; o contador e como
		// elas percebem que ficaram velhas.
		m_Asset->BumpVersion();

		m_Dirty = false;

		AXE_EDITOR_INFO("Control Rig '{}' salvo.", m_Asset->GetName());
		return true;
	}

	// ═══ Undo / Redo ═════════════════════════════════════════════════════════

	ControlRigWindow::RigSnapshot ControlRigWindow::CaptureState() const
	{
		RigSnapshot snap;

		if (m_Asset)
		{
			snap.Elements = m_Asset->GetHierarchy().GetElements();
			snap.Graph = m_Asset->GetGraph();
			snap.Functions = m_Asset->GetFunctions();
		}

		return snap;
	}

	void ControlRigWindow::RestoreState(const RigSnapshot& snap)
	{
		if (!m_Asset)
			return;

		// SetElements e nao GetElements() = ... : o cache de globais precisa
		// ser invalidado junto, senao a hierarquia volta mas as matrizes nao.
		m_Asset->GetHierarchy().SetElements(snap.Elements);
		m_Asset->GetGraph() = snap.Graph;
		m_Asset->GetFunctions() = snap.Functions;

		// O undo escreve DIRETO no asset e nao passa pelo MarkEdited — entao a
		// copia do preview precisa ser invalidada aqui, na mao. Sem isto, um
		// Ctrl+Z mudaria a hierarquia e o viewport continuaria mostrando a
		// anterior ate a proxima edicao qualquer.
		InvalidatePreviewHierarchy();

		// A selecao pode apontar pra algo que nao existe mais.
		if (m_SelectedElement >= (int)m_Asset->GetHierarchy().Size())
			m_SelectedElement = -1;

		if (m_SelectedNode >= 0 && !m_Asset->GetGraph().FindNode(m_SelectedNode))
			m_SelectedNode = -1;

		// A funcao aberta pode ter deixado de existir — um undo que desfez a
		// criacao dela. Sem isto o canvas apontaria pra um indice invalido.
		if (m_EditingFunction >= (int)m_Asset->GetFunctions().size())
		{
			m_EditingFunction = -1;
			m_SelectedNode = -1;
		}

		// As posicoes dos nos vieram do snapshot; o node-editor ainda tem as
		// antigas no contexto dele.
		m_NodePositionsLoaded = false;

		// O campo de rotacao guarda um Euler em cache que agora esta velho.
		m_EulerOwner[0] = -1;
		m_EulerOwner[1] = -1;

		m_Renaming = -1;
		m_Dirty = true;
	}

	void ControlRigWindow::CommitPendingUndo()
	{
		if (!m_PendingUndo || !m_Asset)
			return;

		// ── ESPERA O GESTO TERMINAR ──────────────────────────────────────────
		//
		// Enquanto um campo esta sendo arrastado (IsAnyItemActive) ou o gizmo
		// esta em uso, a acao ainda nao acabou. Fechar o comando agora criaria
		// um passo de undo POR FRAME do arrasto.
		// O botao do mouse ainda pressionado tambem conta como gesto em
		// andamento. E o que cobre o ARRASTO DE NO: o node-editor trata o
		// proprio input, entao IsAnyItemActive fica FALSO enquanto voce
		// arrasta um no — sem esta condicao, mover um no de um canto a outro
		// viraria um passo de undo por frame.
		if (ImGui::IsAnyItemActive() || ImGuizmo::IsUsing() ||
			ImGui::IsMouseDown(ImGuiMouseButton_Left))
			return;

		auto before = m_Baseline;
		auto after = std::make_shared<RigSnapshot>(CaptureState());

		// Primeira edicao da sessao sem baseline: nao ha "antes" pra voltar.
		if (before)
		{
			m_History.Push({
				m_PendingUndoName,

				// Execute = REFAZER. O Push chama isto na hora, mas a mudanca
				// ja esta aplicada, entao restaurar o "depois" e um no-op.
				[this, after]() { RestoreState(*after); },

				[this, before]() { RestoreState(*before); }
				});
		}

		m_Baseline = after;

		m_PendingUndo = false;
		m_PendingUndoName.clear();
	}

	void ControlRigWindow::DoUndo()
	{
		if (!m_History.CanUndo())
			return;

		m_History.Undo();

		// A baseline tem que acompanhar, senao o proximo comando gravaria um
		// "antes" que nao corresponde ao que esta na tela.
		m_Baseline = std::make_shared<RigSnapshot>(CaptureState());
	}

	void ControlRigWindow::DoRedo()
	{
		if (!m_History.CanRedo())
			return;

		m_History.Redo();
		m_Baseline = std::make_shared<RigSnapshot>(CaptureState());
	}

	void ControlRigWindow::DrawToolbar()
	{
		// Ver editor_widgets.hpp: icone + tooltip em vez de fila de palavras. A
		// toolbar tinha SEIS botoes de texto lado a lado, e ler seis palavras e
		// mais lento que reconhecer seis formas.
		if (ui::IconButton(ICON_SAVE, "Salvar  (Ctrl+S)", ui::Accent::Primary))
			SaveAsset();

		ImGui::SameLine();

		// Undo / Redo. Desabilitados quando nao ha o que desfazer, com o nome
		// da acao no tooltip — "desfazer o que?" e a duvida real.
		{
			const bool canUndo = m_History.CanUndo();

			ImGui::BeginDisabled(!canUndo);

			if (ui::IconButton(ICON_UNDO, nullptr))
				DoUndo();

			ImGui::EndDisabled();

			if (canUndo && ImGui::IsItemHovered())
				ImGui::SetTooltip("Desfazer: %s  (Ctrl+Z)", m_History.GetUndoName().c_str());

			ImGui::SameLine();

			const bool canRedo = m_History.CanRedo();

			ImGui::BeginDisabled(!canRedo);

			if (ui::IconButton(ICON_REDO, nullptr))
				DoRedo();

			ImGui::EndDisabled();

			if (canRedo && ImGui::IsItemHovered())
				ImGui::SetTooltip("Refazer: %s  (Ctrl+Shift+Z)", m_History.GetRedoName().c_str());
		}

		ui::ToolbarSeparator();

		// ── Reset pose ───────────────────────────────────────────────────────
		//
		// A rede de seguranca. A hierarquia do rig e uma COPIA do esqueleto, e
		// o asset e COMPARTILHADO com o jogo — mexer aqui aparece no viewport
		// na hora. Sem um caminho de volta, cada experimento no rig e uma
		// aposta.
		//
		// So os OSSOS. Os controles guardam alinhamento autorado, e leva-los
		// junto transformaria o botao de seguranca na pior perda possivel —
		// pra isso existe o "Reset to bind pose" por elemento, no menu de
		// contexto.
		{
			const bool canReset = (m_Skeleton && m_Skeleton->GetSkeleton());

			ImGui::BeginDisabled(!canReset);

			if (ui::IconButton(ICON_ROTATE_LEFT, nullptr, ui::Accent::Warning))
			{
				auto& h = m_Asset->GetHierarchy();

				const int n = h.ResetBonesToBindPose(*m_Skeleton->GetSkeleton());

				if (n > 0)
					MarkEdited("Reset pose");

				AXE_CORE_INFO("Control Rig: {} ossos devolvidos ao repouso do esqueleto.", n);
			}

			ImGui::EndDisabled();

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(canReset
					? "Devolve os OSSOS ao repouso do .axeskel.\n"
					"Controles e Nulls nao sao tocados.\n"
					"Da pra desfazer com Ctrl+Z."
					: "Sem esqueleto carregado.");
			}
		}

		ImGui::SameLine();

		// ── Backward solve ───────────────────────────────────────────────────
		//
		// Fica ao lado do Reset pose de proposito: os dois sao operacoes
		// PONTUAIS sobre a pose, e nao estado que fica ligado. Agrupa-los evita
		// procura-los em cantos diferentes da janela.
		{
			const bool hasEvent = m_Asset
				&& m_Asset->GetGraph().FindNodeByType("BackwardsSolve") != nullptr;

			ImGui::BeginDisabled(!hasEvent);

			if (ui::IconButton(ICON_ARROW_LEFT, nullptr, ui::Accent::Warning))
				RunBackwardSolve();

			ImGui::EndDisabled();

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(hasEvent
					? "Le os ossos e encosta os controles neles.\n"
					"Roda uma vez. Da pra desfazer com Ctrl+Z."
					: "O grafo nao tem um no Backward Solve.\n"
					"Botao direito no grafo -> Events -> Backward Solve.");
			}

			ImGui::SameLine();
		}

		ui::ToolbarSeparator();

		if (ui::IconButton(ICON_TABLE_CELLS, "Restaurar o arranjo das janelas"))
			m_ResetLayout = true;

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Restores the default panel arrangement.\n"
				"Your own arrangement is remembered between sessions.");

		ImGui::SameLine();

		if (m_Dirty)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "*  unsaved changes");
		else
			ImGui::TextDisabled("|  right-click an element to add a Control or Null  |  right-click the graph for nodes  |  double-click a wire to insert a reroute");

		ImGui::Separator();
	}

	void ControlRigWindow::Draw()
	{
		if (!m_Open || !m_Asset)
			return;

		if (m_NeedsContextReset)
		{
			// O contexto de funcao vai junto: ele guarda posicoes de nos de
			// OUTRO asset, e reaproveita-lo misturaria os dois.
			if (m_FuncEdCtx)
			{
				ed::DestroyEditor(m_FuncEdCtx);
				m_FuncEdCtx = nullptr;
			}

			m_EditingFunction = -1;

			if (m_EdCtx)
				ed::DestroyEditor(m_EdCtx);

			ed::Config cfg;

			// Sem arquivo de settings: as posicoes dos nos moram no .axerig,
			// nao num .json solto ao lado do executavel.
			cfg.SettingsFile = nullptr;

			m_EdCtx = ed::CreateEditor(&cfg);
			m_NeedsContextReset = false;
		}

		const std::string title = "Control Rig - " + m_Asset->GetName()
			+ (m_Dirty ? " *" : "") + "###ControlRigWindow";

		m_Focused = false;

		// Renderiza o preview ANTES de submeter a UI: a imagem exibida abaixo
		// e a textura produzida aqui.
		RenderPreview();

		bool open = true;

		ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_FirstUseEver);

		const bool visible = ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse);

		// ── O SUFIXO DE VERSAO NO ID NAO E ENFEITE ───────────────────────
		//
		// O layout padrao so e construido quando o dockspace ainda NAO existe.
		// Isso e o certo — o arranjo que voce montou nao pode ser desfeito a
		// cada abertura. Mas tem um efeito colateral: quando EU acrescento um
		// painel novo, o dockspace ja existe no imgui.ini do usuario, o layout
		// nao e reconstruido, e o painel novo fica FLUTUANDO sem casa (foi
		// exatamente o que aconteceu com o Preview).
		//
		// Subir o numero aqui a cada vez que o CONJUNTO de paineis muda forca
		// UMA reconstrucao, e so uma. Custo: quem tinha arranjo proprio perde
		// ele nessa virada — bem menos irritante que um painel invisivel.
		const ImGuiID dockId = ImGui::GetID("ControlRigDock_v2");

		if (visible)
		{
			DrawToolbar();

			DrawDockLayout(dockId);

			ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
		}
		else
		{
			// JANELA OCULTA (outra aba na frente, ou recolhida).
			//
			// Sem esta chamada o no de dock morre no frame em que a janela sai
			// de vista — e os paineis, que sao janelas de topo, perdem a casa e
			// voltam a FLUTUAR soltos pela tela. Era o que acontecia ao clicar
			// na aba Viewport.
			//
			// KeepAliveOnly mantem o no vivo sem desenhar nada.
			ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
		}

		ImGui::End();

		// ── Os paineis ───────────────────────────────────────────────────
		//
		// Submetidos FORA do Begin/End da janela-mae: e assim que o docking do
		// ImGui funciona — sao janelas de topo, e o dockspace so as hospeda.
		//
		// So quando a mae esta visivel: submeter com ela oculta os desenharia
		// por cima do que estiver na frente.
		if (visible)
		{
			// Barra horizontal: mesmo com o painel estreito, a arvore indenta
			// fundo e o nome nunca fica inalcancavel.
			// O foco e acumulado painel a painel: Ctrl+Z so pode agir quando
			// VOCE esta no editor de rig, senao ele desfaria coisas daqui
			// enquanto voce trabalha noutra janela.
			auto noteFocus = [&]()
				{
					if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
						m_Focused = true;
				};

			ImGui::Begin("Hierarchy###RigHierarchy", nullptr, ImGuiWindowFlags_HorizontalScrollbar);
			noteFocus();
			DrawHierarchyPanel();
			ImGui::End();

			ImGui::Begin("Rig Graph###RigGraph");
			noteFocus();
			DrawGraphCanvas();
			ImGui::End();

			ImGui::Begin("Rig Members###RigMembers");
			noteFocus();
			DrawMembersPanel();
			ImGui::End();

			ImGui::Begin("Details###RigDetails");
			noteFocus();
			DrawDetailsPanel();
			ImGui::End();

			DrawPreviewWindow();
		}

		// ── Atalhos ──────────────────────────────────────────────────────
		//
		// Depois de submeter tudo: e aqui que m_Focused ja esta preenchido.
		if (m_Focused)
		{
			ImGuiIO& io = ImGui::GetIO();

			// Ctrl+Z / Ctrl+Shift+Z, como no Material Editor.
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
			{
				if (io.KeyShift)
					DoRedo();
				else
					DoUndo();
			}

			// Ctrl+Y tambem refaz: e o atalho que muita gente tem no dedo.
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
				DoRedo();

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
				SaveAsset();
		}

		// Fecha o comando pendente, se o gesto ja acabou. Tem que ser DEPOIS
		// de toda a UI: e o unico ponto do frame em que sabemos que nenhum
		// campo continua sendo arrastado.
		CommitPendingUndo();

		if (!open)
			Close();
	}

	void ControlRigWindow::DrawDockLayout(ImGuiID dockspaceId)
	{
		// Layout padrao, construido so quando o dockspace AINDA NAO EXISTE.
		//
		// Uma flag de membro nao bastaria: o objeto persiste entre aberturas
		// da janela, mas o ImGui recria o dockspace toda vez que ela reabre —
		// e a flag, ja em true, pularia a construcao e deixaria os paineis sem
		// casa. DockBuilderGetNode == null e a pergunta certa: "nunca foi
		// montado?". Enquanto existir — inclusive com o arranjo que VOCE
		// mexeu, que o ImGui guarda no imgui.ini — nao encostamos nele.
		if (!m_ResetLayout && ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
			return;

		m_ResetLayout = false;

		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetContentRegionAvail());

		ImGuiID center = dockspaceId;

		// Hierarquia mais larga que o padrao dos outros editores: nomes de
		// osso com namespace ("mixamorig:LeftHandThumb3") sao compridos, e
		// esta e a coluna onde voce mais le.
		ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, nullptr, &center);
		const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);

		// Preview EMBAIXO da hierarquia: as duas se leem juntas — voce clica no
		// osso na arvore e ve onde ele esta.
		ImGuiID leftBottom = left;
		const ImGuiID leftTop = ImGui::DockBuilderSplitNode(leftBottom, ImGuiDir_Up, 0.55f, nullptr, &leftBottom);

		// Rig Members COM a hierarquia, na mesma aba: as duas listam o que o
		// rig CONTEM (elementos de um lado, funcoes do outro), e ocupam o mesmo
		// lugar no fluxo — voce vai la pra escolher no que mexer. E o mesmo
		// arranjo do Script Members no Script Editor.
		ImGui::DockBuilderDockWindow("Hierarchy###RigHierarchy", leftTop);
		ImGui::DockBuilderDockWindow("Rig Members###RigMembers", leftTop);
		ImGui::DockBuilderDockWindow("Preview###RigPreview", leftBottom);
		ImGui::DockBuilderDockWindow("Rig Graph###RigGraph", center);
		ImGui::DockBuilderDockWindow("Details###RigDetails", right);

		ImGui::DockBuilderFinish(dockspaceId);
	}

} // namespace axe