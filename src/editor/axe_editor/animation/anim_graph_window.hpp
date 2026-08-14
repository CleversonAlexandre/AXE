#pragma once

#include "axe/animation/anim_graph_asset.hpp"
#include "axe/animation/anim_nodes.hpp"
#include "axe/animation/animation_world.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/core/command_history.hpp"   // CommandHistory — undo/redo (AG3b)

#include <imgui.h>
#include <imgui_node_editor.h>

#include "axe/scene/scene.hpp"
#include "axe/animation/animation_world.hpp"

#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>

namespace ed = ax::NodeEditor;

namespace axe
{
	class ViewportRenderer;
	class Framebuffer;
	class SceneEnvironment;

	// Editor do AnimGraph (asset `.axeanim`) — grafo de poses, à la Unreal.
	//
	// ── TRÊS NÍVEIS, DOIS TIPOS DE CANVAS ────────────────────────────────
	//
	//   AnimGraph            → grafo de POSES   (nós e links de pose/dado)
	//     Locomotion         → MÁQUINA DE ESTADOS (estados e transições)
	//       Idle             → grafo de POSES   (o sub-grafo do estado)
	//
	// Repare que o nível 1 e o nível 3 são o MESMO canvas — grafo de poses. A
	// recursão do runtime aparece aqui também: não existe "canvas do topo" e
	// "canvas do estado". Existe canvas de grafo, e canvas de máquina.
	//
	// Um estado, três níveis abaixo, pode conter outra máquina de estados. E
	// funciona, sem nenhum código especial.
	class AnimGraphWindow
	{
	public:
		void Initialize();
		void Shutdown();

		void Draw();

		void OpenForAsset(const std::shared_ptr<AnimGraphAsset>& asset,
			const std::shared_ptr<SkeletalMeshAsset>& skeleton);

		void Close() { m_Open = false; }
		bool IsOpen() const { return m_Open; }

	public:
		// Renderiza o preview 3D. Chamado pelo EditorLayer FORA do frame de UI —
		// como o Material, o Particle e o Script fazem.
		//
		// Tem que ser fora: isto faz GL de verdade (bind de framebuffer, draw
		// calls, o compute do skinning). Fazer isso no meio do ImGui corromperia
		// o estado do renderer da UI.
		void RenderPreview();

	private:
		// ── Preview ──────────────────────────────────────────────────────────
		void InitPreviewScene();
		void DrawPreviewWindow();
		void SyncPreviewCharacter();
		void HandlePreviewInput();

		// Monta o layout padrao do dockspace interno — uma vez so. Depois disso
		// quem manda e o usuario, e o ImGui persiste o arranjo no imgui.ini.
		void DrawDockLayout(ImGuiID dockspaceId);

		// Um degrau da navegação.
		//
		// Ou é um grafo de poses (Graph != null), ou é a lista de estados de uma
		// máquina (Sm != null). Nunca os dois.
		struct NavEntry
		{
			std::string             Label;
			AnimPoseGraph* Graph = nullptr;
			AnimNode_StateMachine* Sm = nullptr;

			// SM_UESTYLE_V1: >= 0 significa que este nivel e a REGRA da
			// transicao Sm->Transitions[TransIndex] — o "Crouch to Idle
			// (rule)" da Unreal. Graph continua nulo nesse caso.
			int TransIndex = -1;

			// AG3e — chaves ESTAVEIS do nivel, para reconstruir a navegacao
			// depois de um undo.
			//
			// Graph e Sm sao ponteiros para dentro do grafo raiz. O restore
			// substitui esse grafo inteiro, e os dois viram lixo. Estes dois
			// campos sobrevivem porque descrevem o CAMINHO, nao o endereco:
			//
			//   SmNodeId   — Id do no de State Machine no grafo pai
			//   StateIndex — indice do estado dentro da SM pai
			//
			// Os Ids atravessam o snapshot intactos (AnimNode::Clone copia o
			// Id, e o restore repoe o grafo com a mesma numeracao), entao a
			// busca por Id encontra o mesmo no de antes.
			int SmNodeId = -1;
			int StateIndex = -1;
		};

		void DrawBreadcrumb();
		void DrawToolbar();
		void DrawParametersPanel();
		void DrawDetailsPanel();

