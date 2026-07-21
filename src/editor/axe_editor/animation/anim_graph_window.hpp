#pragma once

#include "axe/animation/anim_graph_asset.hpp"
#include "axe/animation/anim_nodes.hpp"
#include "axe/animation/animation_world.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"

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

		void DrawNodeDetails(AnimNode& node);
		void DrawTransitionDetails(AnimNode_StateMachine& sm, int index);
		void DrawStateDetails(AnimNode_StateMachine& sm, int index);

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
		static int StateNode(int i) { return 0x01000 + i; }
		static int StateIn(int i) { return 0x11000 + i; }
		static int StateOut(int i) { return 0x21000 + i; }
		static int TransLink(int i) { return 0x41000 + i; }

		static constexpr int kAnyStateNode = 0x00900;
		static constexpr int kAnyStateOut = 0x00901;

		// Entry (estilo Unreal): um no fixo de onde UMA seta sai e aponta o
		// estado inicial. Arrastar Entry -> estado troca o EntryState. O link
		// e sempre desenhado e nao pode ser apagado — sem entrada, a maquina
		// nao sabe onde comecar.
		static constexpr int kEntryNode = 0x00800;
		static constexpr int kEntryOut = 0x00801;
		static constexpr int kEntryLink = 0x00902;

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

		void MarkEdited();

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