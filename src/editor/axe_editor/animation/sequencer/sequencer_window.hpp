#pragma once

// src/editor/axe_editor/animation/sequencer/sequencer_window.hpp
//
// SequencerWindow — janela ImGui principal do Sequencer (Fase 2 MVP).
//
// CONFIRMADO contra o repo AXE:
//   - Namespace axe (sem sub-namespace — ver editor_layer.hpp, anim_clip_window.hpp)
//   - Classe sem heranca de Layer. Padrao do projeto: AnimClipWindow / ControlRigWindow
//     sao classes com Draw() publico, membros do EditorUI.
//   - EditorContext tem SelectedEntity, HasSelection(), ActiveScene->GetRegistry().
//   - SkeletalMeshComponent tem BonePalette (vector<mat4>), GetSkeleton().
//   - Skeleton tem FindBone(name) -> int.
//   - Estilo: ui::IconButton / AccentButton / ToggleButton + ICON_* (Font Awesome 6).
//     Ver editor_widgets.hpp + editor_icons.hpp.

#include "axe/animation/sequencer/sequencer_asset.hpp"
#include "axe/animation/sequencer/sequencer_player.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/pose.hpp"              // Pose / BoneTransform
#include "axe/animation/animation_clip.hpp"    // AnimationClip (tracks de clipe)
#include "axe/animation/animation_sampler.hpp" // BuildSkinningMatrices / SamplePose
#include "axe/animation/skeletal_mesh_asset.hpp" // SkeletalMeshAsset::Socket
#include "axe/animation/rig/control_rig_asset.hpp"
#include "axe/animation/rig/rig_hierarchy.hpp"
#include "axe/animation/rig/rig_graph.hpp"
#include "axe/utils/glm_config.hpp"
// AssetType: o registro do .axeseq e do clipe assado passa por aqui.
#include "axe/asset/asset.hpp"

