#pragma once

// src/axe/animation/sequencer/sequencer_track.hpp
//
// Data model completo do Sequencer: Binding -> Track -> Section -> Channel -> Key.
//
// CONFIRMADO contra o repo AXE (clonado):
//   - Namespace e axe (sem axe::editor — ver editor_layer.hpp, anim_clip_window.hpp)
//   - UUID e std::string puro (ver components.hpp: AssetUUID, GraphAssetUUID,
//     MaterialAssetUUID sao todos std::string). NAO existe axe/core/uuid.hpp.
//   - AXE_API definido em axe/core/types.hpp (ja confirmado).
//   - nlohmann/json em <nlohmann/json.hpp> (premade: IncludeDir["nlohmann"] = "src/vendor")
//
// PORQUE ItemArray DO RIGNODE NAO SE APLICA AQUI:
//   O RigNode tem ItemArray para operar sobre cadeias. No Sequencer isso nao
//   funciona: a timeline precisa enderecar cada bone explicitamente. (§5.4.)
//
// PORQUE TransformSocket E TIPO SEPARADO (v1.1):
//   Sockets nao sao bones. Tipo separado = ReadItem devolve -1 limpo. (§5.5.)
//
// INVARIANTES:
//   - Enums sao uint8_t para payload compacto no .axeseqbin (futuro).
//   - Structs sao POD-like: seriazao direto via nlohmann.
//   - NAO inclui imgui.h — vive em src/axe/.
//   - AXE_API porque SequencerAsset/Player cruzam a DLL.

#include "axe/core/types.hpp"  // AXE_API

#include <cstdint>
#include <string>
#include <vector>

namespace axe {

    // ============================================================
    // Enums de tipos (estaveis — vao para o .axeseq como strings)
    // ============================================================

    enum class SequencerTrackType : uint8_t {
        TransformBone = 0,
        TransformControl = 1,
        TransformSocket = 2,    // v1.1
        AnimationClip = 3,
        Property = 4,
        Event = 5,

        // ── O TRANSFORM DA PROPRIA ENTIDADE ──────────────────────────────────
        //
        // Anima o `TransformComponent` da entidade do binding — nao um osso
        // dela.
        //
        // ── POR QUE "ENTIDADE" E NAO "CAMERA" ────────────────────────────────
        //
        // O pedido era uma camera de cutscene. Mas uma camera, no AXE, e uma
        // entidade com CameraComponent: nao ha tipo proprio, e nao deveria
        // haver um aqui.
        //
        // Um `TransformCamera` obrigaria a inventar um segundo caminho no dia
        // em que alguem quisesse animar uma porta, um elevador, o foco de uma
        // luz ou a arma na mao — todos "mover uma entidade no tempo", que e
        // exatamente isto. A camera passa a ser o primeiro USO, e nao um caso
        // especial.
        //
        // Acrescentado DEPOIS do Event, e nunca no meio: o valor numerico vai
        // para o `.axeseqbin` futuro.
        TransformEntity = 6,

        // ── DE QUAL CAMERA A CENA E VISTA ────────────────────────────────────
        //
        // `TargetName` e o nome da ENTIDADE de camera. Cada key diz "deste
        // frame em diante, esta camera" — semantica de degrau, como uma key
        // Step: o corte vale ate a proxima key, de qualquer track de corte.
        //
        // ── POR QUE NAO E UMA TRACK DE EVENT ─────────────────────────────────
        //
        // Era o encaixe obvio, e esta errado por um motivo so, mas decisivo:
        // evento e um instante CRUZADO, e por isso nao dispara em scrub (ver
        // SequencerEventSample). Um corte de camera nao e um instante — e um
        // ESTADO. A pergunta que ele responde e "de qual camera se ve o frame
        // 47?", e ela tem resposta mesmo que o playhead tenha sido arrastado
        // para la, ou aberto direto ali.
        //
        // Com evento, o Play mostraria os cortes e o scrub do editor nao —
        // o animador enquadraria um plano olhando pela camera errada.
        //
        // ── POR QUE O NOME VAI NA TRACK, E UMA TRACK POR CAMERA ──────────────
        //
        // Uma key nao tem campo de texto, e nao vai ganhar um: acrescentar
        // string em SequencerKey engordaria TODA key do arquivo por causa de
        // um caso. Com o nome na track, "corta para a CamA" e uma key numa
        // linha chamada CamA — e a timeline passa a mostrar uma faixa por
        // camera, que e exatamente como um NLE desenha isso.
        CameraCut = 7
    };

