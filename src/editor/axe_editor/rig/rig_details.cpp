#include "control_rig_window.hpp"

#include <algorithm>
#include "axe/log/log.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════════
	//  PAINEL DETALHES — CONTROLRIG_V1
	//
	//  Mostra o que estiver selecionado: um ELEMENTO da hierarquia ou um NO do
	//  grafo. Nunca os dois — a selecao e mutuamente exclusiva, senao ficaria
	//  ambiguo o que voce esta editando.
	//
	//  NOTA DE LINKAGEM: este arquivo compila no editor.exe, e `BoneTransform`
	//  NAO e exportado da axe.dll — chamar ToMatrix()/FromMatrix() aqui daria
	//  unresolved external. Por isso mexemos SEMPRE nos campos soltos
	//  (Translation / Rotation / Scale), nunca em matriz.
	// ═════════════════════════════════════════════════════════════════════════

	namespace
	{
		// Nome sem o prefixo de namespace ("mixamorig:LeftArm" -> "LeftArm").
		const char* ShortName(const std::string& full)
		{
			const std::size_t p = full.find_last_of(":|");
			return (p == std::string::npos) ? full.c_str() : full.c_str() + p + 1;
		}

		// Quaternion <-> Euler em graus, so pra apresentacao.
		//
		// Ninguem digita quaternion a mao. A conversao de ida e volta perde
		// precisao em angulos extremos, entao so escrevemos de volta quando o
		// campo foi REALMENTE editado — reescrever todo frame acumularia
		// deriva no osso sem ninguem ter tocado em nada.
		glm::vec3 ToEuler(const glm::quat& q)
		{
			return glm::degrees(glm::eulerAngles(q));
		}

		glm::quat FromEuler(const glm::vec3& degrees)
		{
			return glm::quat(glm::radians(degrees));
		}

		// ── Linha de tres campos, no mesmo desenho do Script Editor ──────────
		//
		// Cada campo e uma caixa arredondada com a LETRA DO EIXO colorida
		// dentro, a esquerda do numero, e o nome do grupo em cima. E bem mais
		// legivel que o padrao do ImGui (rotulo solto a direita): o olho acha o
		// eixo pela cor, sem contar posicao.
		bool DragVec3Axes(const char* id, glm::vec3& v, float speed, const char* label)
		{
			static const char* kAxis[3] = { "X", "Y", "Z" };

			static const ImVec4 kCol[3] = {
				ImVec4(0.92f, 0.38f, 0.38f, 1.0f),
				ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
				ImVec4(0.42f, 0.62f, 0.98f, 1.0f) };

			bool changed = false;

			ImGui::PushID(id);

			ImGui::TextDisabled("%s", label);

			const ImGuiStyle& st = ImGui::GetStyle();

			const float total = ImGui::GetContentRegionAvail().x;
			const float gap = 4.0f;
			const float w = std::max(52.0f, (total - gap * 2.0f) / 3.0f);
			const float hgt = ImGui::GetFrameHeight();

			ImDrawList* dl = ImGui::GetWindowDrawList();

			for (int i = 0; i < 3; ++i)
			{
				if (i > 0)
					ImGui::SameLine(0.0f, gap);

				ImGui::PushID(i);

				const ImVec2 p = ImGui::GetCursorScreenPos();

				// A caixa e desenhada A MAO e o campo entra por cima com fundo
				// transparente — e o que permite a letra ficar DENTRO da
				// mesma moldura, em vez de num botao separado ao lado.
				dl->AddRectFilled(p, ImVec2(p.x + w, p.y + hgt),
					IM_COL32(28, 30, 36, 255), 4.0f);

				ImGui::SetCursorScreenPos(ImVec2(p.x + 6.0f, p.y + st.FramePadding.y));
				ImGui::TextColored(kCol[i], "%s", kAxis[i]);

				ImGui::SameLine(0.0f, 4.0f);

				ImGui::SetNextItemWidth(w - 22.0f);

				ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 16));
				ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 28));

				if (ImGui::DragFloat("##f", &v[i], speed, 0.0f, 0.0f, "%.3f"))
					changed = true;

				ImGui::PopStyleColor(3);

				ImGui::PopID();
			}

			ImGui::PopID();
			return changed;
		}
	}

	bool ControlRigWindow::DrawTransformEditor(int slot, int ownerId, BoneTransform& t)
	{
		bool changed = false;

		ImGui::PushID(slot);

		if (DragVec3Axes("loc", t.Translation, 0.01f, "Position"))
			changed = true;

		// ── Rotacao com Euler ESTAVEL ────────────────────────────────────────
		//
		// Ver o comentario do m_EulerCache no header: reconverter o quaternion
		// todo frame fazia mexer em UM campo saltar os outros dois. So
		// re-derivamos quando a selecao muda ou quando nada esta sendo
		// arrastado.
		if (m_EulerOwner[slot] != ownerId || !ImGui::IsAnyItemActive())
		{
			m_EulerCache[slot] = ToEuler(t.Rotation);
			m_EulerOwner[slot] = ownerId;
		}

		if (DragVec3Axes("rot", m_EulerCache[slot], 0.5f, "Rotation"))
		{
			t.Rotation = FromEuler(m_EulerCache[slot]);
			changed = true;
		}

		if (DragVec3Axes("scl", t.Scale, 0.01f, "Scale"))
		{
			// Escala zero achata o elemento e nunca e o que se quis dizer.
			for (int i = 0; i < 3; ++i)
				if (std::abs(t.Scale[i]) < 1e-4f)
					t.Scale[i] = 1e-4f;

			changed = true;
		}

		ImGui::PopID();
		return changed;
	}

	void ControlRigWindow::DrawElementDetails(int index)
	{
		auto& h = m_Asset->GetHierarchy();
		RigElement& e = h[index];

		static const char* kType[] = { "Bone", "Control", "Null" };

		ImGui::TextUnformatted(e.Name.c_str());
		ImGui::TextDisabled("%s", kType[(int)e.Type]);

		if (e.Parent >= 0)
			ImGui::TextDisabled("parent: %s", h[e.Parent].Name.c_str());

		ImGui::Spacing();
		ImGui::Separator();

		// ── Transform inicial ────────────────────────────────────────────────
		//
		// Editamos o INITIAL, nao o Current: o Current e resultado do solve
		// deste frame e seria sobrescrito no proximo. Initial e a pose de
		// repouso, que e o que de fato se autora.
		ImGui::TextDisabled("Initial transform (local to parent)");
		ImGui::Spacing();

		if (DrawTransformEditor(0, index, e.Initial))
		{
			// Sem solve rodando, Current acompanha — senao voce arrastaria o
			// campo e nada se moveria na tela.
			e.Current = e.Initial;
			MarkEdited("Edit transform");
		}

		if (ImGui::Button("Reset to bind pose", ImVec2(-1, 0)))
		{
			e.Initial = BoneTransform{};
			e.Current = e.Initial;
			m_EulerOwner[0] = -1;   // forca re-derivar o Euler exibido
			MarkEdited("Reset to bind pose");
		}

		// ── Forma do controle ────────────────────────────────────────────────
		if (e.Type != RigElementType::Control)
			return;

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("Control shape");
		ImGui::Spacing();

		static const char* kShapes[] = { "Circle", "Box", "Sphere", "Diamond", "Arrow" };

		int shape = (int)e.Shape;

		if (ImGui::Combo("Shape", &shape, kShapes, 5))
		{
			e.Shape = (RigControlShape)shape;
			MarkEdited("Change shape");
		}

		if (ImGui::ColorEdit3("Color", &e.ShapeColor.r))
			MarkEdited("Change color");

		if (ImGui::DragFloat("Size", &e.ShapeSize, 0.01f, 0.01f, 10.0f))
			MarkEdited("Change size");

		ImGui::Spacing();
		ImGui::TextDisabled("Shape offset");

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"Desloca so o DESENHO do gizmo, nao o pivo.\n\n"
				"O controle do pe pivota no tornozelo, mas o ponto util pra\n"
				"agarrar e a sola — e isto que separa as duas coisas.");
		}

		if (DrawTransformEditor(1, index, e.ShapeOffset))
			MarkEdited("Edit shape offset");
	}

	void ControlRigWindow::DrawNodeDetails(int nodeId)
	{
		RigGraph& graph = m_Asset->GetGraph();
		RigNode* n = graph.FindNode(nodeId);

		if (!n)
		{
			m_SelectedNode = -1;
			return;
		}

		ImGui::TextUnformatted(n->Title.c_str());
		ImGui::TextDisabled("%s", n->TypeName());

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Titulo editavel: num rig com seis Set Transform, "Set Transform"
		// seis vezes nao diz nada. "Plant left foot" diz.
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%s", n->Title.c_str());

			if (ImGui::InputText("Title", buf, sizeof(buf)))
			{
				n->Title = buf;
				MarkEdited("Rename node");
			}
		}

		ImGui::Spacing();

		static const char* kSpaces[] = { "Global", "Local" };

		// ── Get Transform ────────────────────────────────────────────────────
		if (auto* g = dynamic_cast<RigNode_GetTransform*>(n))
		{
			int space = (int)g->Space;

			if (ImGui::Combo("Space", &space, kSpaces, 2))
			{
				g->Space = (RigSpace)space;
				MarkEdited("Change space");
			}

			if (ImGui::Checkbox("Read initial", &g->Initial))
				MarkEdited("Toggle read initial");

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Le a pose de REPOUSO em vez da atual.\n"
					"E a referencia pra medir o quanto algo se afastou.");

			return;
		}

		// ── Set Transform ────────────────────────────────────────────────────
		if (auto* st = dynamic_cast<RigNode_SetTransform*>(n))
		{
			int space = (int)st->Space;

			if (ImGui::Combo("Space", &space, kSpaces, 2))
			{
				st->Space = (RigSpace)space;
				MarkEdited("Change space");
			}

			if (ImGui::Checkbox("Propagate to children", &st->PropagateToChildren))
				MarkEdited("Toggle propagate");

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(
					"Ligado: mexer no pai leva os filhos junto (o normal).\n\n"
					"Desligado: os filhos ficam onde estao — e o que permite\n"
					"reposicionar um pivo sem arrastar o corpo.");
			}

			return;
		}

		// ── Vector Op ────────────────────────────────────────────────────────
		if (auto* v = dynamic_cast<RigNode_VectorOp*>(n))
		{
			static const char* kOps[] = { "Add", "Subtract", "Scale", "Lerp" };

			int op = (int)v->Operation;

			if (ImGui::Combo("Operation", &op, kOps, 4))
			{
				v->Operation = (RigNode_VectorOp::Op)op;

				// O titulo segue a operacao enquanto voce nao renomear: um
				// "Vector Op" generico no meio do grafo nao diz o que faz.
				v->Title = std::string("Vector ") + kOps[op];

				MarkEdited("Change operation");
			}

			ImGui::TextDisabled("Scale usa Factor; Lerp usa Factor como alpha.");
			return;
		}

		// ── Item Array ───────────────────────────────────────────────────────
		//
		// A lista mora AQUI e nao no no do canvas de proposito: aqui estamos em
		// espaco de tela normal, entao combos e popups abrem onde devem — e o
		// no no grafo fica limpo, com um pino so.
		if (auto* arr = dynamic_cast<RigNode_ItemArray*>(n))
		{
			ImGui::TextDisabled("Items  (a ordem importa)");
			ImGui::Spacing();

			const auto& h = m_Asset->GetHierarchy();

			static const char* kTypes[] = { "Bone", "Control", "Null" };

			int remove = -1;
			int moveUp = -1;

			for (std::size_t i = 0; i < arr->Items.size(); ++i)
			{
				RigItemRef& it = arr->Items[i];

				ImGui::PushID((int)i);

				ImGui::Text("%d", (int)i);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(78.0f);

				int type = (int)it.Type;

				if (ImGui::Combo("##t", &type, kTypes, 3))
				{
					it.Type = (RigElementType)type;
					MarkEdited("Edit item array");
				}

				ImGui::SameLine();
				ImGui::SetNextItemWidth(-52.0f);

				if (ImGui::BeginCombo("##n", it.Name.empty() ? "(pick)" : ShortName(it.Name)))
				{
					// So os elementos do TIPO escolhido: uma lista com os 70
					// ossos misturados aos controles nao ajuda a achar nada.
					for (std::size_t k = 0; k < h.Size(); ++k)
					{
						const RigElement& e = h[(int)k];

						if (e.Type != it.Type)
							continue;

						ImGui::PushID((int)k);

						if (ImGui::Selectable(ShortName(e.Name), e.Name == it.Name))
						{
							it.Name = e.Name;
							MarkEdited("Edit item array");
						}

						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("%s", e.Name.c_str());

						ImGui::PopID();
					}

					ImGui::EndCombo();
				}

				// SOLTAR SOBRE O COMBO substitui este item. Mais curto que
				// abrir a lista e procurar entre 70 ossos.
				{
					std::string dn;
					RigElementType dt = RigElementType::Bone;

					if (AcceptRigElementDrop(dn, dt))
					{
						it.Name = dn;
						it.Type = dt;

						MarkEdited("Edit item array");
					}
				}

				// Reordenar importa: o FK Chain casa as duas listas por
				// POSICAO, entao a ordem e parte do significado.
				ImGui::SameLine();

				ImGui::BeginDisabled(i == 0);

				if (ImGui::SmallButton("^"))
					moveUp = (int)i;

				ImGui::EndDisabled();

				ImGui::SameLine();

				if (ImGui::SmallButton("x"))
					remove = (int)i;

				ImGui::PopID();
			}

			if (moveUp > 0)
			{
				std::swap(arr->Items[moveUp], arr->Items[moveUp - 1]);
				MarkEdited("Reorder item array");
			}

			if (remove >= 0)
			{
				arr->Items.erase(arr->Items.begin() + remove);
				MarkEdited("Edit item array");
			}

			ImGui::Spacing();

			// ── ZONA DE SOLTAR ───────────────────────────────────────────
			//
			// Aceita a SELECAO INTEIRA de uma vez: com Ctrl+clique voce marca a
			// cadeia na hierarquia, arrasta uma vez e a lista sai pronta. E o
			// mesmo gesto que cria um Item Array no grafo, so que somando aqui.
			ImGui::Button("...  arraste ossos ou controles aqui  ...", ImVec2(-1, 0));

			{
				std::string dn;
				RigElementType dt = RigElementType::Bone;

				if (AcceptRigElementDrop(dn, dt))
				{
					const auto& hh = m_Asset->GetHierarchy();

					// Varios selecionados: entram na ORDEM DA HIERARQUIA, que e
					// a unica que faz sentido pra uma cadeia de FK.
					if (m_Selection.size() > 1)
					{
						std::vector<int> sorted = m_Selection;
						std::sort(sorted.begin(), sorted.end());

						for (const int idx : sorted)
						{
							if (idx < 0 || idx >= (int)hh.Size())
								continue;

							RigItemRef r;
							r.Name = hh[idx].Name;
							r.Type = hh[idx].Type;

							arr->Items.push_back(std::move(r));
						}
					}
					else
					{
						RigItemRef r;
						r.Name = dn;
						r.Type = dt;

						arr->Items.push_back(std::move(r));
					}

					MarkEdited("Edit item array");
				}
			}

			ImGui::Spacing();

			if (ImGui::Button("Add item", ImVec2(-1, 0)))
			{
				// Herda o tipo do ultimo: uma lista costuma ser toda de ossos
				// ou toda de controles.
				RigItemRef r;

				if (!arr->Items.empty())
					r.Type = arr->Items.back().Type;

				arr->Items.push_back(r);
				MarkEdited("Edit item array");
			}

			ImGui::Spacing();
			ImGui::TextWrapped("Ligue duas listas num FK Chain — ossos de um lado, "
				"controles do outro, na MESMA ordem.");

			return;
		}

		// ── Sequence ─────────────────────────────────────────────────────────
		if (dynamic_cast<RigNode_Sequence*>(n))
		{
			ImGui::TextDisabled("Execution outputs");
			ImGui::Spacing();

			for (std::size_t i = 0; i < n->ExecOut.size(); ++i)
				ImGui::BulletText("%s", n->ExecOut[i].c_str());

			ImGui::Spacing();

			if (ImGui::Button("Add output", ImVec2(-1, 0)))
			{
				n->ExecOut.push_back("Then " + std::to_string(n->ExecOut.size()));
				MarkEdited("Add output");
			}

			// Nunca abaixo de duas: um Sequence com uma saida so e um no que
			// nao faz nada, e apagar a ultima deixaria a corrente sem
			// continuacao.
			const bool canRemove = n->ExecOut.size() > 2;

			if (ImGui::Button("Remove last", ImVec2(-1, 0)) && canRemove)
			{
				// O fio que saia dela precisa morrer JUNTO. Um fio apontando
				// pra um pino que nao existe mais e a origem classica do
				// "apaguei algo e o editor comecou a se comportar estranho".
				graph.UnlinkExec(n->Id, (int)n->ExecOut.size() - 1);

				n->ExecOut.pop_back();
				MarkEdited("Remove output");
			}

			if (!canRemove)
				ImGui::TextDisabled("(minimo de duas saidas)");

			return;
		}

		ImGui::TextDisabled("Este no nao tem opcoes proprias — tudo\n"
			"e configurado nos pinos, no grafo.");
	}

	void ControlRigWindow::DrawDetailsPanel()
	{
		if (!m_Asset)
			return;

		auto& h = m_Asset->GetHierarchy();

		if (m_SelectedNode >= 0)
		{
			DrawNodeDetails(m_SelectedNode);
			return;
		}

		if (m_SelectedElement >= 0 && m_SelectedElement < (int)h.Size())
		{
			DrawElementDetails(m_SelectedElement);
			return;
		}

		ImGui::TextWrapped("Select an element in the hierarchy, or a node in the graph.");
	}

} // namespace axe