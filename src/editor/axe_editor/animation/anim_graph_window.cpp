#include "anim_graph_window.hpp"
#include "axe/log/log.hpp"
#include "axe/asset/asset_database.hpp"

#include <imgui_internal.h>   // DockBuilder — so pro layout padrao, uma vez

#include <utilities/widgets.h>   // ax::Widgets::Icon — mesmo pino do Material/Script
#include <utilities/drawing.h>

#include <algorithm>
#include <cctype>
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
			return { ImVec4(0.62f, 0.45f, 0.13f, 1.0f), "SAIDA" };

		if (dynamic_cast<const AnimNode_StateMachine*>(&node))
			return { ImVec4(0.42f, 0.28f, 0.62f, 1.0f), "MAQUINA DE ESTADOS" };

		if (node.OutputType() != AnimPinType::Pose)
			return { ImVec4(0.24f, 0.52f, 0.24f, 1.0f), "VARIAVEL" };

		if (dynamic_cast<const AnimNode_ClipPlayer*>(&node) ||
			dynamic_cast<const AnimNode_BlendSpacePlayer*>(&node))
			return { ImVec4(0.18f, 0.42f, 0.60f, 1.0f), "FONTE DE POSE" };

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

	// Pino no estilo Blueprint: SETA (Flow) para pose — o fluxo principal —
	// e CIRCULO para dado. Cheio = ligado, vazado = livre. Forma + cor:
	// legivel ate pra quem nao distingue as cores.
	static void DrawPinIcon(const ImVec4& color, bool connected, bool isPose)
	{
		ax::Widgets::Icon(
			ImVec2(kPinIconSize, kPinIconSize),
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

	void AnimGraphWindow::MarkEdited()
	{
		m_Dirty = true;
		++m_EditSerial;
		m_LastEditTime = ImGui::GetTime();
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
		// editor, os arquivos do ANIMGRAPH_UX_V6 estao em uso (cena segue o
		// grafo ao salvar; drop de FBX vira Clip Player; .axeskel revincula).
		AXE_EDITOR_INFO("AnimGraph editor — ANIMGRAPH_UX_V6");

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
	}

	void AnimGraphWindow::NavigateTo(const NavEntry& entry)
	{
		m_Nav.push_back(entry);
		m_NeedsContextReset = true;
		m_PositionsLoaded = false;
		m_SelectedNode = -1;
		m_SelectedState = -1;
		m_SelectedTransition = -1;
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

			AXE_EDITOR_INFO("AnimGraph '{}' salvo.", m_Asset->GetName());
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
		ImGui::Begin("Parametros##anim");
		DrawParametersPanel();
		ImGui::End();

		ImGui::Begin("Grafo##anim");
		{
			NavEntry& top = m_Nav.back();

			if (top.Graph)
				DrawPoseGraphCanvas(*top.Graph);
			else if (top.Sm)
				DrawStateMachineCanvas(*top.Sm);
		}
		ImGui::End();

		ImGui::Begin("Detalhes##anim");
		DrawDetailsPanel();
		ImGui::End();

		DrawPreviewWindow();
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

		ImGui::DockBuilderDockWindow("Parametros##anim", left);
		ImGui::DockBuilderDockWindow("Grafo##anim", center);
		ImGui::DockBuilderDockWindow("Detalhes##anim", right);
		ImGui::DockBuilderDockWindow("Preview##anim", rightBottom);

		ImGui::DockBuilderFinish(dockspaceId);
	}

	void AnimGraphWindow::DrawToolbar()
	{
		if (ImGui::Button("Salvar"))
			Save();

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		const NavEntry& top = m_Nav.back();

		if (top.Graph)
			ImGui::TextDisabled("botao direito no fundo = adicionar no  |  duplo-clique numa State Machine = entrar nela");
		else
			ImGui::TextDisabled("duplo-clique num estado = abrir o sub-grafo dele  |  arraste entre os pinos = transicao");
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
				ImGui::TextDisabled(">");
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
		ImGui::TextUnformatted("Parametros");
		ImGui::SameLine();
		ImGui::TextDisabled("(arraste para o grafo)");
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

				if (ImGui::MenuItem("Renomear"))
				{
					m_RenamingParam = (int)i;
					m_RenameBuf[0] = '\0';
				}

				if (ImGui::MenuItem("Excluir"))
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

		if (ImGui::Button("+ Parametro", ImVec2(-1, 0)))
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
				ImGui::TextDisabled("(pulso)");
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

					AXE_EDITOR_INFO("AnimGraph '{}' vinculado ao esqueleto '{}' ({} clipe(s)). Salve para persistir.",
						m_Asset->GetName(), skel->GetName(), skel->GetClips().size());
				}
				else
				{
					AXE_EDITOR_ERROR("Nao consegui resolver o esqueleto '{}'.", rec->Name);
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
						AXE_EDITOR_INFO("'{}': {} takes importados — os demais estao no combo de clipes.",
							rec->Name, added);
				}
				else
				{
					AXE_EDITOR_WARN("'{}' nao trouxe nenhum clipe compativel com o esqueleto.", rec->Name);
				}
			}
			else if (rec && isAnimFile && !m_Skeleton)
			{
				AXE_EDITOR_WARN("O grafo esta sem esqueleto — arraste o .axeskel do personagem pro canvas primeiro.");
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

					AXE_EDITOR_INFO("'{}' solto sobre '{}': escolha o clipe no painel Detalhes.",
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

					AXE_EDITOR_INFO("Clip Player criado a partir de '{}'. Escolha o clipe no painel Detalhes.",
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
					AXE_EDITOR_WARN("Parametro '{}' e {}: ainda nao existe no Get para este tipo.",
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
			else if (isSm) { accent = ImVec4(0.20f, 0.23f, 0.33f, 1.0f); sub = "State Machine  (duplo-clique)"; }
			else if (isValue) { accent = ImVec4(0.13f, 0.29f, 0.16f, 1.0f); sub = "Variavel"; }
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
						// destino e pino de dado: a origem NAO pode ser pose
						valid = (src->OutputType() != AnimPinType::Pose)
							&& pB < (int)dst->DataInputs.size();
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
					MarkEdited();
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
						MarkEdited();
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
					MarkEdited();
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
				NavigateTo({ sm->Title.empty() ? "State Machine" : sm->Title, nullptr, sm });
				return;
			}
		}

		// Guarda as posicoes todo frame: barato, e garante que um arrasto nunca
		// se perde — nem se voce fechar a janela sem salvar (o dirty avisa).
		for (const auto& n : graph.GetNodes())
		{
			const ImVec2 pos = ed::GetNodePosition(n->Id);

			if (pos.x != n->EditorX || pos.y != n->EditorY)
			{
				n->EditorX = pos.x;
				n->EditorY = pos.y;
				MarkEdited();
			}
		}

		ed::SetCurrentEditor(nullptr);
	}

	void AnimGraphWindow::DrawNodePalette(AnimPoseGraph& graph)
	{
		ImGui::TextDisabled("Adicionar no");
		ImGui::Separator();

		struct Entry { const char* Label; const char* Type; };

		// Agrupado por FUNCAO, e nao alfabeticamente: quem procura "como misturo
		// duas animacoes" procura em Blend, nao em B.
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

		static const Entry kValues[] = {
			{ "Get Float (parametro)", "GetFloat" },
			{ "Get Bool (parametro)",  "GetBool" },
		};

		auto emit = [&](const Entry* list, int count)
			{
				for (int i = 0; i < count; ++i)
				{
					if (!ImGui::MenuItem(list[i].Label))
						continue;

					auto node = CreateAnimNode(list[i].Type);

					if (!node)
						continue;

					node->Title = list[i].Label;

					const ImVec2 mouse = ImGui::GetMousePos();
					const ImVec2 canvas = ed::ScreenToCanvas(mouse);
					node->EditorX = canvas.x;
					node->EditorY = canvas.y;

					const int id = graph.AddNode(std::move(node));

					ed::SetNodePosition(id, canvas);
					MarkEdited();
				}
			};

		ImGui::TextDisabled("Fontes de pose");
		emit(kSources, 3);

		ImGui::Separator();
		ImGui::TextDisabled("Blends");
		emit(kBlends, 4);

		ImGui::Separator();
		ImGui::TextDisabled("Variaveis");
		emit(kValues, 2);
	}


	// ═════════════════════════════════════════════════════════════════════════
	//  CANVAS DA MAQUINA DE ESTADOS  (nivel 2)
	//
	//  Aqui a semantica muda: no = estado, link = TRANSICAO. A informacao
	//  importante nao vive no no — vive no link.
	// ═════════════════════════════════════════════════════════════════════════

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

		// ANIMGRAPH_STYLE_V2 — mesmo card do grafo de poses: corpo escuro e
		// borda do proprio node-editor, header pintado por cima.
		ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 5));
		ed::PushStyleVar(ed::StyleVar_NodeRounding, 5.0f);
		ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);

		// ── Entry (estilo Unreal) ────────────────────────────────────────────
		//
		// A primeira pergunta de quem abre uma maquina de estados e "onde o
		// personagem comeca?". A resposta e uma SETA, nao uma cor: o Entry
		// aponta o estado inicial, e arrastar dele pra outro estado troca a
		// entrada.
		{
			const ImVec4 entryAccent{ 0.14f, 0.30f, 0.19f, 1.0f };

			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.085f, 0.085f, 0.095f, 0.96f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

			ed::BeginNode(kEntryNode);

			const float w = NodeWidthFor("Entry", nullptr) - 60.0f;   // card curto

			DrawNodeHeader("Entry", nullptr, entryAccent, w);

			const ImVec2 bodyStart = ImGui::GetCursorScreenPos();
			const float contentW = ImGui::CalcTextSize("Sai").x + kPinIconSize + 6.0f;

			ImGui::SetCursorScreenPos(ImVec2(
				bodyStart.x + w - contentW - 8.0f, bodyStart.y));

			ed::BeginPin(kEntryOut, ed::PinKind::Output);
			ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
			ed::PinPivotSize(ImVec2(0, 0));

			ImGui::BeginGroup();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY()
				+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::TextUnformatted("Sai");
			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY()
				- (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
			DrawPinIcon(kPoseColor, true, true);
			ImGui::EndGroup();

			ed::EndPin();

			ImGui::Dummy(ImVec2(w, 1.0f));

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		// ── Any State (OPCIONAL) ─────────────────────────────────────────────
		//
		// Sem ele voce teria que arrastar uma seta de TODOS os estados ate
		// "Morrer" e "Levar dano". Mas ele so aparece se voce PEDIR (menu de
		// contexto) ou se ja existem transicoes partindo dele — por padrao o
		// canvas e limpo, como na Unreal.
		bool hasAnyTransitions = false;

		for (const auto& tr : sm.Transitions)
			if (tr.From < 0) { hasAnyTransitions = true; break; }

		const bool drawAnyState = m_ShowAnyState || hasAnyTransitions;

		const ImVec4 anyAccent{ 0.38f, 0.28f, 0.11f, 1.0f };

		if (drawAnyState)
		{
			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.085f, 0.085f, 0.095f, 0.96f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

			ed::BeginNode(kAnyStateNode);
			{
				const char* anySub = "dispara de qualquer estado";
				const float w = NodeWidthFor("Any State", anySub);

				DrawNodeHeader("Any State", anySub, anyAccent, w);

				const ImVec2 bodyStart = ImGui::GetCursorScreenPos();
				const float contentW = ImGui::CalcTextSize("Sai").x + kPinIconSize + 6.0f;

				ImGui::SetCursorScreenPos(ImVec2(
					bodyStart.x + w - contentW - 8.0f, bodyStart.y));

				ed::BeginPin(kAnyStateOut, ed::PinKind::Output);
				ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
				ed::PinPivotSize(ImVec2(0, 0));

				ImGui::BeginGroup();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY()
					+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
				ImGui::TextUnformatted("Sai");
				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY()
					- (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
				DrawPinIcon(kPoseColor, true, true);
				ImGui::EndGroup();

				ed::EndPin();

				ImGui::Dummy(ImVec2(w, 1.0f));
			}
			ed::EndNode();
			ed::PopStyleColor(2);
		}

		// ── Estados ──────────────────────────────────────────────────────────
		for (std::size_t i = 0; i < sm.States.size(); ++i)
		{
			auto& st = sm.States[i];

			// Quem diz onde o personagem comeca e a SETA do Entry, como na
			// Unreal — nao uma cor especial no estado.
			const ImVec4 accent{ 0.21f, 0.22f, 0.26f, 1.0f };   // cinza UE
			const char* sub = "sub-grafo (duplo-clique)";

			const float w = NodeWidthFor(st.Name.c_str(), sub);

			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.085f, 0.085f, 0.095f, 0.96f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

			ed::BeginNode(StateNode((int)i));

			DrawNodeHeader(st.Name.c_str(), sub, accent, w);

			const ImVec2 bodyStart = ImGui::GetCursorScreenPos();

			// Entra (esquerda)
			ed::BeginPin(StateIn((int)i), ed::PinKind::Input);
			ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
			ed::PinPivotSize(ImVec2(0, 0));

			ImGui::BeginGroup();
			DrawPinIcon(kPoseColor, true, true);
			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY()
				+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::TextUnformatted("Entra");
			ImGui::EndGroup();

			ed::EndPin();

			// Sai (direita, mesma linha)
			{
				const float contentW = ImGui::CalcTextSize("Sai").x + kPinIconSize + 6.0f;

				ImGui::SetCursorScreenPos(ImVec2(
					bodyStart.x + w - contentW - 8.0f, bodyStart.y));

				ed::BeginPin(StateOut((int)i), ed::PinKind::Output);
				ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
				ed::PinPivotSize(ImVec2(0, 0));

				ImGui::BeginGroup();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY()
					+ (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
				ImGui::TextUnformatted("Sai");
				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY()
					- (kPinIconSize - ImGui::GetTextLineHeight()) * 0.5f);
				DrawPinIcon(kPoseColor, true, true);
				ImGui::EndGroup();

				ed::EndPin();
			}

			ImGui::Dummy(ImVec2(w, 1.0f));

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		ed::PopStyleVar(3);

		// ── Link do Entry ────────────────────────────────────────────────────
		//
		// Sempre desenhado (grosso, verde): e a resposta visual de "onde
		// comeca". Nao e uma transicao — mora fora de sm.Transitions.
		if (sm.EntryState >= 0 && sm.EntryState < (int)sm.States.size())
			ed::Link(kEntryLink, kEntryOut, StateIn(sm.EntryState),
				ImVec4(0.30f, 0.75f, 0.40f, 1.0f), 2.5f);

		// ── Transicoes ───────────────────────────────────────────────────────
		for (std::size_t t = 0; t < sm.Transitions.size(); ++t)
		{
			const auto& tr = sm.Transitions[t];

			if (tr.To < 0 || tr.To >= (int)sm.States.size())
				continue;

			const int from = (tr.From < 0) ? kAnyStateOut : StateOut(tr.From);
			const int to = StateIn(tr.To);

			// Transicao com trigger em laranja: ela pode disparar a qualquer
			// momento, e e a que mais confunde ao ler um grafo.
			ImVec4 col{ 0.6f, 0.6f, 0.7f, 1.0f };

			for (const auto& c : tr.Conditions)
				if (c.Op == AnimCompare::TriggerSet)
					col = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);

			ed::Link(TransLink((int)t), from, to, col, tr.From < 0 ? 2.5f : 1.5f);
		}

		// ── Criar transicao ──────────────────────────────────────────────────
		if (ed::BeginCreate())
		{
			ed::PinId a, b;

			if (ed::QueryNewLink(&a, &b))
			{
				const int ia = (int)a.Get();
				const int ib = (int)b.Get();

				int from = -2, to = -2;

				if (ia == kAnyStateOut)                     from = -1;
				else if (ia >= 0x21000 && ia < 0x31000)     from = ia - 0x21000;

				if (ib >= 0x11000 && ib < 0x21000)          to = ib - 0x11000;

				// Arrastar do ENTRY nao cria transicao: REAPONTA a entrada.
				if (ia == kEntryOut)
				{
					if (to >= 0)
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
					// Auto-transicao sem exit time trava o personagem no frame 0
					// pra sempre. Quase sempre e engano de arrasto.
					const bool valid = (from != -2) && (to >= 0) && (from != to);

					if (!valid)
					{
						ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
					}
					else if (ed::AcceptNewItem(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), 3.0f))
					{
						AnimTransition tr;
						tr.From = from;
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

		// ── Apagar ───────────────────────────────────────────────────────────
		if (ed::BeginDelete())
		{
			ed::LinkId lid;
			while (ed::QueryDeletedLink(&lid))
			{
				// O link do Entry nao se apaga: sem entrada, a maquina nao sabe
				// onde comecar. Pra mudar, arraste do Entry pra outro estado.
				if ((int)lid.Get() == kEntryLink)
				{
					ed::RejectDeletedItem();
					continue;
				}

				if (ed::AcceptDeletedItem())
				{
					const int idx = (int)lid.Get() - 0x41000;

					if (idx >= 0 && idx < (int)sm.Transitions.size())
					{
						sm.Transitions.erase(sm.Transitions.begin() + idx);
						m_SelectedTransition = -1;
						MarkEdited();
					}
				}
			}

			ed::NodeId nid;
			while (ed::QueryDeletedNode(&nid))
			{
				const int id = (int)nid.Get();

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
				// bug so apareceria em runtime, como "o personagem vai pro estado
				// errado as vezes".
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

		// ── Menu: novo estado ────────────────────────────────────────────────
		ed::Suspend();

		if (ed::ShowBackgroundContextMenu())
		{
			m_MenuOpenCanvasPos = ed::ScreenToCanvas(ImGui::GetMousePos());
			ImGui::OpenPopup("add_state");
		}

		if (ImGui::BeginPopup("add_state"))
		{
			// Any State e opt-in. Com transicoes partindo dele, esconder
			// deixaria links sem origem — dai o item trava ligado.
			{
				bool anyVisible = false;
				bool anyLocked = false;

				for (const auto& tr : sm.Transitions)
					if (tr.From < 0) { anyLocked = true; break; }

				anyVisible = m_ShowAnyState || anyLocked;

				if (ImGui::MenuItem("Any State", nullptr, anyVisible, !anyLocked))
					m_ShowAnyState = !anyVisible;

				if (anyLocked && ImGui::IsItemHovered())
					ImGui::SetTooltip("Ha transicoes partindo do Any State.\nApague-as para poder esconde-lo.");

				ImGui::Separator();
			}

			if (ImGui::MenuItem("Novo Estado"))
			{
				AnimSmState st;
				st.Name = "Estado " + std::to_string(sm.States.size());

				// Nasce onde o mouse ABRIU o menu — que e onde voce esta olhando.
				//
				// Antes usava ScreenToCanvas em GetMousePos NA HORA do clique no
				// item, que ja tinha se movido pro menu; o estado aparecia longe,
				// as vezes fora da tela. Guardamos a posicao de quando o menu
				// abriu.
				st.EditorX = m_MenuOpenCanvasPos.x;
				st.EditorY = m_MenuOpenCanvasPos.y;

				// O estado nasce com um SUB-GRAFO utilizavel: ClipPlayer ->
				// Output, ja ligados.
				//
				// Um sub-grafo vazio obrigaria o usuario a entrar nele, criar um
				// Output, criar um player e ligar os dois — quatro passos antes de
				// ver qualquer coisa. Ninguem descobre isso sozinho.
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

				// Posiciona SO o no novo — nao marca m_PositionsLoaded=false, que
				// reposicionaria TODOS os nos e embaralharia o que voce ja
				// arrumou.
				ed::SetNodePosition(StateNode(newIdx),
					ImVec2(sm.States[newIdx].EditorX, sm.States[newIdx].EditorY));

				// Ja seleciona: o painel de detalhes abre no estado novo, e voce
				// ve na hora onde ele esta e como configura-lo.
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
		ed::LinkId selLink = 0;

		if (ed::GetSelectedNodes(&selNode, 1) > 0)
		{
			const int id = (int)selNode.Get();

			if (id >= 0x01000 && id < 0x11000)
			{
				m_SelectedState = id - 0x01000;
				m_SelectedTransition = -1;
			}
		}

		if (ed::GetSelectedLinks(&selLink, 1) > 0)
		{
			m_SelectedTransition = (int)selLink.Get() - 0x41000;
			m_SelectedState = -1;
		}

		// ── Duplo-clique num estado = abrir o SUB-GRAFO ──────────────────────
		ed::NodeId dbl = ed::GetDoubleClickedNode();

		if (dbl.Get() != 0 && (int)dbl.Get() != kAnyStateNode)
		{
			const int idx = (int)dbl.Get() - 0x01000;

			if (idx >= 0 && idx < (int)sm.States.size())
			{
				for (std::size_t i = 0; i < sm.States.size(); ++i)
				{
					const ImVec2 pos = ed::GetNodePosition(StateNode((int)i));
					sm.States[i].EditorX = pos.x;
					sm.States[i].EditorY = pos.y;
				}

				ed::SetCurrentEditor(nullptr);
				NavigateTo({ sm.States[idx].Name, &sm.States[idx].Graph, nullptr });
				return;
			}
		}

		for (std::size_t i = 0; i < sm.States.size(); ++i)
		{
			const ImVec2 pos = ed::GetNodePosition(StateNode((int)i));

			if (pos.x != sm.States[i].EditorX || pos.y != sm.States[i].EditorY)
			{
				sm.States[i].EditorX = pos.x;
				sm.States[i].EditorY = pos.y;
				MarkEdited();
			}
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

			ImGui::TextDisabled("Selecione um no.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextWrapped(
				"Este e um GRAFO DE POSES. Os links carregam poses (branco) ou "
				"valores (verde/vermelho).\n\n"
				"A maquina de estados e um NO aqui dentro - de duplo-clique nela "
				"para editar os estados.\n\n"
				"E o que permitira, depois, plugar um Two Bone IK entre ela e o "
				"Output.");
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

			ImGui::TextDisabled("Selecione um estado ou uma transicao.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextWrapped(
				"Estado = o que toca. Cada um contem um SUB-GRAFO de poses "
				"(duplo-clique para abrir).\n\n"
				"Transicao = quando trocar.\n\n"
				"Locomocao inteira cabe em UM estado com um Blend Space. Nao faca "
				"idle/walk/run como tres estados - as transicoes entre eles dao pop "
				"no meio de uma aceleracao.");
		}
	}

	void AnimGraphWindow::DrawNodeDetails(AnimNode& node)
	{
		ImGui::TextUnformatted(node.TypeName());
		ImGui::Separator();

		char title[64];
		std::snprintf(title, sizeof(title), "%s", node.Title.c_str());

		if (ImGui::InputText("Titulo", title, sizeof(title)))
		{
			node.Title = title;
			MarkEdited();
		}

		ImGui::Spacing();

		// ── Clip Player ──────────────────────────────────────────────────────
		if (auto* cp = dynamic_cast<AnimNode_ClipPlayer*>(&node))
		{
			const char* cur = cp->ClipName.empty() ? "(nenhum)" : cp->ClipName.c_str();

			if (ImGui::BeginCombo("Clipe", cur))
			{
				if (ImGui::Selectable("(nenhum)", cp->ClipName.empty()))
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
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Sem esqueleto - nao ha clipes.");

			if (ImGui::DragFloat("Velocidade", &cp->PlayRate, 0.05f, -3.0f, 3.0f, "%.2fx")) MarkEdited();
			if (ImGui::Checkbox("Loop", &cp->Loop)) MarkEdited();
			return;
		}

		// ── Blend Space Player ───────────────────────────────────────────────
		if (auto* bs = dynamic_cast<AnimNode_BlendSpacePlayer*>(&node))
		{
			ImGui::TextWrapped("Locomocao inteira em um no. O valor vem do pino "
				"'Speed' - ligue um no de variavel nele.");
			ImGui::Spacing();

			if (ImGui::DragFloat("Velocidade", &bs->PlayRate, 0.05f, 0.0f, 3.0f, "%.2fx")) MarkEdited();

			ImGui::Separator();
			ImGui::TextDisabled("Amostras");

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

			if (m_Skeleton && ImGui::BeginCombo("+ amostra", "adicionar..."))
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
			ImGui::TextDisabled("Ex: idle=0, walk=150, run=400");
			return;
		}

		// ── Nos de variavel ──────────────────────────────────────────────────
		if (auto* gf = dynamic_cast<AnimNode_GetFloat*>(&node))
		{
			// Dropdown dos parametros DECLARADOS, nao texto livre.
			//
			// Um nome digitado errado le 0 e nunca dispara nada — sem erro, sem
			// aviso. E o bug mais frustrante de um AnimGraph.
			if (ImGui::BeginCombo("Parametro", gf->Parameter.c_str()))
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
			if (ImGui::BeginCombo("Parametro", gb->Parameter.c_str()))
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
				ImGui::SetTooltip("0 = corte seco. Um bool que faz a pose saltar e o\n"
					"defeito mais comum em rig de arma/agachamento.");
			return;
		}

		if (auto* lb = dynamic_cast<AnimNode_LayeredBlend*>(&node))
		{
			ImGui::TextWrapped("Corre com as pernas E atira com os bracos. A mascara "
				"vem do osso raiz para baixo na hierarquia.");
			ImGui::Spacing();

			char bone[64];
			std::snprintf(bone, sizeof(bone), "%s", lb->RootBone.c_str());

			if (ImGui::InputText("Osso raiz", bone, sizeof(bone)))
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
				ImGui::SetTooltip("Suaviza a fronteira ao longo de N ossos.\n"
					"Sem feather, a juncao vira uma dobra rigida no meio das costas.");

			// A lista de ossos do personagem, pra nao ter que digitar de cabeca.
			if (m_Skeleton && m_Skeleton->GetSkeleton() &&
				ImGui::BeginCombo("Escolher osso", "..."))
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

		if (dynamic_cast<AnimNode_StateMachine*>(&node))
		{
			ImGui::TextWrapped("Duplo-clique no no para editar os estados.");
			return;
		}

		if (dynamic_cast<AnimNode_Output*>(&node))
		{
			ImGui::TextWrapped("A pose final do personagem. Tudo que chega aqui e "
				"o que vai pro skin cache.");
			return;
		}

		ImGui::TextDisabled("(sem parametros)");
	}

	void AnimGraphWindow::DrawStateDetails(AnimNode_StateMachine& sm, int index)
	{
		auto& st = sm.States[index];

		ImGui::TextUnformatted("Estado");
		ImGui::Separator();

		char name[64];
		std::snprintf(name, sizeof(name), "%s", st.Name.c_str());

		if (ImGui::InputText("Nome", name, sizeof(name)))
		{
			st.Name = name;
			MarkEdited();
		}

		if (sm.EntryState == index)
		{
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Estado de ENTRADA");
		}
		else if (ImGui::Button("Tornar estado de entrada", ImVec2(-1, 0)))
		{
			sm.EntryState = index;
			MarkEdited();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextWrapped("O que este estado TOCA vive no sub-grafo dele. "
			"De duplo-clique no no para abrir.");

		if (ImGui::Button("Abrir sub-grafo", ImVec2(-1, 0)))
			NavigateTo({ st.Name, &st.Graph, nullptr });
	}

	void AnimGraphWindow::DrawTransitionDetails(AnimNode_StateMachine& sm, int index)
	{
		auto& tr = sm.Transitions[index];

		ImGui::TextUnformatted("Transicao");
		ImGui::Separator();

		const char* fromName = (tr.From < 0) ? "Any State"
			: (tr.From < (int)sm.States.size() ? sm.States[tr.From].Name.c_str() : "?");

		const char* toName = (tr.To >= 0 && tr.To < (int)sm.States.size())
			? sm.States[tr.To].Name.c_str() : "?";

		ImGui::Text("%s  ->  %s", fromName, toName);
		ImGui::Spacing();

		if (ImGui::DragFloat("Duracao", &tr.Duration, 0.01f, 0.0f, 2.0f, "%.2fs")) MarkEdited();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Crossfade. 0 = corte seco.");

		if (ImGui::DragInt("Prioridade", &tr.Priority, 0.1f, 0, 100)) MarkEdited();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Se duas transicoes valem no mesmo frame,\na de maior prioridade vence.");

		ImGui::Spacing();

		if (ImGui::Checkbox("Exit Time", &tr.HasExitTime)) MarkEdited();

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"So permite a transicao depois que o estado completou X do ciclo.\n\n"
				"E o que impede um ataque de ser cortado no primeiro frame: sem\n"
				"isto, 'attack -> idle quando parado' dispara imediatamente e o\n"
				"golpe nunca sai.");
		}

		if (tr.HasExitTime)
		{
			if (ImGui::SliderFloat("##exit", &tr.ExitTime, 0.0f, 1.0f, "%.2f do ciclo")) MarkEdited();
		}

		if (tr.From < 0)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Any State ignora exit time - dano e morte");
			ImGui::TextDisabled("precisam interromper na hora.");

			if (ImGui::Checkbox("Pode redisparar no proprio estado", &tr.CanRetriggerSelf)) MarkEdited();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("Condicoes (TODAS precisam passar)");

		if (tr.Conditions.empty() && !tr.HasExitTime)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Sem condicao e sem exit time:");
			ImGui::TextWrapped("esta transicao dispara no primeiro frame, sempre - "
				"o estado de origem nunca sera visto.");
		}

		const auto& params = m_Asset->GetParameters();

		int removeCond = -1;

		for (std::size_t c = 0; c < tr.Conditions.size(); ++c)
		{
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

			const char* ops[] = { ">", ">=", "<", "<=", "==", "!=", "e true", "e false", "disparou" };
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

		if (removeCond >= 0)
		{
			tr.Conditions.erase(tr.Conditions.begin() + removeCond);
			MarkEdited();
		}

		if (ImGui::Button("+ Condicao", ImVec2(-1, 0)))
		{
			AnimCondition c;
			if (!params.empty()) c.Parameter = params[0].Name;

			tr.Conditions.push_back(c);
			MarkEdited();
		}
	}

} // namespace axe