    enum class SequencerTargetType : uint8_t {
        Bone = 0,
        Control = 1,
        Null = 2,
        Socket = 3,  // v1.1

        // A propria entidade do binding. Ver TransformEntity acima.
        Entity = 4
    };

    enum class SequencerChannelComponent : uint8_t {
        X = 0, Y = 1, Z = 2,
        RotX = 3, RotY = 4, RotZ = 5,
        ScaleX = 6, ScaleY = 7, ScaleZ = 8
    };

    // ── COMO O VALOR CAMINHA ENTRE DUAS KEYS ─────────────────────────────────
    //
    // A curva de um TRECHO vem da key da ESQUERDA (a que comeca o trecho). E a
    // convencao de todo NLE, e vale a pena dizer em voz alta porque ela produz
    // uma armadilha real: por o Bezier na key de CHEGADA nao muda o trecho que
    // chega nela. Ver a nota no painel de key.
    //
    // ── POR QUE OS QUATRO NOVOS EXISTEM ──────────────────────────────────────
    //
    // Step/Linear/Cubic* cobrem movimento CONTINUO — um corpo que se desloca. O
    // que faltava era o vocabulario de IMPACTO: coice de arma, batida, tranco.
    //
    // Um impacto tem duas metades e nenhuma delas e simetrica:
    //
    //   1. a batida, que quase nao tem duracao. `EaseOutStrong` chega em ~80%
    //      do valor no primeiro quinto do trecho e depois so assenta — e o que
    //      transforma uma rampa suave num golpe sem precisar de key extra.
    //
    //   2. a volta, que PASSA DO PONTO e retorna. `EaseOutBack` faz exatamente
    //      isso. Sem ultrapassagem, a arma "desliza" de volta ao repouso, e e
    //      esse deslizar que faz o coice parecer de brinquedo.
    //
    // `CubicEaseIn`/`CubicEaseOut` sao, na verdade, QUADRATICAS (t*t). Os nomes
    // ficam como estao — sao o que vai gravado no `.axeseq` desde a v1 — mas as
    // versoes fortes existem porque uma quadratica e fraca demais para impacto.
    enum class SequencerInterp : uint8_t {
        Step = 0,
        Linear = 1,
        CubicEaseIn = 2,
        CubicEaseOut = 3,
        Bezier = 4,   // tangents explicitos (TangentIn/TangentOut)

        // Acrescentados DEPOIS do Bezier, e nunca no meio: o valor numerico vai
        // para o `.axeseqbin` futuro, e renumerar trocaria a curva de todo
        // arquivo ja gravado.
        EaseInStrong = 5,   // t^4        — segura, e dispara no fim
        EaseOutStrong = 6,  // 1-(1-t)^4  — dispara, e assenta
        EaseInOut = 7,      // smootherstep — parte e chega parado
        EaseOutBack = 8,    // ultrapassa e volta: o coice
        EaseOutBounce = 9   // quica ate assentar
    };

    // ============================================================
    // Helpers de conversao para JSON (string <-> enum)
    // ============================================================
    //
    // Usados pelo SequencerAsset::LoadFromFile/SaveToFile. Manter estaveis
    // — sao o que vai no campo "type"/"interp"/"component" do .axeseq.

    AXE_API const char* SequencerTrackTypeToString(SequencerTrackType t);
    AXE_API SequencerTrackType SequencerTrackTypeFromString(const std::string& s);

    AXE_API const char* SequencerTargetTypeToString(SequencerTargetType t);
    AXE_API SequencerTargetType SequencerTargetTypeFromString(const std::string& s);

    AXE_API const char* SequencerChannelComponentToString(SequencerChannelComponent c);
    AXE_API SequencerChannelComponent SequencerChannelComponentFromString(const std::string& s);

    AXE_API const char* SequencerInterpToString(SequencerInterp i);
    AXE_API SequencerInterp SequencerInterpFromString(const std::string& s);

    // ============================================================
    // Structs do data model
    // ============================================================

    struct AXE_API SequencerKey {
        float              Frame = 0.0f;
        float              Value = 0.0f;
        SequencerInterp    Interp = SequencerInterp::Linear;
        float              TangentIn = 0.0f;   // so Bezier
        float              TangentOut = 0.0f;   // so Bezier

