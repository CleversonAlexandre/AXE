#include "control_rig_window.hpp"
#include "axe/log/log.hpp"

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>

#include <cstring>

namespace axe
{
	namespace
	{
		// Cor por especie. E a leitura mais rapida da arvore: osso e neutro,
		// controle e o que voce agarra (amarelo, como na Unreal), null e o
		// agrupador discreto.
		ImVec4 ColorFor(RigElementType t)
		{
			switch (t)
			{
			case RigElementType::Control: return ImVec4(1.00f, 0.82f, 0.25f, 1.0f);
			case RigElementType::Null:    return ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
			default:                      return ImVec4(0.80f, 0.82f, 0.88f, 1.0f);
			}
		}

		// Nome sem o prefixo de namespace: "mixamorig:LeftHandThumb3" vira
		// "LeftHandThumb3".
		//
		// O prefixo e o mesmo em TODOS os ossos, entao ele nao distingue nada
		// — so consome largura justamente onde a arvore ja indenta fundo. O
		// nome completo continua no tooltip, porque e ele que voce digita nos
		// nos do grafo.
		const char* ShortName(const std::string& full)
		{
			const std::size_t p = full.find_last_of(":|");
			return (p == std::string::npos) ? full.c_str() : full.c_str() + p + 1;
		}

		const char* GlyphFor(RigElementType t)
		{
			switch (t)
			{
			case RigElementType::Control: return "o";
			case RigElementType::Null:    return "+";
			default:                      return "-";
			}
		}
	}

	void ControlRigWindow::DrawElementContextMenu(int index)
	{
		auto& h = m_Asset->GetHierarchy();

		if (index < 0 || index >= (int)h.Size())
			return;

		// Adicionar SEMPRE cria como FILHO do elemento clicado. E a operacao
		// que faz um rig: o controle do pe nasce pendurado no osso do pe, e
		// ja herda a posicao dele.
		// UM SO item, que se ajusta ao que voce clicou.
		//
		// Antes havia dois ("Add Control child" e "Create FK control") e o
		// primeiro criava um controle DESALINHADO, parenteado no proprio osso.
		// Quem montasse o rig inteiro pelo caminho obvio descobria o problema
		// so na hora de usar, e teria que refazer tudo. Um caminho errado que
		// existe e um caminho que alguem vai seguir.
		if (ImGui::MenuItem("Add Control"))
			m_PendingAddControl = index;

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(h[index].Type == RigElementType::Bone
				? "Cria o controle EM CIMA do osso, ja alinhado a ele\n"
				"e pendurado no PAI dele — pronto pra FK ou IK."
				: "Cria um controle filho deste elemento.");
		}

		// Atalho pro caso mais comum: controle de IK precisa ficar FORA da
		// cadeia que ele dirige, senao ele e arrastado pelo proprio resultado.
		if (h[index].Parent >= 0 && ImGui::MenuItem("Move to root"))
		{
			m_PendingReparent = index;
			m_PendingReparentTo = -1;
		}

		if (ImGui::MenuItem("Add Null child"))
			m_PendingAddNull = index;

		ImGui::Separator();

		if (ImGui::MenuItem("Rename"))
		{
			m_Renaming = index;
			m_RenameFocus = true;
			std::snprintf(m_RenameBuf, sizeof(m_RenameBuf), "%s", h[index].Name.c_str());
		}

		// Osso nao se apaga: ele espelha o esqueleto. Apagar aqui so criaria
		// divergencia com o .axeskel, e o SyncNewBones traria ele de volta na
		// proxima abertura.
		const bool isBone = (h[index].Type == RigElementType::Bone);

		if (ImGui::MenuItem("Delete", nullptr, false, !isBone))
			m_PendingRemove = index;