		// ── Desenho bonito dos nos ───────────────────────────────────────────
		//
		// Cabecalho colorido por CATEGORIA, corpo escuro, cantos arredondados,
		// pinos como circulos da cor do tipo.
		//
		// Nao e so estetica: a cor do cabecalho diz o que o no FAZ (fonte de
		// pose / blend / variavel / saida) antes de voce ler o nome, e a cor do
		// pino diz o que ele aceita antes de voce tentar arrastar.
		struct NodeStyle
		{
			ImVec4 Header;
			const char* Category;
		};

		static NodeStyle StyleFor(const AnimNode& node, bool isOutput);

		void DrawPoseGraphCanvas(AnimPoseGraph& graph);
		void DrawStateMachineCanvas(AnimNode_StateMachine& sm);
		void DrawTransitionRuleCanvas(AnimNode_StateMachine& sm, int transIndex);

		void DrawNodeDetails(AnimNode& node);
		void DrawTransitionDetails(AnimNode_StateMachine& sm, int index);
		void DrawStateDetails(AnimNode_StateMachine& sm, int index);

		// Uma linha de condicao (parametro / operador / valor / excluir).
		// Usada pelo Detalhes da transicao E pelo Detalhes do nivel de regra.
		void DrawConditionRow(AnimTransition& tr, std::size_t c, int& removeCond);

		void DrawNodePalette(AnimPoseGraph& graph);

		void NavigateTo(const NavEntry& entry);
		void NavigateUpTo(int depth);

		void Save();

		// ── IDs ──────────────────────────────────────────────────────────────
		//
		// O imgui-node-editor põe nó, pino e link no MESMO espaço de inteiros.
		// Colidir dois deles dá bug de seleção silencioso — o pior tipo, porque
		// o grafo parece certo e reage errado.
		//
		// Cada família ganha uma faixa alta e própria.
		static int PoseInPin(int node, int pin) { return 0x10000 + node * 16 + pin; }
		static int DataInPin(int node, int pin) { return 0x20000 + node * 16 + pin; }
		static int OutPin(int node) { return 0x30000 + node; }
		static int LinkId(int i) { return 0x40000 + i; }

		// Máquina de estados (outro canvas, outra faixa).
		//
		// SM_UESTYLE_V1: nao existem mais pinos Entra/Sai nem ed::Link de
		// transicao. A transicao virou um NO-ICONE (o circulo no meio da
		// seta), e a criacao sai de PINOS DE BORDA — 4 faixas finas por
		// estado, com ed::PinRect explicito. O pino de "drop" cobre o corpo
		// inteiro do estado e so existe enquanto um arrasto esta vivo.
		static int StateNode(int i) { return 0x01000 + i; }
		static int TransIconNode(int t) { return 0x51000 + t; }
		static int StateBorderPin(int i, int side) { return 0x71000 + i * 4 + side; }
		static int StateDropPin(int i) { return 0x81000 + i; }

		static constexpr int kAnyStateNode = 0x00900;
		static constexpr int kAnyBorderPin0 = 0x00910;   // +0..3

		// Entry (estilo Unreal): um no fixo de onde UMA seta sai e aponta o
		// estado inicial. Arrastar da borda do Entry ate um estado troca o
		// EntryState. A seta e sempre desenhada e nao pode ser apagada — sem
		// entrada, a maquina nao sabe onde comecar.
		static constexpr int kEntryNode = 0x00800;
		static constexpr int kEntryBorderPin0 = 0x00810; // +0..3

		// Nivel de REGRA (o grafo da condicao de UMA transicao).
		static int CondNode(int i) { return 0x61000 + i; }
		static constexpr int kRuleResultNode = 0x00A00;
		static constexpr int kRuleExitNode = 0x00A10;

		// Decodifica um pino de volta em (nó, índice, é-dado?).
		static bool DecodePin(int pinId, int& outNode, int& outPin, bool& outIsData, bool& outIsOutput);

		ed::EditorContext* m_EdCtx = nullptr;
		bool m_Open = false;

		std::shared_ptr<AnimGraphAsset>    m_Asset;
		std::shared_ptr<SkeletalMeshAsset> m_Skeleton;

		std::vector<NavEntry> m_Nav;