        // ── O PESO HORIZONTAL DA TANGENTE ────────────────────────────────────
        //
        // Fracao do TRECHO que a alca ocupa no eixo do tempo. Junto com
        // TangentIn/Out (que sao o eixo do valor), formam a alca 2D que o
        // editor de curvas desenha e arrasta.
        //
        // ── POR QUE 1/3, E POR QUE ISSO NAO MUDA NENHUM ARQUIVO ──────────────
        //
        // A implementacao antiga do Bezier era, na aparencia, "1D": interpolava
        // o valor por um cubico com t uniforme. Mas ela E um bezier 2D com as
        // alcas fixas em 1/3 e 2/3 — a conta fecha exatamente:
        //
        //     x(u) = 3(1-u)^2*u*(1/3) + 3(1-u)*u^2*(2/3) + u^3
        //          = u*[(1-u) + u]^2 = u
        //
        // Ou seja: com peso 1/3 dos dois lados, x(u) = u e a formula parametrica
        // colapsa na antiga, termo a termo. Toda key ja gravada avalia
        // IDENTICA — nao ha caminho duplo nem migracao.
        //
        // O que muda e que agora a alca pode sair de 1/3, e ai x(u) != u e o
        // avaliador resolve u a partir de x. E o que permite "segura e dispara"
        // com duas keys so.
        //
        // Clampado em [0.01, 0.99] no avaliador: sao os limites que garantem
        // x(u) monotonico (a mesma restricao da cubic-bezier do CSS). Fora
        // deles a curva dobraria no tempo — dois valores para o mesmo frame.
        float              TangentInWeight = 1.0f / 3.0f;
        float              TangentOutWeight = 1.0f / 3.0f;

        // ── QUANTO PASSA DO PONTO (EaseOutBack / EaseOutBounce) ──────────────
        //
        // Campo PROPRIO, e nao um TangentOut reaproveitado. Reaproveitar teria
        // sido barato e e exatamente o erro que o `RigElement::Initial` ja
        // custou caro neste projeto: um campo com dois significados vira dois
        // bugs que parecem um so.
        //
        // 0 = a constante classica (1.70158, ~10% de ultrapassagem). Valores
        // maiores exageram o coice; e o unico numero que se mexe para tunar
        // "quanto a arma pula".
        float              Overshoot = 0.0f;

        // Comparacao para estabilidade do sort (keys sao mantidas ordenadas
        // por Frame dentro de cada Channel — SortKeys() faz isso no Sample()).
        bool operator<(const SequencerKey& other) const {
            return Frame < other.Frame;
        }
    };

    struct AXE_API SequencerChannel {
        SequencerChannelComponent      Component = SequencerChannelComponent::X;
        std::vector<SequencerKey>      Keys;

        // Ordena keys por Frame. Chamado apos add/move/delete.
        void SortKeys();

        // Encontra as duas keys que cercam o frame atual para interpolacao.
        // Retorna false se nao ha keys suficientes (usa Default no caller).
        bool FindBracketingKeys(float frame, const SequencerKey*& outLeft,
            const SequencerKey*& outRight) const;
    };

    struct AXE_API SequencerSection {
        int                             StartFrame = 0;
        int                             EndFrame = 0;
        // UUID e std::string no AXE (ver components.hpp — AssetUUID etc.)
        std::string                     SourceClipUUID;       // reservado (ver abaixo)

        // ── QUAL CLIPE ESTA SECTION TOCA ─────────────────────────────────────
        //
        // NOME, e nao UUID, porque no AXE um AnimationClip NAO e um asset: nao
        // existe AssetType::AnimationClip nem extensao propria. Um clipe vive
        // dentro do `.axeskel` (uma AnimEntry apontando para o FBX) e chega na
        // cena como item de `SkeletalMeshComponent::Clips`.
        //
        // As duas alternativas eram piores:
        //   - INDICE em Clips: e o que a cena grava hoje (`"clip": 3`), e e
        //     fragil — a ordem depende de m_Animations e do makeUnique de nomes
        //     no Resolve(). Importar uma animacao nova renumera tudo.
        //   - UUID do .axeskel + indice: mesma fragilidade, mais indirecao.
        //
        // Nome e o precedente do proprio engine: o AnimGraph referencia clipe por
        // `AnimNode_ClipPlayer::ClipName`, resolvido varrendo GetClips() por
        // GetName(). SourceClipUUID fica reservado para quando clipe virar asset
        // de verdade — que e exatamente o que o bake vai exigir.
        std::string                     SourceClipName;

        int                             ClipOffset = 0;    // offset dentro do clip