		if (isBone && ImGui::IsItemHovered())
			ImGui::SetTooltip("Bones come from the skeleton and are not deleted here.");
	}

	namespace
	{
		// Achou um elemento pela carga do arrasto. Nome + TIPO, porque Bone e
		// Control podem ter o mesmo nome.
		int FindDragged(const RigHierarchy& h, const RigDragPayload& d)
		{
			return h.Find(d.Name, (RigElementType)d.Type);
		}

		void SubmitDragSource(const RigElement& e, int extra)
		{
			if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
				return;

			RigDragPayload d{};
			std::snprintf(d.Name, sizeof(d.Name), "%s", e.Name.c_str());
			d.Type = (int)e.Type;

			ImGui::SetDragDropPayload("RIG_ELEMENT", &d, sizeof(d));

			// Preview arrastado junto do cursor: sem ele o gesto nao da retorno
			// nenhum e parece que nao pegou.
			ImGui::TextUnformatted(e.Name.c_str());

			if (extra > 0)
				ImGui::Text("+ %d selecionado(s)", extra);

			ImGui::EndDragDropSource();
		}
	}

	// Campo de rename in-place: SUBSTITUI a linha em vez de abrir popup —
	// menos cliques, e o nome fica onde voce esta olhando.
	bool ControlRigWindow::DrawRenameField(int index)
	{
		if (m_Renaming != index)
			return false;

		auto& h = m_Asset->GetHierarchy();

		ImGui::SetNextItemWidth(-1.0f);

		// SO no primeiro frame.
		//
		// Chamar SetKeyboardFocusHere todo frame reancora o foco sem parar: o
		// campo e re-focado antes de processar o que voce digitou, e o texto
		// nunca entra. Era esse um dos dois motivos de "nao consigo renomear".
		if (m_RenameFocus)
		{
			ImGui::SetKeyboardFocusHere();
			m_RenameFocus = false;
		}

		const bool entered = ImGui::InputText("##rename", m_RenameBuf, sizeof(m_RenameBuf),
			ImGuiInputTextFlags_EnterReturnsTrue);

		// Enter OU clicar fora depois de editar: os dois APLICAM.
		//
		// Antes, clicar fora descartava em silencio — voce digitava o nome,
		// clicava noutro lugar e o trabalho sumia sem dizer nada.
		if (entered || ImGui::IsItemDeactivatedAfterEdit())
		{
			if (h.Rename(index, m_RenameBuf))
				MarkEdited("Rename element");

			m_Renaming = -1;
		}
		else if (ImGui::IsItemDeactivated())
		{
			m_Renaming = -1;   // saiu sem mexer em nada
		}

		return true;
	}

	void ControlRigWindow::DrawElementNode(int index)
	{
		auto& h = m_Asset->GetHierarchy();
		const RigElement& e = h[index];

		// Tem filhos? Decide se a arvore desenha a seta de expandir.
		bool hasChildren = false;

		for (std::size_t c = index + 1; c < h.Size(); ++c)
			if (h[c].Parent == index) { hasChildren = true; break; }

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DefaultOpen;

		if (!hasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		if (IsSelected(index))
			flags |= ImGuiTreeNodeFlags_Selected;

		ImGui::PushID(index);

		if (DrawRenameField(index))
		{
			ImGui::PopID();
			return;
		}

		ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(e.Type));

		const std::string label = std::string(GlyphFor(e.Type)) + "  " + ShortName(e.Name);
		const bool opened = ImGui::TreeNodeEx("##el", flags, "%s", label.c_str());

		ImGui::PopStyleColor();

		// O nome COMPLETO no tooltip: e ele que vai nos pinos Item do grafo.
		if (ImGui::IsItemHovered() && e.Name != ShortName(e.Name))
			ImGui::SetTooltip("%s", e.Name.c_str());

		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			// ── CLICAR NUM ITEM JA SELECIONADO NAO DESFAZ A SELECAO ──────
			//
			// IsItemClicked dispara no BOTAO DESCENDO — que e tambem o comeco
			// de um arrasto. Trocar a selecao aqui zerava a selecao multipla no
			// instante em que voce comecava a arrastar, e so o item clicado
			// chegava no grafo.
			//
			// Com Ctrl, alterna normalmente. Sem Ctrl e ja selecionado, so
			// troca qual e o PRINCIPAL (o que o Detalhes mostra).
			const bool ctrl = ImGui::GetIO().KeyCtrl;

			if (ctrl || !IsSelected(index))
				SelectElement(index, ctrl);
			else
				m_SelectedElement = index;
		}

		SubmitDragSource(e, (int)m_Selection.size() - 1);

		// ── SOLTAR AQUI = REPARENTEAR ────────────────────────────────────
		//
		// E o gesto que se espera de uma arvore. Sem ele, mudar um controle de
		// pai exigiria apagar e refazer — perdendo forma, tamanho e posicao.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RIG_ELEMENT"))
			{
				const auto* d = (const RigDragPayload*)pl->Data;
				const int src = FindDragged(h, *d);

				if (src >= 0 && src != index)
				{
					m_PendingReparent = src;
					m_PendingReparentTo = index;
				}
			}

			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextItem("##ctx"))
		{
			if (!IsSelected(index))
				SelectElement(index, false);

			DrawElementContextMenu(index);
			ImGui::EndPopup();
		}

		if (opened && hasChildren)
		{
			for (std::size_t c = index + 1; c < h.Size(); ++c)
				if (h[c].Parent == index)
					DrawElementNode((int)c);

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	void ControlRigWindow::DrawHierarchyPanel()
	{
		if (!m_Asset)
			return;

		auto& h = m_Asset->GetHierarchy();

		ImGui::TextDisabled("Hierarchy");
		ImGui::Separator();

		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##filter", "filter...", m_Filter, sizeof(m_Filter));

		ImGui::Checkbox("Bones", &m_ShowBones);
		ImGui::SameLine();
		ImGui::Checkbox("Ctrl", &m_ShowControls);
		ImGui::SameLine();
		ImGui::Checkbox("Null", &m_ShowNulls);

		ImGui::Separator();

		ImGui::TextDisabled("Ctrl+clique = selecao multipla  |  arraste pro grafo");
		ImGui::Separator();

		const bool filtering = (m_Filter[0] != '\0');

		if (filtering)
		{
			// Com filtro a arvore vira LISTA CHAPADA, de proposito: manter a
			// arvore obrigaria a mostrar pais que nao casam so pra chegar nos
			// filhos que casam — e ai o filtro nao filtra nada.
			for (std::size_t i = 0; i < h.Size(); ++i)
			{
				const RigElement& e = h[i];

				if (e.Type == RigElementType::Bone && !m_ShowBones)       continue;
				if (e.Type == RigElementType::Control && !m_ShowControls) continue;
				if (e.Type == RigElementType::Null && !m_ShowNulls)       continue;

				// Busca sem diferenciar maiuscula.
				std::string lname = e.Name;
				std::string lfilt = m_Filter;

				for (auto& ch : lname) ch = (char)std::tolower((unsigned char)ch);
				for (auto& ch : lfilt) ch = (char)std::tolower((unsigned char)ch);

				if (lname.find(lfilt) == std::string::npos)
					continue;

				ImGui::PushID((int)i);

				// A lista chapada tambem precisa do campo de rename. Sem isto,
				// renomear COM FILTRO ATIVO simplesmente nao acontecia: o menu
				// marcava m_Renaming e ninguem desenhava o campo.
				if (DrawRenameField((int)i))
				{
					ImGui::PopID();
					continue;
				}

				ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(e.Type));

				const std::string label = std::string(GlyphFor(e.Type)) + "  " + ShortName(e.Name);

				if (ImGui::Selectable(label.c_str(), IsSelected((int)i)))
				{
					const bool ctrl = ImGui::GetIO().KeyCtrl;

					if (ctrl || !IsSelected((int)i))
						SelectElement((int)i, ctrl);
					else
						m_SelectedElement = (int)i;
				}

				SubmitDragSource(e, (int)m_Selection.size() - 1);

				ImGui::PopStyleColor();

				if (ImGui::BeginPopupContextItem("##ctx"))
				{
					if (!IsSelected((int)i))
						SelectElement((int)i, false);

					DrawElementContextMenu((int)i);
					ImGui::EndPopup();
				}

				ImGui::PopID();
			}
		}
		else
		{
			for (std::size_t i = 0; i < h.Size(); ++i)
				if (h[i].Parent < 0)
					DrawElementNode((int)i);
		}

		// ── Acoes pendentes ──────────────────────────────────────────────────
		//
		// Executadas so AQUI, depois que a arvore terminou de desenhar. Mexer
		// na hierarquia no meio do percurso invalidaria os indices e o resto do
		// frame desenharia lixo.

		if (m_PendingAddControl >= 0 || m_PendingAddNull >= 0)
		{
			const bool isControl = (m_PendingAddControl >= 0);
			const int  clicked = isControl ? m_PendingAddControl : m_PendingAddNull;

			m_PendingAddControl = -1;
			m_PendingAddNull = -1;

			if (clicked >= 0 && clicked < (int)h.Size())
			{
				const RigElementType type = isControl
					? RigElementType::Control
					: RigElementType::Null;

				// ── ONDE PENDURAR, E ONDE COLOCAR ────────────────────────
				//
				// Num OSSO: o controle vira IRMAO dele (pendurado no PAI) e
				// nasce EXATAMENTE em cima dele.
				//
				// Parentear no proprio osso seria circular — o solve move o
				// osso e arrasta o controle junto, entao a forma desenhada
				// foge do gizmo. Pendurando no PAI, a cadeia funciona
				// sozinha: como o pai tambem e dirigido pelo controle DELE,
				// tudo segue na ordem certa, que e exatamente como FK
				// funciona.
				//
				// Num Control ou Null: filho mesmo, com transform identidade —
				// aninhar controles e legitimo (um "master" movendo um grupo).
				const bool onBone = (h[clicked].Type == RigElementType::Bone);

				const int parent = onBone ? h[clicked].Parent : clicked;

				const std::string base = isControl ? "ctrl_" : "null_";
				const std::string ref = ShortName(h[clicked].Name);

				std::string name = base + ref;

				for (int n = 2; h.Find(name, type) >= 0; ++n)
					name = base + ref + "_" + std::to_string(n);

				const int idx = h.Add(name, type, parent);

				if (idx >= 0)
				{
					if (onBone)
					{
						// Mesmo local do osso = mesma posicao E mesma
						// orientacao que ele.
						h[idx].Initial = h[clicked].Initial;
					}
					else
					{
						// Identidade: nasce em cima do pai. Nascer na origem
						// apareceria longe do corpo.
						h[idx].Initial = BoneTransform{};
					}

					h[idx].Current = h[idx].Initial;

					if (isControl)
					{
						// ── ORIENTA O ANEL AO REDOR DO OSSO ──────────────
						//
						// A forma Circle e um anel no plano XZ, entao a normal
						// dela e +Y. Sem girar, ela fica DEITADA: uma elipse
						// atravessando o membro em vez de uma pulseira em
						// volta dele.
						//
						// Vai no SHAPE OFFSET, nao no Initial. O offset e so
						// DESENHO; girar o Initial mudaria a POSE DE REPOUSO
						// do osso quando o FK copiar o local do controle. E a
						// diferenca entre virar o desenho e torcer o boneco.
						float len = glm::length(h[idx].Initial.Translation);

						if (onBone)
						{
							int child = -1;

							for (std::size_t k = (std::size_t)clicked + 1; k < h.Size(); ++k)
								if (h[(int)k].Parent == clicked && h[(int)k].Type == RigElementType::Bone)
								{
									child = (int)k;
									break;
								}

							if (child >= 0)
							{
								const glm::vec3 d = h[child].Initial.Translation;

								if (glm::length2(d) > 1e-8f)
								{
									// Comprimento REAL do osso e a distancia
									// ate o FILHO; ate o PAI e o osso anterior,
									// e daria anel errado em ombro e quadril.
									len = glm::length(d);

									h[idx].ShapeOffset.Rotation = glm::rotation(
										glm::vec3(0.0f, 1.0f, 0.0f), glm::normalize(d));
								}
							}
						}

						h[idx].Shape = RigControlShape::Circle;

						// Proporcional ao osso: um valor fixo seria enorme num
						// rig em metros e invisivel num em centimetros.
						h[idx].ShapeSize = (len > 1e-4f) ? std::max(len * 0.35f, 0.02f) : 1.0f;
						h[idx].ShapeColor = glm::vec3(0.35f, 0.75f, 1.0f);
					}

					m_SelectedElement = idx;
					m_SelectedNode = -1;

					MarkEdited(isControl ? "Add control" : "Add null");
				}
			}
		}

		if (m_PendingReparentTo != -2)
		{
			const int src = m_PendingReparent;
			const int dst = m_PendingReparentTo;

			m_PendingReparent = -1;
			m_PendingReparentTo = -2;

			const int moved = h.Reparent(src, dst);

			if (moved >= 0)
			{
				// Os indices mudaram: a lista e reordenada pra manter pai antes
				// de filho, entao a selecao tem que acompanhar.
				m_SelectedElement = moved;
				m_Renaming = -1;

				MarkEdited("Reparent");
			}
		}

		if (m_PendingRemove >= 0)
		{
			const int removed = h.Remove(m_PendingRemove);

			if (removed > 1)
				AXE_EDITOR_INFO("Control Rig: {} element(s) removed (the element and its children).",
					removed);

			// A selecao apontava pra um indice que pode nem existir mais — e
			// os indices acima do removido MUDARAM. Zerar e o unico valor
			// seguro sem recalcular.
			m_SelectedElement = -1;
			m_Renaming = -1;
			m_PendingRemove = -1;

			MarkEdited("Delete element");
		}
	}

} // namespace axe