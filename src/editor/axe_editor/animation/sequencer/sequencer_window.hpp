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

#include <imgui.h>
// ImRect e SeparatorEx vivem em imgui_internal.h (mesmo padrao do AXE).
#include <imgui/imgui_internal.h>
#include <entt/entt.hpp>

#include <cstdint>
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

        SequencerAsset& GetAsset() { return m_Asset; }
        SequencerPlayer& GetPlayer() { return m_Player; }

    private:
        bool           m_IsOpen = false;
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

        // Toolbar state.
        std::string m_LastLoadedPath;

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