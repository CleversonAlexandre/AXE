#include "axe/core/command_history.hpp"
#pragma once
#include "axe/core/types.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/script/script_asset.hpp"
#include "axe/script/script_component.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include "editor/axe_editor/inspector_window.hpp"
#include <imgui_node_editor.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include <string>
#include <vector>
#include <utility> // std::pair no retorno de CollectAnimGraphParams
#include <sstream>
#include <entt/entt.hpp>
#include "axe/animation/animation_world.hpp"

#include <filesystem>
#include <functional>

namespace ed = ax::NodeEditor;

namespace axe
{
    // Payload de arrasto de um COMPONENTE do Scene Graph.
    //
    // Um unico gesto — arrastar o nome do componente — serve para os DOIS
    // destinos, porque o ImGui so aceita um DragDropSource por item:
    //   • soltar no canvas do grafo  -> cria o node (usa Node)
    //   • soltar em outro componente -> reparenta   (usa Index)
    // Quem recebe decide qual campo usar. Node vazio = componente sem node
    // correspondente (Mesh, Material...), que ainda assim pode ser movido
    // na hierarquia.
    struct ComponentDragPayload
    {
        int  Index = -1;
        char Node[64] = {};
    };

    class ScriptGraphWindow
    {
    public:
        ScriptGraphWindow();
        ~ScriptGraphWindow();

        // ── Constantes compartilhadas ─────────────────────────────────────────────
        const float ICON_SZ = 20.0f;
        const float PIN_H = 22.0f;

        void Initialize();
        void Draw();
        void SetActiveScene(Scene* scene) { m_ActiveScene = scene; }

        // Undo/Redo — snapshot based
        void PushUndo(const std::string& actionName);  // call BEFORE making changes
        void CommitUndo(const std::string& actionName);   // call AFTER making changes
        void Undo();
        void Redo();
        bool CanUndo() const { return m_History.CanUndo(); }
        bool CanRedo() const { return m_History.CanRedo(); }
        void RenderPreview();

        // Avisa o editor que o script foi SALVO/COMPILADO, pra propagar os
        // componentes pras instancias que ja estao na cena.
        using ScriptSavedCallback = std::function<void(const std::filesystem::path&)>;
        void SetScriptSavedCallback(ScriptSavedCallback cb) { m_ScriptSavedCallback = cb; }

        // O arquivo deste script foi renomeado no Asset Browser: adota o
        // caminho e o nome novos (ver AssetBrowser::OnAssetRenamed).
        void HandleAssetRenamed(const std::filesystem::path& oldPath,
            const std::filesystem::path& newPath,
            const std::string& newName);
        void Shutdown();

        void OpenForEntity(entt::entity entity, ScriptComponent* comp,
            entt::registry* registry);
        void SetInspectorWindow(InspectorWindow* insp) { m_InspectorWindow = insp; }
        void OpenForAsset(std::shared_ptr<ScriptAsset> asset);
        void Close();

        // ── Functions (estilo Function da Unreal) ─────────────────────────────
        // Troca qual grafo está sendo editado no canvas — main graph do
        // ScriptAsset ou o grafo isolado de uma ScriptFunction específica.
        // Seguro reusar o mesmo m_EdCtx entre eles (ver comentário em
        // script_node_graph.cpp linha da posição: a posição de cada node é
        // reaplicada todo frame a partir de node->Position, então IDs que
        // por acaso colidem entre grafos diferentes não causam problema).
        void SwitchToMainGraph();
        void SwitchToFunctionGraph(ScriptFunction* func);
        // Reconstrói os pins do Function Entry/Return Node (no grafo da
        // própria função) e de TODO node "Call <func.Name>" em qualquer
        // grafo do asset (principal + todas as outras funções, recursão
        // inclusive) — chamar sempre que func.Inputs/Outputs mudar.
        void RebuildFunctionCallSites(ScriptFunction& func);
        // Cria um Comment — se houver nodes selecionados no canvas, a caixa
        // nasce dimensionada pra envolvê-los; senão nasce em branco na
        // posição indicada. Usada tanto pelo menu "+ Add Comment" quanto
        // pelo atalho de teclado 'C' (mesmo comportamento da Unreal nos
        // dois casos — um só lugar implementando isso, sem duplicar).
        void CreateCommentNode(ImVec2 fallbackPos);
        bool IsOpen() const { return m_IsOpen; }

