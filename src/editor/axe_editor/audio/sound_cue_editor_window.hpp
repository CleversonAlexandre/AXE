#pragma once
#include "axe/core/types.hpp"
#include "axe/core/command_history.hpp"
#include "axe/audio/sound_cue.hpp"
#include "axe/audio/audio_device.hpp"   // VoiceHandle
#include "editor/axe_editor/editor_context.hpp"

#include <imgui.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ax { namespace NodeEditor { struct EditorContext; } }

namespace axe
{
	// ── SoundCueEditorWindow ─────────────────────────────────────────────────
	//
	// Editor de nos do .axecue, no mesmo molde do Control Rig: janela-mae com
	// dockspace, paineis hospedados nele, toolbar de icones e undo/redo por
	// snapshot.
	//
	// A semelhanca e proposital e vale mais que economia de codigo: quem
	// aprendeu a mexer no rig ja sabe mexer aqui. Um editor de nos com regras
	// proprias de arrasto, atalho e layout seria um segundo idioma dentro do
	// mesmo programa.
	//
	// Nao ha grafo intermediario — a janela desenha direto do SoundCueAsset.
	// O grafo de um cue tem uma dezena de nos no pior caso, e duplicar o
	// modelo so criaria um segundo estado pra manter sincronizado.
	class SoundCueEditorWindow
	{
	public:
		SoundCueEditorWindow() = default;
		~SoundCueEditorWindow();

		void SetContext(EditorContext* context) { m_Context = context; }

		void OpenAsset(std::shared_ptr<SoundCueAsset> cue, const std::filesystem::path& path);
		void Close() { m_Open = false; }

		bool IsOpen()    const { return m_Open; }
		bool IsFocused() const { return m_Focused; }

		void Draw();
		void Save();

	private:
		void DrawDockLayout(ImGuiID dockspaceId);
		void DrawToolbar();
		void DrawGraphPanel();
		void DrawDetailsPanel();

		void DrawNode(SoundCueNode& node);
		void HandleCreate();
		void HandleDelete();
		void HandleContextMenus();
		void HandleShortcuts();

		void SpawnNode(SoundCueNodeType type, const ImVec2& canvasPos);

		// Cria um Wave Player por asset de audio soltos no grafo, empilhados
		// a partir do ponto do drop.
		void SpawnWavePlayers(const std::vector<std::string>& uuids, const ImVec2& canvasPos);
		void HandleAssetDrop(const ImVec2& canvasMin, const ImVec2& canvasSize);

		// ── Copiar / Colar ───────────────────────────────────────────────
		//
		// Mesma mecanica do Control Rig: o clipboard guarda COPIAS dos nos
		// (nao referencias) e os links INTERNOS ao conjunto, com um deslocamento
		// relativo a uma ancora. Colar duas vezes e o caso de uso — uma
		// variacao, depois outra — entao o clipboard nunca e esvaziado pela
		// colagem.
		struct Clipboard
		{
			struct Entry { SoundCueNode Node; float DX = 0.0f, DY = 0.0f; };

			std::vector<Entry> Nodes;

			// Indices DENTRO de Nodes, nao ids: os ids mudam na colagem.
			struct Link { int From = -1, To = -1; };
			std::vector<Link> Links;

			float AnchorX = 0.0f, AnchorY = 0.0f;
		};

		void CopySelectedNodes(bool cut);
		void PasteNodes(const ImVec2& canvasPos);
		void Preview();

		bool WouldCreateCycle(int fromNodeId, int toNodeId) const;
		int  InputCountOf(const SoundCueNode& node) const;
		int  ChildCountOf(int nodeId) const;
		bool InputIsLinked(int nodeId, int slot) const;
		bool OutputIsLinked(int nodeId) const;
		std::vector<int> ChildrenOf(int nodeId) const;

