#include "anim_graph_window.hpp"
#include "axe/log/log.hpp"
#include "axe/asset/asset_database.hpp"

#include <imgui_internal.h>   // DockBuilder — so pro layout padrao, uma vez

#include "editor/axe_editor/ui/editor_widgets.hpp"   // ui::IconButton, ui::Accent (AG3)
#include "editor/axe_editor/ui/editor_icons.hpp"     // ICON_* — Font Awesome subsetada (AG3)

#include <utilities/widgets.h>   // ax::Widgets::Icon — mesmo pino do Material/Script
#include <utilities/drawing.h>

#include <algorithm>
#include <cctype>
#include <cfloat>    // FLT_MAX — guarda de no ainda nao desenhado (AG3b)
#include <cmath>     // std::sqrt — geometria das setas de transicao
#include <iterator>  // std::size — contagem dos arrays do menu (AG1)
#include <set>
#include <cstdio>

namespace axe
{
	// Cores dos pinos. Um pino de pose e um pino de float precisam ser
	// distinguíveis SEM ler o rótulo — é o que impede você de arrastar a coisa
	// errada e ficar procurando por que "não conecta".
	static const ImVec4 kPoseColor{ 0.90f, 0.90f, 0.95f, 1.0f };   // branco
	static const ImVec4 kFloatColor{ 0.45f, 0.85f, 0.30f, 1.0f };  // verde
	static const ImVec4 kBoolColor{ 0.90f, 0.35f, 0.35f, 1.0f };   // vermelho

	static ImVec4 ColorFor(AnimPinType t)
	{
		switch (t)
		{
		case AnimPinType::Float: return kFloatColor;
		case AnimPinType::Bool:  return kBoolColor;
		default:                 return kPoseColor;
		}
	}


	// A cor do cabecalho diz o que o no FAZ antes de voce ler o nome dele.
	AnimGraphWindow::NodeStyle AnimGraphWindow::StyleFor(const AnimNode& node, bool isOutput)
	{
		if (isOutput)
			return { ImVec4(0.62f, 0.45f, 0.13f, 1.0f), "OUTPUT" };

		if (dynamic_cast<const AnimNode_StateMachine*>(&node))
			return { ImVec4(0.42f, 0.28f, 0.62f, 1.0f), "STATE MACHINE" };

		// AG4 — logica tem faixa propria.
		//
		// Sem isto todos cairiam no "VARIABLE" verde logo abaixo, por serem
		// nos de dado. Mas um Compare nao e uma variavel: ele DECIDE. A faixa
		// separada e o que permite bater o olho num grafo e ver onde estao as
		// decisoes.
		if (dynamic_cast<const AnimNode_Not*>(&node) ||
			dynamic_cast<const AnimNode_BoolOp*>(&node) ||
			dynamic_cast<const AnimNode_CompareFloat*>(&node) ||
			dynamic_cast<const AnimNode_SelectFloat*>(&node) ||
			dynamic_cast<const AnimNode_FloatMath*>(&node))
			return { ImVec4(0.58f, 0.44f, 0.16f, 1.0f), "LOGIC" };

		if (node.OutputType() != AnimPinType::Pose)
			return { ImVec4(0.24f, 0.52f, 0.24f, 1.0f), "VARIABLE" };

		if (dynamic_cast<const AnimNode_ClipPlayer*>(&node) ||
			dynamic_cast<const AnimNode_BlendSpacePlayer*>(&node))
			return { ImVec4(0.18f, 0.42f, 0.60f, 1.0f), "POSE SOURCE" };

		return { ImVec4(0.50f, 0.32f, 0.42f, 1.0f), "BLEND" };
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  DESENHO DOS NOS — ANIMGRAPH_STYLE_V2
	//
	//  Mesmo padrao visual do Material/Script editor (utilities/widgets.h):
	//  header colorido pintado no draw list, corpo escuro do proprio
	//  node-editor, pinos com ax::Widgets::Icon.
	//
	//  A versao anterior (StyledNode) pintava o fundo com ChannelsSplit DENTRO
	//  de ed::BeginNode — e o imgui-node-editor ja usa channel splitting
	//  internamente ali. O conflito engolia o fundo/header customizado e
	//  sobrava so o card minimo. Por isso ela foi removida: o caminho correto
	//  (e o que os outros dois editores ja usavam) e deixar o node-editor
	//  pintar corpo/borda e desenhar SO o header por cima, via
	//  GetWindowDrawList, dentro do proprio no.
	// ═════════════════════════════════════════════════════════════════════════

	static constexpr float kPinIconSize = 18.0f;

	// AG3 — o ponto do reroute. Menor que o pino normal de proposito: ele
	// tem que ler como um NO NO FIO, nao como um no minusculo.
	static constexpr float kReroutePinSize = 12.0f;

	// Pino no estilo Blueprint: SETA (Flow) para pose — o fluxo principal —
	// e CIRCULO para dado. Cheio = ligado, vazado = livre. Forma + cor:
	// legivel ate pra quem nao distingue as cores.
	// `size` default = kPinIconSize: todos os call-sites antigos seguem
	// identicos. So o reroute (AG3) passa outro valor.
	static void DrawPinIcon(const ImVec4& color, bool connected, bool isPose,
		float size = kPinIconSize)
	{
		ax::Widgets::Icon(
			ImVec2(size, size),
			isPose ? ax::Drawing::IconType::Flow : ax::Drawing::IconType::Circle,
			connected, color, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));
	}

	// Largura do card: adaptativa ao texto, com um minimo confortavel.
	static float NodeWidthFor(const char* title, const char* subtitle)
	{
		float w = ImGui::CalcTextSize(title).x;
		if (subtitle && *subtitle)
			w = std::max(w, ImGui::CalcTextSize(subtitle).x);
		return std::max(170.0f, w + 32.0f);
	}

	// Barra de titulo: retangulo com gradiente vertical e cantos superiores
	// arredondados, pintado no draw list da janela ANTES do conteudo (dentro
	// de um no, ordem de submissao = ordem de desenho, entao o texto fica por
	// cima). Avanca o cursor para o corpo.
	static void DrawNodeHeader(const char* title, const char* subtitle,
		const ImVec4& accent, float width)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();

		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  lineH = ImGui::GetTextLineHeight();
		const bool   hasSub = (subtitle && *subtitle);
		const float  headerH = lineH + (hasSub ? lineH + 3.0f : 0.0f) + 8.0f;

		// Paleta ja vem escura e dessaturada (estilo Unreal); o gradiente so
		// da um leve volume, sem virar neon.
		const ImU32 top = ImGui::ColorConvertFloat4ToU32(
			ImVec4(accent.x * 1.00f, accent.y * 1.00f, accent.z * 1.00f, 1.0f));
		const ImU32 bot = ImGui::ColorConvertFloat4ToU32(
			ImVec4(accent.x * 0.58f, accent.y * 0.58f, accent.z * 0.58f, 1.0f));

