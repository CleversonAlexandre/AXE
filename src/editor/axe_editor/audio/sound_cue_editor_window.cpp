#include "editor/axe_editor/audio/sound_cue_editor_window.hpp"
#include "editor/axe_editor/asset/asset_picker.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"
#include "editor/axe_editor/ui/editor_widgets.hpp"

#include "axe/audio/audio_engine.hpp"
#include "axe/audio/audio_clip.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui-node-editor/imgui_node_editor.h>
#include <utilities/widgets.h>

#include <algorithm>
#include <vector>

namespace ed = ax::NodeEditor;

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════════
	//  CANVAS DO SOUND CUE
	//
	//  DUAS CAMADAS DE COR, COM SIGNIFICADOS DIFERENTES:
	//
	//    cabecalho -> o PAPEL do no (o que ele faz com o som)
	//    pino/fio  -> o SINAL, que aqui e sempre o mesmo: uma cor so
	//
	//  A segunda linha e a que importa e a que e facil errar. No rig a cor do
	//  pino significa TIPO DE DADO — Transform, Item, Exec sao coisas
	//  diferentes, e a cor te impede de ligar errado. Aqui existe um tipo so:
	//  som descendo pra raiz.
	//
	//  Uma versao anterior coloria o fio pela origem, achando que isso
	//  ajudaria a ler a cadeia. Fazia o oposto: quatro cores de fio sugerem
	//  quatro tipos de conexao que nao existem, e o olho fica procurando uma
	//  regra que nao esta la. Pelo mesmo motivo o Output nao usa o triangulo
	//  de fluxo do rig — la ele marca EXECUCAO, um sinal de outra natureza;
	//  aqui o Output recebe exatamente o que todo mundo recebe.
	// ═════════════════════════════════════════════════════════════════════════

	namespace
	{
		constexpr float kPinIcon = 18.0f;
		constexpr float kMinNodeW = 168.0f;

		const char* TypeName(SoundCueNodeType t)
		{
			switch (t)
			{
			case SoundCueNodeType::Output:      return "Output";
			case SoundCueNodeType::WavePlayer:  return "Wave Player";
			case SoundCueNodeType::Random:      return "Random";
			case SoundCueNodeType::Modulator:   return "Modulator";
			case SoundCueNodeType::Attenuation: return "Attenuation";
			}
			return "?";
		}

		ImVec4 TypeColor(SoundCueNodeType t)
		{
			switch (t)
			{
			case SoundCueNodeType::Output:      return { 0.85f, 0.42f, 0.22f, 1.0f };
			case SoundCueNodeType::WavePlayer:  return { 0.22f, 0.55f, 0.80f, 1.0f };
			case SoundCueNodeType::Random:      return { 0.62f, 0.35f, 0.72f, 1.0f };
			case SoundCueNodeType::Modulator:   return { 0.25f, 0.62f, 0.38f, 1.0f };
			case SoundCueNodeType::Attenuation: return { 0.72f, 0.60f, 0.20f, 1.0f };
			}
			return { 0.5f, 0.5f, 0.5f, 1.0f };
		}

		// A cor do SINAL. Uma so, porque so ha um tipo de conexao no cue.
		// Creme quente: distinta dos quatro tons de cabecalho, e calma o
		// bastante pra os fios nao competirem com os nos pela atencao.
		constexpr ImVec4 kSignal{ 0.92f, 0.88f, 0.76f, 1.0f };

		// Borda de selecao — tambem neutra, pelo mesmo motivo: se ela mudasse
		// de cor por tipo, "selecionado" viraria mais uma cor pra decifrar.
		constexpr ImVec4 kSelected{ 1.0f, 0.80f, 0.35f, 1.0f };

		void PinIcon(bool connected)
		{
			// Circulo pra TODOS, inclusive o Output. Forma diferente teria
			// que significar coisa diferente.
			ax::Widgets::Icon(ImVec2(kPinIcon, kPinIcon),
				ax::Drawing::IconType::Circle,
				connected, kSignal, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));
		}

		// Clipe resolvido pro desenho da onda. Passa pelo cache do
		// AudioEngine, entao um .wav usado em cinco nos e decodificado uma
		// vez so.
		std::shared_ptr<AudioClip> WaveClip(const std::string& uuid)
		{
			if (uuid.empty())
				return nullptr;

			return AudioEngine::GetClip(uuid);
		}

		std::string WaveLabel(const std::string& uuid)
		{
			if (uuid.empty())
				return "(sem som)";

			if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid))
				return rec->Name;

			return "(ausente)";
		}

		// Linhas do corpo do no: o que ele faz, em texto curto. Medidas antes
		// de desenhar porque a largura do no depende delas.
		std::vector<std::string> BodyLines(const SoundCueNode& n, int childCount)
		{
			char buf[128];
			std::vector<std::string> out;

			switch (n.Type)
			{
			case SoundCueNodeType::WavePlayer:
				out.push_back(WaveLabel(n.WaveUUID));
				break;

				// (a forma de onda do Wave Player e desenhada a parte, no
				//  DrawNode: ela nao e texto e nao entra na medicao por string)

			case SoundCueNodeType::Random:
				snprintf(buf, sizeof(buf), "%d variacao(oes)", childCount);
				out.push_back(buf);
				if (n.NoRepeat) out.push_back("sem repetir");
				break;

			case SoundCueNodeType::Modulator:
				snprintf(buf, sizeof(buf), "vol  %.2f - %.2f", n.VolumeMin, n.VolumeMax);
				out.push_back(buf);
				snprintf(buf, sizeof(buf), "pitch %.2f - %.2f", n.PitchMin, n.PitchMax);
				out.push_back(buf);
				break;

			case SoundCueNodeType::Attenuation:
				snprintf(buf, sizeof(buf), "%s   %.0f - %.0f",
					n.Is3D ? "3D" : "2D", n.MinDistance, n.MaxDistance);
				out.push_back(buf);
				break;

			default:
				break;
			}

			return out;
		}
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  Ciclo de vida
	// ═════════════════════════════════════════════════════════════════════════

	SoundCueEditorWindow::~SoundCueEditorWindow()
	{
		if (m_Ed)
			ed::DestroyEditor(m_Ed);
	}

	void SoundCueEditorWindow::OpenAsset(std::shared_ptr<SoundCueAsset> cue,
		const std::filesystem::path& path)
	{
		if (!cue)
			return;

		// Contexto novo por asset: o node-editor guarda posicao e selecao por
		// contexto. Reaproveitar entre cues faria o segundo abrir com o
		// enquadramento do primeiro.
		if (m_Ed)
		{
			ed::DestroyEditor(m_Ed);
			m_Ed = nullptr;
		}

		ed::Config config;
		config.SettingsFile = nullptr;   // a posicao vive no .axecue, nao num .ini
		m_Ed = ed::CreateEditor(&config);

		m_Cue = cue;
		m_Path = path;
		m_Open = true;
		m_Dirty = false;
		m_LayoutApplied = false;
		m_SelectedNode = 0;
		m_FrameCount = 0;
		m_LastPreviewWave.clear();

		m_History = CommandHistory{};
		m_PendingUndo = false;
		m_Baseline = std::make_shared<CueSnapshot>(CaptureState());
	}

	void SoundCueEditorWindow::Save()
	{
		if (!m_Cue || m_Path.empty())
			return;

		if (m_Cue->Save(m_Path))
		{
			m_Dirty = false;
			AXE_EDITOR_INFO("Sound Cue '{}' salvo.", m_Cue->GetName());
		}
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  Undo / Redo
	// ═════════════════════════════════════════════════════════════════════════

	SoundCueEditorWindow::CueSnapshot SoundCueEditorWindow::CaptureState() const
	{
		CueSnapshot s;

		if (m_Cue)
		{
			s.Nodes = m_Cue->GetNodes();
			s.Links = m_Cue->GetLinks();
			s.NextId = m_Cue->GetNextId();
		}

		return s;
	}

	void SoundCueEditorWindow::RestoreState(const CueSnapshot& snap)
	{
		if (!m_Cue)
			return;

		m_Cue->GetNodes() = snap.Nodes;
		m_Cue->GetLinks() = snap.Links;
		m_Cue->SetNextId(snap.NextId);

		// Reaplica as posicoes no canvas: o node-editor guarda a posicao dele
		// proprio, e sem isto um undo restauraria o grafo mas deixaria os nos
		// onde estavam na tela.
		if (m_Ed)
		{
			ed::SetCurrentEditor(m_Ed);

			for (const auto& n : snap.Nodes)
				ed::SetNodePosition(n.Id, ImVec2(n.EditorPos.x, n.EditorPos.y));

			ed::SetCurrentEditor(nullptr);
		}

		if (!m_Cue->FindNode(m_SelectedNode))
			m_SelectedNode = 0;

		m_Dirty = true;
	}

	void SoundCueEditorWindow::CommitPendingUndo()
	{
		if (!m_PendingUndo)
			return;

		// Espera o gesto acabar. Sem isto, arrastar um no por meio segundo
		// geraria trinta passos de undo.
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			return;

		m_PendingUndo = false;

		const CueSnapshot before = m_Baseline ? *m_Baseline : CaptureState();
		const CueSnapshot after = CaptureState();

		m_History.Push({ m_PendingUndoName,
			[this, after]() { RestoreState(after); },
			[this, before]() { RestoreState(before); } });

		m_Baseline = std::make_shared<CueSnapshot>(after);
	}

	void SoundCueEditorWindow::DoUndo()
	{
		if (!m_History.CanUndo())
			return;

		m_History.Undo();

		// A baseline acompanha, senao o proximo comando gravaria um "antes"
		// que nao corresponde ao que esta na tela.
		m_Baseline = std::make_shared<CueSnapshot>(CaptureState());
	}

	void SoundCueEditorWindow::DoRedo()
	{
		if (!m_History.CanRedo())
			return;

		m_History.Redo();
		m_Baseline = std::make_shared<CueSnapshot>(CaptureState());
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  Consultas ao grafo
	// ═════════════════════════════════════════════════════════════════════════

	std::vector<int> SoundCueEditorWindow::ChildrenOf(int nodeId) const
	{
		std::vector<int> out;

		// A ORDEM DA LISTA DE LINKS e a ordem das entradas, e e a mesma que o
		// SoundCueAsset::Evaluate usa. Manter as duas iguais e o que faz o
		// peso mostrado na UI corresponder ao filho certo.
		for (const auto& l : m_Cue->GetLinks())
			if (l.ToNodeId == nodeId)
				out.push_back(l.FromNodeId);

		return out;
	}

	int SoundCueEditorWindow::ChildCountOf(int nodeId) const
	{
		return (int)ChildrenOf(nodeId).size();
	}

	int SoundCueEditorWindow::InputCountOf(const SoundCueNode& node) const
	{
		switch (node.Type)
		{
		case SoundCueNodeType::WavePlayer:
			return 0;

		case SoundCueNodeType::Random:
			// Filhos conectados + UMA vaga livre. E o que permite arrastar a
			// proxima variacao sem botao de "adicionar entrada": o pino vago
			// sempre existe, e some assim que e preenchido — aparecendo outro
			// logo abaixo.
			return ChildCountOf(node.Id) + 1;

		default:
			return 1;
		}
	}

	bool SoundCueEditorWindow::InputIsLinked(int nodeId, int slot) const
	{
		return slot < ChildCountOf(nodeId);
	}

	bool SoundCueEditorWindow::OutputIsLinked(int nodeId) const
	{
		for (const auto& l : m_Cue->GetLinks())
			if (l.FromNodeId == nodeId)
				return true;

		return false;
	}

	bool SoundCueEditorWindow::WouldCreateCycle(int fromNodeId, int toNodeId) const
	{
		// `from` viraria filho de `to`. Ha ciclo se `to` ja e alcancavel a
		// partir de `from`, descendo pelos filhos.
		std::vector<int> stack{ fromNodeId };
		int guard = 0;

		while (!stack.empty() && guard++ < 512)
		{
			const int cur = stack.back();
			stack.pop_back();

			if (cur == toNodeId)
				return true;

			for (const auto& l : m_Cue->GetLinks())
				if (l.ToNodeId == cur)
					stack.push_back(l.FromNodeId);
		}

		return false;
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  Janela-mae e dockspace
	// ═════════════════════════════════════════════════════════════════════════

	void SoundCueEditorWindow::Draw()
	{
		if (!m_Open || !m_Cue)
			return;

		m_Focused = false;
		++m_FrameCount;

		const std::string title = "Sound Cue - " + m_Cue->GetName()
			+ (m_Dirty ? " *" : "") + "###SoundCueEditor";

		ImGui::SetNextWindowSize(ImVec2(1180.0f, 680.0f), ImGuiCond_FirstUseEver);

		bool open = true;
		const bool visible = ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse);

		// Sufixo de versao no id do dock: quando o CONJUNTO de paineis muda,
		// subir o numero forca UMA reconstrucao do layout. Sem isso, um painel
		// novo nasce flutuando sem casa pra quem ja tem imgui.ini salvo.
		const ImGuiID dockId = ImGui::GetID("SoundCueDock_v1");

		if (visible)
		{
			DrawToolbar();
			DrawDockLayout(dockId);
			ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
		}
		else
		{
			// Janela oculta (outra aba na frente). Sem esta chamada o no de
			// dock morre no frame em que ela sai de vista, e os paineis —
			// que sao janelas de topo — voltam a flutuar soltos pela tela.
			ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
		}

		ImGui::End();

		// Paineis submetidos FORA do Begin/End da mae: e assim que o docking
		// do ImGui funciona — sao janelas de topo, e o dockspace so as
		// hospeda.
		if (visible)
		{
			auto noteFocus = [&]()
				{
					if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
						m_Focused = true;
				};

			ImGui::Begin("Cue Graph###SoundCueGraph");
			noteFocus();
			DrawGraphPanel();
			ImGui::End();

			ImGui::Begin("Details###SoundCueDetails");
			noteFocus();
			DrawDetailsPanel();
			ImGui::End();
		}

		HandleShortcuts();
		CommitPendingUndo();

		if (!open)
			Close();
	}

	void SoundCueEditorWindow::DrawDockLayout(ImGuiID dockspaceId)
	{
		// Layout padrao construido so quando o dockspace AINDA NAO EXISTE.
		// Uma flag de membro nao bastaria: o objeto persiste entre aberturas,
		// mas o ImGui recria o dockspace toda vez que a janela reabre — e a
		// flag, ja em true, pularia a construcao e deixaria os paineis sem
		// casa. DockBuilderGetNode == null e a pergunta certa.
		if (!m_ResetLayout && ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
			return;

		m_ResetLayout = false;

		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetContentRegionAvail());

		ImGuiID center = dockspaceId;
		const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f,
			nullptr, &center);

		ImGui::DockBuilderDockWindow("Cue Graph###SoundCueGraph", center);
		ImGui::DockBuilderDockWindow("Details###SoundCueDetails", right);

		ImGui::DockBuilderFinish(dockspaceId);
	}

	void SoundCueEditorWindow::HandleShortcuts()
	{
		// So com a janela em foco: senao o Ctrl+Z daqui desfaria coisas do cue
		// enquanto voce trabalha na cena.
		if (!m_Focused)
			return;

		const ImGuiIO& io = ImGui::GetIO();

		if (!io.KeyCtrl)
			return;

		if (ImGui::IsKeyPressed(ImGuiKey_S, false))
			Save();
		else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
			DoRedo();
		else if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
		{
			if (io.KeyShift) DoRedo();
			else             DoUndo();
		}
	}

	void SoundCueEditorWindow::DrawToolbar()
	{
		if (ui::IconButton(ICON_SAVE, "Salvar  (Ctrl+S)", ui::Accent::Primary))
			Save();

		ImGui::SameLine();
		ui::ToolbarSeparator();
		ImGui::SameLine();

		ImGui::BeginDisabled(!m_History.CanUndo());
		if (ui::IconButton(ICON_UNDO, "Desfazer  (Ctrl+Z)"))
			DoUndo();
		ImGui::EndDisabled();

		ImGui::SameLine();

		ImGui::BeginDisabled(!m_History.CanRedo());
		if (ui::IconButton(ICON_REDO, "Refazer  (Ctrl+Y)"))
			DoRedo();
		ImGui::EndDisabled();

		ImGui::SameLine();
		ui::ToolbarSeparator();
		ImGui::SameLine();

		if (ui::IconButton(ICON_PLAY, "Ouvir o cue agora.\n"
			"Clique varias vezes para ouvir a variacao.", ui::Accent::Primary))
			Preview();

		ImGui::SameLine();

		if (!m_LastPreviewWave.empty())
			ImGui::TextDisabled("  %s    vol %.2f    pitch %.2f",
				m_LastPreviewWave.c_str(), m_LastPreviewVolume, m_LastPreviewPitch);
		else
			ImGui::TextDisabled("  botao direito no grafo cria nos");

		ImGui::Separator();
	}

	void SoundCueEditorWindow::Preview()
	{
		if (!m_Cue)
			return;

		SoundCueResult r;

		if (!m_Cue->Evaluate(r))
		{
			AXE_EDITOR_WARN("Sound Cue: nada a tocar — verifique se o Output esta "
				"conectado e se ha um Wave Player com som.");
			return;
		}

		// Toca o RESULTADO da avaliacao, e nao o UUID do cue: assim o preview
		// funciona com alteracoes ainda nao salvas. Passar o cue pelo
		// AudioEngine leria o arquivo em disco, que e a versao antiga.
		AudioEngine::PlayOneShot(r.WaveUUID, r.Volume, r.Pitch);

		m_LastPreviewWave = WaveLabel(r.WaveUUID);
		m_LastPreviewVolume = r.Volume;
		m_LastPreviewPitch = r.Pitch;
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  Canvas
	// ═════════════════════════════════════════════════════════════════════════

	void SoundCueEditorWindow::DrawGraphPanel()
	{
		// Medido ANTES do ed::Begin: dentro do canvas o ImGui esta em espaco
		// transformado, e o retangulo sairia errado.
		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
		const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

		ed::SetCurrentEditor(m_Ed);
		ed::Begin("SoundCueGraph", ImVec2(0.0f, 0.0f));

		ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 4, 8, 8));
		ed::PushStyleVar(ed::StyleVar_NodeRounding, 5.0f);

		for (auto& node : m_Cue->GetNodes())
		{
			// Posicao vinda do asset, aplicada UMA vez. Reaplicar todo frame
			// prenderia o no no lugar e o arrasto nao funcionaria.
			if (!m_LayoutApplied)
				ed::SetNodePosition(node.Id, ImVec2(node.EditorPos.x, node.EditorPos.y));

			DrawNode(node);
		}

		ed::PopStyleVar(2);

		for (const auto& l : m_Cue->GetLinks())
		{
			int slot = 0;

			for (const auto& other : m_Cue->GetLinks())
			{
				if (&other == &l)
					break;

				if (other.ToNodeId == l.ToNodeId)
					++slot;
			}

			ed::Link(l.Id,
				MakePinId(l.FromNodeId, 0),
				MakePinId(l.ToNodeId, 1 + slot),
				kSignal, 2.0f);
		}

		m_LayoutApplied = true;

		// Grava a posicao TODO frame, como o Control Rig faz: barato, e um
		// arrasto nunca se perde por a janela ter sido fechada antes de
		// salvar. FLT_MAX significa "no ainda nao desenhado" — gravar isso
		// escreveria lixo no asset.
		for (auto& n : m_Cue->GetNodes())
		{
			const ImVec2 p = ed::GetNodePosition(n.Id);

			if (p.x > 1e6f || p.y > 1e6f)
				continue;

			if (p.x != n.EditorPos.x || p.y != n.EditorPos.y)
			{
				n.EditorPos = { p.x, p.y };
				MarkEdited("Mover no");
			}
		}

		if (m_FrameCount > 2)
		{
			HandleCreate();
			HandleDelete();
			HandleContextMenus();
		}

		{
			ed::NodeId sel = 0;

			if (ed::GetSelectedNodes(&sel, 1) > 0)
				m_SelectedNode = (int)sel.Get();
		}

		// Copiar/colar DENTRO do canvas, mas antes do End: precisa da selecao
		// e do ScreenToCanvas, que so existem com o editor corrente ativo.
		//
		// A guarda de foco nao e zelo: sem ela um Ctrl+V digitado no campo de
		// um Details despejaria nos no grafo. IsAnyItemActive cobre o caso de
		// estar editando um valor dentro do proprio canvas.
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::IsAnyItemActive())
		{
			const ImGuiIO& io = ImGui::GetIO();

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
				CopySelectedNodes(false);

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
				CopySelectedNodes(true);

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
			{
				// Cola sob o MOUSE quando ele esta sobre o canvas. Fora dele,
				// num degrau a partir de onde foi copiado — senao a copia
				// nasceria em cima da original e pareceria que nada aconteceu.
				const bool overCanvas =
					ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

				PasteNodes(overCanvas
					? ed::ScreenToCanvas(ImGui::GetMousePos())
					: ImVec2(m_Clipboard.AnchorX + 40.0f, m_Clipboard.AnchorY + 40.0f));
			}

			// Ctrl+D: duplicar. Internamente E copiar e colar, entao
			// SOBRESCREVE o clipboard — igual ao Blueprint.
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
			{
				CopySelectedNodes(false);
				PasteNodes(ImVec2(m_Clipboard.AnchorX + 40.0f,
					m_Clipboard.AnchorY + 40.0f));
			}
		}

		HandleAssetDrop(canvasMin, canvasSize);

		ed::End();

		// Fora do Begin/End (criar nos no meio do canvas confunde o layout do
		// frame corrente) mas AINDA COM O EDITOR CORRENTE ATIVO.
		//
		// A ordem aqui nao e detalhe: ed::SetNodePosition resolve o contexto
		// atual, e depois de SetCurrentEditor(nullptr) ele desreferencia nulo
		// e derruba o editor. Foi exatamente o crash da primeira versao —
		// stack terminando em EditorContext::FindNode com ponteiro invalido.
		if (!m_PendingDrop.empty())
		{
			SpawnWavePlayers(m_PendingDrop, m_DropPos);
			m_PendingDrop.clear();
		}

		ed::SetCurrentEditor(nullptr);
	}

	// ─────────────────────────────────────────────────────────────────────────
	void SoundCueEditorWindow::HandleAssetDrop(const ImVec2& canvasMin,
		const ImVec2& canvasSize)
	{
		// Suspend porque aqui o ImGui volta ao espaco de TELA: tanto o
		// retangulo quanto o ScreenToCanvas so dao o valor certo assim.
		ed::Suspend();

		const ImRect dropRect(canvasMin,
			ImVec2(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y));

		if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("cue_graph_drop")))
		{
			auto accept = [&](const char* type)
				{
					const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(type);

					if (!pl || !pl->Data)
						return;

					std::vector<std::string> uuids;
					const char* text = (const char*)pl->Data;

					// O payload de lista vem com UUIDs separados por '\n'.
					std::string cur;

					for (const char* c = text; *c; ++c)
					{
						if (*c == '\n') { if (!cur.empty()) uuids.push_back(cur); cur.clear(); }
						else { cur += *c; }
					}

					if (!cur.empty())
						uuids.push_back(cur);

					// So audio entra. Soltar uma malha aqui nao deve criar um
					// Wave Player mudo — melhor nao acontecer nada.
					std::vector<std::string> audio;

					for (const auto& u : uuids)
					{
						const AssetRecord* rec = AssetDatabase::Get().GetByUUID(u);

						if (rec && AssetTypeFromExtension(rec->FilePath.extension().string())
							== AssetType::Audio)
							audio.push_back(u);
					}

					if (audio.empty())
						return;

					m_PendingDrop = audio;
					m_DropPos = ed::ScreenToCanvas(ImGui::GetMousePos());
				};

			accept("ASSET_UUID");
			accept("ASSET_UUID_LIST");

			ImGui::EndDragDropTarget();
		}

		ed::Resume();
	}

	void SoundCueEditorWindow::SpawnWavePlayers(const std::vector<std::string>& uuids,
		const ImVec2& canvasPos)
	{
		if (!m_Cue || !m_Ed || uuids.empty())
			return;

		// Garante o contexto mesmo se alguem chamar isto de outro ponto no
		// futuro: SetNodePosition sem editor corrente e crash, nao no-op.
		ed::SetCurrentEditor(m_Ed);

		// Empilhados verticalmente a partir do ponto do drop. Todos na mesma
		// posicao ficariam um em cima do outro e voce teria que separar tres
		// nos na mao antes de ligar qualquer coisa.
		const float step = 96.0f;
		float y = canvasPos.y;

		for (const auto& uuid : uuids)
		{
			SoundCueNode n;
			n.Id = m_Cue->AllocId();
			n.Type = SoundCueNodeType::WavePlayer;
			n.WaveUUID = uuid;
			n.EditorPos = { canvasPos.x, y };

			m_Cue->GetNodes().push_back(n);
			ed::SetNodePosition(n.Id, ImVec2(canvasPos.x, y));

			y += step;
			m_SelectedNode = n.Id;
		}

		MarkEdited(uuids.size() > 1 ? "Soltar sons" : "Soltar som");
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  Copiar / Colar
	// ═════════════════════════════════════════════════════════════════════════

	void SoundCueEditorWindow::CopySelectedNodes(bool cut)
	{
		// GetSelectedObjectCount conta nos E fios; GetSelectedNodes devolve
		// quantos eram nos de fato. Dimensionamos pelo total e confiamos no
		// retorno — um vetor curto seria escrita fora dos limites.
		const int total = ed::GetSelectedObjectCount();

		if (total <= 0)
			return;

		std::vector<ed::NodeId> sel((std::size_t)total);
		const int count = ed::GetSelectedNodes(sel.data(), total);

		// O Output fica de fora, copiando OU recortando: o grafo tem uma raiz
		// so, e uma segunda faria metade do cue virar codigo morto sem aviso.
		// Mesma razao pela qual ele tambem nao se apaga.
		std::vector<int> ids;

		for (int i = 0; i < count; ++i)
		{
			const int id = (int)sel[(std::size_t)i].Get();
			const SoundCueNode* n = m_Cue->FindNode(id);

			if (!n || n->Type == SoundCueNodeType::Output)
				continue;

			ids.push_back(id);
		}

		if (ids.empty())
			return;

		m_Clipboard.Nodes.clear();
		m_Clipboard.Links.clear();

		// Ancora: canto superior esquerdo do conjunto. E o que preserva o
		// arranjo relativo — colar tres nos empilhados tem que devolver tres
		// nos empilhados.
		float minX = m_Cue->FindNode(ids.front())->EditorPos.x;
		float minY = m_Cue->FindNode(ids.front())->EditorPos.y;

		for (int id : ids)
		{
			const SoundCueNode* n = m_Cue->FindNode(id);
			minX = std::min(minX, n->EditorPos.x);
			minY = std::min(minY, n->EditorPos.y);
		}

		m_Clipboard.AnchorX = minX;
		m_Clipboard.AnchorY = minY;

		for (int id : ids)
		{
			const SoundCueNode* n = m_Cue->FindNode(id);

			Clipboard::Entry e;
			e.Node = *n;                       // COPIA, nao referencia
			e.Node._LastPick = -1;             // estado de runtime nao viaja
			e.DX = n->EditorPos.x - minX;
			e.DY = n->EditorPos.y - minY;

			m_Clipboard.Nodes.push_back(e);
		}

		// So os links INTERNOS ao conjunto. Um fio que sai pra um no que ficou
		// de fora nao tem como ser recriado — a outra ponta nao existe na
		// colagem.
		auto indexOf = [&](int id) -> int
			{
				for (std::size_t i = 0; i < ids.size(); ++i)
					if (ids[i] == id) return (int)i;

				return -1;
			};

		for (const auto& l : m_Cue->GetLinks())
		{
			const int a = indexOf(l.FromNodeId);
			const int b = indexOf(l.ToNodeId);

			if (a >= 0 && b >= 0)
				m_Clipboard.Links.push_back({ a, b });
		}

		if (!cut)
			return;

		// ── Recortar ─────────────────────────────────────────────────────
		auto& nodes = m_Cue->GetNodes();
		auto& links = m_Cue->GetLinks();

		for (int id : ids)
		{
			links.erase(std::remove_if(links.begin(), links.end(),
				[id](const SoundCueLink& l)
				{ return l.FromNodeId == id || l.ToNodeId == id; }), links.end());

			nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
				[id](const SoundCueNode& n) { return n.Id == id; }), nodes.end());
		}

		m_SelectedNode = 0;
		MarkEdited("Recortar");
	}

	void SoundCueEditorWindow::PasteNodes(const ImVec2& canvasPos)
	{
		if (!m_Cue || !m_Ed || m_Clipboard.Nodes.empty())
			return;

		// Mesma defesa do SpawnWavePlayers: toda funcao que chama
		// ed::SetNodePosition tem que garantir o contexto, porque a falha nao
		// e um no fora do lugar — e um crash.
		ed::SetCurrentEditor(m_Ed);

		// Id novo de cada entrada, na mesma ordem, pra remontar os fios.
		std::vector<int> fresh(m_Clipboard.Nodes.size(), -1);

		for (std::size_t i = 0; i < m_Clipboard.Nodes.size(); ++i)
		{
			// COPIA DA COPIA: o clipboard guarda o MOLDE. Consumi-lo aqui
			// esvaziaria a area apos a primeira colagem — e colar duas vezes
			// e justamente o caso de uso.
			SoundCueNode n = m_Clipboard.Nodes[i].Node;

			n.Id = m_Cue->AllocId();
			n.EditorPos = { canvasPos.x + m_Clipboard.Nodes[i].DX,
							canvasPos.y + m_Clipboard.Nodes[i].DY };

			m_Cue->GetNodes().push_back(n);

			// O node-editor guarda a posicao no PROPRIO estado; sem isto o no
			// nasceria em (0,0) e so iria pro lugar certo na proxima abertura.
			ed::SetNodePosition(n.Id, ImVec2(n.EditorPos.x, n.EditorPos.y));

			fresh[i] = n.Id;
			m_SelectedNode = n.Id;
		}

		for (const auto& l : m_Clipboard.Links)
		{
			if (l.From < 0 || l.To < 0)
				continue;

			SoundCueLink link;
			link.Id = m_Cue->AllocId();
			link.FromNodeId = fresh[(std::size_t)l.From];
			link.ToNodeId = fresh[(std::size_t)l.To];

			m_Cue->GetLinks().push_back(link);
		}

		MarkEdited("Colar");
	}

	void SoundCueEditorWindow::DrawNode(SoundCueNode& node)
	{
		const int  children = ChildCountOf(node.Id);
		const int  inputs = InputCountOf(node);
		const bool hasOut = (node.Type != SoundCueNodeType::Output);

		const auto lines = BodyLines(node, children);

		// ── MEDIR ANTES DE DESENHAR ──────────────────────────────────────
		//
		// A coluna da direita precisa encostar na BORDA do no, como na
		// Unreal. Pra isso e preciso saber a largura antes de posicionar
		// qualquer coisa — por isso medimos tudo primeiro.
		const float gapCols = 26.0f;

		float bodyW = 0.0f;

		for (const auto& s : lines)
			bodyW = std::max(bodyW, ImGui::CalcTextSize(s.c_str()).x);

		const float pinsW = (inputs > 0 ? kPinIcon : 0.0f)
			+ (hasOut ? kPinIcon : 0.0f)
			+ ((inputs > 0 && hasOut) ? gapCols : 0.0f);

		const float titleW = ImGui::CalcTextSize(TypeName(node.Type)).x;
		const float nodeW = std::max(std::max(std::max(bodyW, titleW), pinsW), kMinNodeW);

		const float rowH = std::max(kPinIcon, ImGui::GetFrameHeight()) + 3.0f;

		ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.11f, 0.115f, 0.135f, 0.97f));
		ed::PushStyleColor(ed::StyleColor_NodeBorder,
			m_SelectedNode == node.Id ? kSelected : ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

		ed::BeginNode(node.Id);

		// ed::BeginNode NAO empurra um ID do ImGui — so o node-editor sabe que
		// mudou de no. Sem este PushID, widgets de mesmo rotulo em nos
		// diferentes colidiriam e arrastar um moveria os dois.
		ImGui::PushID(node.Id);

		const ImVec2 origin = ImGui::GetCursorScreenPos();

		// ── Cabecalho: faixa cheia, de borda a borda ─────────────────────
		{
			const float hh = ImGui::GetTextLineHeight() + 8.0f;

			ImGui::GetWindowDrawList()->AddRectFilled(
				ImVec2(origin.x - 8.0f, origin.y - 4.0f),
				ImVec2(origin.x + nodeW + 8.0f, origin.y + hh - 4.0f),
				ImGui::ColorConvertFloat4ToU32(TypeColor(node.Type)),
				5.0f, ImDrawFlags_RoundCornersTop);

			ImGui::SetCursorScreenPos(origin);
			ImGui::TextUnformatted(TypeName(node.Type));

			// Reserva a largura de uma vez, e ALTURA extra de proposito: o
			// cabecalho e a unica faixa sem pino nenhum, entao e por onde se
			// arrasta o no. Uma faixa fina faz o clique cair no primeiro pino
			// e o editor comeca a puxar um FIO em vez de mover.
			ImGui::Dummy(ImVec2(nodeW, hh - ImGui::GetTextLineHeight() + 6.0f));
		}

		// ── Uma linha por pino ───────────────────────────────────────────
		const int rows = std::max(inputs, hasOut ? 1 : 0);

		for (int r = 0; r < rows; ++r)
		{
			const ImVec2 rp = ImGui::GetCursorScreenPos();

			if (r < inputs)
			{
				ed::BeginPin(MakePinId(node.Id, 1 + r), ed::PinKind::Input);
				PinIcon(InputIsLinked(node.Id, r));
				ed::EndPin();
			}

			if (r == 0 && hasOut)
			{
				ImGui::SetCursorScreenPos(ImVec2(rp.x + nodeW - kPinIcon, rp.y));

				ed::BeginPin(MakePinId(node.Id, 0), ed::PinKind::Output);
				PinIcon(OutputIsLinked(node.Id));
				ed::EndPin();
			}

			ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH));
		}

		// ── Corpo ────────────────────────────────────────────────────────
		for (const auto& s : lines)
			ImGui::TextDisabled("%s", s.c_str());

		// Forma de onda dentro do proprio no.
		//
		// E o que permite achar a variacao certa de relance, sem ler nome de
		// arquivo: num Random com cinco passos, "Step4_1" e "Step4_2" sao
		// indistinguiveis como texto e obvios como desenho.
		if (node.Type == SoundCueNodeType::WavePlayer)
		{
			if (auto clip = WaveClip(node.WaveUUID))
			{
				// Cursor so quando ESTE som e o que esta tocando no preview:
				// desenhar um cursor parado em todos os nos sugeriria que
				// todos estao tocando.
				float head = -1.0f;

				if (m_PreviewVoice != InvalidVoice
					&& m_PreviewNode == node.Id
					&& clip->GetDuration() > 0.0f)
				{
					const float cur = AudioEngine::GetVoiceCursorSeconds(m_PreviewVoice);

					if (cur >= 0.0f)
						head = std::min(1.0f, cur / clip->GetDuration());
				}

				ui::Waveform("wf", clip->GetPeaks(), ImVec2(nodeW, 26.0f),
					ImVec4(0.55f, 0.78f, 0.95f, 0.9f), head);
			}
		}

		ImGui::PopID();
		ed::EndNode();

		ed::PopStyleColor(2);
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  Interacao
	// ═════════════════════════════════════════════════════════════════════════

	void SoundCueEditorWindow::HandleCreate()
	{
		if (ed::BeginCreate(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), 2.5f))
		{
			ed::PinId startId = 0, endId = 0;

			if (ed::QueryNewLink(&startId, &endId))
			{
				int a = (int)startId.Get();
				int b = (int)endId.Get();

				if (a && b)
				{
					// Normaliza: `a` sempre saida, `b` sempre entrada. O
					// usuario pode arrastar em qualquer direcao.
					if (PinIsOutput(b))
						std::swap(a, b);

					const int fromNode = PinNode(a);
					const int toNode = PinNode(b);
					const int toSlot = PinSlot(b) - 1;

					const char* why = nullptr;

					if (!PinIsOutput(a) || PinIsOutput(b))
						why = "ligue uma saida a uma entrada";
					else if (fromNode == toNode)
						why = "o no nao pode alimentar a si mesmo";
					else if (WouldCreateCycle(fromNode, toNode))
						// Barrado na CRIACAO, e nao so no Evaluate. A guarda
						// de profundidade do runtime evita o travamento;
						// barrar aqui evita que voce monte um grafo que nunca
						// vai tocar e so descubra no preview.
						why = "isso criaria um ciclo";
					else
					{
						const SoundCueNode* parent = m_Cue->FindNode(toNode);

						if (!parent)
							why = "no invalido";
						else if (parent->Type != SoundCueNodeType::Random
							&& ChildCountOf(toNode) > 0)
							// So o Random aceita varios filhos. Sobrescrever
							// em silencio faria voce perder a ligacao
							// anterior sem perceber.
							why = "esta entrada ja esta ocupada";
					}

					if (why)
					{
						ed::RejectNewItem(ImVec4(0.90f, 0.35f, 0.35f, 1.0f), 2.5f);

						// A mensagem flutua junto do cursor: recusar sem
						// dizer por que so parece um editor quebrado.
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
						ImGui::TextUnformatted(why);
					}
					else if (ed::AcceptNewItem(ImVec4(0.40f, 0.85f, 0.45f, 1.0f), 3.0f))
					{
						SoundCueLink link;
						link.Id = m_Cue->AllocId();
						link.FromNodeId = fromNode;
						link.ToNodeId = toNode;
						link.ToInputIndex = std::max(0, toSlot);

						m_Cue->GetLinks().push_back(link);
						MarkEdited("Ligar");
					}
				}
			}
		}

		ed::EndCreate();
	}

	void SoundCueEditorWindow::HandleDelete()
	{
		if (ed::BeginDelete())
		{
			ed::LinkId deletedLink;

			while (ed::QueryDeletedLink(&deletedLink))
			{
				if (!ed::AcceptDeletedItem())
					continue;

				const int id = (int)deletedLink.Get();
				auto& links = m_Cue->GetLinks();

				links.erase(std::remove_if(links.begin(), links.end(),
					[id](const SoundCueLink& l) { return l.Id == id; }), links.end());

				MarkEdited("Desligar");
			}

			ed::NodeId deletedNode;

			while (ed::QueryDeletedNode(&deletedNode))
			{
				const int id = (int)deletedNode.Get();
				const SoundCueNode* n = m_Cue->FindNode(id);

				// O Output nasce com o cue e nao pode ser apagado: sem ele o
				// grafo nao tem raiz e o Evaluate nao tem por onde comecar.
				if (n && n->Type == SoundCueNodeType::Output)
				{
					ed::RejectDeletedItem();
					continue;
				}

				if (!ed::AcceptDeletedItem())
					continue;

				auto& nodes = m_Cue->GetNodes();
				auto& links = m_Cue->GetLinks();

				// Links orfaos morrem junto. Deixa-los apontando pra um no
				// inexistente faria o Evaluate falhar em silencio.
				links.erase(std::remove_if(links.begin(), links.end(),
					[id](const SoundCueLink& l)
					{ return l.FromNodeId == id || l.ToNodeId == id; }), links.end());

				nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
					[id](const SoundCueNode& n2) { return n2.Id == id; }), nodes.end());

				if (m_SelectedNode == id)
					m_SelectedNode = 0;

				MarkEdited("Apagar no");
			}
		}

		ed::EndDelete();
	}

	void SoundCueEditorWindow::HandleContextMenus()
	{
		ed::Suspend();

		if (ed::ShowBackgroundContextMenu())
		{
			// Posicao capturada AQUI, no momento do clique: quando o popup
			// abre, o mouse ja se moveu pro item de menu, e o no nasceria
			// debaixo do cursor em vez de onde voce clicou.
			m_MenuCanvasPos = ed::ScreenToCanvas(ImGui::GetMousePos());
			ImGui::OpenPopup("CueCreateNode");
		}

		if (ImGui::BeginPopup("CueCreateNode"))
		{
			ImGui::TextDisabled("Criar no");
			ImGui::Separator();

			struct Entry { SoundCueNodeType Type; const char* Hint; };

			static const Entry entries[] = {
				{ SoundCueNodeType::WavePlayer,  "um arquivo de som" },
				{ SoundCueNodeType::Random,      "sorteia uma das entradas" },
				{ SoundCueNodeType::Modulator,   "varia volume e pitch" },
				{ SoundCueNodeType::Attenuation, "sobrescreve a distancia 3D" },
			};

			for (const auto& e : entries)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, TypeColor(e.Type));
				const bool clicked = ImGui::MenuItem(TypeName(e.Type));
				ImGui::PopStyleColor();

				ImGui::SameLine();
				ImGui::TextDisabled("  %s", e.Hint);

				if (clicked)
					SpawnNode(e.Type, m_MenuCanvasPos);
			}

			ImGui::EndPopup();
		}

		ed::Resume();
	}

	void SoundCueEditorWindow::SpawnNode(SoundCueNodeType type, const ImVec2& canvasPos)
	{
		if (!m_Cue || !m_Ed)
			return;

		ed::SetCurrentEditor(m_Ed);

		SoundCueNode n;
		n.Id = m_Cue->AllocId();
		n.Type = type;
		n.EditorPos = { canvasPos.x, canvasPos.y };

		if (type == SoundCueNodeType::Modulator)
		{
			// Nasce com faixa util. Um Modulator neutro (1.0–1.0) e um no que
			// nao faz nada, e voce teria que descobrir sozinho que precisa
			// mexer nos quatro campos pra ele servir pra alguma coisa.
			n.VolumeMin = 0.9f; n.VolumeMax = 1.0f;
			n.PitchMin = 0.95f; n.PitchMax = 1.05f;
		}

		m_Cue->GetNodes().push_back(n);
		ed::SetNodePosition(n.Id, canvasPos);

		m_SelectedNode = n.Id;
		MarkEdited("Criar no");
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  Details
	// ═════════════════════════════════════════════════════════════════════════

	void SoundCueEditorWindow::DrawDetailsPanel()
	{
		SoundCueNode* node = m_Cue->FindNode(m_SelectedNode);

		if (!node)
		{
			ImGui::TextDisabled("Nenhum no selecionado.");
			ImGui::Separator();
			ImGui::TextWrapped("Botao direito no grafo cria nos. Ligue tudo ate o "
				"Output e use o Play da barra para ouvir.");
			return;
		}

		// Icone por papel do no, do mesmo conjunto que o resto do editor usa.
		const char* icon =
			node->Type == SoundCueNodeType::WavePlayer ? ICON_FILE :
			node->Type == SoundCueNodeType::Random ? ICON_CIRCLE_NODES :
			node->Type == SoundCueNodeType::Modulator ? ICON_SLIDERS :
			node->Type == SoundCueNodeType::Attenuation ? ICON_ARROWS_LEFT_RIGHT :
			ICON_BOLT;

		ui::SectionHeader(icon, TypeName(node->Type), ui::Accent::Neutral);
		ImGui::Spacing();

		switch (node->Type)
		{
		case SoundCueNodeType::Output:
		{
			ImGui::TextWrapped("Raiz do cue: o que estiver ligado aqui e o que toca. "
				"Nao pode ser apagado.");

			ImGui::Spacing();
			ImGui::Separator();

			// A categoria e propriedade do CUE INTEIRO, e nao de um no —
			// mora no Output porque ele e a raiz, o unico lugar onde
			// "propriedade do cue" nao se confunde com "propriedade de uma
			// ramificacao".
			int cat = (int)m_Cue->GetCategory();
			const char* names[(int)SoundCategory::Count];

			for (int i = 0; i < (int)SoundCategory::Count; ++i)
				names[i] = SoundCategoryToString((SoundCategory)i);

			if (ImGui::Combo("Categoria", &cat, names, (int)SoundCategory::Count))
			{
				m_Cue->SetCategory((SoundCategory)cat);
				MarkEdited("Categoria");
			}

			ImGui::TextDisabled("Cor do pulso na visualizacao de som.");

			ImGui::Spacing();

			int bus = (int)m_Cue->GetBus();
			const char* buses[(int)AudioBus::Count];

			for (int i = 0; i < (int)AudioBus::Count; ++i)
				buses[i] = AudioBusToString((AudioBus)i);

			if (ImGui::Combo("Bus", &bus, buses, (int)AudioBus::Count))
			{
				m_Cue->SetBus((AudioBus)bus);
				MarkEdited("Bus");
			}

			ImGui::TextDisabled("Grupo de mixagem. Sobrescreve o do Audio Source.");

			ImGui::Spacing();

			float prio = m_Cue->GetPriority();

			if (ImGui::SliderFloat("Prioridade", &prio, 0.0f, 1.0f, "%.2f"))
			{
				m_Cue->SetPriority(prio);
				MarkEdited("Prioridade");
			}

			ImGui::TextDisabled("0 = pode ser cortado quando o teto de vozes\n"
				"estourar. 1 = nunca cede lugar.");
			break;
		}

		case SoundCueNodeType::WavePlayer:
		{
			std::string uuid = node->WaveUUID;

			if (AssetPicker::Draw("Som", uuid, { AssetType::Audio },
				[&](const AssetRecord& rec) { node->WaveUUID = rec.UUID; MarkEdited("Trocar som"); }))
			{
				node->WaveUUID = uuid;
				MarkEdited("Trocar som");
			}

			ImGui::Spacing();

			// So Audio no filtro, nunca SoundCue: cue dentro de cue e
			// recursao que o grafo nao modela — o link e entre NOS, nao entre
			// assets. Se um dia fizer sentido, e um no proprio.
			ImGui::TextDisabled("Apenas .wav / .mp3 / .flac.");

			if (auto clip = WaveClip(node->WaveUUID))
			{
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				float head = -1.0f;
				const bool playing = m_PreviewVoice != InvalidVoice
					&& m_PreviewNode == node->Id
					&& AudioEngine::IsPlaying(m_PreviewVoice);

				if (playing && clip->GetDuration() > 0.0f)
				{
					const float cur = AudioEngine::GetVoiceCursorSeconds(m_PreviewVoice);

					if (cur >= 0.0f)
						head = std::min(1.0f, cur / clip->GetDuration());
				}

				ui::Waveform("wf_big", clip->GetPeaks(),
					ImVec2(ImGui::GetContentRegionAvail().x, 64.0f),
					ImVec4(0.55f, 0.78f, 0.95f, 0.95f), head);

				ImGui::Spacing();

				// Ouve ESTE som cru, sem passar pelo grafo.
				//
				// Diferente do Play da barra: aquele avalia o cue inteiro e
				// sorteia. Aqui voce quer conferir um arquivo especifico, e
				// sorteio atrapalharia.
				if (ui::IconButton(playing ? ICON_STOP : ICON_PLAY,
					playing ? "Parar" : "Ouvir so este som (sem o cue)"))
				{
					if (playing)
					{
						AudioEngine::Stop(m_PreviewVoice);
						m_PreviewVoice = InvalidVoice;
					}
					else
					{
						if (m_PreviewVoice != InvalidVoice)
							AudioEngine::Stop(m_PreviewVoice);

						m_PreviewVoice = AudioEngine::PlayOneShot(node->WaveUUID);
						m_PreviewNode = node->Id;
					}
				}

				ImGui::SameLine();
				ImGui::TextDisabled("%.2fs   %u ch   %u Hz",
					clip->GetDuration(), clip->GetChannels(), clip->GetSampleRate());
			}
			break;
		}

		case SoundCueNodeType::Random:
		{
			if (ImGui::Checkbox("Nao repetir o anterior", &node->NoRepeat))
				MarkEdited("Random");

			ImGui::TextDisabled("Sorteio puro repete o suficiente\npara o ouvido notar.");
			ImGui::Spacing();
			ImGui::Separator();

			const std::vector<int> children = ChildrenOf(node->Id);

			if (children.empty())
			{
				ImGui::TextDisabled("Nenhuma variacao ligada.");
				ImGui::TextWrapped("Arraste a saida de um Wave Player para a "
					"entrada vaga deste no.");
				break;
			}

			// Pesos acompanham os filhos: um link removido nao pode deixar um
			// peso orfao deslocando todos os outros.
			if ((int)node->Weights.size() != (int)children.size())
				node->Weights.resize(children.size(), 1.0f);

			float total = 0.0f;
			for (float w : node->Weights)
				total += std::max(0.0f, w);

			ImGui::TextDisabled("Pesos");

			for (size_t i = 0; i < children.size(); ++i)
			{
				const SoundCueNode* child = m_Cue->FindNode(children[i]);

				const std::string label = child
					? (child->Type == SoundCueNodeType::WavePlayer
						? WaveLabel(child->WaveUUID)
						: TypeName(child->Type))
					: "?";

				ImGui::PushID((int)i);
				ImGui::SetNextItemWidth(80.0f);

				if (ImGui::DragFloat("##w", &node->Weights[i], 0.05f, 0.0f, 100.0f, "%.2f"))
					MarkEdited("Peso");

				ImGui::SameLine();

				// A chance efetiva vale mais que o peso cru: "2.0" nao diz
				// nada sozinho, "40%" diz.
				if (total > 0.0f)
					ImGui::Text("%s  (%.0f%%)", label.c_str(),
						100.0f * std::max(0.0f, node->Weights[i]) / total);
				else
					ImGui::TextUnformatted(label.c_str());

				ImGui::PopID();
			}
			break;
		}

		case SoundCueNodeType::Modulator:
		{
			ImGui::TextDisabled("Volume");
			if (ImGui::DragFloat("Min##vol", &node->VolumeMin, 0.01f, 0.0f, 4.0f, "%.2f")) MarkEdited("Modulator");
			if (ImGui::DragFloat("Max##vol", &node->VolumeMax, 0.01f, 0.0f, 4.0f, "%.2f")) MarkEdited("Modulator");

			ImGui::Spacing();
			ImGui::TextDisabled("Pitch");
			if (ImGui::DragFloat("Min##pit", &node->PitchMin, 0.01f, 0.05f, 4.0f, "%.2f")) MarkEdited("Modulator");
			if (ImGui::DragFloat("Max##pit", &node->PitchMax, 0.01f, 0.05f, 4.0f, "%.2f")) MarkEdited("Modulator");

			// Max < Min nao e sorteio invertido, e faixa vazia: o sorteio
			// devolve o Min e o no vira constante em silencio.
			node->VolumeMax = std::max(node->VolumeMax, node->VolumeMin);
			node->PitchMax = std::max(node->PitchMax, node->PitchMin);

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextWrapped("Multiplica o que vem de baixo. Modulators aninhados "
				"compoem, e o volume do Audio Source continua valendo por cima.");
			break;
		}

		case SoundCueNodeType::Attenuation:
		{
			if (ImGui::Checkbox("3D", &node->Is3D)) MarkEdited("Attenuation");
			if (ImGui::DragFloat("Min Distance", &node->MinDistance, 0.1f, 0.01f, 10000.0f, "%.2f")) MarkEdited("Attenuation");
			if (ImGui::DragFloat("Max Distance", &node->MaxDistance, 0.5f, 0.02f, 100000.0f, "%.2f")) MarkEdited("Attenuation");

			node->MaxDistance = std::max(node->MaxDistance, node->MinDistance + 0.01f);

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextWrapped("Sobrescreve a atenuacao de QUEM tocar este cue — "
				"inclusive a do Audio Source.");
			break;
		}
		}
	}

} // namespace axe