		// Recriar o contexto do node-editor a cada navegação é DE PROPÓSITO.
		//
		// Os Ids dos nós são por grafo: o nó 1 da raiz e o nó 1 de um sub-grafo
		// têm o MESMO id. Num contexto compartilhado, eles colidiriam — e a
		// posição de um sobrescreveria a do outro, entre outros horrores.
		//
		// Recriar zera tudo. Navegação é rara; o custo é irrelevante.
		bool m_NeedsContextReset = false;
		bool m_PositionsLoaded = false;

		// Painel de parametros (estilo UE): selecao + renomeio inline.
		int  m_SelectedParam = -1;
		int  m_RenamingParam = -1;
		char m_RenameBuf[64] = {};

		int m_SelectedNode = -1;         // no canvas de grafo
		int m_SelectedState = -1;        // no canvas de máquina
		int m_SelectedTransition = -1;
		int m_SelectedCondition = -1;    // no canvas de REGRA

		// O frame ANTERIOR tinha um arrasto de transicao vivo? E o que liga
		// os pinos de "drop" (corpo inteiro) dos estados — eles nao podem
		// existir fora do arrasto, senao roubam o clique de mover o no.
		bool m_WasCreatingLink = false;

		// No que abriu o menu de contexto da maquina/regra.
		int m_CtxNodeId = 0;

		// Any State e OPCIONAL: escondido por padrao (na Unreal ele nem
		// existe). Aparece por escolha no menu de contexto — ou a forca,
		// quando ja existem transicoes partindo dele (senao elas ficariam
		// sem origem visivel).
		bool m_ShowAnyState = false;

		bool m_Dirty = false;

		// ── Sincronia editor -> preview ──────────────────────────────────────
		//
		// O preview roda um CLONE do grafo (GraphInstance::SetAsset clona).
		// Sem isto, toda edicao — escolher um clipe, criar um estado — ficava
		// so no asset, e o clone continuava tocando o grafo VELHO: personagem
		// em T-pose pra sempre. MarkEdited() carimba a edicao; o
		// SyncPreviewCharacter re-clona quando o carimbo muda (com um respiro
		// de ~0.3s pra nao resetar a animacao a cada tick de um DragFloat).
		int    m_EditSerial = 0;
		int    m_PreviewSyncedSerial = 0;
		double m_LastEditTime = 0.0;

		// AG3b — MarkEdited passou a aceitar o NOME da acao, para o undo.
		//
		// O parametro e opcional: as dezenas de call-sites que ja existiam
		// continuam validos e seguem marcando sujo sem abrir passo de undo.
		// Isso e proposital, e nao preguica de migrar — a maioria deles e o
		// loop que grava posicao de no todo frame, e cada frame virar um passo
		// de Ctrl+Z seria pior que nao ter undo.
		void MarkEdited(const char* action = nullptr);

		// ── Undo / Redo ──────────────────────────────────────────────────────
		//
		// Por SNAPSHOT, como no Control Rig, e pelo mesmo motivo: o grafo muda
		// de formas muito diferentes (apagar um no leva os fios junto, inserir
		// um reroute religa duas pontas), e escrever o inverso exato de cada
		// operacao seria muito codigo com um deles errado. O asset inteiro sao
		// alguns milhares de bytes; copiar e barato e nao tem como divergir.
		struct AnimSnapshot
		{
			std::vector<AnimParamDecl> Parameters;
			AnimPoseGraph              Root;   // copia PROFUNDA (ver AnimPoseGraph)
		};

		AnimSnapshot CaptureState() const;
		void         RestoreState(const AnimSnapshot& snap);

		// Fecha o comando pendente quando o gesto termina. Chamado por frame.
		void CommitPendingUndo();

		void DoUndo();
		void DoRedo();

		// AG3c — foco agregado dos paineis do editor, calculado DURANTE o
		// desenho.
		//
		// Os atalhos rodam no fim do frame, depois de todos os ImGui::End().
		// Naquele ponto a janela corrente nao e mais nenhuma janela do
		// AnimGraph, e IsWindowFocused responde sempre false — era exatamente
		// isso que impedia o Ctrl+Z de funcionar. Cada painel marca a flag
		// enquanto esta aberto; os atalhos leem o resultado.
		bool m_ShortcutFocus = false;