#include <imgui.h>
// ImRect e SeparatorEx vivem em imgui_internal.h (mesmo padrao do AXE).
#include <imgui/imgui_internal.h>
#include <entt/entt.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace axe
{
    class EditorContext;

    class SequencerWindow
    {
    public:
        SequencerWindow();
        ~SequencerWindow();

        void Draw();
        void SetContext(EditorContext* context) { m_Context = context; }

        void Open() { m_IsOpen = true; }
        void Close() { m_IsOpen = false; }
        bool IsOpen() const { return m_IsOpen; }

        // Abre um `.axeseq` especifico e traz a janela para a frente. E o que o
        // duplo clique no Asset Browser chama.
        //
        // Publico porque quem sabe qual arquivo o usuario escolheu e o
        // editor_layer, nao esta janela — o mesmo padrao de
        // AnimClipWindow::OpenForAsset e MaterialEditorWindow::OpenMaterial.
        bool OpenFile(const std::filesystem::path& path);

        // ── QUEM RESPONDE AO Ctrl+Z ──────────────────────────────────────────
        //
        // O `HandleSceneInput` do editor_layer trata Ctrl+Z / Ctrl+Y / Ctrl+S
        // SEM guarda de foco. Com o undo do Sequencer no ar, um Ctrl+Z com esta
        // janela em foco dispararia os DOIS: desfazia a edicao da curva e,
        // junto, um movimento de entidade no viewport que ninguem pediu.
        //
        // A precedencia ja existia no editor para o Material Editor
        // (`IsOpen() && IsFocused()`); isto so estende a mesma regra.
        bool IsFocused() const { return m_IsFocused; }

        SequencerAsset& GetAsset() { return m_Asset; }
        SequencerPlayer& GetPlayer() { return m_Player; }

    private:
        bool           m_IsOpen = false;
        bool           m_IsFocused = false;
        SequencerAsset m_Asset;
        SequencerPlayer m_Player;
        bool           m_PlayerStarted = false;
        EditorContext* m_Context = nullptr;

        // --- Estado de UI -------------------------------------------------
        int m_SelectedBinding = -1;
        int m_SelectedTrack = -1;
        int m_SelectedSection = -1;
        int m_SelectedChannel = -1;
        int m_SelectedKey = -1;

        bool m_SnapEnabled = true;
        int  m_SnapFrame = 1;

        float m_OutlinerWidth = 280.0f;

        // Drag state para mover keys na timeline.
        bool  m_DraggingKey = false;
        float m_DragStartMouseX = 0.0f;
        // (m_DragOriginalFrame, singular, foi removido: o arrasto agora move a
        // selecao inteira e o estado equivalente e m_DragOriginalFrames.)

        // Drag state para playhead.
        bool  m_DraggingPlayhead = false;

        // ═══════════════════════════════════════════════════════════════════
        //  ARQUIVO
        //
        //  `m_LastLoadedPath` ja existia, mas os dois botoes da toolbar caiam
        //  em "Assets/sequencer.axeseq" quando ele estava vazio — o que
        //  significa que, na pratica, havia UMA sequence por projeto e abrir
        //  uma segunda sobrescrevia a primeira.
        //
        //  Agora: Novo / Abrir / Salvar / Salvar como, com o FileDialog nativo
        //  que o editor ja usa para cena e material.
        // ═══════════════════════════════════════════════════════════════════
        std::string m_LastLoadedPath;

        // ── MARCA DE SUJO POR ASSINATURA, E NAO POR FLAG ─────────────────────
        //
        // Um `m_Dirty = true` teria de ser escrito em cada um dos ~15 pontos
        // que mutam o asset (add/remove binding, track, section, channel, key,
        // drag, interp, valor, explode, range, fps, mute, lock, attach...), e
        // o primeiro que alguem esquecesse produziria exatamente o pior
        // resultado possivel: fechar sem aviso e perder o trabalho.
        //
        // A assinatura percorre o asset inteiro uma vez por frame. Nao e caro
        // perto do que a janela ja faz: o `SyncFrom` COPIA a mesma estrutura
        // toda, todo frame, e ninguem nota.
        std::uint64_t m_SavedSignature = 0;

        bool IsDirty() const;

        // ═══════════════════════════════════════════════════════════════════
        //  UNDO / REDO
        //
        //  ── POR QUE SNAPSHOT, E NAO COMANDO ────────────────────────────────
        //
        //  O padrao do editor e o `CommandHistory` (par do/undo por acao), e e
        //  o certo para o viewport: mover UMA entidade e uma acao com comeco e
        //  fim claros.
        //
        //  Aqui nao. O Sequencer muta o asset por uns quinze caminhos — add e
        //  remove de binding, track, section, canal e key; drag de key; drag de
        //  playhead com REC ligado; explode; range; fps; mute; lock; anexo de
        //  socket; captura; bake. Escrever um comando para cada e a mesma
        //  aposta da marca de sujo por flag: o primeiro que alguem esquecesse
        //  produziria um Ctrl+Z que nao desfaz — e um undo em que nao se pode
        //  confiar e pior que nenhum, porque o animador so descobre depois de
        //  ja ter apostado nele.
        //
        //  O snapshot nao depende de ninguem lembrar de nada.
        //
        //  ── O CUSTO, MEDIDO CONTRA O QUE JA ACONTECE ───────────────────────
        //
        //  Um snapshot e uma copia de `std::vector<SequencerBinding>`. O
        //  `SequencerPlayer::SyncFrom` faz exatamente essa copia SESSENTA VEZES
        //  POR SEGUNDO, e ninguem nota. Guardar algumas dezenas delas custa
        //  memoria, nao tempo — e a memoria e limitada logo abaixo.
        //
        //  ── COMO UM GESTO VIRA UMA ENTRADA ─────────────────────────────────
        //
        //  A deteccao e por ASSINATURA, a mesma da marca de sujo. Mas um
        //  arrasto muda a assinatura a cada frame, e sessenta entradas por
        //  segundo transformariam o Ctrl+Z num nanico inutil.
        //
        //  Entao o gesto e AGRUPADO: a primeira mudanca abre um pendente com o
        //  estado anterior, e ele so e empilhado quando o mouse esta solto e a
        //  assinatura ficou parada. Um arrasto inteiro vira UM undo, que e o
        //  que a pessoa espera desfazer.
        // ═══════════════════════════════════════════════════════════════════
        struct Snapshot
        {
            std::vector<SequencerBinding> Bindings;
            SequencerFrameRange           Range;
            int                           Fps = 30;
        };

        std::vector<Snapshot> m_UndoStack;
        std::vector<Snapshot> m_RedoStack;

        // Estado do frame anterior — o candidato a ponto de retorno.
        Snapshot      m_LastState;
        bool          m_HasLastState = false;
        std::uint64_t m_LastSignature = 0;

        // Gesto em andamento.
        Snapshot m_UndoPending;
        bool     m_UndoPendingOpen = false;
        int      m_UndoIdleFrames = 0;

        Snapshot TakeSnapshot() const;
        void     RestoreSnapshot(const Snapshot& s);

        // Uma vez por frame, no FIM do Draw: tudo que muta o asset neste frame
        // (inclusive o gizmo, que roda no desenho do viewport) ja aconteceu.
        void TrackUndoState();

        void Undo();
        void Redo();

        bool CanUndo() const { return !m_UndoStack.empty() || m_UndoPendingOpen; }
        bool CanRedo() const { return !m_RedoStack.empty(); }

        // Reassenta selecao, pendencias e player depois de um salto no
        // historico. Indices guardados apontam para uma arvore que acabou de
        // ser substituida.
        void AfterHistoryJump();

        // ═══════════════════════════════════════════════════════════════════
        //  AREA DE TRANSFERENCIA DE KEYS
        //
        //  O recorte guarda o ALVO por nome (binding, tipo, nome da track,
        //  componente) e o frame RELATIVO ao inicio do recorte. Nao guarda
        //  indice de nada: colar depois de apagar uma track deslocaria todos os
        //  indices seguintes e as keys cairiam no osso errado — em silencio, que
        //  e o pior jeito de errar.
        //
        //  O frame relativo e o que faz "copiei o coice do frame 18 ao 24, colo
        //  no 40" funcionar sem conta nenhuma do lado do usuario.
        // ═══════════════════════════════════════════════════════════════════
        struct KeyClip
        {
            int                       Binding = -1;
            SequencerTargetType       TargetType = SequencerTargetType::Bone;
            std::string               TargetName;
            SequencerChannelComponent Component = SequencerChannelComponent::X;
            float                     FrameOffset = 0.0f;
            SequencerKey              Key;
        };

        std::vector<KeyClip> m_KeyClipboard;

        // cut = true recorta (copia e apaga).
        void CopySelectedKeys(bool cut);

        // Cola a partir do playhead. Devolve quantas keys entraram.
        int  PasteKeys(const std::vector<KeyClip>& clips);

        void PasteClipboardAtPlayhead();
        void DuplicateSelectedKeys();

        // Registra um arquivo recem-criado no AssetDatabase: UUID, `.axemeta`,
        // pasta virtual derivada do caminho em disco e gravacao do indice. Ver
        // a nota longa na implementacao — as tres coisas sao necessarias, e
        // faltando qualquer uma o arquivo nao aparece no Asset Browser.
        std::string RegisterProjectAsset(const std::filesystem::path& file,
            AssetType typeOverride);

        // Grava e reassenta a assinatura. Devolve false com log de erro.
        bool SaveToPath(const std::string& path);

        // Salvar: usa o caminho conhecido; sem caminho, cai no Salvar como.
        bool SaveInteractive(bool forceAsk);

        // Descarta tudo e comeca uma sequence vazia. NAO pergunta — quem
        // pergunta e o botao, que so chama isto depois do usuario confirmar.
        void NewSequence();

        // Nome de exibicao do arquivo atual ("(sem titulo)" quando nao ha).
        std::string CurrentFileLabel() const;

        // Confirmacao de "Nova sequence" com trabalho nao salvo.
        bool m_OpenConfirmNewPopup = false;
        void DrawConfirmNewPopup();

        // ═══════════════════════════════════════════════════════════════════
        //  BAKE — SAIR DO SEQUENCER COM UM CLIPE
        //
        //  ── ONDE O CLIPE VAI PARAR, E POR QUE ─────────────────────────────
        //
        //  No AXE um AnimationClip NAO e asset: nao existe AssetType para ele
        //  nem extensao de autoria. Um clipe vive dentro do `.axeskel` como
        //  uma AnimEntry apontando para um arquivo, e o binario cozido
        //  (`.axeclipbin`) ja tem leitura e escrita prontas em ClipCooked.
        //
        //  Entao o bake nao inventa formato nenhum: grava um `.axeclipbin` ao
        //  lado do `.axeskel` e registra a entrada. A partir dai o clipe
        //  aparece no AnimGraph, no Animation Editor, no picker do proprio
        //  Sequencer e toca no jogo — de graca, porque e o caminho que o
        //  engine ja percorre.
        //
        //  O detalhe que fez isso caber em zero linha de runtime: o
        //  `Resolve()` do `.axeskel` chama `ClipCooked::TryLoadFor(source)`
        //  ANTES de tentar importar o FBX, e `PathFor` de um `.axeclipbin`
        //  devolve ele mesmo. Uma entrada que aponta direto para o cozido
        //  carrega pelo caminho rapido e nunca chega no importador.
        //
        //  ── O QUE E AMOSTRADO ─────────────────────────────────────────────
        //
        //  A pose FINAL, frame a frame: clipe base + tracks de osso + solve do
        //  Control Rig. E o que 'bake' quer dizer em qualquer DCC, e o
        //  resultado toca sem precisar do rig.
        //
        //  A amostragem reusa o EvaluateAndApply INTEIRO, um frame por vez.
        //  Poderia haver uma funcao de avaliacao dedicada, mais rapida; ela
        //  seria uma SEGUNDA versao da mesma cadeia, e no dia em que as duas
        //  divergissem o sintoma seria "o clipe assado nao e o que eu via" —
        //  o pior bug possivel numa ferramenta de animacao.
        // ═══════════════════════════════════════════════════════════════════
        bool m_OpenBakePopup = false;
        int  m_BakeBinding = -1;
        char m_BakeClipName[96] = { 0 };
        bool m_BakeLoop = false;

        void DrawBakePopup();

        // ── O TEMPO DURANTE O BAKE ───────────────────────────────────────────
        //
        // O SolveRig entrega `RigExecContext::DeltaTime` ao grafo, e fora do
        // bake esse valor e o dt do FRAME DE UI. Isso esta certo enquanto o
        // usuario esta olhando: um no de mola ou de amortecimento deve reagir
        // no tempo real da tela.
        //
        // Num bake, nao: os 250 passos aconteceriam todos com o mesmo dt de
        // ~16ms independentemente do fps da sequence, e um rig com qualquer
        // no dependente de tempo assaria uma curva que nao corresponde a
        // nenhuma reproducao possivel. Aqui o dt e 1/fps, que e exatamente o
        // intervalo entre os frames que estao sendo gravados.
        bool  m_Baking = false;
        float m_BakeDeltaTime = 0.0f;

        // Devolve o caminho gravado, ou vazio em caso de falha (com log).
        std::filesystem::path BakeBindingToClip(int bindingIndex,
            const std::string& clipName, bool looping);

        // --- Sub-rotinas de UI --------------------------------------------
        void DrawToolbar();
        void DrawOutliner();
        void DrawTimeline();
        void DrawTransportBar();

        // ── GRUPOS E FILTRO DO OUTLINER ──────────────────────────────────────
        //
        // Um clipe explodido produz uma track por osso. Com 52 tracks, o
        // outliner deixa de ser uma arvore e vira uma lista impossivel: para
        // chegar no Control Rig, que fica no topo, era preciso rolar por cima de
        // cinquenta dedos.
        //
        // Os grupos resolvem por dobra, e o filtro por busca. Os dois sao
        // NECESSARIOS e nem um nem outro basta: dobrar sem filtrar obriga a
        // abrir os cinquenta para achar um; filtrar sem dobrar deixa a lista
        // sempre longa quando o campo esta vazio.
        //
        // A ordem dos valores E a ordem de exibicao: animacao (a performance),
        // sockets (o que esta preso), controles (o rig) e por fim os ossos (o
        // FK cru, que e o que mais existe e o que menos se procura).
        enum class TrackGroup : std::uint8_t
        {
            Clip = 0,
            Socket = 1,
            Control = 2,
            Bone = 3,
            Other = 4,
            Count = 5
        };

        static TrackGroup  GroupOf(const SequencerTrack& track);
        static const char* GroupLabel(TrackGroup g);
        static const char* GroupIcon(TrackGroup g);

        // O que cada binding mostra neste frame. Recalculado uma vez por frame,
        // ANTES do outliner e da timeline — as duas leem daqui.
        //
        // Compartilhar a lista nao e economia, e correcao: se cada um decidisse
        // por conta propria o que desenhar, dobrar um grupo esconderia a linha
        // no outliner e deixaria a lane na timeline, e as duas metades da janela
        // passariam a discordar sobre o que existe.
        struct BindingLayout
        {
            // Tracks de cada grupo que passam no filtro, ja na ordem de
            // hierarquia. Usado pelo outliner (mostra a contagem mesmo dobrado).
            std::vector<int> Groups[static_cast<int>(TrackGroup::Count)];

            // As que realmente aparecem (grupo aberto), na ordem final. E o que
            // a timeline desenha, lane a lane.
            std::vector<int> Visible;
        };

        std::vector<BindingLayout> m_Layout;
        void RebuildLayout();

        // Filtro por nome do alvo. Vale para todos os bindings — procurar
        // "LeftHand" e uma pergunta sobre a cena, nao sobre um personagem.
        char m_TrackFilter[96] = { 0 };

        // Aberto/fechado por "<binding>|<grupo>". Mapa, e nao um bool por
        // grupo, porque o numero de bindings e livre.
        std::unordered_map<std::string, bool> m_GroupOpen;

        bool IsGroupOpen(int bindingIndex, TrackGroup g) const;
        void SetGroupOpen(int bindingIndex, TrackGroup g, bool open);

        // Profundidade de um controle na hierarquia do rig. So para indentar —
        // um rig e raso (6 ou 7 niveis) e a indentacao e o que faz a lista LER
        // como a hierarquia que o autor montou, em vez de um monte de nomes.
        int ControlDepth(int bindingIndex, const std::string& controlName);

        // Cria track para TODOS os controles de transform do rig, na ordem da
        // hierarquia. E o gesto normal: quem liga um Control Rig quer o rig, nao
        // um controle.
        int AddAllControlTracks(int bindingIndex);

        void DrawBindingNode(int bindingIndex);
        void DrawTrackGroupNode(int bindingIndex, TrackGroup g);
        void DrawTrackNode(int bindingIndex, int trackIndex);
        void DrawSectionNode(int bindingIndex, int trackIndex, int sectionIndex);
        void DrawChannelNode(int bindingIndex, int trackIndex, int sectionIndex, int channelIndex);
        void DrawSelectedKeyPanel();

        void AddKeyAtPlayhead(int bindingIndex, int trackIndex, int sectionIndex,
            int channelIndex);

        // Binding SEMPRE nasce ligado a uma entidade. Sem entidade nao ha o
        // que animar, e binding decorativo e pior que binding nenhum.
        int  CreateBindingForEntity(entt::entity entity);

        // Track nasce com os canais VAZIOS, de proposito. Um canal com key de
        // valor 0 no frame 0 zera a translacao local do osso no instante em
        // que a track e criada — o esqueleto se desmonta sozinho. A primeira
        // key vem do Capture, com o valor que o osso ja tem.
        int  CreateTransformTrack(int bindingIndex, const std::string& boneName);

        // Track de ANIMACAO EXISTENTE: uma section cobrindo o clipe inteiro.
        //
        // Diferente da track de osso, esta nasce com a section pronta — uma
        // track de clipe sem section nao tem estado intermediario util, e o
        // comprimento nao e escolha do usuario, e o do proprio clipe.
        int  CreateClipTrack(int bindingIndex, const std::string& clipName);

        // ── EXPLODIR: clipe -> uma track de osso por osso animado ────────────
        //
        // Converte a section de clipe em curvas EDITAVEIS. Cada BoneChannel do
        // AnimationClip vira uma SequencerTrack de TransformBone com keys reais
        // nos mesmos tempos do clipe — e a partir daqui a timeline mostra os
        // keyframes da animacao, que e o que "alterar a animacao" quer dizer.
        //
        // NAO gera 53 x 9 canais: um canal so nasce se o valor VARIA ao longo do
        // clipe ou se difere da bind pose. Escala constante 1 e translacao igual
        // a de repouso (o caso de quase todo osso num clipe da Mixamo) somem, e
        // sobram as curvas que realmente descrevem a performance.
        //
        // A track de clipe original nao e apagada — fica MUTADA ao lado, para
        // comparar o original com o que foi editado (e para desfazer sem
        // reimportar).
        //
        // Devolve quantas tracks foram criadas (0 = nada a explodir).
        int  ExplodeClipTrack(int bindingIndex, int trackIndex);

        // ── SOCKETS ──────────────────────────────────────────────────────────
        //
        // Um socket ja e parte do esqueleto no AXE (`SkeletalMeshAsset::Socket`,
        // persistido no `.axeskel`, criado no Animation Editor). O que o
        // Sequencer acrescenta sao duas coisas:
        //
        //   1. Uma track por socket, com curvas de offset. As keys NAO mexem no
        //      `.axeskel` — elas dirigem o transform LOCAL do objeto anexado,
        //      que o `Scene::GetWorldTransform` ja multiplica por cima da matriz
        //      do socket. O socket continua sendo do esqueleto; o que se anima e
        //      a peca presa nele.
        //
        //   2. QUAL objeto esta preso, por sequence e nao por esqueleto. Trocar
        //      pistola por fuzil e trocar o UUID da track — o socket, o rig e
        //      todas as keys ficam de pe. Era exatamente o pedido: "se eu
        //      resolver mudar de objeto, posso usar o mesmo socket".
        int  CreateSocketTrack(int bindingIndex, const std::string& socketName);

        const SkeletalMeshAsset::Socket* FindBindingSocket(int bindingIndex,
            const std::string& socketName) const;

        // Cria/atualiza/destroi as entidades de preview dos objetos anexados.
        //
        // NAO desenha nada por conta propria: monta uma entidade normal com
        // MeshComponent + SocketAttachmentComponent e deixa o caminho que ja
        // existe (AnimationWorld -> _SocketWorld -> Scene::GetWorldTransform)
        // fazer o resto. As entidades levam EditorTransientComponent e por isso
        // nao entram no `.axescene`.
        void SyncSocketPreviews();
        void ClearSocketPreviews();

        // ── CONTROL RIG ──────────────────────────────────────────────────────
        //
        // O rig vem para o Sequencer pelo caminho que o proprio engine ja
        // desenhou: `SequencerBinding::RigAssetUUID` (campo que existe desde a
        // v1 do data model) aponta para um `.axerig`, e a janela mantem uma
        // COPIA DE TRABALHO por binding — hierarquia, grafo e funcoes — igual ao
        // que o `AnimNode_ControlRig` faz por instancia.
        //
        // Copia, e nao o asset: o comentario em `RigElement::Value` ja avisava
        // que o Sequencer teria de escrever no clone de runtime, senao a pose
        // que o animador der aqui vira a pose PADRAO do asset e aparece no jogo,
        // para todos os personagens, ao vivo.
        //
        // As tracks de controle animam `RigElement::Value` — o transform local
        // ao repouso do controle, identidade = neutro. Nao e o `Initial`: aquele
        // e o repouso autorado ao montar o rig.
        struct RigRuntime
        {
            std::shared_ptr<ControlRigAsset> Asset;
            RigHierarchy Hierarchy;
            RigGraph     Graph;
            std::vector<std::pair<std::string, RigGraph>> Functions;

            std::string  SourceUUID;      // de qual .axerig esta copia nasceu
            uint32_t     ClonedVersion = 0;
            bool         Ready = false;
        };

        // Chaveado pelo INDICE do binding, e revalidado contra o RigAssetUUID a
        // cada uso: remover um binding faz os indices deslizarem, e uma copia
        // presa ao indice antigo passaria a dirigir o personagem errado.
        std::unordered_map<int, RigRuntime> m_Rigs;

        RigRuntime* EnsureRig(int bindingIndex);
        void        ClearRigs();

        // ── DIAGNOSTICO DO SOLVE ─────────────────────────────────────────────
        //
        // Mede quanto CADA OSSO mudou entre a pose que entrou no rig e a que
        // saiu dele.
        //
        // Existe por um motivo especifico: "o personagem fica de lado com o rig
        // ligado" e uma frase sobre o resultado, e o resultado passa por um
        // grafo inteiro. Adivinhar qual no e o culpado, de fora, e chute.
        //
        // Com os controles seguindo a animacao e sem nada posado, o rig deveria
        // ser TRANSPARENTE: entra pose, sai a mesma pose. Todo osso que aparecer
        // nesta lista e um osso que o grafo esta mexendo sem que ninguem tenha
        // pedido — e o nome dele diz de que no se trata.
        //
        // O caso classico: `RigNode_FKChain` copia o transform LOCAL do controle
        // para o osso, um par por indice. Duas listas fora de ordem (o Neck no
        // lugar do LeftShoulder, por exemplo) escrevem o local de um no outro, e
        // o membro sai girado sem nenhum erro em lugar nenhum.
        struct RigDeviation
        {
            std::string Bone;
            float       Degrees = 0.0f;
            float       Distance = 0.0f;
        };

        bool m_RigDiagnostics = false;
        std::vector<RigDeviation> m_RigDeviations;
        int  m_RigPosedControls = 0;   // quantos controles estao com Value != neutro

        // ── AUDITORIA ESTATICA DO GRAFO ──────────────────────────────────────
        //
        // "Este osso mudou" diz o sintoma; nao diz o culpado. Esta auditoria le
        // o grafo SEM roda-lo e aponta o no.
        //
        // Duas classes de problema, as duas invisiveis em execucao:
        //
        //  1. FK Chain com as duas ItemArray desalinhadas. O no anda as listas
        //     POR INDICE e copia o LOCAL do controle para o osso. Um par trocado
        //     escreve o local do pescoco no ombro — o braco sai girado e nao ha
        //     erro em lugar nenhum. Comparar `Controls[i].SourceBone` com
        //     `Bones[i].Name` acha isso em um passe.
        //
        //  2. Two Bone IK com Root/Middle/Effector que nao formam uma cadeia
        //     pai->filho contigua. Root um elo acima do que deveria faz o solve
        //     girar o osso de cima (o quadril, por exemplo) — que e exatamente
        //     o tipo de coisa que se ve como "o personagem ficou de lado".
        //
        // Percorre o grafo principal E os grafos de funcao: num rig organizado,
        // o IK mora dentro de uma funcao chamada por um no Call.
        struct RigAuditIssue
        {
            std::string Where;     // "ForwardsSolve" ou o nome da funcao
            std::string Node;      // titulo do no
            std::string Message;
            bool        Severe = true;
        };

        std::vector<RigAuditIssue> m_RigAudit;
        void AuditRigGraph(int bindingIndex);

        // ── RASTREIO NO A NO ─────────────────────────────────────────────────
        //
        // A auditoria estatica so enxerga o que consegue LER: um pino alimentado
        // por fio, ou uma lista que vem de um Get Items em vez de um Item Array,
        // ela pula. Silencio ali nao quer dizer "esta tudo certo" — quer dizer
        // "nao consegui olhar".
        //
        // Este aqui nao depende de ler nada: liga um espiao em
        // RigExecContext::OnNodeExecuted, fotografa a hierarquia antes de cada
        // no e compara depois. O que sai e a frase que responde a pergunta de
        // verdade: "o no X girou o osso Y em Z graus".
        struct RigNodeEffect
        {
            std::string Node;
            std::string Bone;
            float       Degrees = 0.0f;
            float       Distance = 0.0f;
        };

        std::vector<RigNodeEffect> m_RigTrace;

        // Roda o Forwards Solve sobre a pose ja montada. Chamado DEPOIS das
        // tracks de osso: o rig modifica a animacao, nao substitui — e a mesma
        // ordem do AnimNode_ControlRig, que comeca por EvalInput.
        void SolveRig(int bindingIndex, const Skeleton& skel, Pose& pose);

        int  CreateControlTrack(int bindingIndex, const std::string& controlName);
        void DrawAddControlPopup(int bindingIndex);

        int  m_AddControlForBinding = -1;
        bool m_OpenAddControlPopup = false;

        // Clipe do binding por nome (mesma resolucao do AnimGraph).
        const AnimationClip* FindBindingClip(int bindingIndex,
            const std::string& clipName) const;

        void EnsurePlayerStarted();
        void StopPlayer();

        // --- Resolucao de alvo ---------------------------------------------
        //
        // O BINDING e o dono do alvo, nao a selecao do viewport.
        entt::entity ResolveBindingEntity(const SequencerBinding& b) const;

        // Esqueleto do binding (nullptr se a entidade sumiu ou nao tem
        // SkeletalMeshComponent valido). Opcionalmente devolve o componente.
        const Skeleton* GetBindingSkeleton(int bindingIndex,
            SkeletalMeshComponent** outSmc = nullptr) const;

        // --- Avaliacao ------------------------------------------------------
        //
        // Constroi a POSE (transforms locais) de cada binding e converte em
        // matrizes de skinning. Ver a nota longa na implementacao sobre por
        // que escrever direto no BonePalette nao funciona.
        void EvaluateAndApply();

        // Le o valor atual de um componente do osso na pose de trabalho do
        // binding — que e exatamente o que o usuario ve no viewport.
        bool CaptureBoneValue(int bindingIndex,
            const std::string& boneName,
            SequencerChannelComponent component,
            float& outValue) const;

        // Captura pelo TIPO da track. Osso le da pose de trabalho; socket le do
        // transform LOCAL do objeto anexado — que e o que a curva do socket
        // dirige. Sem esta bifurcacao, o Capture numa track de socket caia no
        // fallback zero e a primeira key de Scale encolhia a arma ate sumir.
        bool CaptureChannelValue(int bindingIndex,
            const SequencerTrack& track,
            SequencerChannelComponent component,
            float& outValue) const;

        // Devolve o PoseOverride das entidades ligadas, para o AnimationWorld
        // voltar a mandar na pose.
        void ReleasePoseOverride();

        // ── GIZMO ────────────────────────────────────────────────────────────
        //
        // Editar animacao digitando numero em canal e o que se faz para AJUSTE
        // FINO. Para pousar uma mao, o gesto e agarrar e arrastar — e sem isso o
        // Sequencer nao e uma ferramenta de animacao, e uma planilha.
        //
        // O gizmo nao e desenhado aqui: o viewport e quem tem camera, retangulo
        // e drawlist. Esta janela so PEDE um (ViewportRenderer::ExternalGizmo),
        // entregando a matriz de mundo do alvo e recebendo a manipulada.
        //
        // Reposto TODO FRAME, e limpo quando a janela fecha — um pedido que
        // sobrevivesse deixaria o viewport manipulando o osso de uma sequence
        // que nem esta mais aberta.
        void UpdateGizmo();

        // Matriz de MUNDO do alvo da track selecionada. false quando nao ha
        // alvo manipulavel (track de clipe, osso que sumiu, rig nao resolvido).
        bool GizmoTargetWorld(int bindingIndex, const SequencerTrack& track,
            glm::mat4& outWorld) const;

        // Converte a matriz manipulada de volta em valores de canal e CRAVA AS
        // KEYS no frame atual.
        //
        // Keyar e obrigatorio, nao opcional: a pose e recalculada das keys todo
        // frame, entao uma edicao que nao virasse key desapareceria no quadro
        // seguinte. E o mesmo motivo pelo qual o Sequencer da Unreal exige
        // auto-key ligado para o gizmo ter efeito.
        void ApplyGizmoToBone(int bindingIndex, int trackIndex, const glm::mat4& world);
        void ApplyGizmoToSocket(int bindingIndex, int trackIndex, const glm::mat4& world);
        void ApplyGizmoToControl(int bindingIndex, int trackIndex, const glm::mat4& world);

        // Section ativa no playhead, ou -1. Cria uma cobrindo o range se a
        // track ainda nao tem nenhuma.
        int  SectionAtPlayhead(int bindingIndex, int trackIndex);

        // Indice do canal, criando-o se preciso.
        int  EnsureChannel(int bindingIndex, int trackIndex, int sectionIndex,
            SequencerChannelComponent component);

        // Cria ou substitui a key deste canal no frame atual.
        void SetChannelKeyAtPlayhead(int bindingIndex, int trackIndex, int sectionIndex,
            SequencerChannelComponent component, float value);

        // Grava as tres rotacoes de uma vez, desembrulhando os angulos contra a
        // key anterior. Separado das outras porque Euler nao se escreve eixo a
        // eixo sem produzir saltos de 360 graus — ver ContinuousEuler.
        void SetRotationKeysAtPlayhead(int bindingIndex, int trackIndex, int sectionIndex,
            const glm::quat& rotation);

        bool m_GizmoEnabled = true;

        // ═══════════════════════════════════════════════════════════════════
        //  CONTROLES DO RIG NO VIEWPORT
        //
        //  ── O QUE FALTAVA ─────────────────────────────────────────────────
        //
        //  Para posar um controle era preciso ACHA-LO NA LISTA: abrir o grupo
        //  Controles no outliner e reconhecer "ctrl_LeftHand" entre vinte
        //  nomes. O personagem estava na tela, o controle estava desenhado no
        //  editor de rig, e mesmo assim o gesto natural — apontar e clicar —
        //  nao existia.
        //
        //  As formas sao as MESMAS do editor de rig (`ui::DrawRigControlGizmos`
        //  desenha as duas), entao o que se ve aqui e o que se autorou la:
        //  mesma cor, mesmo tamanho, mesma orientacao.
        //
        //  ── A TRACK NASCE NA MANIPULACAO, NAO NO CLIQUE ───────────────────
        //
        //  Selecionar um controle nao cria track. Uma track por clique encheria
        //  o outliner de linhas vazias so por olhar, e "apagar o que eu nao
        //  pedi" e trabalho que a ferramenta inventou.
        //
        //  Isso obriga o gizmo a saber mirar num controle que ainda NAO tem
        //  track — dai `m_ViewportControlName` existir ao lado de
        //  `m_SelectedTrack`. A track e criada no primeiro
        //  ApplyGizmoToControlByName, e a partir dai o caminho normal (por
        //  track) assume.
        // ═══════════════════════════════════════════════════════════════════
        // ═══════════════════════════════════════════════════════════════════
        //  SELECAO MULTIPLA DE ALVOS (ossos, controles, sockets)
        //
        //  ── PIVO INDIVIDUAL ───────────────────────────────────────────────
        //
        //  Todos os alvos recebem a MESMA transformacao LOCAL, cada um em torno
        //  da propria origem — o "Individual Origins" do Blender.
        //
        //  Para uma cadeia e a unica opcao que faz sentido: girar 5 graus em
        //  tres vertebras da 15 graus de curvatura acumulada, que e o gesto de
        //  arquear as costas. Um pivo COMUM giraria o spine inteiro como um
        //  bloco rigido, que e o oposto do que se quer de um FK.
        //
        //  ── POR QUE PRE-MULTIPLICA ────────────────────────────────────────
        //
        //  O delta do alvo ativo e `dR = R_agora * inverse(R_inicio)` — uma
        //  rotacao no referencial do PAI dele. Aplicada nos outros como
        //  `dR * R_inicio_i`, cada um gira o mesmo tanto em relacao ao proprio
        //  pai. Pos-multiplicar (`R_inicio_i * dR`) giraria no referencial do
        //  proprio corpo de cada um, e numa cadeia isso produz torcao em vez de
        //  dobra.
        //
        //  ── O ALVO E O NOME, NAO O INDICE ─────────────────────────────────
        //
        //  Mesma razao do KeyClip: um controle escolhido no viewport pode nem
        //  ter track ainda (a track so nasce na primeira manipulacao), e
        //  indices deslizam quando alguem remove uma track no meio.
        // ═══════════════════════════════════════════════════════════════════
        struct TargetRef
        {
            int                 Binding = -1;
            SequencerTargetType Type = SequencerTargetType::Control;
            std::string         Name;

            bool operator==(const TargetRef& o) const {
                return Binding == o.Binding && Type == o.Type && Name == o.Name;
            }
        };

        // O ULTIMO e o ATIVO: e nele que o gizmo aparece, e e o delta dele que
        // os outros seguem. Ultimo, e nao primeiro, porque o gesto natural e
        // "seleciono a cadeia e mexo no que cliquei por ultimo".
        std::vector<TargetRef> m_SelectedTargets;

        bool      IsTargetSelected(const TargetRef& t) const;
        void      SelectTarget(const TargetRef& t, bool additive);
        void      ClearTargetSelection();
        TargetRef ActiveTarget() const;

        // ── UMA FONTE DA VERDADE PARA "QUEM E O ATIVO" ───────────────────────
        //
        // Reescreve m_Selected{Binding,Track} e m_ViewportControl* a partir do
        // ULTIMO alvo do grupo.
        //
        // Existe porque um Ctrl+clique pode REMOVER o ativo: o grupo passa a
        // ter outro ultimo, mas os campos antigos continuariam apontando para o
        // que acabou de sair — e o gizmo ficaria manipulando um controle que o
        // outliner ja mostra como nao selecionado.
        void SyncActiveFromTargets();

        // Qual componente do gizmo esta em uso. Enum PROPRIO em vez de
        // ImGuizmo::OPERATION: este header nao inclui ImGuizmo de proposito
        // (ver a nota no topo do .cpp), e arrastar o renderer inteiro para
        // quem so quer o Sequencer seria pagar caro por tres valores.
        enum class GizmoChannel : std::uint8_t { Translate, Rotate, Scale };

        // Transform LOCAL de um alvo, na moeda em que os tres tipos coincidem:
        // local ao pai (osso), local ao repouso (controle), offset sobre o
        // socket. E o mesmo espaco em que as curvas gravam.
        struct TargetLocal
        {
            glm::vec3 T{ 0.0f };
            glm::quat R{ 1.0f, 0.0f, 0.0f, 0.0f };
            glm::vec3 S{ 1.0f };
        };

        bool m_GroupDragActive = false;

        // Paralelo a m_SelectedTargets, tirado no PRIMEIRO frame do arrasto.
        // Sem o instantaneo, cada frame mediria o delta contra o frame anterior
        // e os erros se acumulariam — o grupo iria escorregando.
        std::vector<TargetLocal> m_GroupStart;

        bool ReadTargetLocal(const TargetRef& t, TargetLocal& out) const;
        void WriteTargetLocal(const TargetRef& t, const TargetLocal& v, GizmoChannel op);

        void BeginGroupDrag();
        void EndGroupDrag() { m_GroupDragActive = false; m_GroupStart.clear(); }

        // Chamado pelos tres ApplyGizmoTo* com o local JA manipulado do alvo
        // ativo. Nao faz nada com menos de dois alvos.
        void ApplyGroupDelta(const TargetLocal& activeNow, GizmoChannel op);

        bool m_ShowViewportControls = true;

        // Controle escolhido no viewport que ainda nao tem track. Vazio quando
        // a selecao ja e por track — ver a nota de precedencia em UpdateGizmo.
        int         m_ViewportControlBinding = -1;
        std::string m_ViewportControlName;

        // Sob o mouse no frame anterior, por binding. So muda a espessura do
        // traco; o retorno do hit-test e recalculado do zero todo frame.
        std::unordered_map<int, int> m_ViewportHovered;

        // Repoe (ou limpa) o pedido de desenho no viewport. Todo frame, pela
        // mesma razao do gizmo: um pedido que sobrevivesse ao fechar da janela
        // desenharia controles de uma sequence que nem esta mais aberta.
        void UpdateViewportOverlay();

        // Chamado DE DENTRO da janela do Viewport. Devolve true se consumiu o
        // clique — ver ViewportRenderer::ExternalOverlay.
        bool DrawViewportControls(const glm::vec2& boundsMin, const glm::vec2& boundsMax);

        void SelectControlFromViewport(int bindingIndex, const std::string& controlName,
            bool additive);

        // Track de controle deste binding por nome, ou -1. NAO cria.
        int  FindControlTrack(int bindingIndex, const std::string& controlName) const;

        // Matriz de mundo de um controle pelo NOME — o mesmo calculo do ramo
        // Control de GizmoTargetWorld, mas sem exigir que a track exista.
        bool ControlWorld(int bindingIndex, const std::string& controlName,
            glm::mat4& outWorld) const;

        // Manipulacao de um controle escolhido no viewport. E AQUI que a track
        // nasce, quando ainda nao existe.
        void ApplyGizmoToControlByName(int bindingIndex, const std::string& controlName,
            const glm::mat4& world);

        // ═══════════════════════════════════════════════════════════════════
        //  REC — AUTO-KEY
        //
        //  ── O QUE ESTAVA ERRADO ─────────────────────────────────────────
        //
        //  Todo arrasto de gizmo cravava key, sempre, sem jeito de desligar.
        //  A nota antiga em ApplyGizmoToBone chamava isso de obrigatorio ("a
        //  pose e recalculada das keys todo frame, entao uma edicao que nao
        //  virasse key desapareceria no quadro seguinte"). O raciocinio esta
        //  certo; a conclusao e que faltava a OUTRA metade, nao que auto-key
        //  tinha de ser permanente.
        //
        //  Na pratica isso quer dizer que nao havia como olhar uma pose antes
        //  de aceita-la: girar o ombro para ver como fica ja sujava a curva, e
        //  desfazer era caçar a key no canal certo e apagar.
        //
        //  ── COMO FICA ───────────────────────────────────────────────────
        //
        //  REC LIGADO   → como era: qualquer mudanca vira key no playhead.
        //  REC DESLIGADO → a mudanca vai para m_PendingEdits, uma camada que
        //                  entra na avaliacao mas NAO existe no asset. O
        //                  personagem se move na tela, o disco nao muda, e o
        //                  botao Key (tecla K) e quem promove a pose a
        //                  keyframe — o mesmo gesto do "I" do Blender.
        //
        //  Default DESLIGADO: gravar sem ter pedido e o comportamento que
        //  surpreende, e a surpresa aqui custa a curva do animador.
        // ═══════════════════════════════════════════════════════════════════
        bool m_Recording = false;

        // Edicoes ainda nao keyadas.
        //
        // O tipo e SequencerSample de proposito: uma edicao pendente E um
        // sample — mesmo binding, mesmo alvo, mesmo componente, mesmo escalar.
        // A unica diferenca e a origem (o gizmo, e nao uma key). Reusar o tipo
        // e o que permite que os TRES consumidores de sample (pose de osso,
        // SolveRig, preview de socket) recebam a pose pendente sem nenhum
        // ramo novo — ver m_EffectiveSamples.
        std::vector<SequencerSample> m_PendingEdits;

        // Frame em que a pose pendente foi autorada. Mudar de frame descarta a
        // pendencia: uma pose e uma afirmacao SOBRE UM INSTANTE, e carrega-la
        // para o frame seguinte faria o Key gravar no lugar errado sem que o
        // usuario percebesse.
        float m_PendingFrame = -1.0f;

        // A lista que a avaliacao realmente le: samples do player + pendencias
        // por cima (a pendencia VENCE a key, senao posar em cima de um frame ja
        // keyado nao teria efeito nenhum).
        //
        // Reconstruida uma vez por frame, no comeco do EvaluateAndApply.
        std::vector<SequencerSample> m_EffectiveSamples;

        void RebuildEffectiveSamples();

        // Grava (ou empilha como pendencia) UM canal. Todo caminho de gizmo
        // passa por aqui — e o unico ponto em que o REC e consultado, o que
        // impede a bifurcacao de se espalhar por tres funcoes de Apply.
        void WriteChannelValue(int bindingIndex, int trackIndex, int sectionIndex,
            SequencerChannelComponent component, float value);

        // Idem para rotacao, que nao se escreve eixo a eixo — ver
        // SetRotationKeysAtPlayhead.
        void WriteRotation(int bindingIndex, int trackIndex, int sectionIndex,
            const glm::quat& rotation);

        // Euler continuo do alvo desta track no playhead, com a referencia
        // certa: a pendencia se houver, senao a key anterior. Extraido de
        // SetRotationKeysAtPlayhead para os dois caminhos usarem a MESMA
        // desembrulhagem — duas versoes divergiriam e a pose pendente saltaria
        // 360 graus no instante em que virasse key.
        glm::vec3 ResolveContinuousEuler(int bindingIndex, int trackIndex,
            int sectionIndex, const glm::quat& rotation) const;

        void SetPendingEdit(int bindingIndex, const SequencerTrack& track,
            SequencerChannelComponent component, float value);
        void ClearPendingEdits();

        // Promove tudo que esta pendente a keyframe no playhead. E o botao Key.
        int  CommitPendingEdits();

        // ── CAPTURA DO VIEWPORT ─────────────────────────────────────────────
        //
        // Crava key em TODOS os canais de todas as tracks de transform de um
        // binding (ou de todos, com bindingIndex < 0), com o valor que o alvo
        // tem AGORA na tela.
        //
        // E a resposta para "gravar o que estou vendo": o Capture por canal ja
        // existia, mas exigia clicar canal a canal — com um clipe explodido sao
        // dezenas de tracks e centenas de canais, e ninguem faz isso a mao.
        int  CaptureAllTracksAtPlayhead(int bindingIndex);

        // Popups de escolha (entidade / osso / clipe / socket).
        void DrawAddBindingPopup();
        void DrawAddTrackPopup(int bindingIndex);
        void DrawAddClipPopup(int bindingIndex);
        void DrawAddSocketPopup(int bindingIndex);

        // --- Estado de picker ----------------------------------------------
        bool m_OpenAddBindingPopup = false;
        int  m_AddTrackForBinding = -1;
        bool m_OpenAddTrackPopup = false;
        int  m_AddClipForBinding = -1;
        bool m_OpenAddClipPopup = false;
        char m_PickerFilter[64] = { 0 };

        // Explodir o clipe em tracks de osso assim que ele entra na timeline.
        // Ligado por default: quem poe um clipe no Sequencer quer editar aquela
        // animacao, nao acumular uma camada opaca por cima dela.
        bool m_ExplodeClipOnAdd = true;

        // Scroll vertical das lanes da timeline.
        //
        // A timeline e desenhada a mao com ImDrawList e nunca avanca o cursor do
        // ImGui, entao o child NAO gera scrollbar sozinho: as lanes que nao
        // coubessem eram simplesmente cortadas. Com uma track por osso isso
        // deixou de ser detalhe — um clipe explodido produz dezenas de lanes.
        float m_TimelineScrollY = 0.0f;

        // ── Zoom e pan horizontal ────────────────────────────────────────────
        //
        // A timeline sempre esticou o range inteiro na largura disponivel. Com
        // 125 frames num painel de 700px sao 5,6 px por frame: duas keys em
        // frames vizinhos viram um borrao de losangos sobrepostos, e nao ha
        // como pegar uma sem pegar a outra.
        //
        // m_Zoom multiplica o frameWidth; m_TimelineScrollX e o primeiro frame
        // VISIVEL (em frames, nao em pixels — assim o pan sobrevive a um resize
        // da janela sem escorregar).
        float m_Zoom = 1.0f;
        float m_TimelineScrollX = 0.0f;
        bool  m_PanningTimeline = false;

        // ── Socket picker ────────────────────────────────────────────────────
        int  m_AddSocketForBinding = -1;
        bool m_OpenAddSocketPopup = false;
        char m_NewSocketName[64] = { 0 };
        int  m_NewSocketBone = -1;

        // Entidade de preview de cada objeto anexado, por "<binding>|<socket>".
        struct SocketPreview {
            entt::entity Entity = entt::null;
            std::string  AssetUUID;   // o que ja esta carregado nela
        };
        std::unordered_map<std::string, SocketPreview> m_SocketPreviews;

        // ── SELECAO MULTIPLA DE KEYS ─────────────────────────────────────────
        //
        // Os m_Selected* acima continuam sendo a key PRIMARIA (a que o painel
        // de edicao mostra). Este conjunto e o que o drag move e o Delete apaga.
        //
        // Endereco completo, e nao ponteiro: qualquer AddKey/RemoveKey realoca
        // os vetores do asset, e um ponteiro guardado entre frames apontaria
        // para memoria liberada. Cinco ints sao baratos e sempre validos.
        struct KeyRef {
            int Binding = -1, Track = -1, Section = -1, Channel = -1, Key = -1;

            bool operator==(const KeyRef& o) const {
                return Binding == o.Binding && Track == o.Track &&
                    Section == o.Section && Channel == o.Channel && Key == o.Key;
            }
        };

        std::vector<KeyRef> m_SelectedKeys;

        // Frame original de cada key selecionada no instante em que o drag
        // comecou. Sem isto o grupo se comprime: cada key seria recalculada a
        // partir do frame ja movido, e as diferencas entre elas encolheriam a
        // cada quadro do arrasto.
        std::vector<float>  m_DragOriginalFrames;

        // ═══════════════════════════════════════════════════════════════════
        //  SELECAO EM CAIXA (rubber band)
        //
        //  Ate aqui a unica forma de pegar varias keys era Ctrl+clique, uma a
        //  uma. Num coice de arma com seis controles isso e trinta cliques para
        //  fazer uma coisa que e um gesto so.
        //
        //  ── POR QUE NAO PRECISOU DE UM ITEM NOVO DO ImGui ──────────────────
        //
        //  O `lanes_click` (o InvisibleButton do duplo-clique) ja cobre a area
        //  das lanes e ja e submetido DEPOIS das keys — e no ImGui o PRIMEIRO
        //  item a reivindicar o hover vence. Ou seja: ele so fica "hovered"
        //  quando NAO ha key sob o cursor, que e exatamente a condicao para
        //  comecar uma caixa. Reusa-lo evita mexer no allow-overlap, que muda
        //  de nome entre versoes do ImGui.
        //
        //  ── UM FRAME DE ATRASO, DE PROPOSITO ───────────────────────────────
        //
        //  As keys sao testadas contra a caixa DENTRO do laco que ja as desenha
        //  — nao ha um segundo passe. Como o laco roda antes da deteccao do
        //  clique, a caixa comeca a colher no frame seguinte ao clique. E
        //  imperceptivel, e o que se ganha e nao ter duas listas de posicoes de
        //  key que podem discordar.
        // ═══════════════════════════════════════════════════════════════════
        enum class BoxMode : std::uint8_t { Replace = 0, Add = 1, Remove = 2 };

        bool    m_BoxSelecting = false;
        ImVec2  m_BoxStartPos{ 0.0f, 0.0f };
        BoxMode m_BoxMode = BoxMode::Replace;

        // Colhido a cada frame pelo laco de desenho, consumido no soltar.
        std::vector<KeyRef> m_BoxHits;

        void ApplyBoxSelection();

        // Atalhos do menu de contexto. "Todas deste canal" e o que resolve o
        // caso comum de querer a curva inteira sem arrastar uma caixa por cima
        // de vinte lanes.
        void SelectAllKeysInChannel(int bindingIndex, int trackIndex,
            int sectionIndex, int channelIndex);
        void SelectAllKeysInTrack(int bindingIndex, int trackIndex);

        bool IsKeySelected(const KeyRef& k) const;
        void ToggleKeySelection(const KeyRef& k);
        void SetSingleKeySelection(const KeyRef& k);
        void SelectAllKeys();
        void ClearKeySelection();
        void DeleteSelectedKeys();
        void BeginKeyDrag();

        SequencerKey* ResolveKeyRef(const KeyRef& k);

        // Ordena um canal por Frame e CORRIGE os indices guardados na selecao.
        //
        // `SequencerChannel::SortKeys` sozinho nao serve aqui: ele reordena o
        // vetor e deixa todo KeyRef apontando para a key errada — depois de
        // arrastar um grupo por cima de outro, a selecao passaria a conter keys
        // que o usuario nunca tocou. Esta versao ordena por permutacao e traduz
        // os indices pela mesma permutacao, entao a selecao sobrevive exata.
        void SortChannelAndRemap(int bindingIndex, int trackIndex,
            int sectionIndex, int channelIndex);

        // Pose de trabalho por binding (mesmo indice do binding). Vive aqui, e
        // nao no componente, porque e estado de AUTORIA: some quando a janela
        // fecha, como a preview do Control Rig.
        std::vector<Pose> m_WorkPoses;

        // Quantos bindings o ultimo EvaluateAndApply conseguiu resolver e
        // escrever. Existe para a barra de status: "nao tem efeito" e uma
        // frase; "0 de 1 binding resolvido" e um diagnostico.
        int m_AppliedBindings = 0;

        // --- Deteccao de sobrescrita da pose --------------------------------
        //
        // Assinatura da BonePalette de cada binding no instante em que a
        // deixamos, mais o resultado da comparacao no frame seguinte.
        //
        // Existe porque ha um modo de falha em que TODO o resto reporta saude:
        // a janela resolve o binding, produz samples, escreve a palette — e
        // outro sistema (o AnimationWorld sem o guard `PoseOverride`) a
        // reescreve antes do render. Sem esta checagem, o sintoma e so "nao
        // acontece nada", que e indistinguivel de dez outras causas.
        std::vector<double> m_PaletteStamp;
        bool m_PaletteStampValid = false;
        bool m_PaletteStolen = false;
    };

} // namespace axe