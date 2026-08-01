#include "control_rig_window.hpp"

#include <algorithm>
#include <cstdio>
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
			// ── VOLTA PRO OSSO DE ORIGEM, NAO PRA IDENTIDADE ─────────────
			//
			// Identidade joga o controle pra origem do rig. O que se quer ao
			// "resetar" um controle e devolve-lo pra cima do osso de onde ele
			// nasceu — que e o unico lugar onde o FK volta a ser neutro.
			//
			// Isso importa porque o gizmo escreve no INITIAL: mover um controle
			// pra testar muda o REPOUSO dele, e a partir dai o FK aplica esse
			// desvio pra sempre. E o que faz o membro "descer um pouco" sem
			// motivo aparente.
			const int src = e.SourceBone.empty()
				? -1
				: h.Find(e.SourceBone, RigElementType::Bone);

			e.Initial = (src >= 0) ? h[src].Initial : BoneTransform{};
			e.Current = e.Initial;

			m_EulerOwner[0] = -1;   // forca re-derivar o Euler exibido
			MarkEdited("Reset to bind pose");
		}

		if (!e.SourceBone.empty() && ImGui::IsItemHovered())
			ImGui::SetTooltip("Volta pro repouso de '%s'.", e.SourceBone.c_str());

		// ── Forma do controle ────────────────────────────────────────────────
		if (e.Type != RigElementType::Control)
			return;

		ImGui::Spacing();
		ImGui::Separator();

		// ── CONTROLE DE CANAL ────────────────────────────────────────────────
		//
		// Um controle que carrega so um VALOR, pra ser lido pelo grafo e
		// animado depois pelo sequencer. E assim que se faz um interruptor de
		// IK/FK: o Branch le este valor em vez de um numero digitado no no, que
		// ninguem conseguiria animar.
		{
			static const char* kValueTypes[] = { "Transform", "Bool", "Float" };

			int vt = (int)e.ValueType;

			if (ImGui::Combo("Value type", &vt, kValueTypes, 3))
			{
				e.ValueType = (RigControlValue)vt;
				MarkEdited("Change control type");
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(
					"Transform: o controle normal, com forma no viewport.\n\n"
					"Bool / Float: um CANAL — sem forma, so um valor.\n"
					"Leia com o no Get Control Value.");
			}

			if (e.ValueType == RigControlValue::Bool)
			{
				if (ImGui::Checkbox("Value", &e.BoolValue))
					MarkEdited("Edit control value");
			}
			else if (e.ValueType == RigControlValue::Float)
			{
				if (ImGui::DragFloat("Value", &e.FloatValue, 0.01f, 0.0f, 1.0f))
					MarkEdited("Edit control value");
			}
		}

		// Canal nao tem forma pra configurar: nao ha o que desenhar nem agarrar.
		if (e.ValueType != RigControlValue::Transform)
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

		// ── Pose do animador ─────────────────────────────────────────────────
		//
		// Um controle posado e um em repouso sao IDENTICOS na tela — mesma
		// forma, mesma cor. Sem este aviso, "por que este pe nao acompanha a
		// animacao?" nao tem resposta visivel em lugar nenhum.
		if (e.Type != RigElementType::Bone)
		{
			auto& h = m_Asset->GetHierarchy();

			if (h.HasValue(index))
			{
				ImGui::Separator();

				ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.20f, 1.0f), "Posed");

				ImGui::TextDisabled("Este controle foi movido em modo Pose.");
				ImGui::TextDisabled("A pose soma por cima do repouso.");

				if (ImGui::Button("Clear pose"))
				{
					h.ClearValue(index);
					MarkEdited("Clear pose");
				}

				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Volta ao neutro. O repouso nao e tocado.");
			}
		}
	}

	void ControlRigWindow::DrawNodeDetails(int nodeId)
	{
		// O no selecionado esta no grafo visivel — dentro de uma funcao, ele
		// nao existe no principal e o painel ficaria vazio.
		RigGraph& graph = CurrentGraph();
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
			// A ORDEM tem que espelhar o enum Op, que e serializado como
			// inteiro. IM_ARRAYSIZE e nao um numero digitado: a contagem a mao
			// ja escondeu uma entrada de menu neste projeto.
			static const char* kOps[] =
			{
				"Add", "Subtract", "Scale", "Lerp", "Cross", "Normalize"
			};

			int op = (int)v->Operation;

			if (ImGui::Combo("Operation", &op, kOps, IM_ARRAYSIZE(kOps)))
			{
				v->Operation = (RigNode_VectorOp::Op)op;

				// O titulo segue a operacao enquanto voce nao renomear: um
				// "Vector Op" generico no meio do grafo nao diz o que faz.
				v->Title = std::string("Vector ") + kOps[op];

				MarkEdited("Change operation");
			}

			ImGui::TextDisabled("Scale usa Factor; Lerp usa Factor como alpha.");
			ImGui::TextDisabled("Cross usa A e B; Normalize usa so A.");
			ImGui::Spacing();
			ImGui::TextDisabled("Direcao do personagem sem depender de eixo de osso:\n"
				"  lado   = Normalize(ombroL.Location - ombroR.Location)\n"
				"  frente = Cross(lado, (0,1,0))");
			return;
		}

		if (auto* f = dynamic_cast<RigNode_FloatMath*>(n))
		{
			static const char* kOps[] = {
				"Add", "Subtract", "Multiply", "Divide",
				"Min", "Max", "Clamp", "Lerp", "Abs"
			};

			int op = (int)f->Operation;

			if (ImGui::Combo("Operation", &op, kOps, IM_ARRAYSIZE(kOps)))
			{
				f->Operation = (RigNode_FloatMath::Op)op;

				// Os PINOS seguem a operacao: Clamp mostra Value/Min/Max, Lerp
				// mostra A/B/Alpha. Os fios ligam por INDICE, entao renomear
				// nao quebra ligacao nenhuma.
				f->ApplyOperation();

				f->Title = std::string("Float ") + kOps[op];

				MarkEdited("Change operation");
			}

			ImGui::TextDisabled("Os nomes dos pinos acompanham a operacao.");
			return;
		}

		if (dynamic_cast<RigNode_DampFloat*>(n) || dynamic_cast<RigNode_DampVector*>(n))
		{
			ImGui::TextWrapped("Persegue o valor de entrada em vez de saltar nele. "
				"Speed maior = mais rapido e mais duro; 0 desliga a suavizacao.");
			ImGui::Spacing();
			ImGui::TextDisabled("O primeiro frame entra direto no valor, sem subir de zero.");
			return;
		}


		if (dynamic_cast<RigNode_AlignToVector*>(n))
		{
			ImGui::TextWrapped("Inclina o elemento pela mesma rotacao que leva From "
				"ate To. Pro pe acompanhar a rampa: From = (0,1,0) e "
				"To = a Normal do Ground Trace.");
			ImGui::Spacing();
			ImGui::TextDisabled("Nao supoe eixo nenhum do osso — por isso funciona "
				"em qualquer rig.");
			return;
		}

		// ── Entry / Return: a assinatura da funcao ───────────────────────────
		//
		// Editavel AQUI, e nao numa janela separada: quando voce esta olhando o
		// Entry, "quais sao as entradas desta funcao" e exatamente a pergunta
		// que voce tem. Mexer aqui reconstroi os dois nos e todas as chamadas.
		if (dynamic_cast<RigNode_Entry*>(n) || dynamic_cast<RigNode_Return*>(n))
		{
			const bool isEntry = (dynamic_cast<RigNode_Entry*>(n) != nullptr);

			if (m_EditingFunction < 0
				|| m_EditingFunction >= (int)m_Asset->GetFunctions().size())
			{
				ImGui::TextDisabled("Este no so faz sentido dentro de uma funcao.");
				return;
			}

			RigFunction& fn =
				m_Asset->GetFunctions()[(std::size_t)m_EditingFunction];

			std::vector<RigFunctionParam>& params = isEntry ? fn.Inputs : fn.Outputs;

			ImGui::TextWrapped(isEntry
				? "As ENTRADAS da funcao. Cada uma vira um pino de saida aqui e um "
				"pino de entrada em cada chamada."
				: "As SAIDAS da funcao. Cada uma vira um pino de entrada aqui e um "
				"pino de saida em cada chamada.");

			ImGui::Spacing();
			ImGui::Separator();

			static const char* kTypes[] =
			{
				"Exec", "Bool", "Float", "Vector", "Transform", "Item",
				"Wildcard", "ItemArray"
			};

			int remove = -1;
			bool changed = false;

			for (int i = 0; i < (int)params.size(); ++i)
			{
				ImGui::PushID(700 + i);

				char buf[64];
				std::snprintf(buf, sizeof(buf), "%s", params[(std::size_t)i].Name.c_str());

				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);

				if (ImGui::InputText("##nm", buf, sizeof(buf)))
				{
					params[(std::size_t)i].Name = buf;
					changed = true;
				}

				ImGui::SameLine();

				int t = (int)params[(std::size_t)i].Type;

				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);

				if (ImGui::Combo("##tp", &t, kTypes, IM_ARRAYSIZE(kTypes)))
				{
					params[(std::size_t)i].Type = (RigPinType)t;
					changed = true;
				}

				ImGui::SameLine();

				if (ImGui::SmallButton("x"))
					remove = i;

				ImGui::PopID();
			}

			if (remove >= 0)
			{
				params.erase(params.begin() + remove);
				changed = true;
			}

			ImGui::Spacing();

			if (ImGui::SmallButton(isEntry ? "+ Input" : "+ Output"))
			{
				RigFunctionParam p;
				p.Name = isEntry
					? "In" + std::to_string(params.size())
					: "Out" + std::to_string(params.size());

				params.push_back(std::move(p));
				changed = true;
			}

			if (changed)
			{
				// Reconstroi Entry, Return e TODA chamada. Sem isto, mexer na
				// assinatura deixaria as chamadas com os pinos velhos — e os
				// fios pousariam nos pinos errados.
				RebuildFunctionCallSites(m_EditingFunction);
				MarkEdited("Edit function signature");
			}

			return;
		}

		// ── Call Function ────────────────────────────────────────────────────
		if (auto* call = dynamic_cast<RigNode_CallFunction*>(n))
		{
			auto& funcs = m_Asset->GetFunctions();

			ImGui::TextDisabled("Funcao chamada:");

			if (ImGui::BeginCombo("##fn", call->FunctionName.empty()
				? "(nenhuma)" : call->FunctionName.c_str()))
			{
				for (int i = 0; i < (int)funcs.size(); ++i)
				{
					const bool sel = (funcs[(std::size_t)i].Name == call->FunctionName);

					if (ImGui::Selectable(funcs[(std::size_t)i].Name.c_str(), sel))
					{
						call->FunctionName = funcs[(std::size_t)i].Name;
						call->Title = call->FunctionName;

						// Os pinos vem da definicao escolhida.
						RebuildFunctionCallSites(i);
						MarkEdited("Set called function");
					}
				}

				ImGui::EndCombo();
			}

			ImGui::Spacing();

			if (!call->FunctionName.empty() && !m_Asset->FindFunction(call->FunctionName))
			{
				ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.20f, 1.0f), "Funcao nao existe");
				ImGui::TextDisabled("Ela foi apagada ou renomeada. Este no\nnao faz nada.");
			}

			return;
		}

		if (dynamic_cast<RigNode_Trace*>(n))
		{
			ImGui::TextWrapped("Raycast em qualquer direcao. Pra sondar o ambiente: "
				"parede a frente, teto acima, beirada ao lado.");
			ImGui::Spacing();
			ImGui::TextDisabled("Distance e Start Offset em METROS.");
			ImGui::TextDisabled("A saida Distance vem em espaco de componente.");
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.20f, 1.0f), "Start Offset");
			ImGui::TextDisabled("O raycast NAO ignora o proprio personagem. Um traco\n"
				"do peito pra frente comeca dentro da capsula e acertaria\n"
				"ele mesmo. Este offset empurra a origem pra fora — ponha\n"
				"um pouco mais que o raio da capsula.");
			ImGui::Spacing();
			ImGui::TextDisabled("No preview nao ha mundo: devolve sempre 'nao\n"
				"acertou'. Teste em Play.");
			return;
		}

		if (dynamic_cast<RigNode_BackwardsSolve*>(n))
		{
			ImGui::TextWrapped("Caminho INVERSO: le os ossos animados e encosta os "
				"controles neles.");
			ImGui::Spacing();
			ImGui::TextDisabled("Nao roda por frame. Roda quando voce aperta Backward solve' na barra — e, no futuro, quando o Sequencer carregar uma animacao.");
			ImGui::Spacing();
			ImGui::TextDisabled("Monte com: Get Transform (osso, Global) -> Set Control Pose (controle).");
			return;
		}

		if (dynamic_cast<RigNode_SetControlPose*>(n))
		{
			ImGui::TextWrapped("Poe um controle numa posicao e faz GRUDAR.");
			ImGui::Spacing();
			ImGui::TextDisabled("O Set Transform comum escreve no Current, que e apagado no comeco de cada solve. Este escreve na pose do controle, que sobrevive.");
			ImGui::Spacing();
			ImGui::TextDisabled("Transform em espaco GLOBAL do rig.");
			ImGui::TextDisabled("So Control e Null. Pra osso, use Set Transform.");
			return;
		}

		if (dynamic_cast<RigNode_PelvisDip*>(n))
		{
			ImGui::TextWrapped("Abaixa o quadril ate o pe MAIS BAIXO alcancar o "
				"chao. Ligue os pinos Height dos dois Ground Trace dos pes.");
			ImGui::Spacing();
			ImGui::TextDisabled("Ponha ANTES dos Two Bone IK das pernas.");
			ImGui::TextDisabled("SO DESCE: levantar o quadril faria o personagem\n"
				"flutuar, e o pe alto ja e resolvido dobrando o joelho.");
			ImGui::Spacing();
			ImGui::TextDisabled("Max Dip esta em METROS; Height esta em espaco\n"
				"de componente. A conversao e feita dentro do no.");
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