		// Tampa arredondada no topo (acompanha o canto do card) + gradiente
		// reto SO abaixo dela — assim nenhum pixel quadrado vaza o canto.
		dl->AddRectFilled(p, ImVec2(p.x + width, p.y + 5.0f),
			top, 5.0f, ImDrawFlags_RoundCornersTop);
		dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + 5.0f),
			ImVec2(p.x + width, p.y + headerH),
			top, top, bot, bot);
		dl->AddLine(ImVec2(p.x, p.y + headerH), ImVec2(p.x + width, p.y + headerH),
			ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.45f)), 1.0f);

		ImGui::SetCursorScreenPos(ImVec2(p.x + 10.0f, p.y + 4.0f));
		ImGui::TextUnformatted(title);

		if (hasSub)
		{
			ImGui::SetCursorScreenPos(ImVec2(p.x + 10.0f, p.y + 4.0f + lineH + 2.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.55f));
			ImGui::TextUnformatted(subtitle);
			ImGui::PopStyleColor();
		}

		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + headerH));
		ImGui::Dummy(ImVec2(width, 6.0f));
	}

	void AnimGraphWindow::MarkEdited(const char* action)
	{
		m_Dirty = true;
		++m_EditSerial;
		m_LastEditTime = ImGui::GetTime();

		// AG3b — anota a acao, mas NAO fecha o comando aqui.
		//
		// Um arrasto de no chama MarkEdited dezenas de vezes (o loop que grava
		// posicao roda todo frame). Se cada chamada virasse um passo de undo,
		// desfazer um movimento so exigiria trinta Ctrl+Z. O comando so fecha
		// quando o gesto termina — ver CommitPendingUndo.
		if (action && !m_PendingUndo)
		{
			m_PendingUndo = true;
			m_PendingUndoName = action;
		}
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  UNDO / REDO — por snapshot (AG3b)
	// ═════════════════════════════════════════════════════════════════════════

	AnimGraphWindow::AnimSnapshot AnimGraphWindow::CaptureState() const
	{
		AnimSnapshot snap;

		if (m_Asset)
		{
			snap.Parameters = m_Asset->GetParameters();

			// Copia PROFUNDA: AnimPoseGraph tem copy-ctor que clona os nos.
			// Uma copia rasa daria dois grafos apontando para os mesmos
			// unique_ptr — double free na primeira destruicao.
			snap.Root = m_Asset->GetRoot();
		}

		return snap;
	}

	void AnimGraphWindow::RestoreState(const AnimSnapshot& snap)
	{
		if (!m_Asset)
			return;

		m_Asset->GetParameters() = snap.Parameters;
		m_Asset->GetRoot() = snap.Root;
		m_Asset->GetRoot().Resolve();

		// ── Reconstroi a navegacao (AG3e) ────────────────────────────────────
		//
		// NavEntry guarda PONTEIROS crus para dentro do grafo raiz, e o
		// restore acabou de substituir esse grafo — todos viraram lixo.
		//
		// No AG3b isto era resolvido voltando para a raiz. Funcionava e era
		// seguro, mas na pratica e ruim: voce desce num estado, ajusta um no,
		// aperta Ctrl+Z e e cuspido para fora.
		//
		// Agora o caminho e REFEITO a partir das chaves estaveis
		// (SmNodeId / StateIndex / TransIndex), que sobreviveram porque
		// descrevem o caminho e nao o endereco.
		//
		// Se QUALQUER nivel nao for encontrado — o undo desfez justamente a
		// criacao daquele estado, por exemplo — paramos ali e ficamos no
		// nivel mais fundo que ainda existe. Nunca ha ponteiro invalido: cada
		// nivel so entra depois de ser resolvido no grafo NOVO.
		const std::vector<NavEntry> oldNav = m_Nav;

		m_Nav.clear();
		m_Nav.push_back({ "AnimGraph", &m_Asset->GetRoot(), nullptr });

		for (std::size_t i = 1; i < oldNav.size(); ++i)
		{
			const NavEntry& want = oldNav[i];
			const NavEntry& parent = m_Nav.back();

			// Nivel de STATE MACHINE: acha o no pelo Id no grafo do pai.
			if (want.SmNodeId >= 0 && want.StateIndex < 0 && want.TransIndex < 0)
			{
				if (!parent.Graph)
					break;

				auto* smNode = dynamic_cast<AnimNode_StateMachine*>(
					parent.Graph->FindNode(want.SmNodeId));

				if (!smNode)
					break;

				NavEntry e = want;
				e.Graph = nullptr;
				e.Sm = smNode;
				m_Nav.push_back(e);
				continue;
			}

			// Nivel de ESTADO: sub-grafo de parent.Sm->States[StateIndex].
			if (want.StateIndex >= 0)
			{
				if (!parent.Sm || want.StateIndex >= (int)parent.Sm->States.size())
					break;

				NavEntry e = want;
				e.Graph = &parent.Sm->States[want.StateIndex].Graph;
				e.Sm = nullptr;
				m_Nav.push_back(e);
				continue;
			}

			// Nivel de REGRA: pertence a MESMA state machine do pai.
			if (want.TransIndex >= 0)
			{
				if (!parent.Sm || want.TransIndex >= (int)parent.Sm->Transitions.size())
					break;

				NavEntry e = want;
				e.Graph = nullptr;
				e.Sm = parent.Sm;
				m_Nav.push_back(e);
				continue;
			}

			break;
		}

		m_SelectedNode = -1;
		m_SelectedState = -1;
		m_SelectedTransition = -1;
		m_SelectedCondition = -1;

		// AG3e — SEM ISTO OS NOS EMPILHAM NA ORIGEM.
		//
		// O contexto do node-editor vai ser recriado logo abaixo, e um
		// contexto novo nao conhece posicao de no nenhuma. Quem reaplica
		// EditorX/EditorY e o bloco guardado por `m_PositionsLoaded`, no
		// inicio de cada canvas — e ele so roda quando a flag esta em false.
		//
		// Sem zerar aqui, o contexto novo desenhava tudo em (0,0): era o
		// "ficou tudo empilhado" depois do Ctrl+Z.
		m_PositionsLoaded = false;

		// ── Recriar o contexto SO quando o nivel mudou (AG3f) ────────────────
		//
		// Destruir e recriar o contexto custa um frame ate as posicoes serem
		// reaplicadas — e esse frame e a PISCADA que aparecia a cada Ctrl+Z.
		//
		// A justificativa original ("os Ids mudaram de significado") nao vale
		// para o caso comum do undo: o snapshot PRESERVA os Ids, e se a
		// navegacao foi reconstruida no mesmo nivel, e o mesmo grafo — cada Id
		// significa exatamente o que significava antes.
		//
		// Onde recriar continua sendo necessario: quando o nivel final ficou
		// DIFERENTE do original (um nivel sumiu e subimos). Ai sim os Ids
		// mudam de significado, porque eles sao por grafo — o no 1 da raiz e o
		// no 1 de um sub-grafo sao nos distintos com o mesmo numero, e o
		// contexto guardaria a posicao de um para o outro.
		//
		// Via FLAG, e nao destruindo aqui: o restore e disparado de dentro do
		// frame (atalho lido durante o desenho), e destruir o contexto no meio
		// da passada do ImGui deixaria o resto do frame desenhando num
		// ponteiro morto.
		bool sameLevel = (m_Nav.size() == oldNav.size());

		if (sameLevel)
		{
			for (std::size_t i = 0; i < m_Nav.size(); ++i)
			{
				if (m_Nav[i].SmNodeId != oldNav[i].SmNodeId
					|| m_Nav[i].StateIndex != oldNav[i].StateIndex
					|| m_Nav[i].TransIndex != oldNav[i].TransIndex)
				{
					sameLevel = false;
					break;
				}
			}
		}

		if (!sameLevel)
			m_NeedsContextReset = true;

		m_Dirty = true;
		++m_EditSerial;
		m_LastEditTime = ImGui::GetTime();
	}

	void AnimGraphWindow::CommitPendingUndo()
	{
		if (!m_PendingUndo)
			return;

		// So fecha quando o gesto ACABOU. Sem esta espera, arrastar um no
		// geraria um comando por frame.
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive())
			return;

		m_PendingUndo = false;

		if (!m_Asset)
			return;

		// O "antes" e o baseline (estado no fim do comando anterior); o
		// "depois" e agora. Guardar um por comando em vez de dois.
		auto before = m_Baseline;
		auto after = std::make_shared<AnimSnapshot>(CaptureState());

		if (!before)
		{
			// Primeira edicao da sessao: nao ha antes para voltar.
			m_Baseline = after;
			return;
		}

		const std::string name = m_PendingUndoName;

		// ── Por que o flag `skipFirst` ───────────────────────────────────────
		//
		// `CommandHistory::Push` EXECUTA o Execute do comando, e `Redo`
		// tambem. Mas neste ponto o estado do editor JA E o "depois" — a
		// edicao acabou de acontecer. Deixar o Push restaurar seria inofensivo
		// para os dados e visivel para o usuario: RestoreState reseta a
		// navegacao para a raiz e recria o contexto do node-editor. Voce
		// seria jogado para fora do sub-grafo a cada acao.
		//
		// `ReplaceTop` nao resolve — ele tambem chama Execute.
		//
		// Entao o Execute engole a PRIMEIRA chamada (a do Push) e passa a
		// valer da segunda em diante, que e o Redo de verdade.
		auto skipFirst = std::make_shared<bool>(true);

		Command cmd;
		cmd.Name = name;

		cmd.Execute = [this, after, skipFirst]()
			{
				if (*skipFirst) { *skipFirst = false; return; }
				RestoreState(*after);
				m_Baseline = after;
			};

		cmd.Undo = [this, before]()
			{
				RestoreState(*before);
				m_Baseline = before;
			};

		m_History.Push(cmd);

		m_Baseline = after;
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  CLIPBOARD — copiar / recortar / colar / duplicar (AG3c)
	// ═════════════════════════════════════════════════════════════════════════

	void AnimGraphWindow::CopySelection(bool cut)
	{
		if (m_Nav.empty() || !m_Nav.back().Graph || !m_EdCtx)
			return;

		AnimPoseGraph& graph = *m_Nav.back().Graph;

		// ── AG3d: o contexto do node-editor precisa estar ATIVO aqui ─────────
		//
		// Esta funcao roda no fim do frame, e a essa altura o
		// DrawPoseGraphCanvas ja chamou ed::SetCurrentEditor(nullptr).
		// GetSelectedObjectCount le a lista de selecao DO CONTEXTO — sem ele,
		// desreferencia nulo e o programa morre com access violation. Foi
		// exatamente o crash do Ctrl+X.
		//
		// Religar e valido: as funcoes de consulta e SetNodePosition nao
		// exigem estar entre Begin/End, so exigem um contexto corrente.
		// Desligamos no fim para nao deixar estado pendurado para quem vier
		// depois nesta mesma passada.
		// AG5 — SALVA e RESTAURA, em vez de zerar.
		//
		// A versao do AG3d zerava o contexto na saida, e isso bastava enquanto
		// esta funcao so era chamada do fim do frame, com o contexto ja nulo.
		//
		// Agora ela tambem e chamada do item "Paste" da paleta — que roda
		// DENTRO de ed::Suspend()/Resume(), com o contexto ATIVO. Zerar ali
		// deixaria o ed::Resume() e o ed::End() seguintes sem contexto.
		struct CtxGuard
		{
			ed::EditorContext* Prev;
			explicit CtxGuard(ed::EditorContext* next)
				: Prev(ed::GetCurrentEditor()) {
				ed::SetCurrentEditor(next);
			}
			~CtxGuard() { ed::SetCurrentEditor(Prev); }
		} ctxGuard(m_EdCtx);

		// GetSelectedObjectCount conta nos E fios; GetSelectedNodes devolve
		// quantos eram nos de fato. Dimensionamos pelo total e confiamos no
		// retorno — um vetor curto seria escrita fora dos limites. Mesmo
		// cuidado do Control Rig (rig_graph_canvas.cpp), e pela mesma razao:
		// um array fixo aqui e um estouro esperando por um grafo grande.
		const int total = ed::GetSelectedObjectCount();

		if (total <= 0)
			return;

		std::vector<ed::NodeId> ids((std::size_t)total);
		const int count = ed::GetSelectedNodes(ids.data(), total);

		if (count <= 0)
			return;

		std::vector<int> picked;
		picked.reserve(count);

		m_Clipboard.Nodes.clear();
		m_Clipboard.Links.clear();

		for (int i = 0; i < count; ++i)
		{
			const int id = (int)ids[i].Get();

			// O Output NUNCA vai junto. Existe exatamente um por grafo, e um
			// segundo colado deixaria o grafo com duas saidas e nenhuma regra
			// para decidir qual vale.
			if (id == graph.GetOutputNode())
				continue;

			AnimNode* n = graph.FindNode(id);
			if (!n)
				continue;

			picked.push_back(id);
			m_Clipboard.Nodes.push_back(n->Clone());

			// O Clone PRESERVA o Id (CopyCommonTo copia `dst.Id = Id`), mas
			// isso nao ajuda: AnimPoseGraph::AddNode sobrescreve com
			// `m_NextId++`. E melhor assim — colar preservando o Id daria dois
			// nos com o mesmo Id no mesmo grafo, e FindNode devolveria o
			// primeiro que achasse.
			//
			// Por isso `picked` guarda os ids antigos na MESMA ordem dos
			// clones, e o Paste casa por indice para religar os fios.
		}

		if (m_Clipboard.Nodes.empty())
			return;

		// Links INTERNOS ao recorte. Um fio que sai da selecao para fora nao
		// vem junto: colar recriaria uma ligacao para um no que o usuario nao
		// copiou, e o resultado seria um grafo que ele nao pediu.
		auto inSelection = [&picked](int id)
			{
				return std::find(picked.begin(), picked.end(), id) != picked.end();
			};

		for (const AnimLink& l : graph.GetLinks())
		{
			if (inSelection(l.FromNode) && inSelection(l.ToNode))
				m_Clipboard.Links.push_back(l);
		}

		// Os ids antigos viajam junto com o clipboard, na mesma ordem dos
		// clones. Guardados aqui para o Paste remapear.
		m_ClipboardIds = picked;

		if (cut)
		{
			for (int id : picked)
				graph.RemoveNode(id);

			m_SelectedNode = -1;
			MarkEdited("Cut");
		}
	}

	void AnimGraphWindow::PasteClipboard(const ImVec2* at)
	{
		if (m_Clipboard.Nodes.empty() || m_Nav.empty() || !m_Nav.back().Graph || !m_EdCtx)
			return;

		AnimPoseGraph& graph = *m_Nav.back().Graph;

		// Mesmo motivo do CopySelection: ed::SetNodePosition tambem precisa de
		// contexto corrente. Ver a nota la.
		// AG5 — SALVA e RESTAURA, em vez de zerar.
		//
		// A versao do AG3d zerava o contexto na saida, e isso bastava enquanto
		// esta funcao so era chamada do fim do frame, com o contexto ja nulo.
		//
		// Agora ela tambem e chamada do item "Paste" da paleta — que roda
		// DENTRO de ed::Suspend()/Resume(), com o contexto ATIVO. Zerar ali
		// deixaria o ed::Resume() e o ed::End() seguintes sem contexto.
		struct CtxGuard
		{
			ed::EditorContext* Prev;
			explicit CtxGuard(ed::EditorContext* next)
				: Prev(ed::GetCurrentEditor()) {
				ed::SetCurrentEditor(next);
			}
			~CtxGuard() { ed::SetCurrentEditor(Prev); }
		} ctxGuard(m_EdCtx);

		// Onde colar: sob o cursor, com o recorte inteiro deslocado em bloco.
		//
		// Ancoramos no no mais acima-a-esquerda do recorte e movemos todos
		// pelo mesmo delta — colar empilhando tudo no mesmo ponto destruiria
		// o arranjo que a pessoa acabou de copiar.
		float minX = FLT_MAX, minY = FLT_MAX;

		for (const auto& n : m_Clipboard.Nodes)
		{
			minX = std::min(minX, n->EditorX);
			minY = std::min(minY, n->EditorY);
		}

		const ImVec2 target = at ? *at : m_LastCanvasMousePos;

		const float dx = target.x - minX;
		const float dy = target.y - minY;

		// old id -> new id, para religar os fios internos.
		std::vector<std::pair<int, int>> remap;
		remap.reserve(m_Clipboard.Nodes.size());

		for (std::size_t i = 0; i < m_Clipboard.Nodes.size(); ++i)
		{
			// Clona DE NOVO, a partir do clipboard: sem isto, o primeiro
			// Paste esvaziaria o clipboard (os unique_ptr seriam movidos) e um
			// segundo Ctrl+V nao colaria nada.
			auto copy = m_Clipboard.Nodes[i]->Clone();

			copy->EditorX = m_Clipboard.Nodes[i]->EditorX + dx;
			copy->EditorY = m_Clipboard.Nodes[i]->EditorY + dy;

			const ImVec2 pos(copy->EditorX, copy->EditorY);

			const int newId = graph.AddNode(std::move(copy));

			ed::SetNodePosition(newId, pos);

			if (i < m_ClipboardIds.size())
				remap.emplace_back(m_ClipboardIds[i], newId);
		}

		auto mapId = [&remap](int oldId) -> int
			{
				for (const auto& p : remap)
					if (p.first == oldId) return p.second;
				return -1;
			};

		for (const AnimLink& l : m_Clipboard.Links)
		{
			const int from = mapId(l.FromNode);
			const int to = mapId(l.ToNode);

			if (from >= 0 && to >= 0)
				graph.AddLink(from, to, l.ToPin, l.Kind);
		}

		graph.Resolve();
		MarkEdited("Paste");
	}

	void AnimGraphWindow::DoUndo()
	{
		if (!m_History.CanUndo())
			return;

		m_History.Undo();
	}

	void AnimGraphWindow::DoRedo()
	{
		if (!m_History.CanRedo())
			return;

		m_History.Redo();
	}

	void AnimGraphWindow::Initialize()
	{
		ed::Config cfg;
		m_EdCtx = ed::CreateEditor(&cfg);
	}

	void AnimGraphWindow::Shutdown()
	{
		if (m_EdCtx)
		{
			ed::DestroyEditor(m_EdCtx);
			m_EdCtx = nullptr;
		}
	}

	void AnimGraphWindow::OpenForAsset(const std::shared_ptr<AnimGraphAsset>& asset,
		const std::shared_ptr<SkeletalMeshAsset>& skeleton)
	{
		// Carimbo de versao. Se esta linha aparecer no console ao abrir o
		// editor, os arquivos do SM_UESTYLE_V1 estao em uso (maquina de
		// estados estilo Unreal: setas retas borda-a-borda, icone de
		// transicao clicavel, arrasto de borda cria transicao, nivel de
		// regra navegavel).
		AXE_EDITOR_INFO("AnimGraph editor - SM_UESTYLE_V1 + AG3b");

		m_Asset = asset;
		m_Skeleton = skeleton;
		m_Open = true;
		m_Dirty = false;

		m_Nav.clear();

		if (asset)
			m_Nav.push_back({ "AnimGraph", &asset->GetRoot(), nullptr });

		m_NeedsContextReset = true;
		m_PositionsLoaded = false;
		m_SelectedNode = -1;
		m_SelectedState = -1;
		m_SelectedTransition = -1;
		m_SelectedCondition = -1;

		// AG3b — historico zerado e baseline semeado com o estado RECEM-ABERTO.
		//
		// Zerar e obrigatorio: um Ctrl+Z herdado do asset anterior restauraria
		// o grafo DAQUELE asset por cima deste.
		//
		// E o baseline precisa existir desde ja. Sem ele, a primeira edicao
		// nao teria "antes" para onde voltar e seria engolida — voce faria uma
		// mudanca, apertaria Ctrl+Z, e nada aconteceria.
		m_History.Clear();
		m_PendingUndo = false;
		m_PendingUndoName.clear();
		m_Baseline = asset ? std::make_shared<AnimSnapshot>(CaptureState()) : nullptr;
	}

	void AnimGraphWindow::NavigateTo(const NavEntry& entry)
	{
		m_Nav.push_back(entry);
		m_NeedsContextReset = true;
		m_PositionsLoaded = false;
		m_SelectedNode = -1;
		m_SelectedState = -1;
		m_SelectedTransition = -1;
		m_SelectedCondition = -1;
	}

	void AnimGraphWindow::NavigateUpTo(int depth)
	{
		if (depth < 0 || depth >= (int)m_Nav.size())
			return;

		m_Nav.resize(depth + 1);
		m_NeedsContextReset = true;
		m_PositionsLoaded = false;
		m_SelectedNode = -1;
		m_SelectedState = -1;
		m_SelectedTransition = -1;
		m_SelectedCondition = -1;
	}

	bool AnimGraphWindow::DecodePin(int pinId, int& outNode, int& outPin, bool& outIsData, bool& outIsOutput)
	{
		outIsData = false;
		outIsOutput = false;

		if (pinId >= 0x30000 && pinId < 0x40000)
		{
			outNode = pinId - 0x30000;
			outPin = 0;
			outIsOutput = true;
			return true;
		}

		if (pinId >= 0x20000 && pinId < 0x30000)
		{
			const int v = pinId - 0x20000;
			outNode = v / 16;
			outPin = v % 16;
			outIsData = true;
			return true;
		}

		if (pinId >= 0x10000 && pinId < 0x20000)
		{
			const int v = pinId - 0x10000;
			outNode = v / 16;
			outPin = v % 16;
			return true;
		}

		return false;
	}

	void AnimGraphWindow::Save()
	{
		if (!m_Asset)
			return;

		// Grava a posição dos nós do nível ATUAL antes de salvar.
		//
		// Só do atual: as posições dos outros níveis já foram gravadas quando
		// você saiu deles (ver DrawPoseGraphCanvas / DrawStateMachineCanvas —
		// eles leem a posição de volta todo frame).
		if (m_Asset->Save())
		{
			m_Dirty = false;

			// Avisa as instancias (personagens na CENA) que o grafo mudou —
			// cada uma re-clona no proximo Update. E o "compilar" do nosso
			// Anim Blueprint: salvou, a cena acompanha.
			m_Asset->BumpVersion();

			AXE_EDITOR_INFO("AnimGraph '{}' saved.", m_Asset->GetName());
		}
	}

	void AnimGraphWindow::Draw()
	{
		if (!m_Open || !m_Asset || m_Nav.empty())
			return;

		// NOTA: RenderPreview() NAO e chamado aqui.
		//
		// Ele roda no EditorLayer::OnRender, ANTES do EditorUI->Draw() — junto
		// com os previews do Material, do Particle e do Script. Renderizar num
		// framebuffer no meio da passada do ImGui misturaria estado de GL, e o
		// sintoma seria a UI piscando ou o preview vindo em branco.
		if (m_NeedsContextReset)
		{
			if (m_EdCtx)
				ed::DestroyEditor(m_EdCtx);

			ed::Config cfg;
			m_EdCtx = ed::CreateEditor(&cfg);

			m_NeedsContextReset = false;
		}

		std::string title = "AnimGraph - " + m_Asset->GetName();
		if (m_Dirty) title += " *";
		title += "###AnimGraphWindow";

		ImGui::SetNextWindowSize(ImVec2(1400, 800), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin(title.c_str(), &m_Open, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		DrawToolbar();
		DrawBreadcrumb();
		ImGui::Separator();

		// ── Dockspace PROPRIO ────────────────────────────────────────────────
		//
		// Os paineis viram janelas de verdade: arrastaveis, redimensionaveis,
		// empilhaveis em abas. O `BeginChild` de tamanho fixo que estava aqui
		// nao fazia nada disso — e num editor de grafo, onde as vezes voce quer
		// o canvas inteiro e as vezes o preview grande, isso incomoda em cinco
		// minutos de uso.
		const ImGuiID dockId = ImGui::GetID("AnimGraphDock");

		DrawDockLayout(dockId);

		ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

		ImGui::End();

		// ── Os paineis ───────────────────────────────────────────────────────
		//
		// Submetidos FORA do Begin/End da janela-mae. E como o docking do ImGui
		// funciona: eles sao janelas de topo, e o dockspace apenas os hospeda.
		// Os atalhos valem em qualquer painel do editor, nao so no canvas —
		// Ctrl+S com o foco nos Parametros tem que salvar. O foco e a OR dos
		// tres; ver a nota em "Graph###anim_graph".
		m_ShortcutFocus = false;

		ImGui::Begin("Parameters###anim_params");
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			m_ShortcutFocus = true;
		DrawParametersPanel();
		ImGui::End();

		ImGui::Begin("Graph###anim_graph");
		{
			// AG3c — o foco e capturado AQUI, dentro do Begin/End da janela.
			//
			// Os atalhos rodam no fim do frame, depois de todos os End() — e
			// naquele ponto a "janela corrente" do ImGui nao e mais nenhuma
			// janela do AnimGraph, entao IsWindowFocused respondia sempre
			// FALSE e nenhum atalho disparava. Era isso que fazia Ctrl+Z nao
			// funcionar.
			m_ShortcutFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			NavEntry& top = m_Nav.back();

			if (top.Graph)
				DrawPoseGraphCanvas(*top.Graph);
			else if (top.Sm && top.TransIndex >= 0)
				DrawTransitionRuleCanvas(*top.Sm, top.TransIndex);
			else if (top.Sm)
				DrawStateMachineCanvas(*top.Sm);
		}
		ImGui::End();

		ImGui::Begin("Details###anim_details");
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			m_ShortcutFocus = true;
		DrawDetailsPanel();
		ImGui::End();

		DrawPreviewWindow();

		// ── Atalhos e fechamento de comando (AG3b) ───────────────────────────
		//
		// No FIM do frame, depois de todos os canvases, por dois motivos:
		//
		//   1. CommitPendingUndo precisa ver o gesto ja terminado. Rodando
		//      antes, `IsMouseDown` ainda estaria true no ultimo frame do
		//      arrasto e o comando so fecharia no frame seguinte.
		//   2. Um Ctrl+Z lido aqui dispara o RestoreState com o desenho ja
		//      feito, entao nada nesta passada toca no grafo que acabou de ser
		//      trocado.
		//
		// A guarda de foco nao e zelo excessivo: sem ela, um Ctrl+Z digitado
		// enquanto se renomeia um parametro desfaria o grafo em vez do texto.
		// IsAnyItemActive cobre exatamente esse caso.
		CommitPendingUndo();

		if (m_ShortcutFocus && !ImGui::IsAnyItemActive())
		{
			const ImGuiIO& io = ImGui::GetIO();

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
			{
				// Ctrl+Shift+Z e redo — a convencao que a maioria das
				// ferramentas de grafo usa. Ctrl+Y continua valendo para quem
				// vem do Windows. Testado ANTES do undo puro, senao o Shift
				// seria ignorado e o Ctrl+Z levaria os dois caminhos.
				if (io.KeyShift) DoRedo();
				else             DoUndo();
			}

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
				DoRedo();

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
				Save();

			// ── Clipboard ────────────────────────────────────────────────────
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
				CopySelection(false);

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
				CopySelection(true);

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
				PasteClipboard();

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
			{
				CopySelection(false);
				PasteClipboard();
			}
		}
	}

	void AnimGraphWindow::DrawDockLayout(ImGuiID dockspaceId)
	{
		// Layout padrao, construido so quando o dockspace AINDA NAO EXISTE.
		//
		// A flag de membro (m_LayoutBuilt) nao bastava: o objeto persiste entre
		// aberturas da janela, mas o ImGui recria o dockspace toda vez que a
		// janela reabre — e a flag, ja em true, pulava a construcao, deixando os
		// paineis sem casa (era o "Detalhes que nao persiste, sempre tenho que
		// dockar de novo").
		//
		// DockBuilderGetNode == null significa "nunca foi montado". Enquanto
		// existir — inclusive com o arranjo que VOCE mexeu, que o ImGui salva no
		// imgui.ini — nao tocamos nele.
		if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
			return;

		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetContentRegionAvail());

		ImGuiID center = dockspaceId;

		// Esquerda: parametros (estreito). Direita: detalhes. Direita-baixo: o
		// preview — abaixo dos detalhes, porque voce olha os dois juntos: mexe
		// numa condicao e ve o personagem reagir.
		const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.16f, nullptr, &center);
		const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
		const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, nullptr);

		ImGui::DockBuilderDockWindow("Parameters###anim_params", left);
		ImGui::DockBuilderDockWindow("Graph###anim_graph", center);
		ImGui::DockBuilderDockWindow("Details###anim_details", right);
		ImGui::DockBuilderDockWindow("Preview###anim_preview", rightBottom);

		ImGui::DockBuilderFinish(dockspaceId);
	}

	void AnimGraphWindow::DrawToolbar()
	{
		// AG3 — mesmo vocabulario do Control Rig e do Script Editor.
		//
		// Nao e so estetica: as tres janelas sao o MESMO tipo de ferramenta
		// (um grafo com preview e detalhes), e ate aqui a de animacao era a
		// unica com botao de texto puro. Trocar de janela custava um reajuste
		// de leitura que nao deveria existir.
		//
		// ui::IconButton vem de editor_widgets.hpp, o header compartilhado; os
		// ICON_* vem da Font Awesome 6 subsetada. Nada novo foi criado aqui.
		if (ui::IconButton(ICON_SAVE, "Save  (Ctrl+S)", ui::Accent::Primary))
			Save();

		ImGui::SameLine();

		// AG3b — undo / redo, mesmo arranjo do Control Rig: desabilitados
		// quando nao ha o que desfazer, com o NOME da acao no tooltip.
		// "Desfazer o que?" e a duvida real de quem esta com a mao no Ctrl+Z.
		{
			const bool canUndo = m_History.CanUndo();

			ImGui::BeginDisabled(!canUndo);
			if (ui::IconButton(ICON_UNDO, nullptr))
				DoUndo();
			ImGui::EndDisabled();

			if (canUndo && ImGui::IsItemHovered())
				ImGui::SetTooltip("Undo: %s  (Ctrl+Z)", m_History.GetUndoName().c_str());

			ImGui::SameLine();

			const bool canRedo = m_History.CanRedo();

			ImGui::BeginDisabled(!canRedo);
			if (ui::IconButton(ICON_REDO, nullptr))
				DoRedo();
			ImGui::EndDisabled();

			if (canRedo && ImGui::IsItemHovered())
				ImGui::SetTooltip("Redo: %s  (Ctrl+Y)", m_History.GetRedoName().c_str());
		}

		ImGui::SameLine();
		ui::ToolbarSeparator();
		ImGui::SameLine();

		const NavEntry& top = m_Nav.back();

		// A dica de uso ganhou o icone de informacao — do mesmo jeito que as
		// outras janelas marcam texto auxiliar. Sem ele, a linha compete
		// visualmente com o botao de salvar.
		if (top.Graph)
			ImGui::TextDisabled(ICON_CIRCLE_INFO "  right-click the background = add node  |  double-click a State Machine = step into it");
		else if (top.TransIndex >= 0)
			ImGui::TextDisabled(ICON_CIRCLE_INFO "  right-click the background = new condition  |  select a condition node to edit  |  Delete removes");
		else
			ImGui::TextDisabled(ICON_CIRCLE_INFO "  drag from a state's EDGE = transition  |  the arrow's circle IS the transition (double-click opens its rule)");
	}

	void AnimGraphWindow::DrawBreadcrumb()
	{
		// A trilha. Sem ela, três níveis abaixo você não sabe onde está — e é o
		// primeiro lugar onde um editor de grafos aninhados fica confuso.
		for (std::size_t i = 0; i < m_Nav.size(); ++i)
		{
			if (i > 0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled(ICON_ARROW_RIGHT);
				ImGui::SameLine();
			}

			const bool isLast = (i + 1 == m_Nav.size());

			if (isLast)
			{
				ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", m_Nav[i].Label.c_str());
			}
			else
			{
				ImGui::PushID((int)i);

				if (ImGui::SmallButton(m_Nav[i].Label.c_str()))
					NavigateUpTo((int)i);

				ImGui::PopID();
			}
		}
	}

	void AnimGraphWindow::DrawParametersPanel()
	{
		// ── Painel estilo "My Blueprint > Variables" da Unreal ───────────────
		//
		// Linha = pilula colorida do tipo + nome + tipo a direita. A LINHA
		// INTEIRA e arrastavel pro grafo. Clique seleciona; duplo-clique
		// renomeia; o default do selecionado aparece embaixo — em vez de
		// cada linha carregar combo + botao + campo, que era o formulario
		// entulhado de antes.
		ui::SectionHeader(ICON_SLIDERS, "Parameters", ui::Accent::Primary);
		ImGui::SameLine();
		ImGui::TextDisabled("(drag onto the graph)");
		ImGui::Separator();
		ImGui::Spacing();

		auto& params = m_Asset->GetParameters();

		auto pillColor = [](AnimParamType t) -> ImVec4
			{
				switch (t)
				{
				case AnimParamType::Float:   return ImVec4(0.38f, 0.75f, 0.36f, 1.0f);
				case AnimParamType::Int:     return ImVec4(0.25f, 0.65f, 0.85f, 1.0f);
				case AnimParamType::Bool:    return ImVec4(0.80f, 0.28f, 0.28f, 1.0f);
				case AnimParamType::Trigger: return ImVec4(0.85f, 0.55f, 0.20f, 1.0f);
				}
				return ImVec4(1, 1, 1, 1);
			};

		auto typeName = [](AnimParamType t) -> const char*
			{
				switch (t)
				{
				case AnimParamType::Float:   return "Float";
				case AnimParamType::Int:     return "Int";
				case AnimParamType::Bool:    return "Bool";
				case AnimParamType::Trigger: return "Trigger";
				}
				return "?";
			};

		int removeIdx = -1;
		const float rowH = ImGui::GetTextLineHeight() + 8.0f;

		for (std::size_t i = 0; i < params.size(); ++i)
		{
			ImGui::PushID((int)i);

			// ── Modo renomeio ────────────────────────────────────────────────
			if (m_RenamingParam == (int)i)
			{
				ImGui::SetNextItemWidth(-1);

				const bool firstFrame = (m_RenameBuf[0] == '\0');

				if (firstFrame)
				{
					std::snprintf(m_RenameBuf, sizeof(m_RenameBuf), "%s", params[i].Name.c_str());

					// SO no primeiro frame: chamar todo frame rouba o foco
					// eternamente e o clique-fora nunca consegue confirmar.
					ImGui::SetKeyboardFocusHere();
				}

				const bool done = ImGui::InputText("##ren", m_RenameBuf, sizeof(m_RenameBuf),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

				if (done || ImGui::IsItemDeactivated())
				{
					if (m_RenameBuf[0] != '\0' && params[i].Name != m_RenameBuf)
					{
						params[i].Name = m_RenameBuf;
						MarkEdited();
					}

					m_RenamingParam = -1;
					m_RenameBuf[0] = '\0';
				}

				ImGui::PopID();
				continue;
			}

			// ── Linha normal ─────────────────────────────────────────────────
			const ImVec2 rowMin = ImGui::GetCursorScreenPos();
			const float  rowW = ImGui::GetContentRegionAvail().x;

			if (ImGui::Selectable("##row", m_SelectedParam == (int)i,
				ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowH)))
			{
				m_SelectedParam = (int)i;

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_RenamingParam = (int)i;
					m_RenameBuf[0] = '\0';
				}
			}

			// A linha inteira e a fonte do arrasto, como na Unreal.
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				const int idx = (int)i;
				ImGui::SetDragDropPayload("ANIM_PARAM", &idx, sizeof(int));
				ImGui::Text("Get %s", params[i].Name.c_str());
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginPopupContextItem("param_ctx"))
			{
				m_SelectedParam = (int)i;

				if (ImGui::MenuItem("Rename"))
				{
					m_RenamingParam = (int)i;
					m_RenameBuf[0] = '\0';
				}

				if (ImGui::MenuItem("Delete"))
					removeIdx = (int)i;

				ImGui::EndPopup();
			}

			// Desenho por cima do Selectable: pilula + nome + tipo a direita.
			ImDrawList* dl = ImGui::GetWindowDrawList();

			const float  midY = rowMin.y + rowH * 0.5f;
			const ImVec4 col = pillColor(params[i].Type);

			dl->AddRectFilled(
				ImVec2(rowMin.x + 4.0f, midY - 4.5f),
				ImVec2(rowMin.x + 20.0f, midY + 4.5f),
				ImGui::ColorConvertFloat4ToU32(col), 4.5f);

			dl->AddText(ImVec2(rowMin.x + 27.0f, midY - ImGui::GetTextLineHeight() * 0.5f),
				ImGui::GetColorU32(ImGuiCol_Text), params[i].Name.c_str());

			const char* tn = typeName(params[i].Type);
			const float tw = ImGui::CalcTextSize(tn).x;

			dl->AddText(ImVec2(rowMin.x + rowW - tw - 6.0f, midY - ImGui::GetTextLineHeight() * 0.5f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), tn);

			ImGui::PopID();
		}

		if (ImGui::Button("+ Parameter", ImVec2(-1, 0)))
		{
			AnimParamDecl d;
			d.Name = "NovoParam";
			d.Type = AnimParamType::Float;
			params.push_back(d);

			m_SelectedParam = (int)params.size() - 1;
			m_RenamingParam = m_SelectedParam;   // ja nasce renomeando
			m_RenameBuf[0] = '\0';
			MarkEdited();
		}

		// ── Detalhes do parametro selecionado ────────────────────────────────
		//
		// Tipo e default moram AQUI, nao em cada linha — o mesmo desenho da
		// Unreal (selecionar a variavel abre os detalhes dela).
		if (m_SelectedParam >= 0 && m_SelectedParam < (int)params.size())
		{
			auto& p = params[m_SelectedParam];

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::TextDisabled("%s", p.Name.c_str());

			const char* kinds[] = { "Float", "Int", "Bool", "Trigger" };
			int kind = (int)p.Type;

			ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo("##t", &kind, kinds, 4)) { p.Type = (AnimParamType)kind; MarkEdited(); }

			// O DEFAULT importa mais do que parece: se "Speed" comeca em 0 mas
			// o personagem spawna correndo, o grafo passa um frame no estado
			// errado — e voce ve um piscar de idle.
			switch (p.Type)
			{
			case AnimParamType::Float:
				ImGui::SetNextItemWidth(-1);
				if (ImGui::DragFloat("##d", &p.DefaultF, 0.5f)) MarkEdited();
				break;
			case AnimParamType::Int:
				ImGui::SetNextItemWidth(-1);
				if (ImGui::DragInt("##d", &p.DefaultI)) MarkEdited();
				break;
			case AnimParamType::Bool:
				if (ImGui::Checkbox("default", &p.DefaultB)) MarkEdited();
				break;
			case AnimParamType::Trigger:
				// Trigger nao tem default: um pulso armado no frame 1
				// dispararia a transicao sozinho, sem ninguem ter apertado.
				ImGui::TextDisabled("(pulse)");
				break;
			}
		}

		if (removeIdx >= 0)
		{
			params.erase(params.begin() + removeIdx);

			if (m_SelectedParam == removeIdx) m_SelectedParam = -1;
			else if (m_SelectedParam > removeIdx) --m_SelectedParam;

			m_RenamingParam = -1;
			MarkEdited();
		}
	}


	// ═════════════════════════════════════════════════════════════════════════
	//  CANVAS DE GRAFO DE POSES  (nivel 1 e nivel 3 — o MESMO canvas)
	// ═════════════════════════════════════════════════════════════════════════

	void AnimGraphWindow::DrawPoseGraphCanvas(AnimPoseGraph& graph)
	{
		// ── Alvo de drop do Asset Browser ────────────────────────────────────
		//
		// Um .axeskel arrastado aqui vira um Clip Player. O ImGui::DockSpace da
		// janela-mae ja aceita o payload; capturamos aqui, ANTES do ed::Begin,
		// porque dropar DENTRO do node editor tem problema de coordenada.
		//
		// Guardamos o pedido e criamos o no depois do ed::Begin, ja com a
		// posicao certa no canvas.
		bool wantDropNode = false;
		std::string dropUuid;

		// Parametro arrastado do painel — vira um no Get, como arrastar uma
		// variavel no Blueprint da Unreal.
		int dropParamIdx = -1;

		// ImGui::BeginDragDropTarget() "normal" se prende ao ULTIMO ITEM
		// desenhado — que aqui era qualquer coisa, menos o canvas. O alvo
		// nunca existia de verdade, e nada podia ser solto no grafo. O
		// TargetCustom recebe o RETANGULO do canvas explicitamente.
		{
			const ImVec2 cmin = ImGui::GetCursorScreenPos();
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const ImRect canvasRect(cmin,
				ImVec2(cmin.x + avail.x, cmin.y + avail.y));

			if (ImGui::BeginDragDropTargetCustom(canvasRect,
				ImGui::GetID("anim_canvas_drop")))
			{
				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_UUID"))
				{
					wantDropNode = true;
					dropUuid = std::string((const char*)pl->Data);
				}

				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ANIM_PARAM"))
					dropParamIdx = *(const int*)pl->Data;

				ImGui::EndDragDropTarget();
			}
		}

		ed::SetCurrentEditor(m_EdCtx);
		ed::Begin("PoseGraph");

		// Trata o drop, se houve.
		if (wantDropNode && m_Asset)
		{
			const AssetRecord* rec = AssetDatabase::Get().GetByUUID(dropUuid);

			std::string ext = rec ? rec->FilePath.extension().string() : std::string{};
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char ch) { return (char)std::tolower(ch); });

			const bool isAnimFile =
				(ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae");

			// ── .axeskel num grafo ORFAO: (re)vincula o esqueleto ───────────
			//
			// Acontece quando o .axeskel referenciado foi apagado e recriado
			// (UUID novo) — o grafo abre com "Sem esqueleto". Arrastar o
			// .axeskel novo pro canvas conserta o vinculo na hora.
			if (rec && ext == ".axeskel" && !m_Skeleton)
			{
				auto skel = SkeletalMeshAsset::LoadFromFile(rec->FilePath);

				if (skel && skel->Resolve())
				{
					m_Skeleton = skel;
					m_Asset->SetSkeletonUUID(dropUuid);
					m_Asset->Resolve(*skel);
					MarkEdited();

					AXE_EDITOR_INFO("AnimGraph '{}' bound to skeleton '{}' ({} clip(s)). Save to persist.",
						m_Asset->GetName(), skel->GetName(), skel->GetClips().size());
				}
				else
				{
					AXE_EDITOR_ERROR("Could not resolve skeleton '{}'.", rec->Name);
				}
			}
			// ── Arquivo de ANIMACAO (o fluxo "arrasta o idle pro grafo") ────
			//
			// Importa os clipes pro .axeskel (mesmo caminho do botao Importar
			// do Inspector) e ja cria um Clip Player com o primeiro take
			// pronto pra ligar — como arrastar uma AnimSequence na Unreal.
			else if (rec && isAnimFile && m_Skeleton)
			{
				const int before = (int)m_Skeleton->GetClips().size();
				const int added = m_Skeleton->AddAnimation(rec->FilePath);

				if (added > 0)
				{
					m_Skeleton->Save();

					const auto& clips = m_Skeleton->GetClips();
					const auto& firstNew = clips[before];

					auto node = CreateAnimNode("ClipPlayer");
					node->Title = firstNew->GetName();

					if (auto* cp = dynamic_cast<AnimNode_ClipPlayer*>(node.get()))
					{
						cp->ClipName = firstNew->GetName();
						cp->Clip = firstNew;
					}

					const ImVec2 c = ed::ScreenToCanvas(ImGui::GetMousePos());
					node->EditorX = c.x;
					node->EditorY = c.y;

					const int id = graph.AddNode(std::move(node));
					ed::SetNodePosition(id, c);

					m_SelectedNode = id;
					MarkEdited();

					if (added > 1)
						AXE_EDITOR_INFO("'{}': {} takes imported — the rest are in the clip combo.",
							rec->Name, added);
				}
				else
				{
					AXE_EDITOR_WARN("'{}' brought no clips compatible with the skeleton.", rec->Name);
				}
			}
			else if (rec && isAnimFile && !m_Skeleton)
			{
				AXE_EDITOR_WARN("The graph has no skeleton — drag the character's .axeskel onto the canvas first.");
			}
			// ── .axeskel com esqueleto ja vinculado: fluxo de clipe ─────────
			else if (rec && ext == ".axeskel" && m_Skeleton)
			{
				const ImVec2 c = ed::ScreenToCanvas(ImGui::GetMousePos());

				// Caiu EM CIMA de um player existente? Entao e atribuicao, nao
				// criacao: seleciona o no e o painel Detalhes vira o lugar de
				// escolher o clipe — em vez de nascer um segundo player por cima.
				AnimNode* hit = nullptr;

				for (const auto& n : graph.GetNodes())
				{
					const ImVec2 p = ed::GetNodePosition(n->Id);
					const ImVec2 s = ed::GetNodeSize(n->Id);

					if (c.x >= p.x && c.x <= p.x + s.x &&
						c.y >= p.y && c.y <= p.y + s.y)
					{
						hit = n.get();
						break;
					}
				}

				const bool hitPlayer = hit &&
					(dynamic_cast<AnimNode_ClipPlayer*>(hit) != nullptr ||
						dynamic_cast<AnimNode_BlendSpacePlayer*>(hit) != nullptr);

				if (hitPlayer)
				{
					m_SelectedNode = hit->Id;

					AXE_EDITOR_INFO("'{}' dropped onto '{}': pick the clip in the Details panel.",
						rec->Name, hit->Title.empty() ? hit->TypeName() : hit->Title);
				}
				else
				{
					auto cp = CreateAnimNode("ClipPlayer");
					cp->Title = "Clip Player";
					cp->EditorX = c.x;
					cp->EditorY = c.y;

					const int id = graph.AddNode(std::move(cp));
					ed::SetNodePosition(id, c);

					m_SelectedNode = id;
					MarkEdited();

					AXE_EDITOR_INFO("Clip Player created from '{}'. Pick the clip in the Details panel.",
						rec->Name);
				}
			}
		}

		// Cria o no de PARAMETRO do drop, se houve — o Get ja nasce com o nome
		// e o titulo do parametro, pronto pra ligar.
		if (dropParamIdx >= 0 && m_Asset)
		{
			auto& params = m_Asset->GetParameters();

			if (dropParamIdx < (int)params.size())
			{
				const auto& p = params[dropParamIdx];

				const char* type = nullptr;

				switch (p.Type)
				{
				case AnimParamType::Float: type = "GetFloat"; break;
				case AnimParamType::Bool:  type = "GetBool";  break;
				default: break;   // Int/Trigger ainda nao tem no Get
				}

				if (type)
				{
					auto node = CreateAnimNode(type);
					node->Title = p.Name;

					if (auto* gf = dynamic_cast<AnimNode_GetFloat*>(node.get()))
						gf->Parameter = p.Name;
					else if (auto* gb = dynamic_cast<AnimNode_GetBool*>(node.get()))
						gb->Parameter = p.Name;

					const ImVec2 c = ed::ScreenToCanvas(ImGui::GetMousePos());
					node->EditorX = c.x;
					node->EditorY = c.y;

					const int id = graph.AddNode(std::move(node));
					ed::SetNodePosition(id, c);

					m_SelectedNode = id;
					MarkEdited();
				}
				else
				{
					AXE_EDITOR_WARN("Parameter '{}' is {}: there is no Get node for this type yet.",
						p.Name, p.Type == AnimParamType::Int ? "Int" : "Trigger");
				}
			}
		}

		const auto& nodes = graph.GetNodes();

		if (!m_PositionsLoaded)
		{
			for (const auto& n : nodes)
				ed::SetNodePosition(n->Id, ImVec2(n->EditorX, n->EditorY));

			m_PositionsLoaded = true;
		}

		// ── Nos ──────────────────────────────────────────────────────────────
		//
		// ANIMGRAPH_STYLE_V2 — layout em duas colunas, como no Material/Script
		// editor: entradas na esquerda, a saida na direita da PRIMEIRA linha.
		// Header colorido por TIPO — a cor nao e decoracao: num grafo com 20
		// nos, identificar o tipo de relance e a diferenca entre navegar e
		// procurar.
		ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 5));
		ed::PushStyleVar(ed::StyleVar_NodeRounding, 5.0f);
		ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);

		for (const auto& n : nodes)
		{
			// ── Reroute: caminho de desenho totalmente separado (AG3) ────────
			//
			// Nao passa pelo header + duas colunas: um reroute com header
			// seria um card, e um card no meio do fio atrapalha exatamente o
			// que ele veio resolver. O que se quer e um PONTO no fio.
			//
			// Os dois pinos ficam colados (SameLine com espacamento zero), o
			// que da a impressao de um unico ponto mesmo sendo dois ed::PinId
			// distintos por tras — mesmo truque do reroute do Script Editor.
			if (dynamic_cast<AnimNode_Reroute*>(n.get()))
			{
				ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(2, 2, 2, 2));
				ed::PushStyleVar(ed::StyleVar_NodeRounding, kReroutePinSize);
				ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.10f, 0.10f, 0.12f, 0.95f));
				ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

				// AG3b — o reroute segue o tipo do fio: pose usa o pino de
				// pose e a seta; dado usa o pino de dado e o circulo, com a
				// cor do tipo. Sem isto, um reroute num fio de Float
				// desenharia seta branca de pose no meio de um fio verde.
				const bool isPoseRr = (n->OutputType() == AnimPinType::Pose);
				const ImVec4 rrCol = isPoseRr ? kPoseColor : ColorFor(n->OutputType());

				const bool rrIn = isPoseRr
					? (!n->Inputs.empty() && n->Inputs[0] != nullptr)
					: (!n->DataInputs.empty() && n->DataInputs[0].Link != nullptr);

				ed::BeginNode(n->Id);

				ed::BeginPin(isPoseRr ? PoseInPin(n->Id, 0) : DataInPin(n->Id, 0),
					ed::PinKind::Input);
				ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
				ed::PinPivotSize(ImVec2(0, 0));
				DrawPinIcon(rrCol, rrIn, isPoseRr, kReroutePinSize);
				ed::EndPin();

				ImGui::SameLine(0.0f, 0.0f);

				ed::BeginPin(OutPin(n->Id), ed::PinKind::Output);
				ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
				ed::PinPivotSize(ImVec2(0, 0));
				DrawPinIcon(rrCol, true, isPoseRr, kReroutePinSize);
				ed::EndPin();

				ed::EndNode();
				ed::PopStyleColor(2);
				ed::PopStyleVar(2);
				continue;
			}

			const bool isOutput = (n->Id == graph.GetOutputNode());
			const bool isSm = (dynamic_cast<AnimNode_StateMachine*>(n.get()) != nullptr);
			const bool isValue = (n->OutputType() != AnimPinType::Pose);
			const bool isPlayer =
				(dynamic_cast<AnimNode_ClipPlayer*>(n.get()) != nullptr) ||
				(dynamic_cast<AnimNode_BlendSpacePlayer*>(n.get()) != nullptr);

			// Paleta estilo Unreal: escura, dessaturada, e a COR MORA SO NO
			// HEADER — o corpo e a borda sao neutros em todos os nos. E o que
			// separa "editor profissional" de "arvore de natal".
			ImVec4 accent{ 0.20f, 0.21f, 0.24f, 1.0f };   // blends e afins: cinza
			const char* sub = nullptr;

			if (isOutput) { accent = ImVec4(0.27f, 0.21f, 0.13f, 1.0f); sub = "AnimGraph"; }
			else if (isSm) { accent = ImVec4(0.20f, 0.23f, 0.33f, 1.0f); sub = "State Machine  (double-click)"; }
			else if (isValue) { accent = ImVec4(0.13f, 0.29f, 0.16f, 1.0f); sub = "Variable"; }
			else if (isPlayer) { accent = ImVec4(0.14f, 0.30f, 0.21f, 1.0f); }   // player: verde UE

			const std::string title = n->Title.empty() ? n->TypeName() : n->Title;

			// Largura do card: o maior entre titulo/subtitulo E o conteudo das
			// duas colunas (pinos da esquerda + saida da direita). Sem a
			// segunda parte, um pino de nome comprido com campo inline
			// invadiria a coluna da saida.
			float leftW = 0.0f;

			for (int i = 0; i < n->InputCount(); ++i)
				leftW = std::max(leftW,
					kPinIconSize + 4.0f + ImGui::CalcTextSize(n->InputName(i)).x);

			for (const auto& d : n->DataInputs)
			{
				float wRow = kPinIconSize + 4.0f + ImGui::CalcTextSize(d.Name.c_str()).x;
				if (!d.Link)
					wRow += (d.Type == AnimPinType::Bool)
					? ImGui::GetFrameHeight() + 8.0f
					: 56.0f + 8.0f;
				leftW = std::max(leftW, wRow);
			}

			const float outW = isOutput ? 0.0f
				: ImGui::CalcTextSize(n->OutputType() == AnimPinType::Pose ? "OutPose" : "Out").x
				+ kPinIconSize + 6.0f;

			const float nodeWidth = std::max(
				NodeWidthFor(title.c_str(), sub),
				leftW + outW + 32.0f);

			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.085f, 0.085f, 0.095f, 0.96f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

			ed::BeginNode(n->Id);

			DrawNodeHeader(title.c_str(), sub, accent, nodeWidth);

			const ImVec2 bodyStart = ImGui::GetCursorScreenPos();

			// ── Coluna esquerda: entradas ────────────────────────────────────
			ImGui::BeginGroup();

			// Pinos de POSE. Icone CHEIO = ligado, VAZIO = solto — voce ve o
			// que falta conectar sem clicar em nada.
			for (int i = 0; i < n->InputCount(); ++i)
			{
				ed::BeginPin(PoseInPin(n->Id, i), ed::PinKind::Input);
				ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
				ed::PinPivotSize(ImVec2(0, 0));

				ImGui::BeginGroup();
				DrawPinIcon(kPoseColor, n->Inputs[i] != nullptr, true);
				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY()
					+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
				ImGui::TextUnformatted(n->InputName(i));
				ImGui::EndGroup();

				ed::EndPin();
			}

			// Pinos de DADO. Desconectado = valor inline editavel ao lado do
			// nome (como digitar direto no pino, na Unreal). Conectado = so o
			// nome; o icone cheio ja diz que quem manda e o link.
			for (std::size_t i = 0; i < n->DataInputs.size(); ++i)
			{
				auto& d = n->DataInputs[i];
				const ImVec4 col = ColorFor(d.Type);

				ed::BeginPin(DataInPin(n->Id, (int)i), ed::PinKind::Input);
				ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
				ed::PinPivotSize(ImVec2(0, 0));

				ImGui::BeginGroup();
				DrawPinIcon(col, d.Link != nullptr, false);
				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY()
					+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
				ImGui::TextUnformatted(d.Name.c_str());
				ImGui::EndGroup();

				ed::EndPin();

				if (!d.Link)
				{
					ImGui::SameLine();
					ImGui::PushID((int)(n->Id * 32 + i));

					if (d.Type == AnimPinType::Bool)
					{
						if (ImGui::Checkbox("##b", &d.InlineBool)) MarkEdited();
					}
					else
					{
						ImGui::SetNextItemWidth(56.0f);
						if (ImGui::DragFloat("##f", &d.InlineFloat, 0.5f, 0.0f, 0.0f, "%.2f"))
							MarkEdited();
					}

					ImGui::PopID();
				}
			}

			ImGui::EndGroup();

			// ── Coluna direita: a SAIDA, na primeira linha ───────────────────
			//
			// Um so pino: todo no produz exatamente um valor. Um no com duas
			// saidas seria dois nos.
			if (!isOutput)
			{
				const bool poseOut = (n->OutputType() == AnimPinType::Pose);
				const char* outName = poseOut ? "OutPose" : "Out";

				const float contentW = ImGui::CalcTextSize(outName).x + kPinIconSize + 6.0f;

				ImGui::SetCursorScreenPos(ImVec2(
					bodyStart.x + nodeWidth - contentW - 8.0f, bodyStart.y));

				ed::BeginPin(OutPin(n->Id), ed::PinKind::Output);
				ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
				ed::PinPivotSize(ImVec2(0, 0));

				ImGui::BeginGroup();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY()
					+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
				ImGui::TextUnformatted(outName);
				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY()
					- (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
				DrawPinIcon(ColorFor(n->OutputType()), true, poseOut);
				ImGui::EndGroup();

				ed::EndPin();
			}

			// Garante a largura minima do card mesmo com pouco conteudo.
			ImGui::Dummy(ImVec2(nodeWidth, 1.0f));

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		ed::PopStyleVar(3);

		// ── Links ────────────────────────────────────────────────────────────
		const auto& links = graph.GetLinks();

		for (std::size_t i = 0; i < links.size(); ++i)
		{
			const auto& l = links[i];

			const int from = OutPin(l.FromNode);
			const int to = (l.Kind == AnimLinkKind::Data)
				? DataInPin(l.ToNode, l.ToPin)
				: PoseInPin(l.ToNode, l.ToPin);

			ImVec4 col = kPoseColor;

			if (l.Kind == AnimLinkKind::Data)
			{
				AnimNode* src = graph.FindNode(l.FromNode);
				col = src ? ColorFor(src->OutputType()) : kFloatColor;
			}

			ed::Link(LinkId((int)i), from, to, col, l.Kind == AnimLinkKind::Data ? 1.5f : 2.5f);
		}

		// ── Criar link ───────────────────────────────────────────────────────
		if (ed::BeginCreate())
		{
			ed::PinId a, b;

			if (ed::QueryNewLink(&a, &b))
			{
				int nA, pA, nB, pB;
				bool dA, oA, dB, oB;

				const bool okA = DecodePin((int)a.Get(), nA, pA, dA, oA);
				const bool okB = DecodePin((int)b.Get(), nB, pB, dB, oB);

				bool valid = okA && okB && oA && !oB && nA != nB;

				// TIPAGEM.
				//
				// Sem esta checagem, arrastar um float num pino de pose criaria um
				// link que o Resolve() depois rejeita — e o usuario veria a linha
				// desenhada, acharia que funcionou, e o personagem tocaria bind
				// pose sem explicacao. Rejeitar AQUI, em vermelho, e a diferenca
				// entre um editor honesto e um que mente.
				if (valid)
				{
					AnimNode* src = graph.FindNode(nA);
					AnimNode* dst = graph.FindNode(nB);

					if (!src || !dst) valid = false;
					else if (dB)
					{
						// AG4 — destino e pino de dado: o TIPO tem que bater.
						//
						// Antes a regra era so "a origem nao pode ser pose", e
						// isso deixava passar Bool -> pino Float. O sintoma era
						// cruel: o editor desenhava o fio em VERDE, e o
						// ReadFloat chamava EvaluateFloat num no que so
						// implementa EvaluateBool — recebendo o default 0.0f
						// para sempre. Foi exatamente o que fez o Get Bool
						// "IsAim" ligado no Alpha nunca funcionar.
						//
						// Com os nos logicos misturando Bool e Float no mesmo
						// grafo, esse engano deixa de ser possibilidade remota
						// e vira o erro mais provavel do dia a dia.
						valid = (src->OutputType() != AnimPinType::Pose)
							&& pB < (int)dst->DataInputs.size()
							&& src->OutputType() == dst->DataInputs[pB].Type;
					}
					else
					{
						// destino e pino de pose: a origem TEM que ser pose
						valid = (src->OutputType() == AnimPinType::Pose)
							&& pB < dst->InputCount();
					}
				}

				if (!valid)
				{
					ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
				}
				else if (ed::AcceptNewItem(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), 3.0f))
				{
					graph.AddLink(nA, nB, pB, dB ? AnimLinkKind::Data : AnimLinkKind::Pose);
					MarkEdited("Connect");
				}
			}
		}
		ed::EndCreate();

		// ── Apagar ───────────────────────────────────────────────────────────
		if (ed::BeginDelete())
		{
			ed::LinkId lid;
			while (ed::QueryDeletedLink(&lid))
			{
				if (ed::AcceptDeletedItem())
				{
					const int idx = (int)lid.Get() - 0x40000;
					const auto& ls = graph.GetLinks();

					if (idx >= 0 && idx < (int)ls.size())
					{
						const AnimLink l = ls[idx];
						graph.RemoveLinksTo(l.ToNode, l.ToPin, l.Kind);
						graph.Resolve();
						MarkEdited("Delete link");
					}
				}
			}

			ed::NodeId nid;
			while (ed::QueryDeletedNode(&nid))
			{
				const int id = (int)nid.Get();

				// O Output nao pode ser apagado: sem ele o grafo nao produz pose
				// nenhuma, e nao ha como recria-lo pela interface.
				if (id == graph.GetOutputNode())
				{
					ed::RejectDeletedItem();
					continue;
				}

				if (ed::AcceptDeletedItem())
				{
					graph.RemoveNode(id);
					m_SelectedNode = -1;
					MarkEdited("Delete node");
				}
			}
		}
		ed::EndDelete();

		// ── Menu de contexto: adicionar no ───────────────────────────────────
		ed::Suspend();

		if (ed::ShowBackgroundContextMenu())
		{
			m_MenuOpenCanvasPos = ed::ScreenToCanvas(ImGui::GetMousePos());
			ImGui::OpenPopup("add_node");
		}

		if (ImGui::BeginPopup("add_node"))
		{
			DrawNodePalette(graph);
			ImGui::EndPopup();
		}

		ed::Resume();

		ed::End();

		// AG3c — posicao do cursor em canvas, para o Ctrl+V colar sob o mouse.
		// Capturada AQUI porque o contexto do node-editor ainda esta ativo;
		// no fim do frame, onde os atalhos rodam, ScreenToCanvas nao teria
		// como converter.
		m_LastCanvasMousePos = ed::ScreenToCanvas(ImGui::GetMousePos());

		// ── Duplo-clique num fio insere um REROUTE (AG3b) ────────────────────
		//
		// O gesto que todo mundo tenta primeiro quando um fio atravessa meio
		// grafo. O ponto nasce onde voce clicou e o fio original vira DOIS.
		//
		// DEPOIS DO ed::End(), e isso nao e detalhe de arrumacao — e o que faz
		// a posicao sair certa.
		//
		//   Dentro do Begin/End o node-editor aplica a transformacao do canvas
		//   ao ImGui, entao ImGui::GetMousePos() JA devolve coordenada de
		//   canvas. Chamar ScreenToCanvas ali transforma duas vezes: o no
		//   nascia longe do cursor, e o erro crescia com o zoom e o pan.
		//
		//   Fora do Begin/End o mouse volta a ser coordenada de TELA, que e o
		//   que ScreenToCanvas espera. Mesmo lugar em que o Control Rig faz
		//   isto (rig_graph_canvas.cpp, depois do End dele).
		//
		// A guarda do fio HOVERED vem junto: sem ela, dois cliques rapidos
		// enquanto voce arruma um no caem num fio proximo e religam o grafo
		// pelas costas.
		{
			const int lid = (ed::GetHoveredLink().Get() != 0)
				? (int)ed::GetDoubleClickedLink().Get()
				: 0;

			const int idx = lid - 0x40000;
			const auto& ls = graph.GetLinks();

			if (idx >= 0 && idx < (int)ls.size())
			{
				// COPIA, nao referencia: AddLink e RemoveLinksTo mexem no
				// vetor, e uma referencia para o elemento viraria lixo no meio
				// da operacao.
				const AnimLink l = ls[idx];

				if (auto node = CreateAnimNode("Reroute"))
				{
					// AG3b — o reroute adota o tipo do fio.
					//
					// Um fio de dado (Get Float -> Alpha) precisa de um
					// reroute que se declare Float, senao ele nasce Pose, a
					// validacao de link recusa a segunda ponta e sobra um no
					// solto no meio do grafo.
					const AnimNode* src = graph.FindNode(l.FromNode);
					auto* rr = static_cast<AnimNode_Reroute*>(node.get());
					rr->SetPinType(src ? src->OutputType() : AnimPinType::Pose);
					rr->RebuildPins();

					ImVec2 pos = ed::ScreenToCanvas(ImGui::GetMousePos());

					// Centraliza o ponto no lugar clicado, em vez de pendurar
					// o canto superior esquerdo ali.
					pos.x -= kReroutePinSize;
					pos.y -= kReroutePinSize * 0.5f;

					node->Title = "Reroute";
					node->EditorX = pos.x;
					node->EditorY = pos.y;

					const int id = graph.AddNode(std::move(node));

					ed::SetNodePosition(id, pos);

					// A segunda ligacao SUBSTITUI o fio original: AddLink
					// remove o que estiver chegando no mesmo pino de destino,
					// e resolve o grafo sozinho ao final.
					graph.AddLink(l.FromNode, id, 0, l.Kind);
					graph.AddLink(id, l.ToNode, l.ToPin, l.Kind);

					m_SelectedNode = id;
					MarkEdited("Insert reroute");
				}
			}
		}

		// ── Selecao e duplo-clique (DEPOIS do End, como manda o node-editor) ──
		ed::NodeId sel = 0;

		if (ed::GetSelectedNodes(&sel, 1) > 0)
			m_SelectedNode = (int)sel.Get();
		else
			m_SelectedNode = -1;

		// Duplo-clique numa State Machine = entrar nela.
		ed::NodeId dbl = ed::GetDoubleClickedNode();

		if (dbl.Get() != 0)
		{
			AnimNode* n = graph.FindNode((int)dbl.Get());

			if (auto* sm = dynamic_cast<AnimNode_StateMachine*>(n))
			{
				// Grava as posicoes ANTES de sair do nivel — senao o arrasto que
				// voce acabou de fazer some ao voltar.
				for (const auto& nn : graph.GetNodes())
				{
					const ImVec2 pos = ed::GetNodePosition(nn->Id);
					nn->EditorX = pos.x;
					nn->EditorY = pos.y;
				}

				ed::SetCurrentEditor(nullptr);
				NavigateTo({ sm->Title.empty() ? "State Machine" : sm->Title, nullptr, sm,
					-1, sm->Id });
				return;
			}
		}

		// Guarda as posicoes todo frame: barato, e garante que um arrasto nunca
		// se perde — nem se voce fechar a janela sem salvar (o dirty avisa).
		//
		// AG3b — com a guarda de FLT_MAX.
		//
		// `ed::GetNodePosition` devolve FLT_MAX para um no que o node-editor
		// ainda NAO DESENHOU. Isso acontece de verdade: um no criado depois do
		// loop de desenho — pelo menu, ou pelo duplo-clique no fio — so vai ser
		// desenhado no frame seguinte, e este loop roda antes disso.
		//
		// Sem a guarda, EditorX/EditorY viravam FLT_MAX, e era esse valor que
		// ia para o .axeanim. O no reaparecia no infinito depois de salvar e
		// reabrir, e ninguem ligava o defeito a criacao do no, porque na tela
		// ele estava no lugar certo o tempo todo.
		for (const auto& n : graph.GetNodes())
		{
			const ImVec2 pos = ed::GetNodePosition(n->Id);

			if (pos.x >= FLT_MAX * 0.5f || pos.y >= FLT_MAX * 0.5f)
				continue;

			if (pos.x != n->EditorX || pos.y != n->EditorY)
			{
				n->EditorX = pos.x;
				n->EditorY = pos.y;

				// COM nome: o PendingUndo agrupa o arrasto inteiro num passo
				// so. E precisamente o caso que o agrupamento existe para
				// resolver — sem ele, seriam dezenas de comandos por gesto.
				MarkEdited("Move node");
			}
		}

		ed::SetCurrentEditor(nullptr);
	}

	// ── Paleta de nos (AG5) ──────────────────────────────────────────────────
	//
	// Reescrita para o mesmo formato do Control Rig e do Script Editor: busca
	// no topo com foco automatico, categorias coloridas colapsaveis, e a cor
	// do item ecoando a do no que ele cria.
	//
	// A versao anterior era uma lista corrida com separadores e rotulos
	// cinzas. Funcionava com dez nos; com os cinco de logica do AG4 ela passou
	// a exigir leitura linear para achar qualquer coisa — e era a unica das
	// tres janelas de grafo sem busca.
	void AnimGraphWindow::DrawNodePalette(AnimPoseGraph& graph)
	{
		// Foco automatico: o gesto vira "botao direito, digita", sem clicar no
		// campo. Mesmo comportamento das outras duas janelas.
		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();

		ImGui::InputTextWithHint("##animpalfilter", "Search node...",
			m_PaletteFilter, sizeof(m_PaletteFilter));

		ImGui::Separator();

		// ── Atalhos do topo ──────────────────────────────────────────────────
		//
		// Colar so aparece quando ha algo no clipboard: um item permanentemente
		// cinza vira ruido. E some quando ha busca ativa — quem digitou esta
		// procurando um no, e um "Paste" no meio do resultado nao pertence ali.
		// Mesma regra do Control Rig.
		if (!m_Clipboard.Nodes.empty() && m_PaletteFilter[0] == 0)
		{
			if (ImGui::MenuItem("Paste", "Ctrl+V"))
				PasteClipboard(&m_MenuOpenCanvasPos);

			ImGui::Separator();
		}

		struct Entry { const char* Label; const char* Type; };

		static const Entry kSources[] = {
			{ "Clip Player",        "ClipPlayer" },
			{ "Blend Space Player", "BlendSpacePlayer" },
			{ "State Machine",      "StateMachine" },
		};

		static const Entry kBlends[] = {
			{ "Blend by Float",         "BlendByFloat" },
			{ "Blend by Bool",          "BlendByBool" },
			{ "Layered Blend per Bone", "LayeredBlend" },
			{ "Apply Additive",         "ApplyAdditive" },
		};

		static const Entry kPostProcess[] = {
			{ "Control Rig", "ControlRig" },
		};

		static const Entry kLogic[] = {
			{ "Not",            "Not" },
			{ "And / Or / Xor", "BoolOp" },
			{ "Compare Float",  "CompareFloat" },
			{ "Select Float",   "SelectFloat" },
			{ "Float Math",     "FloatMath" },
		};

		static const Entry kOrganize[] = {
			{ "Reroute", "Reroute" },
		};

		// Criar e DESENHAR separados: o desenho mora no laco das categorias
		// (que precisa filtrar), e aqui fica so o que acontece no clique.
		auto spawn = [&](const char* type)
			{
				auto node = CreateAnimNode(type);

				if (!node)
					return;

				// AG3b — a posicao do CLIQUE DIREITO, gravada quando o menu
				// abriu. Reler o mouse aqui pegaria a posicao sobre o ITEM DE
				// MENU, que com a paleta alta fica bem longe do ponto pedido.
				const ImVec2 canvas = m_MenuOpenCanvasPos;

				node->Title = type;
				node->EditorX = canvas.x;
				node->EditorY = canvas.y;

				const int id = graph.AddNode(std::move(node));

				// Posiciona SO o no novo. Mexer na flag global de posicoes
				// reposicionaria todos e desfaria o arranjo ja montado.
				ed::SetNodePosition(id, canvas);

				m_SelectedNode = id;
				MarkEdited("Add node");

				m_PaletteFilter[0] = 0;
			};

		struct Cat
		{
			const char* Name;
			const Entry* Items;
			int          Count;
			ImVec4       Color;
		};

		// As cores ecoam a faixa do NO no canvas (ver StyleFor): a categoria de
		// onde voce tirou o no e a cor que ele tem na tela.
		//
		// IM_ARRAYSIZE e nao um numero digitado — a contagem a mao ja escondeu
		// uma entrada de menu neste projeto.
		const Cat kCats[] =
		{
			{ "Pose sources",  kSources,     IM_ARRAYSIZE(kSources),     ImVec4(0.18f, 0.42f, 0.60f, 1.0f) },
			{ "Blends",          kBlends,      IM_ARRAYSIZE(kBlends),      ImVec4(0.50f, 0.32f, 0.42f, 1.0f) },
			{ "Post-processing", kPostProcess, IM_ARRAYSIZE(kPostProcess), ImVec4(0.42f, 0.28f, 0.62f, 1.0f) },
			{ "Logic",          kLogic,       IM_ARRAYSIZE(kLogic),       ImVec4(0.58f, 0.44f, 0.16f, 1.0f) },
			{ "Organize",     kOrganize,    IM_ARRAYSIZE(kOrganize),    ImVec4(0.45f, 0.42f, 0.30f, 1.0f) },
		};

		static_assert(IM_ARRAYSIZE(kCats) <= 8,
			"m_PaletteOpen tem 8 posicoes; aumente o array se acrescentar categorias.");

		// Busca em minusculas dos dois lados. Sem isto "clip" nao acharia
		// "Clip Player" — e ninguem digita com a caixa certa.
		std::string needle = m_PaletteFilter;
		std::transform(needle.begin(), needle.end(), needle.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });

		const bool filtering = !needle.empty();

		const auto matches = [&](const char* label)
			{
				if (!filtering)
					return true;

				std::string low = label;
				std::transform(low.begin(), low.end(), low.begin(),
					[](unsigned char c) { return (char)std::tolower(c); });

				return low.find(needle) != std::string::npos;
			};

		int shown = 0;

		for (int ci = 0; ci < IM_ARRAYSIZE(kCats); ++ci)
		{
			const Cat& cat = kCats[ci];

			// Categoria sem nenhum item que case some inteira — filtrar e
			// mostrar cinco cabecalhos vazios nao ajuda ninguem.
			int hits = 0;

			for (int i = 0; i < cat.Count; ++i)
				if (matches(cat.Items[i].Label))
					++hits;

			if (!hits)
				continue;

			shown += hits;

			ImGui::PushStyleColor(ImGuiCol_Header, cat.Color);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
				ImVec4(cat.Color.x * 1.35f, cat.Color.y * 1.35f, cat.Color.z * 1.35f, 1.0f));

			// Filtrando, tudo abre: voce buscou para ver o resultado, nao para
			// abrir categoria por categoria.
			const bool open = filtering
				|| ImGui::CollapsingHeader(cat.Name,
					m_PaletteOpen[ci] ? ImGuiTreeNodeFlags_DefaultOpen : 0);

			// So memoriza quando NAO esta filtrando — senao a busca deixaria
			// todas as categorias abertas para sempre.
			if (!filtering)
				m_PaletteOpen[ci] = open;

			ImGui::PopStyleColor(2);

			if (!open)
				continue;

			ImGui::Indent(8.0f);

			for (int i = 0; i < cat.Count; ++i)
			{
				if (!matches(cat.Items[i].Label))
					continue;

				ImGui::PushStyleColor(ImGuiCol_Text, cat.Color);
				const bool clicked = ImGui::MenuItem(cat.Items[i].Label);
				ImGui::PopStyleColor();

				if (clicked)
					spawn(cat.Items[i].Type);
			}

			ImGui::Unindent(8.0f);
		}

		if (!shown)
			ImGui::TextDisabled("No node matching \"%s\".", m_PaletteFilter);
	}


	// ═════════════════════════════════════════════════════════════════════════
	//  CANVAS DA MAQUINA DE ESTADOS  (nivel 2)
	//
	//  Aqui a semantica muda: no = estado, link = TRANSICAO. A informacao
	//  importante nao vive no no — vive no link.
	// ═════════════════════════════════════════════════════════════════════════

	// ═════════════════════════════════════════════════════════════════════════
	//  MAQUINA DE ESTADOS — SM_UESTYLE_V1
	//
	//  A versao anterior desenhava transicao como ed::Link entre pinos
	//  "Entra"/"Sai". Com 4 estados isso ja era um emaranhado: todo fio nasce
	//  e morre no MESMO ponto do card, e bezier cruzando bezier nao diz nem
	//  DE ONDE nem PARA ONDE.
	//
	//  Agora e como na Unreal:
	//
	//    - estado e uma CAIXA compacta, sem pino nenhum;
	//    - transicao e uma SETA RETA de borda a borda; ida e volta correm em
	//      linhas PARALELAS (cada uma deslocada pra sua direita), entao
	//      Idle<->Walk sao duas setas lado a lado, nao um X;
	//    - no meio da seta vive um CIRCULO com a direcao — ELE e a transicao:
	//      clique seleciona (painel Detalhes), duplo-clique ABRE A REGRA,
	//      botao direito da o menu, Delete apaga;
	//    - criar transicao = arrastar da BORDA de um estado (a borda acende
	//      ao passar o mouse) e soltar em cima do outro. O menu de contexto
	//      do estado tambem cria, pra quem preferir descobrir por ali.
	//
	//  Truques de implementacao (nenhum e obvio, todos importam):
	//
	//    - o circulo da transicao e um NO do node-editor SEM pinos,
	//      re-ancorado com SetNodePosition TODO frame no meio da seta. E o
	//      que da selecao, duplo-clique, menu de contexto e Delete de graca —
	//      e impede o editor de tratar o clique como box-select do fundo;
	//    - as bordas sao 4 pinos finos com ed::PinRect explicito. O rect do
	//      no vem do frame ANTERIOR (GetNodeSize) — no primeiro frame ele e
	//      zero e os pinos so nascem no seguinte, imperceptivel;
	//    - enquanto um arrasto de transicao esta vivo, cada estado ganha um
	//      pino EXTRA cobrindo o corpo inteiro: soltar no MEIO do alvo
	//      funciona (como na Unreal). Fora do arrasto esse pino nao existe,
	//      senao ele roubaria o clique de MOVER o estado;
	//    - a seta e desenhada em DOIS segmentos com um vao no meio — o
	//      circulo senta no vao, entao nao importa em qual canal do splitter
	//      do node-editor o draw list caiu: nada risca por cima do icone.
	// ═════════════════════════════════════════════════════════════════════════

	// Ponto onde o segmento (from -> to) SAI do retangulo que contem `from`.
	// Liang-Barsky de saida: e o que faz a seta nascer na BORDA do card, nao
	// no centro dele.
	static ImVec2 RectExitPoint(const ImVec2& mn, const ImVec2& mx,
		const ImVec2& from, const ImVec2& to)
	{
		const float dx = to.x - from.x;
		const float dy = to.y - from.y;

		float t = 1.0f;

		if (dx > 0.0f)      t = std::min(t, (mx.x - from.x) / dx);
		else if (dx < 0.0f) t = std::min(t, (mn.x - from.x) / dx);

		if (dy > 0.0f)      t = std::min(t, (mx.y - from.y) / dy);
		else if (dy < 0.0f) t = std::min(t, (mn.y - from.y) / dy);

		if (t < 0.0f) t = 0.0f;

		return ImVec2(from.x + dx * t, from.y + dy * t);
	}

	static void DrawArrowHead(ImDrawList* dl, const ImVec2& tip,
		const ImVec2& dir, ImU32 col, float size)
	{
		const ImVec2 n(-dir.y, dir.x);
		const ImVec2 back(tip.x - dir.x * size, tip.y - dir.y * size);

		dl->AddTriangleFilled(tip,
			ImVec2(back.x + n.x * size * 0.55f, back.y + n.y * size * 0.55f),
			ImVec2(back.x - n.x * size * 0.55f, back.y - n.y * size * 0.55f),
			col);
	}

	void AnimGraphWindow::DrawStateMachineCanvas(AnimNode_StateMachine& sm)
	{
		ed::SetCurrentEditor(m_EdCtx);
		ed::Begin("StateMachine");

		if (!m_PositionsLoaded)
		{
			for (std::size_t i = 0; i < sm.States.size(); ++i)
				ed::SetNodePosition(StateNode((int)i), ImVec2(sm.States[i].EditorX, sm.States[i].EditorY));

			ed::SetNodePosition(kEntryNode, ImVec2(40.0f, 160.0f));
			ed::SetNodePosition(kAnyStateNode, ImVec2(40.0f, 20.0f));
			m_PositionsLoaded = true;
		}

		// Navegacoes decididas dentro do frame (duplo-clique, menu) sao
		// EXECUTADAS so no fim — depois de gravar as posicoes dos nos.
		int pendingStateNav = -1;
		int pendingRuleNav = -1;

		ImDrawList* dl = ImGui::GetWindowDrawList();

		// ── Pinos de borda / drop (PinRect explicito) ────────────────────────
		//
		// Chamados DENTRO do BeginNode do dono. Usam o rect do frame anterior.
		auto SubmitBorderPins = [](int nodeId, int pinIdBase)
			{
				const ImVec2 p = ed::GetNodePosition(nodeId);
				const ImVec2 s = ed::GetNodeSize(nodeId);

				if (s.x <= 0.0f || s.y <= 0.0f)
					return;

				const ImVec2 mn = p;
				const ImVec2 mx(p.x + s.x, p.y + s.y);
				const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);

				const float b = 9.0f;   // faixa pra dentro
				const float o = 4.0f;   // folga pra fora (facilita a pegada)

				const ImVec2 r[4][2] = {
					{ ImVec2(mn.x - o, mn.y - o), ImVec2(mx.x + o, mn.y + b) },   // topo
					{ ImVec2(mn.x - o, mx.y - b), ImVec2(mx.x + o, mx.y + o) },   // baixo
					{ ImVec2(mn.x - o, mn.y),     ImVec2(mn.x + b, mx.y) },       // esquerda
					{ ImVec2(mx.x - b, mn.y),     ImVec2(mx.x + o, mx.y) },       // direita
				};

				for (int side = 0; side < 4; ++side)
				{
					ed::BeginPin(pinIdBase + side, ed::PinKind::Output);
					ed::PinRect(r[side][0], r[side][1]);
					ed::PinPivotRect(c, c);   // o fio-preview nasce do centro
					ed::EndPin();
				}
			};

		auto SubmitDropPin = [](int nodeId, int pinId)
			{
				const ImVec2 p = ed::GetNodePosition(nodeId);
				const ImVec2 s = ed::GetNodeSize(nodeId);

				if (s.x <= 0.0f || s.y <= 0.0f)
					return;

				const ImVec2 c(p.x + s.x * 0.5f, p.y + s.y * 0.5f);

				ed::BeginPin(pinId, ed::PinKind::Input);
				ed::PinRect(ImVec2(p.x - 2.0f, p.y - 2.0f),
					ImVec2(p.x + s.x + 2.0f, p.y + s.y + 2.0f));
				ed::PinPivotRect(c, c);
				ed::EndPin();
			};

		// ── Caixas ───────────────────────────────────────────────────────────
		ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(12, 7, 12, 7));
		ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
		ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.4f);

		// Entry: caixinha verde com um "play". Arrastar da borda dele pra um
		// estado REAPONTA a entrada (nao cria transicao).
		{
			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.095f, 0.20f, 0.12f, 0.98f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.9f));

			ed::BeginNode(kEntryNode);

			const ImVec2 gp = ImGui::GetCursorScreenPos();
			const float  th = ImGui::GetTextLineHeight();

			dl->AddTriangleFilled(
				ImVec2(gp.x + 2.0f, gp.y + 2.0f),
				ImVec2(gp.x + 2.0f, gp.y + th - 2.0f),
				ImVec2(gp.x + 12.0f, gp.y + th * 0.5f),
				IM_COL32(110, 220, 130, 255));

			ImGui::Dummy(ImVec2(16.0f, th));
			ImGui::SameLine();
			ImGui::TextUnformatted("Entry");

			SubmitBorderPins(kEntryNode, kEntryBorderPin0);

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		// ── Any State (OPCIONAL) ─────────────────────────────────────────────
		//
		// Sem ele voce teria que arrastar uma seta de TODOS os estados ate
		// "Morrer" e "Levar dano". Mas ele so aparece se voce PEDIR (menu de
		// contexto) ou se ja existem transicoes partindo dele.
		bool hasAnyTransitions = false;

		for (const auto& tr : sm.Transitions)
			if (tr.From < 0) { hasAnyTransitions = true; break; }

		const bool drawAnyState = m_ShowAnyState || hasAnyTransitions;

		if (drawAnyState)
		{
			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.24f, 0.185f, 0.075f, 0.98f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.9f));

			ed::BeginNode(kAnyStateNode);

			const ImVec2 gp = ImGui::GetCursorScreenPos();
			const float  th = ImGui::GetTextLineHeight();
			const ImVec2 gc(gp.x + 7.0f, gp.y + th * 0.5f);

			dl->AddCircle(gc, 6.0f, IM_COL32(255, 196, 90, 255), 0, 1.6f);
			dl->AddCircleFilled(gc, 2.4f, IM_COL32(255, 196, 90, 255));

			ImGui::Dummy(ImVec2(17.0f, th));
			ImGui::SameLine();
			ImGui::TextUnformatted("Any State");

			SubmitBorderPins(kAnyStateNode, kAnyBorderPin0);

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		// ── Estados ──────────────────────────────────────────────────────────
		for (std::size_t i = 0; i < sm.States.size(); ++i)
		{
			auto& st = sm.States[i];

			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.155f, 0.165f, 0.195f, 0.98f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.9f));

			ed::BeginNode(StateNode((int)i));

			const ImVec2 gp = ImGui::GetCursorScreenPos();
			const float  th = ImGui::GetTextLineHeight();
			const ImVec2 gc(gp.x + 6.0f, gp.y + th * 0.5f);

			// Bolinha de "estado", como o iconezinho da UE.
			dl->AddCircleFilled(gc, 5.0f, IM_COL32(205, 208, 218, 255));
			dl->AddCircle(gc, 5.0f, IM_COL32(0, 0, 0, 160), 0, 1.2f);

			ImGui::Dummy(ImVec2(15.0f, th));
			ImGui::SameLine();
			ImGui::TextUnformatted(st.Name.c_str());

			// Largura minima: caixa muito curta e dificil de acertar.
			const float used = 15.0f + ImGui::CalcTextSize(st.Name.c_str()).x;

			if (used < 78.0f)
			{
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::Dummy(ImVec2(78.0f - used, 1.0f));
			}

			SubmitBorderPins(StateNode((int)i), StateBorderPin((int)i, 0));

			// So durante um arrasto vivo — ver comentario do cabecalho.
			if (m_WasCreatingLink)
				SubmitDropPin(StateNode((int)i), StateDropPin((int)i));

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		ed::PopStyleVar(3);

		// ── Feedback de borda ────────────────────────────────────────────────
		//
		// Mouse numa faixa de borda = anel azul no dono + cursor de mao. E o
		// aviso de "arrastar daqui cria transicao" — sem ele a borda e um
		// gesto invisivel.
		{
			const int hp = (int)ed::GetHoveredPin().Get();
			int ringNode = 0;

			if (hp >= kEntryBorderPin0 && hp < kEntryBorderPin0 + 4)      ringNode = kEntryNode;
			else if (hp >= kAnyBorderPin0 && hp < kAnyBorderPin0 + 4)     ringNode = kAnyStateNode;
			else if (hp >= 0x71000 && hp < 0x81000)                       ringNode = StateNode((hp - 0x71000) / 4);

			if (ringNode != 0)
			{
				const ImVec2 p = ed::GetNodePosition(ringNode);
				const ImVec2 s = ed::GetNodeSize(ringNode);

				if (s.x > 0.0f)
				{
					dl->AddRect(ImVec2(p.x - 3.0f, p.y - 3.0f),
						ImVec2(p.x + s.x + 3.0f, p.y + s.y + 3.0f),
						IM_COL32(120, 190, 255, 220), 8.0f, 0, 2.0f);
				}

				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			}
		}

		auto NodeRect = [](int id, ImVec2& mn, ImVec2& mx) -> bool
			{
				const ImVec2 p = ed::GetNodePosition(id);
				const ImVec2 s = ed::GetNodeSize(id);

				if (s.x <= 0.0f || s.y <= 0.0f)
					return false;

				mn = p;
				mx = ImVec2(p.x + s.x, p.y + s.y);
				return true;
			};

		// ── Seta do Entry ────────────────────────────────────────────────────
		if (sm.EntryState >= 0 && sm.EntryState < (int)sm.States.size())
		{
			ImVec2 emn, emx, smn, smx;

			if (NodeRect(kEntryNode, emn, emx) && NodeRect(StateNode(sm.EntryState), smn, smx))
			{
				const ImVec2 ce((emn.x + emx.x) * 0.5f, (emn.y + emx.y) * 0.5f);
				const ImVec2 cs((smn.x + smx.x) * 0.5f, (smn.y + smx.y) * 0.5f);

				const ImVec2 A = RectExitPoint(emn, emx, ce, cs);
				const ImVec2 B = RectExitPoint(smn, smx, cs, ce);

				const float dx = B.x - A.x, dy = B.y - A.y;
				const float len = std::sqrt(dx * dx + dy * dy);

				if (len > 4.0f)
				{
					const ImVec2 dir(dx / len, dy / len);
					const ImU32  col = IM_COL32(88, 205, 112, 255);

					dl->AddLine(A, ImVec2(B.x - dir.x * 9.0f, B.y - dir.y * 9.0f), col, 2.4f);
					DrawArrowHead(dl, B, dir, col, 11.0f);
				}
			}
		}

		// ── Geometria das transicoes ─────────────────────────────────────────
		struct TGeom
		{
			ImVec2 A, B, Dir, Mid;
			ImU32  Col = 0;
			bool   Valid = false;
		};

		std::vector<TGeom> geo(sm.Transitions.size());

		const int hoveredNodeId = (int)ed::GetHoveredNode().Get();

		for (std::size_t t = 0; t < sm.Transitions.size(); ++t)
		{
			const auto& tr = sm.Transitions[t];

			if (tr.To < 0 || tr.To >= (int)sm.States.size())
				continue;

			ImVec2 fmn, fmx, tmn, tmx;

			const bool okFrom = (tr.From < 0)
				? (drawAnyState && NodeRect(kAnyStateNode, fmn, fmx))
				: (tr.From < (int)sm.States.size() && NodeRect(StateNode(tr.From), fmn, fmx));

			if (!okFrom || !NodeRect(StateNode(tr.To), tmn, tmx))
				continue;

			const ImVec2 ca((fmn.x + fmx.x) * 0.5f, (fmn.y + fmx.y) * 0.5f);
			const ImVec2 cb((tmn.x + tmx.x) * 0.5f, (tmn.y + tmx.y) * 0.5f);

			float dx = cb.x - ca.x, dy = cb.y - ca.y;
			float len = std::sqrt(dx * dx + dy * dy);

			if (len < 4.0f)
				continue;

			ImVec2 dir(dx / len, dy / len);
			const ImVec2 n(-dir.y, dir.x);

			// Faixa da seta: cada transicao anda pra SUA direita. A->B e B->A
			// caem em lados opostos automaticamente — e o paralelismo da UE.
			// Duplicatas na MESMA direcao ganham faixas extras.
			int lane = 0;

			for (std::size_t t2 = 0; t2 < t; ++t2)
				if (sm.Transitions[t2].From == tr.From && sm.Transitions[t2].To == tr.To)
					++lane;

			float off = 13.0f + 16.0f * (float)lane;

			const float half = std::min(
				std::min((fmx.x - fmn.x), (tmx.x - tmn.x)) * 0.5f,
				std::min((fmx.y - fmn.y), (tmx.y - tmn.y)) * 0.5f) - 4.0f;

			off = std::min(off, std::max(6.0f, half));

			const ImVec2 pa(ca.x + n.x * off, ca.y + n.y * off);
			const ImVec2 pb(cb.x + n.x * off, cb.y + n.y * off);

			const ImVec2 A = RectExitPoint(fmn, fmx, pa, pb);
			const ImVec2 B = RectExitPoint(tmn, tmx, pb, pa);

			dx = B.x - A.x; dy = B.y - A.y;
			len = std::sqrt(dx * dx + dy * dy);

			if (len < 2.0f)
				continue;

			dir = ImVec2(dx / len, dy / len);

			bool hasTrigger = false;

			for (const auto& c : tr.Conditions)
				if (c.Op == AnimCompare::TriggerSet)
					hasTrigger = true;

			ImU32 col = hasTrigger ? IM_COL32(255, 178, 64, 235)
				: IM_COL32(196, 198, 208, 235);

			if (hoveredNodeId == TransIconNode((int)t)) col = IM_COL32(255, 255, 255, 255);
			if (m_SelectedTransition == (int)t)         col = IM_COL32(255, 200, 90, 255);

			auto& g = geo[t];
			g.A = A;
			g.B = B;
			g.Dir = dir;
			g.Mid = ImVec2((A.x + B.x) * 0.5f, (A.y + B.y) * 0.5f);
			g.Col = col;
			g.Valid = true;

			// A seta: dois segmentos com o vao do icone no meio.
			const float thick = (m_SelectedTransition == (int)t) ? 2.8f : 2.0f;
			const float gap = 15.0f;
			const ImVec2 tipBack(B.x - dir.x * 9.0f, B.y - dir.y * 9.0f);

			if (len > 2.0f * gap + 24.0f)
			{
				dl->AddLine(A, ImVec2(g.Mid.x - dir.x * gap, g.Mid.y - dir.y * gap), col, thick);
				dl->AddLine(ImVec2(g.Mid.x + dir.x * gap, g.Mid.y + dir.y * gap), tipBack, col, thick);
			}
			else
			{
				dl->AddLine(A, tipBack, col, thick);
			}

			DrawArrowHead(dl, B, dir, col, 11.0f);
		}

		// ── Icones de transicao (nos sem pinos, ancorados todo frame) ────────
		ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
		ed::PushStyleVar(ed::StyleVar_NodeRounding, 11.0f);
		ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.6f);

		for (std::size_t t = 0; t < sm.Transitions.size(); ++t)
		{
			const auto& g = geo[t];

			if (!g.Valid)
				continue;

			const bool sel = (m_SelectedTransition == (int)t);
			const bool hov = (hoveredNodeId == TransIconNode((int)t));

			const ImVec4 bg = sel ? ImVec4(0.95f, 0.66f, 0.20f, 1.0f)
				: hov ? ImVec4(0.30f, 0.32f, 0.38f, 1.0f)
				: ImVec4(0.14f, 0.15f, 0.18f, 1.0f);

			ed::PushStyleColor(ed::StyleColor_NodeBg, bg);
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImGui::ColorConvertU32ToFloat4(g.Col));

			ed::SetNodePosition(TransIconNode((int)t), ImVec2(g.Mid.x - 11.0f, g.Mid.y - 11.0f));
			ed::BeginNode(TransIconNode((int)t));

			const ImVec2 gp = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(22.0f, 22.0f));

			// Setinha interna apontando a DIRECAO — e o que resolve "essa
			// transicao vai ou volta?" sem seguir a linha com o olho.
			const ImVec2 c(gp.x + 11.0f, gp.y + 11.0f);
			const ImVec2 d = g.Dir;
			const ImVec2 nn(-d.y, d.x);

			const ImU32 gcol = sel ? IM_COL32(28, 22, 10, 255) : g.Col;

			dl->AddTriangleFilled(
				ImVec2(c.x + d.x * 5.0f, c.y + d.y * 5.0f),
				ImVec2(c.x - d.x * 4.0f + nn.x * 4.2f, c.y - d.y * 4.0f + nn.y * 4.2f),
				ImVec2(c.x - d.x * 4.0f - nn.x * 4.2f, c.y - d.y * 4.0f - nn.y * 4.2f),
				gcol);

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		ed::PopStyleVar(3);

		// ── Criar transicao (arrasto de borda) ───────────────────────────────
		//
		// Qualquer pino decodifica de volta pro DONO. Entry re-aponta a
		// entrada; Any State cria transicao com From = -1.
		auto PinOwner = [](int id, bool& isEntry, bool& isAny) -> int
			{
				isEntry = false;
				isAny = false;

				if (id >= kEntryBorderPin0 && id < kEntryBorderPin0 + 4) { isEntry = true; return -2; }
				if (id >= kAnyBorderPin0 && id < kAnyBorderPin0 + 4) { isAny = true;   return -1; }
				if (id >= 0x71000 && id < 0x81000)                        return (id - 0x71000) / 4;
				if (id >= 0x81000 && id < 0x91000)                        return id - 0x81000;

				return -3;
			};

		const bool creating = ed::BeginCreate(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), 2.5f);

		if (creating)
		{
			ed::PinId a, b;

			if (ed::QueryNewLink(&a, &b))
			{
				bool aEntry = false, aAny = false, bEntry = false, bAny = false;

				const int from = PinOwner((int)a.Get(), aEntry, aAny);
				const int to = PinOwner((int)b.Get(), bEntry, bAny);

				const bool toIsState = (to >= 0) && !bEntry && !bAny;

				if (aEntry)
				{
					// Arrastar do ENTRY nao cria transicao: REAPONTA a entrada.
					if (toIsState)
					{
						if (ed::AcceptNewItem(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), 3.0f))
						{
							sm.EntryState = to;
							MarkEdited();
						}
					}
					else
					{
						ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
					}
				}
				else
				{
					// Auto-transicao sem exit time trava o personagem no frame
					// 0 pra sempre. Quase sempre e engano de arrasto.
					const bool valid = toIsState && (aAny || (from >= 0 && from != to));

					if (!valid)
					{
						ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
					}
					else if (ed::AcceptNewItem(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), 3.0f))
					{
						AnimTransition tr;
						tr.From = aAny ? -1 : from;
						tr.To = to;
						tr.Duration = 0.2f;

						sm.Transitions.push_back(tr);
						m_SelectedTransition = (int)sm.Transitions.size() - 1;
						m_SelectedState = -1;
						MarkEdited();
					}
				}
			}
		}
		ed::EndCreate();

		m_WasCreatingLink = creating;

		// ── Apagar (Delete) ──────────────────────────────────────────────────
		if (ed::BeginDelete())
		{
			ed::NodeId nid;

			while (ed::QueryDeletedNode(&nid))
			{
				const int id = (int)nid.Get();

				// Icone de transicao selecionado + Delete = apagar a transicao.
				if (id >= 0x51000 && id < 0x61000)
				{
					if (!ed::AcceptDeletedItem())
						continue;

					const int t = id - 0x51000;

					if (t >= 0 && t < (int)sm.Transitions.size())
					{
						sm.Transitions.erase(sm.Transitions.begin() + t);
						m_SelectedTransition = -1;
						MarkEdited();
					}

					continue;
				}

				if (id == kAnyStateNode || id == kEntryNode)
				{
					ed::RejectDeletedItem();   // nao sao estados
					continue;
				}

				if (!ed::AcceptDeletedItem())
					continue;

				const int idx = id - 0x01000;

				if (idx < 0 || idx >= (int)sm.States.size())
					continue;

				// REINDEXA as transicoes sobreviventes.
				//
				// Sem isto, apagar o estado 2 faria toda transicao que apontava
				// pro 3 passar a apontar pro estado errado — em silencio. E o
				// bug so apareceria em runtime, como "o personagem vai pro
				// estado errado as vezes".
				sm.States.erase(sm.States.begin() + idx);

				sm.Transitions.erase(
					std::remove_if(sm.Transitions.begin(), sm.Transitions.end(),
						[idx](const AnimTransition& t)
						{
							return t.From == idx || t.To == idx;
						}),
					sm.Transitions.end());

				for (auto& t : sm.Transitions)
				{
					if (t.From > idx) --t.From;
					if (t.To > idx)   --t.To;
				}

				if (sm.EntryState == idx)      sm.EntryState = 0;
				else if (sm.EntryState > idx)  --sm.EntryState;

				m_SelectedState = -1;
				m_SelectedTransition = -1;
				m_PositionsLoaded = false;   // os indices mudaram
				MarkEdited();
			}
		}
		ed::EndDelete();

		// ── Menus de contexto ────────────────────────────────────────────────
		ed::Suspend();

		ed::NodeId ctxNode;

		if (ed::ShowNodeContextMenu(&ctxNode))
		{
			m_CtxNodeId = (int)ctxNode.Get();
			ImGui::OpenPopup("sm_node_ctx");
		}

		if (ed::ShowBackgroundContextMenu())
		{
			m_MenuOpenCanvasPos = ed::ScreenToCanvas(ImGui::GetMousePos());
			ImGui::OpenPopup("add_state");
		}

		if (ImGui::BeginPopup("sm_node_ctx"))
		{
			const int id = m_CtxNodeId;

			if (id >= 0x51000 && id < 0x61000)          // ── transicao
			{
				const int t = id - 0x51000;

				if (t >= 0 && t < (int)sm.Transitions.size())
				{
					if (ImGui::MenuItem("Edit rule"))
						pendingRuleNav = t;

					if (ImGui::MenuItem("Delete transition"))
					{
						sm.Transitions.erase(sm.Transitions.begin() + t);
						m_SelectedTransition = -1;
						MarkEdited();
					}
				}
			}
			else if (id == kAnyStateNode)               // ── Any State
			{
				if (ImGui::BeginMenu("Create transition to"))
				{
					for (std::size_t j = 0; j < sm.States.size(); ++j)
					{
						if (ImGui::MenuItem(sm.States[j].Name.c_str()))
						{
							AnimTransition tr;
							tr.From = -1;
							tr.To = (int)j;
							tr.Duration = 0.2f;

							sm.Transitions.push_back(tr);
							m_SelectedTransition = (int)sm.Transitions.size() - 1;
							m_SelectedState = -1;
							MarkEdited();
						}
					}

					ImGui::EndMenu();
				}
			}
			else if (id >= 0x01000 && id < 0x11000)     // ── estado
			{
				const int i = id - 0x01000;

				if (i >= 0 && i < (int)sm.States.size())
				{
					if (ImGui::BeginMenu("Create transition to"))
					{
						for (std::size_t j = 0; j < sm.States.size(); ++j)
						{
							if ((int)j == i)
								continue;

							if (ImGui::MenuItem(sm.States[j].Name.c_str()))
							{
								AnimTransition tr;
								tr.From = i;
								tr.To = (int)j;
								tr.Duration = 0.2f;

								sm.Transitions.push_back(tr);
								m_SelectedTransition = (int)sm.Transitions.size() - 1;
								m_SelectedState = -1;
								MarkEdited();
							}
						}

						ImGui::EndMenu();
					}

					if (sm.EntryState != i && ImGui::MenuItem("Make entry state"))
					{
						sm.EntryState = i;
						MarkEdited();
					}

					ImGui::Separator();

					if (ImGui::MenuItem("Open sub-graph"))
						pendingStateNav = i;
				}
			}

			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("add_state"))
		{
			// Any State e opt-in. Com transicoes partindo dele, esconder
			// deixaria setas sem origem — dai o item trava ligado.
			{
				bool anyLocked = false;

				for (const auto& tr : sm.Transitions)
					if (tr.From < 0) { anyLocked = true; break; }

				const bool anyVisible = m_ShowAnyState || anyLocked;

				if (ImGui::MenuItem("Any State", nullptr, anyVisible, !anyLocked))
					m_ShowAnyState = !anyVisible;

				if (anyLocked && ImGui::IsItemHovered())
					ImGui::SetTooltip("There are transitions leaving Any State.\nDelete them before hiding it.");

				ImGui::Separator();
			}

			if (ImGui::MenuItem("New State"))
			{
				AnimSmState st;
				st.Name = "State " + std::to_string(sm.States.size());

				// Nasce onde o mouse ABRIU o menu — que e onde voce esta
				// olhando. (GetMousePos na hora do clique ja se moveu pro
				// proprio menu.)
				st.EditorX = m_MenuOpenCanvasPos.x;
				st.EditorY = m_MenuOpenCanvasPos.y;

				// O estado nasce com um SUB-GRAFO utilizavel: ClipPlayer ->
				// Output, ja ligados. Um sub-grafo vazio obrigaria o usuario a
				// entrar nele, criar um Output, criar um player e ligar os
				// dois — quatro passos antes de ver qualquer coisa.
				const int outId = st.Graph.AddNode(CreateAnimNode("Output"));
				st.Graph.SetOutputNode(outId);

				AnimNode* o = st.Graph.FindNode(outId);
				o->Title = "Output Animation Pose";
				o->EditorX = 420.0f;
				o->EditorY = 150.0f;

				const int cpId = st.Graph.AddNode(CreateAnimNode("ClipPlayer"));
				AnimNode* cp = st.Graph.FindNode(cpId);
				cp->Title = "Clip Player";
				cp->EditorX = 120.0f;
				cp->EditorY = 150.0f;

				st.Graph.AddLink(cpId, outId, 0, AnimLinkKind::Pose);

				const int newIdx = (int)sm.States.size();
				sm.States.push_back(std::move(st));

				// Posiciona SO o no novo — nao marca m_PositionsLoaded=false,
				// que reposicionaria TODOS os nos e embaralharia o que voce ja
				// arrumou.
				ed::SetNodePosition(StateNode(newIdx),
					ImVec2(sm.States[newIdx].EditorX, sm.States[newIdx].EditorY));

				m_SelectedState = newIdx;
				m_SelectedTransition = -1;

				MarkEdited();
			}

			ImGui::EndPopup();
		}

		ed::Resume();
		ed::End();

		// ── Selecao ──────────────────────────────────────────────────────────
		ed::NodeId selNode = 0;

		if (ed::GetSelectedNodes(&selNode, 1) > 0)
		{
			const int id = (int)selNode.Get();

			if (id >= 0x01000 && id < 0x11000)
			{
				m_SelectedState = id - 0x01000;
				m_SelectedTransition = -1;
			}
			else if (id >= 0x51000 && id < 0x61000)
			{
				m_SelectedTransition = id - 0x51000;
				m_SelectedState = -1;
			}
		}

		// ── Duplo-clique ─────────────────────────────────────────────────────
		//
		// Estado = abrir o sub-grafo. Icone de transicao = abrir a REGRA.
		{
			const ed::NodeId dbl = ed::GetDoubleClickedNode();
			const int id = (int)dbl.Get();

			if (id >= 0x01000 && id < 0x11000)
				pendingStateNav = id - 0x01000;
			else if (id >= 0x51000 && id < 0x61000)
				pendingRuleNav = id - 0x51000;
		}

		// Grava as posicoes TODO frame — barato, e um arrasto nunca se perde.
		//
		// AG3b — mesma guarda de FLT_MAX do grafo de poses. O estado criado
		// pelo menu "add_state" so e desenhado no frame seguinte, e ate la
		// GetNodePosition devolve FLT_MAX. Sem a guarda, era esse valor que ia
		// para o .axeanim.
		for (std::size_t i = 0; i < sm.States.size(); ++i)
		{
			const ImVec2 pos = ed::GetNodePosition(StateNode((int)i));

			if (pos.x >= FLT_MAX * 0.5f || pos.y >= FLT_MAX * 0.5f)
				continue;

			if (pos.x != sm.States[i].EditorX || pos.y != sm.States[i].EditorY)
			{
				sm.States[i].EditorX = pos.x;
				sm.States[i].EditorY = pos.y;
				MarkEdited();
			}
		}

		// ── Navegacoes pendentes (posicoes ja gravadas acima) ────────────────
		if (pendingStateNav >= 0 && pendingStateNav < (int)sm.States.size())
		{
			ed::SetCurrentEditor(nullptr);
			NavigateTo({ sm.States[pendingStateNav].Name, &sm.States[pendingStateNav].Graph, nullptr,
				-1, -1, pendingStateNav });
			return;
		}

		if (pendingRuleNav >= 0 && pendingRuleNav < (int)sm.Transitions.size())
		{
			const auto& tr = sm.Transitions[pendingRuleNav];

			const std::string fromName = (tr.From < 0) ? "Any State"
				: (tr.From < (int)sm.States.size() ? sm.States[tr.From].Name : "?");

			const std::string toName = (tr.To >= 0 && tr.To < (int)sm.States.size())
				? sm.States[tr.To].Name : "?";

			ed::SetCurrentEditor(nullptr);
			NavigateTo({ fromName + " -> " + toName + " (rule)", nullptr, &sm, pendingRuleNav,
				m_Nav.back().SmNodeId });
			return;
		}

		ed::SetCurrentEditor(nullptr);
	}

	// ═════════════════════════════════════════════════════════════════════════
	//  GRAFO DA REGRA — o "Crouch to Idle (rule)" da Unreal
	//
	//  Um nivel navegavel proprio: nos vermelhos de condicao alimentando o no
	//  Result ("Can Enter Transition"). As condicoes continuam sendo DADOS
	//  (AnimCondition), nao nos compilaveis — este canvas e a APRESENTACAO
	//  delas: selecionar um no edita a condicao no painel Detalhes, botao
	//  direito no fundo adiciona, Delete remove.
	//
	//  Posicoes aqui sao por SESSAO (nao persistem no .axeanim): sao poucos
	//  nos e o layout automatico ja e legivel.
	// ═════════════════════════════════════════════════════════════════════════
	void AnimGraphWindow::DrawTransitionRuleCanvas(AnimNode_StateMachine& sm, int transIndex)
	{
		// A transicao pode ter sido apagada por outro caminho — sobe um nivel.
		if (transIndex < 0 || transIndex >= (int)sm.Transitions.size())
		{
			NavigateUpTo((int)m_Nav.size() - 2);
			return;
		}

		auto& tr = sm.Transitions[transIndex];
		const auto& params = m_Asset->GetParameters();

		ed::SetCurrentEditor(m_EdCtx);
		ed::Begin("TransitionRule");

		if (!m_PositionsLoaded)
		{
			ed::SetNodePosition(kRuleResultNode, ImVec2(560.0f, 160.0f));
			ed::SetNodePosition(kRuleExitNode, ImVec2(150.0f, 30.0f));

			for (std::size_t c = 0; c < tr.Conditions.size(); ++c)
				ed::SetNodePosition(CondNode((int)c), ImVec2(150.0f, 130.0f + (float)c * 100.0f));

			m_PositionsLoaded = true;
		}

		ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 5));
		ed::PushStyleVar(ed::StyleVar_NodeRounding, 5.0f);
		ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);

		ImDrawList* dl = ImGui::GetWindowDrawList();

		static const char* kOpLabels[] = { ">", ">=", "<", "<=", "==", "!=", "is true", "is false", "disparou" };

		// ── Result ───────────────────────────────────────────────────────────
		{
			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.085f, 0.085f, 0.095f, 0.96f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

			ed::BeginNode(kRuleResultNode);

			const float w = NodeWidthFor("Result", "Can Enter Transition");

			DrawNodeHeader("Result", nullptr, ImVec4(0.33f, 0.33f, 0.38f, 1.0f), w);

			DrawPinIcon(kBoolColor, !tr.Conditions.empty() || tr.HasExitTime, false);
			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY()
				+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::TextUnformatted("Can Enter Transition");

			ImGui::Dummy(ImVec2(w, 1.0f));

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		// ── Condicoes ────────────────────────────────────────────────────────
		char buf[96];

		for (std::size_t c = 0; c < tr.Conditions.size(); ++c)
		{
			const auto& cond = tr.Conditions[c];
			const char* pname = cond.Parameter.empty() ? "(parameter?)" : cond.Parameter.c_str();
			const int   op = (int)cond.Op;

			if (cond.Op <= AnimCompare::NotEqual)
				std::snprintf(buf, sizeof(buf), "%s %s %.2f", pname, kOpLabels[op], cond.Value);
			else
				std::snprintf(buf, sizeof(buf), "%s %s", pname, kOpLabels[op]);

			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.085f, 0.085f, 0.095f, 0.96f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

			ed::BeginNode(CondNode((int)c));

			const float w = NodeWidthFor("Condition", buf);

			DrawNodeHeader("Condition", nullptr, ImVec4(0.50f, 0.18f, 0.18f, 1.0f), w);

			const ImVec2 bodyStart = ImGui::GetCursorScreenPos();

			ImGui::SetCursorScreenPos(ImVec2(bodyStart.x + 8.0f, bodyStart.y));
			ImGui::SetCursorPosY(ImGui::GetCursorPosY()
				+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::TextUnformatted(buf);

			// Pino decorativo de saida, encostado na direita.
			ImGui::SetCursorScreenPos(ImVec2(bodyStart.x + w - kPinIconSize - 6.0f, bodyStart.y));
			DrawPinIcon(kBoolColor, true, false);

			ImGui::Dummy(ImVec2(w, 1.0f));

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		// ── Exit Time (informativo — edita no Detalhes) ──────────────────────
		if (tr.HasExitTime)
		{
			std::snprintf(buf, sizeof(buf), "after %.2f of the cycle", tr.ExitTime);

			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.085f, 0.085f, 0.095f, 0.96f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

			ed::BeginNode(kRuleExitNode);

			const float w = NodeWidthFor("Exit Time", buf);

			DrawNodeHeader("Exit Time", nullptr, ImVec4(0.16f, 0.32f, 0.50f, 1.0f), w);

			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
			ImGui::TextUnformatted(buf);
			ImGui::Dummy(ImVec2(w, 1.0f));

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		ed::PopStyleVar(3);

		// ── Fios (retos, com seta — vermelho = bool, azul = tempo) ───────────
		{
			auto NodeRect = [](int id, ImVec2& mn, ImVec2& mx) -> bool
				{
					const ImVec2 p = ed::GetNodePosition(id);
					const ImVec2 s = ed::GetNodeSize(id);

					if (s.x <= 0.0f || s.y <= 0.0f)
						return false;

					mn = p;
					mx = ImVec2(p.x + s.x, p.y + s.y);
					return true;
				};

			ImVec2 rmn, rmx;

			if (NodeRect(kRuleResultNode, rmn, rmx))
			{
				const ImVec2 rc((rmn.x + rmx.x) * 0.5f, (rmn.y + rmx.y) * 0.5f);

				auto DrawWire = [&](int fromNodeId, ImU32 col)
					{
						ImVec2 cmn, cmx;

						if (!NodeRect(fromNodeId, cmn, cmx))
							return;

						const ImVec2 cc((cmn.x + cmx.x) * 0.5f, (cmn.y + cmx.y) * 0.5f);

						const ImVec2 A = RectExitPoint(cmn, cmx, cc, rc);
						const ImVec2 B = RectExitPoint(rmn, rmx, rc, cc);

						const float dx = B.x - A.x, dy = B.y - A.y;
						const float len = std::sqrt(dx * dx + dy * dy);

						if (len < 4.0f)
							return;

						const ImVec2 dir(dx / len, dy / len);

						dl->AddLine(A, ImVec2(B.x - dir.x * 8.0f, B.y - dir.y * 8.0f), col, 2.0f);
						DrawArrowHead(dl, B, dir, col, 10.0f);
					};

				for (std::size_t c = 0; c < tr.Conditions.size(); ++c)
					DrawWire(CondNode((int)c), IM_COL32(214, 92, 92, 235));

				if (tr.HasExitTime)
					DrawWire(kRuleExitNode, IM_COL32(96, 156, 214, 235));
			}
		}

		// ── Apagar condicao (Delete) ─────────────────────────────────────────
		if (ed::BeginDelete())
		{
			ed::NodeId nid;

			while (ed::QueryDeletedNode(&nid))
			{
				const int id = (int)nid.Get();

				if (id >= 0x61000 && id < 0x71000)
				{
					if (!ed::AcceptDeletedItem())
						continue;

					const int c = id - 0x61000;

					if (c >= 0 && c < (int)tr.Conditions.size())
					{
						tr.Conditions.erase(tr.Conditions.begin() + c);
						m_SelectedCondition = -1;
						m_PositionsLoaded = false;   // re-arruma o layout
						MarkEdited();
					}
				}
				else
				{
					ed::RejectDeletedItem();   // Result e Exit Time ficam
				}
			}
		}
		ed::EndDelete();

		// ── Menus ────────────────────────────────────────────────────────────
		ed::Suspend();

		ed::NodeId ctxNode;

		if (ed::ShowNodeContextMenu(&ctxNode))
		{
			m_CtxNodeId = (int)ctxNode.Get();
			ImGui::OpenPopup("rule_node_ctx");
		}

		if (ed::ShowBackgroundContextMenu())
		{
			m_MenuOpenCanvasPos = ed::ScreenToCanvas(ImGui::GetMousePos());
			ImGui::OpenPopup("rule_bg_ctx");
		}

		if (ImGui::BeginPopup("rule_node_ctx"))
		{
			const int id = m_CtxNodeId;

			if (id >= 0x61000 && id < 0x71000)
			{
				const int c = id - 0x61000;

				if (c < (int)tr.Conditions.size() && ImGui::MenuItem("Delete condition"))
				{
					tr.Conditions.erase(tr.Conditions.begin() + c);
					m_SelectedCondition = -1;
					m_PositionsLoaded = false;
					MarkEdited();
				}
			}

			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("rule_bg_ctx"))
		{
			if (ImGui::MenuItem("+ Condition"))
			{
				AnimCondition c;

				if (!params.empty())
					c.Parameter = params[0].Name;

				tr.Conditions.push_back(c);

				const int newIdx = (int)tr.Conditions.size() - 1;

				ed::SetNodePosition(CondNode(newIdx), m_MenuOpenCanvasPos);
				m_SelectedCondition = newIdx;
				MarkEdited();
			}

			ImGui::EndPopup();
		}

		ed::Resume();
		ed::End();

		// ── Selecao ──────────────────────────────────────────────────────────
		ed::NodeId selNode = 0;

		if (ed::GetSelectedNodes(&selNode, 1) > 0)
		{
			const int id = (int)selNode.Get();

			m_SelectedCondition = (id >= 0x61000 && id < 0x71000) ? (id - 0x61000) : -1;
		}

		ed::SetCurrentEditor(nullptr);
	}


	// ═════════════════════════════════════════════════════════════════════════
	//  PAINEL DE DETALHES
	//
	//  Muda conforme a selecao. Num grafo de poses, a informacao mora no NO;
	//  numa maquina de estados, mora na TRANSICAO. Por isso o painel nao pode
	//  ser um so.
	// ═════════════════════════════════════════════════════════════════════════

	void AnimGraphWindow::DrawDetailsPanel()
	{
		const NavEntry& top = m_Nav.back();

		if (top.Graph)
		{
			if (m_SelectedNode > 0)
			{
				if (AnimNode* n = top.Graph->FindNode(m_SelectedNode))
				{
					DrawNodeDetails(*n);
					return;
				}
			}

			ImGui::TextDisabled("Select a node.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextWrapped(
				"This is a POSE GRAPH. Links carry poses (white) or "
				"values (green/red).\n\n"
				"The state machine is a NODE in here - double-click it "
				"to edit its states.\n\n"
				"It is what will later let you plug a Two Bone IK between it and the "
				"Output.");
			return;
		}

		// ── Nivel de REGRA (SM_UESTYLE_V1) ───────────────────────────────────
		//
		// No de condicao selecionado -> edita SO aquela condicao. Nada
		// selecionado -> as opcoes da transicao inteira (como a aba Details
		// da Unreal com a transicao aberta).
		if (top.Sm && top.TransIndex >= 0)
		{
			if (top.TransIndex >= (int)top.Sm->Transitions.size())
				return;

			auto& tr = top.Sm->Transitions[top.TransIndex];

			if (m_SelectedCondition >= 0 && m_SelectedCondition < (int)tr.Conditions.size())
			{
				ImGui::TextUnformatted("Condition");
				ImGui::Separator();

				int removeCond = -1;

				DrawConditionRow(tr, (std::size_t)m_SelectedCondition, removeCond);

				ImGui::Spacing();

				if (ImGui::Button("Delete condition", ImVec2(-1, 0)))
					removeCond = m_SelectedCondition;

				if (removeCond >= 0)
				{
					tr.Conditions.erase(tr.Conditions.begin() + removeCond);
					m_SelectedCondition = -1;
					m_PositionsLoaded = false;   // re-arruma os nos da regra
					MarkEdited();
				}

				return;
			}

			DrawTransitionDetails(*top.Sm, top.TransIndex);
			return;
		}

		if (top.Sm)
		{
			if (m_SelectedTransition >= 0 && m_SelectedTransition < (int)top.Sm->Transitions.size())
			{
				DrawTransitionDetails(*top.Sm, m_SelectedTransition);
				return;
			}

			if (m_SelectedState >= 0 && m_SelectedState < (int)top.Sm->States.size())
			{
				DrawStateDetails(*top.Sm, m_SelectedState);
				return;
			}

			ImGui::TextDisabled("Select a state or a transition.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextWrapped(
				"State = what plays. Each holds a pose SUB-GRAPH "
				"(double-click to open).\n\n"
				"Transition = when to switch. Drag from a state's EDGE to "
				"another to create it; the circle mid-arrow IS the transition "
				"(click selects, double-click opens the condition's RULE).\n\n"
				"A whole locomotion fits in ONE state with a Blend Space. Do not make "
				"idle/walk/run as three states - the transitions between them pop "
				"in the middle of an acceleration.");
		}
	}

	void AnimGraphWindow::DrawNodeDetails(AnimNode& node)
	{
		ImGui::TextUnformatted(node.TypeName());
		ImGui::Separator();

		char title[64];
		std::snprintf(title, sizeof(title), "%s", node.Title.c_str());

		if (ImGui::InputText("Title", title, sizeof(title)))
		{
			node.Title = title;
			MarkEdited();
		}

		ImGui::Spacing();

		// ── Clip Player ──────────────────────────────────────────────────────
		if (auto* cp = dynamic_cast<AnimNode_ClipPlayer*>(&node))
		{
			const char* cur = cp->ClipName.empty() ? "(none)" : cp->ClipName.c_str();

			if (ImGui::BeginCombo("Clip", cur))
			{
				if (ImGui::Selectable("(none)", cp->ClipName.empty()))
				{
					cp->ClipName.clear();
					cp->Clip.reset();
					MarkEdited();
				}

				if (m_Skeleton)
				{
					// Deduplica por NOME. O idle.fbx da Mixamo traz 3 takes com o
					// mesmo nome (idle, idle_1, idle_2 aparecem repetidos) — sem
					// deduplicar, o dropdown vira uma lista de repetidos onde
					// voce nao sabe qual escolher. O primeiro com cada nome vence.
					std::set<std::string> seen;

					for (const auto& c : m_Skeleton->GetClips())
					{
						if (!c) continue;

						const std::string& name = c->GetName();
						if (!seen.insert(name).second) continue;   // ja apareceu

						if (ImGui::Selectable(name.c_str(), cp->ClipName == name))
						{
							cp->ClipName = name;
							cp->Clip = c;      // resolve na hora: o preview responde ja
							MarkEdited();
						}
					}
				}

				ImGui::EndCombo();
			}

			if (!m_Skeleton)
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No skeleton - no clips.");

			if (ImGui::DragFloat("Speed", &cp->PlayRate, 0.05f, -3.0f, 3.0f, "%.2fx")) MarkEdited();
			if (ImGui::Checkbox("Loop", &cp->Loop)) MarkEdited();
			return;
		}

		// ── Blend Space Player ───────────────────────────────────────────────
		if (auto* bs = dynamic_cast<AnimNode_BlendSpacePlayer*>(&node))
		{
			ImGui::TextWrapped("A whole locomotion in one node. The value comes from the pin "
				"'Speed' - wire a variable node into it.");
			ImGui::Spacing();

			if (ImGui::DragFloat("Speed", &bs->PlayRate, 0.05f, 0.0f, 3.0f, "%.2fx")) MarkEdited();

			ImGui::Separator();
			ImGui::TextDisabled("Samples");

			int removeIdx = -1;

			for (std::size_t i = 0; i < bs->Samples.size(); ++i)
			{
				ImGui::PushID((int)i);

				ImGui::TextUnformatted(bs->Samples[i].first.c_str());
				ImGui::SameLine(150.0f);

				ImGui::SetNextItemWidth(70.0f);
				if (ImGui::DragFloat("##v", &bs->Samples[i].second, 1.0f)) MarkEdited();

				ImGui::SameLine();
				if (ImGui::SmallButton("x")) removeIdx = (int)i;

				ImGui::PopID();
			}

			if (removeIdx >= 0)
			{
				bs->Samples.erase(bs->Samples.begin() + removeIdx);
				MarkEdited();
			}

			if (m_Skeleton && ImGui::BeginCombo("+ sample", "add..."))
			{
				std::set<std::string> seen;

				for (const auto& c : m_Skeleton->GetClips())
				{
					if (!c) continue;
					if (!seen.insert(c->GetName()).second) continue;

					if (ImGui::Selectable(c->GetName().c_str()))
					{
						bs->Samples.push_back({ c->GetName(), 0.0f });
						MarkEdited();
					}
				}

				ImGui::EndCombo();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("e.g. idle=0, walk=150, run=400");
			return;
		}

		// ── Nos de variavel ──────────────────────────────────────────────────
		if (auto* gf = dynamic_cast<AnimNode_GetFloat*>(&node))
		{
			// Dropdown dos parametros DECLARADOS, nao texto livre.
			//
			// Um nome digitado errado le 0 e nunca dispara nada — sem erro, sem
			// aviso. E o bug mais frustrante de um AnimGraph.
			if (ImGui::BeginCombo("Parameter", gf->Parameter.c_str()))
			{
				for (const auto& p : m_Asset->GetParameters())
				{
					if (p.Type != AnimParamType::Float && p.Type != AnimParamType::Int)
						continue;

					if (ImGui::Selectable(p.Name.c_str(), p.Name == gf->Parameter))
					{
						gf->Parameter = p.Name;
						gf->Title = p.Name;
						MarkEdited();
					}
				}

				ImGui::EndCombo();
			}
			return;
		}

		if (auto* gb = dynamic_cast<AnimNode_GetBool*>(&node))
		{
			if (ImGui::BeginCombo("Parameter", gb->Parameter.c_str()))
			{
				for (const auto& p : m_Asset->GetParameters())
				{
					if (p.Type != AnimParamType::Bool)
						continue;

					if (ImGui::Selectable(p.Name.c_str(), p.Name == gb->Parameter))
					{
						gb->Parameter = p.Name;
						gb->Title = p.Name;
						MarkEdited();
					}
				}

				ImGui::EndCombo();
			}
			return;
		}

		// ── Blends ───────────────────────────────────────────────────────────
		if (auto* bf = dynamic_cast<AnimNode_BlendByFloat*>(&node))
		{
			ImGui::TextWrapped("Alpha = Min -> pose A.  Alpha = Max -> pose B.");
			if (ImGui::DragFloat("Min", &bf->MinValue, 0.5f)) MarkEdited();
			if (ImGui::DragFloat("Max", &bf->MaxValue, 0.5f)) MarkEdited();
			return;
		}

		if (auto* bb = dynamic_cast<AnimNode_BlendByBool*>(&node))
		{
			if (ImGui::DragFloat("Blend Time", &bb->BlendTime, 0.01f, 0.0f, 2.0f, "%.2fs")) MarkEdited();

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("0 = hard cut. A bool that makes the pose jump is the\n"
					"the most common flaw in weapon/crouch rigs.");
			return;
		}

		if (auto* lb = dynamic_cast<AnimNode_LayeredBlend*>(&node))
		{
			ImGui::TextWrapped("Runs with the legs AND shoots with the arms. The mask "
				"runs from the root bone down the hierarchy.");
			ImGui::Spacing();

			char bone[64];
			std::snprintf(bone, sizeof(bone), "%s", lb->RootBone.c_str());

			if (ImGui::InputText("Root bone", bone, sizeof(bone)))
			{
				lb->RootBone = bone;
				lb->Reset();       // forca reconstruir a mascara
				MarkEdited();
			}

			if (ImGui::DragInt("Feather", &lb->FeatherBones, 0.1f, 0, 8))
			{
				lb->Reset();
				MarkEdited();
			}

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Smooths the boundary across N bones.\n"
					"Without feather, the seam becomes a hard crease mid-back.");

			// A lista de ossos do personagem, pra nao ter que digitar de cabeca.
			if (m_Skeleton && m_Skeleton->GetSkeleton() &&
				ImGui::BeginCombo("Pick bone", "..."))
			{
				const auto& bones = m_Skeleton->GetSkeleton()->GetBones();

				for (const auto& b : bones)
				{
					if (ImGui::Selectable(b.Name.c_str()))
					{
						lb->RootBone = b.Name;
						lb->Reset();
						MarkEdited();
					}
				}

				ImGui::EndCombo();
			}
			return;
		}

		if (auto* cr = dynamic_cast<AnimNode_ControlRig*>(&node))
		{
			ImGui::TextWrapped("Runs a Control Rig (.axerig) ON TOP of the incoming pose. "
				"The rig's Forwards Solve processes the bones (foot IK, look-at, "
				"procedural fixes) and the corrected pose continues down the graph.");
			ImGui::Spacing();

			// Nome do rig escolhido (UUID -> nome legivel) pro combo.
			std::string current = "(none)";
			if (!cr->RigUUID.empty())
			{
				if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(cr->RigUUID))
					current = rec->Name.empty() ? rec->FilePath.stem().string() : rec->Name;
				else
					current = "(missing)";
			}

			if (ImGui::BeginCombo("Control Rig", current.c_str()))
			{
				// "(none)" desliga o no — a pose passa intacta.
				if (ImGui::Selectable("(none)", cr->RigUUID.empty()))
				{
					cr->RigUUID.clear();
					cr->ResolveRig(m_Skeleton ? m_Skeleton->GetSkeleton().get() : nullptr, "editor");
					MarkEdited();
				}

				// Lista os .axerig do projeto. Filtra por EXTENSAO (a verdade
				// no disco), nao por record.Type, que pode estar velho pra
				// assets registrados antes do tipo ControlRig existir.
				for (const auto& kv : AssetDatabase::Get().GetAll())
				{
					const AssetRecord& rec = kv.second;
					if (rec.FilePath.extension() != ".axerig")
						continue;

					const std::string label = rec.Name.empty()
						? rec.FilePath.stem().string() : rec.Name;

					if (ImGui::Selectable(label.c_str(), rec.UUID == cr->RigUUID))
					{
						cr->RigUUID = rec.UUID;
						cr->ResolveRig(m_Skeleton ? m_Skeleton->GetSkeleton().get() : nullptr, "editor");   // carrega ja, pro preview refletir na hora
						MarkEdited();
					}
				}

				ImGui::EndCombo();
			}

			if (cr->RigUUID.empty())
				ImGui::TextDisabled("Pick an .axerig for this node to do anything.");
			else if (!cr->RigAsset)
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
					"The rig did not load. Save/reopen the graph or check the file.");

			ImGui::Spacing();

			// ── O CHECKBOX QUE FALTAVA ───────────────────────────────────
			//
			// Ver a nota longa em AnimNode_ControlRig::ControlsFollowAnimation.
			// Resumo: desligado, os controles ficam no repouso enquanto os
			// ossos vao para a animacao, e todo IK que mira num controle passa
			// a mirar na T-pose. O sintoma e postura torta na animacao que mais
			// se afasta do repouso — em geral a de mira.
			if (ImGui::Checkbox("Controls follow the animation",
				&cr->ControlsFollowAnimation))
				MarkEdited();

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Leva cada controle ate o osso que ele representa antes do\n"
					"solve, preservando o offset autorado (o pole vector fica no\n"
					"lugar).\n\n"
					"Desligado, um Two Bone IK mira no controle parado na bind\n"
					"pose: a perna vai atras, o quadril compensa e o tronco sai\n"
					"torto. Quanto mais a animacao se afasta do repouso, pior.\n\n"
					"So desligue num rig cujos controles sao alvos ABSOLUTOS no\n"
					"mundo (uma mao presa a um objeto fixo).");

			if (!cr->ControlsFollowAnimation)
				ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
					"Off: IK aims at bind-pose controls.");

			ImGui::Spacing();
			ImGui::TextDisabled("The Alpha pin controls the intensity (0..1).");
			return;
		}

		if (dynamic_cast<AnimNode_StateMachine*>(&node))
		{
			ImGui::TextWrapped("Double-click the node to edit its states.");
			return;
		}

		if (dynamic_cast<AnimNode_Output*>(&node))
		{
			ImGui::TextWrapped("The character's final pose. Whatever reaches here is "
				"what goes to the skin cache.");
			return;
		}

		// ── Nos logicos (AG4) ────────────────────────────────────────────────
		if (auto* bo = dynamic_cast<AnimNode_BoolOp*>(&node))
		{
			static const char* kOps[] = { "AND", "OR", "XOR" };
			int op = (int)bo->Operation;

			if (ImGui::Combo("Operation", &op, kOps, (int)std::size(kOps)))
			{
				bo->Operation = (AnimNode_BoolOp::Op)op;
				MarkEdited("Change operator");
			}

			ImGui::Spacing();
			ImGui::TextDisabled("AND: both. OR: either. XOR: exactly one.");
			return;
		}

		if (auto* cf = dynamic_cast<AnimNode_CompareFloat*>(&node))
		{
			static const char* kOps[] = { "A > B", "A < B", "A >= B", "A <= B",
										  "A == B", "A != B" };
			int op = (int)cf->Operation;

			if (ImGui::Combo("Comparison", &op, kOps, (int)std::size(kOps)))
			{
				cf->Operation = (AnimNode_CompareFloat::Op)op;
				MarkEdited("Change comparison");
			}

			// A tolerancia so significa alguma coisa nos dois operadores de
			// igualdade; mostrar sempre sugeriria que ela afeta o "maior que".
			const bool usesTol = (cf->Operation == AnimNode_CompareFloat::Op::Equal
				|| cf->Operation == AnimNode_CompareFloat::Op::NotEqual);

			if (usesTol)
			{
				if (ImGui::DragFloat("Tolerance", &cf->Tolerance, 0.001f, 0.0f, 1.0f, "%.4f"))
					MarkEdited("Change tolerance");

				ImGui::TextDisabled("Exact float comparison almost never works:\n"
					"a value that 'should' be 1.0 is usually 0.99999994.");
			}

			return;
		}

		if (auto* sf = dynamic_cast<AnimNode_SelectFloat*>(&node))
		{
			if (ImGui::DragFloat("Blend (s)", &sf->BlendTime, 0.01f, 0.0f, 2.0f, "%.2f"))
				MarkEdited("Change blend time");

			ImGui::Spacing();
			ImGui::TextWrapped("Picks between the two values based on the condition. "
				"The blend avoids the snap of switching in a single frame; "
				"zero switches instantly.");
			return;
		}

		if (auto* fm = dynamic_cast<AnimNode_FloatMath*>(&node))
		{
			static const char* kOps[] = { "A + B", "A - B", "A * B", "A / B",
										  "Min(A, B)", "Max(A, B)" };
			int op = (int)fm->Operation;

			if (ImGui::Combo("Operation", &op, kOps, (int)std::size(kOps)))
			{
				fm->Operation = (AnimNode_FloatMath::Op)op;
				MarkEdited("Change operator");
			}

			if (fm->Operation == AnimNode_FloatMath::Op::Divide)
				ImGui::TextDisabled("B equal to zero returns A, not infinity.");

			return;
		}

		if (dynamic_cast<AnimNode_Not*>(&node))
		{
			ImGui::TextWrapped("Inverts the incoming boolean.");
			return;
		}

		// AG3 — o reroute cairia no "(no parameters)" generico, que e tecnicamente
		// verdade e praticamente inutil: quem clica num pontinho quer saber o que
		// aquilo e.
		if (dynamic_cast<AnimNode_Reroute*>(&node))
		{
			ImGui::TextWrapped("A node on the wire. The pose goes in and comes out unchanged — it only serves "
				"to bend the path and straighten out the graph.");
			ImGui::Spacing();
			ImGui::TextDisabled("Delete removes the point; the wires on both sides detach.");
			return;
		}

		ImGui::TextDisabled("(no parameters)");
	}

	void AnimGraphWindow::DrawStateDetails(AnimNode_StateMachine& sm, int index)
	{
		auto& st = sm.States[index];

		ImGui::TextUnformatted("State");
		ImGui::Separator();

		char name[64];
		std::snprintf(name, sizeof(name), "%s", st.Name.c_str());

		if (ImGui::InputText("Name", name, sizeof(name)))
		{
			st.Name = name;
			MarkEdited();
		}

		if (sm.EntryState == index)
		{
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "ENTRY state");
		}
		else if (ImGui::Button("Make entry state", ImVec2(-1, 0)))
		{
			sm.EntryState = index;
			MarkEdited();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextWrapped("What this state PLAYS lives in its sub-graph. "
			"Double-click the node to open it.");

		if (ImGui::Button("Open sub-graph", ImVec2(-1, 0)))
			NavigateTo({ st.Name, &st.Graph, nullptr, -1, -1, index });
	}

	// Uma linha do editor de condicoes: [parametro] [operador] [valor] [x].
	// Extraida pra ser reusada pelo Detalhes da transicao (lista completa) e
	// pelo Detalhes do nivel de REGRA (uma condicao so, a selecionada).
	void AnimGraphWindow::DrawConditionRow(AnimTransition& tr, std::size_t c, int& removeCond)
	{
		const auto& params = m_Asset->GetParameters();

		ImGui::PushID((int)c);

		auto& cond = tr.Conditions[c];

		// Dropdown dos parametros declarados. Digitar o nome a mao produz o
		// pior bug possivel: le 0 e nunca dispara, sem erro nenhum.
		ImGui::SetNextItemWidth(90.0f);

		if (ImGui::BeginCombo("##p", cond.Parameter.empty() ? "param" : cond.Parameter.c_str()))
		{
			for (const auto& p : params)
			{
				if (!ImGui::Selectable(p.Name.c_str(), p.Name == cond.Parameter))
					continue;

				cond.Parameter = p.Name;

				// Ajusta o operador ao tipo: comparar ">" um Trigger nao faz
				// sentido, e o usuario nao deveria ter que saber disso.
				if (p.Type == AnimParamType::Trigger)   cond.Op = AnimCompare::TriggerSet;
				else if (p.Type == AnimParamType::Bool) cond.Op = AnimCompare::IsTrue;

				MarkEdited();
			}

			ImGui::EndCombo();
		}

		ImGui::SameLine();

		const char* ops[] = { ">", ">=", "<", "<=", "==", "!=", "is true", "is false", "disparou" };
		int op = (int)cond.Op;

		ImGui::SetNextItemWidth(78.0f);
		if (ImGui::Combo("##o", &op, ops, 9)) { cond.Op = (AnimCompare)op; MarkEdited(); }

		if (cond.Op <= AnimCompare::NotEqual)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(60.0f);
			if (ImGui::DragFloat("##v", &cond.Value, 0.5f)) MarkEdited();
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("x")) removeCond = (int)c;

		ImGui::PopID();
	}

	void AnimGraphWindow::DrawTransitionDetails(AnimNode_StateMachine& sm, int index)
	{
		auto& tr = sm.Transitions[index];

		ImGui::TextUnformatted("Transition");
		ImGui::Separator();

		const char* fromName = (tr.From < 0) ? "Any State"
			: (tr.From < (int)sm.States.size() ? sm.States[tr.From].Name.c_str() : "?");

		const char* toName = (tr.To >= 0 && tr.To < (int)sm.States.size())
			? sm.States[tr.To].Name.c_str() : "?";

		ImGui::Text("%s  ->  %s", fromName, toName);

		// Botao pra ENTRAR na regra (so quando ainda nao estamos nela — senao
		// empilharia um segundo nivel de regra identico).
		if (m_Nav.back().TransIndex < 0 &&
			ImGui::Button("Open rule graph", ImVec2(-1, 0)))
		{
			ed::SetCurrentEditor(nullptr);
			NavigateTo({ std::string(fromName) + " -> " + toName + " (rule)",
				nullptr, &sm, index, m_Nav.back().SmNodeId });
			return;
		}

		ImGui::Spacing();

		if (ImGui::DragFloat("Duration", &tr.Duration, 0.01f, 0.0f, 2.0f, "%.2fs")) MarkEdited();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Crossfade. 0 = hard cut.");

		if (ImGui::DragInt("Priority", &tr.Priority, 0.1f, 0, 100)) MarkEdited();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("If two transitions hold on the same frame,\nthe higher priority wins.");

		ImGui::Spacing();

		if (ImGui::Checkbox("Exit Time", &tr.HasExitTime)) MarkEdited();

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"Only allows the transition after the state completed X of its cycle.\n\n"
				"It is what keeps an attack from being cut on frame one: without\n"
				"that, 'attack -> idle when still' fires immediately and\n"
				"the strike never lands.");
		}

		if (tr.HasExitTime)
		{
			if (ImGui::SliderFloat("##exit", &tr.ExitTime, 0.0f, 1.0f, "%.2f of the cycle")) MarkEdited();
		}

		if (tr.From < 0)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Any State ignores exit time - damage and death");
			ImGui::TextDisabled("must interrupt right away.");

			if (ImGui::Checkbox("Can retrigger on the same state", &tr.CanRetriggerSelf)) MarkEdited();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("Conditions (ALL must pass)");

		if (tr.Conditions.empty() && !tr.HasExitTime)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No condition and no exit time:");
			ImGui::TextWrapped("this transition fires on frame one, always - "
				"the source state will never be seen.");
		}

		const auto& params = m_Asset->GetParameters();

		int removeCond = -1;

		for (std::size_t c = 0; c < tr.Conditions.size(); ++c)
			DrawConditionRow(tr, c, removeCond);

		if (removeCond >= 0)
		{
			tr.Conditions.erase(tr.Conditions.begin() + removeCond);
			MarkEdited();
		}

		if (ImGui::Button("+ Condition", ImVec2(-1, 0)))
		{
			AnimCondition c;
			if (!params.empty()) c.Parameter = params[0].Name;

			tr.Conditions.push_back(c);
			MarkEdited();
		}
	}

} // namespace axe