    private:
        void DrawPreviewWindow();
        void DrawGraphWindow();
        void DrawNodeGraph();        // lógica do node editor — só chamado quando janela visível
        void DrawSceneGraphWindow();
        void DrawDetailsWindow();
        void DrawConsoleWindow();

        void DrawNode(ScriptNode* node);
        void HandlePreviewInput();
        void DrawPreviewGizmo();       // gizmo sobreposto na preview
        void DrawScriptDetails();      // conteúdo do painel Details quando objeto selecionado
        // Conteúdo de detalhes de UMA variável (Type, Name, Default Value,
        // Tamanho para arrays, Exposed, Description, Categoria). Extraído de
        // DrawScriptDetails para ser reutilizável tanto por um node Get/Set
        // Variable selecionado no canvas quanto pela seleção direta na lista
        // do painel Script Members (sem precisar de nenhum node existir).
        void DrawVariableDetailsPanel(ScriptVariable& v);
        // Editor do valor LOCAL de um node Set Variable (FloatValue/BoolValue/
        // IntLocalValue/Vec3Value/StringLocalValue — os mesmos campos lidos
        // pelo compilador quando o pin Value está desconectado). Função única
        // reutilizada pela caixinha inline no canvas (script_node_draw.cpp) E
        // pelo painel Script Details (script_details.cpp), para que o bug de
        // "editar aqui também muda lá" não possa voltar a existir escondido
        // em só um dos dois lugares — qualquer fix futuro nesse valor é feito
        // uma vez só, aqui. width < 0 usa a largura disponível inteira
        // (ImGui::SetNextItemWidth(-1)); width >= 0 usa esse valor exato.
        void DrawSetVariableLocalValueEditor(ScriptNode* node, ScriptVarType varType, float width);
        void DrawComponentFields(ScriptComponentDef& def, int i); // campos por tipo de componente — usado por DrawScriptDetails e pelo collapse inline no Scene Graph
        void DrawMyBlueprintWindow();  // painel Variables / Events / Dispatchers
        void CompileScript();
        void InitPreviewScene();
        void SyncMeshFromSource();
        void SyncMeshFromAsset();
        void SyncComponentsToPreview();  // espelha ScriptComponentDef → componentes reais no preview

        // ── Parametros do AnimGraph (Set Anim Float/Bool, Anim Trigger) ───────
        //
        // Compartilhados entre o combo do CANVAS e o painel Script Details, que
        // e o que ele pediu: "seria interessante ter nos dois". Uma fonte so
        // evita as duas telas divergirem.
        //
        // O tipo vai como int (cast de AnimParamType) para nao arrastar o
        // header do sistema de animacao para dentro deste.
        std::vector<std::pair<std::string, int>> CollectAnimGraphParams(bool forceReload);

        // Se o node for um dos tres de animacao, devolve o pin "Parametro" e
        // escreve em wantType o tipo esperado. Fora disso, nullptr.
        static ScriptPin* FindAnimParamPin(ScriptNode* node, int* wantType);

        // Desenha a lista de parametros (conteudo do popup/combo). Devolve
        // true se algo foi escolhido.
        bool DrawAnimParamList(ScriptPin* paramPin, int wantType);

        // Guarda onde o popup deve nascer, a partir do ultimo item submetido.
        void CaptureComboAnchor();

