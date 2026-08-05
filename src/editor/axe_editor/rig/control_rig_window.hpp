#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/rig/control_rig_asset.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/core/command_history.hpp"

#include "axe/utils/glm_config.hpp"

#include <entt/entity/fwd.hpp>
#include <imgui.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <memory>
#include <string>

namespace ed = ax::NodeEditor;

namespace axe
{
	// ── CARGA DO ARRASTO DA HIERARQUIA ───────────────────────────────────────
	//
	// Mora no header porque TRES arquivos precisam dela (arvore, canvas e
	// detalhes). Duplicar a struct em cada um funcionaria ate alguem mudar um
	// campo e esquecer os outros dois — e o erro so apareceria como lixo no
	// nome do osso.
	//
	// NOME + TIPO: Bone e Control podem ter o mesmo nome, e pegar o errado
	// deforma o personagem sem dizer nada.
	struct RigDragPayload
	{
		char Name[64];
		int  Type;
	};

	// Aceita um elemento solto SOBRE O ULTIMO WIDGET desenhado. Devolve true e
	// preenche os dois campos quando alguem soltou algo ali.
	inline bool AcceptRigElementDrop(std::string& outName, RigElementType& outType)
	{
		bool got = false;

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RIG_ELEMENT"))
			{
				const auto* d = (const RigDragPayload*)pl->Data;

				outName = d->Name;
				outType = (RigElementType)d->Type;
				got = true;
			}

			ImGui::EndDragDropTarget();
		}