        // ── RATE SCALE CONGELADO NO MOMENTO EM QUE A SECTION NASCEU ──────────
        //
        // O Player vive em src/axe/ e NAO conhece AnimationClip: tudo que ele
        // sabe fazer e converter frame -> segundo. O CreateClipTrack, do outro
        // lado, calcula o comprimento da section dividindo a duracao pelo
        // RateScale do clipe. Sem guardar o mesmo numero aqui, o mapeamento
        // inverso (frame -> segundo do clipe) nao fecha com o direto, e a
        // performance termina antes ou depois da borda da section.
        //
        // 1.0 e o default e o valor de praticamente todo clipe importado, entao
        // `.axeseq` gravado antes deste campo carrega sem migracao nenhuma.
        float                           ClipRateScale = 1.0f;

        std::vector<SequencerChannel>   Channels;

        bool ContainsFrame(float frame) const {
            return frame >= static_cast<float>(StartFrame) &&
                frame <= static_cast<float>(EndFrame);
        }
    };

    struct AXE_API SequencerTrack {
        SequencerTrackType              Type = SequencerTrackType::TransformBone;
        std::string                     TargetName;            // bone/control/socket name
        SequencerTargetType             TargetType = SequencerTargetType::Bone;
        std::vector<SequencerSection>   Sections;
        bool                            Muted = false;
        bool                            Locked = false;

        // ── FORA DAS SECTIONS: SEGURAR A POSE OU LARGAR? (tracks de clipe) ───
        //
        // true  — o playhead fora de toda section avalia a BORDA mais proxima:
        //         antes da primeira, o primeiro frame; depois da ultima, o
        //         ultimo. A performance congela em vez de sumir.
        // false — a track nao produz nada, e a base volta a ser a bind pose.
        //
        // Default true porque o contrario e um susto: uma sequence de 125 frames
        // com um clipe de 35 mostrava o personagem abrindo os bracos em T-pose no
        // frame 36 e ficando assim ate o fim. E o "Keep State" do Sequencer da
        // Unreal, que tambem e o default de la.
        //
        // So tem efeito em SequencerTrackType::AnimationClip — tracks de canal
        // ja sabem se virar (uma key fora da section simplesmente nao existe).
        bool                            HoldOutsideSections = true;

        // ── O QUE ESTA PRESO NESTE SOCKET (tracks de TransformSocket) ────────
        //
        // UUID de um asset de Mesh ou SkeletalMesh. Vazio = a track anima o
        // socket mas nada visivel esta preso nele.
        //
        // ── POR QUE MORA NA TRACK, E NAO NO SOCKET DO .axeskel ───────────────
        //
        // O `SkeletalMeshAsset::Socket` ja tem um `PreviewMeshUUID`, e seria
        // tentador reusa-lo. Mas aquele campo e do ESQUELETO: mudar a arma ali
        // muda em toda cena, toda sequence e todo personagem que use o mesmo
        // `.axeskel`.
        //
        // O que o animador quer e o oposto — o socket "RightHandWeapon" e
        // estavel, e o que troca e a peca: pistola numa sequence, fuzil na
        // outra, nada num close de rosto. Guardando na track, trocar o objeto e
        // trocar um UUID; o socket, as keys e o rig ficam todos de pe.
        std::string                     AttachedAssetUUID;

        // Helper: acha a section ativa num frame dado. Null se nenhuma.
        SequencerSection* FindActiveSection(float frame) {
            for (auto& sec : Sections)
                if (sec.ContainsFrame(frame)) return &sec;
            return nullptr;
        }
    };

    struct AXE_API SequencerBinding {
        // ── A QUEM ESTE BINDING PERTENCE ─────────────────────────────────────
        //
        // Um binding SEM entidade e decoracao: a timeline mostra tracks que nao
        // dirigem nada. Era o estado ate aqui — todo hook resolvia o alvo por
        // "entidade selecionada no viewport", entao o binding no outliner e o
        // esqueleto animado podiam ser dois objetos diferentes, e adicionar um
        // esqueleto so funcionava selecionando no viewport.
        //
        // EntityName e o handle PERSISTENTE, e nao e escolha estetica: a cena do
        // AXE nao tem UUID de entidade. O `id` gravado no `.axescene` e o
        // `entt::entity` cru, que NAO sobrevive ao load — o SceneSerializer
        // recria as entidades e traduz as referencias por um idMap de sessao.
        // Nome e o unico identificador que atravessa save/load hoje.
        //
        // Consequencia honesta: renomear a entidade quebra o binding, e dois
        // objetos com o mesmo nome sao ambiguos. Quando a cena ganhar UUID de
        // entidade, EntityUUID vira a fonte da verdade e o nome vira fallback —
        // por isso o campo ja existe aqui.
        std::string                     EntityName;
        std::string                     EntityUUID;         // reservado (ver acima)
        std::string                     DisplayName;
        std::string                     RigAssetUUID;       // opcional