        // ── Combo deferido do canvas ─────────────────────────────────────────
        //
        // Popup aberto de dentro de um node do imgui-node-editor nao expande:
        // ele precisa ser desenhado no bloco ed::Suspend()/ed::Resume(), que
        // roda DEPOIS de todos os nodes. Entao o widget no node vira um botao
        // que so REGISTRA o pedido, e o popup e desenhado la.
        ed::NodeId m_ComboNodeId = ed::NodeId(0);
        int        m_ComboKind = 0;      // 0 = nenhum, 1 = action/axis, 2 = anim param

        // Retangulo do botao em coordenadas de TELA. O botao e desenhado em
        // espaco de canvas (que difere da tela quando ha zoom), entao a
        // conversao acontece na hora de guardar — o popup nasce colado nele e
        // com a mesma largura, como um combo de verdade.
        ImVec2     m_ComboAnchor{};
        float      m_ComboWidth = 0.f;
        bool       m_ComboRequested = false;
        void SaveNodePositions();           // salva posições do editor de volta nos ScriptNodes

        // Node editor
        ed::EditorContext* m_EdCtx = nullptr;
        ScriptGraph* m_Graph = nullptr;
        // Índice (não ponteiro!) da ScriptFunction atualmente aberta no
        // canvas, ou -1 se for o grafo principal. Índice em vez de ponteiro
        // pelo mesmo motivo de m_SelectedVar: adicionar/remover qualquer
        // outra função pode realocar ou deslocar o vector<ScriptFunction>,
        // o que invalidaria/trocaria silenciosamente o que um ponteiro
        // estaria apontando.
        int m_EditingFunctionIndex = -1;
        ScriptComponent* m_Component = nullptr;
        entt::entity       m_Entity = entt::null;
        entt::registry* m_SourceRegistry = nullptr;
        InspectorWindow* m_InspectorWindow = nullptr;  // para DrawMaterialGraphParams
        std::shared_ptr<ScriptAsset> m_ScriptAsset;

        // Preview
        std::unique_ptr<ViewportRenderer>  m_PreviewRenderer;
        std::shared_ptr<Framebuffer>       m_PreviewFramebuffer;
        std::unique_ptr<Scene>             m_PreviewScene;
        std::unique_ptr<SceneEnvironment>  m_PreviewEnvironment;

        // Anima o personagem do preview quando o script tem um componente
        // SkeletalMesh — sem isto o Y Bot apareceria congelado na bind pose.
        std::unique_ptr<AnimationWorld>    m_PreviewAnim;

        // Qual asset ja teve o transform raiz aplicado no preview (ponteiro
        // como identidade — nao desreferenciado).
        const void* m_RootTransformAppliedFor = nullptr;

        ScriptSavedCallback m_ScriptSavedCallback;
        entt::entity                       m_PreviewEntity = entt::null;
        ImVec2                             m_PreviewSize = { 0, 0 };
        bool                               m_PreviewHovered = false;
        ImVec2                             m_PreviewMouseDelta = {};
        bool                               m_PreviewEntitySelected = true;  // objeto sempre selecionado no preview
        ImVec2                             m_PreviewBoundsMin = {};
        ImVec2                             m_PreviewBoundsMax = {};
        ImGuizmo::OPERATION                m_GizmoOp = ImGuizmo::TRANSLATE;

        // Context menu
        ImVec2      m_CtxCanvasPos = {};
        ed::PinId   m_CtxPinId;
        ed::NodeId  m_CtxNodeId;
        char        m_CtxBuf[128] = {};
        std::string m_PendingNodeType;         // node a criar no próximo frame (dentro do Begin/End)
        ImVec2      m_PendingNodePos = {};
        std::string m_PendingNodeStrValue;
        // Variable drop Get/Set popup
        std::string m_VarDropName;
        ImVec2      m_VarDropPos = {};
        bool        m_VarDropPending = false;
        bool        m_InsideNodeEditorFrame = false;
        bool        m_SpringArmDragging = false;
        int         m_PendingVarType = 0;
        bool        m_VarDropIsCanvas = false;
        // Promote to Variable pending state
        ed::PinId          m_PendingPromotePinId = {};
        bool               m_PendingPromoteIsInput = false;
        int                m_PendingPromoteVarType = 0;
        ScriptPinType      m_PendingPromotePinType = ScriptPinType::Float;
        bool   m_CtxOpen[7] = { true, true, true, true, true, false, false };