		// ── Clipboard ────────────────────────────────────────────────────────
		//
		// Guarda os nos COPIADOS (clones profundos) e os links INTERNOS ao
		// recorte, com os ids antigos. O Paste remapeia.
		//
		// Nao usa o clipboard do sistema de proposito: colar um grafo de
		// animacao dentro de um editor de texto nao serve a ninguem, e
		// serializar/desserializar so para atravessar a area de transferencia
		// seria trabalho a mais com mais chance de erro.
		struct GraphClipboard
		{
			std::vector<std::unique_ptr<AnimNode>> Nodes;
			std::vector<AnimLink>                  Links;   // ids ANTIGOS
		};

		GraphClipboard m_Clipboard;

		// Ids ORIGINAIS dos nos copiados, na mesma ordem de m_Clipboard.Nodes.
		// O Clone nao carrega o Id (o AddNode atribui um novo), entao o Paste
		// precisa desta lista para remapear os links internos.
		std::vector<int> m_ClipboardIds;

		// Posicao do cursor em coordenada de CANVAS, capturada durante o
		// desenho.
		//
		// O Paste roda no fim do frame, fora do Begin/End do node-editor —
		// e la nao ha contexto para ScreenToCanvas converter. Guardar o valor
		// enquanto o contexto existe e a forma de o Ctrl+V colar sob o cursor
		// em vez de num ponto arbitrario.
		ImVec2 m_LastCanvasMousePos{ 0, 0 };

		// ── Paleta de nos (AG5) ──────────────────────────────────────────────
		//
		// Mesmo formato do Control Rig e do Script Editor: busca no topo,
		// categorias coloridas colapsaveis, cor do item ecoando a do no.
		//
		// m_PaletteOpen guarda quais categorias ficaram abertas ENTRE
		// aberturas do menu. Sem isso, quem trabalha o dia todo com Blends
		// reabriria a categoria a cada clique direito.
		char m_PaletteFilter[64] = {};
		bool m_PaletteOpen[8] = {};

		void CopySelection(bool cut);
		// `at` = onde colar, em coordenada de canvas. Nulo usa a posicao do
		// cursor capturada no ultimo frame — o caminho do Ctrl+V. O item
		// "Colar" da paleta passa o ponto do clique direito, que e o que o
		// usuario apontou.
		void PasteClipboard(const ImVec2* at = nullptr);

		CommandHistory m_History;

		// Estado no fim do ultimo comando — serve de "antes" do proximo, entao
		// guardamos UM snapshot por comando em vez de dois.
		std::shared_ptr<AnimSnapshot> m_Baseline;

		bool        m_PendingUndo = false;
		std::string m_PendingUndoName;

		// ── Preview 3D ───────────────────────────────────────────────────────
		//
		// Cena PROPRIA, com o personagem e uma luz. Roda o AnimationWorld nela —
		// entao o grafo que voce esta editando anima ali, ao vivo, com os
		// parametros que voce mexer no painel.
		//
		// E a diferenca entre "montar um grafo no escuro" e VER o resultado.
		std::unique_ptr<ViewportRenderer>  m_PreviewRenderer;
		std::shared_ptr<Framebuffer>       m_PreviewFramebuffer;
		std::unique_ptr<Scene>             m_PreviewScene;
		std::unique_ptr<SceneEnvironment>  m_PreviewEnvironment;
		std::unique_ptr<AnimationWorld>    m_PreviewAnim;

		entt::entity m_PreviewEntity = entt::null;
		ImVec2 m_PreviewSize{ 0, 0 };
		bool   m_PreviewHovered = false;
		bool   m_PreviewInit = false;

		// O asset que a cena de preview esta usando. Se mudar, resincroniza.
		std::shared_ptr<AnimGraphAsset> m_PreviewAssetInScene;

		bool m_PreviewPlaying = true;

		// ── Docking ──────────────────────────────────────────────────────────
		//
		// Layout padrao construido UMA vez. Depois o ImGui persiste o arranjo do
		// usuario no imgui.ini — arrastar um painel uma vez vale pra sempre.
		bool m_LayoutBuilt = false;

		// Posicao no canvas de quando o menu de contexto abriu — pra o no/estado
		// novo nascer ali, e nao onde o mouse foi parar depois.
		ImVec2 m_MenuOpenCanvasPos{ 0, 0 };
	};

} // namespace axe