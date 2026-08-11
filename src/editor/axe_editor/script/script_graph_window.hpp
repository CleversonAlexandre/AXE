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

        // ── SC7: undo por GESTO (portado do Control Rig) ──────────────────────
        //
        // PushUndo/CommitUndo exigem que quem edita conheça o INÍCIO e o FIM da
        // ação. Para um clique de menu isso é trivial; para um ARRASTO é
        // impossível — quando o código percebe que o node se moveu, o gesto já
        // começou, e não há onde encaixar o PushUndo. Foi exatamente por isso
        // que "Move Node" nunca entrou no histórico daqui: das 28 ações
        // registradas, mover node não é uma delas, e o Control Rig tem a dele.
        //
        // O mecanismo do rig resolve invertendo: em vez de fotografar o "antes"
        // quando a ação começa, mantém-se SEMPRE uma foto do último estado
        // confirmado (m_Baseline). Aí qualquer edição só precisa AVISAR que
        // aconteceu; o comando é fechado depois, quando o gesto termina de
        // verdade.
        //
        // MarkEdited pode ser chamado dezenas de vezes durante um arrasto: só a
        // primeira abre o passo, e as seguintes são no-op. Sem isso, arrastar
        // um node de um canto ao outro viraria um passo de undo POR FRAME.
        void MarkEdited(const char* actionName);

        // ── SC9: aplicar as posicoes do MODELO de volta no canvas ─────────────
        //
        // O node-editor guarda a posicao de cada node por conta propria, e ela
        // so era empurrada de volta no m_FirstFrame. Depois de um Undo, o
        // modelo voltava para a posicao antiga mas o CANVAS continuava com a
        // nova — e no frame seguinte a deteccao de arrasto via a diferenca,
        // marcava "Move Node" e gravava a posicao do canvas por cima da
        // restaurada. O undo era desfeito por si mesmo, em silencio.
        //
        // Flag em vez de chamada direta porque Undo() roda FORA do par
        // ed::Begin/End; aplicar ali dependeria de o node-editor aceitar
        // SetNodePosition sem frame aberto. Consumida no inicio do desenho do
        // canvas, onde o contexto e garantido.
        bool m_PendingPositionSync = false;

        // Nodes a selecionar no proximo frame do canvas. Mesma razao do
        // m_PendingPositionSync: ed::SelectNode fora do Begin/End opera sobre
        // um contexto que ainda nao viu os nodes.
        std::vector<ed::NodeId> m_PendingSelectNodes;

        // ── SC11: posicoes no instante em que o botao do mouse desceu ─────────
        //
        // O arrasto de node deixou de depender de heuristica. Antes eu tentava
        // NOTAR que um node se moveu e fechar o passo "quando o gesto parecer
        // ter acabado" (IsAnyItemActive + IsMouseDown). Nao consegui provar em
        // que ponto isso falhava — e um mecanismo de undo que funciona "quase
        // sempre" e pior do que nao ter, porque o autor deixa de confiar e
        // passa a salvar antes de cada movimento.
        //
        // Agora o inicio e o fim do gesto sao dois eventos EXATOS do mouse.
        // No clique, guarda-se onde cada node estava; na soltura, compara-se.
        // Nao ha o que estimar.
        std::vector<std::pair<int, ImVec2>> m_DragStartPositions;

        // Restaura um snapshot preservando em qual grafo o autor estava.
        void RestoreFromSnapshot(const std::string& snapshot);

        // ── SC8: área de transferência de nodes ───────────────────────────────
        //
        // Ctrl+C copiava nada e ainda CRIAVA um Comment: o atalho de comentário
        // era a tecla 'C' sem checar modificador, então qualquer Ctrl+C no
        // canvas virava um comentário novo.
        //
        // O clipboard é uma string JSON no próprio editor, e não o clipboard do
        // sistema: colar texto arbitrário de fora produziria um grafo
        // imprevisível, e não há como validar de onde o JSON veio.
        void CopySelectedNodes(bool cut);
        void PasteNodes();
        void DuplicateSelectedNodes();
        void SaveScript();          // o mesmo que o botão Save da barra
        std::string m_NodeClipboard;
        void CommitPendingUndo();   // uma vez por frame, no fim do desenho

        // ── SC16: cobertura automatica de undo ────────────────────────────────
        //
        // A auditoria dos paineis achou ~60 widgets que escrevem direto no
        // asset sem passar por undo nenhum: praticamente todo o inspetor de
        // componentes (Rigidbody, Collider, CharacterController, SpringArm,
        // Camera), os defaults de variavel no painel Node, e parte do Members.
        //
        // Sair colocando PushUndo/CommitUndo em sessenta lugares seria repetir
        // o erro que gerou o problema: um site novo nasce sem cobertura e
        // ninguem nota, porque a falta de undo nao quebra nada — so custa o
        // trabalho de quem editou.
        //
        // Como o snapshot ja e o ASSET INTEIRO, o undo nao precisa saber ONDE
        // a edicao aconteceu. Basta perceber QUANDO uma interacao terminou e
        // comparar. m_AnyItemActiveLastFrame detecta a borda de desativacao de
        // qualquer widget de qualquer painel; a comparacao contra o baseline
        // decide se houve mudanca de fato. Um site novo nasce coberto.
        bool        m_AnyItemActiveLastFrame = false;
        std::string m_ActiveWidgetWindow;   // painel do widget em uso, para nomear o passo

        // Fecha o passo de undo de um arrasto de node usando as posicoes
        // guardadas em m_DragStartPositions como "antes".
        void CommitNodeDrag();
        void RefreshBaseline();     // realinha o "antes" com o estado atual
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
        void DrawScriptDetails();

        // ── S3: painel dos nodes de referencia entre scripts ──────────────────
        //
        // Escolhe o .axescript alvo (guardado por UUID em StringValue) e, nos
        // nodes de variavel, qual variavel dele (nome em StringLocalValue).
        //
        // A lista de variaveis vem do MANIFESTO — o proprio .axescript, lido do
        // disco. Nao da DLL do alvo: carrega-la aqui criaria dependencia de
        // build entre scripts e mataria o hot reload isolado, que e a razao de
        // cada script ser uma DLL propria.
        void DrawScriptRefNodeDetails(ScriptNode* node);      // conteúdo do painel Details quando objeto selecionado
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

        // ── SC15: enquadra a camera do preview no conteudo ────────────────────
        //
        // A camera do preview nascia sempre na posicao padrao da EditorCamera
        // (foco na origem, distancia fixa). Como o Script Editor NAO normaliza
        // a escala do personagem — quem manda e o Transform raiz do asset, que
        // o autor definiu — a camera padrao caia dentro do modelo e a tela
        // abria mostrando os pes.
        //
        // Mede a malha em espaco de MUNDO (bounds x escala da entidade) e
        // aponta a camera para o meio dela. Uma vez por asset aberto: refazer
        // isso a cada sync roubaria a orbita que o autor acabou de ajustar —
        // mesma regra ja aplicada no preview do AnimGraph.
        void FramePreviewCamera();
        const void* m_CameraFramedFor = nullptr;
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
        // SC18 — 11 posicoes, uma por categoria de s_Cats. Era 7 enquanto o
        // menu percorria 8 categorias: a oitava (Input) lia e escrevia UMA
        // POSICAO ALEM do array, em cima do que estivesse na memoria a seguir.
        // Nao dava crash porque bool[7] costuma cair num bloco com folga, mas
        // era estouro de buffer real toda vez que o menu abria.
        bool   m_CtxOpen[11] = { true, true, true, true, true, false, false,
                                 false, false, true, false };

        char   m_CompSearchBuf[128] = {};
        int    m_SelectedCompIndex = -1; // componente selecionado no Scene Graph

        bool m_IsOpen = false;
        bool m_FirstFrame = true;
        bool m_LayoutBuilt = false;

        std::string m_Msg;
        bool        m_MsgOk = false;
        float       m_MsgTimer = 0.0f;

        std::vector<std::string> m_ConsoleLines;

        // ── SC15: filtro do console ───────────────────────────────────────────
        // Publico porque ClassifyConsoleLine, no .cpp, e uma funcao livre.
    public:
        enum class ConsoleSeverity { Info, Warning, Error };
    private:
        bool m_ConsoleShowInfo = true;
        bool m_ConsoleShowWarn = true;
        bool m_ConsoleShowError = true;

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

        // ── SC2: fio carregado no Ctrl + clique esquerdo ──────────────────────
        // Ctrl+LMB num pin conectado DESPLUGA os fios daquele pin e passa a
        // "carregá-los" presos ao mouse, para replugar num pin compatível —
        // mesmo gesto da Unreal. Ctrl+RMB despluga na hora, sem carregar.
        //
        // O imgui-node-editor não tem esse modo nativo: ele só sabe iniciar um
        // link a partir de um drag que ele mesmo detectou. Não existe API para
        // injetar um drag sintético, então o carregamento é estado nosso, e o
        // fio provisório é desenhado à mão (ver DrawCarriedWire).
        //
        // Um pin de SAÍDA pode ter N fios; ao pegá-lo, todos vêm juntos e todos
        // são replugados no destino novo. É o comportamento da Unreal e o que
        // evita a pergunta "qual dos cinco fios você quis mover?".
        bool                    m_CarryingWire = false;
        // Pins da OUTRA ponta de cada fio pego — os que continuam ancorados no
        // grafo. Se o pin agarrado era Input, estes são Outputs (e o destino
        // novo tem de ser um Input), e vice-versa.
        std::vector<ed::PinId>  m_CarriedRemotePins;
        // Pin de onde os fios foram arrancados. Só serve para restaurar no
        // cancelamento (Esc / clique direito / clique no vazio).
        ed::PinId               m_CarriedFromPin = {};

        // Posição (em espaço de canvas) do centro do ícone de cada pin, no
        // frame atual. O imgui-node-editor não expõe a posição de um pin —
        // GetNodePosition/GetNodeSize dão só a caixa do node — então o
        // preview do fio carregado não teria de onde partir. Preenchido por
        // TrackPinRect durante o desenho dos nodes e limpo a cada frame; nunca
        // consultar fora do par ed::Begin/ed::End.
        std::vector<std::pair<int, ImVec2>> m_PinCanvasPos;
        void   TrackPinRect(ed::PinId id);          // chamar logo após o ícone do pin
        bool   GetPinCanvasPos(ed::PinId id, ImVec2& out) const;

        // Um gesto de Ctrl+direito (desplugar, ou abortar o carregamento) não
        // pode deixar um menu de contexto abrir logo atrás dele — pareceria
        // que foi o menu que apagou o fio. Vale só pelo frame em que o gesto
        // aconteceu.
        bool m_SuppressCtxMenuThisFrame = false;

        // Cursor de mão desenhado por nós. Nem o ImGui nem o GLFW nem o Win32
        // têm "palma aberta"/"punho fechado" entre os cursores padrão — o
        // ImGuiMouseCursor_Hand é o dedo apontando (aquele "L"), e a lista do
        // GLFW e a do Windows param no mesmo lugar. Um cursor customizado do
        // sistema exigiria bitmap + glfwCreateCursor, o que só existe atrás da
        // abstração Window da engine. Desenhar no ImGui resolve sem furar
        // camada nenhuma: escondemos o cursor do SO e pintamos o nosso.
        enum class HandCursor { None, Open, Closed };
        HandCursor m_HandCursor = HandCursor::None;

        // Desenhado DEPOIS de ed::End(), em espaço de tela: dentro do canvas o
        // desenho é escalado pelo zoom, e um cursor que encolhe quando você
        // afasta a vista está errado.
        void DrawHandCursor();

        // ── SC3: valor padrão de pin de entrada ───────────────────────────────
        // ScriptPin carrega um ScriptValue (SC17; antes eram cinco campos
        // paralelos), e ScriptGraphCompiler::ResolvePin() o LÊ para todo pin
        // de entrada sem fio. Só que nenhuma tela do editor jamais ESCREVEU
        // neles — grep no src/editor inteiro não acha uma atribuição sequer
        // (as que aparecem são do Material Graph, que é outro PinType e faz
        // isso certo). Consequência: entrada desconectada sempre compilava
        // 0 / false / "" / vec3(0), e digitar uma constante era impossível.
        //
        // Uma função só, usada pelo canvas E pelo painel Node, para as duas
        // telas não terem como divergir — mesmo motivo pelo qual
        // DrawSetVariableLocalValueEditor foi extraída.
        float PinDefaultEditorWidth(const ScriptPin& pin) const;  // 0 = sem editor
        void  DrawPinDefaultEditor(ScriptPin& pin, float width);

        void DrawCarriedWire();   // preview do(s) fio(s) presos ao mouse
        void CancelCarriedWire(); // religa tudo no pin original e sai do modo
        bool  m_CompCollapsed[32] = {};
        bool  m_ScaleLocked = false; // cadeado do Scale: true = escala uniforme (todos os eixos juntos)
        ImVec2      m_GraphWindowCenter = {};
        entt::entity m_CameraPreviewEntity = entt::null;  // mesh de câmera no preview 3D
        Scene* m_ActiveScene = nullptr;
        CommandHistory m_History;
        std::string    m_SnapshotBeforeAction;

        // Foto do último estado CONFIRMADO. É o "antes" de todo passo aberto
        // por MarkEdited. Precisa ser realinhado depois de cada commit, de
        // cada Undo/Redo e ao abrir um asset — um baseline velho faria o
        // próximo Ctrl+Z voltar demais, engolindo edições que já tinham
        // passo próprio.
        std::string    m_Baseline;
        bool           m_PendingUndo = false;
        std::string    m_PendingUndoName;

        // ── SC6: estado de compilação, refletido no botão Compilar ────────────
        //
        // O grafo na tela e o C++ compilado podem estar dessincronizados sem
        // que nada avise. Antes o botão parecia igual nos três casos — nunca
        // compilado, compilado com sucesso, compilado com erro — e a única
        // pista era rolar o console.
        //
        // m_GraphDirty é marcado dentro de CommitUndo(), e não em cada ponto
        // de edição: CommitUndo é o funil por onde TODA mutação do grafo passa
        // (é o que o sistema de undo exige), então marcar ali cobre node novo,
        // variável, função, valor de pin e ligação de uma vez. Marcar em cada
        // chamador seria vinte lugares para esquecer um.
        bool m_GraphDirty = true;    // true no início: nunca compilado nesta sessão
        bool m_LastCompileFailed = false;
        std::string    m_PendingUndoSnapshot;
        std::string    m_PendingRedoSnapshot;  // cena ativa do editor (para propagar mudanças em Play)
    };

} // namespace axe