        char   m_CompSearchBuf[128] = {};
        int    m_SelectedCompIndex = -1; // componente selecionado no Scene Graph

        bool m_IsOpen = false;
        bool m_FirstFrame = true;
        bool m_LayoutBuilt = false;

        std::string m_Msg;
        bool        m_MsgOk = false;
        float       m_MsgTimer = 0.0f;

        std::vector<std::string> m_ConsoleLines;

        // My Blueprint panel state
        char  m_NewVarName[64] = "NewVar";
        int   m_NewVarType = 0;
        char  m_NewVarCategory[64] = "";
        char  m_NewEvtName[64] = "OnMyEvent";
        char  m_NewFuncName[64] = "NewFunction";
        // Nome da função atualmente "expandida" na lista (mostrando o editor
        // de Inputs/Outputs) — vazio = nenhuma expandida. Só uma por vez,
        // igual a um accordion, pra não poluir o painel com todas abertas.
        std::string m_ExpandedFunc;
        char  m_NewParamName[64] = "Param";
        int   m_NewParamType = 0;
        int   m_SelectedVar = -1;
        // Último node selecionado no canvas (frame anterior) — usado para
        // detectar QUANDO a seleção do canvas muda, e nesse momento limpar
        // m_SelectedVar. Sem isso, uma variável selecionada na lista do
        // Script Members nunca era liberada, mesmo clicando em nodes no
        // grafo (bug: seleção da variável "eterna", aba Node travada nela).
        ed::NodeId m_LastCanvasSelectedNode = {};
        int   m_RenamingVar = -1;
        char  m_RenameBuf[64] = {};
        bool  m_RenameJustStarted = false;
        int   m_DeleteVarIndex = -1;
        std::string m_DeleteVarName;
        bool  m_DeleteVarAlsoNodes = true;

        // Edição de categoria — buffer persistente (evita reset a cada frame)
        char  m_VarCatEditBuf[64] = {};
        int   m_VarCatEditIdx = -1;

        // Drag and drop de variável para categoria — índice armazenado de forma
        // estável (evita depender do payload binário referenciar a variável de
        // loop, cujo endereço/valor podia não sobreviver de forma confiável
        // entre o frame do BeginDragDropSource e o frame do drop no target).
        int   m_DragVarIndex = -1;

        // Renomear categoria — duplo clique no header
        std::string m_RenamingCat;
        char        m_RenameCatBuf[64] = {};
        bool        m_RenameCatJustStarted = false;

        // Rename inline do título do Comment box (duplo clique) — guarda o
        // ID do node em edição, igual ao padrão de m_RenamingCat acima.
        int  m_RenamingComment = -1;
        bool m_RenameCommentJustStarted = false;
        std::vector<ed::NodeId> m_PendingDeleteNodes;
        bool  m_CompCollapsed[32] = {};
        bool  m_ScaleLocked = false; // cadeado do Scale: true = escala uniforme (todos os eixos juntos)
        ImVec2      m_GraphWindowCenter = {};
        entt::entity m_CameraPreviewEntity = entt::null;  // mesh de câmera no preview 3D
        Scene* m_ActiveScene = nullptr;
        CommandHistory m_History;
        std::string    m_SnapshotBeforeAction;
        std::string    m_PendingUndoSnapshot;
        std::string    m_PendingRedoSnapshot;  // cena ativa do editor (para propagar mudanças em Play)
    };

} // namespace axe