		// ── Undo / Redo ──────────────────────────────────────────────────
		//
		// Por SNAPSHOT, e nao por comando fino — mesma escolha do Control Rig
		// e pelo mesmo motivo: apagar um no leva os links junto, e mexer nos
		// links reindexa os pesos do Random. Escrever o inverso exato de cada
		// operacao seria muito codigo, e um deles estaria errado. O grafo
		// inteiro cabe em algumas dezenas de structs.
		struct CueSnapshot
		{
			std::vector<SoundCueNode> Nodes;
			std::vector<SoundCueLink> Links;
			int                       NextId = 1;
		};

		CueSnapshot CaptureState() const;
		void        RestoreState(const CueSnapshot& snap);
		void        CommitPendingUndo();
		void        DoUndo();
		void        DoRedo();

		// Marca sujo e ANOTA a acao pro undo.
		//
		// O comando nao e fechado aqui: arrastar um no chama isto todo frame,
		// e cada frame viraria um passo de undo — trinta Ctrl+Z pra desfazer
		// um movimento so. O comando fecha quando o gesto termina.
		void MarkEdited(const char* action)
		{
			m_Dirty = true;

			if (action && !m_PendingUndo)
			{
				m_PendingUndo = true;
				m_PendingUndoName = action;
			}
		}

		// ── Codificacao de pin ───────────────────────────────────────────
		//
		// O SoundCueAsset guarda nos e links entre NOS — pin e conceito de UI
		// e o runtime nao tem o que fazer com um. Mas o node-editor exige um
		// id por pino, entao derivamos deterministicamente do id do no.
		//
		// Deterministico importa: id gerado por frame faria o editor perder
		// selecao e posicao a cada quadro. Mesmo esquema (e mesmo motivo) do
		// kPinStride do rig_graph_canvas.
		//
		//   slot 0    = saida
		//   slot 1..N = entradas
		static constexpr int kSlots = 64;
		static int  MakePinId(int nodeId, int slot) { return nodeId * kSlots + slot; }
		static int  PinNode(int pinId) { return pinId / kSlots; }
		static int  PinSlot(int pinId) { return pinId % kSlots; }
		static bool PinIsOutput(int pinId) { return PinSlot(pinId) == 0; }

		EditorContext* m_Context = nullptr;
		ax::NodeEditor::EditorContext* m_Ed = nullptr;

		std::shared_ptr<SoundCueAsset> m_Cue;
		std::filesystem::path          m_Path;

		bool m_Open = false;
		bool m_Focused = false;
		bool m_Dirty = false;
		bool m_LayoutApplied = false;
		bool m_ResetLayout = false;

		int m_SelectedNode = 0;
		int m_FrameCount = 0;

		ImVec2 m_MenuCanvasPos{ 0.0f, 0.0f };

		Clipboard m_Clipboard;

		// Drop resolvido FORA do alvo de drag-and-drop: criar nos mexe no
		// grafo, e fazer isso no meio do BeginDragDropTarget mistura duas
		// coisas que o ImGui prefere separadas.
		std::vector<std::string> m_PendingDrop;
		ImVec2                   m_DropPos{ 0.0f, 0.0f };

		CommandHistory               m_History;
		std::shared_ptr<CueSnapshot> m_Baseline;
		bool                         m_PendingUndo = false;
		std::string                  m_PendingUndoName;

		// Ultimo resultado de preview — a toolbar mostra o que saiu, que e o
		// que torna a aleatoriedade visivel enquanto se edita.
		// Audicao de UM Wave Player (o botao do Details), separada do preview
		// do cue inteiro: sao perguntas diferentes — "como e este arquivo?" e
		// "como o cue soa?".
		VoiceHandle m_PreviewVoice = InvalidVoice;
		int         m_PreviewNode = 0;

		std::string m_LastPreviewWave;
		float       m_LastPreviewVolume = 1.0f;
		float       m_LastPreviewPitch = 1.0f;
	};

} // namespace axe