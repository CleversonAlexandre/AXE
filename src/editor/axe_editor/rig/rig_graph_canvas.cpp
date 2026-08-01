#include "control_rig_window.hpp"
#include "axe/log/log.hpp"

#include <utilities/widgets.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════════
	//  CANVAS DO RIG GRAPH — CONTROLRIG_V1
	//
	//  O grafo tem DOIS tipos de fio, e o canvas precisa deixar isso obvio na
	//  primeira olhada — senao voce liga um no de dados numa entrada de
	//  execucao e passa cinco minutos entendendo por que "nao acontece nada".
	//
	//    Execucao — triangulo branco, fio grosso. Diz QUANDO cada no roda.
	//    Dados    — circulo colorido por tipo, fio fino. Diz DE ONDE vem cada
	//               valor.
	//
	//  ── IDENTIDADE DOS PINOS ─────────────────────────────────────────────
	//
	//  O node-editor exige um inteiro unico por pino, e nos so temos "no +
	//  indice". A codificacao e nodeId * 100 + faixa:
	//
	//      +0       execucao de entrada
	//      +1..+9   execucao de saida
	//      +20..+39 dados de entrada
	//      +50..+69 dados de saida
	//
	//  Reservar faixas em vez de numerar tudo em sequencia e o que permite
	//  DECODIFICAR o pino de volta (dono e papel) com duas divisoes — sem
	//  tabela auxiliar que poderia sair de sincronia com o grafo.
	// ═════════════════════════════════════════════════════════════════════════

	namespace
	{
		constexpr int kPinStride = 100;
		constexpr int kExecInOff = 0;
		constexpr int kExecOutOff = 1;
		constexpr int kDataInOff = 20;
		constexpr int kDataOutOff = 50;

		int ExecInPin(int node) { return node * kPinStride + kExecInOff; }
		int ExecOutPin(int node, int i) { return node * kPinStride + kExecOutOff + i; }
		int DataInPin(int node, int i) { return node * kPinStride + kDataInOff + i; }
		int DataOutPin(int node, int i) { return node * kPinStride + kDataOutOff + i; }

		int PinNode(int pin) { return pin / kPinStride; }
		int PinOffset(int pin) { return pin % kPinStride; }

		bool IsExecIn(int pin) { return PinOffset(pin) == kExecInOff; }
		bool IsExecOut(int pin) { const int o = PinOffset(pin); return o >= kExecOutOff && o < kDataInOff; }
		bool IsDataIn(int pin) { const int o = PinOffset(pin); return o >= kDataInOff && o < kDataOutOff; }
		bool IsDataOut(int pin) { return PinOffset(pin) >= kDataOutOff; }

		int ExecOutIndex(int pin) { return PinOffset(pin) - kExecOutOff; }
		int DataInIndex(int pin) { return PinOffset(pin) - kDataInOff; }
		int DataOutIndex(int pin) { return PinOffset(pin) - kDataOutOff; }

		// Os links precisam de ids proprios, em faixas separadas dos pinos.
		constexpr int kExecLinkBase = 1000000;
		constexpr int kDataLinkBase = 2000000;

		// Cor por tipo de dado. Mesma logica do AnimGraph: a cor e o que faz
		// voce enxergar "isto e um Transform" sem ler o rotulo.
		ImVec4 ColorForPin(RigPinType t)
		{
			switch (t)
			{
			case RigPinType::Bool:      return ImVec4(0.85f, 0.35f, 0.35f, 1.0f);
			case RigPinType::Float:     return ImVec4(0.55f, 0.85f, 0.45f, 1.0f);
			case RigPinType::Vector:    return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
			case RigPinType::Transform: return ImVec4(0.95f, 0.55f, 0.25f, 1.0f);
			case RigPinType::Item:      return ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
			case RigPinType::Wildcard:  return ImVec4(0.72f, 0.72f, 0.78f, 1.0f);
			case RigPinType::ItemArray: return ImVec4(0.40f, 0.85f, 0.80f, 1.0f);
			default:                    return ImVec4(0.85f, 0.85f, 0.90f, 1.0f);
			}
		}

		// Cor do cabecalho por familia de no. Evento se destaca; escrita
		// (Set/IK) e quente porque MUDA a hierarquia; leitura e fria.
		ImVec4 HeaderColorFor(const RigNode& n)
		{
			const std::string t = n.TypeName();

			if (t == "ForwardsSolve")                      return ImVec4(0.55f, 0.20f, 0.20f, 1.0f);

			// Backward tambem e evento, mas de outra COR: os dois nunca rodam
			// juntos, e confundir qual corrente voce esta olhando e o erro mais
			// caro que este grafo permite.
			if (t == "BackwardsSolve")                     return ImVec4(0.20f, 0.40f, 0.55f, 1.0f);

			// Entry e Return sao os eventos DA FUNCAO: mesma familia visual dos
			// eventos do grafo principal, tom proprio.
			if (t == "Entry" || t == "Return")             return ImVec4(0.35f, 0.30f, 0.55f, 1.0f);

			// Call nao e nenhuma das categorias — ele CONTEM todas.
			if (t == "CallFunction")                       return ImVec4(0.13f, 0.38f, 0.35f, 1.0f);
			if (t == "Sequence" || t == "Branch" || t == "ForEach")
				return ImVec4(0.32f, 0.32f, 0.36f, 1.0f);
			if (t == "SetTransform" || t == "TwoBoneIK"
				|| t == "FKChain" || t == "ParentConstraint"
				|| t == "AlignToVector"
				|| t == "SetControlPose"
				|| t == "PelvisDip")
				return ImVec4(0.50f, 0.33f, 0.14f, 1.0f);
			if (t == "GetTransform" || t == "GroundTrace") return ImVec4(0.16f, 0.34f, 0.50f, 1.0f);

			return ImVec4(0.26f, 0.28f, 0.34f, 1.0f);
		}

		constexpr float kPinIcon = 18.0f;

		void PinIcon(const ImVec4& color, bool connected, bool isExec)
		{
			ax::Widgets::Icon(ImVec2(kPinIcon, kPinIcon),
				isExec ? ax::Drawing::IconType::Flow : ax::Drawing::IconType::Circle,
				connected, color, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));
		}

		// ── O DESENHO DO PINO DIZ O TIPO ─────────────────────────────────────
		//
		// Cor sozinha nao basta: quem tem daltonismo, ou so esta cansado, le a
		// FORMA mais rapido. E o array em especial precisa se distinguir do
		// valor unico a distancia — a grade e o mesmo simbolo que a Unreal usa,
		// entao quem vem de la ja sabe o que significa.
		ax::Drawing::IconType IconFor(RigPinType t)
		{
			switch (t)
			{
			case RigPinType::Exec:      return ax::Drawing::IconType::Flow;
			case RigPinType::ItemArray: return ax::Drawing::IconType::Grid;
			case RigPinType::Item:      return ax::Drawing::IconType::Square;
			case RigPinType::Transform: return ax::Drawing::IconType::RoundSquare;
			case RigPinType::Wildcard:  return ax::Drawing::IconType::Diamond;
			default:                    return ax::Drawing::IconType::Circle;
			}
		}

		void PinIconFor(RigPinType t, bool connected)
		{
			ax::Widgets::Icon(ImVec2(kPinIcon, kPinIcon), IconFor(t),
				connected, ColorForPin(t), ImVec4(0.09f, 0.09f, 0.11f, 1.0f));
		}

		// Largura do editor inline por tipo.
		//
		// Precisa ser CONHECIDA ANTES de desenhar: e o que permite medir a
		// linha inteira e so entao decidir a largura do no. Tem que casar com
		// o que o DrawInlinePinEditor realmente desenha — se divergir, a coluna
		// da direita sai torta.
		float InlineEditorWidth(RigPinType t)
		{
			switch (t)
			{
			case RigPinType::Float:  return 62.0f;
			case RigPinType::Bool:   return ImGui::GetFrameHeight();
			case RigPinType::Vector: return 3.0f * 46.0f + 8.0f;
			case RigPinType::Item:   return 132.0f;
			case RigPinType::ItemArray: return 0.0f;   // so por fio
			default:                 return 0.0f;
			}
		}

		const char* ShortItemName(const std::string& full)
		{
			const std::size_t p = full.find_last_of(":|");
			return (p == std::string::npos) ? full.c_str() : full.c_str() + p + 1;
		}
	}

	// Editor do valor inline de um pino de entrada SEM fio.
	//
	// E o que evita um grafo cheio de nos-constante: digitar 0.35 no proprio
	// pino resolve a maioria dos casos. So aparece quando o pino esta solto —
	// com fio ligado, o valor vem de la e um campo editavel seria mentira.
	void ControlRigWindow::DrawInlinePinEditor(RigNode& node, int pinIndex)
	{
		RigPin& pin = node.Inputs[pinIndex];

		ImGui::PushID(pinIndex);

		switch (pin.Type)
		{
		case RigPinType::Float:
			ImGui::SetNextItemWidth(InlineEditorWidth(pin.Type));
			if (ImGui::DragFloat("##f", &pin.Default.Float, 0.01f, 0.0f, 0.0f, "%.3f"))
				MarkEdited("Edit pin value");
			break;

		case RigPinType::Bool:
			if (ImGui::Checkbox("##b", &pin.Default.Bool))
				MarkEdited("Edit pin value");
			break;

		case RigPinType::Vector:
			ImGui::SetNextItemWidth(InlineEditorWidth(pin.Type));
			if (ImGui::DragFloat3("##v", &pin.Default.Vector.x, 0.01f, 0.0f, 0.0f, "%.2f"))
				MarkEdited("Edit pin value");
			break;

		case RigPinType::Item:
		{
			// BOTAO, nao combo.
			//
			// Um ImGui::BeginCombo dentro de um no abre o popup em ESPACO DE
			// TELA, enquanto o item que o ancora vive no espaco TRANSFORMADO
			// do canvas — por isso a lista aparecia longe, no canto. A saida e
			// nao abrir popup nenhum aqui dentro: o clique so ANOTA o pedido,
			// e o seletor e desenhado depois do ed::End(), ja fora do canvas.
			const char* label = pin.Default.ItemName.empty()
				? "(pick)"
				: ShortItemName(pin.Default.ItemName);

			if (ImGui::Button(label, ImVec2(InlineEditorWidth(pin.Type), 0.0f)))
			{
				m_ItemPickerNode = node.Id;
				m_ItemPickerPin = pinIndex;
				m_ItemPickerOpen = true;
				m_ItemPickerFilter[0] = '\0';
			}

			// Soltar um elemento da hierarquia DIRETO no pino: e o caminho
			// mais curto entre "quero esse osso aqui" e o osso estar ali.
			{
				std::string dropName;
				RigElementType dropType = RigElementType::Bone;

				if (AcceptRigElementDrop(dropName, dropType))
				{
					pin.Default.ItemName = dropName;
					pin.Default.ItemType = dropType;

					MarkEdited("Set item");
				}
			}

			break;
		}

		default:
			break;
		}

		ImGui::PopID();
	}

	// ═══ Funcoes ═════════════════════════════════════════════════════════════

	RigGraph& ControlRigWindow::CurrentGraph()
	{
		auto& funcs = m_Asset->GetFunctions();

		// O indice pode ter ficado velho — um undo que desfez a criacao da
		// funcao, por exemplo. Cair no principal e o comportamento seguro.
		if (m_EditingFunction >= 0 && m_EditingFunction < (int)funcs.size())
			return funcs[(std::size_t)m_EditingFunction].Graph;

		m_EditingFunction = -1;
		return m_Asset->GetGraph();
	}

	ed::EditorContext* ControlRigWindow::CurrentEdCtx()
	{
		if (m_EditingFunction < 0)
			return m_EdCtx;

		// Sob demanda: quem nunca abre uma funcao nao paga nada.
		if (!m_FuncEdCtx)
		{
			ed::Config cfg;

			// Mesma razao do contexto principal: as posicoes moram no .axerig,
			// nao num .json solto ao lado do executavel.
			cfg.SettingsFile = nullptr;

			m_FuncEdCtx = ed::CreateEditor(&cfg);
		}

		return m_FuncEdCtx;
	}

	void ControlRigWindow::SwitchToMainGraph()
	{
		if (m_EditingFunction < 0)
			return;

		m_EditingFunction = -1;
		m_SelectedNode = -1;

		// O contexto do grafo principal ainda tem as posicoes de la, mas a
		// flag e compartilhada — reaplicar e barato e garante o arranjo certo.
		m_NodePositionsLoaded = false;
	}

	void ControlRigWindow::SwitchToFunction(int index)
	{
		auto& funcs = m_Asset->GetFunctions();

		if (index < 0 || index >= (int)funcs.size() || m_EditingFunction == index)
			return;

		m_EditingFunction = index;

		// A selecao era do grafo anterior e nao significa nada aqui.
		m_SelectedNode = -1;
		m_NodePositionsLoaded = false;
	}

	void ControlRigWindow::RebuildFunctionCallSites(int index)
	{
		auto& funcs = m_Asset->GetFunctions();

		if (index < 0 || index >= (int)funcs.size())
			return;

		RigFunction& fn = funcs[(std::size_t)index];

		// ── Entry e Return, dentro da propria funcao ─────────────────────────
		//
		// O Entry EXPOE os Inputs como saidas (voce puxa dele o que entrou), e
		// o Return RECEBE os Outputs como entradas. A inversao e o ponto: visto
		// de dentro, entrada da funcao e saida de no.
		for (const auto& n : fn.Graph.GetNodes())
		{
			const std::string t = n->TypeName();

			if (t == "Entry")
			{
				n->Outputs.clear();

				for (const auto& p : fn.Inputs)
				{
					RigPin pin;
					pin.Name = p.Name;
					pin.Type = p.Type;

					n->Outputs.push_back(std::move(pin));
				}
			}
			else if (t == "Return")
			{
				n->Inputs.clear();

				for (const auto& p : fn.Outputs)
				{
					RigPin pin;
					pin.Name = p.Name;
					pin.Type = p.Type;

					n->Inputs.push_back(std::move(pin));
				}
			}
		}

		// ── Os nos Call, em TODO grafo do asset ──────────────────────────────
		//
		// Inclusive no grafo da propria funcao: recursao e valida, e uma
		// chamada recursiva com pinos velhos ficaria quebrada em silencio.
		const auto fix = [&](RigGraph& g)
			{
				for (const auto& n : g.GetNodes())
				{
					auto* call = dynamic_cast<RigNode_CallFunction*>(n.get());

					if (!call || call->FunctionName != fn.Name)
						continue;

					// Os DEFAULTS ja digitados sao preservados por NOME. Por
					// indice, acrescentar um parametro no meio da lista
					// embaralharia todos os valores seguintes.
					std::vector<RigPin> old = call->Inputs;

					call->Inputs.clear();

					for (const auto& p : fn.Inputs)
					{
						RigPin pin;
						pin.Name = p.Name;
						pin.Type = p.Type;

						for (const auto& o : old)
							if (o.Name == p.Name && o.Type == p.Type)
							{
								pin.Default = o.Default;
								break;
							}

						call->Inputs.push_back(std::move(pin));
					}

					call->Outputs.clear();

					for (const auto& p : fn.Outputs)
					{
						RigPin pin;
						pin.Name = p.Name;
						pin.Type = p.Type;

						call->Outputs.push_back(std::move(pin));
					}

					call->Title = fn.Name;
				}
			};

		fix(m_Asset->GetGraph());

		for (auto& f : m_Asset->GetFunctions())
			fix(f.Graph);
	}

	void ControlRigWindow::DrawGraphCanvas()
	{
		if (!m_Asset || !m_EdCtx)
			return;

		// ── Breadcrumb de funcao ─────────────────────────────────────────────
		//
		// Sem isto, abrir uma funcao troca o grafo em silencio — e Delete,
		// Ctrl+V e a paleta passam a agir num lugar diferente do que voce
		// pensa. Mesmo padrao do Script Editor, de proposito: quem aprendeu la
		// nao deveria ter que aprender de novo aqui.
		if (m_EditingFunction >= 0
			&& m_EditingFunction < (int)m_Asset->GetFunctions().size())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.85f, 0.78f, 1.0f));
			ImGui::Text("Function: %s",
				m_Asset->GetFunctions()[(std::size_t)m_EditingFunction].Name.c_str());
			ImGui::PopStyleColor();

			ImGui::SameLine(0, 8);

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.38f, 0.40f, 1.0f));

			if (ImGui::SmallButton("< Voltar ao grafo principal"))
			{
				ImGui::PopStyleColor(2);
				SwitchToMainGraph();
				return;
			}

			ImGui::PopStyleColor(2);
			ImGui::Separator();
		}

		RigGraph& graph = CurrentGraph();

		// Rect do canvas, capturado ANTES do ed::Begin: e a area que aceita o
		// arrasto vindo da hierarquia.
		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
		const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

		ed::SetCurrentEditor(CurrentEdCtx());
		ed::Begin("RigGraph");

		// Restaura as posicoes salvas no .axerig, uma vez por abertura.
		if (!m_NodePositionsLoaded)
		{
			for (const auto& n : graph.GetNodes())
				ed::SetNodePosition(n->Id, ImVec2(n->EditorX, n->EditorY));

			m_NodePositionsLoaded = true;
		}

		// Quais pinos tem fio — so pra pintar o icone cheio ou vazio.
		auto execOutLinked = [&](int nodeId, int i)
			{
				for (const auto& l : graph.GetExecLinks())
					if (l.FromNode == nodeId && l.FromExec == i)
						return true;
				return false;
			};

		auto execInLinked = [&](int nodeId)
			{
				for (const auto& l : graph.GetExecLinks())
					if (l.ToNode == nodeId)
						return true;
				return false;
			};

		auto dataInLinked = [&](int nodeId, int i)
			{
				for (const auto& l : graph.GetDataLinks())
					if (l.ToNode == nodeId && l.ToPin == i)
						return true;
				return false;
			};

		auto dataOutLinked = [&](int nodeId, int i)
			{
				for (const auto& l : graph.GetDataLinks())
					if (l.FromNode == nodeId && l.FromPin == i)
						return true;
				return false;
			};

		// ── Nos ──────────────────────────────────────────────────────────────
		// ── COMMENTS PRIMEIRO ────────────────────────────────────────────────
		//
		// Desenhados antes dos outros nos pra ficarem ATRAS deles — um
		// comentario por cima esconderia o que ele agrupa.
		//
		// Este bloco espelha o comment do Script Editor, que ja e testado:
		// padding PADRAO (nao zero), Alpha do ImGui pra translucidez, titulo
		// como WIDGET de verdade, ed::Group no fim e o tamanho relido com
		// GetNodeSize. A minha versao anterior mexia no NodePadding e chamava
		// ed::SetGroupSize no meio do frame — as duas coisas que o caminho
		// testado NAO faz.
		for (const auto& np : graph.GetNodes())
		{
			auto* c = dynamic_cast<RigNode_Comment*>(np.get());

			if (!c)
				continue;

			const ImVec4 bg(c->Color.r, c->Color.g, c->Color.b, 60.0f / 255.0f);
			const ImVec4 border(c->Color.r, c->Color.g, c->Color.b, 200.0f / 255.0f);

			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.75f);
			ed::PushStyleColor(ed::StyleColor_NodeBg, bg);
			ed::PushStyleColor(ed::StyleColor_NodeBorder, border);

			ed::BeginNode(c->Id);

			ImGui::PushID(c->Id);

			// ── TITULO EDITAVEL NO PROPRIO NO ────────────────────────────
			//
			// Duplo-clique troca o texto por um campo, como no Script Editor.
			// Um comment com titulo generico nao organiza nada — o valor dele
			// esta justamente em dizer "isto aqui e a perna esquerda".
			if (m_RenamingComment == c->Id)
			{
				// Foco so no primeiro frame: reancorar todo frame impede o
				// texto de entrar (a mesma armadilha do rename da hierarquia).
				if (m_RenameCommentFocus)
				{
					ImGui::SetKeyboardFocusHere();
					m_RenameCommentFocus = false;
				}

				ImGui::SetNextItemWidth(std::max(c->SizeX - 16.0f, 90.0f));

				const bool entered = ImGui::InputText("##ctitle", m_CommentBuf, sizeof(m_CommentBuf),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

				// Enter OU clicar fora depois de editar: os dois aplicam.
				if (entered || ImGui::IsItemDeactivatedAfterEdit())
				{
					c->Title = m_CommentBuf;
					m_RenamingComment = -1;
					MarkEdited("Rename comment");
				}
				else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape))
				{
					m_RenamingComment = -1;
				}
			}
			else
			{
				ImGui::TextUnformatted(c->Title.c_str());

				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_RenamingComment = c->Id;
					m_RenameCommentFocus = true;

					std::snprintf(m_CommentBuf, sizeof(m_CommentBuf), "%s", c->Title.c_str());
				}
			}

			ImGui::PopID();

			ed::Group(ImVec2(c->SizeX, c->SizeY));

			ed::EndNode();

			ed::PopStyleColor(2);
			ImGui::PopStyleVar();

			// Le o tamanho de volta: o resize pela borda e tratado dentro da
			// lib, e sem isto ele nao sobreviveria ao proximo frame.
			const ImVec2 sz = ed::GetNodeSize(c->Id);

			if (sz.x > 1.0f && (std::abs(sz.x - c->SizeX) > 0.5f || std::abs(sz.y - c->SizeY) > 0.5f))
			{
				c->SizeX = sz.x;
				c->SizeY = sz.y;
				MarkEdited("Resize comment");
			}
		}

		// ── REROUTES: pilula compacta ────────────────────────────────────────
		//
		// UM no para os dois tipos de fio. Indeciso mostra os dois pares de
		// pino (triangulo de execucao em cima, circulo de dados embaixo); ao
		// ligar o primeiro fio ele se decide e o outro par some.
		for (const auto& np : graph.GetNodes())
		{
			auto* rr = dynamic_cast<RigNode_Reroute*>(np.get());

			if (!rr)
				continue;

			const bool hasData = !rr->Inputs.empty();
			const bool hasExec = rr->HasExecIn;

			const ImVec4 pc = hasData
				? ColorForPin(rr->Inputs[0].Type)
				: ImVec4(1, 1, 1, 0.9f);

			// Padding generoso de proposito: o reroute e feito SO de pinos, e sem
			// uma borda livre em volta nao sobra onde clicar pra MOVER ele —
			// todo clique vira arrasto de fio.
			ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(9, 7, 9, 7));
			ed::PushStyleVar(ed::StyleVar_NodeRounding, 16.0f);
			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.13f, 0.14f, 0.17f, 0.97f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, pc);

			ed::BeginNode(rr->Id);

			if (hasExec)
			{
				ed::BeginPin(ExecInPin(rr->Id), ed::PinKind::Input);
				PinIcon(ImVec4(1, 1, 1, 1), execInLinked(rr->Id), true);
				ed::EndPin();

				ImGui::SameLine(0.0f, 2.0f);

				ed::BeginPin(ExecOutPin(rr->Id, 0), ed::PinKind::Output);
				PinIcon(ImVec4(1, 1, 1, 1), execOutLinked(rr->Id, 0), true);
				ed::EndPin();
			}

			if (hasData)
			{
				ed::BeginPin(DataInPin(rr->Id, 0), ed::PinKind::Input);
				PinIconFor(rr->Inputs[0].Type, dataInLinked(rr->Id, 0));
				ed::EndPin();

				ImGui::SameLine(0.0f, 2.0f);

				ed::BeginPin(DataOutPin(rr->Id, 0), ed::PinKind::Output);
				PinIconFor(rr->Outputs[0].Type, dataOutLinked(rr->Id, 0));
				ed::EndPin();
			}

			ed::EndNode();

			ed::PopStyleColor(2);
			ed::PopStyleVar(2);
		}

		ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 4, 8, 8));
		ed::PushStyleVar(ed::StyleVar_NodeRounding, 5.0f);

		for (const auto& np : graph.GetNodes())
		{
			RigNode& n = *np;

			// Comments e reroutes ja foram desenhados acima, com layout
			// proprio.
			if (dynamic_cast<RigNode_Comment*>(&n) || dynamic_cast<RigNode_Reroute*>(&n))
				continue;

			// ═══ MEDIR ANTES DE DESENHAR ═══════════════════════════════════
			//
			// A versao anterior empilhava entradas e saidas em dois grupos
			// independentes e emendava com SameLine. O resultado era o do
			// print: a saida da linha 1 caia embaixo da entrada da linha 1, e
			// a coluna da direita ficava onde o texto terminasse.
			//
			// Na Unreal cada pino tem A SUA LINHA e a coluna direita encosta na
			// BORDA do no. Pra isso e preciso saber a largura do no ANTES de
			// desenhar — e por isso medimos tudo primeiro.
			const float gapCols = 26.0f;   // vao minimo entre as duas colunas
			const float gapIcon = 6.0f;    // entre icone e rotulo

			float leftW = 0.0f;

			if (n.HasExecIn)
				leftW = kPinIcon;

			for (std::size_t r = 0; r < n.Inputs.size(); ++r)
			{
				const RigPin& pin = n.Inputs[r];

				float w = kPinIcon + gapIcon + ImGui::CalcTextSize(pin.Name.c_str()).x;

				// O editor inline so existe quando o pino esta SOLTO — entao
				// so ocupa largura nesse caso.
				if (!dataInLinked(n.Id, (int)r))
				{
					const float ew = InlineEditorWidth(pin.Type);

					if (ew > 0.0f)
						w += gapIcon + ew;
				}

				leftW = std::max(leftW, w);
			}

			float rightW = 0.0f;

			for (const auto& e : n.ExecOut)
				rightW = std::max(rightW,
					ImGui::CalcTextSize(e.c_str()).x + (e.empty() ? 0.0f : gapIcon) + kPinIcon);

			for (const auto& o : n.Outputs)
				rightW = std::max(rightW,
					ImGui::CalcTextSize(o.Name.c_str()).x + gapIcon + kPinIcon);

			const float titleW = ImGui::CalcTextSize(n.Title.c_str()).x;

			const float bodyW = leftW + (rightW > 0.0f ? gapCols + rightW : 0.0f);
			const float nodeW = std::max(std::max(bodyW, titleW), 168.0f);

			const float rowH = std::max(kPinIcon, ImGui::GetFrameHeight()) + 3.0f;

			ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.11f, 0.115f, 0.135f, 0.97f));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));

			ed::BeginNode(n.Id);

			// ── ISOLA OS IDs DO IMGUI ────────────────────────────────────
			//
			// ed::BeginNode NAO empurra um ID do ImGui — so o node-editor sabe
			// que mudou de no. Sem este PushID, o campo "Weight" de um Two Bone
			// IK e o de OUTRO tem exatamente o mesmo ID, e o ImGui trata os
			// dois como o MESMO widget: ao arrastar um, ambos se veem ativos e
			// aplicam o mesmo delta. Era isso que fazia "mexer no peso de um
			// mudar o do outro".
			ImGui::PushID(n.Id);

			const ImVec2 origin = ImGui::GetCursorScreenPos();

			// ── Cabecalho: faixa cheia, da borda a borda ──────────────────
			{
				const float hh = ImGui::GetTextLineHeight() + 8.0f;

				ImGui::GetWindowDrawList()->AddRectFilled(
					ImVec2(origin.x - 8.0f, origin.y - 4.0f),
					ImVec2(origin.x + nodeW + 8.0f, origin.y + hh - 4.0f),
					ImGui::ColorConvertFloat4ToU32(HeaderColorFor(n)),
					5.0f, ImDrawFlags_RoundCornersTop);

				ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
				ImGui::TextUnformatted(n.Title.c_str());

				// Reserva a largura do no de uma vez: sem isto o no encolhe
				// pro tamanho do conteudo e a coluna direita perde a borda.
				//
				// A ALTURA extra tambem importa: o cabecalho e a unica faixa do
				// no que NAO tem pino nenhum, entao e por onde se arrasta. Uma
				// faixa fina demais faz o clique cair no primeiro pino e o
				// editor comeca a puxar um FIO em vez de mover o no — que e
				// exatamente a sensacao de "nao consigo posicionar".
				ImGui::Dummy(ImVec2(nodeW, hh - ImGui::GetTextLineHeight() + 6.0f));
			}

			// ── UMA LINHA POR PINO ────────────────────────────────────────
			//
			// Cada linha posiciona a esquerda no inicio e a direita ENCOSTADA
			// na borda (origin.x + nodeW - larguraDaLinha).
			const std::size_t execRows =
				std::max<std::size_t>(n.HasExecIn ? 1u : 0u, n.ExecOut.size());

			for (std::size_t r = 0; r < execRows; ++r)
			{
				const ImVec2 rp = ImGui::GetCursorScreenPos();

				if (r == 0 && n.HasExecIn)
				{
					ed::BeginPin(ExecInPin(n.Id), ed::PinKind::Input);
					PinIcon(ImVec4(1, 1, 1, 1), execInLinked(n.Id), true);
					ed::EndPin();
				}

				if (r < n.ExecOut.size())
				{
					const std::string& lbl = n.ExecOut[r];

					const float lw = ImGui::CalcTextSize(lbl.c_str()).x;
					const float w = lw + (lbl.empty() ? 0.0f : gapIcon) + kPinIcon;

					ImGui::SetCursorScreenPos(ImVec2(rp.x + nodeW - w, rp.y));

					if (!lbl.empty())
					{
						ImGui::AlignTextToFramePadding();
						ImGui::TextUnformatted(lbl.c_str());
						ImGui::SameLine(0.0f, gapIcon);
					}

					ed::BeginPin(ExecOutPin(n.Id, (int)r), ed::PinKind::Output);
					PinIcon(ImVec4(1, 1, 1, 1), execOutLinked(n.Id, (int)r), true);
					ed::EndPin();
				}

				ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH));
			}

			const std::size_t dataRows = std::max(n.Inputs.size(), n.Outputs.size());

			for (std::size_t r = 0; r < dataRows; ++r)
			{
				const ImVec2 rp = ImGui::GetCursorScreenPos();

				if (r < n.Inputs.size())
				{
					const RigPin& pin = n.Inputs[r];
					const bool linked = dataInLinked(n.Id, (int)r);

					ed::BeginPin(DataInPin(n.Id, (int)r), ed::PinKind::Input);
					PinIconFor(pin.Type, linked);
					ed::EndPin();

					ImGui::SameLine(0.0f, gapIcon);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(pin.Name.c_str());

					if (!linked && InlineEditorWidth(pin.Type) > 0.0f)
					{
						ImGui::SameLine(0.0f, gapIcon);
						DrawInlinePinEditor(n, (int)r);
					}
				}

				if (r < n.Outputs.size())
				{
					const RigPin& pin = n.Outputs[r];

					const float w = ImGui::CalcTextSize(pin.Name.c_str()).x + gapIcon + kPinIcon;

					ImGui::SetCursorScreenPos(ImVec2(rp.x + nodeW - w, rp.y));

					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(pin.Name.c_str());
					ImGui::SameLine(0.0f, gapIcon);

					ed::BeginPin(DataOutPin(n.Id, (int)r), ed::PinKind::Output);
					PinIconFor(pin.Type, dataOutLinked(n.Id, (int)r));
					ed::EndPin();
				}

				ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH));
			}

			ImGui::PopID();

			ed::EndNode();
			ed::PopStyleColor(2);
		}

		ed::PopStyleVar(2);

		// ── Fios ─────────────────────────────────────────────────────────────
		//
		// Execucao mais GROSSO que dados, de proposito: quando os dois se
		// cruzam, a espessura diz qual e a espinha do grafo.
		{
			const auto& execLinks = graph.GetExecLinks();

			for (std::size_t i = 0; i < execLinks.size(); ++i)
			{
				const auto& l = execLinks[i];

				ed::Link(kExecLinkBase + (int)i,
					ExecOutPin(l.FromNode, l.FromExec),
					ExecInPin(l.ToNode),
					ImVec4(1.0f, 1.0f, 1.0f, 0.9f), 2.6f);
			}

			const auto& dataLinks = graph.GetDataLinks();

			for (std::size_t i = 0; i < dataLinks.size(); ++i)
			{
				const auto& l = dataLinks[i];

				const RigNode* from = graph.FindNode(l.FromNode);

				const ImVec4 col = (from && l.FromPin < (int)from->Outputs.size())
					? ColorForPin(from->Outputs[l.FromPin].Type)
					: ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

				ed::Link(kDataLinkBase + (int)i,
					DataOutPin(l.FromNode, l.FromPin),
					DataInPin(l.ToNode, l.ToPin),
					col, 1.8f);
			}
		}

		// ── Criar fio ────────────────────────────────────────────────────────
		if (ed::BeginCreate(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), 2.5f))
		{
			ed::PinId a, b;

			if (ed::QueryNewLink(&a, &b))
			{
				int pa = (int)a.Get();
				int pb = (int)b.Get();

				// O node-editor nao garante ordem: normalizamos pra saida->entrada.
				if (IsExecIn(pa) || IsDataIn(pa))
					std::swap(pa, pb);

				const bool execPair = IsExecOut(pa) && IsExecIn(pb);
				const bool dataPair = IsDataOut(pa) && IsDataIn(pb);

				const int nodeA = PinNode(pa);
				const int nodeB = PinNode(pb);

				RigNode* na = graph.FindNode(nodeA);
				RigNode* nb = graph.FindNode(nodeB);

				bool ok = (execPair || dataPair) && na && nb && nodeA != nodeB;

				// Tipos precisam bater. Ligar um Float numa entrada de
				// Transform nao tem conversao obvia — e aceitar em silencio
				// produziria um valor zerado sem explicacao.
				if (ok && dataPair)
				{
					const int io = DataOutIndex(pa);
					const int ii = DataInIndex(pb);

					// Wildcard casa com qualquer tipo — e o que permite o Reroute
					// existir sem um no por tipo.
					ok = io < (int)na->Outputs.size()
						&& ii < (int)nb->Inputs.size()
						&& (na->Outputs[io].Type == nb->Inputs[ii].Type
							|| na->Outputs[io].Type == RigPinType::Wildcard
							|| nb->Inputs[ii].Type == RigPinType::Wildcard);

					// ── CICLO DE DADOS ───────────────────────────────────
					//
					// Ligar A -> B faz B DEPENDER de A. Se A ja depende de B
					// (por qualquer caminho), o fio fecha um laco: o valor
					// passa a precisar de si mesmo.
					//
					// Antes isso so era detectado NA EXECUCAO, e o preco era um
					// erro por frame no console e um valor zerado no meio do
					// grafo. Recusar na hora de ligar e melhor: o laco nunca
					// chega a existir.
					if (ok)
					{
						std::vector<int> stack{ nodeA };
						std::vector<int> seen;

						while (!stack.empty())
						{
							const int cur = stack.back();
							stack.pop_back();

							if (cur == nodeB)
							{
								ok = false;
								break;
							}

							if (std::find(seen.begin(), seen.end(), cur) != seen.end())
								continue;

							seen.push_back(cur);

							// Quem alimenta `cur`?
							for (const auto& l : graph.GetDataLinks())
								if (l.ToNode == cur)
									stack.push_back(l.FromNode);
						}
					}
				}

				if (!ok)
				{
					ed::RejectNewItem(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), 2.0f);
				}
				else if (ed::AcceptNewItem(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), 3.0f))
				{
					if (execPair)
					{
						graph.LinkExec(nodeA, ExecOutIndex(pa), nodeB);

						// Um reroute INDECISO nas pontas se decide agora: o par
						// de pinos de dados some e sobra so a corrente.
						if (auto* r = dynamic_cast<RigNode_Reroute*>(na))
							if (r->CurrentMode == RigNode_Reroute::Mode::Undecided)
								r->SetMode(RigNode_Reroute::Mode::Exec);

						if (auto* r = dynamic_cast<RigNode_Reroute*>(nb))
							if (r->CurrentMode == RigNode_Reroute::Mode::Undecided)
								r->SetMode(RigNode_Reroute::Mode::Exec);
					}
					else
					{
						const int io = DataOutIndex(pa);
						const int ii = DataInIndex(pb);

						// Um lado Wildcard ADOTA o tipo do outro, nos dois
						// sentidos: um reroute pode receber o tipo de quem
						// entra ou de quem sai dele.
						if (na->Outputs[io].Type == RigPinType::Wildcard)
							na->AdoptType(nb->Inputs[ii].Type);
						else if (nb->Inputs[ii].Type == RigPinType::Wildcard)
							nb->AdoptType(na->Outputs[io].Type);

						graph.LinkData(nodeA, io, nodeB, ii);
					}

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
				if (!ed::AcceptDeletedItem())
					continue;

				const int id = (int)lid.Get();

				if (id >= kDataLinkBase)
				{
					const int i = id - kDataLinkBase;
					const auto& dl = graph.GetDataLinks();

					if (i >= 0 && i < (int)dl.size())
					{
						graph.UnlinkDataInput(dl[i].ToNode, dl[i].ToPin);
						MarkEdited("Disconnect");
					}
				}
				else if (id >= kExecLinkBase)
				{
					const int i = id - kExecLinkBase;
					const auto& el = graph.GetExecLinks();

					if (i >= 0 && i < (int)el.size())
					{
						graph.UnlinkExec(el[i].FromNode, el[i].FromExec);
						MarkEdited("Disconnect");
					}
				}
			}

			ed::NodeId nid;

			while (ed::QueryDeletedNode(&nid))
			{
				const int id = (int)nid.Get();
				const RigNode* n = graph.FindNode(id);

				// O evento nao se apaga: sem ele o rig nao roda, e apagar por
				// acidente daria um rig mudo sem nenhuma pista do motivo.
				if (n)
				{
					const std::string tn = n->TypeName();

					// Entry e Return sao a FRONTEIRA da funcao: sem eles nao ha
					// por onde o dado entrar nem sair, e os nos Call ficariam
					// com pinos sem contraparte.
					if (tn == "ForwardsSolve" || tn == "BackwardsSolve"
						|| tn == "Entry" || tn == "Return")
					{
						ed::RejectDeletedItem();
						continue;
					}
				}

				if (!ed::AcceptDeletedItem())
					continue;

				graph.RemoveNode(id);

				if (m_SelectedNode == id)
					m_SelectedNode = -1;

				MarkEdited("Delete node");
			}
		}
		ed::EndDelete();

		// ── Menu de fundo: paleta ────────────────────────────────────────────
		ed::Suspend();

		if (ed::ShowBackgroundContextMenu())
		{
			m_MenuCanvasPos = ed::ScreenToCanvas(ImGui::GetMousePos());
			ImGui::OpenPopup("rig_palette");
		}

		// ── Soltar um elemento da hierarquia ─────────────────────────────────
		//
		// Dentro do Suspend de proposito: aqui o ImGui esta em espaco de TELA
		// normal, entao tanto o rect quanto o ScreenToCanvas dao o valor certo.
		// Tentar isto no meio do canvas transformado erra a posicao.
		{
			const ImRect dropRect(canvasMin,
				ImVec2(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y));

			if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("rig_graph_drop")))
			{
				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RIG_ELEMENT"))
				{
					const auto* d = (const RigDragPayload*)pl->Data;

					m_DropName = d->Name;
					m_DropType = (RigElementType)d->Type;
					m_DropPos = ed::ScreenToCanvas(ImGui::GetMousePos());

					ImGui::OpenPopup("rig_drop_menu");
				}

				// ── Funcao solta no grafo ────────────────────────────────────
				//
				// Sem menu, ao contrario do osso: soltar um osso e ambiguo (Get
				// ou Set?), soltar uma funcao so pode significar uma coisa —
				// chamar. Perguntar aqui seria cerimonia sem escolha.
				if (const ImGuiPayload* pf = ImGui::AcceptDragDropPayload("RIG_FUNCTION"))
				{
					m_DropFunction = (const char*)pf->Data;
					m_DropPos = ed::ScreenToCanvas(ImGui::GetMousePos());
				}

				ImGui::EndDragDropTarget();
			}
		}

		// Fora do bloco de drop: criar o no mexe no grafo, e o RebuildCallSites
		// percorre todos os grafos do asset — nao no meio do alvo de drag.
		if (!m_DropFunction.empty())
		{
			const std::string fname = m_DropFunction;
			m_DropFunction.clear();

			if (auto node = CreateRigNode("CallFunction"))
			{
				auto* call = static_cast<RigNode_CallFunction*>(node.get());

				call->FunctionName = fname;
				call->Title = fname;

				node->EditorX = m_DropPos.x;
				node->EditorY = m_DropPos.y;

				const int id = graph.AddNode(std::move(node));

				ed::SetNodePosition(id, m_DropPos);

				// Os pinos vem da definicao. Sem isto o no nasceria sem nenhum,
				// e so apareceriam depois de alguem mexer na assinatura.
				const auto& funcs = m_Asset->GetFunctions();

				for (int i = 0; i < (int)funcs.size(); ++i)
					if (funcs[(std::size_t)i].Name == fname)
					{
						RebuildFunctionCallSites(i);
						break;
					}

				m_SelectedNode = id;
				m_SelectedElement = -1;

				MarkEdited("Add function call");
			}
		}

		if (ImGui::BeginPopup("rig_drop_menu"))
		{
			ImGui::TextDisabled("%s", ShortItemName(m_DropName));
			ImGui::Separator();

			// Get ou Set: a duvida real ao soltar um osso no grafo. Criar um
			// dos dois por conta seria adivinhar, e metade das vezes errado.
			auto spawn = [&](const char* type)
				{
					auto node = CreateRigNode(type);

					if (!node)
						return;

					// O pino 0 desses dois nos e sempre o Item.
					if (!node->Inputs.empty() && node->Inputs[0].Type == RigPinType::Item)
					{
						node->Inputs[0].Default.ItemName = m_DropName;
						node->Inputs[0].Default.ItemType = m_DropType;
					}

					node->EditorX = m_DropPos.x;
					node->EditorY = m_DropPos.y;

					const int id = graph.AddNode(std::move(node));

					ed::SetNodePosition(id, m_DropPos);

					m_SelectedNode = id;
					m_SelectedElement = -1;

					MarkEdited("Add node");
				};

			// ── O MENU SEGUE O QUE FOI SOLTO ─────────────────────────────
			//
			// Um controle de CANAL nao tem posicao no espaco: oferecer Get/Set
			// Transform pra ele seria oferecer um caminho que nao leva a lugar
			// nenhum — e a pessoa so descobre isso depois de criar o no e
			// procurar um pino que nao existe.
			const auto& hd = m_Asset->GetHierarchy();
			const int dropped = hd.Find(m_DropName, m_DropType);

			const bool isChannel = (dropped >= 0)
				&& hd[dropped].Type == RigElementType::Control
				&& hd[dropped].ValueType != RigControlValue::Transform;

			if (isChannel)
			{
				if (ImGui::MenuItem("Get Control Value"))
					spawn("GetControlValue");
			}
			else
			{
				if (ImGui::MenuItem("Get Transform"))
					spawn("GetTransform");

				if (ImGui::MenuItem("Set Transform"))
					spawn("SetTransform");
			}

			// ── SOLTOU VARIOS: vira uma LISTA ────────────────────────────
			//
			// A selecao nao vai no payload — ela e lida direto do membro. Nao
			// da pra "chegar velha": o arrasto e o drop acontecem no mesmo
			// gesto, e nada mexe na hierarquia entre um e outro.
			if (m_Selection.size() > 1)
			{
				ImGui::Separator();

				char label[64];
				std::snprintf(label, sizeof(label), "Item Array  (%d itens)",
					(int)m_Selection.size());

				if (ImGui::MenuItem(label))
				{
					auto node = CreateRigNode("ItemArray");

					if (auto* arr = dynamic_cast<RigNode_ItemArray*>(node.get()))
					{
						const auto& h = m_Asset->GetHierarchy();

						// Na ORDEM DA HIERARQUIA, nao na ordem dos cliques: uma
						// cadeia de FK so faz sentido de pai pra filho, e
						// reordenar seis itens a mao no painel e o tipo de
						// tarefa que o computador faz melhor.
						std::vector<int> sorted = m_Selection;
						std::sort(sorted.begin(), sorted.end());

						for (const int idx : sorted)
						{
							if (idx < 0 || idx >= (int)h.Size())
								continue;

							RigItemRef r;
							r.Name = h[idx].Name;
							r.Type = h[idx].Type;

							arr->Items.push_back(std::move(r));
						}

						node->EditorX = m_DropPos.x;
						node->EditorY = m_DropPos.y;

						const int id = graph.AddNode(std::move(node));

						ed::SetNodePosition(id, m_DropPos);

						m_SelectedNode = id;
						m_SelectedElement = -1;

						MarkEdited("Add node");
					}
				}
			}

			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("rig_palette"))
		{
			// Colar no ponto onde o menu foi aberto. So aparece quando ha algo
			// pra colar: um item permanentemente cinza vira ruido.
			if (!m_Clipboard.Nodes.empty())
			{
				if (ImGui::MenuItem("Colar", "Ctrl+V"))
					PasteNodes(m_MenuCanvasPos);

				ImGui::Separator();
			}

			struct Entry { const char* Label; const char* Type; };

			// Agrupada por PAPEL, nao alfabetica: e assim que se procura um no
			// ("preciso escrever num osso"), nao pela letra inicial.
			static const Entry kFlow[] = {
				{ "Sequence", "Sequence" },
				{ "Branch",   "Branch" },
				{ "For Each", "ForEach" },
			};

			static const Entry kRead[] = {
				{ "Get Transform", "GetTransform" },
				{ "Ground Trace",  "GroundTrace" },
				{ "Trace",         "Trace" },
				{ "Item Array",    "ItemArray" },
				{ "At",            "At" },
				{ "Get Control Value", "GetControlValue" },
				{ "Project to New Parent", "ProjectToNewParent" },
			};

			static const Entry kFunc[] = {
				{ "Call Function", "CallFunction" },
			};

			static const Entry kEvents[] = {
				{ "Backward Solve", "BackwardsSolve" },
			};

			static const Entry kWrite[] = {
				{ "Set Transform", "SetTransform" },
				{ "Set Control Pose", "SetControlPose" },
				{ "Two Bone IK",   "TwoBoneIK" },
				{ "FK Chain",      "FKChain" },
				{ "Parent Constraint", "ParentConstraint" },
				{ "Hide Controls", "HideControls" },
				{ "Align To Vector", "AlignToVector" },
				{ "Pelvis Dip",    "PelvisDip" },
			};

			static const Entry kOrganize[] = {
				{ "Comment", "Comment" },
				{ "Reroute", "Reroute" },
			};

			static const Entry kMath[] = {
				{ "Make Transform",  "MakeTransform" },
				{ "Break Transform", "BreakTransform" },
				{ "Vector Op",       "VectorOp" },
				{ "Float Math",      "FloatMath" },
				{ "Select Float",    "SelectFloat" },
				{ "Damp Float",      "DampFloat" },
				{ "Damp Vector",     "DampVector" },
			};

			auto emit = [&](const Entry* list, int count)
				{
					for (int i = 0; i < count; ++i)
					{
						if (!ImGui::MenuItem(list[i].Label))
							continue;

						auto node = CreateRigNode(list[i].Type);

						if (!node)
							continue;

						node->EditorX = m_MenuCanvasPos.x;
						node->EditorY = m_MenuCanvasPos.y;

						const int id = graph.AddNode(std::move(node));

						// Posiciona SO o no novo. Nao mexemos na flag global de
						// posicoes, que reposicionaria todos e desfaria o
						// arranjo que voce ja montou.
						ed::SetNodePosition(id, m_MenuCanvasPos);

						m_SelectedNode = id;
						m_SelectedElement = -1;

						MarkEdited("Add node");
					}
				};

			// IM_ARRAYSIZE e nao um numero digitado: a contagem a mao ja
			// escondeu uma entrada de menu neste projeto (o "Particle Burst"
			// do script editor, que ficou invisivel por meses porque a tabela
			// dizia 11 com 12 entradas). Agora acrescentar uma linha na tabela
			// basta.
			ImGui::TextDisabled("Events");
			emit(kEvents, IM_ARRAYSIZE(kEvents));

			ImGui::Separator();
			ImGui::TextDisabled("Functions");
			emit(kFunc, IM_ARRAYSIZE(kFunc));

			ImGui::Separator();
			ImGui::TextDisabled("Flow");
			emit(kFlow, IM_ARRAYSIZE(kFlow));

			ImGui::Separator();
			ImGui::TextDisabled("Read");
			emit(kRead, IM_ARRAYSIZE(kRead));

			ImGui::Separator();
			ImGui::TextDisabled("Write");
			emit(kWrite, IM_ARRAYSIZE(kWrite));

			ImGui::Separator();
			ImGui::TextDisabled("Math");
			emit(kMath, IM_ARRAYSIZE(kMath));

			ImGui::Separator();
			ImGui::TextDisabled("Organize");
			emit(kOrganize, IM_ARRAYSIZE(kOrganize));

			ImGui::EndPopup();
		}

		// ── Duplo clique num Call abre a funcao ──────────────────────────────
		//
		// Dentro do Begin/End porque GetDoubleClickedNode e do node-editor; a
		// TROCA de grafo fica pra depois do End, senao o node-editor passaria
		// metade do frame apontando pra um grafo e metade pra outro.
		if (const ed::NodeId dbl = ed::GetDoubleClickedNode())
		{
			if (auto* call = dynamic_cast<RigNode_CallFunction*>(
				graph.FindNode((int)dbl.Get())))
			{
				const auto& funcs = m_Asset->GetFunctions();

				for (int i = 0; i < (int)funcs.size(); ++i)
					if (funcs[(std::size_t)i].Name == call->FunctionName)
					{
						m_PendingOpenFunction = i;
						break;
					}
			}
		}

		ed::Resume();
		ed::End();

		// DEPOIS do End, pelo motivo acima.
		if (m_PendingOpenFunction >= 0)
		{
			SwitchToFunction(m_PendingOpenFunction);
			m_PendingOpenFunction = -1;
			return;
		}

		// ── Copiar / Recortar / Colar / Duplicar ─────────────────────────────
		//
		// DEPOIS do ed::End() de proposito: dentro do Begin/End o node-editor e
		// dono do teclado — e o mesmo motivo pelo qual o Delete e tratado la
		// dentro, via QueryDeletedNode, e nao aqui.
		//
		// A guarda de foco nao e zelo excessivo: sem ela, um Ctrl+V digitado no
		// campo "filter..." da hierarquia despejaria nos no grafo. E o
		// IsAnyItemActive cobre o caso de estar editando um valor de pino
		// dentro do proprio canvas.
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
				// cai num degrau diagonal a partir de onde foi copiado — sem
				// isso a copia nasceria exatamente em cima da original e
				// pareceria que nada aconteceu.
				const bool overCanvas =
					ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

				PasteNodes(overCanvas
					? ed::ScreenToCanvas(ImGui::GetMousePos())
					: ImVec2(m_Clipboard.AnchorX + 40.0f, m_Clipboard.AnchorY + 40.0f));
			}

			// Ctrl+D: duplicar sem passar pela area de transferencia do ponto
			// de vista do usuario. Internamente E copiar e colar — o que
			// significa que ele SOBRESCREVE o clipboard, igual ao Blueprint.
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
			{
				CopySelectedNodes(false);
				PasteNodes(ImVec2(m_Clipboard.AnchorX + 40.0f,
					m_Clipboard.AnchorY + 40.0f));
			}
		}

		// ── Duplo-clique num fio insere um REROUTE ───────────────────────────
		//
		// O gesto que todo mundo tenta primeiro quando um fio atravessa meio
		// grafo. O no nasce no ponto clicado e o fio original vira DOIS.
		//
		// Repare que copiamos a struct do link antes de mexer: LinkExec e
		// LinkData apagam e reinserem no vetor, entao uma REFERENCIA pro
		// elemento viraria lixo no meio da operacao.
		{
			// Exige o fio HOVERED alem do duplo-clique: sem isso, dois cliques
			// rapidos enquanto voce arruma um no podiam cair num fio proximo e
			// inserir um reroute que voce nao pediu — religando o grafo pelas
			// costas.
			const int lid = (ed::GetHoveredLink().Get() != 0)
				? (int)ed::GetDoubleClickedLink().Get()
				: 0;

			if (lid > 0)
			{
				ImVec2 pos = ed::ScreenToCanvas(ImGui::GetMousePos());

				// Centraliza a pilula no ponto clicado.
				pos.x -= 14.0f;
				pos.y -= 12.0f;

				if (lid >= kDataLinkBase)
				{
					const int i = lid - kDataLinkBase;
					const auto& links = graph.GetDataLinks();

					if (i >= 0 && i < (int)links.size())
					{
						const RigDataLink l = links[i];

						if (auto node = CreateRigNode("Reroute"))
						{
							// Adota o tipo de quem alimenta o fio, pra o
							// reroute ja nascer com a cor e a validacao certas.
							if (const RigNode* from = graph.FindNode(l.FromNode))
								if (l.FromPin < (int)from->Outputs.size())
									node->AdoptType(from->Outputs[l.FromPin].Type);

							node->EditorX = pos.x;
							node->EditorY = pos.y;

							const int id = graph.AddNode(std::move(node));

							ed::SetNodePosition(id, pos);

							// A segunda ligacao SUBSTITUI o fio original, porque
							// LinkData troca o que estiver na entrada de destino.
							graph.LinkData(l.FromNode, l.FromPin, id, 0);
							graph.LinkData(id, 0, l.ToNode, l.ToPin);

							m_SelectedNode = id;
							m_SelectedElement = -1;

							MarkEdited("Insert reroute");
						}
					}
				}
				else if (lid >= kExecLinkBase)
				{
					const int i = lid - kExecLinkBase;
					const auto& links = graph.GetExecLinks();

					if (i >= 0 && i < (int)links.size())
					{
						const RigExecLink l = links[i];

						auto node = CreateRigNode("Reroute");

						// Nasce ja decidido: o fio que ele vai substituir e de
						// execucao.
						if (auto* r = dynamic_cast<RigNode_Reroute*>(node.get()))
							r->SetMode(RigNode_Reroute::Mode::Exec);

						if (node)
						{
							node->EditorX = pos.x;
							node->EditorY = pos.y;

							const int id = graph.AddNode(std::move(node));

							ed::SetNodePosition(id, pos);

							graph.LinkExec(l.FromNode, l.FromExec, id);
							graph.LinkExec(id, 0, l.ToNode);

							m_SelectedNode = id;
							m_SelectedElement = -1;

							MarkEdited("Insert reroute");
						}
					}
				}
			}
		}

		// ── Seletor de elemento (fora do canvas) ─────────────────────────────
		//
		// Aqui ja estamos em espaco de TELA normal, entao o popup abre onde
		// deve. Com busca: 70 ossos numa lista rolavel e pior que tres letras.
		if (m_ItemPickerOpen)
		{
			ImGui::OpenPopup("rig_item_picker");
			m_ItemPickerOpen = false;
		}

		if (ImGui::BeginPopup("rig_item_picker"))
		{
			RigNode* target = graph.FindNode(m_ItemPickerNode);

			if (!target || m_ItemPickerPin < 0 || m_ItemPickerPin >= (int)target->Inputs.size())
			{
				ImGui::CloseCurrentPopup();
			}
			else
			{
				ImGui::SetNextItemWidth(220.0f);

				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();

				ImGui::InputTextWithHint("##pf", "search...",
					m_ItemPickerFilter, sizeof(m_ItemPickerFilter));

				ImGui::Separator();

				const auto& h = m_Asset->GetHierarchy();

				std::string filt = m_ItemPickerFilter;
				for (auto& c : filt) c = (char)std::tolower((unsigned char)c);

				ImGui::BeginChild("##pl", ImVec2(220.0f, 260.0f));

				for (std::size_t i = 0; i < h.Size(); ++i)
				{
					const RigElement& e = h[(int)i];

					if (!filt.empty())
					{
						std::string lname = e.Name;
						for (auto& c : lname) c = (char)std::tolower((unsigned char)c);

						if (lname.find(filt) == std::string::npos)
							continue;
					}

					ImGui::PushID((int)i);

					if (ImGui::Selectable(ShortItemName(e.Name),
						e.Name == target->Inputs[m_ItemPickerPin].Default.ItemName))
					{
						target->Inputs[m_ItemPickerPin].Default.ItemName = e.Name;
						target->Inputs[m_ItemPickerPin].Default.ItemType = e.Type;

						MarkEdited("Set item");
						ImGui::CloseCurrentPopup();
					}

					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", e.Name.c_str());

					ImGui::PopID();
				}

				ImGui::EndChild();
			}

			ImGui::EndPopup();
		}

		// ── Selecao ──────────────────────────────────────────────────────────
		{
			ed::NodeId sel = 0;

			if (ed::GetSelectedNodes(&sel, 1) > 0)
			{
				m_SelectedNode = (int)sel.Get();
				m_SelectedElement = -1;
			}
		}

		// Grava as posicoes TODO frame: barato, e um arrasto nunca se perde.
		for (const auto& n : graph.GetNodes())
		{
			const ImVec2 p = ed::GetNodePosition(n->Id);

			if (p.x != n->EditorX || p.y != n->EditorY)
			{
				n->EditorX = p.x;
				n->EditorY = p.y;
				MarkEdited("Move node");
			}
		}

		ed::SetCurrentEditor(nullptr);
	}

	// ═══ Copiar / Recortar ═══════════════════════════════════════════════════

	void ControlRigWindow::CopySelectedNodes(bool cut)
	{
		if (!m_Asset)
			return;

		// O grafo VISIVEL, nao o principal: copiar dentro de uma funcao tem que
		// pegar os nos que estao na tela.
		RigGraph& graph = CurrentGraph();

		// GetSelectedObjectCount conta nos E fios; GetSelectedNodes devolve
		// quantos eram nos de fato. Dimensionamos pelo total e confiamos no
		// retorno — um vetor curto seria escrita fora dos limites.
		const int total = ed::GetSelectedObjectCount();

		if (total <= 0)
			return;

		std::vector<ed::NodeId> sel((std::size_t)total);
		const int count = ed::GetSelectedNodes(sel.data(), total);

		// ── O QUE ENTRA ──────────────────────────────────────────────────────
		//
		// O evento fica de fora. O grafo roda procurando UM no ForwardsSolve;
		// um segundo faria metade do rig virar codigo morto sem nenhum aviso.
		// Mesma razao pela qual ele tambem nao se apaga.
		std::vector<int> ids;
		ids.reserve((std::size_t)count);

		for (int i = 0; i < count; ++i)
		{
			const int id = (int)sel[(std::size_t)i].Get();
			const RigNode* n = graph.FindNode(id);

			if (!n || std::string(n->TypeName()) == "ForwardsSolve")
				continue;

			ids.push_back(id);
		}

		if (ids.empty())
			return;

		m_Clipboard.Nodes.clear();
		m_Clipboard.ExecLinks.clear();
		m_Clipboard.DataLinks.clear();

		// Ancora: canto superior esquerdo do conjunto.
		float minX = graph.FindNode(ids.front())->EditorX;
		float minY = graph.FindNode(ids.front())->EditorY;

		for (int id : ids)
		{
			const RigNode* n = graph.FindNode(id);

			minX = std::min(minX, n->EditorX);
			minY = std::min(minY, n->EditorY);
		}

		m_Clipboard.AnchorX = minX;
		m_Clipboard.AnchorY = minY;

		// Id do no -> indice no clipboard. Precisamos dele pra reescrever os
		// fios em termos de posicao na lista, e nao de Id.
		std::unordered_map<int, int> index;

		for (int id : ids)
		{
			const RigNode* n = graph.FindNode(id);

			RigClipboard::Entry e;

			e.Node = n->Clone();
			e.DX = n->EditorX - minX;
			e.DY = n->EditorY - minY;

			index[id] = (int)m_Clipboard.Nodes.size();
			m_Clipboard.Nodes.push_back(std::move(e));
		}

		// ── OS FIOS ──────────────────────────────────────────────────────────
		//
		// So os INTERNOS, com as DUAS pontas na selecao. Reroute nao precisa de
		// tratamento especial: ele e um no como qualquer outro, entao entra na
		// selecao e seus fios entram junto.
		//
		// Fio que cruza a fronteira fica de fora, e nao por preguica:
		//
		//   uma ENTRADA de dados aceita um fio so, entao preservar uma origem
		//   externa roubaria a ligacao do no original;
		//
		//   uma SAIDA de execucao vai pra um lugar so, entao preservar uma
		//   entrada externa desviaria a corrente do original pra copia.
		//
		// Nos dois casos colar MEXERIA no que voce copiou — e colar nao pode
		// alterar o original. Blueprint e o Material Graph fazem igual.
		for (const auto& l : graph.GetDataLinks())
		{
			const auto a = index.find(l.FromNode);
			const auto b = index.find(l.ToNode);

			if (a == index.end() || b == index.end())
				continue;

			m_Clipboard.DataLinks.push_back({ a->second, l.FromPin, b->second, l.ToPin });
		}

		for (const auto& l : graph.GetExecLinks())
		{
			const auto a = index.find(l.FromNode);
			const auto b = index.find(l.ToNode);

			if (a == index.end() || b == index.end())
				continue;

			m_Clipboard.ExecLinks.push_back({ a->second, l.FromExec, b->second });
		}

		if (!cut)
			return;

		// RemoveNode ja leva os fios do no junto — inclusive os que iam pra
		// fora da selecao, que e o certo: eles apontariam pro nada.
		for (int id : ids)
		{
			graph.RemoveNode(id);

			if (m_SelectedNode == id)
				m_SelectedNode = -1;
		}

		ed::ClearSelection();
		MarkEdited("Cut nodes");
	}

	// ═══ Colar ═══════════════════════════════════════════════════════════════

	void ControlRigWindow::PasteNodes(const ImVec2& canvasPos)
	{
		if (!m_Asset || m_Clipboard.Nodes.empty())
			return;

		// Cola no grafo VISIVEL. E o que permite copiar no principal, abrir uma
		// funcao e colar dentro dela.
		RigGraph& graph = CurrentGraph();

		// Id novo de cada entrada do clipboard, na mesma ordem. -1 marca as que
		// falharam, pra um fio nao ser criado apontando pro vazio.
		std::vector<int> fresh(m_Clipboard.Nodes.size(), -1);

		for (std::size_t i = 0; i < m_Clipboard.Nodes.size(); ++i)
		{
			const auto& e = m_Clipboard.Nodes[i];

			// CLONE DO CLONE: o clipboard guarda o MOLDE. Mover o no de dentro
			// dele esvaziaria a area apos a primeira colagem — e colar duas
			// vezes e justamente o caso de uso (uma perna, depois a outra).
			auto copy = e.Node->Clone();

			if (!copy)
				continue;

			const ImVec2 pos(canvasPos.x + e.DX, canvasPos.y + e.DY);

			copy->EditorX = pos.x;
			copy->EditorY = pos.y;

			const int id = graph.AddNode(std::move(copy));

			if (id < 0)
				continue;

			fresh[i] = id;

			// O node-editor guarda a posicao no PROPRIO estado; sem isto o no
			// nasceria em (0,0) e so iria pro lugar certo na proxima abertura
			// do asset. Mesmo passo que a paleta ja faz.
			ed::SetNodePosition(id, pos);
		}

		for (const auto& l : m_Clipboard.DataLinks)
		{
			const int from = fresh[(std::size_t)l.FromNode];
			const int to = fresh[(std::size_t)l.ToNode];

			if (from >= 0 && to >= 0)
				graph.LinkData(from, l.FromPin, to, l.ToPin);
		}

		for (const auto& l : m_Clipboard.ExecLinks)
		{
			const int from = fresh[(std::size_t)l.FromNode];
			const int to = fresh[(std::size_t)l.ToNode];

			if (from >= 0 && to >= 0)
				graph.LinkExec(from, l.FromExec, to);
		}

		// A COPIA fica selecionada, nao o original. E ela que voce vai arrastar
		// e renomear em seguida — e sem isto o proximo Ctrl+D duplicaria a
		// coisa errada.
		ed::ClearSelection();

		m_SelectedNode = -1;

		for (int id : fresh)
		{
			if (id < 0)
				continue;

			ed::SelectNode(id, true);

			if (m_SelectedNode < 0)
			{
				m_SelectedNode = id;
				m_SelectedElement = -1;
			}
		}

		MarkEdited("Paste nodes");
	}

} // namespace axed