        // ── OS CONTROLES SEGUEM A ANIMACAO? ──────────────────────────────────
        //
        // true  — antes do solve, todo controle que nasceu de um osso vai para
        //         onde esse osso esta NA ANIMACAO. O Value do animador continua
        //         valendo por cima.
        // false — os controles ficam no repouso do rig (a bind pose), que e o
        //         comportamento do preview do Control Rig.
        //
        // Default true, e a diferenca nao e sutil: com false, subir o peso de um
        // FK Chain faz o membro SALTAR para a T-pose, e um Two Bone IK mira no
        // controle de pe parado na posicao de bind — a animacao de tiro vira um
        // espacate. Os dois sao o grafo funcionando corretamente sobre controles
        // que estao no lugar errado.
        //
        // Fica no BINDING porque e uma decisao por personagem: um prop rig, cujo
        // controle nao representa osso nenhum, nao quer isto (e para ele o campo
        // e inofensivo — controle sem SourceBone nao se move).
        bool                            RigControlsFollowAnimation = true;

        std::vector<SequencerTrack>     Tracks;
    };

    // ============================================================
    // Asset (cabecalho do .axeseq)
    // ============================================================

    struct AXE_API SequencerFrameRange {
        int Start = 0;
        int End = 90;
    };

    // ============================================================
    // Tabela de samples (saida do Player::Sample)
    // ============================================================
    //
    // O SequencerPlayer::Sample() produz uma lista destes por frame. O editor
    // (ou SceneRuntime) aplica no BonePalette / RigHierarchy.

    struct AXE_API SequencerSample {
        int                 BindingIndex = -1;
        std::string         TargetName;
        SequencerTargetType TargetType = SequencerTargetType::Bone;
        SequencerChannelComponent Component = SequencerChannelComponent::X;
        float               Value = 0.0f;   // valor escalar sampleado
    };

    // Saida das tracks de AnimationClip — a CAMADA BASE da avaliacao.
    //
    // Nao cabe num SequencerSample: aquele carrega um escalar por canal, e um
    // clipe produz a pose inteira. Sao duas saidas porque sao duas camadas, e a
    // ordem entre elas e o comportamento inteiro do sistema:
    //
    //     bind pose  ->  clipe (base)  ->  tracks de osso (override)
    //
    // E o mesmo empilhamento do Sequencer do Unreal: a section de animacao da a
    // performance, e o que voce keya por cima vence localmente. Sem isso, colocar
    // um clipe e keyar um osso seriam operacoes mutuamente exclusivas.
    //
    // O tempo ja vem em SEGUNDOS: quem converte frame->segundo e o Player, unico
    // que conhece o fps. AnimationClip trabalha em segundos do inicio ao fim (o
    // ticks-per-second morre dentro do importador de FBX).
    struct AXE_API SequencerClipSample {
        int         BindingIndex = -1;
        std::string ClipName;
        float       TimeSeconds = 0.0f;
    };

    // ── SAIDA DAS TRACKS DE EVENT ────────────────────────────────────────────
    //
    // Um evento nao e um VALOR amostrado, e um INSTANTE cruzado. A diferenca
    // decide o formato: `SequencerSample` responde "quanto vale este canal
    // agora?", e a resposta existe em todo frame. Aqui a pergunta e "o playhead
    // passou por cima desta key desde a ultima vez?", e a resposta e quase
    // sempre "nao".
    //
    // POR QUE ISSO NAO PODE SAIR DO Resample():
    //   `Resample()` roda tambem em scrub, e roda varias vezes no mesmo frame
    //   (o editor chama depois de cada Scrub). Disparar evento dali faria o
    //   animador arrastar o playhead para tras e para frente e ouvir o tiro
    //   trinta vezes. Evento so nasce quando o TEMPO ANDA — em OnUpdate, e so
    //   no modo Playing.
    struct AXE_API SequencerEventSample {
        int         BindingIndex = -1;
        std::string EventName;
        float       Value = 0.0f;
        float       Frame = 0.0f;
    };

} // namespace axe