		return got;
	}

	class ViewportRenderer;
	class Framebuffer;
	class Scene;
	class SceneEnvironment;
	class AnimationWorld;

	// ═════════════════════════════════════════════════════════════════════════
	//  CONTROL RIG WINDOW — CONTROLRIG_V1
	//
	//  Quatro paineis, como o Script Editor:
	//
	//    Hierarquia — a arvore de Bones, Controls e Nulls. E aqui que voce
	//                 CRIA controle: botao direito num osso, "Adicionar
	//                 Control filho".
	//    Preview    — o personagem, com os gizmos dos controles.
	//    Grafo      — o Rig Graph (Forwards Solve).
	//    Detalhes   — o que estiver selecionado, seja elemento ou no.
	//
	//  DIVIDIDO EM ARQUIVOS DESDE O INICIO, de proposito. O anim_graph_window
	//  virou um arquivo de 3 mil linhas por ter comecado com "so mais um
	//  painel aqui". A repartição:
	//
	//    control_rig_window.cpp  — a casca: abrir, salvar, layout, toolbar
	//    rig_hierarchy_panel.cpp — a arvore de elementos
	//    rig_graph_canvas.cpp    — o canvas de nos
	//    rig_details.cpp         — o painel de propriedades
	//    rig_preview.cpp         — o viewport e os gizmos
	// ═════════════════════════════════════════════════════════════════════════
	class ControlRigWindow
	{
	public:
		ControlRigWindow() = default;
		~ControlRigWindow();

		void OpenForAsset(const std::shared_ptr<ControlRigAsset>& rig,
			const std::shared_ptr<SkeletalMeshAsset>& skeleton);

		void Draw();
		void Close();

		bool IsOpen() const { return m_Open; }

		const std::shared_ptr<ControlRigAsset>& GetAsset() const { return m_Asset; }

	private:
		// ── Paineis (cada um no seu arquivo) ─────────────────────────────────
		void DrawToolbar();
		void DrawDockLayout(ImGuiID dockspaceId);
		void DrawHierarchyPanel();
		void DrawGraphCanvas();
		void DrawDetailsPanel();

		// Detalhes por selecao (rig_details.cpp).
		void DrawElementDetails(int index);

		// Editor de transform com eixos rotulados e Euler estavel.
		bool DrawTransformEditor(int slot, int ownerId, BoneTransform& t);
		void DrawNodeDetails(int nodeId);

		// ── Preview 3D (rig_preview.cpp) ─────────────────────────────────────
		void InitPreviewScene();
		void SyncPreviewCharacter();
		void RenderPreview();
		void DrawPreviewWindow();
		void HandlePreviewInput();

		// Gizmo de arrasto do controle selecionado.
		void DrawManipulator(const ImVec2& imgMin, const ImVec2& imgSize);

		// Roda o Forwards Solve e empurra o resultado pro personagem do
		// preview. Sem isto o rig e decorativo: voce edita o grafo e nada
		// muda na tela.
		void SolveRigIntoPreview();

		// Desenha as formas dos Controls sobre a imagem e devolve o indice do
		// que estiver sob o mouse (-1 se nenhum).
		int  DrawControlGizmos(const ImVec2& imgMin, const ImVec2& imgSize);

		// Auxiliares da arvore, em rig_hierarchy_panel.cpp.
		void DrawElementNode(int index);

		// Editor do valor digitado num pino solto (rig_graph_canvas.cpp).
		void DrawInlinePinEditor(RigNode& node, int pinIndex);
		void DrawElementContextMenu(int index);

		// Campo de rename in-place. Devolve true enquanto estiver ativo, pra o
		// chamador pular o desenho normal da linha.
		bool DrawRenameField(int index);

		// Marca sujo e ANOTA a acao pro undo.
		//
		// O comando nao e criado aqui: um arrasto chama isto DEZENAS de vezes,
		// e cada frame viraria um passo de undo — voce apertaria Ctrl+Z trinta
		// vezes pra desfazer um movimento so. O comando so e fechado quando o
		// gesto termina (ver CommitPendingUndo).
		void MarkEdited(const char* action = nullptr)
		{
			m_Dirty = true;

			// Qualquer edicao pode ter mexido na DEFINICAO — criar um controle,
			// espelhar um lado, mudar um pino. A copia do preview pode estar
			// velha, entao ela reclona no proximo acesso.
			//
			// O gizmo em Setup e a excecao: ele ja escreve nos dois lados e
			// revalida a copia, pra o arraste nao piscar.
			InvalidatePreviewHierarchy();

			if (action && !m_PendingUndo)
			{
				m_PendingUndo = true;
				m_PendingUndoName = action;
			}
		}
		bool SaveAsset();

		std::shared_ptr<ControlRigAsset>   m_Asset;
		std::shared_ptr<SkeletalMeshAsset> m_Skeleton;

		// ── Undo / Redo ──────────────────────────────────────────────────────
		//
		// Por SNAPSHOT, nao por comando fino.
		//
		// Um rig muda de formas muito diferentes — apagar um elemento
		// reindexa metade da hierarquia, apagar um no leva os fios junto.
		// Escrever o inverso exato de cada operacao seria muito codigo e um
		// deles estaria errado. Guardar o estado inteiro e barato aqui (algumas
		// centenas de elementos) e nao tem como divergir.
		struct RigSnapshot
		{
			std::vector<RigElement> Elements;
			RigGraph                Graph;

			// As FUNCOES tambem. Sem isto, criar uma funcao, mexer na
			// assinatura ou apagar nao seria desfeito — e pior: um Ctrl+Z
			// restauraria o grafo principal com nos Call apontando pra uma
			// definicao que o undo nao trouxe de volta.
			//
			// Copiavel por valor porque RigGraph tem copia profunda.
			std::vector<RigFunction> Functions;
		};

		RigSnapshot CaptureState() const;
		void        RestoreState(const RigSnapshot& snap);

		// Fecha o comando pendente, se o gesto ja acabou.
		void CommitPendingUndo();

		void DoUndo();
		void DoRedo();

		CommandHistory m_History;

		// Estado no fim do ultimo comando — serve de "antes" do proximo, entao
		// guardamos UM snapshot por comando em vez de dois.
		std::shared_ptr<RigSnapshot> m_Baseline;

		bool        m_PendingUndo = false;
		std::string m_PendingUndoName;

		// Alguma janela do rig esta em foco? Ctrl+Z so age quando sim — senao
		// mexeria no historico daqui enquanto voce trabalha noutro editor.
		bool m_Focused = false;

		bool m_Open = false;
		bool m_Dirty = false;

		// ── Selecao ──────────────────────────────────────────────────────────
		//
		// Elemento e no sao selecoes SEPARADAS e mutuamente exclusivas: o
		// painel Detalhes mostra uma coisa de cada vez, e manter as duas vivas
		// deixaria ambiguo o que voce esta editando.
		int m_SelectedElement = -1;

		// Selecao MULTIPLA da hierarquia (Ctrl+clique). m_SelectedElement
		// continua sendo o "principal" — o que o painel Detalhes mostra.
		//
		// Existe pra arrastar uma cadeia inteira pro grafo de uma vez: montar
		// um Item Array item a item, num braco de tres juntas, e trabalho
		// repetitivo que o computador faz melhor.
		std::vector<int> m_Selection;

		bool IsSelected(int index) const
		{
			return std::find(m_Selection.begin(), m_Selection.end(), index) != m_Selection.end();
		}

		// Clique normal troca a selecao; Ctrl+clique adiciona ou tira.
		void SelectElement(int index, bool additive)
		{
			if (!additive)
			{
				m_Selection.clear();
				m_Selection.push_back(index);
			}
			else
			{
				const auto it = std::find(m_Selection.begin(), m_Selection.end(), index);

				if (it != m_Selection.end())
					m_Selection.erase(it);
				else
					m_Selection.push_back(index);
			}

			m_SelectedElement = m_Selection.empty() ? -1 : m_Selection.back();
			m_SelectedNode = -1;
		}
		int m_SelectedNode = -1;

		// ── Area de transferencia do grafo ───────────────────────────────────
		//
		// Guarda CLONES, nao Ids.
		//
		// Um clipboard de Ids parece mais barato ate voce recortar: os nos
		// somem do grafo e a colagem passa a apontar pro nada. Guardando o
		// objeto, Copiar e Recortar viram a mesma operacao com um passo a mais,
		// e colar continua funcionando depois de apagar o original.
		//
		// Clone() ja copia tudo que e autoral — titulo, pinos, defaults (os
		// nomes de osso escolhidos) e o estado proprio de cada no: o Space do
		// Get Transform, os Items do Item Array, o modo do Reroute. E o mesmo
		// mecanismo da copia profunda do RigGraph, entao nao existe um segundo
		// caminho que possa sair de sincronia com ele.
		struct RigClipboard
		{
			struct Entry
			{
				std::unique_ptr<RigNode> Node;

				// Posicao RELATIVA ao canto superior esquerdo do conjunto. E o
				// que preserva o arranjo: colar oito nos empilhados num ponto
				// so nao serviria pra nada.
				float DX = 0.0f;
				float DY = 0.0f;
			};

			std::vector<Entry> Nodes;

			// Fios INTERNOS. Os campos FromNode/ToNode aqui sao INDICES no
			// vetor acima, nao Ids de no — uma colagem cria Ids novos, e
			// guardar os antigos daria fio apontando pro original.
			std::vector<RigExecLink> ExecLinks;
			std::vector<RigDataLink> DataLinks;

			// Canto superior esquerdo de onde o conjunto foi copiado. Serve pro
			// degrau diagonal do Ctrl+D e do Ctrl+V sem mouse sobre o canvas.
			float AnchorX = 0.0f;
			float AnchorY = 0.0f;
		};

		RigClipboard m_Clipboard;

		// cut = true recorta (copia e depois apaga). Sao a mesma funcao porque
		// a unica diferenca e um laco de RemoveNode no fim.
		void CopySelectedNodes(bool cut);
		void PasteNodes(const ImVec2& canvasPos);

		// ── Estado da arvore ─────────────────────────────────────────────────
		char m_Filter[64] = {};

		bool m_ShowBones = true;
		bool m_ShowControls = true;
		bool m_ShowNulls = true;

		// Elemento sendo renomeado in-place (-1 = nenhum).
		int  m_Renaming = -1;

		// Primeiro frame do rename? So nele o foco e forcado — ver o helper.
		bool m_RenameFocus = false;
		char m_RenameBuf[64] = {};

		// Acoes decididas durante o desenho da arvore e executadas DEPOIS.
		//
		// Mexer na hierarquia no meio do loop que a percorre invalida os
		// indices — a arvore passaria a desenhar lixo no resto do frame.
		int m_PendingRemove = -1;
		int m_PendingAddControl = -1;
		int m_PendingAddNull = -1;

		// Reparentear. -2 = nenhum pedido; -1 E um destino valido (a raiz),
		// entao nao da pra usar -1 como "vazio".
		int m_PendingReparent = -1;
		int m_PendingReparentTo = -2;

		// ── Setup x Pose ─────────────────────────────────────────────────────
		//
		// O que o gizmo escreve. E o modo em que VOCE esta, nao propriedade de
		// cada controle — por isso um alternador so, ao lado do T/R/S, e nao um
		// campo por elemento. Voce olha um lugar e sabe onde esta.
		//
		//   Setup — escreve no Initial. Monta o rig: alinha o controle no osso,
		//           define o repouso. Vai pro ASSET, vale pra toda instancia,
		//           aparece no jogo na hora.
		//
		//   Pose  — escreve no Value. Posa o personagem por cima do repouso.
		//           E o que um Sequencer vai keyar.
		//
		// ── NASCE EM POSE ────────────────────────────────────────────────────
		//
		// Nasceu em Setup por um tempo, pra preservar o comportamento anterior
		// ao modo existir. Foi a escolha errada: Setup escreve no ASSET, e o
		// asset e compartilhado com a cena viva. Quem so queria conferir uma
		// pose girava um controle e movia o personagem que estava rodando.
		//
		// O padrao tem que ser o modo SEGURO. Pose mexe so na copia do preview;
		// pra alterar a definicao do rig voce clica e assume — que e como deve
		// ser pra uma acao que sai do editor.
		bool m_PoseMode = true;

		// Roda o evento Backward Solve uma vez.
		void RunBackwardSolve();

		// Devolver um controle pro osso de origem.
		int m_PendingResetToBone = -1;

		// Espelhar. Mesma razao das outras pendencias: Mirror ACRESCENTA
		// elementos, e a arvore nao pode crescer no meio do proprio loop.
		int m_PendingMirror = -1;

		// Eixo normal ao plano de simetria: 0=X (o do corpo humano), 1=Y, 2=Z.
		// Guardado entre chamadas pra quem espelha um rig inteiro nao ter que
		// reescolher a cada elemento.
		int m_MirrorAxis = 0;


		// ── Preview ──────────────────────────────────────────────────────────
		std::unique_ptr<ViewportRenderer> m_PreviewRenderer;
		std::shared_ptr<Framebuffer>      m_PreviewFramebuffer;
		std::unique_ptr<Scene>            m_PreviewScene;
		std::unique_ptr<SceneEnvironment> m_PreviewEnvironment;

		entt::entity m_PreviewEntity{};
		ImVec2 m_PreviewSize{ 0.0f, 0.0f };

		bool  m_PreviewHovered = false;
		bool  m_PreviewInit = false;
		bool  m_ShowControlGizmos = true;
		float m_PreviewScale = 1.0f;
		float m_PreviewOffsetY = 0.0f;
		bool  m_ShowSkeleton = true;

		// Operacao do gizmo. Guardado como INT pra nao arrastar o ImGuizmo.h
		// pra dentro deste header: 7 = TRANSLATE, 120 = ROTATE, 896 = SCALE
		// (os valores do enum ImGuizmo::OPERATION). A conversao acontece so no
		// rig_preview.cpp, que ja inclui o header.
		int   m_GizmoOp = 7;

		// Desenhamos um manipulador NESTE frame?
		//
		// O ImGuizmo tem UM contexto global, compartilhado com o viewport
		// principal. Sem esta flag, perguntar IsOver() quando nao ha gizmo
		// nosso na tela responde sobre o gizmo DE OUTRA JANELA.
		bool  m_ManipulatorDrawn = false;

		// ── Cache de Euler dos campos de rotacao ─────────────────────────────
		//
		// Quaternion -> Euler NAO tem resposta unica: a mesma rotacao tem
		// varias representacoes, e glm::eulerAngles devolve a canonica, que
		// raramente e a que voce digitou. Reconverter todo frame fazia mexer
		// em UM campo saltar os outros dois.
		//
		// Guardamos o valor enquanto voce edita e so re-derivamos do
		// quaternion quando a selecao muda ou nenhum campo esta ativo.
		// Slot 0 = Initial transform, slot 1 = Shape offset.
		glm::vec3 m_EulerCache[2]{};
		int       m_EulerOwner[2] = { -1, -1 };

		// O TICK da animacao. Sem ele as matrizes de skinning nunca sao
		// calculadas e o personagem simplesmente NAO APARECE — era esse o
		// motivo do preview vazio, nao enquadramento de camera.
		std::unique_ptr<AnimationWorld> m_PreviewAnim;

		// Pose de trabalho do solve. E MEMBRO, e nao local, pra nao realocar o
		// vetor de ossos a cada frame.
		Pose m_RigPose;

		// Qual esqueleto ja esta na cena de preview — evita re-sincronizar
		// todo frame.
		std::shared_ptr<SkeletalMeshAsset> m_PreviewSynced;

		// ── HIERARQUIA DO PREVIEW ────────────────────────────────────────────
		//
		// O preview roda numa COPIA, nao na hierarquia do asset.
		//
		// O ControlRigAsset e cacheado por identidade: o mesmo arquivo devolve o
		// mesmo objeto. Sem esta copia, o editor e a cena viva dividem a MESMA
		// RigHierarchy — e arrastar um controle aqui movia o personagem que esta
		// rodando la, na hora. "Deixei o braco pra cima no editor" virava um
		// braco pra cima no jogo.
		//
		// E o mesmo desenho que o AnimNode_ControlRig ja usa: cada consumidor
		// roda a propria copia, e o asset e so o molde. O editor passa a ser
		// mais um consumidor.
		//
		// O que continua indo pro asset e a DEFINICAO — o gizmo em modo Setup, o
		// painel de detalhes, a arvore de elementos. Isso e o rig em si e tem
		// que persistir. O que fica so aqui e a POSE.
		RigHierarchy m_PreviewHierarchy;

		// Versao do asset de que esta copia nasceu. Diferente = alguem editou a
		// definicao (criou controle, espelhou, mudou forma) e o clone esta
		// velho. Mesma tecnica do m_ClonedVersion do AnimNode_ControlRig.
		uint32_t m_PreviewVersion = 0;
		bool     m_PreviewCloned = false;

		// Garante que a copia existe e esta na versao do asset. Chamada no
		// comeco de todo caminho que toca o preview.
		RigHierarchy& PreviewHierarchy();

		// Forca reclone no proximo acesso. Chamada pelo MarkEdited: qualquer
		// edicao de definicao invalida a copia.
		void InvalidatePreviewHierarchy() { m_PreviewCloned = false; }

		ed::EditorContext* m_EdCtx = nullptr;

		// ── FUNCOES ──────────────────────────────────────────────────────────
		//
		// INDICE, nao ponteiro. Adicionar ou remover qualquer outra funcao pode
		// realocar o vector<RigFunction>, e um ponteiro guardado entre frames
		// passaria a apontar pra lixo — em silencio. Mesma decisao, e pelo
		// mesmo motivo, do m_EditingFunctionIndex do Script Editor.
		//
		// -1 = editando o grafo principal.
		int m_EditingFunction = -1;

		// Contexto de node-editor PROPRIO pra funcoes.
		//
		// Os Ids de no sao por grafo: o no 3 de uma funcao e o no 3 do grafo
		// principal sao coisas diferentes, e um contexto so misturaria as
		// posicoes dos dois. Barato porque as posicoes de verdade moram em
		// EditorX/EditorY no proprio no; o contexto e so cache de interacao.
		ed::EditorContext* m_FuncEdCtx = nullptr;

		// Grafo que esta na tela: o principal, ou o da funcao aberta.
		RigGraph& CurrentGraph();

		// Contexto do grafo atual, criando o de funcao se preciso.
		ed::EditorContext* CurrentEdCtx();

		void SwitchToMainGraph();
		void SwitchToFunction(int index);

		// Reconstroi os pinos do Entry e do Return da funcao, e de TODO no Call
		// que a chame — em qualquer grafo do asset, o principal e o das outras
		// funcoes. Chamar sempre que Inputs/Outputs mudar: sem isto, mexer na
		// assinatura deixaria as chamadas com os pinos velhos.
		void RebuildFunctionCallSites(int index);

		// Painel de funcoes, no dock da esquerda.
		void DrawMembersPanel();

		char m_NewFuncName[64] = "NewFunction";

		// ── PALETA ───────────────────────────────────────────────────────────
		//
		// O menu passou de trinta nos. Sem busca, achar "Vector Length" no meio
		// virou rolagem — e a lista so cresce.
		char m_PaletteFilter[64] = "";

		// Estado aberto/fechado por categoria, LEMBRADO entre aberturas: quem
		// trabalha em Math nao quer reabrir Math toda vez. Sete = o numero de
		// categorias; se acrescentar uma, aumente aqui.
		bool m_PaletteOpen[8] = { true, true, true, true, true, true, true, true };

		// Acoes diferidas: mexem no vector de funcoes ou trocam o grafo
		// desenhado, e nenhuma das duas pode acontecer no meio do loop que
		// desenha a lista ou dentro do Begin/End do node-editor.
		int m_PendingOpenFunction = -1;

		// Funcao solta no grafo, aguardando virar um no Call. String e nao
		// indice: entre o drop e a criacao o vector de funcoes pode mudar, e um
		// indice velho criaria a chamada errada.
		std::string m_DropFunction;
		int m_PendingRemoveFunction = -1;
		bool m_NeedsContextReset = false;

		// Pedido explicito de "Restaurar layout". Nao da pra reconstruir o
		// dockspace no meio do frame em que o botao foi clicado — os paineis
		// desse frame ja foram posicionados —, entao o pedido espera o
		// proximo.
		bool m_ResetLayout = false;

		// Posicoes dos nos vem do .axerig e sao aplicadas uma vez por abertura.
		bool m_NodePositionsLoaded = false;

		// Aplica o tamanho salvo dos comentarios uma vez por abertura (dai em
		// diante quem manda e o bounds interno do node-editor).
		// Titulo de comment sendo editado in-place (-1 = nenhum). Separado do
		// rename da hierarquia: os dois podem coexistir na tela.
		int  m_RenamingComment = -1;
		bool m_RenameCommentFocus = false;
		char m_CommentBuf[96] = {};

		// ── Arrastar da hierarquia pro grafo ─────────────────────────────────
		//
		// O que foi solto e ONDE. O no nao e criado na hora do drop: primeiro
		// abre um menu perguntando Get ou Set — que e a duvida real, e sao
		// dois nos bem diferentes.
		std::string    m_DropName;
		RigElementType m_DropType = RigElementType::Bone;
		ImVec2         m_DropPos{ 0.0f, 0.0f };

		// Onde o menu de fundo foi ABERTO — e onde o no novo nasce. Usar a
		// posicao do mouse na hora do clique nao serve: a essa altura ele ja
		// se moveu pra cima do proprio menu.
		ImVec2 m_MenuCanvasPos{ 0.0f, 0.0f };

		// Seletor de elemento de um pino Item. Desenhado DEPOIS do canvas —
		// ver o comentario do botao em rig_graph_canvas.cpp.
		int  m_ItemPickerNode = -1;
		int  m_ItemPickerPin = -1;
		bool m_ItemPickerOpen = false;
		char m_ItemPickerFilter[64] = {};
	};

} // namespace axe