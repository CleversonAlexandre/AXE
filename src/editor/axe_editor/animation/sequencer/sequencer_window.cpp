// src/editor/axe_editor/animation/sequencer/sequencer_window.cpp
//
// Implementacao da SequencerWindow. Fase 2 (MVP) com UX melhorada:
//   - Timeline ocupa largura total (frameWidth dinamico)
//   - Regua mais alta e legivel
//   - Playhead clicavel e arrastavel (como o diamond)
//   - Selecao automatica do esqueleto da entidade selecionada
//   - Botoes com icones (Font Awesome 6 via ui::IconButton, AccentButton, ToggleButton)
//
// UI desenhada com ImGui puro. imgui-node-editor PROIBIDO para a timeline.

#include "editor/axe_editor/animation/sequencer/sequencer_window.hpp"
#include "editor/axe_editor/editor_context.hpp"
// So no .cpp: o header do Sequencer nao precisa arrastar o renderer inteiro
// (nem ImGuizmo) para todo mundo que o incluir. O EditorContext guarda o
// ponteiro por forward declaration; a definicao completa so faz falta aqui.
#include "editor/axe_editor/viewport_renderer.hpp"
// Formas dos Controls — as MESMAS que o editor de rig desenha.
#include "editor/axe_editor/rig/rig_control_gizmos.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"
#include "editor/axe_editor/asset/asset_picker.hpp"
#include "editor/axe_editor/file_dialog.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/mesh/mesh_factory.hpp"
// Bake: o binario cozido de clipe, que ja existe e ja e lido pelo .axeskel.
#include "axe/animation/clip_cooked.hpp"
// Auditoria estatica do grafo do rig: precisa enxergar RigNode_ItemArray para
// ler os itens de uma lista sem executar o grafo.
#include "axe/animation/rig/rig_nodes.hpp"
#include "axe/animation/sequencer/sequence_apply.hpp"
#include "axe/log/log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>
#include <set>

namespace axe {

    // ============================================================
    // ctor / dtor
    // ============================================================

    SequencerWindow::SequencerWindow() = default;

    SequencerWindow::~SequencerWindow() {
        StopPlayer();
    }

    // ============================================================
    // Arquivo (.axeseq)
    // ============================================================

    namespace {

        // ── ASSINATURA DE CONTEUDO ───────────────────────────────────────────
        //
        // FNV-1a sobre tudo que o `.axeseq` grava. Serve a uma pergunta so:
        // "o que esta na tela e igual ao que esta no disco?"
        //
        // Float entra pelos BITS (memcpy para uint32), e nao pelo valor: somar
        // floats acumula erro e dois assets diferentes acabariam com a mesma
        // soma. Comparacao de bits e exata por construcao.
        //
        // Nao e hash criptografico e nao precisa ser — colisao aqui custa um
        // "salvar" que o usuario faria de qualquer jeito, nao um dado perdido.
        std::uint64_t Fnv(std::uint64_t h, std::uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                h ^= (v >> (i * 8)) & 0xFF;
                h *= 1099511628211ull;
            }
            return h;
        }

        std::uint64_t FnvF(std::uint64_t h, float f) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            return Fnv(h, bits);
        }

        std::uint64_t FnvS(std::uint64_t h, const std::string& s) {
            h = Fnv(h, s.size());
            for (unsigned char c : s) {
                h ^= c;
                h *= 1099511628211ull;
            }
            return h;
        }

        std::uint64_t SignatureOf(const SequencerAsset& asset) {
            std::uint64_t h = 14695981039346656037ull;

            h = Fnv(h, static_cast<std::uint64_t>(asset.GetFps()));
            h = Fnv(h, static_cast<std::uint64_t>(asset.GetFrameRange().Start));
            h = Fnv(h, static_cast<std::uint64_t>(asset.GetFrameRange().End));

            for (const auto& b : asset.GetBindings()) {
                h = FnvS(h, b.EntityName);
                h = FnvS(h, b.DisplayName);
                h = FnvS(h, b.RigAssetUUID);
                h = Fnv(h, b.RigControlsFollowAnimation ? 1u : 0u);

                for (const auto& tr : b.Tracks) {
                    h = Fnv(h, static_cast<std::uint64_t>(tr.Type));
                    h = Fnv(h, static_cast<std::uint64_t>(tr.TargetType));
                    h = FnvS(h, tr.TargetName);
                    h = FnvS(h, tr.AttachedAssetUUID);
                    h = Fnv(h, (tr.Muted ? 1u : 0u) | (tr.Locked ? 2u : 0u) |
                        (tr.HoldOutsideSections ? 4u : 0u));

                    for (const auto& sec : tr.Sections) {
                        h = Fnv(h, static_cast<std::uint64_t>(sec.StartFrame));
                        h = Fnv(h, static_cast<std::uint64_t>(sec.EndFrame));
                        h = Fnv(h, static_cast<std::uint64_t>(sec.ClipOffset));
                        h = FnvF(h, sec.ClipRateScale);
                        h = FnvS(h, sec.SourceClipName);
                        h = FnvS(h, sec.SourceClipUUID);

                        for (const auto& ch : sec.Channels) {
                            h = Fnv(h, static_cast<std::uint64_t>(ch.Component));
                            h = Fnv(h, ch.Keys.size());

                            for (const auto& k : ch.Keys) {
                                h = FnvF(h, k.Frame);
                                h = FnvF(h, k.Value);
                                h = Fnv(h, static_cast<std::uint64_t>(k.Interp));
                                h = FnvF(h, k.TangentIn);
                                h = FnvF(h, k.TangentOut);
                            }
                        }
                    }
                }
            }
            return h;
        }

        // Filtro do FileDialog. Os '\0' internos sao exigencia da API do
        // Windows (par rotulo/padrao terminado em duplo nulo), e por isso a
        // string PRECISA ser montada com escapes explicitos — um literal
        // comum pararia no primeiro nulo.
        const char* kSeqFilter = "AXE Sequence\0*.axeseq\0Todos os arquivos\0*.*\0";

    } // namespace

    bool SequencerWindow::IsDirty() const {
        return SignatureOf(m_Asset) != m_SavedSignature;
    }

    std::string SequencerWindow::CurrentFileLabel() const {
        if (m_LastLoadedPath.empty()) return "(sem titulo)";
        return std::filesystem::path(m_LastLoadedPath).filename().string();
    }

    // ── POR QUE REGISTRAR NAO BASTAVA ────────────────────────────────────────
    //
    // `AssetDatabase::Register` cria o record e escreve o `.axemeta`, e eu achei
    // que isso fosse suficiente para o arquivo aparecer no Asset Browser. Nao e,
    // por DUAS razoes que so aparecem juntas:
    //
    //  1. O browser filtra por `record.VirtualFolder == pasta selecionada`, e
    //     VirtualFolder NAO e o caminho em disco: e escriturario do editor,
    //     preenchido so quando alguem importa ou arrasta o asset para uma pasta.
    //     Um record novo nasce com ela vazia — o arquivo existe, tem UUID, e
    //     aparece apenas em "/ All". Salvar dentro de `Assets/Meshes/Sequencer`
    //     nao o poe na pasta "Meshes/Sequencer" do browser.
    //
    //     A convencao existe e e a inversa: o `RelocateAssets` MOVE o arquivo
    //     para `<AssetsPath>/<VirtualFolder>`. Ou seja, a pasta virtual e a
    //     verdade e o disco a segue. Derivar uma da outra aqui e so fechar o
    //     ciclo no sentido que faltava.
    //
    //  2. O indice nao era gravado. Sem `AssetDatabase::Save`, o record vive so
    //     nesta sessao — fechar e reabrir o editor perdia o registro.
    //
    // `typeOverride` existe para o clipe assado: a extensao dele
    // (`.axeclipbin`) NAO mapeia para tipo nenhum, de proposito (ver
    // AssetType::AnimationClip), entao o tipo tem de ser dito aqui.
    std::string SequencerWindow::RegisterProjectAsset(const std::filesystem::path& file,
        AssetType typeOverride) {
        const std::string uuid = AssetDatabase::Get().Register(file);
        if (uuid.empty()) return {};

        auto* rec = const_cast<AssetRecord*>(AssetDatabase::Get().GetByUUID(uuid));
        if (!rec) return uuid;

        if (typeOverride != AssetType::Unknown)
            rec->Type = typeOverride;

        if (ProjectManager::Get().HasProject()) {
            const auto& proj = ProjectManager::Get().GetCurrent();

            std::error_code ec;
            const auto rel = std::filesystem::relative(
                file.parent_path(), proj.AssetsPath, ec);

            // Fora da pasta Assets (ou erro): deixa a pasta virtual VAZIA, e o
            // asset aparece em "/ All". Inventar uma pasta com ".." dentro
            // faria o RelocateAssets tentar mover o arquivo para fora do
            // projeto na proxima vez que alguem clicasse nele.
            if (!ec && !rel.empty() && rel.native()[0] != '.')
                rec->VirtualFolder = rel.generic_string();

            AssetDatabase::Get().Save(proj.RootPath);
        }

        return uuid;
    }

    bool SequencerWindow::SaveToPath(const std::string& path) {
        if (path.empty()) return false;

        if (!m_Asset.SaveToFile(path)) {
            AXE_EDITOR_ERROR("Sequencer: nao consegui gravar '{}'.", path);
            return false;
        }

        m_LastLoadedPath = path;
        m_SavedSignature = SignatureOf(m_Asset);

        // Ver a nota em RegisterProjectAsset: registrar sozinho nao bastava.
        RegisterProjectAsset(path, AssetType::Sequence);

        AXE_EDITOR_INFO("Sequencer: '{}' salva.", path);
        return true;
    }

    bool SequencerWindow::SaveInteractive(bool forceAsk) {
        std::string path = m_LastLoadedPath;

        if (forceAsk || path.empty()) {
            const std::filesystem::path chosen =
                FileDialog::Save(kSeqFilter, "Salvar Sequence", "axeseq");

            if (chosen.empty()) return false;   // cancelado

            std::filesystem::path p = chosen;

            // O dialog nativo devolve o que o usuario digitou. Sem extensao, o
            // arquivo nasce sem tipo: nao casa com AssetTypeFromExtension, nao
            // entra no database, e o duplo clique nunca vai existir para ele.
            if (p.extension() != ".axeseq")
                p.replace_extension(".axeseq");

            path = p.string();
        }

        return SaveToPath(path);
    }

    bool SequencerWindow::OpenFile(const std::filesystem::path& path) {
        if (path.empty()) return false;

        // Para o player ANTES de trocar o conteudo: ele guarda uma copia
        // profunda das bindings e devolve o PoseOverride das entidades da
        // sequence antiga. Sem isto, o personagem da sequence que estava
        // aberta ficaria congelado na ultima pose dela.
        StopPlayer();

        if (!m_Asset.LoadFromFile(path.string())) {
            AXE_EDITOR_ERROR("Sequencer: '{}' nao e um .axeseq valido.", path.string());
            return false;
        }

        m_LastLoadedPath = path.string();
        m_SavedSignature = SignatureOf(m_Asset);

        // Estado de AUTORIA nao sobrevive a troca de arquivo: as copias de
        // trabalho do rig, as previews de socket e a selecao apontam para
        // indices e nomes da sequence anterior.
        ClearRigs();
        ClearSocketPreviews();
        ClearKeySelection();
        ClearPendingEdits();

        m_SelectedBinding = m_SelectedTrack = m_SelectedSection = -1;
        m_SelectedChannel = m_SelectedKey = -1;

        m_Zoom = 1.0f;
        m_TimelineScrollX = 0.0f;
        m_TimelineScrollY = 0.0f;


        // ── O HISTORICO NAO ATRAVESSA ARQUIVOS ───────────────────────────────
        //
        // Sem isto, a troca de conteudo seria vista como "uma mudanca" pelo
        // rastreio, e um Ctrl+Z logo depois de abrir despejaria as bindings da
        // sequence ANTERIOR dentro da recem-aberta — com o nome do arquivo novo
        // no titulo. Um estado que nunca existiu, e impossivel de entender.
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_UndoPendingOpen = false;
        m_UndoIdleFrames = 0;
        m_LastState = TakeSnapshot();
        m_LastSignature = SignatureOf(m_Asset);
        m_HasLastState = true;
        m_KeyClipboard.clear();

        m_Player.Scrub(static_cast<float>(m_Asset.GetFrameRange().Start));
        EnsurePlayerStarted();

        m_IsOpen = true;
        ImGui::SetWindowFocus("Sequencer");

        AXE_EDITOR_INFO("Sequencer: '{}' aberta ({} binding(s)).",
            path.filename().string(), (int)m_Asset.GetBindingCount());
        return true;
    }

    void SequencerWindow::DrawConfirmNewPopup() {
        if (m_OpenConfirmNewPopup) {
            ImGui::OpenPopup("Descartar alteracoes?##seqnew");
            m_OpenConfirmNewPopup = false;
        }

        if (!ImGui::BeginPopupModal("Descartar alteracoes?##seqnew", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted("Esta sequence tem alteracoes nao salvas.");
        ImGui::TextDisabled("%s", CurrentFileLabel().c_str());
        ImGui::Spacing();

        // Salvar e sair fica em PRIMEIRO e com o acento: e a resposta certa na
        // maioria das vezes, e a errada aqui custa o trabalho da sessao.
        if (ui::AccentButton(ICON_SAVE " Salvar e continuar", ui::Accent::Primary)) {
            if (SaveInteractive(false)) {
                NewSequence();
                ImGui::CloseCurrentPopup();
            }
            // Save cancelado: NAO fecha o popup. Fechar mandaria a mensagem
            // errada — "salvei" — logo antes de descartar tudo.
        }

        ImGui::SameLine();
        if (ui::AccentButton(ICON_TRASH " Descartar", ui::Accent::Danger)) {
            NewSequence();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_XMARK " Cancelar"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }


    // ============================================================
    // Bake — a sequence vira um clipe
    // ============================================================

    namespace {

        bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float eps) {
            return std::abs(a.x - b.x) <= eps &&
                std::abs(a.y - b.y) <= eps &&
                std::abs(a.z - b.z) <= eps;
        }

        bool NearlyEqual(const glm::quat& a, const glm::quat& b, float eps) {
            // |dot| porque q e -q sao a MESMA rotacao. Comparar componente a
            // componente marcaria como diferente um canal que so trocou de
            // sinal — e sinal e escolha de representacao, nao de pose.
            return std::abs(std::abs(glm::dot(a, b)) - 1.0f) <= eps;
        }

        // Curva de valor constante vira UMA key. Um bake denso produz uma key
        // por frame para os 52 ossos; num take de 250 frames sao 39 mil keys, e
        // a esmagadora maioria e a mesma escala 1 repetida.
        template <typename KeyT, typename EqT>
        void CollapseIfConstant(std::vector<KeyT>& keys, EqT eq) {
            if (keys.size() < 2) return;

            for (std::size_t i = 1; i < keys.size(); ++i)
                if (!eq(keys[i].Value, keys[0].Value)) return;

            keys.resize(1);
            keys[0].Time = 0.0f;
        }

    } // namespace

    std::filesystem::path SequencerWindow::BakeBindingToClip(int bindingIndex,
        const std::string& clipName, bool looping) {
        namespace fs = std::filesystem;

        if (!m_Context || !m_Context->ActiveScene) return {};
        if (clipName.empty()) {
            AXE_EDITOR_ERROR("Sequencer: o clipe precisa de um nome.");
            return {};
        }

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return {};

        // ── O NOME VIRA NOME DE ARQUIVO ──────────────────────────────────────
        //
        // A sugestao do dialogo sai do DisplayName do binding, que e o nome da
        // entidade — e nome de entidade aceita ':' e '/' sem problema nenhum.
        // Num caminho de arquivo os dois sao separador: 'Hero:Idle' viraria uma
        // pasta no Windows, e o ClipCooked::Write falharia com um erro de
        // "nao consegui abrir" que nao explica coisa nenhuma.
        std::string safeName;
        safeName.reserve(clipName.size());

        for (char c : clipName) {
            const bool bad = (c == '/' || c == '\\' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|');
            safeName.push_back(bad ? '_' : c);
        }

        while (!safeName.empty() && (safeName.back() == ' ' || safeName.back() == '.'))
            safeName.pop_back();

        if (safeName.empty()) {
            AXE_EDITOR_ERROR("Sequencer: '{}' nao serve como nome de arquivo.", clipName);
            return {};
        }

        SkeletalMeshComponent* smc = nullptr;
        const Skeleton* skel = GetBindingSkeleton(bindingIndex, &smc);

        if (!skel || !smc || !smc->Asset) {
            AXE_EDITOR_ERROR("Sequencer: o binding '{}' nao tem um SkeletalMeshAsset "
                "resolvido — nao ha esqueleto onde gravar o clipe.",
                b->DisplayName);
            return {};
        }

        const fs::path skelPath = smc->Asset->GetFilePath();
        if (skelPath.empty()) {
            AXE_EDITOR_ERROR("Sequencer: o personagem deste binding nao veio de um "
                ".axeskel em disco — salve o esqueleto antes de assar.");
            return {};
        }

        const int start = m_Asset.GetFrameRange().Start;
        const int end = m_Asset.GetFrameRange().End;
        const int fps = (m_Asset.GetFps() > 0) ? m_Asset.GetFps() : 30;

        if (end <= start) {
            AXE_EDITOR_ERROR("Sequencer: range invalido ({} -> {}).", start, end);
            return {};
        }

        const int boneCount = static_cast<int>(skel->GetBones().size());
        if (boneCount == 0) return {};

        // ── AMOSTRAGEM ───────────────────────────────────────────────────────
        //
        // Um passo por frame, e cada passo e o EvaluateAndApply INTEIRO. Ver a
        // nota no header sobre por que nao ha um caminho de avaliacao proprio.
        //
        // Custo: uma pose + BuildSkinningMatrices por frame. Num take de 250
        // frames e o mesmo trabalho de 250 frames de scrub - quatro segundos de
        // uso normal da janela.
        std::vector<BoneChannel> channels(boneCount);
        for (int i = 0; i < boneCount; ++i)
            channels[i].BoneIndex = i;

        const float savedFrame = m_Player.GetCurrentFrame();
        const SequencerPlaybackMode savedMode = m_Player.GetMode();

        EnsurePlayerStarted();
        m_Player.SetMode(SequencerPlaybackMode::Scrubbing);

        // Diagnostico desligado durante o bake: ele copia uma Pose inteira e
        // roda a comparacao osso a osso POR FRAME. Num take de 250 frames isso
        // e o custo dominante, e o resultado - uma lista de desvios do ultimo
        // frame - nao serve para nada aqui.
        const bool savedDiag = m_RigDiagnostics;
        m_RigDiagnostics = false;

        m_Baking = true;
        m_BakeDeltaTime = 1.0f / static_cast<float>(fps);

        int sampled = 0;

        for (int f = start; f <= end; ++f) {
            m_Player.Scrub(static_cast<float>(f));
            m_Player.Resample();
            EvaluateAndApply();

            if (bindingIndex >= static_cast<int>(m_WorkPoses.size())) break;

            const Pose& pose = m_WorkPoses[bindingIndex];
            const float t = static_cast<float>(f - start) / static_cast<float>(fps);

            const int n = std::min(boneCount, static_cast<int>(pose.Size()));

            for (int i = 0; i < n; ++i) {
                const BoneTransform& bt = pose[i];

                channels[i].PositionKeys.push_back(VectorKey{ t, bt.Translation });
                channels[i].ScaleKeys.push_back(VectorKey{ t, bt.Scale });

                // ── CONTINUIDADE DE SINAL ────────────────────────────────────
                //
                // q e -q descrevem a mesma rotacao, mas o slerp entre eles vai
                // pelo caminho LONGO - uma volta inteira entre dois frames
                // vizinhos. O glm::slerp do sampler ja corrige na leitura; a
                // correcao aqui e para o ARQUIVO nao carregar o problema, e
                // para qualquer outro leitor (ou um futuro lerp otimizado) ver
                // uma curva continua.
                glm::quat q = glm::normalize(bt.Rotation);

                if (!channels[i].RotationKeys.empty() &&
                    glm::dot(channels[i].RotationKeys.back().Value, q) < 0.0f)
                    q = -q;

                channels[i].RotationKeys.push_back(QuatKey{ t, q });
            }

            ++sampled;
        }

        // Devolve o playhead e refaz a pose: sem isto o personagem ficaria na
        // pose do ULTIMO frame do bake, e o usuario veria a agulha num lugar e
        // o boneco noutro.
        m_Baking = false;
        m_RigDiagnostics = savedDiag;

        m_Player.Scrub(savedFrame);
        m_Player.SetMode(savedMode);
        m_Player.Resample();
        EvaluateAndApply();

        if (sampled == 0) {
            AXE_EDITOR_ERROR("Sequencer: o bake nao conseguiu avaliar nenhum frame.");
            return {};
        }

        // ── LIMPEZA DAS CURVAS ───────────────────────────────────────────────
        //
        // Mesma regra que o ExplodeClipTrack ja aplica no sentido contrario:
        // canal que nao varia e canal que nao descreve nada. Um osso parado na
        // pose de repouso some inteiro - o sampler cai na LocalBindPose para
        // quem nao tem canal, entao o resultado e identico e o arquivo fica uma
        // ordem de grandeza menor.
        Pose bind;
        Pose::FromBindPose(*skel, bind);

        auto clip = std::make_shared<AnimationClip>();
        clip->SetName(safeName);
        clip->SetDuration(static_cast<float>(end - start) / static_cast<float>(fps));
        clip->SetLooping(looping);

        const float kPosEps = 1e-5f;
        const float kRotEps = 1e-6f;
        const float kScaleEps = 1e-5f;

        int kept = 0;

        for (int i = 0; i < boneCount; ++i) {
            BoneChannel& ch = channels[i];

            CollapseIfConstant(ch.PositionKeys,
                [&](const glm::vec3& a, const glm::vec3& c) { return NearlyEqual(a, c, kPosEps); });
            CollapseIfConstant(ch.ScaleKeys,
                [&](const glm::vec3& a, const glm::vec3& c) { return NearlyEqual(a, c, kScaleEps); });
            CollapseIfConstant(ch.RotationKeys,
                [&](const glm::quat& a, const glm::quat& c) { return NearlyEqual(a, c, kRotEps); });

            const bool constant =
                ch.PositionKeys.size() <= 1 &&
                ch.RotationKeys.size() <= 1 &&
                ch.ScaleKeys.size() <= 1;

            if (constant && i < static_cast<int>(bind.Size())) {
                const BoneTransform& rest = bind[i];

                const bool sameAsRest =
                    (ch.PositionKeys.empty() || NearlyEqual(ch.PositionKeys[0].Value, rest.Translation, kPosEps)) &&
                    (ch.ScaleKeys.empty() || NearlyEqual(ch.ScaleKeys[0].Value, rest.Scale, kScaleEps)) &&
                    (ch.RotationKeys.empty() || NearlyEqual(ch.RotationKeys[0].Value, rest.Rotation, kRotEps));

                if (sameAsRest) continue;   // o sampler cai na bind pose sozinho
            }

            clip->AddChannel(ch);
            ++kept;
        }

        if (kept == 0) {
            AXE_EDITOR_ERROR("Sequencer: nada a assar — nenhum osso se move nesta "
                "sequence.");
            return {};
        }

        // ── GRAVACAO ─────────────────────────────────────────────────────────
        //
        // Ao lado do `.axeskel`, com o nome do clipe. O nome do ARQUIVO e o que
        // vira o nome da entrada no `.axeskel` (o AddAnimation usa o stem), e
        // por isso ele e o nome que o usuario escolheu - nao um derivado da
        // sequence.
        fs::path out = skelPath.parent_path() / (safeName + ".axeclipbin");

        if (!ClipCooked::Write(out, { clip }, *skel)) {
            AXE_EDITOR_ERROR("Sequencer: falha ao gravar '{}'.", out.string());
            return {};
        }

        // ── REGISTRO NO .axeskel ─────────────────────────────────────────────
        //
        // Reassar sobrescreve o arquivo, mas o AddAnimation DEDUPLICA por
        // caminho e devolveria 0 sem recarregar nada - o `.axeskel` continuaria
        // com a curva antiga em memoria, e o usuario veria o bake "nao ter
        // efeito". Remover a entrada primeiro forca a releitura.
        const int existing = smc->Asset->FindAnimationEntryBySource(out);
        if (existing >= 0)
            smc->Asset->RemoveAnimation(existing);

        const int added = smc->Asset->AddAnimation(out);

        if (added <= 0) {
            AXE_EDITOR_ERROR("Sequencer: '{}' foi gravado, mas o .axeskel nao o "
                "aceitou (os nomes de osso batem?).", out.filename().string());
            return {};
        }

        // ── O CLIPE ASSADO ENTRA NO BROWSER ──────────────────────────────────
        //
        // Um `.axeclipbin` normal e derivado e nao deve aparecer. Este nao veio
        // de FBX nenhum: se nao aparecer, o unico jeito de encontra-lo e saber
        // de cor que ele foi parar dentro da lista de animacoes do `.axeskel`.
        // Ver AssetType::AnimationClip.
        RegisterProjectAsset(out, AssetType::AnimationClip);

        if (!smc->Asset->Save()) {
            AXE_EDITOR_WARN("Sequencer: o clipe entrou, mas nao consegui regravar "
                "'{}' - a entrada se perde ao recarregar o projeto.",
                skelPath.filename().string());
        }

        // ── O CLIPE APARECE AGORA, E NAO SO NO PROXIMO LOAD ───────────────────
        //
        // `SkeletalMeshComponent::Clips` e uma COPIA da lista do asset, feita
        // no momento em que a entidade foi montada. Sem reassinar, o clipe
        // recem-assado nao apareceria no picker do proprio Sequencer que acabou
        // de cria-lo.
        //
        // Todas as entidades que usam o mesmo asset, e nao so a do binding: o
        // AnimGraph de qualquer uma delas passa a poder referenciar o clipe.
        {
            auto& reg = m_Context->ActiveScene->GetRegistry();
            for (auto e : reg.view<SkeletalMeshComponent>()) {
                auto& c = reg.get<SkeletalMeshComponent>(e);
                if (c.Asset == smc->Asset)
                    c.Clips = smc->Asset->GetClips();
            }
        }

        AXE_EDITOR_INFO("Sequencer: clipe '{}' assado - {} frames, {} osso(s) com "
            "curva, {:.2f}s. Gravado em '{}'.",
            safeName, sampled, kept, clip->GetDuration(), out.string());

        return out;
    }

    void SequencerWindow::DrawBakePopup() {
        if (m_OpenBakePopup) {
            ImGui::OpenPopup("Assar clipe##seqbake");
            m_OpenBakePopup = false;
        }

        if (!ImGui::BeginPopupModal("Assar clipe##seqbake", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
            return;

        const int bindingCount = static_cast<int>(m_Asset.GetBindingCount());

        if (bindingCount == 0) {
            ImGui::TextDisabled("Nenhum binding nesta sequence.");
            if (ImGui::Button("Fechar")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        if (m_BakeBinding < 0 || m_BakeBinding >= bindingCount)
            m_BakeBinding = (m_SelectedBinding >= 0 && m_SelectedBinding < bindingCount)
            ? m_SelectedBinding : 0;

        // Quem entra no clipe. Um clipe e de UM esqueleto — assar dois
        // personagens num arquivo so nao teria como ser tocado depois.
        ImGui::TextDisabled("Personagem");
        for (int i = 0; i < bindingCount; ++i) {
            const auto* b = m_Asset.GetBinding(i);
            if (!b) continue;

            char label[160];
            std::snprintf(label, sizeof(label), "%s##bakebind%d",
                b->DisplayName.empty() ? b->EntityName.c_str() : b->DisplayName.c_str(), i);

            if (ImGui::RadioButton(label, m_BakeBinding == i))
                m_BakeBinding = i;
        }

        ImGui::Separator();

        // Nome sugerido na primeira abertura: <sequence>_<personagem>. Deixar
        // vazio faria o usuario inventar um nome com o dialogo ja aberto, que e
        // o pior momento para decidir.
        if (m_BakeClipName[0] == '\0') {
            const auto* b = m_Asset.GetBinding(m_BakeBinding);
            const std::string seq = m_LastLoadedPath.empty()
                ? std::string("Sequence")
                : std::filesystem::path(m_LastLoadedPath).stem().string();

            std::snprintf(m_BakeClipName, sizeof(m_BakeClipName), "%s_%s",
                seq.c_str(), b ? b->DisplayName.c_str() : "Bake");
        }

        ImGui::SetNextItemWidth(320);
        ImGui::InputText("Nome do clipe", m_BakeClipName, sizeof(m_BakeClipName));

        ImGui::Checkbox("Loop", &m_BakeLoop);

        const int start = m_Asset.GetFrameRange().Start;
        const int end = m_Asset.GetFrameRange().End;
        const int fps = (m_Asset.GetFps() > 0) ? m_Asset.GetFps() : 30;

        ImGui::Spacing();
        ImGui::TextDisabled("Frames %d..%d a %d fps  =  %.2fs",
            start, end, fps, static_cast<float>(end - start) / static_cast<float>(fps));

        // Onde o arquivo vai cair, ANTES de clicar. "Gravei" sem dizer onde e
        // metade de uma resposta.
        {
            SkeletalMeshComponent* smc = nullptr;
            const Skeleton* skel = GetBindingSkeleton(m_BakeBinding, &smc);

            if (skel && smc && smc->Asset && !smc->Asset->GetFilePath().empty()) {
                const std::filesystem::path out =
                    smc->Asset->GetFilePath().parent_path() /
                    (std::string(m_BakeClipName) + ".axeclipbin");

                ImGui::TextDisabled("-> %s", out.generic_string().c_str());
                ImGui::TextDisabled("   e uma entrada em %s",
                    smc->Asset->GetFilePath().filename().string().c_str());
            }
            else {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                    ICON_TRIANGLE_EXCLAMATION " Este binding nao tem um .axeskel "
                    "em disco.");
            }
        }

        ImGui::Spacing();

        // Pose pendente e trabalho que ainda nao existe no asset. Assar com ela
        // na tela produziria um clipe que NAO corresponde ao arquivo salvo — e
        // a diferenca so apareceria depois, sem nada que a explicasse.
        const bool blocked = !m_PendingEdits.empty();

        if (blocked) {
            ImGui::TextColored(ui::AccentColor(ui::Accent::Warning),
                ICON_TRIANGLE_EXCLAMATION " Ha pose nao gravada. Crave com K "
                "(ou descarte com Esc) antes de assar.");
        }

        ImGui::BeginDisabled(blocked || m_BakeClipName[0] == '\0');

        if (ui::AccentButton(ICON_FILM " Assar", ui::Accent::Primary)) {
            if (!BakeBindingToClip(m_BakeBinding, m_BakeClipName, m_BakeLoop).empty())
                ImGui::CloseCurrentPopup();
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button(ICON_XMARK " Cancelar"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void SequencerWindow::NewSequence() {
        StopPlayer();

        m_Asset = SequencerAsset{};
        m_LastLoadedPath.clear();
        m_SavedSignature = SignatureOf(m_Asset);

        ClearRigs();
        ClearSocketPreviews();
        ClearKeySelection();
        ClearPendingEdits();

        m_SelectedBinding = m_SelectedTrack = m_SelectedSection = -1;
        m_SelectedChannel = m_SelectedKey = -1;

        m_Zoom = 1.0f;
        m_TimelineScrollX = 0.0f;
        m_TimelineScrollY = 0.0f;

        // ── O HISTORICO NAO ATRAVESSA ARQUIVOS ───────────────────────────────
        //
        // Sem isto, a troca de conteudo seria vista como "uma mudanca" pelo
        // rastreio, e um Ctrl+Z logo depois de abrir despejaria as bindings da
        // sequence ANTERIOR dentro da recem-aberta — com o nome do arquivo novo
        // no titulo. Um estado que nunca existiu, e impossivel de entender.
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_UndoPendingOpen = false;
        m_UndoIdleFrames = 0;
        m_LastState = TakeSnapshot();
        m_LastSignature = SignatureOf(m_Asset);
        m_HasLastState = true;
        m_KeyClipboard.clear();
    }

    // ============================================================
    // Draw
    // ============================================================

    void SequencerWindow::Draw() {
        if (!m_IsOpen) {
            // Janela fechada: a pose volta a ser do AnimationWorld. Sem isto o
            // personagem congelaria na ultima pose autorada e o usuario nao teria
            // como desfazer, a nao ser reabrindo o Sequencer.
            ReleasePoseOverride();
            return;
        }

        // ── Avanco do tempo ──────────────────────────────────────────────────
        if (m_PlayerStarted && m_Player.GetMode() == SequencerPlaybackMode::Playing) {
            m_Player.OnUpdate(ImGui::GetIO().DeltaTime);
        }

        // ── A POSE PENDENTE PERTENCE A UM FRAME ──────────────────────────────
        //
        // Sair do frame descarta o que nao foi gravado. Nao e economia de
        // memoria: uma pose e uma afirmacao sobre um INSTANTE. Arrastada para o
        // frame seguinte, ela continuaria na tela sobrepondo a animacao — e o
        // Key gravaria num frame que o usuario nao estava olhando quando posou.
        //
        // Tolerancia de meio frame porque o playhead e float e o scrub passa por
        // valores fracionarios.
        if (!m_PendingEdits.empty() &&
            std::abs(m_Player.GetCurrentFrame() - m_PendingFrame) > 0.5f) {
            ClearPendingEdits();
        }

        // ── Aplicacao da pose, TODO FRAME ────────────────────────────────────
        //
        // Antes isto so acontecia durante o Play. Como o AnimationWorld reescreve
        // o BonePalette a cada frame, o resultado de um scrub sobrevivia menos de
        // um frame: o usuario arrastava o playhead e nao via nada acontecer.
        //
        // Agora a janela aberta e dona da pose das entidades ligadas (o
        // PoseOverride e ligado dentro de EvaluateAndApply) e reescreve a pose
        // todo frame. Custo: uma Pose + BuildSkinningMatrices por binding — o
        // mesmo que a preview do Control Rig ja paga.
        // ── EM PLAY, A CENA NAO E MAIS NOSSA ─────────────────────────────────
        //
        // Ver SetScenePlaying: quem toca a sequence no Play e o SequenceWorld.
        // Soltar a pose UMA vez na transicao (e nao a cada frame) porque
        // ReleasePoseOverride varre todas as entidades com esqueleto.
        if (m_ScenePlaying) {
            if (!m_ReleasedForPlay) {
                ReleasePoseOverride();
                m_ReleasedForPlay = true;
            }
        }
        else if (m_ReleasedForPlay) {
            m_ReleasedForPlay = false;
        }

        if (m_PlayerStarted && !m_ScenePlaying) {
            // O ASSET e a fonte da verdade no editor; a copia viva do player
            // segue. Sincronizar aqui, uma vez por frame, elimina a classe
            // inteira de bugs "editei e o sample nao viu" — em vez de lembrar de
            // chamar sync em cada um dos ~8 pontos que mutam o asset (add key,
            // drag, delete, interp, valor, add/remove track, add/remove binding).
            // O custo e copiar alguns vetores pequenos por frame.
            m_Player.SyncFrom(m_Asset);
            EvaluateAndApply();

            // Depois da avaliacao: o transform da camera deste frame ja foi
            // escrito, entao trocar o alvo do pilot agora mostra o plano novo
            // no MESMO frame — e nao um quadro atrasado.
            SyncPilotToCameraCut();
        }

        // O pedido de gizmo e reposto TODO FRAME, e depois da avaliacao: a
        // matriz que ele entrega e a do osso NESTA pose, e a pose acabou de ser
        // recalculada. Pedir antes deixaria o gizmo um frame atrasado — visivel
        // como um tremor durante o arrasto.
        UpdateGizmo();

        // Irmao do gizmo, e reposto pela mesma razao: um pedido de desenho que
        // sobrevivesse ao fechar da janela desenharia os controles de uma
        // sequence que nem esta mais aberta.
        UpdateViewportOverlay();

        // Quem aparece neste frame — grupos dobrados e filtro ja resolvidos.
        // UMA vez, antes do outliner e da timeline: os dois leem desta lista, e
        // e isso que garante que a linha no outliner e a lane na timeline
        // concordem sobre o que existe.
        RebuildLayout();

        ImGui::SetNextWindowSize(ImVec2(1100.0f, 520.0f), ImGuiCond_FirstUseEver);
        bool open = m_IsOpen;

        // ── TITULO COM O ARQUIVO, ID FIXO ────────────────────────────────────
        //
        // O `###Sequencer` e obrigatorio, nao enfeite: o ImGui identifica
        // janela pelo rotulo, e um titulo que muda a cada save/load seria uma
        // janela DIFERENTE a cada vez — o dock, o tamanho e a posicao que o
        // usuario ajustou se perderiam no instante em que ele abrisse um
        // arquivo. Com `###`, so o que aparece muda.
        //
        // Todo mundo que chama SetWindowFocus("Sequencer") continua achando a
        // janela: o ImGui casa pela parte depois do `###`.
        char title[256];
        std::snprintf(title, sizeof(title), "Sequencer - %s%s###Sequencer",
            CurrentFileLabel().c_str(), IsDirty() ? " *" : "");

        // Fora do `if`: uma janela fechada (colapsada ou fora do dock visivel)
        // nao esta em foco, e deixar a flag do frame anterior de pe faria o
        // editor continuar achando que o Ctrl+Z e nosso.
        m_IsFocused = false;

        if (ImGui::Begin(title, &open)) {
            m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

            DrawToolbar();

            // ── ATALHOS DE SELECAO ───────────────────────────────────────────
            //
            // ANTES dos paineis: apagar keys no meio do desenho da timeline
            // mudaria os indices sob os lacos que ja comecaram a rodar.
            //
            // `WantTextInput` e a guarda que importa. Sem ela, Ctrl+A dentro do
            // filtro do picker ou do campo de nome de socket selecionaria todas
            // as keys da sequence em vez do texto que se esta editando — e o
            // Delete seguinte apagaria a animacao inteira.
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput) {
                const ImGuiIO& io = ImGui::GetIO();

                // ── HISTORICO E AREA DE TRANSFERENCIA ────────────────────
                //
                // Antes dos outros atalhos porque sao os que o usuario aperta
                // por reflexo, e um Ctrl+Z que as vezes nao responde e pior que
                // nenhum.
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                    if (io.KeyShift) Redo();
                    else             Undo();
                }

                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                    Redo();

                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
                    CopySelectedKeys(false);

                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
                    CopySelectedKeys(true);

                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
                    PasteClipboardAtPlayhead();

                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
                    DuplicateSelectedKeys();

                // Ctrl+S / Ctrl+Shift+S. O Shift e testado PRIMEIRO: sem isso,
                // "Salvar como" cairia no ramo do "Salvar" e regravaria no
                // arquivo atual — o oposto do que foi pedido.
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                    SaveInteractive(io.KeyShift);

                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
                    SelectAllKeys();

                // K crava a pose. Com pendencia, promove o que esta na tela;
                // sem pendencia (REC ligado, ou nada tocado), captura todas as
                // tracks do binding — que e o gesto de "guarda este instante".
                if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
                    if (CommitPendingEdits() == 0)
                        CaptureAllTracksAtPlayhead(m_SelectedBinding);
                }

                // Esc descarta a pose pendente ANTES de mexer na selecao: com
                // pose na tela, "cancelar" quer dizer aquilo, e nao desmarcar
                // keys que o usuario nem esta olhando.
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    if (!m_PendingEdits.empty()) ClearPendingEdits();
                    else                         ClearKeySelection();
                }

                if (!m_SelectedKeys.empty() &&
                    (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
                    DeleteSelectedKeys();
                }
            }

            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            float  transportHeight = 42.0f;
            float  mainHeight = contentSize.y - transportHeight;

            // Largura do outliner clampada ANTES do BeginChild: a janela pode ter
            // encolhido desde o frame passado, e uma largura maior que o conteudo
            // disponivel empurra a timeline para fora da tela.
            const float minOutliner = 180.0f;
            const float maxOutliner = std::max(minOutliner, contentSize.x - 220.0f);
            m_OutlinerWidth = std::clamp(m_OutlinerWidth, minOutliner, maxOutliner);

            // Outliner (esquerda).
            ImGui::BeginChild("SequencerOutliner",
                ImVec2(m_OutlinerWidth, mainHeight), true);
            DrawOutliner();
            ImGui::EndChild();

            // ── SPLITTER ─────────────────────────────────────────────────────
            //
            // O outliner era 280px fixos. Nome de osso do Mixamo
            // ("mixamorig:RightHandThumb2") nao cabe nisso, e a section de clipe
            // tem dois DragInt lado a lado que ficavam cortados pela metade — o
            // "Offset" nao mostrava nem a legenda.
            //
            // SameLine com spacing ZERO nos dois lados: com o espacamento padrao
            // a barra flutua no meio de um vao e nao parece a divisoria de nada.
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::InvisibleButton("##seq_splitter", ImVec2(6.0f, mainHeight));

            const bool splitterActive = ImGui::IsItemActive();
            const bool splitterHovered = ImGui::IsItemHovered();

            if (splitterHovered || splitterActive)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

            if (splitterActive)
                m_OutlinerWidth += ImGui::GetIO().MouseDelta.x;

            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                ImGui::GetColorU32(splitterActive ? ImGuiCol_SeparatorActive
                    : splitterHovered ? ImGuiCol_SeparatorHovered
                    : ImGuiCol_Separator));

            ImGui::SameLine(0.0f, 0.0f);

            // Timeline (direita).
            //
            // NoScrollWithMouse/NoScrollbar: a timeline gerencia o proprio scroll
            // vertical (ver m_TimelineScrollY em DrawTimeline). Sem estas flags o
            // ImGui e a timeline disputariam a roda do mouse.
            ImGui::BeginChild("SequencerTimeline",
                ImVec2(0, mainHeight), true,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            DrawTimeline();
            ImGui::EndChild();

            DrawTransportBar();

            // Popups por ultimo: precisam estar no mesmo escopo de janela em que
            // o botao que os abriu vive, senao o ImGui nao acha o ID.
            DrawAddBindingPopup();
            DrawConfirmNewPopup();
            DrawBakePopup();
            if (m_AddTrackForBinding >= 0)
                DrawAddTrackPopup(m_AddTrackForBinding);
            if (m_AddClipForBinding >= 0)
                DrawAddClipPopup(m_AddClipForBinding);
            if (m_AddSocketForBinding >= 0)
                DrawAddSocketPopup(m_AddSocketForBinding);
            if (m_AddControlForBinding >= 0)
                DrawAddControlPopup(m_AddControlForBinding);
            if (m_AddEventForBinding >= 0)
                DrawAddEventPopup(m_AddEventForBinding);
            if (m_AddCameraCutForBinding >= 0)
                DrawAddCameraCutPopup(m_AddCameraCutForBinding);
        }
        ImGui::End();

        // ── NO FIM, E NAO NO COMECO ──────────────────────────────────────────
        //
        // Tudo que muta o asset neste frame ja aconteceu: os popups, os
        // atalhos, o outliner, a timeline — e tambem o gizmo, que roda mais
        // cedo, no desenho do viewport. Rastrear no comeco perderia um frame e,
        // pior, atribuiria a mudanca ao gesto seguinte.
        TrackUndoState();

        if (!open) {
            m_IsOpen = false;
            ReleasePoseOverride();   // fechar no X tambem devolve a pose
        }
    }

    // ============================================================
    // Pickers
    // ============================================================

    void SequencerWindow::DrawAddBindingPopup() {
        if (m_OpenAddBindingPopup) {
            ImGui::OpenPopup("Adicionar entidade##seqbind");
            m_OpenAddBindingPopup = false;
        }

        if (!ImGui::BeginPopup("Adicionar entidade##seqbind")) return;

        // ── QUEM PODE ENTRAR NUMA SEQUENCE ───────────────────────────────────
        //
        // Antes: so entidades com SkeletalMeshComponent. Isso deixava de fora
        // exatamente o que motivou o transform de entidade — a CAMERA.
        //
        // Listar TODAS por padrao seria pior: uma cena tem dezenas de entidades
        // e o picker viraria um despejo. Esqueleto e camera sao os dois que se
        // procura noventa por cento das vezes; o resto fica atras de um
        // interruptor.
        ImGui::Checkbox("Todas as entidades", &m_PickerShowAllEntities);

        ImGui::TextDisabled(m_PickerShowAllEntities
            ? "Qualquer entidade com transform"
            : "Personagens e cameras");
        ImGui::Separator();

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##filter", "filtrar...", m_PickerFilter,
            sizeof(m_PickerFilter));

        if (!m_Context || !m_Context->ActiveScene) {
            ImGui::TextDisabled("Nenhuma cena aberta.");
            ImGui::EndPopup();
            return;
        }

        auto& reg = m_Context->ActiveScene->GetRegistry();

        // A cena e a fonte da lista — nao a selecao. E esta e a diferenca que o
        // usuario sentiu: dava para colocar um esqueleto na timeline clicando
        // nele no viewport, mas nao procurando por ele aqui.
        int shown = 0;

        // Transform e o requisito minimo: sem ele nao ha o que animar nem onde
        // pendurar um esqueleto.
        for (auto entity : reg.view<TransformComponent>()) {
            auto* nm = reg.try_get<NameComponent>(entity);

            // Sem nome nao ha como religar depois do load — o binding guarda o
            // NOME, nao o handle. Melhor nem oferecer.
            if (!nm || nm->Name.empty()) continue;

            const std::string& name = nm->Name;

            if (m_PickerFilter[0] != '\0' &&
                name.find(m_PickerFilter) == std::string::npos)
                continue;

            auto* smc = reg.try_get<SkeletalMeshComponent>(entity);
            const bool isCamera = reg.all_of<CameraComponent>(entity);

            if (!m_PickerShowAllEntities && !smc && !isCamera)
                continue;

            const Skeleton* sk = smc ? smc->GetSkeleton() : nullptr;

            ImGui::PushID((int)entity);

            const char* icon = smc ? ICON_PERSON : (isCamera ? ICON_CAMERA : ICON_CUBE);

            char label[224];
            std::snprintf(label, sizeof(label), "%s %s%s",
                icon, name.c_str(),
                (smc && !sk) ? "  (sem esqueleto carregado)" : "");

            // ── SO O ESQUELETO PELA METADE FICA DESABILITADO ─────────────────
            //
            // Uma entidade sem esqueleto e um alvo LEGITIMO agora (a camera).
            // Um SkeletalMeshComponent que existe mas nao resolveu, nao: ali o
            // usuario esperaria tracks de osso, e elas nao teriam onde casar.
            const bool broken = (smc && !sk);

            if (ImGui::Selectable(label, false,
                broken ? ImGuiSelectableFlags_Disabled : 0)) {
                int idx = CreateBindingForEntity(entity);
                if (idx >= 0) {
                    m_SelectedBinding = idx;
                    m_SelectedTrack = m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;

                    // Camera (ou qualquer coisa sem esqueleto) so tem um alvo
                    // possivel: o proprio transform. Criar a track no ato poupa
                    // uma cacada por um botao que so faz uma coisa.
                    if (!sk) {
                        const int ti = CreateEntityTransformTrack(idx);
                        if (ti >= 0) m_SelectedTrack = ti;
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::IsItemHovered()) {
                if (sk)           ImGui::SetTooltip("%d ossos", (int)sk->GetBoneCount());
                else if (isCamera) ImGui::SetTooltip("Camera — a track de transform e criada junto");
            }

            ImGui::PopID();
            ++shown;
        }

        if (shown == 0)
            ImGui::TextDisabled(m_PickerShowAllEntities
                ? "Nenhuma entidade com nome na cena."
                : "Nenhum personagem ou camera. Marque 'Todas as entidades'.");

        ImGui::EndPopup();
    }

    void SequencerWindow::DrawAddTrackPopup(int bindingIndex) {
        // OpenPopup UMA vez, no frame do clique. Chamar todo frame reabriria o
        // popup logo depois do ImGui fecha-lo por clique-fora — ele nunca fecharia.
        if (m_OpenAddTrackPopup) {
            ImGui::OpenPopup("Adicionar osso##seqtrack");
            m_OpenAddTrackPopup = false;
        }

        if (!ImGui::BeginPopup("Adicionar osso##seqtrack")) {
            m_AddTrackForBinding = -1;   // fechou: encerra o pedido
            return;
        }

        const Skeleton* sk = GetBindingSkeleton(bindingIndex);
        if (!sk) {
            ImGui::TextDisabled("Binding sem esqueleto resolvido.");
            ImGui::EndPopup();
            m_AddTrackForBinding = -1;
            return;
        }

        ImGui::TextDisabled("Ossos de '%s'", m_Asset.GetBinding(bindingIndex)->EntityName.c_str());
        ImGui::Separator();

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##bonefilter", "filtrar...", m_PickerFilter,
            sizeof(m_PickerFilter));

        // Nomes de osso do Mixamo sao longos ("mixamorig:RightHand") — sem o
        // filtro, achar um osso numa lista de 53 e rolagem cega.
        ImGui::BeginChild("##bonelist", ImVec2(280.0f, 320.0f), true);

        const auto& bones = sk->GetBones();
        for (int i = 0; i < (int)bones.size(); ++i) {
            const std::string& bn = bones[i].Name;

            if (m_PickerFilter[0] != '\0' && bn.find(m_PickerFilter) == std::string::npos)
                continue;

            ImGui::PushID(i);

            // Indentacao pela profundidade na hierarquia: le como esqueleto, nao
            // como lista alfabetica. Os ossos ja vem em ordem topologica.
            int depth = 0;
            for (int p = bones[i].ParentIndex; p >= 0; p = bones[p].ParentIndex) ++depth;
            if (depth > 0) ImGui::Indent(depth * 8.0f);

            if (ImGui::Selectable(bn.c_str())) {
                int ti = CreateTransformTrack(bindingIndex, bn);
                if (ti >= 0) {
                    m_SelectedBinding = bindingIndex;
                    m_SelectedTrack = ti;
                    m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
                }
                m_AddTrackForBinding = -1;
                ImGui::CloseCurrentPopup();
            }

            if (depth > 0) ImGui::Unindent(depth * 8.0f);
            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

    // Picker de ANIMACAO EXISTENTE.
    //
    // A lista vem de `SkeletalMeshComponent::Clips` — a MESMA lista que o
    // Inspector mostra em "Animacoes (N)" e que o AnimGraph oferece no
    // ClipPlayer. Nao ha um segundo caminho de import aqui de proposito: quem
    // importa animacao e o Asset Browser / o `.axeskel`, e um clipe que o
    // Sequencer conhecesse e o Inspector nao seria um clipe fantasma.
    void SequencerWindow::DrawAddClipPopup(int bindingIndex) {
        if (m_OpenAddClipPopup) {
            ImGui::OpenPopup("Adicionar animacao##seqclip");
            m_OpenAddClipPopup = false;
        }

        if (!ImGui::BeginPopup("Adicionar animacao##seqclip")) {
            m_AddClipForBinding = -1;
            return;
        }

        SkeletalMeshComponent* smc = nullptr;
        const Skeleton* sk = GetBindingSkeleton(bindingIndex, &smc);

        if (!sk || !smc) {
            ImGui::TextDisabled("Binding sem esqueleto resolvido.");
            ImGui::EndPopup();
            m_AddClipForBinding = -1;
            return;
        }

        const auto* b = m_Asset.GetBinding(bindingIndex);
        ImGui::TextDisabled("Animacoes de '%s'", b ? b->EntityName.c_str() : "?");
        ImGui::Separator();

        if (smc->Clips.empty()) {
            ImGui::TextWrapped("Esta entidade nao tem nenhuma animacao.\n"
                "Importe pelo Inspector (Skeletal Mesh > Importar animacao...).");
            ImGui::EndPopup();
            return;
        }

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##clipfilter", "filtrar...", m_PickerFilter,
            sizeof(m_PickerFilter));

        // As duas maneiras de usar um clipe aqui, e elas sao mesmo diferentes:
        //
        //   LIGADO  — o clipe vira curvas de osso editaveis. E o que responde a
        //             "quero alterar esta animacao": os keyframes aparecem na
        //             timeline e cada um deles arrasta.
        //   DESLIGADO — o clipe entra como camada base opaca e voce autora POR
        //             CIMA dele com tracks de override. Nao mexe no original e
        //             a timeline fica limpa, mas nao ha keyframe para editar.
        ImGui::Checkbox("Explodir em tracks de osso", &m_ExplodeClipOnAdd);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Ligado: gera uma track por osso animado, com as keys do clipe.\n"
                "        A timeline mostra os keyframes e voce edita a animacao.\n\n"
                "Desligado: o clipe entra como camada base (nao editavel) e voce\n"
                "        sobrepoe tracks de osso por cima.");
        }

        ImGui::BeginChild("##cliplist", ImVec2(300.0f, 300.0f), true);

        const int fps = (m_Asset.GetFps() > 0) ? m_Asset.GetFps() : 30;

        for (int i = 0; i < (int)smc->Clips.size(); ++i) {
            const auto& c = smc->Clips[i];
            if (!c) continue;

            const std::string& cn = c->GetName();
            if (m_PickerFilter[0] != '\0' && cn.find(m_PickerFilter) == std::string::npos)
                continue;

            ImGui::PushID(i);

            char label[192];
            std::snprintf(label, sizeof(label), ICON_FILM " %s", cn.c_str());

            if (ImGui::Selectable(label)) {
                int ti = CreateClipTrack(bindingIndex, cn);
                if (ti >= 0) {
                    m_SelectedBinding = bindingIndex;
                    m_SelectedTrack = ti;
                    m_SelectedSection = 0;
                    m_SelectedChannel = m_SelectedKey = -1;
                }
                m_AddClipForBinding = -1;
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%.2f s  (~%d frames a %d fps)%s",
                    c->GetDuration(),
                    (int)std::ceil(c->GetDuration() * (float)fps),
                    fps,
                    c->IsLooping() ? "\nloop" : "");
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

    // Picker de SOCKET.
    //
    // Lista os sockets do `.axeskel` — os MESMOS que o Animation Editor edita e
    // que o SocketAttachmentComponent do gameplay resolve por nome. Nao ha uma
    // segunda nocao de socket aqui, e nao pode haver: um socket que o Sequencer
    // conhecesse e o runtime nao seria um socket fantasma, e a arma apareceria
    // na animacao e sumiria no jogo.
    //
    // A criacao inline grava no `.axeskel` na hora. Custa um Save() e evita a
    // viagem "fecha o Sequencer, abre o Animation Editor, cria, salva, volta,
    // reabre a sequence" — que e onde se perde o fio do que se estava fazendo.
    void SequencerWindow::DrawAddSocketPopup(int bindingIndex) {
        if (m_OpenAddSocketPopup) {
            ImGui::OpenPopup("Adicionar socket##seqsock");
            m_OpenAddSocketPopup = false;
        }

        if (!ImGui::BeginPopup("Adicionar socket##seqsock")) {
            m_AddSocketForBinding = -1;
            return;
        }

        SkeletalMeshComponent* smc = nullptr;
        const Skeleton* sk = GetBindingSkeleton(bindingIndex, &smc);

        if (!sk || !smc || !smc->Asset) {
            ImGui::TextWrapped("Binding sem esqueleto resolvido (ou sem asset "
                ".axeskel). Sockets vivem no asset, nao no Skeleton.");
            ImGui::EndPopup();
            m_AddSocketForBinding = -1;
            return;
        }

        auto& sockets = smc->Asset->GetSockets();
        const auto& bones = sk->GetBones();

        ImGui::TextDisabled("Sockets de '%s'", smc->Asset->GetName().c_str());
        ImGui::Separator();

        if (sockets.empty()) {
            ImGui::TextWrapped("Este esqueleto ainda nao tem socket nenhum.\n"
                "Crie um abaixo - ele passa a existir no .axeskel e vale para\n"
                "o gameplay tambem, nao so para esta sequence.");
        }
        else {
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##sockfilter", "filtrar...", m_PickerFilter,
                sizeof(m_PickerFilter));

            ImGui::BeginChild("##socklist", ImVec2(320.0f, 180.0f), true);

            for (int i = 0; i < static_cast<int>(sockets.size()); ++i) {
                const auto& s = sockets[i];
                if (m_PickerFilter[0] != '\0' &&
                    s.Name.find(m_PickerFilter) == std::string::npos) continue;

                ImGui::PushID(i);

                char label[256];
                std::snprintf(label, sizeof(label), ICON_LINK " %s",
                    s.Name.empty() ? "(sem nome)" : s.Name.c_str());

                if (ImGui::Selectable(label)) {
                    const int ti = CreateSocketTrack(bindingIndex, s.Name);
                    if (ti >= 0) {
                        m_SelectedBinding = bindingIndex;
                        m_SelectedTrack = ti;
                        m_SelectedSection = 0;
                        m_SelectedChannel = m_SelectedKey = -1;
                    }
                    m_AddSocketForBinding = -1;
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("osso pai: %s\noffset: %.2f, %.2f, %.2f",
                        s.BoneName.empty() ? "(nenhum)" : s.BoneName.c_str(),
                        s.Location.x, s.Location.y, s.Location.z);
                }

                ImGui::PopID();
            }

            ImGui::EndChild();
        }

        // ── Criar socket novo ────────────────────────────────────────────────
        ImGui::SeparatorText("Criar socket");

        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##newsockname", "nome (ex: WeaponSocket)",
            m_NewSocketName, sizeof(m_NewSocketName));

        // Combo de osso pai. Sem osso nao ha socket: o socket E um offset em
        // relacao a um osso, e um sem pai nunca se moveria com a animacao.
        const char* boneLabel = (m_NewSocketBone >= 0 &&
            m_NewSocketBone < static_cast<int>(bones.size()))
            ? bones[m_NewSocketBone].Name.c_str()
            : "escolher osso pai...";

        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::BeginCombo("##newsockbone", boneLabel)) {
            for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
                const bool sel = (m_NewSocketBone == i);
                if (ImGui::Selectable(bones[i].Name.c_str(), sel))
                    m_NewSocketBone = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        const std::string newName = m_NewSocketName;
        const bool nameOk = !newName.empty() &&
            (smc->Asset->FindSocket(newName) == nullptr);
        const bool boneOk = (m_NewSocketBone >= 0 &&
            m_NewSocketBone < static_cast<int>(bones.size()));

        if (!newName.empty() && !nameOk) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                ICON_TRIANGLE_EXCLAMATION " ja existe um socket '%s'", newName.c_str());
        }

        ImGui::BeginDisabled(!nameOk || !boneOk);

        if (ui::AccentButton(ICON_PLUS " Criar e adicionar", ui::Accent::Add)) {
            SkeletalMeshAsset::Socket s;
            s.Name = newName;
            s.BoneName = bones[m_NewSocketBone].Name;
            // Nasce na origem do osso. Posicionar de olho e trabalho do
            // Animation Editor, que tem gizmo e preview dedicados — duplicar
            // aquilo aqui seria manter dois editores da mesma coisa.
            s.Location = glm::vec3(0.0f);
            s.Rotation = glm::vec3(0.0f);
            s.Scale = glm::vec3(1.0f);

            smc->Asset->GetSockets().push_back(s);

            if (smc->Asset->Save()) {
                AXE_EDITOR_INFO("Sequencer: socket '{}' criado em '{}' (osso '{}').",
                    s.Name, smc->Asset->GetName(), s.BoneName);
            }
            else {
                AXE_EDITOR_ERROR("Sequencer: socket '{}' criado em memoria, mas o "
                    ".axeskel NAO foi salvo. Salve pelo Animation Editor antes de "
                    "fechar, senao ele se perde.", s.Name);
            }

            const int ti = CreateSocketTrack(bindingIndex, s.Name);
            if (ti >= 0) {
                m_SelectedBinding = bindingIndex;
                m_SelectedTrack = ti;
                m_SelectedSection = 0;
                m_SelectedChannel = m_SelectedKey = -1;
            }

            m_NewSocketName[0] = '\0';
            m_NewSocketBone = -1;
            m_AddSocketForBinding = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("(posicione o offset no Animation Editor)");

        ImGui::EndPopup();
    }

    // ============================================================
    // Toolbar (com icones Font Awesome via ui::IconButton / AccentButton)
    // ============================================================

    void SequencerWindow::DrawToolbar() {
        // ── ARQUIVO ──────────────────────────────────────────────────────────
        //
        // Os dois botoes anteriores caiam num caminho FIXO
        // ("Assets/sequencer.axeseq") sempre que m_LastLoadedPath estava vazio.
        // Na pratica isso queria dizer: uma sequence por projeto, e criar a
        // segunda sobrescrevia a primeira sem perguntar.
        const bool dirty = IsDirty();

        if (ui::IconButton(ICON_FILE, "Nova sequence")) {
            // Confirma so quando ha o que perder. Um dialogo em cima de uma
            // sequence vazia e ruido puro.
            if (dirty) m_OpenConfirmNewPopup = true;
            else       NewSequence();
        }

        ImGui::SameLine();
        if (ui::IconButton(ICON_FOLDER_OPEN, "Abrir .axeseq")) {
            const std::filesystem::path chosen =
                FileDialog::Open(kSeqFilter, "Abrir Sequence", "axeseq");
            if (!chosen.empty()) OpenFile(chosen);
        }

        ImGui::SameLine();

        // O asterisco no rotulo, e o acento so quando ha mudanca: um botao de
        // salvar permanentemente aceso deixa de comunicar qualquer coisa.
        if (ui::AccentButton(dirty ? ICON_SAVE " *" : ICON_SAVE,
            dirty ? ui::Accent::Warning : ui::Accent::Neutral,
            "Salvar (Ctrl+S).\nSem arquivo ainda, pergunta onde.")) {
            SaveInteractive(false);
        }

        ImGui::SameLine();
        if (ui::IconButton(ICON_CLONE, "Salvar como... (Ctrl+Shift+S)")) {
            SaveInteractive(true);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", CurrentFileLabel().c_str(), dirty ? " *" : "");

        // ── HISTORICO ────────────────────────────────────────────────────────
        //
        // Os botoes existem apesar do Ctrl+Z: um undo que so tem atalho e um
        // undo cuja EXISTENCIA metade das pessoas nao descobre — e a pessoa que
        // nao sabe que ha undo trabalha com medo, evitando experimentar.
        ui::ToolbarSeparator();

        ImGui::BeginDisabled(!CanUndo());
        if (ui::IconButton(ICON_UNDO, "Desfazer (Ctrl+Z)"))
            Undo();
        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(!CanRedo());
        if (ui::IconButton(ICON_REDO, "Refazer (Ctrl+Y)"))
            Redo();
        ImGui::EndDisabled();

        ui::ToolbarSeparator();

        // Add Binding — abre o picker de entidade.
        //
        // Antes este botao criava um binding vazio e o alvo real era "o que
        // estiver selecionado no viewport". Agora ele pergunta QUEM entra na
        // sequence, que e a pergunta que o usuario esta fazendo.
        if (ui::IconButton(ICON_PLUS, "Adicionar entidade a sequence", ui::Accent::Add)) {
            m_OpenAddBindingPopup = true;
            m_PickerFilter[0] = '\0';
        }

        // Atalho: adiciona direto a entidade selecionada no viewport, que
        // continua sendo o caminho mais rapido quando ela ja esta na mao.
        if (m_Context && m_Context->HasSelection()) {
            auto& reg = m_Context->ActiveScene->GetRegistry();
            if (reg.try_get<SkeletalMeshComponent>(m_Context->SelectedEntity)) {
                ImGui::SameLine();
                if (ui::IconButton(ICON_PERSON, "Adicionar a entidade selecionada")) {
                    int idx = CreateBindingForEntity(m_Context->SelectedEntity);
                    if (idx >= 0) {
                        m_SelectedBinding = idx;
                        m_SelectedTrack = m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
                    }
                }
            }
        }

        ui::ToolbarSeparator();

        // FPS.
        //
        // DOIS bugs moravam nestas quatro linhas, e juntos eram o motivo de o
        // Play nao mover a agulha:
        //
        //   1. `ImGui::Combo` trabalha com INDICE (0,1,2...), mas o resultado era
        //      gravado como VALOR de fps. Escolher o primeiro item punha
        //      `fps = 0`, e o player faz `frame += deltaTime * fps` — com zero, o
        //      tempo simplesmente nao anda. Nenhum erro, nenhum aviso.
        //
        //   2. A lista era "24\030\060\0120\0". `\030` nao e "\0" seguido de
        //      "3" e "0": e um escape OCTAL, o caractere 24. A lista saia
        //      corrompida — era o "24?0" que aparecia no combo.
        //      Strings separadas e concatenadas evitam a armadilha para sempre.
        static const int kFpsValues[] = { 24, 30, 60, 120 };

        int fpsIdx = 1;   // 30 e o default do asset
        for (int i = 0; i < 4; ++i)
            if (kFpsValues[i] == m_Asset.GetFps()) { fpsIdx = i; break; }

        ImGui::SetNextItemWidth(70);
        if (ImGui::Combo("FPS", &fpsIdx, "24\0" "30\0" "60\0" "120\0")) {
            m_Asset.SetFps(kFpsValues[fpsIdx]);
            m_Player.SetFps(kFpsValues[fpsIdx]);
        }

        ImGui::SameLine();

        // ── TAMANHO DA TIMELINE ──────────────────────────────────────────────
        //
        // O range JA existia no data model (`SequencerFrameRange`, serializado
        // no `.axeseq` desde a v1) e JA era lido pela regua, pelo slider de
        // transporte, pelo clamp do playhead e pelo nascimento de toda section.
        // O que nao existia era um jeito de MUDAR: `{0, 90}` era o default e o
        // unico valor possivel, a nao ser importando um clipe mais longo (o
        // CreateClipTrack estica o range) ou editando o JSON a mao.
        //
        // O modelo e o do Blender: comeca em Start (0 na esmagadora maioria das
        // vezes) e vai ate o frame que o usuario decidir.
        //
        // Nao ha SetFrameRange no player aqui: o `SyncFrom` do inicio do Draw
        // ja copia o range do asset todo frame, e ele proprio reclampa o
        // playhead se o fim encolheu por baixo da agulha. Escrever nos dois
        // lugares criaria duas fontes da verdade para o mesmo numero.
        SequencerFrameRange range = m_Asset.GetFrameRange();
        bool rangeChanged = false;

        ImGui::TextDisabled("Range");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tamanho da timeline: do frame inicial ao final.\n"
                "A regua, o slider de transporte e as sections novas seguem daqui.");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(64);
        if (ImGui::DragInt("##seq_range_start", &range.Start, 1.0f, 0, 100000, "ini %d"))
            rangeChanged = true;
        ImGui::SameLine();

        ImGui::SetNextItemWidth(64);
        if (ImGui::DragInt("##seq_range_end", &range.End, 1.0f, 0, 100000, "fim %d"))
            rangeChanged = true;

        if (rangeChanged) {
            // Start nunca negativo, e End sempre pelo menos um frame adiante.
            //
            // Um range invertido (ou de comprimento zero) nao e um estado
            // "estranho mas inofensivo": o frameWidth da timeline divide por
            // (End - Start), e o loop do player faz fmod pela mesma diferenca.
            // Clampar na entrada e mais barato que blindar os dois consumidores.
            if (range.Start < 0) range.Start = 0;
            if (range.End <= range.Start) range.End = range.Start + 1;

            m_Asset.SetFrameRange(range);

            // A pose pendente foi autorada num frame que pode ter deixado de
            // existir. Ver m_PendingFrame.
            if (m_Player.GetCurrentFrame() < static_cast<float>(range.Start) ||
                m_Player.GetCurrentFrame() > static_cast<float>(range.End))
                ClearPendingEdits();
        }

        ImGui::SameLine();

        // Snap toggle (ToggleButton com icone de ima).
        if (ui::ToggleButton(ICON_MAGNET " Snap", m_SnapEnabled, "Snap pra frames inteiros")) {
            m_SnapEnabled = !m_SnapEnabled;
        }
        if (m_SnapEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            ImGui::InputInt("##snap", &m_SnapFrame, 1, 5);
            if (m_SnapFrame < 1) m_SnapFrame = 1;
        }

        // ── ZOOM ─────────────────────────────────────────────────────────────
        //
        // Os botoes existem apesar de Ctrl+roda ser mais rapido: atalho e coisa
        // que se aprende, e um zoom que so tem atalho e um zoom que metade das
        // pessoas nao descobre que existe. O rotulo com o valor tambem responde
        // "por que a timeline esta assim" sem precisar experimentar.
        ui::ToolbarSeparator();

        // ── DOPE / CURVAS ────────────────────────────────────────────────────
        //
        // Duas perguntas sobre a mesma coisa: "quando acontece" e "como
        // acontece". O dope sheet responde a primeira, o grafico a segunda.
        ui::ToolbarSeparator();

        if (ui::ToggleButton(m_CurveMode ? ICON_FUNCTION " Curvas" : ICON_TABLE_CELLS " Dope",
            m_CurveMode,
            "Alterna entre o dope sheet (quando cada key acontece) e o\n"
            "grafico de curvas (como o valor caminha entre elas).\n\n"
            "No grafico: roda = zoom vertical, arrastar key move em tempo E\n"
            "valor, e as keys em Bezier ganham alcas 2D.")) {
            m_CurveMode = !m_CurveMode;
            m_CurveFitPending = true;
        }

        if (m_CurveMode) {
            // Filtros por grupo. Tres alvos selecionados dao 27 curvas
            // sobrepostas, e quase sempre so uma delas interessa.
            ImGui::SameLine();
            if (ui::ToggleButton("T", m_CurveShowT, "Curvas de translacao"))
                m_CurveShowT = !m_CurveShowT;

            ImGui::SameLine();
            if (ui::ToggleButton("R", m_CurveShowR, "Curvas de rotacao"))
                m_CurveShowR = !m_CurveShowR;

            ImGui::SameLine();
            if (ui::ToggleButton("S", m_CurveShowS, "Curvas de escala"))
                m_CurveShowS = !m_CurveShowS;

            ImGui::SameLine();
            if (ui::IconButton(ICON_EXPAND, "Enquadrar as curvas visiveis"))
                m_CurveFitPending = true;
        }

        ui::ToolbarSeparator();

        if (ui::IconButton(ICON_MINUS, "Zoom out (Ctrl + roda)")) {
            m_Zoom = std::clamp(m_Zoom / 1.4f, 1.0f, 80.0f);
        }
        ImGui::SameLine();
        if (ui::IconButton(ICON_PLUS, "Zoom in (Ctrl + roda)")) {
            m_Zoom = std::clamp(m_Zoom * 1.4f, 1.0f, 80.0f);
        }
        ImGui::SameLine();
        if (ui::IconButton(ICON_EXPAND, "Enquadrar a sequence inteira")) {
            m_Zoom = 1.0f;
            m_TimelineScrollX = 0.0f;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f%%", m_Zoom * 100.0f);

        // ── GIZMO ────────────────────────────────────────────────────────────
        //
        // Desligavel porque o gizmo externo SUPRIME o de entidade: quem estiver
        // com o Sequencer aberto e quiser mover o personagem inteiro no viewport
        // precisa de um jeito de sair do caminho sem fechar a janela.
        ui::ToolbarSeparator();

        if (ui::ToggleButton(ICON_ARROWS " Gizmo", m_GizmoEnabled,
            "Gizmo no viewport para o osso/socket/controle da track selecionada.\n"
            "T/R/S do viewport escolhem a operacao - e so a operacao ativa vira key.\n"
            "Local/World do viewport escolhem o espaco dos eixos.\n"
            "Desligado, o gizmo volta a ser o da entidade selecionada.")) {
            m_GizmoEnabled = !m_GizmoEnabled;
        }

        ImGui::SameLine();

        // ── CONTROLES NO VIEWPORT ────────────────────────────────────────────
        //
        // Desligavel porque as formas ficam POR CIMA da imagem, sem teste de
        // profundidade (e de proposito — um controle escondido atras da perna
        // e um controle que nao se consegue clicar). Vinte delas atrapalham
        // quem esta olhando a cena, e nao o personagem.
        if (ui::ToggleButton(ICON_CIRCLE_NODES " Controles", m_ShowViewportControls,
            "Desenha as formas dos Controls do rig no viewport, como no editor\n"
            "de Control Rig. Clique numa forma para seleciona-la.\n\n"
            "Ctrl (ou Shift) + clique SOMA ao grupo. Com varios selecionados,\n"
            "todos recebem a mesma transformacao local, cada um no proprio\n"
            "pivo - e o que faz uma cadeia de spine arquear em vez de girar\n"
            "como um bloco.\n\n"
            "Selecionar NAO cria track: ela nasce na primeira vez que voce\n"
            "move, gira ou escala o controle.")) {
            m_ShowViewportControls = !m_ShowViewportControls;

            if (!m_ShowViewportControls) {
                m_ViewportControlBinding = -1;
                m_ViewportControlName.clear();
                m_ViewportHovered.clear();
            }
        }

        // ── REC ──────────────────────────────────────────────────────────────
        //
        // Ver a nota longa em `m_Recording` no header. Ligar o REC COMETE o que
        // estiver pendente, em vez de descartar: quem posou e depois apertou
        // REC quis guardar aquilo — descartar seria perder trabalho por causa
        // da ordem em que dois botoes foram clicados.
        ui::ToolbarSeparator();

        if (ui::ToggleButton(ICON_FILM " REC", m_Recording,
            "GRAVANDO: toda mudanca no viewport vira keyframe no frame atual.\n"
            "Desligado: a mudanca aparece na tela mas NAO e gravada - use o\n"
            "botao Key (ou a tecla K) para cravar a pose quando ela estiver boa.",
            ui::Accent::Danger)) {
            m_Recording = !m_Recording;
            if (m_Recording) CommitPendingEdits();
        }

        // Ponto vermelho pulsando ao lado do botao. Um toggle aceso e facil de
        // nao notar quando a atencao esta no viewport, e "por que apareceu uma
        // key aqui?" e uma pergunta cara de responder depois.
        if (m_Recording) {
            ImGui::SameLine();
            const float t = static_cast<float>(ImGui::GetTime());
            const float pulse = 0.55f + 0.45f * std::sin(t * 6.0f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float r = ImGui::GetTextLineHeight() * 0.30f;

            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(p.x + r + 2.0f, p.y + ImGui::GetFrameHeight() * 0.5f), r,
                IM_COL32(230, 60, 60, static_cast<int>(120 + 135 * pulse)));

            ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, ImGui::GetFrameHeight()));
        }

        ImGui::SameLine();

        // Key manual. Existe com REC ligado tambem — e o jeito de cravar uma
        // key sem tocar no gizmo (segurar a pose por N frames, por exemplo).
        {
            const bool hasPending = !m_PendingEdits.empty();

            char keyLabel[64];
            std::snprintf(keyLabel, sizeof(keyLabel), ICON_PLUS " Key%s",
                hasPending ? "*" : "");

            if (ui::AccentButton(keyLabel,
                hasPending ? ui::Accent::Warning : ui::Accent::Neutral,
                "Crava a pose atual como keyframe no frame do playhead (K).\n"
                "O asterisco avisa que ha pose na tela ainda nao gravada.")) {
                if (CommitPendingEdits() == 0)
                    CaptureAllTracksAtPlayhead(m_SelectedBinding);
            }
        }

        ImGui::SameLine();

        if (ui::IconButton(ICON_CAMERA, "Capturar TODAS as tracks deste binding no\n"
            "frame atual, com o valor que elas tem na tela.")) {
            CaptureAllTracksAtPlayhead(m_SelectedBinding);
        }

        // ── BAKE ─────────────────────────────────────────────────────────────
        //
        // A saida da ferramenta. Ate aqui a sequence so existia como sequence:
        // para usar a animacao num AnimGraph, ou no jogo, nao havia caminho.
        ui::ToolbarSeparator();

        if (ui::AccentButton(ICON_FILM " Bake", ui::Accent::Add,
            "Assar a sequence num clipe de animacao (.axeclipbin) e registra-lo\n"
            "no .axeskel do personagem.\n\n"
            "Amostra a pose FINAL frame a frame - clipe base, tracks de osso e\n"
            "o solve do Control Rig, tudo achatado em curvas de osso.")) {
            m_OpenBakePopup = true;
            m_BakeBinding = m_SelectedBinding;
            m_BakeClipName[0] = '\0';   // renomeia a sugestao pelo binding atual
        }

        ImGui::Separator();

        // Faixa de aviso: pose na tela que nao existe no arquivo. Sem ela, o
        // sintoma seria "eu movi o braco, salvei, reabri e o braco voltou".
        if (!m_PendingEdits.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::AccentColor(ui::Accent::Warning));
            ImGui::TextUnformatted(ICON_TRIANGLE_EXCLAMATION
                "  Pose nao gravada (REC desligado) - K crava, Esc descarta.");
            ImGui::PopStyleColor();
        }
    }

    // ============================================================
    // Outliner
    // ============================================================

    void SequencerWindow::DrawOutliner() {
        ImGui::TextDisabled("Outliner");
        ImGui::Separator();

        // ── Barra de status ──────────────────────────────────────────────
        //
        // Tres numeros que respondem "por que nao acontece nada?" sem abrir o
        // debugger: quantos bindings a janela conseguiu resolver e escrever,
        // quantos canais estao produzindo valor no frame atual, e a qual fps o
        // tempo esta correndo (fps zero foi um bug real, e mudo).
        {
            const int bindings = static_cast<int>(m_Asset.GetBindingCount());
            // A lista EFETIVA (keys + pose pendente), e nao a do player: com REC
            // desligado o que esta na tela inclui edicoes que ainda nao viraram
            // key, e um diagnostico que nao as conta contradiz o viewport.
            const int samples = static_cast<int>(m_EffectiveSamples.size());

            const bool healthy = (bindings > 0 && m_AppliedBindings == bindings);

            ImGui::TextColored(healthy ? ImVec4(0.4f, 0.85f, 0.6f, 1.0f)
                : ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                ICON_CIRCLE_NODES " %d/%d binding(s) aplicado(s)",
                m_AppliedBindings, bindings);

            const int clips = static_cast<int>(m_Player.GetLastClipSamples().size());

            ImGui::SameLine();
            ImGui::TextDisabled("| %d sample(s) | %d clipe(s) | %d fps",
                samples, clips, m_Asset.GetFps());

            if (!m_SelectedKeys.empty()) {
                ImGui::TextColored(ImVec4(0.47f, 0.75f, 1.0f, 1.0f),
                    "%d key(s) selecionada(s)  |  Del apaga  |  Ctrl+C/X/V  |  Ctrl+D",
                    (int)m_SelectedKeys.size());
            }

            if (!m_KeyClipboard.empty()) {
                ImGui::TextDisabled("%d key(s) na area de transferencia - "
                    "Ctrl+V cola no playhead", (int)m_KeyClipboard.size());
            }

            if (bindings > 0 && m_AppliedBindings == 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                    "A entidade do binding nao foi encontrada na cena.");
            }
            else if (m_AppliedBindings > 0 && samples == 0 && clips == 0) {
                ImGui::TextDisabled("Sem clipe e sem key - a pose fica a de repouso.");
            }

            // O unico erro que TODO o resto do diagnostico dava como saudavel:
            // escrevemos a pose e alguem a reescreveu antes do proximo frame.
            if (m_PaletteStolen) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    ICON_TRIANGLE_EXCLAMATION " A pose esta sendo sobrescrita.");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "O BonePalette mudou entre dois frames do Sequencer.\n"
                        "Quase sempre significa que falta o guard em\n"
                        "src/axe/animation/animation_world.cpp:\n\n"
                        "    if (skel.PoseOverride)\n"
                        "        continue;\n\n"
                        "logo apos o teste de esqueleto vazio.");
                }
            }

            // Lista crua do que esta sendo escrito neste frame. Quando o osso nao
            // se mexe, a primeira pergunta e sempre "o valor certo chegou no osso
            // certo?" — e esta linha responde sem debugger.
            if (samples > 0 && ImGui::TreeNodeEx("Samples do frame",
                ImGuiTreeNodeFlags_SpanAvailWidth)) {
                const auto& ss = m_EffectiveSamples;
                const int shown = (int)ss.size() > 12 ? 12 : (int)ss.size();
                for (int i = 0; i < shown; ++i) {
                    ImGui::TextDisabled("%s . %s = %.3f",
                        ss[i].TargetName.c_str(),
                        SequencerChannelComponentToString(ss[i].Component),
                        ss[i].Value);
                }
                if ((int)ss.size() > shown)
                    ImGui::TextDisabled("... +%d", (int)ss.size() - shown);
                ImGui::TreePop();
            }
        }
        ImGui::Separator();

        int bindingCount = static_cast<int>(m_Asset.GetBindingCount());
        if (bindingCount == 0) {
            ImGui::TextWrapped("Nenhum binding. Clique em " ICON_PLUS " (Add Binding) na toolbar.");
            ImGui::Separator();
            DrawSelectedKeyPanel();
            return;
        }

        // ── FILTRO DE TRACK ──────────────────────────────────────────────────
        //
        // Vale para todos os bindings: procurar "LeftHand" e uma pergunta sobre
        // a cena, nao sobre um personagem. Enquanto ha texto aqui, todos os
        // grupos ficam abertos — filtrar e nao ver resultado porque o grupo
        // estava dobrado seria o pior dos dois mundos.
        ImGui::SetNextItemWidth(-30.0f);
        ImGui::InputTextWithHint("##trackfilter",
            ICON_MAGNIFYING_GLASS "  filtrar osso / controle / socket...",
            m_TrackFilter, sizeof(m_TrackFilter));

        if (m_TrackFilter[0] != '\0') {
            ImGui::SameLine();
            if (ui::IconButton(ICON_XMARK, "Limpar filtro"))
                m_TrackFilter[0] = '\0';
        }

        ImGui::Separator();

        for (int bi = 0; bi < bindingCount; ++bi) {
            DrawBindingNode(bi);
        }

        DrawSelectedKeyPanel();
    }

    // ============================================================
    // Grupos, filtro e ordem de hierarquia
    // ============================================================

    SequencerWindow::TrackGroup SequencerWindow::GroupOf(const SequencerTrack& track) {
        // Controle de CANAL e uma track `Property`, mas para o animador ele e um
        // controle do rig como qualquer outro — procurar "Use_Control" em um
        // grupo "Outras" nao passaria pela cabeca de ninguem. O alvo manda.
        if (track.TargetType == SequencerTargetType::Control)
            return TrackGroup::Control;

        if (track.TargetType == SequencerTargetType::Entity)
            return TrackGroup::Entity;

        switch (track.Type) {
        case SequencerTrackType::Event:            return TrackGroup::Event;
        case SequencerTrackType::CameraCut:        return TrackGroup::Cut;
        case SequencerTrackType::AnimationClip:    return TrackGroup::Clip;
        case SequencerTrackType::TransformSocket:  return TrackGroup::Socket;
        case SequencerTrackType::TransformControl: return TrackGroup::Control;
        case SequencerTrackType::TransformBone:    return TrackGroup::Bone;
        default:                                   return TrackGroup::Other;
        }
    }

    const char* SequencerWindow::GroupLabel(TrackGroup g) {
        switch (g) {
        case TrackGroup::Entity:  return "Transform";
        case TrackGroup::Clip:    return "Animacoes";
        case TrackGroup::Socket:  return "Sockets";
        case TrackGroup::Control: return "Controles";
        case TrackGroup::Bone:    return "Ossos";
        case TrackGroup::Event:   return "Eventos";
        case TrackGroup::Cut:     return "Cortes";
        default:                  return "Outras";
        }
    }

    const char* SequencerWindow::GroupIcon(TrackGroup g) {
        switch (g) {
        case TrackGroup::Entity:  return ICON_ARROWS;
        case TrackGroup::Clip:    return ICON_FILM;
        case TrackGroup::Socket:  return ICON_LINK;
        case TrackGroup::Control: return ICON_CIRCLE_NODES;
        case TrackGroup::Bone:    return ICON_BONE;
        case TrackGroup::Event:   return ICON_BOLT;
        case TrackGroup::Cut:     return ICON_CAMERA;
        default:                  return ICON_LIST;
        }
    }

    bool SequencerWindow::IsGroupOpen(int bindingIndex, TrackGroup g) const {
        const std::string key = std::to_string(bindingIndex) + "|" +
            std::to_string(static_cast<int>(g));

        auto it = m_GroupOpen.find(key);
        if (it != m_GroupOpen.end()) return it->second;

        // Default por grupo, e nao "tudo aberto".
        //
        // Ossos comeca FECHADO: e o grupo que explode para cinquenta e poucas
        // linhas, e quem acabou de explodir um clipe quer ver que existem, nao
        // percorre-las. Os outros sao curtos e abrem.
        return (g != TrackGroup::Bone);
    }

    void SequencerWindow::SetGroupOpen(int bindingIndex, TrackGroup g, bool open) {
        const std::string key = std::to_string(bindingIndex) + "|" +
            std::to_string(static_cast<int>(g));
        m_GroupOpen[key] = open;
    }

    int SequencerWindow::ControlDepth(int bindingIndex, const std::string& controlName) {
        RigRuntime* rt = EnsureRig(bindingIndex);
        if (!rt || !rt->Ready) return 0;

        int idx = rt->Hierarchy.Find(controlName, RigElementType::Control);
        if (idx < 0) return 0;

        int depth = 0;
        // A invariante do RigHierarchy garante Parent < indice, entao a subida
        // termina. O teto de 64 e so contra um `.axerig` corrompido a mao.
        while (depth < 64) {
            const int parent = rt->Hierarchy[idx].Parent;
            if (parent < 0 || parent >= static_cast<int>(rt->Hierarchy.Size())) break;
            idx = parent;
            ++depth;
        }
        return depth;
    }

    void SequencerWindow::RebuildLayout() {
        const int bindingCount = static_cast<int>(m_Asset.GetBindingCount());
        m_Layout.assign(bindingCount, BindingLayout{});

        // Filtro em minusculas uma vez, e nao por track: com 52 tracks x N
        // bindings, converter dentro do laco e trabalho repetido a cada frame.
        std::string needle = m_TrackFilter;
        std::transform(needle.begin(), needle.end(), needle.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        const bool filtering = !needle.empty();

        for (int bi = 0; bi < bindingCount; ++bi) {
            auto* b = m_Asset.GetBinding(bi);
            if (!b) continue;

            BindingLayout& L = m_Layout[bi];

            const Skeleton* skel = GetBindingSkeleton(bi);
            RigRuntime* rt = b->RigAssetUUID.empty() ? nullptr : EnsureRig(bi);

            // (chave de ordem, indice da track), por grupo.
            std::vector<std::pair<int, int>> sorted[static_cast<int>(TrackGroup::Count)];

            for (int ti = 0; ti < static_cast<int>(b->Tracks.size()); ++ti) {
                const auto& tr = b->Tracks[ti];

                if (filtering) {
                    std::string name = tr.TargetName;
                    std::transform(name.begin(), name.end(), name.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (name.find(needle) == std::string::npos) continue;
                }

                const TrackGroup g = GroupOf(tr);

                // ── ORDEM DE HIERARQUIA ──────────────────────────────────────
                //
                // A ordem de criacao e a ordem em que o assimp entregou os
                // canais: util para ninguem. Tanto o Skeleton quanto o
                // RigHierarchy garantem ordem TOPOLOGICA (pai antes do filho),
                // entao ordenar pelo indice deles reproduz a hierarquia que o
                // autor montou — quadril, coluna, ombro, braco, mao, dedos.
                int key = ti;

                if (g == TrackGroup::Bone && skel) {
                    const int bone = skel->FindBone(tr.TargetName);
                    key = (bone >= 0) ? bone : 100000 + ti;   // orfao vai pro fim
                }
                else if (g == TrackGroup::Control && rt && rt->Ready) {
                    const int el = rt->Hierarchy.Find(tr.TargetName, RigElementType::Control);
                    key = (el >= 0) ? el : 100000 + ti;
                }

                sorted[static_cast<int>(g)].emplace_back(key, ti);
            }

            for (int g = 0; g < static_cast<int>(TrackGroup::Count); ++g) {
                std::sort(sorted[g].begin(), sorted[g].end());

                for (const auto& [k, ti] : sorted[g])
                    L.Groups[g].push_back(ti);

                // Filtro ativo ABRE os grupos. Filtrar e ver zero resultados
                // porque o grupo estava dobrado seria o pior dos dois mundos.
                const bool open = filtering ||
                    IsGroupOpen(bi, static_cast<TrackGroup>(g));

                if (open) {
                    for (const auto& [k, ti] : sorted[g])
                        L.Visible.push_back(ti);
                }
            }
        }
    }

    void SequencerWindow::DrawTrackGroupNode(int bindingIndex, TrackGroup g) {
        if (bindingIndex < 0 || bindingIndex >= static_cast<int>(m_Layout.size())) return;

        const auto& list = m_Layout[bindingIndex].Groups[static_cast<int>(g)];
        if (list.empty()) return;

        const bool filtering = (m_TrackFilter[0] != '\0');
        const bool open = filtering || IsGroupOpen(bindingIndex, g);

        ImGui::PushID(1000 + static_cast<int>(g));

        char label[128];
        std::snprintf(label, sizeof(label), "%s %s  (%d)",
            GroupIcon(g), GroupLabel(g), static_cast<int>(list.size()));

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick;

        // Cond_Always: quem manda no aberto/fechado e o m_GroupOpen, nao o
        // estado interno do ImGui. Sem isto, o filtro nao conseguiria forcar a
        // abertura, e o estado se perderia quando a arvore fosse reconstruida.
        ImGui::SetNextItemOpen(open, ImGuiCond_Always);
        const bool nodeOpen = ImGui::TreeNodeEx(label, flags);

        // UM if para os dois gestos. O triangulo dispara IsItemToggledOpen; o
        // rotulo (com OpenOnArrow) dispara so IsItemClicked. Em ifs separados,
        // um clique que disparasse os dois alternaria duas vezes e nao faria
        // nada — e o rotulo e o alvo maior, que e onde as pessoas clicam.
        if (!filtering && (ImGui::IsItemToggledOpen() || ImGui::IsItemClicked()))
            SetGroupOpen(bindingIndex, g, !open);

        if (filtering && ImGui::IsItemHovered())
            ImGui::SetTooltip("Grupos ficam abertos enquanto ha filtro.");

        if (nodeOpen) {
            for (int ti : list)
                DrawTrackNode(bindingIndex, ti);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    int SequencerWindow::AddAllControlTracks(int bindingIndex) {
        RigRuntime* rt = EnsureRig(bindingIndex);
        if (!rt || !rt->Ready) {
            AXE_EDITOR_ERROR("Sequencer: nenhum Control Rig resolvido neste binding.");
            return 0;
        }

        int created = 0;

        // Na ordem da hierarquia — que e a ordem em que o autor montou o rig.
        // CreateControlTrack devolve a existente em vez de duplicar, entao
        // chamar isto duas vezes e inofensivo.
        for (int i = 0; i < static_cast<int>(rt->Hierarchy.Size()); ++i) {
            const RigElement& el = rt->Hierarchy[i];

            if (el.Type != RigElementType::Control) continue;

            // Controles de CANAL entram tambem. Eles nao tem forma no viewport,
            // mas sao o que liga e desliga o resto do rig — deixa-los de fora
            // obrigaria a voltar ao Control Rig so para acionar um interruptor,
            // e o valor nem seria animavel.

            const auto* b = m_Asset.GetBinding(bindingIndex);
            if (!b) break;

            bool exists = false;
            for (const auto& tr : b->Tracks) {
                if (tr.TargetType == SequencerTargetType::Control &&
                    tr.TargetName == el.Name) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;

            if (CreateControlTrack(bindingIndex, el.Name) >= 0) ++created;
        }

        AXE_EDITOR_INFO("Sequencer: {} track(s) de controle criada(s).", created);
        return created;
    }

    void SequencerWindow::DrawBindingNode(int bindingIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return;

        ImGui::PushID(bindingIndex);

        char label[128];
        std::snprintf(label, sizeof(label), "%s [%d track%s]",
            b->DisplayName.empty() ? "Binding" : b->DisplayName.c_str(),
            static_cast<int>(b->Tracks.size()),
            b->Tracks.size() == 1 ? "" : "s");

        bool selected = (m_SelectedBinding == bindingIndex);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick |
            (selected ? ImGuiTreeNodeFlags_Selected : 0);

        bool open = ImGui::TreeNodeEx(label, flags);
        if (ImGui::IsItemClicked()) {
            m_SelectedBinding = bindingIndex;
            m_SelectedTrack = m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
        }

        if (open) {
            // Estado do vinculo — a informacao que faltava no outliner.
            //
            // Um binding que aponta para uma entidade que sumiu (renomeada,
            // deletada, ou cena trocada) continuava desenhando tracks como se
            // estivesse tudo bem. Dizer isso aqui e a diferenca entre "nao
            // funciona" e "o alvo sumiu".
            {
                const entt::entity e = ResolveBindingEntity(*b);
                if (e == entt::null) {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                        ICON_TRIANGLE_EXCLAMATION " entidade '%s' nao encontrada",
                        b->EntityName.empty() ? "(vazio)" : b->EntityName.c_str());
                    ImGui::SameLine();
                    if (ui::IconButton(ICON_PERSON, "Religar na entidade selecionada")) {
                        if (m_Context && m_Context->HasSelection()) {
                            auto& reg2 = m_Context->ActiveScene->GetRegistry();
                            if (auto* nm = reg2.try_get<NameComponent>(m_Context->SelectedEntity))
                                b->EntityName = nm->Name;
                        }
                    }
                }
                else if (const Skeleton* sk = GetBindingSkeleton(bindingIndex)) {
                    ImGui::TextDisabled(ICON_BONE " %s - %d ossos",
                        b->EntityName.c_str(), (int)sk->GetBoneCount());
                }
            }

            // ── CONTROL RIG DO BINDING ───────────────────────────────────────
            //
            // O slot fica AQUI, no binding, e nao numa track: o rig e do
            // personagem, e todas as tracks de controle falam dele. Uma sequence
            // com dois personagens tem dois rigs, um por binding — que e
            // exatamente o que `SequencerBinding::RigAssetUUID` sempre quis
            // dizer.
            //
            // Ele nao precisa estar NA CENA. Um `.axerig` no projeto basta: a
            // janela clona a hierarquia e o grafo e roda o Forwards Solve por
            // conta propria, como o AnimNode_ControlRig faz por instancia. Nada
            // e adicionado ao `.axescene`.
            {
                std::string rigUUID = b->RigAssetUUID;

                if (AssetPicker::Draw("Control Rig", rigUUID,
                    { AssetType::ControlRig }, [](const AssetRecord&) {})) {
                    // Nada aqui: o EnsureRig do proximo frame ve o UUID novo e
                    // reclona sozinho.
                }

                if (rigUUID != b->RigAssetUUID) {
                    b->RigAssetUUID = rigUUID;
                    m_Rigs.erase(bindingIndex);   // forca reclonagem
                }

                if (!b->RigAssetUUID.empty()) {
                    if (RigRuntime* rt = EnsureRig(bindingIndex)) {
                        int controls = 0;
                        int channels = 0;
                        int followers = 0;

                        for (int i = 0; i < (int)rt->Hierarchy.Size(); ++i) {
                            const RigElement& el = rt->Hierarchy[i];
                            if (el.Type != RigElementType::Control) continue;
                            ++controls;
                            if (el.ValueType != RigControlValue::Transform) ++channels;
                            if (!el.SourceBone.empty()) ++followers;
                        }

                        ImGui::TextDisabled(ICON_CIRCLE_NODES
                            " %d controle(s), %d canal(is)", controls, channels);

                        // ── O AJUSTE QUE MUDA TUDO ───────────────────────────
                        bool follow = b->RigControlsFollowAnimation;
                        if (ImGui::Checkbox("Controles seguem a animacao", &follow))
                            b->RigControlsFollowAnimation = follow;

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "Ligado: antes do solve, todo controle que nasceu de um osso\n"
                                "        vai para onde esse osso esta NA ANIMACAO. Subir o peso\n"
                                "        de um FK Chain passa a reproduzir a animacao, e o seu\n"
                                "        ajuste entra por cima dela.\n\n"
                                "Desligado: os controles ficam no repouso do rig (T-pose), que\n"
                                "        e o comportamento do preview do Control Rig. E o que\n"
                                "        faz o membro saltar quando o Use_Control liga.");
                        }

                        // ── DIAGNOSTICO ──────────────────────────────────────
                        //
                        // Desligado por padrao: custa uma copia de Pose por
                        // frame, e so serve quando algo esta errado.
                        ImGui::Checkbox("Diagnostico do solve", &m_RigDiagnostics);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "Compara a pose que ENTRA no rig com a que SAI.\n"
                                "Sem controle posado, o rig deveria ser transparente -\n"
                                "todo osso que aparecer na lista o grafo esta mexendo\n"
                                "por conta propria.");
                        }

                        if (m_RigDiagnostics) {
                            ImGui::Indent(12.0f);

                            // ── AUDITORIA DO GRAFO ───────────────────────────
                            //
                            // Roda por frame; e leitura de alguns vetores, e so
                            // acontece com o diagnostico ligado.
                            AuditRigGraph(bindingIndex);

                            if (m_RigAudit.empty()) {
                                // "Nenhum problema" NAO e a mesma coisa que
                                // "esta tudo certo": a leitura estatica pula
                                // pino alimentado por fio e lista que nao venha
                                // de um Item Array. Dizer isso evita que o
                                // silencio seja lido como aprovacao — que foi
                                // exatamente o que aconteceu na primeira vez.
                                ImGui::TextDisabled("grafo: nada legivel fora de ordem");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip(
                                        "A leitura estatica so enxerga pino com valor digitado\n"
                                        "e lista vinda de um no Item Array. Pino ligado por fio\n"
                                        "ela pula - entao isto nao e um atestado.\n\n"
                                        "O rastreio no a no, abaixo, nao depende de ler nada.");
                                }
                            }
                            else {
                                int severe = 0;
                                for (const auto& is : m_RigAudit) if (is.Severe) ++severe;

                                ImGui::TextColored(
                                    severe > 0 ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                    : ImVec4(0.58f, 0.58f, 0.58f, 1.0f),
                                    "%s grafo: %d problema(s), %d informativo(s)",
                                    severe > 0 ? ICON_TRIANGLE_EXCLAMATION : ICON_CIRCLE_INFO,
                                    severe, (int)m_RigAudit.size() - severe);

                                for (const auto& is : m_RigAudit) {
                                    // Informativo em cinza, problema em ambar. A
                                    // lista mistura os dois de proposito: o
                                    // contexto ("le X, escreve Y") e o que torna
                                    // o aviso acionavel.
                                    ImGui::TextColored(
                                        is.Severe ? ImVec4(1.0f, 0.65f, 0.30f, 1.0f)
                                        : ImVec4(0.58f, 0.58f, 0.58f, 1.0f),
                                        "  [%s / %s] %s",
                                        is.Where.c_str(), is.Node.c_str(), is.Message.c_str());
                                }
                            }

                            ImGui::Separator();

                            // ── ESPELHAMENTO, SO PARA CONTROLE DE FK ─────────
                            //
                            // O FK Chain copia o LOCAL do controle para o osso, e
                            // local so quer dizer a mesma coisa dos dois lados se
                            // o pai do controle for o par do pai do osso.
                            //
                            // Isso vale SO para controle que representa o osso —
                            // aquele montado exatamente em cima dele. Um IK
                            // effector ou um pole vector e pendurado na raiz DE
                            // PROPOSITO e lido em GLOBAL pelo Two Bone IK;
                            // cobrar hierarquia dele era ruido, e enterrava o
                            // aviso que importa no meio de seis que nao.
                            //
                            // ── E QUEM DECIDE ISSO E O GRAFO, NAO O NOME ─────
                            //
                            // Filtrar por "offset ate o osso e quase nulo" nao
                            // bastou: um IK_LeftFoot fica exatamente em cima do
                            // pe, entao passava no filtro e virava aviso — tres
                            // deles, sobre controles pendurados na raiz de
                            // proposito.
                            //
                            // A pergunta certa e se ALGUEM COPIA O LOCAL desse
                            // controle. Isso esta escrito no grafo: um FK Chain
                            // em Local, ou um Set Transform em Local lendo um
                            // Get Transform em Local. Se nenhum no faz isso com
                            // este controle, o pai dele nao interessa.
                            std::set<std::string> localCopied;
                            {
                                auto scan = [&](const RigGraph& g) {
                                    for (const auto& np : g.GetNodes()) {
                                        if (!np) continue;

                                        // No morto nao copia nada.
                                        bool reached = false;
                                        for (const auto& e : g.GetExecLinks())
                                            if (e.ToNode == np->Id) { reached = true; break; }
                                        if (!reached) continue;

                                        const std::string t = np->TypeName();

                                        if (t == "FKChain") {
                                            if (static_cast<const RigNode_FKChain*>(np.get())->Space
                                                != RigSpace::Local) continue;

                                            for (const auto& lk : g.GetDataLinks()) {
                                                if (lk.ToNode != np->Id || lk.ToPin != 1) continue;
                                                for (const auto& s : g.GetNodes())
                                                    if (s && s->Id == lk.FromNode &&
                                                        std::string(s->TypeName()) == "ItemArray")
                                                        for (const auto& it :
                                                            static_cast<const RigNode_ItemArray*>(s.get())->Items)
                                                            localCopied.insert(it.Name);
                                            }
                                            continue;
                                        }

                                        if (t != "SetTransform") continue;
                                        if (static_cast<const RigNode_SetTransform*>(np.get())->Space
                                            != RigSpace::Local) continue;

                                        for (const auto& lk : g.GetDataLinks()) {
                                            if (lk.ToNode != np->Id || lk.ToPin != 1) continue;
                                            for (const auto& s : g.GetNodes()) {
                                                if (!s || s->Id != lk.FromNode) continue;
                                                if (std::string(s->TypeName()) != "GetTransform") continue;
                                                if (static_cast<const RigNode_GetTransform*>(s.get())->Space
                                                    != RigSpace::Local) continue;
                                                if (!s->Inputs.empty())
                                                    localCopied.insert(s->Inputs[0].Default.ItemName);
                                            }
                                        }
                                    }
                                    };

                                scan(rt->Graph);
                                for (const auto& f : rt->Functions) scan(f.second);
                            }

                            int broken = 0;

                            for (int i = 0; i < (int)rt->Hierarchy.Size(); ++i) {
                                const RigElement& el = rt->Hierarchy[i];
                                if (el.Type != RigElementType::Control) continue;
                                if (el.SourceBone.empty()) continue;

                                // Ninguem copia o local dele -> o pai nao importa.
                                if (!localCopied.count(el.Name)) continue;

                                const int bone = rt->Hierarchy.Find(el.SourceBone,
                                    RigElementType::Bone);
                                if (bone < 0) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                        "%s: osso '%s' nao existe no rig",
                                        el.Name.c_str(), el.SourceBone.c_str());
                                    ++broken;
                                    continue;
                                }

                                // Controle de FK = offset praticamente nulo ate o
                                // osso. Alvo de IK / pole vector vive deslocado.
                                const glm::mat4 off =
                                    glm::inverse(rt->Hierarchy.GetInitialGlobal(bone))
                                    * rt->Hierarchy.GetInitialGlobal(i);

                                if (glm::length(glm::vec3(off[3])) > 0.01f) continue;

                                const int boneParent = rt->Hierarchy[bone].Parent;
                                const int ctrlParent = el.Parent;

                                if (boneParent < 0 || ctrlParent < 0) continue;

                                const std::string& expected = rt->Hierarchy[boneParent].Name;
                                const std::string& got = rt->Hierarchy[ctrlParent].SourceBone;

                                // ── NOME DIFERENTE NAO E LUGAR DIFERENTE ─────
                                //
                                // O que quebra o FK e o pai do controle estar
                                // em outro LUGAR, nao com outro nome. Um
                                // `ctrl_RootNode` criado na origem e o
                                // `RootNode` do FBX, os dois identidade, sao o
                                // mesmo referencial — e o topo de qualquer rig
                                // gerado a partir do esqueleto, ou seja, o caso
                                // COMUM, nao a excecao.
                                //
                                // Cobrar o nome aqui acusava esse caso todo
                                // frame. O aviso era falso, e falso o bastante
                                // pra mandar trocar o Space de nos que estavam
                                // certos — foi o que aconteceu.
                                if (rt->Hierarchy.SameInitialFrame(ctrlParent, boneParent))
                                    continue;

                                if (got != expected) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                                        "%s: pai e '%s', mas o pai do osso e '%s'",
                                        el.Name.c_str(),
                                        rt->Hierarchy[ctrlParent].Name.c_str(),
                                        expected.c_str());
                                    ++broken;
                                }
                            }

                            if (broken == 0) {
                                if (localCopied.empty())
                                    ImGui::TextDisabled("nenhum no copia LOCAL de controle - "
                                        "a hierarquia dos controles nao interfere");
                                else
                                    ImGui::TextDisabled("os %d controle(s) copiados em LOCAL "
                                        "espelham a hierarquia dos ossos",
                                        (int)localCopied.size());
                            }

                            ImGui::Separator();

                            if (m_RigPosedControls > 0) {
                                ImGui::TextDisabled("%d controle(s) posado(s) - parte do desvio "
                                    "abaixo e seu.", m_RigPosedControls);
                            }

                            if (m_RigDeviations.empty()) {
                                ImGui::TextDisabled("o rig nao esta mudando nenhum osso");
                            }
                            else {
                                ImGui::TextDisabled("ossos que o solve mudou:");
                                for (const auto& d : m_RigDeviations) {
                                    ImGui::TextColored(
                                        d.Degrees > 20.0f ? ImVec4(1.0f, 0.65f, 0.30f, 1.0f)
                                        : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                        "  %-28s %6.1f deg   %.3f",
                                        d.Bone.c_str(), d.Degrees, d.Distance);
                                }
                            }

                            // ── QUEM FEZ, E QUANDO ───────────────────────────
                            //
                            // Em ordem de EXECUCAO. E a lista que fecha o caso:
                            // o nome do no ao lado do osso que ele mexeu.
                            ImGui::Separator();

                            if (m_RigTrace.empty()) {
                                ImGui::TextDisabled("nenhum no escreveu em osso neste frame");
                            }
                            else {
                                ImGui::TextDisabled("na ordem do solve - quem mexeu em que:");
                                for (const auto& fx : m_RigTrace) {
                                    ImGui::TextColored(
                                        fx.Degrees > 20.0f ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                        : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                        "  %-18s -> %-26s %6.1f deg  %.3f",
                                        fx.Node.c_str(), fx.Bone.c_str(),
                                        fx.Degrees, fx.Distance);
                                }
                            }

                            ImGui::Unindent(12.0f);
                        }

                        if (follow && followers == 0) {
                            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                                ICON_TRIANGLE_EXCLAMATION " nenhum controle tem osso de origem");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip(
                                    "O snap usa o campo SourceBone de cada controle, gravado\n"
                                    "quando o rig e criado a partir do esqueleto. Sem ele nao ha\n"
                                    "como adivinhar qual osso o controle representa, e nada se\n"
                                    "move - o efeito e o mesmo de deixar a opcao desligada.");
                            }
                        }
                    }
                    else {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                            ICON_TRIANGLE_EXCLAMATION " o .axerig nao resolveu");
                    }
                }
            }

            // + Track — abre o picker de osso DESTE binding.
            // ── O TRANSFORM DA PROPRIA ENTIDADE ──────────────────────────
            //
            // Primeiro da fila e desabilitado depois de criado: e um por
            // binding (o binding E a entidade), e um botao que nao faz nada na
            // segunda vez e pior que um botao cinza.
            {
                const bool has = BindingHasEntityTrack(bindingIndex);

                ImGui::BeginDisabled(has);
                if (ui::IconButton(ICON_ARROWS,
                    has ? "A entidade ja tem track de transform"
                    : "Animar o transform da PROPRIA entidade\n"
                    "(camera de cutscene, porta, elevador, prop)",
                    ui::Accent::Add)) {
                    const int ti = CreateEntityTransformTrack(bindingIndex);
                    if (ti >= 0) {
                        m_SelectedBinding = bindingIndex;
                        m_SelectedTrack = ti;
                        m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
                        SetGroupOpen(bindingIndex, TrackGroup::Entity, true);
                    }
                }
                ImGui::EndDisabled();

                ImGui::SameLine();
            }

            if (ui::IconButton(ICON_BONE, "Adicionar track de osso", ui::Accent::Add)) {
                m_AddTrackForBinding = bindingIndex;
                m_OpenAddTrackPopup = true;
                m_PickerFilter[0] = '\0';
            }
            ImGui::SameLine();

            // + Animacao — poe um clipe existente como camada base.
            if (ui::IconButton(ICON_FILM, "Adicionar animacao existente", ui::Accent::Add)) {
                m_AddClipForBinding = bindingIndex;
                m_OpenAddClipPopup = true;
                m_PickerFilter[0] = '\0';
            }
            ImGui::SameLine();

            // + Corte — de qual camera a cena e vista a partir de cada key.
            if (ui::IconButton(ICON_CAMERA, "Cortar para outra camera\n"
                "(uma key por plano)", ui::Accent::Add)) {
                m_AddCameraCutForBinding = bindingIndex;
                m_OpenAddCameraCutPopup = true;
            }
            ImGui::SameLine();

            // + Evento — a track que nao anima nada: avisa o script no frame em
            // que o playhead passa por ela. E o que liga a cutscene ao
            // gameplay ("aqui ele atira", "aqui a porta abre").
            if (ui::IconButton(ICON_BOLT, "Adicionar track de evento\n"
                "(avisa o script num frame durante o Play)", ui::Accent::Add)) {
                m_AddEventForBinding = bindingIndex;
                m_OpenAddEventPopup = true;
                m_NewEventName[0] = '\0';
            }
            ImGui::SameLine();

            // + Socket — track de um ponto de ancoragem do esqueleto, com o
            // objeto que fica preso nele.
            if (ui::IconButton(ICON_LINK, "Adicionar socket (arma, prop...)",
                ui::Accent::Add)) {
                m_AddSocketForBinding = bindingIndex;
                m_OpenAddSocketPopup = true;
                m_PickerFilter[0] = '\0';
                m_NewSocketName[0] = '\0';
                m_NewSocketBone = -1;
            }
            ImGui::SameLine();

            // + Controle — so faz sentido com um rig ligado, entao fica
            // desabilitado ate haver um. Escondido seria pior: o usuario nao
            // teria como descobrir que a opcao existe.
            {
                const bool hasRig = !b->RigAssetUUID.empty();
                ImGui::BeginDisabled(!hasRig);

                if (ui::IconButton(ICON_CIRCLE_NODES,
                    hasRig ? "Adicionar UM controle do rig"
                    : "Ligue um Control Rig abaixo primeiro",
                    ui::Accent::Add)) {
                    m_AddControlForBinding = bindingIndex;
                    m_OpenAddControlPopup = true;
                    m_PickerFilter[0] = '\0';
                }

                ImGui::SameLine();

                // Todos de uma vez. E o gesto normal — quem liga um Control Rig
                // quer o RIG, nao um controle; adicionar dezenove um a um e
                // trabalho braçal que a maquina faz melhor.
                if (ui::IconButton(ICON_SITEMAP,
                    hasRig ? "Adicionar TODOS os controles (na ordem da hierarquia)"
                    : "Ligue um Control Rig abaixo primeiro",
                    ui::Accent::Primary)) {
                    AddAllControlTracks(bindingIndex);
                    ImGui::EndDisabled();
                    ImGui::TreePop();
                    ImGui::PopID();
                    return;   // b e as referencias acima foram realocadas
                }

                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            // - Binding (icone trash, vermelho)
            if (ui::IconButton(ICON_TRASH, "Remover binding", ui::Accent::Danger)) {
                m_Asset.RemoveBinding(bindingIndex);
                m_SelectedBinding = m_SelectedTrack = -1;
                // Todo KeyRef guardado aponta para indices que acabaram de
                // deslizar. Limpar e a unica resposta honesta — remapear
                // exigiria saber quais bindings andaram, e o custo de errar
                // aqui e apagar as keys erradas no proximo Delete.
                ClearKeySelection();

                // As copias de rig tambem sao chaveadas por indice. Se dois
                // bindings usarem o MESMO .axerig, a revalidacao por UUID nao
                // detecta a troca — e um personagem passaria a herdar a pose de
                // controle do outro. Reclonar todos custa um frame.
                ClearRigs();
                ImGui::TreePop();
                ImGui::PopID();
                return;
            }

            // Tracks agrupadas, na ordem definida em TrackGroup. Cada grupo
            // dobra sozinho e o filtro ja foi aplicado no RebuildLayout — o que
            // chega aqui e exatamente o que a timeline vai desenhar.
            for (int g = 0; g < static_cast<int>(TrackGroup::Count); ++g)
                DrawTrackGroupNode(bindingIndex, static_cast<TrackGroup>(g));

            // Nada visivel, mas ha tracks: e o filtro. Dizer isso evita a
            // conclusao errada de que as tracks sumiram.
            if (!b->Tracks.empty() && m_TrackFilter[0] != '\0' &&
                bindingIndex < static_cast<int>(m_Layout.size())) {
                bool any = false;
                for (int g = 0; g < static_cast<int>(TrackGroup::Count); ++g)
                    if (!m_Layout[bindingIndex].Groups[g].empty()) { any = true; break; }

                if (!any) {
                    ImGui::TextDisabled("nenhuma track casa com '%s'", m_TrackFilter);
                }
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SequencerWindow::DrawTrackNode(int bindingIndex, int trackIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;

        auto& tr = b->Tracks[trackIndex];

        // ── INDENTACAO POR PROFUNDIDADE ──────────────────────────────────────
        //
        // A lista ja vem ordenada por hierarquia (ver RebuildLayout), mas ordem
        // sozinha nao mostra PARENTESCO: "ctrl_LeftHand" logo depois de
        // "ctrl_LeftForeArm" pode ser filho ou irmao, e a diferenca importa
        // muito na hora de mover um e esperar que o outro va junto.
        //
        // RAII porque esta funcao tem varias saidas antecipadas (explodir,
        // remover track) — um Unindent esquecido desloca TODO o resto do
        // outliner, e o sintoma aparece longe da causa.
        struct IndentGuard {
            float Amount;
            explicit IndentGuard(float a) : Amount(a) { if (Amount > 0.0f) ImGui::Indent(Amount); }
            ~IndentGuard() { if (Amount > 0.0f) ImGui::Unindent(Amount); }
        };

        int depth = 0;

        if (tr.Type == SequencerTrackType::TransformControl) {
            depth = ControlDepth(bindingIndex, tr.TargetName);
        }
        else if (tr.Type == SequencerTrackType::TransformBone) {
            if (const Skeleton* sk = GetBindingSkeleton(bindingIndex)) {
                int bone = sk->FindBone(tr.TargetName);
                const auto& bones = sk->GetBones();
                while (bone >= 0 && depth < 64) {
                    bone = bones[bone].ParentIndex;
                    if (bone >= 0) ++depth;
                }
            }
        }

        // Teto de 6 niveis: um dedo da Mixamo esta a 8 de profundidade, e
        // indentar tudo empurraria o nome para fora do painel.
        const IndentGuard indent(static_cast<float>(std::min(depth, 6)) * 8.0f);

        ImGui::PushID(trackIndex | (bindingIndex << 16));

        const bool isClipTrack = (tr.Type == SequencerTrackType::AnimationClip);
        const bool isSocketTrack = (tr.Type == SequencerTrackType::TransformSocket);
        // Pelo ALVO, e nao pelo tipo: um controle de canal e uma track
        // `Property`, e continua sendo um controle do rig.
        const bool isControlTrack = (tr.TargetType == SequencerTargetType::Control);
        const bool isChannelTrack = (isControlTrack &&
            tr.Type == SequencerTrackType::Property);

        char label[256];
        if (isControlTrack) {
            RigRuntime* rt = EnsureRig(bindingIndex);
            const bool resolved = rt && rt->Ready &&
                rt->Hierarchy.Find(tr.TargetName, RigElementType::Control) >= 0;

            std::snprintf(label, sizeof(label), "%s %s%s",
                isChannelTrack ? ICON_SLIDERS : ICON_CIRCLE_NODES,
                tr.TargetName.empty() ? "(sem controle)" : tr.TargetName.c_str(),
                resolved ? "" : "  (nao existe no rig)");
        }
        else if (isSocketTrack) {
            // Le como "que socket e, e o que esta preso nele" — que sao as duas
            // perguntas do animador. "TransformSocket: X [Socket]" nao responde
            // nenhuma das duas.
            const bool resolved = (FindBindingSocket(bindingIndex, tr.TargetName) != nullptr);

            const AssetRecord* rec = tr.AttachedAssetUUID.empty()
                ? nullptr : AssetDatabase::Get().GetByUUID(tr.AttachedAssetUUID);

            const std::string attached = rec ? rec->Name
                : (tr.AttachedAssetUUID.empty() ? std::string("vazio")
                    : std::string("asset sumiu"));

            std::snprintf(label, sizeof(label), ICON_LINK " %s%s  - %s",
                tr.TargetName.empty() ? "(sem socket)" : tr.TargetName.c_str(),
                resolved ? "" : "  (nao existe no esqueleto)",
                attached.c_str());
        }
        else if (isClipTrack) {
            // Track de clipe le como "que animacao esta tocando", nao como
            // "Bone/Null" — o tipo de alvo aqui nao diz nada ao animador.
            const bool resolved = (FindBindingClip(bindingIndex, tr.TargetName) != nullptr);
            std::snprintf(label, sizeof(label), ICON_FILM " %s%s",
                tr.TargetName.empty() ? "(sem clipe)" : tr.TargetName.c_str(),
                resolved ? "" : "  (nao encontrado)");
        }
        else {
            std::snprintf(label, sizeof(label), ICON_BONE " %s: %s [%s]",
                SequencerTrackTypeToString(tr.Type),
                tr.TargetName.empty() ? "(no target)" : tr.TargetName.c_str(),
                SequencerTargetTypeToString(tr.TargetType));
        }

        bool selected = (m_SelectedBinding == bindingIndex && m_SelectedTrack == trackIndex);

        // Abrir tudo por default so faz sentido enquanto sao poucas tracks. Um
        // clipe explodido traz dezenas — e cada uma com section e ate 9 canais
        // abertos vira uma parede de milhares de linhas no outliner.
        const bool manyTracks = (b->Tracks.size() > 8);

        // O realce mostra o GRUPO, nao so o ativo: uma selecao de cinco tracks
        // em que so uma acende nao parece uma selecao de cinco.
        const bool inGroup = IsTargetSelected(
            TargetRef{ bindingIndex, tr.TargetType, tr.TargetName });

        const bool highlighted = selected || inGroup;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ((manyTracks && !highlighted) ? 0 : ImGuiTreeNodeFlags_DefaultOpen) |
            (highlighted ? ImGuiTreeNodeFlags_Selected : 0);

        bool open = ImGui::TreeNodeEx(label, flags);
        if (ImGui::IsItemClicked()) {
            const ImGuiIO& io = ImGui::GetIO();

            SelectTarget(TargetRef{ bindingIndex, tr.TargetType, tr.TargetName },
                io.KeyCtrl || io.KeyShift);

            SyncActiveFromTargets();
        }

        // Mute / Lock toggle buttons.
        ImGui::SameLine();
        if (ui::ToggleButton(ICON_EYE_SLASH " M", tr.Muted, "Mute track")) tr.Muted = !tr.Muted;
        ImGui::SameLine();
        if (ui::ToggleButton(tr.Locked ? ICON_LOCK " L" : ICON_UNLOCK " L", tr.Locked,
            tr.Locked ? "Locked" : "Lock")) tr.Locked = !tr.Locked;

        // Explodir: so faz sentido em track de clipe, e so se o clipe resolve.
        if (isClipTrack) {
            ImGui::SameLine();
            if (ui::IconButton(ICON_SITEMAP,
                "Explodir em tracks de osso (keys editaveis)", ui::Accent::Primary)) {
                const int n = ExplodeClipTrack(bindingIndex, trackIndex);
                if (n > 0) {
                    m_SelectedBinding = bindingIndex;
                    m_SelectedTrack = m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
                    ClearKeySelection();   // dezenas de tracks novas: os indices mudaram
                }
                // b e as referencias acima podem ter sido realocadas pelo AddTrack.
                if (open) ImGui::TreePop();
                ImGui::PopID();
                return;
            }
        }

        // Remover track. Vale para os dois tipos: uma track de clipe que aponta
        // para um clipe que sumiu so tem um uso — sair da lista.
        ImGui::SameLine();
        if (ui::IconButton(ICON_TRASH, "Remover track", ui::Accent::Danger)) {
            m_Asset.RemoveTrack(bindingIndex, trackIndex);
            if (m_SelectedBinding == bindingIndex && m_SelectedTrack == trackIndex)
                m_SelectedTrack = m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
            ClearKeySelection();   // ver a nota no RemoveBinding
            m_DraggingKey = false;
            if (open) ImGui::TreePop();
            ImGui::PopID();
            return;
        }

        if (open) {
            // ── VALOR DO CONTROLE DE CANAL ───────────────────────────────────
            //
            // Editavel aqui, sem ir ao Control Rig — que era o pedido: "no
            // sequencer eu posso alterar os valores dele sem precisar ir no
            // control rig ativa-lo".
            //
            // Mexer no widget CRAVA KEY no playhead, pelo mesmo motivo do
            // gizmo: o valor e recalculado das keys a cada frame, entao uma
            // edicao que nao virasse key sumiria no quadro seguinte.
            if (isChannelTrack) {
                ImGui::Indent(20.0f);

                RigRuntime* rt = EnsureRig(bindingIndex);
                const int elIdx = (rt && rt->Ready)
                    ? rt->Hierarchy.Find(tr.TargetName, RigElementType::Control) : -1;

                if (elIdx >= 0) {
                    RigElement& el = rt->Hierarchy[elIdx];

                    const int si = SectionAtPlayhead(bindingIndex, trackIndex);

                    if (el.ValueType == RigControlValue::Bool) {
                        bool v = el.BoolValue;
                        if (ImGui::Checkbox("Ligado", &v)) {
                            el.BoolValue = v;
                            el.FloatValue = v ? 1.0f : 0.0f;
                            if (si >= 0) {
                                SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si,
                                    SequencerChannelComponent::X, el.FloatValue);
                            }
                        }
                    }
                    else {
                        float v = el.FloatValue;
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat("Valor", &v, 0.01f, 0.0f, 1.0f)) {
                            el.FloatValue = v;
                            el.BoolValue = (v >= 0.5f);
                            if (si >= 0) {
                                SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si,
                                    SequencerChannelComponent::X, v);
                            }
                        }
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Mexer aqui crava key no frame atual.\n"
                            "Interpolacao Step deixa o interruptor ligar seco;\n"
                            "Linear faz o peso subir ao longo dos frames.");
                    }
                }
                else {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                        ICON_TRIANGLE_EXCLAMATION " controle '%s' nao existe no rig",
                        tr.TargetName.c_str());
                }

                ImGui::Unindent(20.0f);
            }

            // ── O QUE ESTA PRESO NO SOCKET ───────────────────────────────────
            //
            // Por TRACK, e nao pelo PreviewMeshUUID do socket: aquele campo e do
            // esqueleto e mudaria a arma em toda cena e toda sequence que usem o
            // mesmo `.axeskel`. Aqui, trocar pistola por fuzil e trocar este
            // UUID — o socket e as keys ficam de pe.
            if (isSocketTrack) {
                ImGui::Indent(20.0f);

                if (AssetPicker::Draw("Objeto", tr.AttachedAssetUUID,
                    { AssetType::Mesh, AssetType::SkeletalMesh },
                    [](const AssetRecord&) {})) {
                    // O SyncSocketPreviews do proximo frame ve o UUID novo e
                    // recarrega. Nada a fazer aqui.
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Malha ou skeletal mesh que fica presa neste socket.\n"
                        "Vale so para esta sequence - o socket continua sendo do\n"
                        "esqueleto, entao trocar o objeto nao invalida nenhuma key.");
                }

                if (const auto* s = FindBindingSocket(bindingIndex, tr.TargetName)) {
                    ImGui::TextDisabled("osso pai: %s",
                        s->BoneName.empty() ? "(nenhum)" : s->BoneName.c_str());
                }
                else {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                        ICON_TRIANGLE_EXCLAMATION " socket '%s' nao existe neste esqueleto",
                        tr.TargetName.c_str());
                }

                ImGui::Unindent(20.0f);
            }

            if (tr.Sections.empty() && !isClipTrack) {
                if (ui::IconButton(ICON_PLUS, "Adicionar section (full range)", ui::Accent::Add)) {
                    SequencerSection sec;
                    sec.StartFrame = m_Asset.GetFrameRange().Start;
                    sec.EndFrame = m_Asset.GetFrameRange().End;
                    m_Asset.AddSection(bindingIndex, trackIndex, sec);
                }
            }
            for (int si = 0; si < static_cast<int>(tr.Sections.size()); ++si) {
                DrawSectionNode(bindingIndex, trackIndex, si);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SequencerWindow::DrawSectionNode(int bindingIndex, int trackIndex, int sectionIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;
        auto& trk = b->Tracks[trackIndex];
        auto& sec = trk.Sections[sectionIndex];
        const bool isClipSection = (trk.Type == SequencerTrackType::AnimationClip);

        ImGui::PushID(sectionIndex | (trackIndex << 8) | (bindingIndex << 16));

        char label[128];
        if (isClipSection) {
            std::snprintf(label, sizeof(label), ICON_FILM " %s [%d..%d]",
                sec.SourceClipName.empty() ? "(sem clipe)" : sec.SourceClipName.c_str(),
                sec.StartFrame, sec.EndFrame);
        }
        else {
            std::snprintf(label, sizeof(label), ICON_TABLE_CELLS " Section [%d..%d]",
                sec.StartFrame, sec.EndFrame);
        }

        bool selected = (m_SelectedBinding == bindingIndex &&
            m_SelectedTrack == trackIndex &&
            m_SelectedSection == sectionIndex);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_Leaf |
            (selected ? ImGuiTreeNodeFlags_Selected : 0);

        bool open = ImGui::TreeNodeEx(label, flags);
        if (ImGui::IsItemClicked()) {
            m_SelectedBinding = bindingIndex;
            m_SelectedTrack = trackIndex;
            m_SelectedSection = sectionIndex;
            m_SelectedChannel = m_SelectedKey = -1;
        }
        if (open) ImGui::TreePop();

        // Controles da section de clipe: onde ela comeca na timeline e de que
        // ponto do clipe ela le. Nao ha canais aqui — um clipe nao se edita por
        // componente; se edita colocando tracks de osso POR CIMA dele.
        if (isClipSection) {
            ImGui::Indent(20.0f);

            int startF = sec.StartFrame;
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::DragInt("Inicio", &startF, 1.0f, 0, 100000)) {
                const int len = sec.EndFrame - sec.StartFrame;
                sec.StartFrame = startF;
                sec.EndFrame = startF + len;   // arrasta a section inteira
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Frame da TIMELINE em que a performance comeca.\n"
                    "Arrasta a section inteira, sem mudar a duracao.");

            ImGui::SameLine();
            int offset = sec.ClipOffset;
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::DragInt("Offset", &offset, 1.0f, -100000, 100000))
                sec.ClipOffset = offset;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Frame do CLIPE em que a section comeca a ler.");

            // ── O QUE ACONTECE FORA DA SECTION ───────────────────────────────
            //
            // Vive na section, mas o dado e da TRACK: e ela que decide o que
            // fazer quando nenhuma das suas sections esta ativa. Fica aqui
            // porque e aqui que o animador esta olhando quando descobre o
            // problema — logo depois de arrastar o playhead para depois do fim.
            bool hold = trk.HoldOutsideSections;
            if (ImGui::Checkbox("Segurar pose fora da section", &hold))
                trk.HoldOutsideSections = hold;
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Ligado: antes do inicio e depois do fim, o personagem\n"
                    "        congela no primeiro/ultimo frame do clipe.\n\n"
                    "Desligado: fora da section a track nao produz nada e a\n"
                    "        base volta a ser a pose de repouso (T-pose).");
            }

            if (const AnimationClip* c = FindBindingClip(bindingIndex, sec.SourceClipName)) {
                ImGui::TextDisabled("%.2f s | %d canais de osso",
                    c->GetDuration(), (int)c->GetChannelCount());
            }
            else {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                    ICON_TRIANGLE_EXCLAMATION " clipe '%s' nao existe nesta entidade",
                    sec.SourceClipName.c_str());
            }

            ImGui::Unindent(20.0f);
            ImGui::PopID();
            return;
        }

        // Right-click context: add channels.
        std::string popupId = "sec_ctx_" + std::to_string(sectionIndex);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup(popupId.c_str());
        }
        if (ImGui::BeginPopup(popupId.c_str())) {
            // ── OS NOVE CANAIS, E NAO SO TRANSLACAO ──────────────────────────
            //
            // O menu oferecia X/Y/Z e mais nada. Como o painel de key nao cria
            // canal, isso queria dizer que ROTACAO era inalcancavel pela UI: a
            // unica forma de existir uma curva de RotX era explodir um clipe.
            // Girar um osso a mao — o caso mais comum de todos — nao tinha
            // caminho.
            //
            // O laco evita a terceira copia colada do mesmo bloco, que e onde a
            // quarta teria nascido faltando.
            struct ChannelEntry {
                const char* Label;
                SequencerChannelComponent Component;
            };

            static const ChannelEntry kTranslation[] = {
                { "Location X", SequencerChannelComponent::X },
                { "Location Y", SequencerChannelComponent::Y },
                { "Location Z", SequencerChannelComponent::Z },
            };
            static const ChannelEntry kRotation[] = {
                { "Rotation X", SequencerChannelComponent::RotX },
                { "Rotation Y", SequencerChannelComponent::RotY },
                { "Rotation Z", SequencerChannelComponent::RotZ },
            };
            static const ChannelEntry kScale[] = {
                { "Scale X", SequencerChannelComponent::ScaleX },
                { "Scale Y", SequencerChannelComponent::ScaleY },
                { "Scale Z", SequencerChannelComponent::ScaleZ },
            };

            auto drawGroup = [&](const char* title, const ChannelEntry* items) {
                ImGui::SeparatorText(title);
                for (int i = 0; i < 3; ++i) {
                    // Canal que ja existe fica desabilitado em vez de sumir: um
                    // menu que muda de tamanho conforme o estado obriga a
                    // procurar o item toda vez.
                    bool exists = false;
                    for (const auto& c : sec.Channels)
                        if (c.Component == items[i].Component) { exists = true; break; }

                    ImGui::BeginDisabled(exists);
                    if (ImGui::MenuItem(items[i].Label)) {
                        SequencerChannel ch;
                        ch.Component = items[i].Component;
                        m_Asset.AddChannel(bindingIndex, trackIndex, sectionIndex, ch);
                    }
                    ImGui::EndDisabled();
                }
                };

            drawGroup("Translacao", kTranslation);
            drawGroup("Rotacao (graus)", kRotation);
            drawGroup("Escala", kScale);

            ImGui::Separator();
            if (ImGui::MenuItem(ICON_PLUS " Todos os 9")) {
                for (const auto* group : { kTranslation, kRotation, kScale }) {
                    for (int i = 0; i < 3; ++i) {
                        bool exists = false;
                        for (const auto& c : sec.Channels)
                            if (c.Component == group[i].Component) { exists = true; break; }
                        if (exists) continue;

                        SequencerChannel ch;
                        ch.Component = group[i].Component;
                        m_Asset.AddChannel(bindingIndex, trackIndex, sectionIndex, ch);
                    }
                }
            }

            ImGui::EndPopup();
        }

        // Sub-arvore de channels.
        ImGui::Indent(20.0f);
        for (int ci = 0; ci < static_cast<int>(sec.Channels.size()); ++ci) {
            DrawChannelNode(bindingIndex, trackIndex, sectionIndex, ci);
        }
        ImGui::Unindent(20.0f);

        ImGui::PopID();
    }

    void SequencerWindow::DrawChannelNode(int bindingIndex, int trackIndex,
        int sectionIndex, int channelIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return;
        auto& sec = b->Tracks[trackIndex].Sections[sectionIndex];
        auto& ch = sec.Channels[channelIndex];

        ImGui::PushID(channelIndex | (sectionIndex << 8) | (trackIndex << 16) | (bindingIndex << 24));

        // ── O ROTULO DEPENDE DO QUE A TRACK FAZ ──────────────────────────────
        //
        // Em track de transform, o canal E um eixo, e "X" e a palavra certa.
        // Em evento e corte o canal nao significa eixo nenhum — e so onde as
        // keys moram. Chamar aquilo de "X" convidaria o animador a procurar um
        // "Y" que nao existe, e a perguntar por que a camera nao anda no eixo X.
        const SequencerTrackType trType = b->Tracks[trackIndex].Type;
        const bool instantTrack = (trType == SequencerTrackType::Event ||
            trType == SequencerTrackType::CameraCut);

        char label[64];

        if (instantTrack) {
            std::snprintf(label, sizeof(label), "%s (%d)",
                trType == SequencerTrackType::CameraCut ? "cortes" : "disparos",
                static_cast<int>(ch.Keys.size()));
        }
        else {
            std::snprintf(label, sizeof(label), "%s (%d keys)",
                SequencerChannelComponentToString(ch.Component),
                static_cast<int>(ch.Keys.size()));
        }

        bool selected = (m_SelectedBinding == bindingIndex &&
            m_SelectedTrack == trackIndex &&
            m_SelectedSection == sectionIndex &&
            m_SelectedChannel == channelIndex);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
            (selected ? ImGuiTreeNodeFlags_Selected : 0);

        bool open = ImGui::TreeNodeEx(label, flags);
        if (ImGui::IsItemClicked()) {
            m_SelectedBinding = bindingIndex;
            m_SelectedTrack = trackIndex;
            m_SelectedSection = sectionIndex;
            m_SelectedChannel = channelIndex;
            m_SelectedKey = -1;
        }
        if (open) ImGui::TreePop();

        // Inline: crava uma key no frame atual. Em track de transform o valor
        // vem do viewport; em evento e corte a key E a informacao toda.
        ImGui::SameLine();
        if (ui::IconButton(instantTrack ? ICON_BOLT : ICON_CAMERA,
            instantTrack
            ? (trType == SequencerTrackType::CameraCut
                ? "Cortar para esta camera NESTE frame"
                : "Disparar este evento NESTE frame")
            : "Capturar valor do bone no viewport")) {
            AddKeyAtPlayhead(bindingIndex, trackIndex, sectionIndex, channelIndex);
        }

        ImGui::PopID();
    }

    // ============================================================
    // Painel "Selected Key" — editor de Frame / Value / Interp
    // ============================================================

    void SequencerWindow::DrawSelectedKeyPanel() {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled(ICON_BONE " Selected Key");
        ImGui::Spacing();

        if (m_SelectedBinding < 0 || m_SelectedTrack < 0 ||
            m_SelectedSection < 0 || m_SelectedChannel < 0 || m_SelectedKey < 0) {
            ImGui::TextDisabled("Nenhuma key selecionada.");
            ImGui::TextWrapped("Clique num key (diamond) na timeline, ou use o botao " ICON_CAMERA ".");
            return;
        }

        auto* b = m_Asset.GetBinding(m_SelectedBinding);
        if (!b || m_SelectedTrack >= static_cast<int>(b->Tracks.size())) return;
        auto& tr = b->Tracks[m_SelectedTrack];
        if (m_SelectedSection >= static_cast<int>(tr.Sections.size())) return;
        auto& sec = tr.Sections[m_SelectedSection];
        if (m_SelectedChannel >= static_cast<int>(sec.Channels.size())) return;
        auto& ch = sec.Channels[m_SelectedChannel];
        if (m_SelectedKey >= static_cast<int>(ch.Keys.size())) return;
        auto& k = ch.Keys[m_SelectedKey];

        ImGui::Text("Binding: %s", b->DisplayName.c_str());
        ImGui::Text("Track:   %s (%s)", tr.TargetName.c_str(),
            SequencerTrackTypeToString(tr.Type));
        ImGui::Text("Channel: %s", SequencerChannelComponentToString(ch.Component));
        ImGui::Separator();

        // Frame edit.
        float frame = k.Frame;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputFloat("Frame", &frame, 1.0f, 5.0f, "%.2f")) {
            if (m_SnapEnabled && m_SnapFrame > 0) {
                frame = std::round(frame / m_SnapFrame) * m_SnapFrame;
            }
            frame = std::clamp(frame,
                static_cast<float>(m_Asset.GetFrameRange().Start),
                static_cast<float>(m_Asset.GetFrameRange().End));
            k.Frame = frame;
            ch.SortKeys();
            for (int j = 0; j < static_cast<int>(ch.Keys.size()); ++j) {
                if (ch.Keys[j].Frame == frame) { m_SelectedKey = j; break; }
            }
        }

        // Value edit.
        float value = k.Value;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputFloat("Value", &value, 0.1f, 1.0f, "%.3f")) {
            k.Value = value;
        }

        // ── INTERPOLACAO: DE QUAL TRECHO ESTAMOS FALANDO ─────────────────────
        //
        // A curva de um trecho vem da key da ESQUERDA. Isto e padrao em todo
        // NLE, e produzia aqui uma armadilha silenciosa: escolher Bezier na key
        // de CHEGADA e ajustar o Tangent In nao mudava nada, porque quem manda
        // no trecho que chega e a key anterior.
        //
        // O painel nao dizia isso em lugar nenhum. Agora diz, e avisa quando o
        // numero que a pessoa acabou de digitar e inerte.
        const bool hasPrev = (m_SelectedKey > 0);
        const bool hasNext = (m_SelectedKey + 1 < static_cast<int>(ch.Keys.size()));

        ImGui::Spacing();
        ImGui::TextDisabled("Curva DESTA key ate a proxima");

        ImGui::SetNextItemWidth(-1);
        int interp = static_cast<int>(k.Interp);

        const char* interpItems =
            "Step\0"
            "Linear\0"
            "CubicEaseIn\0"
            "CubicEaseOut\0"
            "Bezier\0"
            "EaseInStrong (segura e dispara)\0"
            "EaseOutStrong (dispara e assenta)\0"
            "EaseInOut (suave nas duas pontas)\0"
            "EaseOutBack (passa do ponto e volta)\0"
            "EaseOutBounce (quica)\0";

        if (ImGui::Combo("Interp", &interp, interpItems)) {
            k.Interp = static_cast<SequencerInterp>(interp);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Vale para o trecho que SAI desta key.\n\n"
                "Para um impacto (coice de arma, batida):\n"
                "  EaseOutStrong na key de repouso -> o golpe\n"
                "  EaseOutBack   na key do pico    -> a volta passa do ponto\n\n"
                "Curva nenhuma salva um pico distante: se o golpe leva 20\n"
                "frames para chegar, ele nao e um golpe. Aproxime as keys\n"
                "primeiro, a curva depois.");
        }

        if (!hasNext) {
            ImGui::TextDisabled("(ultima key do canal - a curva nao tem trecho)");
        }

        if (k.Interp == SequencerInterp::Bezier) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputFloat("Tangent Out", &k.TangentOut, 0.1f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Puxa o inicio do trecho que SAI desta key.");
        }

        // ── O TANGENT IN PERTENCE AO TRECHO ANTERIOR ─────────────────────────
        //
        // Por isso ele aparece separado, e por isso ha um aviso quando a key
        // anterior nao e Bezier: nesse caso o valor esta gravado e nao e lido
        // por ninguem. Era exatamente este o caso de "mudei a interpolacao e
        // nao mudou nada".
        if (hasPrev) {
            const SequencerKey& prev = ch.Keys[m_SelectedKey - 1];
            const bool prevIsBezier = (prev.Interp == SequencerInterp::Bezier);

            if (prevIsBezier || k.TangentIn != 0.0f) {
                ImGui::Spacing();
                ImGui::TextDisabled("Trecho que CHEGA nesta key");

                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("Tangent In", &k.TangentIn, 0.1f, 1.0f, "%.3f");

                if (!prevIsBezier) {
                    ImGui::TextColored(ui::AccentColor(ui::Accent::Warning),
                        ICON_TRIANGLE_EXCLAMATION " Sem efeito: a key do frame "
                        "%.0f nao e Bezier.", prev.Frame);

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Corrigir")) {
                        ch.Keys[m_SelectedKey - 1].Interp = SequencerInterp::Bezier;
                    }
                }
            }
        }

        if (k.Interp == SequencerInterp::EaseOutBack ||
            k.Interp == SequencerInterp::EaseOutBounce) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputFloat("Overshoot", &k.Overshoot, 0.1f, 0.5f, "%.3f");

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Quanto passa do ponto. 0 = padrao (~10%%).\n"
                    "E o unico numero que se mexe para tunar 'quanto a arma pula'.");

            if (k.Overshoot < 0.0f) k.Overshoot = 0.0f;
        }

        ImGui::Spacing();
        if (ui::AccentButton(ICON_CAMERA " Capture from Viewport", ui::Accent::Primary)) {
            float v = 0.0f;
            if (CaptureChannelValue(m_SelectedBinding, tr, ch.Component, v))
                k.Value = v;
        }
        ImGui::SameLine();

        const int selCount = static_cast<int>(m_SelectedKeys.size());

        char delLabel[64];
        std::snprintf(delLabel, sizeof(delLabel), ICON_TRASH " Delete%s",
            selCount > 1 ? " (todas)" : "");

        if (ui::AccentButton(delLabel, ui::Accent::Danger)) {
            if (selCount > 1) {
                DeleteSelectedKeys();
            }
            else {
                m_Asset.RemoveKey(m_SelectedBinding, m_SelectedTrack,
                    m_SelectedSection, m_SelectedChannel, m_SelectedKey);
                ClearKeySelection();
                m_SelectedKey = -1;
            }
        }

        if (selCount > 1) {
            ImGui::TextDisabled("%d keys selecionadas - o painel edita a de cor "
                "ambar; Delete apaga todas.", selCount);
        }
    }

    // ============================================================
    // Timeline (largura total + playhead arrastavel)
    // ============================================================

    void SequencerWindow::DrawTimeline() {
        ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < 50 || size.y < 50) return;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();

        // Layout constants.
        const float rulerHeight = 32.0f;  // mais alta pra numeros legiveis
        const float laneHeight = 32.0f;
        const int   rangeStart = m_Asset.GetFrameRange().Start;
        const int   rangeEnd = m_Asset.GetFrameRange().End;
        const int   totalFrames = std::max(1, rangeEnd - rangeStart);

        // Padding de 20px a direita pra playhead nao ficar colada na borda.
        const float availableWidth = std::max(1.0f, size.x - 20.0f);

        // ── ZOOM E PAN ───────────────────────────────────────────────────────
        //
        // Ate aqui a timeline esticava o range inteiro na largura disponivel.
        // Com 125 frames num painel de 700px sao 5,6 px por frame: dois losangos
        // em frames vizinhos se sobrepoem e nao ha como pegar um sem pegar o
        // outro. Um clipe explodido da Mixamo tem key em TODO frame — sem zoom,
        // ele e uma barra continua de losangos.
        //
        // Zoom 1 = comportamento antigo (o range inteiro cabe na tela), e por
        // isso o clamp inferior e 1: nunca se afasta alem do range, o que
        // deixaria uma faixa morta na direita.
        m_Zoom = std::clamp(m_Zoom, 1.0f, 80.0f);

        const float baseFrameWidth = availableWidth / static_cast<float>(totalFrames);
        const float frameWidth = baseFrameWidth * m_Zoom;

        // Scroll em FRAMES, nao em pixels: assim o pan sobrevive a um resize da
        // janela sem escorregar de posicao.
        const float visibleFrames = availableWidth / frameWidth;
        const float maxScrollX = std::max(0.0f,
            static_cast<float>(totalFrames) - visibleFrames);

        m_TimelineScrollX = std::clamp(m_TimelineScrollX, 0.0f, maxScrollX);

        // Primeiro frame visivel. FLOAT de proposito — zoom alto com scroll
        // fracionario e o que faz o pan parecer continuo em vez de saltar de
        // frame em frame.
        const float startFrame = static_cast<float>(rangeStart) + m_TimelineScrollX;

        // Background.
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
            IM_COL32(28, 28, 30, 255));

        // Regua de frames.
        ImRect rulerRect(origin, ImVec2(origin.x + size.x, origin.y + rulerHeight));
        dl->AddRectFilled(rulerRect.Min, rulerRect.Max, IM_COL32(45, 45, 48, 255));

        // ── ENTRADA: roda do mouse e pan com o botao do meio ─────────────────
        //
        //   Ctrl  + roda   -> zoom, ANCORADO NO CURSOR
        //   Shift + roda   -> pan horizontal
        //   roda           -> scroll vertical das lanes (mais abaixo)
        //   botao do meio  -> pan horizontal arrastando
        //
        // Ancorar o zoom no cursor e o que faz isto ser navegacao em vez de
        // sorteio: o frame que esta sob o mouse continua sob o mouse, entao voce
        // mira no trecho e aproxima. Zoom ancorado na borda esquerda obriga a
        // corrigir o pan depois de cada passo, e a corrigir errado.
        //
        // O botao do meio, e nao Space+arrasto: Space ja e Play no transporte.
        const bool timelineHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        {
            ImGuiIO& io = ImGui::GetIO();

            if (timelineHovered && io.MouseWheel != 0.0f && io.KeyCtrl) {
                const float frameUnderMouse =
                    startFrame + (io.MousePos.x - origin.x) / frameWidth;

                const float newZoom = std::clamp(
                    m_Zoom * std::pow(1.15f, io.MouseWheel), 1.0f, 80.0f);

                const float newFrameWidth = baseFrameWidth * newZoom;

                m_TimelineScrollX = (frameUnderMouse - static_cast<float>(rangeStart))
                    - (io.MousePos.x - origin.x) / newFrameWidth;

                m_Zoom = newZoom;
            }
            else if (timelineHovered && io.MouseWheel != 0.0f && io.KeyShift) {
                m_TimelineScrollX -= io.MouseWheel * std::max(1.0f, visibleFrames * 0.1f);
            }

            if (timelineHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                m_PanningTimeline = true;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                m_PanningTimeline = false;

            if (m_PanningTimeline) {
                m_TimelineScrollX -= io.MouseDelta.x / frameWidth;
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }

            // Reclampado aqui e de novo no topo do proximo frame. O valor so
            // passa a valer no proximo Draw de proposito: mudar o mapeamento
            // frame->pixel no meio do desenho deixaria a regua e as lanes deste
            // frame em escalas diferentes.
            m_TimelineScrollX = std::clamp(m_TimelineScrollX, 0.0f, maxScrollX);
        }

        // ── TICKS ────────────────────────────────────────────────────────────
        //
        // O passo sai da DENSIDADE em pixels, nao do tamanho do range. A tabela
        // antiga decidia por totalFrames e ignorava o zoom: dando zoom numa
        // sequence de 300 frames, os rotulos continuavam de 30 em 30 e ficavam
        // a 300px de distancia, com um oceano vazio entre eles.
        //
        // Passos "redondos" para a leitura em frames fazer sentido (nada de 7 ou
        // 23), com pelo menos 64px entre rotulos.
        static const int kSteps[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800 };

        int majorStep = kSteps[0];
        for (int s : kSteps) {
            majorStep = s;
            if (s * frameWidth >= 64.0f) break;
        }

        // Minor = uma subdivisao natural do major, nunca menor que 4px (abaixo
        // disso a regua vira um bloco cinza solido).
        int minorStep = std::max(1, majorStep / 5);
        if (majorStep % 5 != 0) minorStep = std::max(1, majorStep / 2);
        if (minorStep * frameWidth < 4.0f) minorStep = majorStep;

        // So os frames VISIVEIS entram no laco. Antes ele percorria o range
        // inteiro e saia no break — barato com 90 frames, nao com 3000 e zoom.
        const int firstTick = static_cast<int>(std::floor(startFrame));
        const int lastTick = std::min(rangeEnd,
            static_cast<int>(std::ceil(startFrame + visibleFrames)) + 1);

        for (int f = firstTick; f <= lastTick; ++f) {
            if (f < rangeStart) continue;

            float x = origin.x + (f - startFrame) * frameWidth;
            if (x < origin.x - 40.0f) continue;
            if (x > origin.x + size.x) break;

            // Ancorado em rangeStart, e nao no primeiro frame visivel: com o
            // scroll fracionario, ancorar na tela faria os rotulos mudarem de
            // valor enquanto se arrasta.
            bool major = ((f - rangeStart) % majorStep == 0);
            bool minor = ((f - rangeStart) % minorStep == 0);

            if (major) {
                dl->AddLine(ImVec2(x, origin.y),
                    ImVec2(x, origin.y + rulerHeight),
                    IM_COL32(180, 180, 180, 255), 1.0f);
                char label[16];
                std::snprintf(label, sizeof(label), "%d", f);
                dl->AddText(ImVec2(x + 4, origin.y + 8),
                    IM_COL32(220, 220, 220, 255), label);
            }
            else if (minor) {
                dl->AddLine(ImVec2(x, origin.y),
                    ImVec2(x, origin.y + rulerHeight * 0.4f),
                    IM_COL32(120, 120, 120, 255), 1.0f);
            }
        }

        // ── Lanes (uma por track de cada binding), com scroll vertical ───────
        //
        // A timeline e desenhada a mao com ImDrawList: o cursor do ImGui nunca
        // anda, entao o child nao tem "conteudo" e nunca gerou scrollbar.
        // Enquanto eram tres tracks isso passou despercebido; com um clipe
        // explodido as lanes de baixo eram cortadas sem nem um indicio de que
        // existiam — parecia que as tracks nao tinham sido criadas.
        const float lanesTop = origin.y + rulerHeight + 4.0f;
        const float lanesBottom = origin.y + size.y;
        const float lanePitch = laneHeight + 2.0f;

        int bindingCount = static_cast<int>(m_Asset.GetBindingCount());

        // Conta so as lanes VISIVEIS. Contar todas faria a barra de scroll
        // prometer conteudo que nao existe: com o grupo Ossos dobrado, sobrariam
        // cinquenta lanes de altura em branco abaixo da ultima.
        int totalLanes = 0;
        for (int bi = 0; bi < bindingCount && bi < static_cast<int>(m_Layout.size()); ++bi)
            totalLanes += static_cast<int>(m_Layout[bi].Visible.size());

        const float contentHeight = totalLanes * lanePitch;
        const float viewHeight = std::max(0.0f, lanesBottom - lanesTop);
        const float maxScroll = std::max(0.0f, contentHeight - viewHeight);

        // O child foi criado com NoScrollWithMouse justamente para o ImGui nao
        // disputar a roda conosco.
        //
        // Ctrl e Shift ficam de fora: a roda ja foi consumida la em cima pelo
        // zoom e pelo pan. Sem esta guarda, dar zoom rolaria as lanes junto.
        // `!m_CurveMode`: no grafico a roda sem modificador e o zoom VERTICAL
        // (ver DrawCurveArea). Sem esta guarda os dois consumiriam o mesmo
        // gesto, e o scroll de lanes ficaria escorregando por baixo do modo
        // curva sem nada na tela explicando por que.
        if (!m_CurveMode && maxScroll > 0.0f && timelineHovered &&
            !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) m_TimelineScrollY -= wheel * lanePitch * 2.0f;
        }
        m_TimelineScrollY = std::clamp(m_TimelineScrollY, 0.0f, maxScroll);

        // ── CAIXA DE SELECAO: O RETANGULO DESTE FRAME ────────────────────────
        //
        // Calculado ANTES do laco porque e ele quem colhe: cada key testa a
        // propria posicao de tela contra esta caixa no mesmo ponto em que ja
        // calcula onde desenhar o losango. Um segundo passe teria de recalcular
        // as mesmas posicoes, e duas contas do mesmo numero acabam divergindo.
        ImRect boxRect;
        const bool boxActive = m_BoxSelecting;

        if (boxActive) {
            const ImVec2 mp = ImGui::GetMousePos();
            boxRect = ImRect(
                ImVec2(std::min(m_BoxStartPos.x, mp.x), std::min(m_BoxStartPos.y, mp.y)),
                ImVec2(std::max(m_BoxStartPos.x, mp.x), std::max(m_BoxStartPos.y, mp.y)));
            m_BoxHits.clear();
        }

        // ── ACAO ADIADA DO MENU DE CONTEXTO ──────────────────────────────────
        //
        // Copiar, colar, recortar e apagar mudam a ESTRUTURA (numero de keys,
        // de canais, de sections). Executar isso dentro do laco que esta
        // percorrendo essa mesma estrutura invalidaria os indices sob os pes do
        // proprio laco.
        //
        // O Delete antigo escapava com um `break`, e funcionava porque so mexia
        // no canal corrente. Colar nao tem esse luxo: pode criar canal em
        // varias tracks de uma vez. Adiar e o unico jeito que continua correto
        // quando a lista de operacoes cresce.
        enum class DeferredKeyOp {
            None, Copy, Cut, Paste, Duplicate, Delete,
            SelectChannel, SelectTrack
        };

        DeferredKeyOp deferredOp = DeferredKeyOp::None;
        KeyRef        deferredRef{};

        // Recorta as lanes na area delas: sem isto uma lane meio rolada desenha
        // por cima da regua de frames.
        dl->PushClipRect(ImVec2(origin.x, lanesTop),
            ImVec2(origin.x + size.x, lanesBottom), true);

        // ── DOIS MODOS, UM EIXO DO TEMPO ─────────────────────────────────────
        //
        // Tudo acima (regua, zoom, pan, playhead, caixa de selecao) e
        // compartilhado por construcao: o grafico usa o MESMO frameToX que o
        // dope sheet. E isso que garante que os dois nunca discordem sobre onde
        // esta o frame 20.
        if (m_CurveMode) {
            DrawCurveArea(dl, origin, size, lanesTop, lanesBottom,
                startFrame, frameWidth, timelineHovered, boxRect, boxActive);
        }

        float laneY = lanesTop - m_TimelineScrollY;
        for (int bi = 0; bi < bindingCount && !m_CurveMode; ++bi) {
            SequencerBinding* b = m_Asset.GetBinding(bi);
            if (!b) continue;
            if (bi >= static_cast<int>(m_Layout.size())) break;

            // A MESMA lista que o outliner desenhou, na MESMA ordem — inclusive
            // a ordem de hierarquia. Percorrer b->Tracks aqui traria de volta a
            // ordem de criacao, e a lane deixaria de corresponder a linha.
            for (int ti : m_Layout[bi].Visible) {
                if (ti < 0 || ti >= static_cast<int>(b->Tracks.size())) continue;
                auto& tr = b->Tracks[ti];

                // Lane fora da vista: pula sem desenhar E sem criar os
                // InvisibleButtons das keys — fora do clip rect eles continuariam
                // capturando clique, roubando o mouse da regua e do playhead.
                if (laneY + laneHeight < lanesTop || laneY > lanesBottom) {
                    laneY += lanePitch;
                    continue;
                }

                ImRect laneRect(ImVec2(origin.x, laneY),
                    ImVec2(origin.x + size.x, laneY + laneHeight));

                bool laneSelected = (m_SelectedBinding == bi && m_SelectedTrack == ti);
                ImU32 laneBg = laneSelected ? IM_COL32(60, 60, 75, 255)
                    : IM_COL32(38, 38, 42, 255);
                dl->AddRectFilled(laneRect.Min, laneRect.Max, laneBg);
                dl->AddLine(laneRect.GetBL(), laneRect.GetBR(),
                    IM_COL32(20, 20, 20, 255));

                char laneLabel[256];
                std::snprintf(laneLabel, sizeof(laneLabel), "%s / %s%s",
                    b->DisplayName.c_str(), tr.TargetName.c_str(),
                    tr.Muted ? "  (mute)" : "");
                dl->AddText(ImVec2(laneRect.Min.x + 6, laneRect.Min.y + 7),
                    tr.Muted ? IM_COL32(140, 140, 140, 255)
                    : IM_COL32(220, 220, 220, 255), laneLabel);

                // ── RASTRO DE HOLD ───────────────────────────────────────────
                //
                // Onde a track de clipe segura a pose da borda em vez de largar.
                // Sem este rastro, "esta congelado no ultimo frame" e "nao tem
                // nada aqui" sao pixels identicos — e essa ambiguidade e
                // exatamente a duvida que a T-pose no frame 36 levantou.
                if (tr.Type == SequencerTrackType::AnimationClip &&
                    tr.HoldOutsideSections && !tr.Muted && !tr.Sections.empty()) {
                    int lo = tr.Sections[0].StartFrame;
                    int hi = tr.Sections[0].EndFrame;
                    for (const auto& s2 : tr.Sections) {
                        lo = std::min(lo, s2.StartFrame);
                        hi = std::max(hi, s2.EndFrame);
                    }
                    const float xLo = origin.x + (lo - startFrame) * frameWidth;
                    const float xHi = origin.x + (hi - startFrame) * frameWidth;
                    const ImU32 holdCol = IM_COL32(160, 100, 200, 45);

                    if (xLo > laneRect.Min.x)
                        dl->AddRectFilled(ImVec2(laneRect.Min.x, laneY + 11),
                            ImVec2(xLo, laneY + laneHeight - 11), holdCol);
                    if (xHi < laneRect.Max.x)
                        dl->AddRectFilled(ImVec2(xHi, laneY + 11),
                            ImVec2(laneRect.Max.x, laneY + laneHeight - 11), holdCol);
                }

                // Sections.
                for (int si = 0; si < static_cast<int>(tr.Sections.size()); ++si) {
                    auto& sec = tr.Sections[si];
                    float secStart = origin.x + (sec.StartFrame - startFrame) * frameWidth;
                    float secEnd = origin.x + (sec.EndFrame - startFrame) * frameWidth;
                    if (secEnd < laneRect.Min.x || secStart > laneRect.Max.x) continue;

                    ImRect secRect(ImVec2(secStart, laneY + 2),
                        ImVec2(secEnd, laneY + laneHeight - 2));

                    ImU32 secColor;
                    switch (tr.Type) {
                    case SequencerTrackType::TransformBone:
                        secColor = IM_COL32(200, 140, 60, 200); break;
                    case SequencerTrackType::TransformControl:
                        secColor = IM_COL32(80, 140, 220, 200); break;
                    case SequencerTrackType::TransformSocket:
                        secColor = IM_COL32(180, 100, 200, 200); break;
                    case SequencerTrackType::AnimationClip:
                        secColor = IM_COL32(160, 100, 200, 200); break;
                    case SequencerTrackType::Property:
                        secColor = IM_COL32(80, 180, 100, 200); break;
                    case SequencerTrackType::Event:
                        secColor = IM_COL32(220, 200, 80, 200); break;
                    }

                    // Track mutada sai da avaliacao (o Resample pula). A lane
                    // precisa dizer isso: depois de explodir um clipe a track de
                    // clipe fica mutada, e uma barra roxa opaca sugeriria que ela
                    // ainda esta mandando na pose.
                    if (tr.Muted)
                        secColor = (secColor & ~IM_COL32_A_MASK) |
                        (static_cast<ImU32>(55) << IM_COL32_A_SHIFT);

                    dl->AddRectFilled(secRect.Min, secRect.Max, secColor);
                    dl->AddRect(secRect.Min, secRect.Max,
                        tr.Muted ? IM_COL32(255, 255, 255, 60)
                        : IM_COL32(255, 255, 255, 180), 1.0f);

                    // Keys dentro da section.
                    for (int ci = 0; ci < static_cast<int>(sec.Channels.size()); ++ci) {
                        auto& ch = sec.Channels[ci];
                        for (int ki = 0; ki < static_cast<int>(ch.Keys.size()); ++ki) {
                            auto& k = ch.Keys[ki];
                            float kx = origin.x + (k.Frame - startFrame) * frameWidth;
                            float ky = laneY + laneHeight * 0.5f;

                            // Fora da vista horizontal: nem desenha nem cria o
                            // InvisibleButton. Com zoom alto a maioria das keys
                            // de um clipe explodido esta fora da tela, e cada
                            // uma delas custaria um item de ImGui por frame.
                            if (kx < origin.x - 12.0f || kx > origin.x + size.x + 12.0f)
                                continue;

                            const float r = 7.0f;
                            const KeyRef ref{ bi, ti, si, ci, ki };

                            // Track travada nao entra na caixa. Um cadeado que
                            // deixa a key ser pega e apagada em grupo e meio
                            // cadeado — a mesma regra que ja vale para o gizmo.
                            if (boxActive && !tr.Locked &&
                                kx >= boxRect.Min.x && kx <= boxRect.Max.x &&
                                ky >= boxRect.Min.y && ky <= boxRect.Max.y) {
                                m_BoxHits.push_back(ref);
                            }

                            const bool isPrimary = (m_SelectedBinding == bi &&
                                m_SelectedTrack == ti &&
                                m_SelectedSection == si &&
                                m_SelectedChannel == ci &&
                                m_SelectedKey == ki);
                            const bool inSelection = IsKeySelected(ref);

                            // Tres estados, nao dois: a PRIMARIA e a que o painel
                            // de edicao esta mostrando, e distingui-la do resto do
                            // grupo e o que evita editar um valor achando que se
                            // esta editando outro.
                            ImU32 keyFill = isPrimary ? IM_COL32(255, 200, 80, 255)
                                : inSelection ? IM_COL32(120, 190, 255, 255)
                                : IM_COL32(255, 255, 255, 220);

                            // Hit-test.
                            ImGui::SetCursorScreenPos(ImVec2(kx - r, ky - r));
                            char kid[64];
                            std::snprintf(kid, sizeof(kid), "key_%d_%d_%d_%d_%d",
                                bi, ti, si, ci, ki);
                            ImGui::InvisibleButton(kid, ImVec2(r * 2, r * 2));

                            if (ImGui::IsItemActivated()) {
                                const ImGuiIO& io = ImGui::GetIO();

                                if (io.KeyCtrl || io.KeyShift) {
                                    // Ctrl+click alterna. Ctrl+click numa key que
                                    // ja estava dentro TIRA do grupo — e por isso
                                    // nao inicia arrasto: o gesto foi "remover
                                    // esta", nao "mover o grupo".
                                    ToggleKeySelection(ref);
                                }
                                else if (!inSelection) {
                                    // Click simples fora do grupo recomeca a
                                    // selecao. Click DENTRO do grupo preserva ele
                                    // — senao pegar um grupo de 40 keys para
                                    // arrastar desfaria a selecao no toque.
                                    SetSingleKeySelection(ref);
                                }

                                m_SelectedBinding = bi;
                                m_SelectedTrack = ti;
                                m_SelectedSection = si;
                                m_SelectedChannel = ci;
                                m_SelectedKey = ki;

                                if (IsKeySelected(ref) && !tr.Locked) {
                                    m_DraggingKey = true;
                                    m_DragStartMouseX = ImGui::GetMousePos().x;
                                    BeginKeyDrag();
                                }
                            }

                            // Right-click: menu de contexto.
                            std::string keyPopup = "key_ctx_" + std::string(kid);
                            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                                // Botao direito fora do grupo passa a mirar so
                                // esta key; dentro do grupo, o menu age sobre o
                                // grupo inteiro.
                                if (!inSelection) SetSingleKeySelection(ref);
                                ImGui::OpenPopup(keyPopup.c_str());
                            }
                            if (ImGui::BeginPopup(keyPopup.c_str())) {
                                m_SelectedBinding = bi;
                                m_SelectedTrack = ti;
                                m_SelectedSection = si;
                                m_SelectedChannel = ci;
                                m_SelectedKey = ki;

                                const int selCount = static_cast<int>(m_SelectedKeys.size());

                                char delLabel[64];
                                std::snprintf(delLabel, sizeof(delLabel),
                                    ICON_TRASH " Delete%s",
                                    selCount > 1 ? "  (selecao inteira)" : "");

                                // Todas as operacoes de ESTRUTURA sao adiadas —
                                // ver a nota em DeferredKeyOp. O menu so
                                // registra a intencao.
                                if (ImGui::MenuItem(delLabel, "Del")) {
                                    deferredOp = DeferredKeyOp::Delete;
                                    deferredRef = ref;
                                }

                                ImGui::Separator();

                                char copyLabel[64];
                                std::snprintf(copyLabel, sizeof(copyLabel),
                                    ICON_COPY " Copiar%s",
                                    selCount > 1 ? "  (selecao)" : "");

                                if (ImGui::MenuItem(copyLabel, "Ctrl+C")) {
                                    deferredOp = DeferredKeyOp::Copy;
                                    deferredRef = ref;
                                }

                                char cutLabel[64];
                                std::snprintf(cutLabel, sizeof(cutLabel),
                                    ICON_CUT " Recortar%s",
                                    selCount > 1 ? "  (selecao)" : "");

                                if (ImGui::MenuItem(cutLabel, "Ctrl+X")) {
                                    deferredOp = DeferredKeyOp::Cut;
                                    deferredRef = ref;
                                }

                                // Desabilitado com a area vazia, e nao escondido:
                                // um item que some faz o menu mudar de tamanho
                                // entre um clique e outro, e a pessoa erra o
                                // alvo. Cinza diz "existe, mas nao agora".
                                ImGui::BeginDisabled(m_KeyClipboard.empty());
                                if (ImGui::MenuItem(ICON_PASTE " Colar no playhead", "Ctrl+V")) {
                                    deferredOp = DeferredKeyOp::Paste;
                                    deferredRef = ref;
                                }
                                ImGui::EndDisabled();

                                if (ImGui::MenuItem(ICON_CLONE " Duplicar", "Ctrl+D")) {
                                    deferredOp = DeferredKeyOp::Duplicate;
                                    deferredRef = ref;
                                }

                                ImGui::SeparatorText("Selecionar");

                                if (ImGui::MenuItem("Todas as keys deste canal")) {
                                    deferredOp = DeferredKeyOp::SelectChannel;
                                    deferredRef = ref;
                                }

                                if (ImGui::MenuItem("Todas as keys desta track")) {
                                    deferredOp = DeferredKeyOp::SelectTrack;
                                    deferredRef = ref;
                                }

                                ImGui::SeparatorText(selCount > 1
                                    ? "Interpolation (selecao)" : "Interpolation");

                                // Aplica na SELECAO. Trocar a interpolacao de 40
                                // keys uma a uma nao e um fluxo, e um castigo.
                                auto applyInterp = [&](SequencerInterp mode) {
                                    if (selCount > 1) {
                                        for (const auto& sel : m_SelectedKeys)
                                            if (SequencerKey* kk = ResolveKeyRef(sel))
                                                kk->Interp = mode;
                                    }
                                    else {
                                        k.Interp = mode;
                                    }
                                    };

                                if (ImGui::MenuItem("Step"))          applyInterp(SequencerInterp::Step);
                                if (ImGui::MenuItem("Linear"))        applyInterp(SequencerInterp::Linear);
                                if (ImGui::MenuItem("CubicEaseIn"))   applyInterp(SequencerInterp::CubicEaseIn);
                                if (ImGui::MenuItem("CubicEaseOut"))  applyInterp(SequencerInterp::CubicEaseOut);
                                if (ImGui::MenuItem("Bezier"))        applyInterp(SequencerInterp::Bezier);

                                // As curvas de IMPACTO ficam num grupo proprio:
                                // sao as unicas que o animador procura quando o
                                // problema e "isto esta suave demais", e
                                // misturadas na mesma lista elas se perdiam.
                                ImGui::SeparatorText("Impacto");

                                if (ImGui::MenuItem("EaseInStrong"))   applyInterp(SequencerInterp::EaseInStrong);
                                if (ImGui::MenuItem("EaseOutStrong"))  applyInterp(SequencerInterp::EaseOutStrong);
                                if (ImGui::MenuItem("EaseInOut"))      applyInterp(SequencerInterp::EaseInOut);
                                if (ImGui::MenuItem("EaseOutBack"))    applyInterp(SequencerInterp::EaseOutBack);
                                if (ImGui::MenuItem("EaseOutBounce"))  applyInterp(SequencerInterp::EaseOutBounce);

                                ImGui::EndPopup();
                            }

                            // Desenha diamond por cima do InvisibleButton.
                            dl->AddQuadFilled(
                                ImVec2(kx, ky - r),
                                ImVec2(kx + r, ky),
                                ImVec2(kx, ky + r),
                                ImVec2(kx - r, ky),
                                keyFill);
                            dl->AddQuad(
                                ImVec2(kx, ky - r),
                                ImVec2(kx + r, ky),
                                ImVec2(kx, ky + r),
                                ImVec2(kx - r, ky),
                                IM_COL32(0, 0, 0, 255), 1.0f);
                        }
                    }
                }

                laneY += lanePitch;
            }
        }

        dl->PopClipRect();

        // ── A ACAO ADIADA, FORA DO LACO ──────────────────────────────────────
        //
        // Aqui os indices podem mudar a vontade: ninguem esta iterando a
        // estrutura. Ver a nota em DeferredKeyOp.
        switch (deferredOp) {
        case DeferredKeyOp::Delete:
            if (static_cast<int>(m_SelectedKeys.size()) > 1) {
                DeleteSelectedKeys();
            }
            else {
                m_Asset.RemoveKey(deferredRef.Binding, deferredRef.Track,
                    deferredRef.Section, deferredRef.Channel, deferredRef.Key);
                ClearKeySelection();
                m_SelectedKey = -1;
            }
            break;

        case DeferredKeyOp::Copy:      CopySelectedKeys(false);      break;
        case DeferredKeyOp::Cut:       CopySelectedKeys(true);       break;
        case DeferredKeyOp::Paste:     PasteClipboardAtPlayhead();   break;
        case DeferredKeyOp::Duplicate: DuplicateSelectedKeys();      break;

        case DeferredKeyOp::SelectChannel:
            SelectAllKeysInChannel(deferredRef.Binding, deferredRef.Track,
                deferredRef.Section, deferredRef.Channel);
            break;

        case DeferredKeyOp::SelectTrack:
            SelectAllKeysInTrack(deferredRef.Binding, deferredRef.Track);
            break;

        default: break;
        }

        // ── O RETANGULO DA CAIXA ─────────────────────────────────────────────
        //
        // Desenhado depois das lanes para ficar por cima delas, e recortado na
        // area da timeline: arrastar para fora da janela nao deve pintar por
        // cima do resto do editor.
        if (boxActive) {
            dl->PushClipRect(ImVec2(origin.x, origin.y),
                ImVec2(origin.x + size.x, origin.y + size.y), true);

            dl->AddRectFilled(boxRect.Min, boxRect.Max, IM_COL32(120, 190, 255, 40));
            dl->AddRect(boxRect.Min, boxRect.Max, IM_COL32(120, 190, 255, 200), 0.0f, 0, 1.5f);

            // Contagem ao vivo. Numa caixa que atravessa vinte lanes, "quantas
            // peguei?" e a pergunta que decide se solta agora ou continua
            // arrastando — e responde-la depois de soltar e tarde.
            if (!m_BoxHits.empty()) {
                char cnt[48];
                std::snprintf(cnt, sizeof(cnt), "%d key(s)", (int)m_BoxHits.size());
                dl->AddText(ImVec2(boxRect.Max.x + 6.0f, boxRect.Min.y),
                    IM_COL32(160, 210, 255, 255), cnt);
            }

            dl->PopClipRect();
        }

        // ── ARRASTO DO GRUPO DE KEYS ─────────────────────────────────────────
        //
        // FORA do laco de lanes, de proposito. Enquanto o drag morava dentro do
        // bloco da propria key, so a key sob o mouse podia se mover — e nao
        // havia onde pendurar o movimento das outras, porque uma key
        // selecionada pode estar numa lane que nem foi desenhada (rolada para
        // fora, ou fora da janela de zoom).
        if (m_DraggingKey) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float dx = ImGui::GetMousePos().x - m_DragStartMouseX;
                float deltaFrames = dx / frameWidth;

                // ── SNAP NO DELTA, NAO EM CADA KEY ───────────────────────────
                //
                // Arredondar key a key colapsaria o grupo: duas keys nos frames
                // 3 e 4, com snap 5, cairiam as duas no mesmo lugar e nunca mais
                // se separariam. Arredondando o DESLOCAMENTO, o grupo anda em
                // passos redondos e o espacamento interno fica intacto.
                if (m_SnapEnabled && m_SnapFrame > 0)
                    deltaFrames = std::round(deltaFrames / m_SnapFrame) * m_SnapFrame;

                // ── LIMITE DO GRUPO, NAO DE CADA KEY ─────────────────────────
                //
                // Clampando individualmente, a primeira key a encostar na borda
                // para e as outras continuam — o grupo se amassa contra o frame
                // zero e nao volta ao arrastar de volta. Limitando o delta pelos
                // extremos, o grupo inteiro para junto e preserva a forma.
                if (!m_DragOriginalFrames.empty()) {
                    float minOrig = std::numeric_limits<float>::max();
                    float maxOrig = std::numeric_limits<float>::lowest();

                    for (float f : m_DragOriginalFrames) {
                        minOrig = std::min(minOrig, f);
                        maxOrig = std::max(maxOrig, f);
                    }

                    deltaFrames = std::clamp(deltaFrames,
                        static_cast<float>(rangeStart) - minOrig,
                        static_cast<float>(rangeEnd) - maxOrig);
                }

                const std::size_t n = std::min(m_SelectedKeys.size(),
                    m_DragOriginalFrames.size());

                // ── O EIXO VERTICAL, SO NO GRAFICO ───────────────────────────
                //
                // No dope sheet a altura de uma key nao quer dizer nada (e a
                // lane dela), entao arrastar para cima nao pode mudar valor. No
                // grafico quer dizer tudo.
                //
                // SEM snap: snap e do tempo. Um valor "redondo" nao existe —
                // 15 graus e tao arbitrario quanto 14,7 —, e arredondar
                // silenciosamente destruiria a pose que o animador acabou de
                // capturar do viewport.
                float deltaValue = 0.0f;

                if (m_CurveMode && m_CurvePixelsPerUnit > 0.0001f) {
                    deltaValue = -(ImGui::GetMousePos().y - m_DragStartMouseY)
                        / m_CurvePixelsPerUnit;
                }

                for (std::size_t i = 0; i < n; ++i) {
                    if (SequencerKey* kk = ResolveKeyRef(m_SelectedKeys[i])) {
                        kk->Frame = m_DragOriginalFrames[i] + deltaFrames;

                        if (m_CurveMode && i < m_DragOriginalValues.size())
                            kk->Value = m_DragOriginalValues[i] + deltaValue;
                    }
                }
            }
            else {
                // Solto. Reordena cada canal tocado UMA vez — e traduz os
                // indices da selecao pela mesma permutacao, para o grupo
                // continuar sendo o mesmo grupo depois do arrasto.
                std::vector<KeyRef> channels;

                for (const auto& r : m_SelectedKeys) {
                    bool seen = false;
                    for (const auto& e : channels) {
                        if (e.Binding == r.Binding && e.Track == r.Track &&
                            e.Section == r.Section && e.Channel == r.Channel) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen)
                        channels.push_back(KeyRef{ r.Binding, r.Track, r.Section, r.Channel, -1 });
                }

                for (const auto& c : channels)
                    SortChannelAndRemap(c.Binding, c.Track, c.Section, c.Channel);

                m_DraggingKey = false;
                m_DragOriginalFrames.clear();
            }
        }

        // Indicador de scroll: uma barra fina na direita. Nao e arrastavel de
        // proposito — serve para dizer "tem mais coisa aqui embaixo", que era
        // exatamente a informacao que faltava.
        if (!m_CurveMode && maxScroll > 0.0f && viewHeight > 0.0f) {
            const float trackX = origin.x + size.x - 6.0f;
            dl->AddRectFilled(ImVec2(trackX, lanesTop),
                ImVec2(trackX + 4.0f, lanesBottom),
                IM_COL32(255, 255, 255, 20), 2.0f);

            const float thumbH = std::max(24.0f, viewHeight * (viewHeight / contentHeight));
            const float thumbY = lanesTop +
                (m_TimelineScrollY / maxScroll) * (viewHeight - thumbH);
            dl->AddRectFilled(ImVec2(trackX, thumbY),
                ImVec2(trackX + 4.0f, thumbY + thumbH),
                IM_COL32(200, 200, 200, 110), 2.0f);
        }

        // --- Playhead (clicavel e arrastavel) ---
        float playheadX = origin.x +
            (m_Player.GetCurrentFrame() - startFrame) * frameWidth;

        if (playheadX >= origin.x && playheadX <= origin.x + size.x) {
            // Handle de drag no topo da regua.
            ImRect playheadHandle(ImVec2(playheadX - 8, origin.y),
                ImVec2(playheadX + 8, origin.y + rulerHeight));
            ImGui::SetCursorScreenPos(playheadHandle.Min);
            ImGui::InvisibleButton("playhead_handle",
                ImVec2(playheadHandle.GetWidth(), playheadHandle.GetHeight()));

            if (ImGui::IsItemActivated() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                m_DraggingPlayhead = true;
            }
            if (m_DraggingPlayhead) {
                ImVec2 mp = ImGui::GetMousePos();
                float frameF = (mp.x - origin.x) / frameWidth + startFrame;
                if (m_SnapEnabled && m_SnapFrame > 0) {
                    frameF = std::round(frameF / m_SnapFrame) * m_SnapFrame;
                }
                // Clampa no RANGE, nao na janela visivel: com zoom, arrastar o
                // playhead ate a borda direita da tela pararia no meio da
                // sequence em vez de continuar ate o fim dela.
                frameF = std::clamp(frameF,
                    static_cast<float>(rangeStart),
                    static_cast<float>(rangeEnd));
                m_Player.Scrub(frameF);
                m_Player.SetMode(SequencerPlaybackMode::Scrubbing);
                EnsurePlayerStarted();
                m_Player.Resample();
                EvaluateAndApply();
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_DraggingPlayhead = false;
                }
            }

            // Triangulo no topo.
            bool playheadHovered = ImGui::IsItemHovered();
            ImU32 headColor = (m_DraggingPlayhead || playheadHovered)
                ? IM_COL32(255, 120, 120, 255)
                : IM_COL32(255, 80, 80, 255);
            dl->AddTriangleFilled(
                ImVec2(playheadX - 7, origin.y),
                ImVec2(playheadX + 7, origin.y),
                ImVec2(playheadX, origin.y + 9),
                headColor);

            // Linha vertical ate embaixo.
            dl->AddLine(ImVec2(playheadX, origin.y + 9),
                ImVec2(playheadX, origin.y + size.y),
                headColor, 2.0f);
        }

        // Click na regua (sem ser no handle): scrub imediato.
        ImGui::SetCursorScreenPos(rulerRect.Min);
        ImGui::InvisibleButton("ruler_scrub",
            ImVec2(rulerRect.GetWidth(), rulerRect.GetHeight()));
        if (!m_DraggingPlayhead &&
            ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImVec2 mp = ImGui::GetMousePos();
            float frameF = (mp.x - origin.x) / frameWidth + startFrame;
            if (m_SnapEnabled && m_SnapFrame > 0) {
                frameF = std::round(frameF / m_SnapFrame) * m_SnapFrame;
            }
            frameF = std::clamp(frameF, static_cast<float>(rangeStart),
                static_cast<float>(rangeEnd));
            m_Player.Scrub(frameF);
            m_Player.SetMode(SequencerPlaybackMode::Scrubbing);
            EnsurePlayerStarted();
            m_Player.Resample();
            EvaluateAndApply();
            m_DraggingPlayhead = true;  // começa drag a partir do click
        }

        // Double-click na lane: adiciona key. E, desde a caixa de selecao, a
        // area onde um arrasto no vazio comeca a selecionar.
        //
        // O fundo vai ate `lanesBottom`, e nao ate a ultima lane: com tres
        // tracks sobra meia janela vazia, e comecar a caixa ali e o gesto mais
        // natural que existe — de fora, para dentro.
        ImRect lanesRect(ImVec2(origin.x, origin.y + rulerHeight),
            ImVec2(origin.x + size.x, std::max(laneY, lanesBottom)));
        ImGui::SetCursorScreenPos(lanesRect.Min);
        ImGui::InvisibleButton("lanes_click",
            ImVec2(lanesRect.GetWidth(), lanesRect.GetHeight()));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (m_SelectedBinding >= 0 && m_SelectedTrack >= 0 &&
                m_SelectedSection >= 0 && m_SelectedChannel >= 0) {
                AddKeyAtPlayhead(m_SelectedBinding, m_SelectedTrack,
                    m_SelectedSection, m_SelectedChannel);
            }
        }

        // ── COMECA A CAIXA ───────────────────────────────────────────────────
        //
        // Este botao so fica "hovered" quando NAO ha key sob o cursor: as keys
        // sao submetidas antes e, no ImGui, o primeiro item a reivindicar o
        // hover vence. Entao "hovered aqui" e exatamente "clicou no vazio".
        //
        // Os outros tres estados sao testados porque um arrasto de key, do
        // playhead ou de pan pode passar por cima desta area, e comecar uma
        // caixa no meio deles seria um segundo gesto por cima do primeiro.
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !m_DraggingKey && !m_DraggingPlayhead && !m_PanningTimeline) {
            const ImGuiIO& io = ImGui::GetIO();

            m_BoxSelecting = true;
            m_BoxStartPos = ImGui::GetMousePos();
            m_BoxMode = io.KeyShift ? BoxMode::Add
                : io.KeyCtrl ? BoxMode::Remove
                : BoxMode::Replace;
            m_BoxHits.clear();
        }

        // ── E FECHA ──────────────────────────────────────────────────────────
        //
        // Fora do teste de hover, de proposito: soltar o botao com o cursor ja
        // fora da timeline (ou por cima de uma key) tem de valer igual. Sem
        // isso, uma caixa arrastada para fora ficaria acesa para sempre.
        //
        // `m_BoxHits` foi colhido pelo laco DESTE frame, entao neste ponto ele
        // descreve o retangulo exato que o usuario esta vendo.
        if (m_BoxSelecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ApplyBoxSelection();
            m_BoxSelecting = false;
            m_BoxHits.clear();
        }
    }

    // ============================================================
    // Transport bar (com icones)
    // ============================================================

    void SequencerWindow::DrawTransportBar() {
        ImGui::Separator();
        ImGui::Spacing();

        SequencerPlaybackMode mode = m_Player.GetMode();
        bool isPlaying = (mode == SequencerPlaybackMode::Playing);

        // Play / Pause com icone.
        if (ui::IconButton(isPlaying ? ICON_PAUSE : ICON_PLAY,
            isPlaying ? "Pausar" : "Tocar (Espaco)",
            ui::Accent::Primary)) {
            if (isPlaying) {
                m_Player.SetMode(SequencerPlaybackMode::Paused);
            }
            else {
                EnsurePlayerStarted();

                // Play com o playhead no FIM rebobina, em vez de nao fazer nada.
                // Sem isto o OnUpdate clampa e volta para Paused no mesmo frame —
                // o usuario clica em Play e a agulha fica parada em 90/90, que e
                // indistinguivel de "quebrado".
                const float startF = static_cast<float>(m_Asset.GetFrameRange().Start);
                const float endF = static_cast<float>(m_Asset.GetFrameRange().End);
                if (m_Player.GetCurrentFrame() >= endF - 0.001f)
                    m_Player.Scrub(startF);

                m_Player.SetMode(SequencerPlaybackMode::Playing);
            }
        }
        ImGui::SameLine();

        if (ui::IconButton(ICON_STOP, "Parar e voltar ao inicio")) {
            StopPlayer();
            m_Player.Scrub(static_cast<float>(m_Asset.GetFrameRange().Start));
        }
        ImGui::SameLine();

        if (ui::ToggleButton(ICON_ARROWS_LEFT_RIGHT " Loop", m_Player.GetLoop(),
            "Loop na reproducao")) {
            m_Player.SetLoop(!m_Player.GetLoop());
        }

        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();

        // O denominador e o frame FINAL, nao a contagem de frames: a agulha
        // caminha ate End, e mostrar (End - Start) so coincide quando Start e 0.
        const int curFrame = static_cast<int>(m_Player.GetCurrentFrame());
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f),
            "Frame: %d / %d", curFrame, m_Asset.GetFrameRange().End);
        ImGui::SameLine();

        ImGui::SetNextItemWidth(220);
        float frame = m_Player.GetCurrentFrame();
        if (ImGui::SliderFloat("##frame_slider",
            &frame,
            static_cast<float>(m_Asset.GetFrameRange().Start),
            static_cast<float>(m_Asset.GetFrameRange().End),
            "%.0f")) {
            if (m_SnapEnabled && m_SnapFrame > 0) {
                frame = std::round(frame / m_SnapFrame) * m_SnapFrame;
            }
            m_Player.Scrub(frame);
            m_Player.SetMode(SequencerPlaybackMode::Scrubbing);
            EnsurePlayerStarted();
            m_Player.Resample();
            EvaluateAndApply();
        }
    }

    // ============================================================
    // Hooks de integracao
    // ============================================================

    // ── POR QUE ESTE ARQUIVO NAO ESCREVE MAIS NO BonePalette ─────────────────
    //
    // A versao anterior lia e escrevia direto em `SkeletalMeshComponent::BonePalette`.
    // Isso nao podia funcionar, por tres motivos independentes:
    //
    //   1. BonePalette NAO e a pose. E o resultado FINAL do skinning:
    //      `globalDoOsso * inverseBindPose`. Decompor essa matriz e trocar "X"
    //      nao move o osso — devolve um numero sem significado geometrico (era
    //      dai que vinha o 50.000 aparecendo no painel de key).
    //
    //   2. Escrever numa matriz de skinning NAO propaga para os filhos. Mover o
    //      Hips deixaria as pernas para tras: a hierarquia so existe enquanto a
    //      pose e LOCAL, antes do BuildSkinningMatrices compor os globais.
    //
    //   3. O AnimationWorld reescreve o BonePalette inteiro a cada frame, para
    //      toda entidade com AnimGraph/BlendSpace/clipe. O que o Sequencer
    //      escrevia durava menos de um frame. `PreviewInEditor` nao resolve isso:
    //      ele so decide se o TEMPO avanca — a pose continua sendo recalculada e
    //      regravada. Por isso existe agora o `PoseOverride`.
    //
    // O caminho certo ja existia no engine, e e o mesmo que o preview do Control
    // Rig usa (rig_preview.cpp:332): montar uma `Pose` (transforms LOCAIS por
    // osso) e chamar `AnimationSampler::BuildSkinningMatrices`.

    entt::entity SequencerWindow::ResolveBindingEntity(const SequencerBinding& b) const {
        if (!m_Context || !m_Context->ActiveScene) return entt::null;
        if (b.EntityName.empty()) return entt::null;

        entt::entity e = m_Context->ActiveScene->FindByName(b.EntityName);
        if (e == entt::null) return entt::null;

        auto& reg = m_Context->ActiveScene->GetRegistry();
        if (!reg.valid(e)) return entt::null;

        return e;
    }

    const Skeleton* SequencerWindow::GetBindingSkeleton(int bindingIndex,
        SkeletalMeshComponent** outSmc) const {
        if (outSmc) *outSmc = nullptr;

        const auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return nullptr;

        entt::entity e = ResolveBindingEntity(*b);
        if (e == entt::null) return nullptr;

        auto& reg = m_Context->ActiveScene->GetRegistry();
        auto* smc = reg.try_get<SkeletalMeshComponent>(e);
        if (!smc || !smc->Data) return nullptr;

        if (outSmc) *outSmc = smc;
        return smc->GetSkeleton();
    }

    namespace {

        // ── A MATEMATICA MUDOU DE ENDERECO ───────────────────────────────────
        //
        // `WriteComponent`, `RotationAxisOf` e `BoneEulerEdit` moravam aqui,
        // dentro de um namespace anonimo de um arquivo do EDITOR. Isso queria
        // dizer que o runtime nao tinha como alcancar nenhum deles — e a
        // cutscene, para tocar no jogo, precisa exatamente destas contas.
        //
        // Agora vivem em `axe/animation/sequencer/sequence_apply.hpp`, e os dois
        // lados chamam as mesmas funcoes. Ver a nota de topo daquele arquivo
        // sobre por que uma segunda implementacao seria o pior desfecho
        // possivel para uma ferramenta de cutscene.
        using sequence::BoneEulerEdit;
        using sequence::RotationAxisOf;
        using sequence::WriteComponent;

        // Assinatura numerica de uma palette de skinning.
        //
        // Serve para uma pergunta so: "a palette que eu escrevi no frame passado
        // ainda e a que esta la?". Nao precisa ser hash criptografico — precisa
        // mudar quando qualquer osso mudar, e ser barata. O peso por indice evita
        // que trocas simetricas se cancelem.
        double PaletteStamp(const std::vector<glm::mat4>& palette) {
            double acc = 0.0;
            for (std::size_t i = 0; i < palette.size(); ++i) {
                const float* p = &palette[i][0][0];
                for (int k = 0; k < 16; ++k)
                    acc += static_cast<double>(p[k]) * static_cast<double>(i * 16 + k + 1);
            }
            return acc;
        }

        // ── QUATERNION -> EULER XYZ (graus) CONTINUO ─────────────────────────
        //
        // Usado so pelo ExplodeClipTrack, e ele nao funciona sem isto.
        //
        // `glm::eulerAngles` devolve SEMPRE a representacao canonica (Y em
        // [-90,90], X e Z em [-180,180]). O problema e que a mesma rotacao tem
        // infinitas representacoes em Euler, e uma curva suave em quaternion
        // atravessa esses limites o tempo todo. Convertendo key a key sem
        // cuidado, dois frames vizinhos saem como -179 e +179 — o Sequencer
        // interpola linearmente entre eles e o osso da um giro de 358 graus no
        // lugar de andar 2. E o "bake que gira o boneco" classico.
        //
        // Duas correcoes, nesta ordem:
        //   1. A representacao ALTERNATIVA (x+180, 180-y, z+180) descreve a
        //      mesma rotacao e as vezes esta muito mais perto da key anterior —
        //      e o que resolve a virada de ramo no polo.
        //   2. Desembrulho de +-360 em cada eixo, aproximando da key anterior.
        //
        // O resultado pode passar de 180 graus (uma volta completa vira 360, nao
        // 0) e isso e desejado: e o que faz a curva ser continua e editavel.
        // WriteComponent aceita qualquer faixa — `glm::quat(radians(euler))` nao
        // se importa com voltas.
        glm::vec3 ContinuousEuler(const glm::quat& q, const glm::vec3& prev, bool hasPrev) {
            const glm::vec3 canonical = glm::degrees(glm::eulerAngles(q));
            if (!hasPrev) return canonical;

            const glm::vec3 alternate(canonical.x + 180.0f,
                180.0f - canonical.y,
                canonical.z + 180.0f);

            auto unwrap = [&prev](glm::vec3 v) {
                for (int i = 0; i < 3; ++i)
                    v[i] += 360.0f * std::round((prev[i] - v[i]) / 360.0f);
                return v;
                };

            const glm::vec3 a = unwrap(canonical);
            const glm::vec3 b = unwrap(alternate);

            return (glm::length(b - prev) < glm::length(a - prev)) ? b : a;
        }

        float ReadComponent(const BoneTransform& t, SequencerChannelComponent c) {
            const glm::vec3 euler = glm::degrees(glm::eulerAngles(t.Rotation));

            switch (c) {
            case SequencerChannelComponent::X:      return t.Translation.x;
            case SequencerChannelComponent::Y:      return t.Translation.y;
            case SequencerChannelComponent::Z:      return t.Translation.z;
            case SequencerChannelComponent::RotX:   return euler.x;
            case SequencerChannelComponent::RotY:   return euler.y;
            case SequencerChannelComponent::RotZ:   return euler.z;
            case SequencerChannelComponent::ScaleX: return t.Scale.x;
            case SequencerChannelComponent::ScaleY: return t.Scale.y;
            case SequencerChannelComponent::ScaleZ: return t.Scale.z;
            }
            return 0.0f;
        }

    } // namespace

    bool SequencerWindow::CaptureBoneValue(int bindingIndex,
        const std::string& boneName,
        SequencerChannelComponent component,
        float& outValue) const {
        const Skeleton* skel = GetBindingSkeleton(bindingIndex);
        if (!skel) return false;

        const int boneIdx = skel->FindBone(boneName);
        if (boneIdx < 0) return false;

        // Le da POSE DE TRABALHO — que e o que esta na tela. Ler do BonePalette
        // devolveria a matriz de skinning; ler da bind pose ignoraria tudo que o
        // usuario ja autorou nos frames anteriores.
        if (bindingIndex >= 0 && bindingIndex < (int)m_WorkPoses.size()) {
            const Pose& p = m_WorkPoses[bindingIndex];
            if (boneIdx < (int)p.Size()) {
                outValue = ReadComponent(p[boneIdx], component);
                return true;
            }
        }

        // Sem pose de trabalho ainda (primeira key logo apos criar a track):
        // o valor de repouso do osso e a resposta certa — capturar zero moveria
        // o osso no instante em que a key nasce.
        Pose bind;
        Pose::FromBindPose(*skel, bind);
        if (boneIdx >= (int)bind.Size()) return false;

        outValue = ReadComponent(bind[boneIdx], component);
        return true;
    }

    bool SequencerWindow::CaptureChannelValue(int bindingIndex,
        const SequencerTrack& track,
        SequencerChannelComponent component,
        float& outValue) const {
        // ── CONTROLE DE RIG ──────────────────────────────────────────────────
        //
        // Le `RigElement::Value`, que e a pose do animador — nao `Current`, que
        // e o resultado do solve. Capturar o Current transformaria o efeito do
        // grafo (um Foot IK, um Follow Bone) numa key fixa, e o rig deixaria de
        // reagir a qualquer coisa a partir dali.
        if (track.TargetType == SequencerTargetType::Control) {
            outValue = (component == SequencerChannelComponent::ScaleX ||
                component == SequencerChannelComponent::ScaleY ||
                component == SequencerChannelComponent::ScaleZ) ? 1.0f : 0.0f;

            auto it = m_Rigs.find(bindingIndex);
            if (it == m_Rigs.end() || !it->second.Ready) return true;

            const int idx = it->second.Hierarchy.Find(track.TargetName,
                RigElementType::Control);
            if (idx < 0) return true;

            const RigElement& el = it->second.Hierarchy[idx];

            // Canal: o unico canal da track carrega FloatValue. Nao passa pelo
            // switch de componentes abaixo — ali X quer dizer "translacao X", e
            // aqui quer dizer "o valor".
            if (el.ValueType != RigControlValue::Transform) {
                outValue = el.FloatValue;
                return true;
            }

            const glm::vec3 euler = glm::degrees(glm::eulerAngles(el.Value.Rotation));

            switch (component) {
            case SequencerChannelComponent::X:      outValue = el.Value.Translation.x; break;
            case SequencerChannelComponent::Y:      outValue = el.Value.Translation.y; break;
            case SequencerChannelComponent::Z:      outValue = el.Value.Translation.z; break;
            case SequencerChannelComponent::RotX:   outValue = euler.x; break;
            case SequencerChannelComponent::RotY:   outValue = euler.y; break;
            case SequencerChannelComponent::RotZ:   outValue = euler.z; break;
            case SequencerChannelComponent::ScaleX: outValue = el.Value.Scale.x; break;
            case SequencerChannelComponent::ScaleY: outValue = el.Value.Scale.y; break;
            case SequencerChannelComponent::ScaleZ: outValue = el.Value.Scale.z; break;
            }
            return true;
        }

        // ── ENTIDADE ─────────────────────────────────────────────────────────
        //
        // Le o transform que esta na cena AGORA. Como o ApplyEntityTransforms
        // parte do original guardado e sobrepoe so os canais animados, o que se
        // captura aqui e exatamente o que se ve — inclusive nos eixos que a
        // sequence ainda nao anima.
        if (track.TargetType == SequencerTargetType::Entity) {
            outValue = (component == SequencerChannelComponent::ScaleX ||
                component == SequencerChannelComponent::ScaleY ||
                component == SequencerChannelComponent::ScaleZ) ? 1.0f : 0.0f;

            if (!m_Context || !m_Context->ActiveScene) return true;

            const auto* b = m_Asset.GetBinding(bindingIndex);
            if (!b) return true;

            const entt::entity e = ResolveBindingEntity(*b);
            if (e == entt::null) return true;

            auto& reg = m_Context->ActiveScene->GetRegistry();
            const auto* tc = reg.try_get<TransformComponent>(e);
            if (!tc) return true;

            switch (component) {
            case SequencerChannelComponent::X:      outValue = tc->Data.Position.x; break;
            case SequencerChannelComponent::Y:      outValue = tc->Data.Position.y; break;
            case SequencerChannelComponent::Z:      outValue = tc->Data.Position.z; break;
                // RADIANOS no Transform, GRAUS na curva — a mesma fronteira do
                // socket, e o mesmo motivo.
            case SequencerChannelComponent::RotX:   outValue = glm::degrees(tc->Data.Rotation.x); break;
            case SequencerChannelComponent::RotY:   outValue = glm::degrees(tc->Data.Rotation.y); break;
            case SequencerChannelComponent::RotZ:   outValue = glm::degrees(tc->Data.Rotation.z); break;
            case SequencerChannelComponent::ScaleX: outValue = tc->Data.Scale.x; break;
            case SequencerChannelComponent::ScaleY: outValue = tc->Data.Scale.y; break;
            case SequencerChannelComponent::ScaleZ: outValue = tc->Data.Scale.z; break;
            }
            return true;
        }

        if (track.Type != SequencerTrackType::TransformSocket)
            return CaptureBoneValue(bindingIndex, track.TargetName, component, outValue);

        // NEUTRO do socket, e nao zero: escala 1, offset 0. Cravar zero em
        // ScaleX no instante em que a key nasce sumiria com a arma — o mesmo
        // erro que a track de osso ja evita nascendo sem canais.
        switch (component) {
        case SequencerChannelComponent::ScaleX:
        case SequencerChannelComponent::ScaleY:
        case SequencerChannelComponent::ScaleZ: outValue = 1.0f; break;
        default:                                outValue = 0.0f; break;
        }

        // Se ja existe preview na cena, o valor que o usuario esta VENDO vale
        // mais que o neutro teorico.
        if (!m_Context || !m_Context->ActiveScene) return true;

        const std::string key = std::to_string(bindingIndex) + "|" + track.TargetName;
        auto it = m_SocketPreviews.find(key);
        if (it == m_SocketPreviews.end()) return true;

        auto& reg = m_Context->ActiveScene->GetRegistry();
        if (it->second.Entity == entt::null || !reg.valid(it->second.Entity)) return true;

        const auto* tc = reg.try_get<TransformComponent>(it->second.Entity);
        if (!tc) return true;

        switch (component) {
        case SequencerChannelComponent::X:      outValue = tc->Data.Position.x; break;
        case SequencerChannelComponent::Y:      outValue = tc->Data.Position.y; break;
        case SequencerChannelComponent::Z:      outValue = tc->Data.Position.z; break;
            // Radianos no Transform, graus na curva — ver a nota simetrica em
            // SyncSocketPreviews.
        case SequencerChannelComponent::RotX:   outValue = glm::degrees(tc->Data.Rotation.x); break;
        case SequencerChannelComponent::RotY:   outValue = glm::degrees(tc->Data.Rotation.y); break;
        case SequencerChannelComponent::RotZ:   outValue = glm::degrees(tc->Data.Rotation.z); break;
        case SequencerChannelComponent::ScaleX: outValue = tc->Data.Scale.x;    break;
        case SequencerChannelComponent::ScaleY: outValue = tc->Data.Scale.y;    break;
        case SequencerChannelComponent::ScaleZ: outValue = tc->Data.Scale.z;    break;
        }
        return true;
    }

    void SequencerWindow::EvaluateAndApply() {
        if (!m_Context || !m_Context->ActiveScene) return;

        auto& reg = m_Context->ActiveScene->GetRegistry();
        // Keys + pose pendente, nesta ordem de precedencia. UMA vez por frame,
        // antes de qualquer consumidor — os tres (pose de osso, SolveRig,
        // preview de socket) leem desta lista.
        RebuildEffectiveSamples();
        const auto& samples = m_EffectiveSamples;

        // ── ANTES DO LACO DE POSE ────────────────────────────────────────────
        //
        // Independe de esqueleto, entao nao pode viver dentro de um laco que
        // desiste do binding quando nao ha um. Ver ApplyEntityTransforms.
        ApplyEntityTransforms();
        const auto& clipSamples = m_Player.GetLastClipSamples();

        const int bindingCount = static_cast<int>(m_Asset.GetBindingCount());
        m_WorkPoses.resize(bindingCount);
        m_PaletteStamp.resize(bindingCount, 0.0);
        m_AppliedBindings = 0;

        // Ninguem roubou a palette ate prova em contrario, e a prova so existe se
        // ja escrevemos pelo menos um frame.
        bool stolen = false;

        // Reaproveitado entre bindings: no maximo alguns ossos por sequence.
        std::vector<BoneEulerEdit> rotEdits;

        for (int bi = 0; bi < bindingCount; ++bi) {
            const auto* b = m_Asset.GetBinding(bi);
            if (!b) continue;

            SkeletalMeshComponent* smc = nullptr;
            const Skeleton* skel = GetBindingSkeleton(bi, &smc);

            // ── ESQUELETO E OPCIONAL ─────────────────────────────────────────
            //
            // Este `continue` descartava o binding INTEIRO quando nao havia
            // esqueleto — e uma camera nao tem nenhum. O transform da entidade
            // ja foi aplicado antes deste laco (ApplyEntityTransforms); daqui
            // para baixo e so o caminho de pose, que so faz sentido com ossos.
            if (!skel || !smc) {
                if (BindingHasEntityTrack(bi)) ++m_AppliedBindings;
                continue;
            }

            // 0. A palette ainda e a que deixamos no frame anterior?
            //
            //    Se nao for, alguem escreveu depois de nos — na pratica, o
            //    AnimationWorld sem o guard `if (skel.PoseOverride) continue;`.
            //    Esse era o modo de falha mais cruel do Sequencer: TUDO daqui
            //    para tras funciona e reporta saude ("1/1 binding aplicado,
            //    1 sample"), e mesmo assim o personagem nao se mexe, porque a
            //    escrita e desfeita alguns microssegundos depois.
            if (m_PaletteStampValid && !smc->BonePalette.empty()) {
                const double now = PaletteStamp(smc->BonePalette);
                if (std::abs(now - m_PaletteStamp[bi]) > 1e-4)
                    stolen = true;
            }

            // 1. CAMADA BASE.
            //
            //    Sem track de clipe: a pose de repouso. Todo osso que a sequence
            //    NAO anima fica exatamente onde o esqueleto o coloca — e por isso
            //    animar so o Hips nao desmonta o resto do personagem.
            //
            //    Com track de clipe: a pose do clipe no tempo da section. E o que
            //    torna "pegar uma animacao da Mixamo e ajustar" possivel — o
            //    animador nao reconstroi a performance, ele corrige por cima.
            //
            //    Se a section aponta para um clipe que nao existe mais na entidade
            //    (renomeado, ou .axeskel trocado), cai na bind pose em vez de
            //    congelar: a track fica visivelmente inerte, e o outliner marca o
            //    clipe como nao resolvido.
            Pose& pose = m_WorkPoses[bi];

            const AnimationClip* baseClip = nullptr;
            float baseTime = 0.0f;

            for (const auto& cs : clipSamples) {
                if (cs.BindingIndex != bi) continue;
                if (const AnimationClip* c = FindBindingClip(bi, cs.ClipName)) {
                    baseClip = c;
                    baseTime = cs.TimeSeconds;
                    break;   // a primeira track de clipe ativa vence (sem blend na Fase 2)
                }
            }

            if (baseClip)
                AnimationSampler::SamplePose(*skel, *baseClip, baseTime, pose);
            else
                Pose::FromBindPose(*skel, pose);

            // 2. Sobrepoe os canais autorados, em espaco LOCAL.
            //
            //    Translacao e escala vao direto. Rotacao vai para o acumulador:
            //    ver a nota em BoneEulerEdit sobre por que escrever RotX e RotZ
            //    um depois do outro perdia a primeira edicao.
            //
            //    A conta e a MESMA que o SequenceWorld chama no Play. Ver
            //    sequence_apply.hpp.
            sequence::ApplyBoneSamples(pose, *skel, samples, bi, rotEdits);

            // 2c. CONTROL RIG, por ultimo entre as camadas de pose.
            //
            //     A ordem e a mesma do runtime: o AnimNode_ControlRig comeca
            //     por EvalInput e MODIFICA a pose que chegou. Aqui a pose que
            //     chega e clipe + tracks de osso — entao o rig corrige por cima
            //     de tudo, que e o que faz um controle de pe conseguir plantar
            //     o pe de uma animacao importada.
            //
            //     Rodar antes das tracks de osso inverteria isso: o FK bruto
            //     sobrescreveria o solve, e o Foot IK do rig nao teria efeito
            //     nenhum em qualquer osso que o animador tivesse tocado.
            if (!b->RigAssetUUID.empty())
                SolveRig(bi, *skel, pose);

            // 3. A janela e a dona da pose desta entidade enquanto estiver aberta.
            //    Sem isto o AnimationWorld reescreve o BonePalette no mesmo frame
            //    e nada do que fizemos acima aparece.
            smc->PoseOverride = true;

            // 4. Pose local -> matrizes de skinning. Aqui a hierarquia e composta:
            //    mover um osso leva os filhos junto, que e o comportamento que
            //    faltava.
            // ── QUEM PRECISA DAS GLOBALS ─────────────────────────────────────
            //
            // `_WantsBoneGlobals` e recontado pelo AnimationWorld a partir dos
            // SocketAttachmentComponent que EXISTEM. As nossas entidades de
            // preview so nascem depois desta funcao rodar, entao no primeiro
            // frame de cada preview a flag ainda esta falsa, BoneGlobals fica
            // vazio e a arma pisca na origem do personagem.
            //
            // Perguntar direto ao asset "este binding tem track de socket?"
            // resolve sem depender da ordem entre os dois sistemas.
            bool wantsGlobals = smc->ShowSkeleton || smc->_WantsBoneGlobals;

            if (!wantsGlobals) {
                for (const auto& tr : b->Tracks) {
                    if (tr.Type == SequencerTrackType::TransformSocket) {
                        wantsGlobals = true;
                        break;
                    }
                }
            }

            AnimationSampler::BuildSkinningMatrices(
                *skel, pose, smc->BonePalette,
                wantsGlobals ? &smc->BoneGlobals : nullptr);

            m_PaletteStamp[bi] = PaletteStamp(smc->BonePalette);
            ++m_AppliedBindings;
        }

        m_PaletteStolen = m_PaletteStampValid && stolen;
        m_PaletteStampValid = (m_AppliedBindings > 0);

        // Depois das poses, nunca antes: o preview do socket le BoneGlobals, que
        // so existe a partir do BuildSkinningMatrices acima.
        SyncSocketPreviews();
    }

    void SequencerWindow::ReleasePoseOverride() {
        if (!m_Context || !m_Context->ActiveScene) return;

        auto& reg = m_Context->ActiveScene->GetRegistry();

        // Varre TODAS as entidades com esqueleto, e nao so as ligadas: um binding
        // removido (ou renomeado) enquanto a janela estava aberta deixaria a
        // entidade congelada para sempre, sem ninguem para desligar o flag.
        for (auto entity : reg.view<SkeletalMeshComponent>()) {
            auto& smc = reg.get<SkeletalMeshComponent>(entity);
            smc.PoseOverride = false;
        }

        // A partir daqui a palette e legitimamente de outro dono: comparar com o
        // que deixamos la so produziria um falso positivo de "sobrescrita".
        m_PaletteStampValid = false;
        m_PaletteStolen = false;

        // As armas de preview vao junto. Elas so fazem sentido enquanto a janela
        // esta dirigindo a pose; deixadas para tras, ficariam presas ao socket
        // seguindo a animacao do AnimationWorld, sem ninguem que as reconhecesse.
        ClearSocketPreviews();

        // E os transforms das entidades que a sequence estava dirigindo. Ver a
        // nota em m_EntityRestore: isto e o PoseOverride das entidades.
        RestoreEntityTransforms();

        // E o gizmo. Um pedido esquecido no viewport continuaria manipulando o
        // osso de uma sequence que nem esta mais aberta — com o agravante de
        // que o gizmo externo SUPRIME o de entidade, entao o usuario perderia o
        // gizmo normal sem entender por que.
        if (m_Context && m_Context->Viewport) {
            m_Context->Viewport->ClearExternalGizmo();
            m_Context->Viewport->ClearExternalOverlay();
        }

        m_ViewportControlBinding = -1;
        m_ViewportControlName.clear();
        m_ViewportHovered.clear();
        ClearTargetSelection();

        // As copias de trabalho do rig tambem: elas guardam a pose do animador
        // (RigElement::Value) e uma cena trocada com a janela aberta as deixaria
        // apontando para o personagem errado.
        ClearRigs();
    }

    // ============================================================
    // Helpers
    // ============================================================

    void SequencerWindow::AddKeyAtPlayhead(int bindingIndex, int trackIndex,
        int sectionIndex, int channelIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;
        auto& tr = b->Tracks[trackIndex];
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tr.Sections.size())) return;
        auto& sec = tr.Sections[sectionIndex];
        if (channelIndex < 0 || channelIndex >= static_cast<int>(sec.Channels.size())) return;
        auto& ch = sec.Channels[channelIndex];

        float value = 0.0f;
        // Valor inicial = o que o osso JA tem na pose de trabalho. Cravar zero
        // faria a key nascer movendo o osso, que e o oposto do esperado.
        if (!CaptureChannelValue(bindingIndex, tr, ch.Component, value)) {
            value = 0.0f;
        }

        SequencerKey k;
        k.Frame = m_Player.GetCurrentFrame();
        k.Value = value;
        k.Interp = SequencerInterp::Linear;
        int ki = m_Asset.AddKey(bindingIndex, trackIndex, sectionIndex, channelIndex, k);
        m_SelectedKey = ki;
    }

    int SequencerWindow::CreateBindingForEntity(entt::entity entity) {
        if (!m_Context || !m_Context->ActiveScene) return -1;

        auto& reg = m_Context->ActiveScene->GetRegistry();
        if (!reg.valid(entity)) return -1;

        // ── NAO EXIGE ESQUELETO ──────────────────────────────────────────────
        //
        // Exigia, e isso impedia o caso que motivou tudo isto: uma CAMERA de
        // cutscene nao tem esqueleto nenhum. O que um binding realmente precisa
        // e de um transform (para animar a entidade) e de um NOME (para religar
        // depois do load) — esqueleto so quando ha track de osso.
        if (!reg.try_get<TransformComponent>(entity)) return -1;

        auto* nm = reg.try_get<NameComponent>(entity);
        if (!nm || nm->Name.empty()) return -1;   // sem nome nao ha como religar

        // Ja existe binding para esta entidade? Duplicar produziria duas timelines
        // disputando a mesma pose, e a ultima a ser avaliada ganharia — bug mudo.
        for (int i = 0; i < (int)m_Asset.GetBindingCount(); ++i) {
            if (const auto* b = m_Asset.GetBinding(i))
                if (b->EntityName == nm->Name) return i;
        }

        SequencerBinding b;
        b.EntityName = nm->Name;
        b.DisplayName = nm->Name;
        const int idx = m_Asset.AddBinding(b);

        // NENHUMA track por default.
        //
        // Criar uma track "Hand_R" chutada gerava dois problemas de uma vez: o
        // osso nao existe num esqueleto Mixamo (la e "mixamorig:RightHand"), e a
        // track nascia com keys de valor zero, que zeram a translacao local do
        // osso assim que a avaliacao roda. O usuario escolhe o osso no picker.
        // Nao chamar OnStart aqui: ele reseta o playhead para o inicio, e
        // adicionar um segundo personagem no meio de um scrub jogaria o tempo
        // fora. O SyncFrom por frame (ver Draw) ja leva o binding novo.
        EnsurePlayerStarted();
        return idx;
    }

    int SequencerWindow::CreateTransformTrack(int bindingIndex,
        const std::string& boneName) {
        const Skeleton* skel = GetBindingSkeleton(bindingIndex);
        if (!skel) return -1;
        if (skel->FindBone(boneName) < 0) return -1;

        SequencerTrack tr;
        tr.Type = SequencerTrackType::TransformBone;
        tr.TargetName = boneName;
        tr.TargetType = SequencerTargetType::Bone;

        SequencerSection sec;
        sec.StartFrame = m_Asset.GetFrameRange().Start;
        sec.EndFrame = m_Asset.GetFrameRange().End;

        // Canais VAZIOS. Um canal sem key nao e sampleado (o player pula), entao
        // o osso continua na pose de repouso ate o usuario cravar a primeira key
        // — que e capturada do valor atual, nao de zero.
        //
        // Os seis canais de translacao e rotacao entram juntos porque e assim que
        // se autora uma pose: gira e move o mesmo osso no mesmo frame. Escala fica
        // de fora por padrao (raramente animada, e so poluiria a lista).
        for (auto comp : { SequencerChannelComponent::X,
                          SequencerChannelComponent::Y,
                          SequencerChannelComponent::Z,
                          SequencerChannelComponent::RotX,
                          SequencerChannelComponent::RotY,
                          SequencerChannelComponent::RotZ }) {
            SequencerChannel ch;
            ch.Component = comp;
            sec.Channels.push_back(ch);
        }

        tr.Sections.push_back(sec);
        const int ti = m_Asset.AddTrack(bindingIndex, tr);

        EnsurePlayerStarted();   // o SyncFrom por frame leva a track nova
        return ti;
    }

    const AnimationClip* SequencerWindow::FindBindingClip(int bindingIndex,
        const std::string& clipName) const {
        if (clipName.empty()) return nullptr;

        SkeletalMeshComponent* smc = nullptr;
        if (!GetBindingSkeleton(bindingIndex, &smc) || !smc) return nullptr;

        // Resolucao POR NOME, igual ao AnimGraph (anim_graph_asset.cpp: findClip).
        // Indice nao serve: a ordem de Clips depende da ordem de importacao e do
        // makeUnique de nomes do Resolve(), entao importar uma animacao nova
        // reapontaria silenciosamente todas as sections.
        for (const auto& c : smc->Clips) {
            if (c && c->GetName() == clipName)
                return c.get();
        }
        return nullptr;
    }

    int SequencerWindow::CreateClipTrack(int bindingIndex, const std::string& clipName) {
        const AnimationClip* clip = FindBindingClip(bindingIndex, clipName);
        if (!clip) return -1;

        const int fps = (m_Asset.GetFps() > 0) ? m_Asset.GetFps() : 30;

        // Duracao do clipe em frames. RateScale entra aqui porque e o multiplicador
        // de velocidade que o AnimClipWindow ja deixa o usuario editar — ignora-lo
        // faria a section ter um comprimento diferente do que o clipe realmente
        // ocupa quando tocado pelo engine.
        const float rate = (clip->RateScale > 0.0001f) ? clip->RateScale : 1.0f;
        const float seconds = clip->GetDuration() / rate;
        int lengthInFrames = static_cast<int>(std::ceil(seconds * static_cast<float>(fps)));
        if (lengthInFrames < 1) lengthInFrames = 1;

        SequencerTrack tr;
        tr.Type = SequencerTrackType::AnimationClip;
        tr.TargetName = clipName;
        // Null e o alvo certo: a track nao endereca um osso, ela dirige a pose
        // inteira. Marcar como Bone faria o EvaluateAndApply tentar achar um osso
        // chamado "Walking" e falhar em silencio.
        tr.TargetType = SequencerTargetType::Null;

        SequencerSection sec;
        sec.StartFrame = static_cast<int>(m_Player.GetCurrentFrame());
        sec.EndFrame = sec.StartFrame + lengthInFrames;
        sec.SourceClipName = clipName;
        sec.ClipOffset = 0;
        // O rate vai CONGELADO na section: e o que fecha o mapeamento inverso
        // frame->segundo la no Player, que nao conhece AnimationClip.
        sec.ClipRateScale = rate;
        tr.Sections.push_back(sec);

        const int ti = m_Asset.AddTrack(bindingIndex, tr);

        // A sequence cresce para caber o clipe. O contrario — clipe cortado pela
        // borda da timeline, sem aviso — e o tipo de coisa que o usuario atribui
        // ao import, nao ao range.
        SequencerFrameRange range = m_Asset.GetFrameRange();
        if (sec.EndFrame > range.End) {
            range.End = sec.EndFrame;
            m_Asset.SetFrameRange(range);
            m_Player.SetFrameRange(range.Start, range.End);
        }

        EnsurePlayerStarted();

        // Explode ja na entrada, se o modo estiver ligado. Feito DEPOIS do
        // AddTrack e do range porque a explosao le a section como ela ficou.
        if (m_ExplodeClipOnAdd)
            ExplodeClipTrack(bindingIndex, ti);

        return ti;
    }

    // ============================================================
    // Explodir clipe -> tracks de osso
    // ============================================================
    //
    // O QUE ISTO RESOLVE
    //
    //   A track de clipe e uma camada OPACA: ela produz a pose inteira e nao
    //   tem canais, entao a timeline nao tinha keyframe nenhum para mostrar e
    //   nao havia o que arrastar. Para "camada base + override por cima" isso e
    //   o desenho certo; para ALTERAR a animacao, nao — o animador precisa das
    //   curvas do proprio clipe na tela.
    //
    //   Explodir e o caminho de sempre num DCC (o "bake to keys" do Maya, o
    //   Bake To Control Rig da Unreal): a performance vira keys reais, e a
    //   partir dai tudo que ja existia no Sequencer — drag, snap, interpolacao,
    //   painel de key, Capture — passa a funcionar sobre ela sem uma linha nova.
    //
    // POR QUE AS CURVAS SAO EULER, E NAO QUATERNION
    //
    //   O data model do Sequencer e escalar por canal (RotX/RotY/RotZ em graus),
    //   e e assim de proposito: e o que o painel de key mostra e o que o
    //   animador digita. A conversao quat->euler NAO e inocente e esta tratada
    //   em ContinuousEuler — sem aquele desembrulho, o osso da giros de 180 ou
    //   360 graus entre frames vizinhos.
    //
    // POR QUE NAO SAI UMA TRACK PARA CADA OSSO
    //
    //   O assimp entrega canais de T/R/S para praticamente todo osso, quase
    //   todos constantes e iguais a bind pose. Emitir esses canais criaria
    //   centenas de curvas retas que so atrapalham — e, pior, transformariam a
    //   bind pose numa key, congelando o osso caso o esqueleto mude depois.

    int SequencerWindow::ExplodeClipTrack(int bindingIndex, int trackIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return 0;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return 0;

        // COPIA, nao referencia: cada AddTrack abaixo pode realocar b->Tracks e
        // invalidar qualquer ponteiro para dentro dele. Este foi o unico jeito
        // de nao repetir o classico "iterador invalidado no meio do laco".
        const SequencerTrack clipTrack = b->Tracks[trackIndex];
        if (clipTrack.Type != SequencerTrackType::AnimationClip) return 0;
        if (clipTrack.Sections.empty()) return 0;

        const SequencerSection sec = clipTrack.Sections[0];

        const std::string clipName = sec.SourceClipName.empty()
            ? clipTrack.TargetName : sec.SourceClipName;

        // Os dois motivos reais de falha ganham log. Sem eles, "explodir" que
        // nao faz nada e indistinguivel de "explodir" que nao foi chamado — e o
        // usuario nao tem como saber qual dos dois aconteceu.
        const AnimationClip* clip = FindBindingClip(bindingIndex, clipName);
        if (!clip) {
            AXE_EDITOR_ERROR("Sequencer: clipe '{}' nao existe nesta entidade - "
                "nada a explodir.", clipName);
            return 0;
        }

        const Skeleton* skel = GetBindingSkeleton(bindingIndex);
        if (!skel) {
            AXE_EDITOR_ERROR("Sequencer: binding sem esqueleto resolvido - "
                "nao da para explodir o clipe '{}'.", clipName);
            return 0;
        }

        Pose bindPose;
        Pose::FromBindPose(*skel, bindPose);

        const float fps = static_cast<float>((m_Asset.GetFps() > 0) ? m_Asset.GetFps() : 30);
        const float rate = (sec.ClipRateScale > 0.0001f) ? sec.ClipRateScale : 1.0f;

        const float secStart = static_cast<float>(sec.StartFrame);
        const float secEnd = static_cast<float>(sec.EndFrame);

        // tempo do clipe (s) -> frame da timeline. Inversa exata do que o
        // SequencerPlayer::Resample faz no sentido contrario.
        auto timeToFrame = [&](float t) {
            return secStart + (t / rate) * fps - static_cast<float>(sec.ClipOffset);
            };

        const auto& bones = skel->GetBones();
        int created = 0;

        for (const auto& bc : clip->GetChannels()) {
            if (bc.BoneIndex < 0 || bc.BoneIndex >= static_cast<int>(bones.size())) continue;
            if (bc.IsEmpty()) continue;

            const BoneTransform& rest = bindPose[bc.BoneIndex];

            SequencerTrack tr;
            tr.Type = SequencerTrackType::TransformBone;
            tr.TargetName = bones[bc.BoneIndex].Name;
            tr.TargetType = SequencerTargetType::Bone;

            SequencerSection ns;
            ns.StartFrame = sec.StartFrame;
            ns.EndFrame = sec.EndFrame;
            // A track de osso nao le clipe nenhum: as keys SAO os dados agora.
            ns.SourceClipName.clear();
            ns.ClipOffset = 0;

            // ── TRANSLACAO ───────────────────────────────────────────────────
            if (!bc.PositionKeys.empty()) {
                const glm::vec3 first = bc.PositionKeys.front().Value;

                bool varies = false;
                for (const auto& k : bc.PositionKeys) {
                    if (glm::length(k.Value - first) > 1e-4f) { varies = true; break; }
                }
                // Constante E igual ao repouso: a bind pose ja da esse valor.
                const bool matchesRest = !varies && glm::length(first - rest.Translation) <= 1e-4f;

                if (!matchesRest) {
                    const SequencerChannelComponent comps[3] = {
                        SequencerChannelComponent::X,
                        SequencerChannelComponent::Y,
                        SequencerChannelComponent::Z
                    };
                    for (int a = 0; a < 3; ++a) {
                        SequencerChannel ch;
                        ch.Component = comps[a];
                        for (const auto& k : bc.PositionKeys) {
                            const float f = timeToFrame(k.Time);
                            if (f < secStart - 0.5f || f > secEnd + 0.5f) continue;
                            SequencerKey sk;
                            sk.Frame = std::clamp(f, secStart, secEnd);
                            sk.Value = k.Value[a];
                            sk.Interp = SequencerInterp::Linear;
                            ch.Keys.push_back(sk);
                        }
                        if (!ch.Keys.empty()) { ch.SortKeys(); ns.Channels.push_back(ch); }
                    }
                }
            }

            // ── ROTACAO ──────────────────────────────────────────────────────
            //
            // Os tres eixos entram JUNTOS ou nenhum entra: RotX/RotY/RotZ sao
            // uma decomposicao unica de um quaternion, e keyar so um deles
            // deixaria os outros dois na pose de repouso — o osso torceria.
            if (!bc.RotationKeys.empty()) {
                const glm::quat q0 = bc.RotationKeys.front().Value;

                bool varies = false;
                for (const auto& k : bc.RotationKeys) {
                    if (std::abs(glm::dot(k.Value, q0)) < 0.99999f) { varies = true; break; }
                }
                const bool matchesRest =
                    !varies && std::abs(glm::dot(q0, rest.Rotation)) > 0.99999f;

                if (!matchesRest) {
                    SequencerChannel chx, chy, chz;
                    chx.Component = SequencerChannelComponent::RotX;
                    chy.Component = SequencerChannelComponent::RotY;
                    chz.Component = SequencerChannelComponent::RotZ;

                    glm::vec3 prev(0.0f);
                    bool hasPrev = false;

                    for (const auto& k : bc.RotationKeys) {
                        const float f = timeToFrame(k.Time);
                        if (f < secStart - 0.5f || f > secEnd + 0.5f) continue;

                        const glm::vec3 e = ContinuousEuler(k.Value, prev, hasPrev);
                        prev = e;
                        hasPrev = true;

                        const float frame = std::clamp(f, secStart, secEnd);

                        SequencerKey sk;
                        sk.Frame = frame;
                        sk.Interp = SequencerInterp::Linear;

                        sk.Value = e.x; chx.Keys.push_back(sk);
                        sk.Value = e.y; chy.Keys.push_back(sk);
                        sk.Value = e.z; chz.Keys.push_back(sk);
                    }

                    if (!chx.Keys.empty()) {
                        chx.SortKeys(); chy.SortKeys(); chz.SortKeys();
                        ns.Channels.push_back(chx);
                        ns.Channels.push_back(chy);
                        ns.Channels.push_back(chz);
                    }
                }
            }

            // ── ESCALA ───────────────────────────────────────────────────────
            if (!bc.ScaleKeys.empty()) {
                const glm::vec3 first = bc.ScaleKeys.front().Value;

                bool varies = false;
                for (const auto& k : bc.ScaleKeys) {
                    if (glm::length(k.Value - first) > 1e-4f) { varies = true; break; }
                }
                const bool matchesRest = !varies && glm::length(first - rest.Scale) <= 1e-4f;

                if (!matchesRest) {
                    const SequencerChannelComponent comps[3] = {
                        SequencerChannelComponent::ScaleX,
                        SequencerChannelComponent::ScaleY,
                        SequencerChannelComponent::ScaleZ
                    };
                    for (int a = 0; a < 3; ++a) {
                        SequencerChannel ch;
                        ch.Component = comps[a];
                        for (const auto& k : bc.ScaleKeys) {
                            const float f = timeToFrame(k.Time);
                            if (f < secStart - 0.5f || f > secEnd + 0.5f) continue;
                            SequencerKey sk;
                            sk.Frame = std::clamp(f, secStart, secEnd);
                            sk.Value = k.Value[a];
                            sk.Interp = SequencerInterp::Linear;
                            ch.Keys.push_back(sk);
                        }
                        if (!ch.Keys.empty()) { ch.SortKeys(); ns.Channels.push_back(ch); }
                    }
                }
            }

            // Osso sem nenhuma curva util: nao vira track. Uma track vazia na
            // lista e ruido — e sao dezenas delas num clipe tipico.
            if (ns.Channels.empty()) continue;

            tr.Sections.push_back(ns);
            m_Asset.AddTrack(bindingIndex, tr);
            ++created;
        }

        if (created > 0) {
            // A track de clipe fica MUTADA, nao removida.
            //
            // Mutada, ela sai da avaliacao (o Resample pula tracks mutadas) e a
            // base volta a ser a bind pose — que e exatamente o que as curvas
            // recem-criadas esperam por baixo. Somar as duas camadas daria a
            // mesma pose, mas com o clipe mandando em todo osso que NAO virou
            // track, e ai apagar uma curva nao teria efeito visivel nenhum.
            //
            // Manter a track na lista custa nada e da o desfazer: e so
            // desmutar o clipe e apagar as tracks geradas.
            if (auto* bb = m_Asset.GetBinding(bindingIndex)) {
                if (trackIndex < static_cast<int>(bb->Tracks.size()))
                    bb->Tracks[trackIndex].Muted = true;
            }
            EnsurePlayerStarted();

            AXE_EDITOR_INFO("Sequencer: clipe '{}' explodido em {} track(s) de osso "
                "(de {} canais no clipe). A track de clipe ficou mutada.",
                clipName, created, (int)clip->GetChannelCount());
        }
        else {
            AXE_EDITOR_ERROR("Sequencer: clipe '{}' tem {} canais, mas nenhum difere "
                "da bind pose - nenhuma track criada.",
                clipName, (int)clip->GetChannelCount());
        }

        return created;
    }

    // ============================================================
    // Control Rig
    // ============================================================

    SequencerWindow::RigRuntime* SequencerWindow::EnsureRig(int bindingIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return nullptr;

        if (b->RigAssetUUID.empty()) {
            m_Rigs.erase(bindingIndex);
            return nullptr;
        }

        RigRuntime& rt = m_Rigs[bindingIndex];

        // Recarrega quando o UUID mudou (troca de rig, ou os indices de binding
        // deslizaram e esta entrada passou a pertencer a outro personagem).
        if (rt.SourceUUID != b->RigAssetUUID) {
            rt = RigRuntime{};
            rt.SourceUUID = b->RigAssetUUID;

            const AssetRecord* rec = AssetDatabase::Get().GetByUUID(b->RigAssetUUID);
            if (!rec) {
                AXE_EDITOR_ERROR("Sequencer: o .axerig deste binding nao esta no "
                    "AssetDatabase. As tracks de controle ficam inertes.");
                return nullptr;
            }

            rt.Asset = ControlRigAsset::LoadFromFile(rec->FilePath);
            if (!rt.Asset) {
                AXE_EDITOR_ERROR("Sequencer: falha ao ler o Control Rig '{}'.", rec->Name);
                return nullptr;
            }
        }

        if (!rt.Asset) return nullptr;

        // ── COPIA PROFUNDA DO MOLDE ──────────────────────────────────────────
        //
        // Exatamente o que AnimNode_ControlRig::EnsureWorkingCopy faz, e pela
        // mesma razao: o `.axerig` e compartilhado com o jogo (o mesmo arquivo
        // devolve o mesmo objeto), entao escrever `Value` no asset faria a pose
        // do animador virar a pose padrao de TODOS os personagens que usam
        // aquele rig — ao vivo, no viewport, sem ninguem pedir.
        //
        // O proprio RigElement::Value ja documentava que faltava "alguem
        // escrever no clone de runtime". E aqui.
        if (!rt.Ready || rt.ClonedVersion != rt.Asset->GetVersion()) {
            rt.Hierarchy = rt.Asset->GetHierarchy();
            rt.Graph = rt.Asset->GetGraph();

            rt.Functions.clear();
            for (const auto& f : rt.Asset->GetFunctions())
                rt.Functions.emplace_back(f.Name, f.Graph);

            rt.ClonedVersion = rt.Asset->GetVersion();
            rt.Ready = true;
        }

        return &rt;
    }

    void SequencerWindow::ClearRigs() {
        m_Rigs.clear();
        m_RigAudit.clear();
    }

    // ── AUDITORIA ESTATICA DO GRAFO ──────────────────────────────────────────
    //
    // Le o grafo, nao o executa. Aponta o NO, e nao so o osso.
    void SequencerWindow::AuditRigGraph(int bindingIndex) {
        m_RigAudit.clear();

        RigRuntime* rt = EnsureRig(bindingIndex);
        if (!rt || !rt->Ready) return;

        RigHierarchy& h = rt->Hierarchy;

        // Segue o fio de dados que chega no pino `toPin` do no `node` e, se do
        // outro lado houver um Item Array, devolve os itens dele.
        //
        // Devolve nullptr quando nao ha fio (a lista foi deixada vazia) ou
        // quando a origem e outra coisa — um Get Items, um For Each. Nesses
        // casos a auditoria se cala em vez de chutar.
        auto arrayFeeding = [](const RigGraph& g, int nodeId, int toPin)
            -> const std::vector<RigItemRef>*
            {
                for (const auto& link : g.GetDataLinks()) {
                    if (link.ToNode != nodeId || link.ToPin != toPin) continue;

                    for (const auto& n : g.GetNodes()) {
                        if (!n || n->Id != link.FromNode) continue;
                        if (std::string(n->TypeName()) != "ItemArray") return nullptr;
                        return &static_cast<const RigNode_ItemArray*>(n.get())->Items;
                    }
                }
                return nullptr;
            };

        // Nome do item que chega num pino: o fio vence, o default do pino serve
        // de fallback (e como o usuario digita o osso direto no no).
        auto itemAt = [](const RigGraph& g, const RigNode& n, int pin) -> std::string
            {
                for (const auto& link : g.GetDataLinks())
                    if (link.ToNode == n.Id && link.ToPin == pin)
                        return {};   // vem de outro no — nao da para saber estatico

                if (pin < 0 || pin >= static_cast<int>(n.Inputs.size())) return {};
                return n.Inputs[pin].Default.ItemName;
            };

        // Ha FIO chegando neste pino? Um pino de entrada solto usa o default —
        // e num Make Transform o default de Rotation e (0,0,0), ou seja,
        // IDENTIDADE. Ver a regra do Make Transform mais abaixo.
        auto pinIsWired = [](const RigGraph& g, int nodeId, int pin) -> bool
            {
                for (const auto& link : g.GetDataLinks())
                    if (link.ToNode == nodeId && link.ToPin == pin) return true;
                return false;
            };

        // Dois pais sao o mesmo referencial? Comparar NOME erra no caso mais
        // comum que existe — ver RigHierarchy::SameInitialFrame.
        auto sameFrame = [&](int a, int b) { return h.SameInitialFrame(a, b); };

        auto auditGraph = [&](const RigGraph& g, const std::string& where)
            {
                for (const auto& np : g.GetNodes()) {
                    if (!np) continue;
                    const RigNode& n = *np;
                    const std::string type = n.TypeName();

                    // ── NO DE EXECUCAO SEM FIO DE ENTRADA NUNCA RODA ─────────
                    //
                    // Um no puro (Get Transform, Vector Add) solto no grafo e
                    // inofensivo: ninguem puxa o valor dele. Um no de EXECUCAO
                    // solto e outra coisa — ele parece estar no grafo, o painel
                    // mostra os parametros, e ele simplesmente nao acontece.
                    //
                    // E o estado em que fica o no que foi SUBSTITUIDO e nao
                    // apagado. Meses depois, "mas o Pelvis Dip esta ali" e uma
                    // frase verdadeira sobre um no morto.
                    //
                    // Informativo, nao erro: desligar um no pra testar e uma
                    // coisa legitima de se fazer no meio da autoria.
                    if (n.HasExecIn) {
                        bool reached = false;
                        for (const auto& e : g.GetExecLinks())
                            if (e.ToNode == n.Id) { reached = true; break; }

                        if (!reached) {
                            m_RigAudit.push_back({ where, n.Title,
                                "nao tem fio de execucao chegando: este no NUNCA roda", false });

                            // E para por aqui. Cobrar espaco, par de itens ou
                            // pino solto de um no morto e ruido em cima de
                            // ruido: nada disso pode causar bug, porque nada
                            // disso acontece. Antes desta linha, um Set
                            // Transform desligado rendia tres avisos vermelhos
                            // sobre um no que nao roda.
                            continue;
                        }
                    }

                    // ── FK CHAIN: as duas listas tem de casar POR INDICE ──────
                    if (type == "FKChain") {
                        const auto* bones = arrayFeeding(g, n.Id, 0);
                        const auto* ctrls = arrayFeeding(g, n.Id, 1);

                        if (!bones || !ctrls) continue;

                        if (bones->size() != ctrls->size()) {
                            m_RigAudit.push_back({ where, n.Title,
                                "listas de tamanhos diferentes (" +
                                std::to_string(bones->size()) + " ossos, " +
                                std::to_string(ctrls->size()) + " controles)", true });
                        }

                        const auto* fkN = static_cast<const RigNode_FKChain*>(&n);
                        const bool fkLocal = (fkN->Space == RigSpace::Local);

                        const std::size_t cnt = std::min(bones->size(), ctrls->size());
                        int frameMismatch = 0;

                        for (std::size_t i = 0; i < cnt; ++i) {
                            const std::string& boneName = (*bones)[i].Name;
                            const std::string& ctrlName = (*ctrls)[i].Name;

                            const int ci = h.Find(ctrlName, RigElementType::Control);
                            if (ci < 0) continue;

                            const std::string& src = h[ci].SourceBone;

                            if (!src.empty() && src != boneName) {
                                m_RigAudit.push_back({ where, n.Title,
                                    "par " + std::to_string(i) + ": osso '" + boneName +
                                    "' recebe o transform de '" + ctrlName +
                                    "', que representa '" + src + "'", true });
                                continue;
                            }

                            // ── EM LOCAL, OS PAIS TEM DE SER EQUIVALENTES ────
                            //
                            // Transform local e medido no referencial do pai.
                            // Copiar de um elemento para outro so quer dizer a
                            // mesma coisa se os dois pais forem o mesmo lugar.
                            //
                            // Isso vale no miolo da cadeia e falha na RAIZ dela,
                            // que e onde o controle pende de um `ctrl_RootNode`
                            // criado na origem e o osso pende do `RootNode` do
                            // FBX. O par esta certo, os nomes estao certos, e o
                            // osso sai girado pela diferenca entre os dois.
                            if (!fkLocal) continue;

                            const int bi2 = h.Find(boneName, RigElementType::Bone);
                            if (bi2 < 0) continue;

                            const int cp = h[ci].Parent;
                            const int bp = h[bi2].Parent;
                            if (cp < 0 || bp < 0) continue;

                            // Pais equivalentes: o mesmo elemento, o controle
                            // pai representa o osso pai, ou — o que decide de
                            // fato — os dois pais ocupam o mesmo lugar.
                            if (cp == bp) continue;
                            if (h[cp].SourceBone == h[bp].Name) continue;
                            if (sameFrame(cp, bp)) continue;

                            ++frameMismatch;

                            m_RigAudit.push_back({ where, n.Title,
                                "par " + std::to_string(i) + " em LOCAL: '" + ctrlName +
                                "' pende de '" + h[cp].Name + "' e '" + boneName +
                                "' pende de '" + h[bp].Name +
                                "' - referenciais diferentes", true });
                        }

                        if (frameMismatch > 0) {
                            m_RigAudit.push_back({ where, n.Title,
                                "ponha o Space deste FK Chain em GLOBAL - em Local os "
                                "pares acima copiam entre referenciais diferentes", true });
                        }
                    }

                    // ── SET TRANSFORM: o par Get/Set tem de estar no MESMO
                    //    espaco, e o que se le tem de casar com onde se escreve.
                    //
                    // O `Space` de cada no e um MEMBRO, nao um pino — entao isto
                    // se le estatico, sem executar nada.
                    //
                    // Ler o LOCAL de um controle e escrever como se fosse GLOBAL
                    // (ou o contrario) produz uma rotacao constante que nao vem
                    // de lugar nenhum. E, mesmo com os dois em Local, copiar
                    // local entre elementos de PAIS DIFERENTES compara mocas com
                    // laranjas: o numero e o mesmo, o referencial nao.
                    if (type == "SetTransform") {
                        const auto* setN = static_cast<const RigNode_SetTransform*>(&n);
                        const std::string dst = itemAt(g, n, 0);

                        auto spaceName = [](RigSpace s) {
                            return s == RigSpace::Local ? "Local" : "Global";
                            };

                        // De onde vem o transform escrito (pino 1)?
                        const RigNode* srcNode = nullptr;
                        for (const auto& link : g.GetDataLinks()) {
                            if (link.ToNode != n.Id || link.ToPin != 1) continue;
                            for (const auto& c : g.GetNodes())
                                if (c && c->Id == link.FromNode) srcNode = c.get();
                        }

                        // ── MAKE TRANSFORM COM PINO SOLTO APAGA O QUE FALTA ──
                        //
                        // O Make Transform CONSTROI um transform do zero. Um
                        // pino de entrada que ficou solto nao quer dizer "nao
                        // mexa nisso" — quer dizer "escreva o default". Em
                        // Rotation o default e (0,0,0): IDENTIDADE.
                        //
                        // O caso classico e o dip do quadril montado a mao —
                        // Get Transform -> soma um vetor -> Make Transform ->
                        // Set Transform. A translacao esta certa e a rotacao do
                        // quadril e ZERADA todo frame. Com a animacao de pe e
                        // de frente ninguem nota, porque o quadril ja esta perto
                        // da identidade; num clipe com o torso girado, o
                        // personagem inteiro vira de lado e a culpa parece ser
                        // do Sequencer.
                        //
                        // Trocar o Space do Set nao conserta: em Global com
                        // Propagate desligado os filhos sao recompensados, a
                        // rotacao "some" e o deslocamento tambem — o quadril
                        // descola do corpo em vez de baixar. Some um sintoma e
                        // nasce outro, e a causa segue no pino solto.
                        //
                        // Para "empurre isto um pouco pra la" existe o Offset
                        // Location, que so soma na translacao.
                        if (srcNode && std::string(srcNode->TypeName()) == "MakeTransform") {
                            const bool rotWired = pinIsWired(g, srcNode->Id, 1);
                            const bool sclWired = pinIsWired(g, srcNode->Id, 2);

                            if (!rotWired) {
                                m_RigAudit.push_back({ where, n.Title,
                                    "recebe um Make Transform com o pino Rotation SOLTO: "
                                    "escreve rotacao IDENTIDADE em '" + dst +
                                    "' todo frame, apagando a rotacao da animacao. "
                                    "Se a intencao era so deslocar, troque este par por "
                                    "um Offset Location", true });
                            }

                            if (!sclWired) {
                                m_RigAudit.push_back({ where, n.Title,
                                    "recebe um Make Transform com o pino Scale solto: "
                                    "escreve escala 1 em '" + dst + "'", false });
                            }
                        }

                        if (srcNode && std::string(srcNode->TypeName()) == "GetTransform") {
                            const auto* getN = static_cast<const RigNode_GetTransform*>(srcNode);
                            const std::string src = itemAt(g, *srcNode, 0);

                            // ── SEMPRE MOSTRA O PAR, MESMO SEM PROBLEMA ──────
                            //
                            // Um Set Transform que aparece no rastreio mexendo
                            // num osso e a pergunta "escrevendo O QUE?". Sem
                            // esta linha, so restaria abrir o grafo e conferir
                            // dois checkboxes a olho.
                            //
                            // `Initial` importa tanto quanto o espaco: um Get
                            // com Initial ligado le o REPOUSO, entao o Set
                            // devolve o osso a bind pose e apaga a animacao
                            // daquele osso — e o desvio so aparece quando ha
                            // animacao, o que faz parecer bug do Sequencer.
                            m_RigAudit.push_back({ where, n.Title,
                                std::string("le '") + src + "' (" +
                                spaceName(getN->Space) +
                                (getN->Initial ? ", INITIAL" : "") +
                                ") -> escreve '" + dst + "' (" +
                                spaceName(setN->Space) + ")", false });

                            if (getN->Initial) {
                                m_RigAudit.push_back({ where, n.Title,
                                    "o Get esta em INITIAL: escreve o REPOUSO em '" + dst +
                                    "', apagando a animacao desse osso", true });
                            }

                            if (getN->Space != setN->Space) {
                                m_RigAudit.push_back({ where, n.Title,
                                    std::string("le '") + src + "' em " + spaceName(getN->Space) +
                                    " e escreve '" + dst + "' em " + spaceName(setN->Space) +
                                    " - espacos diferentes", true });
                            }
                            else if (setN->Space == RigSpace::Local &&
                                !src.empty() && !dst.empty()) {

                                const int si = h.FindFlexible(src);
                                const int di = h.FindFlexible(dst);

                                if (si >= 0 && di >= 0) {
                                    const int sp = h[si].Parent;
                                    const int dp = h[di].Parent;

                                    // Local so significa a mesma coisa dos dois
                                    // lados se os pais forem o mesmo elemento, ou
                                    // um o par do outro.
                                    const bool same = (sp == dp) ||
                                        (sp >= 0 && dp >= 0 && h[sp].SourceBone == h[dp].Name) ||
                                        sameFrame(sp, dp);

                                    if (!same) {
                                        // A recomendacao vai junto porque ela e
                                        // unica: GLOBAL e o unico espaco em que
                                        // copiar entre elementos de pais
                                        // diferentes quer dizer o que se espera.
                                        // Reparentar tambem resolve, mas amarra
                                        // a arvore de controles a de ossos.
                                        m_RigAudit.push_back({ where, n.Title,
                                            std::string("copia LOCAL de '") + src + "' (pai '" +
                                            (sp >= 0 ? h[sp].Name : std::string("raiz")) +
                                            "') para '" + dst + "' (pai '" +
                                            (dp >= 0 ? h[dp].Name : std::string("raiz")) +
                                            "') - referenciais diferentes. Ponha os dois em GLOBAL.",
                                            true });
                                    }
                                }
                            }
                        }
                    }

                    // ── TWO BONE IK: Root -> Middle -> Effector contiguos ─────
                    if (type == "TwoBoneIK") {
                        const std::string root = itemAt(g, n, 0);
                        const std::string mid = itemAt(g, n, 1);
                        const std::string eff = itemAt(g, n, 2);

                        if (root.empty() || mid.empty() || eff.empty()) continue;

                        const int ri = h.Find(root, RigElementType::Bone);
                        const int mi = h.Find(mid, RigElementType::Bone);
                        const int ei = h.Find(eff, RigElementType::Bone);

                        if (ri < 0 || mi < 0 || ei < 0) {
                            m_RigAudit.push_back({ where, n.Title,
                                "algum dos tres ossos nao existe no rig", true });
                            continue;
                        }

                        // O solve gira o ROOT. Se ele nao for o pai direto do
                        // Middle, quem gira e um osso acima do pretendido — o
                        // quadril em vez da coxa, e o personagem inteiro torce.
                        if (h[mi].Parent != ri) {
                            m_RigAudit.push_back({ where, n.Title,
                                "'" + mid + "' nao e filho direto de '" + root +
                                "' - o solve vai girar '" + root + "'", true });
                        }

                        if (h[ei].Parent != mi) {
                            m_RigAudit.push_back({ where, n.Title,
                                "'" + eff + "' nao e filho direto de '" + mid + "'", true });
                        }
                    }
                }
            };

        auditGraph(rt->Graph, "ForwardsSolve");

        for (const auto& f : rt->Functions)
            auditGraph(f.second, f.first);
    }

    void SequencerWindow::SolveRig(int bindingIndex, const Skeleton& skel, Pose& pose) {
        RigRuntime* rt = EnsureRig(bindingIndex);
        if (!rt || !rt->Ready) return;

        const auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return;

        // ── 1. A POSE DO ANIMADOR, ANTES DE TUDO ─────────────────────────────
        //
        //    Escrever `Value` tem de vir ANTES do ResetToInitial, e nao depois:
        //    e o proprio ResetToInitial que resolve `Current = Initial * Value`.
        //
        //    Na ordem invertida (como estava), o valor escrito num frame so
        //    aparecia no ResetToInitial do frame SEGUINTE — um quadro de atraso
        //    permanente entre a key e a pose. Invisivel parado, visivel como
        //    arrasto durante o scrub.
        // ── O VALUE VOLTA AO NEUTRO ANTES DE SER REESCRITO ───────────────────
        //
        // `RigElement::Value` e o unico estado do solve que NAO e recomposto do
        // zero a cada frame: o ResetToInitial resolve `Current = Initial *
        // Value` e deixa o Value como esta, de proposito (senao um interruptor
        // voltaria ao padrao a cada quadro).
        //
        // Enquanto todo arrasto de gizmo virava key, isso nao aparecia — o
        // proximo frame reescrevia o mesmo numero a partir da key e o resultado
        // era identico. Com o REC desligado deixa de ser: uma pose descartada
        // (Esc, ou troca de frame) ficaria pendurada no controle para sempre,
        // sem key nenhuma explicando de onde veio, e o `.axerig` recebe Value no
        // save — a pose vazaria para o asset.
        //
        // So os controles que TEM track neste binding sao zerados. Controle sem
        // track nao e dirigido por esta janela, e mexer nele aqui apagaria o que
        // o proprio grafo tiver posto la.
        //
        // Zerar + escrever e uma coisa so, e mora em sequence_apply: o Play
        // executa exatamente a mesma sequencia de passos.
        sequence::ApplyControlSamples(rt->Hierarchy, *b, m_EffectiveSamples,
            bindingIndex);

        // ── 2. REPOUSO + VALUE, E ENTAO A ANIMACAO NOS OSSOS ─────────────────
        //
        //    ResetToInitial resolve `Current = Initial * Value` para todo mundo
        //    e religa Visible (sem ele, um Hide Controls deixaria controles
        //    sumidos para sempre). ApplyPose troca so os OSSOS pela animacao —
        //    nao toca em controle.
        rt->Hierarchy.ResetToInitial();
        rt->Hierarchy.ApplyPose(skel, pose);

        // ── 3. OS CONTROLES VAO PARA CIMA DA ANIMACAO ────────────────────────
        //
        //    Este passo e o que faltava, e ele explica dois sintomas que
        //    pareciam sem relacao:
        //
        //      - o personagem "aponta para o lado" com o rig ligado. Nao e o
        //        clipe: e o Two Bone IK mirando nos controles de pe, que estao
        //        parados na posicao de BIND enquanto a animacao ja saiu de la;
        //
        //      - ligar o Use_Control faz o braco saltar para a T-pose. Tambem
        //        nao e bug: o FK Chain sobe o peso e copia o controle para o
        //        osso — e o controle esta no repouso, porque ninguem o levou
        //        ate a animacao.
        //
        //    Nos dois casos o grafo esta certo e a animacao esta certa; o que
        //    esta fora do lugar sao os controles. Depois do snap, peso 1
        //    reproduz a animacao e o Value do animador vira ajuste POR CIMA
        //    dela — que e o unico jeito de "corrigir a pose que ja estava la".
        if (b->RigControlsFollowAnimation)
            rt->Hierarchy.SnapControlsToCurrentBones();

        // Copia da pose ANTES do solve. So quando o diagnostico esta ligado —
        // e uma Pose inteira por binding por frame.
        Pose beforeSolve;
        if (m_RigDiagnostics)
            beforeSolve = pose;

        // 4. Forwards Solve.
        RigExecContext rc;
        rc.Hierarchy = &rt->Hierarchy;
        rc.Skel = &skel;

        if (m_Context && m_Context->ActiveScene) {
            const entt::entity e = ResolveBindingEntity(*b);
            if (e != entt::null)
                rc.WorldTransform = m_Context->ActiveScene->GetWorldTransform(e);
        }

        // Fisica so em Play. Autoria roda em Edit, e um raycast aqui devolveria
        // resultado de um mundo que ainda nao existe.
        rc.AllowWorldQueries = false;
        rc.UseEditorGround = false;
        rc.DeltaTime = m_Baking ? m_BakeDeltaTime : ImGui::GetIO().DeltaTime;
        rc.Graph = &rt->Graph;
        rc.Blackboard = nullptr;   // sem AnimGraph, sem parametros de gameplay

        rc.ResolveFunction = [rt](const std::string& name) -> RigGraph* {
            for (auto& f : rt->Functions)
                if (f.first == name) return &f.second;
            return nullptr;
            };

        // ── ESPIAO: QUAL NO MEXEU EM QUAL OSSO ───────────────────────────────
        //
        // Fotografa o Current de todo elemento antes de cada no e compara
        // depois. Custa uma copia de vetor por no — por isso so existe com o
        // diagnostico ligado.
        std::vector<BoneTransform> snapshot;

        if (m_RigDiagnostics) {
            m_RigTrace.clear();

            snapshot.resize(rt->Hierarchy.Size());
            for (int i = 0; i < static_cast<int>(rt->Hierarchy.Size()); ++i)
                snapshot[i] = rt->Hierarchy[i].Current;

            rc.OnNodeExecuted = [this, rt, &snapshot](const RigNode& node) {
                const int n = static_cast<int>(rt->Hierarchy.Size());
                if (static_cast<int>(snapshot.size()) != n) return;

                for (int i = 0; i < n; ++i) {
                    // So OSSO. Um no mexer em controle e o funcionamento normal
                    // (o Control Follow Bone faz isso o tempo todo); o que
                    // interessa e quem escreve na POSE.
                    if (rt->Hierarchy[i].Type != RigElementType::Bone) continue;

                    const BoneTransform& a = snapshot[i];
                    const BoneTransform& c = rt->Hierarchy[i].Current;

                    const float d = std::min(1.0f, std::abs(glm::dot(a.Rotation, c.Rotation)));
                    const float deg = glm::degrees(2.0f * std::acos(d));
                    const float dist = glm::length(c.Translation - a.Translation);

                    if (deg >= 0.5f || dist >= 0.001f) {
                        RigNodeEffect fx;
                        fx.Node = node.Title.empty() ? node.TypeName() : node.Title;
                        fx.Bone = rt->Hierarchy[i].Name;
                        fx.Degrees = deg;
                        fx.Distance = dist;
                        m_RigTrace.push_back(fx);
                    }

                    // A foto avanca: o proximo no e comparado com o estado que
                    // ESTE deixou, senao todos herdariam a mudanca do primeiro.
                    snapshot[i] = c;
                }
                };
        }

        rt->Graph.Execute(rc, "ForwardsSolve");

        // 4. De volta para a pose.
        //
        //    WritePose so escreve os ossos que existem na hierarquia do rig, e
        //    um rig quase nunca tem todos (dedos, twist bones). Como a base e a
        //    pose que ja estava montada — e nao a bind pose — osso de fora do
        //    rig simplesmente continua com o que o clipe e as tracks deram.
        rt->Hierarchy.WritePose(skel, pose);

        // ── 6. QUEM O RIG MEXEU? ─────────────────────────────────────────────
        //
        // Com os controles seguindo a animacao e nenhum controle posado, o rig
        // deveria ser transparente. Todo osso que aparecer aqui e um osso que o
        // grafo esta mudando por conta propria.
        if (m_RigDiagnostics) {
            m_RigDeviations.clear();
            m_RigPosedControls = 0;

            for (int i = 0; i < static_cast<int>(rt->Hierarchy.Size()); ++i) {
                const RigElement& el = rt->Hierarchy[i];
                if (el.Type != RigElementType::Control) continue;
                if (el.ValueType != RigControlValue::Transform) continue;
                if (rt->Hierarchy.HasValue(i)) ++m_RigPosedControls;
            }

            const auto& bones = skel.GetBones();
            const std::size_t n = std::min(beforeSolve.Size(), pose.Size());

            for (std::size_t i = 0; i < n; ++i) {
                const BoneTransform& a = beforeSolve[static_cast<int>(i)];
                const BoneTransform& c = pose[static_cast<int>(i)];

                // Angulo entre dois quaternions. `abs` no dot porque q e -q sao
                // a MESMA rotacao — sem ele, metade das comparacoes acusaria
                // 180 graus de diferenca onde nao ha nenhuma.
                const float d = std::min(1.0f, std::abs(glm::dot(a.Rotation, c.Rotation)));
                const float deg = glm::degrees(2.0f * std::acos(d));
                const float dist = glm::length(c.Translation - a.Translation);

                if (deg < 0.5f && dist < 0.001f) continue;

                RigDeviation dv;
                dv.Bone = bones[i].Name;
                dv.Degrees = deg;
                dv.Distance = dist;
                m_RigDeviations.push_back(dv);
            }

            std::sort(m_RigDeviations.begin(), m_RigDeviations.end(),
                [](const RigDeviation& x, const RigDeviation& y) {
                    return x.Degrees > y.Degrees;
                });

            if (m_RigDeviations.size() > 14)
                m_RigDeviations.resize(14);

            // O rastreio sai na ORDEM DE EXECUCAO, nao ordenado por angulo: a
            // pergunta aqui e "em que ponto do solve isto aconteceu", e a
            // sequencia e a resposta. Ordenar destruiria a unica informacao que
            // a lista de desvios ja nao da.
            if (m_RigTrace.size() > 24)
                m_RigTrace.resize(24);
        }
    }

    int SequencerWindow::CreateControlTrack(int bindingIndex, const std::string& controlName) {
        if (controlName.empty()) return -1;

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return -1;

        // ── CANAL OU TRANSFORM? ──────────────────────────────────────────────
        //
        // Um controle de CANAL (o interruptor de IK/FK, a abertura da mao) nao
        // tem forma no viewport e nao se anima com gizmo — ele carrega um
        // numero. Vira uma track `Property` com UM canal, em vez de nove.
        //
        // O proprio RigControlValue ja dizia que isto ia acontecer: "aparece na
        // hierarquia (pra ser animado depois pelo sequencer)".
        bool isChannel = false;
        if (RigRuntime* rt = EnsureRig(bindingIndex)) {
            const int idx = rt->Hierarchy.Find(controlName, RigElementType::Control);
            if (idx >= 0)
                isChannel = (rt->Hierarchy[idx].ValueType != RigControlValue::Transform);
        }

        const SequencerTrackType wantType = isChannel
            ? SequencerTrackType::Property
            : SequencerTrackType::TransformControl;

        for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
            if (b->Tracks[i].TargetType == SequencerTargetType::Control &&
                b->Tracks[i].TargetName == controlName) {
                return i;
            }
        }

        SequencerTrack tr;
        tr.Type = wantType;
        tr.TargetName = controlName;
        tr.TargetType = SequencerTargetType::Control;

        SequencerSection sec;
        sec.StartFrame = m_Asset.GetFrameRange().Start;
        sec.EndFrame = m_Asset.GetFrameRange().End;

        // Canal ja nasce com o unico canal que ele tem. Diferente do transform,
        // aqui nao ha gizmo para cria-lo sob demanda, e uma track de valor sem
        // lugar onde por o valor seria uma linha morta.
        if (isChannel) {
            SequencerChannel ch;
            ch.Component = SequencerChannelComponent::X;   // "o valor"
            sec.Channels.push_back(ch);
        }

        tr.Sections.push_back(sec);   // transform: canais vazios, o gizmo cria

        const int ti = m_Asset.AddTrack(bindingIndex, tr);
        EnsurePlayerStarted();

        AXE_EDITOR_INFO("Sequencer: track do controle '{}' criada.", controlName);
        return ti;
    }

    void SequencerWindow::ApplyGizmoToControl(int bindingIndex, int trackIndex,
        const glm::mat4& world) {
        if (!m_Context || !m_Context->ActiveScene) return;

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;

        const std::string controlName = b->Tracks[trackIndex].TargetName;

        RigRuntime* rt = EnsureRig(bindingIndex);
        if (!rt || !rt->Ready) return;

        const int idx = rt->Hierarchy.Find(controlName, RigElementType::Control);
        if (idx < 0) return;

        const entt::entity e = ResolveBindingEntity(*b);
        if (e == entt::null) return;

        const glm::mat4 entityWorld = m_Context->ActiveScene->GetWorldTransform(e);

        // ── A CONTA FICA DENTRO DA DLL ───────────────────────────────────────
        //
        // `SetValueFromGlobal` resolve `Value' = Value * inverse(GetGlobal) *
        // wanted`. Passar pelo global ATUAL — e nao pelo Initial — e o que faz
        // a pose se somar ao que o grafo ja mandou em vez de brigar com ele.
        // Refazer essa conta aqui fora seria manter duas versoes da mesma
        // matematica, e a de fora nem tem acesso ao BoneTransform::ToMatrix.
        // ANTES do SetValueFromGlobal: o instantaneo tem de ser o estado
        // pre-arrasto, e essa chamada ja escreve no Value.
        if (!m_GroupDragActive && m_SelectedTargets.size() > 1) BeginGroupDrag();

        rt->Hierarchy.SetValueFromGlobal(idx, glm::inverse(entityWorld) * world);

        const RigElement& el = rt->Hierarchy[idx];

        // ── SECTION SO QUANDO VAI VIRAR KEY ──────────────────────────────────
        //
        // Com REC desligado nao ha nada a gravar, e o SectionAtPlayhead CRIA
        // section numa track que ainda nao tem nenhuma. Chama-lo aqui faria uma
        // pose descartada deixar uma section vazia para tras — um efeito
        // colateral permanente de um gesto explicitamente temporario.
        const int si = m_Recording ? SectionAtPlayhead(bindingIndex, trackIndex) : -1;
        if (m_Recording && si < 0) return;

        const ImGuizmo::OPERATION op = m_Context->Viewport
            ? m_Context->Viewport->GetGizmoOperation() : ImGuizmo::TRANSLATE;

        TargetLocal now;
        now.T = el.Value.Translation;
        now.R = el.Value.Rotation;
        now.S = el.Value.Scale;

        if (op == ImGuizmo::TRANSLATE) {
            WriteChannelValue(bindingIndex, trackIndex, si,
                SequencerChannelComponent::X, el.Value.Translation.x);
            WriteChannelValue(bindingIndex, trackIndex, si,
                SequencerChannelComponent::Y, el.Value.Translation.y);
            WriteChannelValue(bindingIndex, trackIndex, si,
                SequencerChannelComponent::Z, el.Value.Translation.z);
            ApplyGroupDelta(now, GizmoChannel::Translate);
        }
        else if (op == ImGuizmo::ROTATE) {
            WriteRotation(bindingIndex, trackIndex, si, el.Value.Rotation);
            ApplyGroupDelta(now, GizmoChannel::Rotate);
        }
        else if (op == ImGuizmo::SCALE) {
            WriteChannelValue(bindingIndex, trackIndex, si,
                SequencerChannelComponent::ScaleX, el.Value.Scale.x);
            WriteChannelValue(bindingIndex, trackIndex, si,
                SequencerChannelComponent::ScaleY, el.Value.Scale.y);
            WriteChannelValue(bindingIndex, trackIndex, si,
                SequencerChannelComponent::ScaleZ, el.Value.Scale.z);
            ApplyGroupDelta(now, GizmoChannel::Scale);
        }
    }

    // Picker de CONTROLE do rig ligado ao binding.
    void SequencerWindow::DrawAddCameraCutPopup(int bindingIndex) {
        if (m_OpenAddCameraCutPopup) {
            ImGui::OpenPopup("Cortar para##seqcut");
            m_OpenAddCameraCutPopup = false;
        }

        if (!ImGui::BeginPopup("Cortar para##seqcut")) {
            m_AddCameraCutForBinding = -1;
            return;
        }

        if (!m_Context || !m_Context->ActiveScene) {
            ImGui::TextDisabled("Sem cena.");
            ImGui::EndPopup();
            m_AddCameraCutForBinding = -1;
            return;
        }

        ImGui::TextDisabled("Cameras da cena");
        ImGui::Separator();

        auto& reg = m_Context->ActiveScene->GetRegistry();
        auto view = reg.view<CameraComponent>();

        int shown = 0;

        for (auto e : view) {
            const auto* nc = reg.try_get<NameComponent>(e);
            if (!nc || nc->Name.empty()) continue;

            ++shown;
            // (int)entity e o mesmo cast que o resto do editor usa (ver
            // hierarchy_window / inspector_window) — entt::entity e enum class,
            // entao o cast direto nao depende de qual versao do EnTT esta em uso.
            ImGui::PushID((int)e);

            char label[192];
            std::snprintf(label, sizeof(label), ICON_CAMERA " %s", nc->Name.c_str());

            if (ImGui::Selectable(label)) {
                const int ti = CreateCameraCutTrack(bindingIndex, nc->Name);
                if (ti >= 0) {
                    m_SelectedBinding = bindingIndex;
                    m_SelectedTrack = ti;
                    m_SelectedSection = 0;
                    m_SelectedChannel = m_SelectedKey = -1;
                    SetGroupOpen(bindingIndex, TrackGroup::Cut, true);
                }
                m_AddCameraCutForBinding = -1;
                ImGui::CloseCurrentPopup();
            }

            ImGui::PopID();
        }

        if (shown == 0) {
            // O caminho e Criar > Camera no outliner da cena. Dizer isso aqui
            // poupa a viagem de descobrir que a entidade de camera existe.
            ImGui::TextWrapped("Nenhuma entidade de camera nesta cena.\n"
                "Crie uma pelo menu Criar > Camera.");
        }
        else {
            ImGui::Separator();
            ImGui::TextDisabled(
                "Uma key por corte: dali em diante a cena e vista por\n"
                "esta camera, ate a proxima key de corte.");
        }

        ImGui::EndPopup();
    }

    void SequencerWindow::DrawAddEventPopup(int bindingIndex) {
        if (m_OpenAddEventPopup) {
            ImGui::OpenPopup("Adicionar evento##seqevt");
            m_OpenAddEventPopup = false;
        }

        if (!ImGui::BeginPopup("Adicionar evento##seqevt")) {
            m_AddEventForBinding = -1;
            return;
        }

        ImGui::TextDisabled("Nome do evento");
        ImGui::Separator();

        ImGui::SetNextItemWidth(240.0f);

        // EnterReturnsTrue: digitar o nome e apertar Enter e o gesto inteiro.
        const bool submitted = ImGui::InputText("##evtname", m_NewEventName,
            sizeof(m_NewEventName), ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere(-1);

        ImGui::TextDisabled(
            "Chega no script da entidade deste binding como OnEvent(nome, valor).\n"
            "O valor e o campo Value de cada key.");

        const bool empty = (m_NewEventName[0] == '\0');

        ImGui::BeginDisabled(empty);
        const bool clicked = ui::AccentButton(ICON_BOLT " Criar", ui::Accent::Primary,
            "Cria a track. As keys voce poe na timeline, no frame que quiser.");
        ImGui::EndDisabled();

        if (!empty && (clicked || submitted)) {
            const int ti = CreateEventTrack(bindingIndex, m_NewEventName);
            if (ti >= 0) {
                m_SelectedBinding = bindingIndex;
                m_SelectedTrack = ti;
                m_SelectedSection = 0;
                m_SelectedChannel = m_SelectedKey = -1;
            }
            m_NewEventName[0] = '\0';
            m_AddEventForBinding = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void SequencerWindow::DrawAddControlPopup(int bindingIndex) {
        if (m_OpenAddControlPopup) {
            ImGui::OpenPopup("Adicionar controle##seqctrl");
            m_OpenAddControlPopup = false;
        }

        if (!ImGui::BeginPopup("Adicionar controle##seqctrl")) {
            m_AddControlForBinding = -1;
            return;
        }

        RigRuntime* rt = EnsureRig(bindingIndex);

        if (!rt || !rt->Ready) {
            ImGui::TextWrapped("Nenhum Control Rig ligado a este binding.\n"
                "Escolha um .axerig no slot do binding primeiro.");
            ImGui::EndPopup();
            m_AddControlForBinding = -1;
            return;
        }

        ImGui::TextDisabled("Controles de '%s'", rt->Asset->GetName().c_str());
        ImGui::Separator();

        if (ui::AccentButton(ICON_SITEMAP " Adicionar todos", ui::Accent::Primary,
            "Uma track por controle de transform, na ordem da hierarquia do rig.")) {
            AddAllControlTracks(bindingIndex);
            m_AddControlForBinding = -1;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::Separator();

        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##ctrlfilter", "filtrar...", m_PickerFilter,
            sizeof(m_PickerFilter));

        ImGui::BeginChild("##ctrllist", ImVec2(320.0f, 300.0f), true);

        int shown = 0;

        for (int i = 0; i < static_cast<int>(rt->Hierarchy.Size()); ++i) {
            const RigElement& el = rt->Hierarchy[i];

            if (el.Type != RigElementType::Control) continue;

            if (m_PickerFilter[0] != '\0' &&
                el.Name.find(m_PickerFilter) == std::string::npos) continue;

            ImGui::PushID(i);
            ++shown;

            const bool channel = (el.ValueType != RigControlValue::Transform);

            // Icone diferente porque sao coisas diferentes de usar: um se
            // agarra com o gizmo, o outro se digita.
            char label[256];
            std::snprintf(label, sizeof(label), "%s %s%s",
                channel ? ICON_SLIDERS : ICON_CIRCLE_NODES,
                el.Name.c_str(),
                channel ? (el.ValueType == RigControlValue::Bool
                    ? "   (interruptor)" : "   (valor)") : "");

            if (ImGui::Selectable(label)) {
                const int ti = CreateControlTrack(bindingIndex, el.Name);
                if (ti >= 0) {
                    m_SelectedBinding = bindingIndex;
                    m_SelectedTrack = ti;
                    m_SelectedSection = 0;
                    m_SelectedChannel = m_SelectedKey = -1;
                }
                m_AddControlForBinding = -1;
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::IsItemHovered() && !el.SourceBone.empty())
                ImGui::SetTooltip("nasceu do osso: %s", el.SourceBone.c_str());

            ImGui::PopID();
        }

        if (shown == 0)
            ImGui::TextDisabled("nenhum controle de transform");

        ImGui::EndChild();
        ImGui::EndPopup();
    }

    // ============================================================
    // Gizmo
    // ============================================================

    int SequencerWindow::SectionAtPlayhead(int bindingIndex, int trackIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return -1;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return -1;

        auto& tr = b->Tracks[trackIndex];
        const float f = m_Player.GetCurrentFrame();

        for (int i = 0; i < static_cast<int>(tr.Sections.size()); ++i)
            if (tr.Sections[i].ContainsFrame(f)) return i;

        // Sem section nenhuma: cria uma cobrindo o range. Uma track de osso
        // nasce sem section, e exigir que o usuario descubra o botao "+ section"
        // antes de poder mexer no gizmo transformaria o primeiro contato com a
        // ferramenta numa cacada.
        if (tr.Sections.empty()) {
            SequencerSection sec;
            sec.StartFrame = m_Asset.GetFrameRange().Start;
            sec.EndFrame = m_Asset.GetFrameRange().End;
            return m_Asset.AddSection(bindingIndex, trackIndex, sec);
        }

        // Ha sections, mas o playhead esta fora de todas. Nao inventa nada: o
        // gesto certo e mover o playhead ou esticar a section, e criar uma
        // terceira por baixo do pano deixaria a timeline com buracos que
        // ninguem pediu.
        return -1;
    }

    int SequencerWindow::EnsureChannel(int bindingIndex, int trackIndex, int sectionIndex,
        SequencerChannelComponent component) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return -1;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return -1;

        auto& tr = b->Tracks[trackIndex];
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tr.Sections.size())) return -1;

        auto& sec = tr.Sections[sectionIndex];
        for (int i = 0; i < static_cast<int>(sec.Channels.size()); ++i)
            if (sec.Channels[i].Component == component) return i;

        SequencerChannel ch;
        ch.Component = component;
        return m_Asset.AddChannel(bindingIndex, trackIndex, sectionIndex, ch);
    }

    void SequencerWindow::SetChannelKeyAtPlayhead(int bindingIndex, int trackIndex,
        int sectionIndex, SequencerChannelComponent component, float value) {
        const int ci = EnsureChannel(bindingIndex, trackIndex, sectionIndex, component);
        if (ci < 0) return;

        SequencerKey k;
        k.Frame = m_Player.GetCurrentFrame();
        k.Value = value;
        k.Interp = SequencerInterp::Linear;

        // AddKey substitui a key existente no mesmo frame (tolerancia 0.01), que
        // e exatamente o que um arrasto continuo precisa: um gesto de meio
        // segundo a 60fps produziria trinta keys empilhadas no mesmo frame.
        m_Asset.AddKey(bindingIndex, trackIndex, sectionIndex, ci, k);
    }

    // ── EULER CONTINUO: A REFERENCIA CERTA ───────────────────────────────────
    //
    // Extraido do SetRotationKeysAtPlayhead porque agora HA DOIS caminhos que
    // precisam da mesma resposta (a key, com REC ligado, e a pose pendente, com
    // REC desligado). Duas copias divergiriam, e a divergencia apareceria como
    // um salto de 360 graus no instante em que a pose pendente virasse key —
    // exatamente o bug que o desembrulho existe para evitar.
    //
    // Ordem das referencias:
    //
    //   1. A PENDENCIA, se houver. Enquanto o usuario arrasta com REC
    //      desligado, o angulo anterior e o que ele mesmo acabou de produzir;
    //      medir contra a key antiga faria o valor pular no meio do gesto.
    //   2. A key de rotacao mais proxima ANTES do playhead.
    //   3. Nada — a forma canonica serve, nao ha com o que ser continuo.
    glm::vec3 SequencerWindow::ResolveContinuousEuler(int bindingIndex, int trackIndex,
        int sectionIndex, const glm::quat& rotation) const {
        glm::vec3 prev(0.0f);
        bool hasPrev = false;

        const SequencerChannelComponent rot[3] = {
            SequencerChannelComponent::RotX,
            SequencerChannelComponent::RotY,
            SequencerChannelComponent::RotZ
        };

        const auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size()))
            return ContinuousEuler(rotation, prev, hasPrev);

        const SequencerTrack& tr = b->Tracks[trackIndex];

        // 1. Pendencia.
        for (const auto& p : m_PendingEdits) {
            if (p.BindingIndex != bindingIndex) continue;
            if (p.TargetType != tr.TargetType) continue;
            if (p.TargetName != tr.TargetName) continue;

            for (int axis = 0; axis < 3; ++axis) {
                if (p.Component != rot[axis]) continue;
                prev[axis] = p.Value;
                hasPrev = true;
            }
        }
        if (hasPrev) return ContinuousEuler(rotation, prev, hasPrev);

        // 2. Key anterior.
        if (sectionIndex >= 0 && sectionIndex < static_cast<int>(tr.Sections.size())) {
            const auto& sec = tr.Sections[sectionIndex];
            const float now = m_Player.GetCurrentFrame();

            for (int axis = 0; axis < 3; ++axis) {
                for (const auto& ch : sec.Channels) {
                    if (ch.Component != rot[axis]) continue;

                    float best = 0.0f;
                    float bestFrame = -std::numeric_limits<float>::max();

                    for (const auto& k : ch.Keys) {
                        if (k.Frame <= now && k.Frame > bestFrame) {
                            bestFrame = k.Frame;
                            best = k.Value;
                        }
                    }
                    if (bestFrame > -std::numeric_limits<float>::max()) {
                        prev[axis] = best;
                        hasPrev = true;
                    }
                }
            }
        }

        return ContinuousEuler(rotation, prev, hasPrev);
    }

    void SequencerWindow::SetRotationKeysAtPlayhead(int bindingIndex, int trackIndex,
        int sectionIndex, const glm::quat& rotation) {
        const glm::vec3 euler =
            ResolveContinuousEuler(bindingIndex, trackIndex, sectionIndex, rotation);

        SetChannelKeyAtPlayhead(bindingIndex, trackIndex, sectionIndex,
            SequencerChannelComponent::RotX, euler.x);
        SetChannelKeyAtPlayhead(bindingIndex, trackIndex, sectionIndex,
            SequencerChannelComponent::RotY, euler.y);
        SetChannelKeyAtPlayhead(bindingIndex, trackIndex, sectionIndex,
            SequencerChannelComponent::RotZ, euler.z);
    }

    // ============================================================
    // REC — auto-key, pose pendente e captura
    // ============================================================

    void SequencerWindow::SetPendingEdit(int bindingIndex, const SequencerTrack& track,
        SequencerChannelComponent component, float value) {
        // O frame de autoria e reafirmado a cada escrita. Durante um arrasto o
        // playhead nao anda, entao isto e sempre o mesmo numero; o que ele
        // impede e a pendencia de um frame anterior ser adotada por este.
        m_PendingFrame = m_Player.GetCurrentFrame();

        for (auto& p : m_PendingEdits) {
            if (p.BindingIndex == bindingIndex &&
                p.TargetType == track.TargetType &&
                p.TargetName == track.TargetName &&
                p.Component == component) {
                p.Value = value;
                return;
            }
        }

        SequencerSample s;
        s.BindingIndex = bindingIndex;
        s.TargetName = track.TargetName;
        s.TargetType = track.TargetType;
        s.Component = component;
        s.Value = value;
        m_PendingEdits.push_back(s);
    }

    void SequencerWindow::ClearPendingEdits() {
        m_PendingEdits.clear();
        m_PendingFrame = -1.0f;

        // Os controles de rig precisam de uma palavra a mais.
        //
        // Osso e socket sao recompostos do zero todo frame (Pose::FromBindPose /
        // o transform neutro do preview), entao esquecer a pendencia ja os
        // devolve ao lugar. `RigElement::Value` NAO: o gizmo escreveu ali via
        // SetValueFromGlobal, e nem o ResetToInitial nem o solve limpam esse
        // campo — descartar a pose deixaria o controle onde o usuario largou,
        // sem key nenhuma explicando por que.
        //
        // O SolveRig zera o Value das tracks de controle antes de reescrever a
        // partir dos samples; basta, portanto, nao ter mais o sample — e por
        // isso nao ha nada a desfazer aqui.
    }

    void SequencerWindow::WriteChannelValue(int bindingIndex, int trackIndex,
        int sectionIndex, SequencerChannelComponent component, float value) {
        if (m_Recording) {
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, sectionIndex,
                component, value);
            return;
        }

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size()))
            return;

        SetPendingEdit(bindingIndex, b->Tracks[trackIndex], component, value);
    }

    void SequencerWindow::WriteRotation(int bindingIndex, int trackIndex,
        int sectionIndex, const glm::quat& rotation) {
        if (m_Recording) {
            SetRotationKeysAtPlayhead(bindingIndex, trackIndex, sectionIndex, rotation);
            return;
        }

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size()))
            return;

        const glm::vec3 euler =
            ResolveContinuousEuler(bindingIndex, trackIndex, sectionIndex, rotation);

        const SequencerTrack& tr = b->Tracks[trackIndex];
        SetPendingEdit(bindingIndex, tr, SequencerChannelComponent::RotX, euler.x);
        SetPendingEdit(bindingIndex, tr, SequencerChannelComponent::RotY, euler.y);
        SetPendingEdit(bindingIndex, tr, SequencerChannelComponent::RotZ, euler.z);
    }

    // ── A LISTA QUE A AVALIACAO LE ───────────────────────────────────────────
    //
    // Samples do player + pendencias POR CIMA. A ordem importa: sem a
    // precedencia, posar num frame que ja tem key nao teria efeito visivel, e o
    // sintoma seria "o gizmo nao funciona em cima de uma key" — que e onde o
    // animador mais precisa dele.
    //
    // A substituicao e por (binding, alvo, componente), e nao um append cego:
    // dois samples do mesmo canal fariam o consumidor depender da ordem de
    // iteracao — o de osso pega o ULTIMO, o de socket tambem, mas o de controle
    // recompoe o quaternion a cada eixo e o resultado dependeria de qual veio
    // primeiro.
    void SequencerWindow::RebuildEffectiveSamples() {
        m_EffectiveSamples = m_Player.GetLastSamples();

        if (m_PendingEdits.empty()) return;

        for (const auto& p : m_PendingEdits) {
            bool replaced = false;

            for (auto& s : m_EffectiveSamples) {
                if (s.BindingIndex == p.BindingIndex &&
                    s.TargetType == p.TargetType &&
                    s.TargetName == p.TargetName &&
                    s.Component == p.Component) {
                    s.Value = p.Value;
                    replaced = true;
                    break;
                }
            }

            if (!replaced) m_EffectiveSamples.push_back(p);
        }
    }

    int SequencerWindow::CommitPendingEdits() {
        if (m_PendingEdits.empty()) return 0;

        // O frame de destino e o da AUTORIA, nao o de agora. Na pratica sao o
        // mesmo numero (mudar de frame descarta a pendencia — ver Draw), mas
        // depender disso implicitamente seria contar com um invariante mantido
        // em outro arquivo.
        const float authored = m_PendingFrame;
        const float current = m_Player.GetCurrentFrame();

        if (authored >= 0.0f && std::abs(authored - current) > 0.5f) {
            AXE_EDITOR_WARN("Sequencer: pose pendente e do frame {:.0f}, o playhead "
                "esta em {:.0f}. Descartada.", authored, current);
            ClearPendingEdits();
            return 0;
        }

        // Cópia: SetChannelKeyAtPlayhead pode criar section e canal, e o
        // ClearPendingEdits no fim invalidaria o vetor sob o laco.
        const std::vector<SequencerSample> pending = m_PendingEdits;
        int written = 0;

        for (const auto& p : pending) {
            auto* b = m_Asset.GetBinding(p.BindingIndex);
            if (!b) continue;

            // Do alvo de volta para o indice da track. A pendencia guarda NOME
            // (como o sample), e nao indice, porque uma track removida entre a
            // pose e o Key deslocaria todos os indices seguintes.
            int ti = -1;
            for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
                if (b->Tracks[i].TargetType == p.TargetType &&
                    b->Tracks[i].TargetName == p.TargetName) {
                    ti = i;
                    break;
                }
            }
            if (ti < 0) continue;
            if (b->Tracks[ti].Locked) continue;

            const int si = SectionAtPlayhead(p.BindingIndex, ti);
            if (si < 0) continue;

            // Os valores JA estao no espaco da curva (graus para rotacao,
            // desembrulhados contra a key anterior no momento em que foram
            // produzidos). Nao passam de novo pelo ContinuousEuler: refazer o
            // desembrulho contra si mesmo e como ele acaba somando 360.
            SetChannelKeyAtPlayhead(p.BindingIndex, ti, si, p.Component, p.Value);
            ++written;
        }

        // Ordena os canais tocados — mesma razao do OnFinish do gizmo.
        for (const auto& p : pending) {
            auto* b = m_Asset.GetBinding(p.BindingIndex);
            if (!b) continue;
            for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
                if (b->Tracks[i].TargetType != p.TargetType) continue;
                if (b->Tracks[i].TargetName != p.TargetName) continue;
                for (int si = 0; si < static_cast<int>(b->Tracks[i].Sections.size()); ++si)
                    for (int ci = 0; ci < static_cast<int>(b->Tracks[i].Sections[si].Channels.size()); ++ci)
                        SortChannelAndRemap(p.BindingIndex, i, si, ci);
            }
        }

        ClearPendingEdits();

        if (written > 0)
            AXE_EDITOR_INFO("Sequencer: {} canal(is) gravado(s) no frame {:.0f}.",
                written, current);

        return written;
    }

    // ── CAPTURAR O QUE ESTA NA TELA ──────────────────────────────────────────
    //
    // Quais canais capturar e a unica decisao interessante aqui, e ela tem
    // precedente no proprio arquivo: o ExplodeClipTrack ja nao cria canal que
    // nao varia, e o ApplyGizmoToBone ja key so a operacao ativa. O motivo e o
    // mesmo — canal de escala constante 1 e translacao igual a de repouso
    // enchem a timeline de curvas retas que ninguem pediu, e cada uma delas
    // depois CONGELA o osso se o esqueleto mudar.
    //
    // Entao:
    //   - track que JA tem canais: captura exatamente esses. E o registro do
    //     que o animador decidiu animar nesta track.
    //   - track ainda sem canal nenhum: translacao + rotacao. Escala fica de
    //     fora ate alguem pedir escala com o gizmo.
    int SequencerWindow::CaptureAllTracksAtPlayhead(int bindingIndex) {
        const int bindingCount = static_cast<int>(m_Asset.GetBindingCount());
        if (bindingCount == 0) return 0;

        const int first = (bindingIndex >= 0) ? bindingIndex : 0;
        const int last = (bindingIndex >= 0) ? bindingIndex : bindingCount - 1;
        if (first < 0 || last >= bindingCount) return 0;

        static const SequencerChannelComponent kDefault[6] = {
            SequencerChannelComponent::X,
            SequencerChannelComponent::Y,
            SequencerChannelComponent::Z,
            SequencerChannelComponent::RotX,
            SequencerChannelComponent::RotY,
            SequencerChannelComponent::RotZ
        };

        int written = 0;

        for (int bi = first; bi <= last; ++bi) {
            auto* b = m_Asset.GetBinding(bi);
            if (!b) continue;

            const int trackCount = static_cast<int>(b->Tracks.size());

            for (int ti = 0; ti < trackCount; ++ti) {
                // Re-obtido a cada volta: SectionAtPlayhead e AddChannel mexem
                // nos vetores do asset e invalidam qualquer referencia guardada.
                auto* bb = m_Asset.GetBinding(bi);
                if (!bb || ti >= static_cast<int>(bb->Tracks.size())) break;

                const SequencerTrack& tr = bb->Tracks[ti];

                if (tr.Locked || tr.Muted) continue;
                if (tr.Type == SequencerTrackType::AnimationClip) continue;
                if (tr.Type == SequencerTrackType::Event) continue;

                const int si = SectionAtPlayhead(bi, ti);
                if (si < 0) continue;

                // Snapshot dos componentes ANTES de escrever: gravar a primeira
                // key cria canais, e iterar sobre a lista que esta crescendo
                // capturaria os canais recem-criados de novo.
                std::vector<SequencerChannelComponent> comps;
                {
                    auto* b2 = m_Asset.GetBinding(bi);
                    const auto& sec = b2->Tracks[ti].Sections[si];
                    for (const auto& ch : sec.Channels)
                        comps.push_back(ch.Component);
                }

                if (comps.empty()) {
                    // Property (controle de canal) carrega UM valor, no canal X.
                    // Dar-lhe seis seria inventar eixos que o controle nao tem.
                    if (tr.Type == SequencerTrackType::Property)
                        comps.push_back(SequencerChannelComponent::X);
                    else
                        comps.assign(kDefault, kDefault + 6);
                }

                for (SequencerChannelComponent c : comps) {
                    auto* b3 = m_Asset.GetBinding(bi);
                    if (!b3 || ti >= static_cast<int>(b3->Tracks.size())) break;

                    float v = 0.0f;
                    if (!CaptureChannelValue(bi, b3->Tracks[ti], c, v)) continue;

                    SetChannelKeyAtPlayhead(bi, ti, si, c, v);
                    ++written;
                }
            }
        }

        if (written > 0)
            AXE_EDITOR_INFO("Sequencer: {} canal(is) capturado(s) do viewport no "
                "frame {:.0f}.", written, m_Player.GetCurrentFrame());
        else
            AXE_EDITOR_WARN("Sequencer: nada para capturar - nenhuma track de "
                "transform destravada no playhead.");

        return written;
    }

    bool SequencerWindow::GizmoTargetWorld(int bindingIndex, const SequencerTrack& track,
        glm::mat4& outWorld) const {
        if (!m_Context || !m_Context->ActiveScene) return false;

        const auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return false;

        const entt::entity e = ResolveBindingEntity(*b);
        if (e == entt::null) return false;

        const glm::mat4 entityWorld = m_Context->ActiveScene->GetWorldTransform(e);

        SkeletalMeshComponent* smc = nullptr;
        const Skeleton* skel = GetBindingSkeleton(bindingIndex, &smc);

        // A track de ENTIDADE nao precisa de esqueleto — e o caso da camera.
        // Testado antes do resto para nao esbarrar no guard abaixo.
        if (track.Type == SequencerTrackType::TransformEntity) {
            outWorld = entityWorld;
            return true;
        }

        if (!skel || !smc) return false;

        switch (track.Type) {

        case SequencerTrackType::TransformBone: {
            const int boneIdx = skel->FindBone(track.TargetName);
            if (boneIdx < 0 || boneIdx >= static_cast<int>(smc->BoneGlobals.size()))
                return false;

            // `entityWorld * BoneGlobals[i]` — a MESMA composicao que o
            // AnimationWorld usa para o socket. Se as duas divergirem, o gizmo
            // aparece num lugar e a arma em outro, e o animador passa a
            // desconfiar da ferramenta inteira.
            outWorld = entityWorld * smc->BoneGlobals[boneIdx];
            return true;
        }

        case SequencerTrackType::TransformSocket: {
            const std::string key = std::to_string(bindingIndex) + "|" + track.TargetName;
            auto it = m_SocketPreviews.find(key);

            auto& reg = m_Context->ActiveScene->GetRegistry();

            // Com objeto anexado, o gizmo vai no OBJETO — e onde o olho do
            // usuario ja esta.
            if (it != m_SocketPreviews.end() && it->second.Entity != entt::null &&
                reg.valid(it->second.Entity)) {
                outWorld = m_Context->ActiveScene->GetWorldTransform(it->second.Entity);
                return true;
            }

            // Socket vazio: o gizmo vai no proprio socket, para dar onde pegar
            // antes de escolher o objeto.
            const auto* sock = FindBindingSocket(bindingIndex, track.TargetName);
            if (!sock || !smc->Asset) return false;

            const int boneIdx = skel->FindBone(sock->BoneName);
            if (boneIdx < 0 || boneIdx >= static_cast<int>(smc->BoneGlobals.size()))
                return false;

            outWorld = entityWorld * smc->BoneGlobals[boneIdx]
                * smc->Asset->GetSocketLocalTransform(*sock);
            return true;
        }

        case SequencerTrackType::TransformEntity:
            // A propria entidade: o mundo dela E o alvo, sem composicao
            // nenhuma. Um osso precisa de `entityWorld * BoneGlobals[i]`
            // justamente porque nao e uma entidade.
            outWorld = entityWorld;
            return true;

        case SequencerTrackType::TransformControl: {
            auto it = m_Rigs.find(bindingIndex);
            if (it == m_Rigs.end() || !it->second.Ready) return false;

            const int idx = it->second.Hierarchy.Find(track.TargetName,
                RigElementType::Control);
            if (idx < 0) return false;

            outWorld = entityWorld * it->second.Hierarchy.GetGlobal(idx);
            return true;
        }

        default:
            return false;
        }
    }



    // ============================================================
    // Undo / Redo por snapshot
    // ============================================================

    namespace {

        // Estimativa grosseira do que um snapshot ocupa. Nao precisa ser exata:
        // serve para decidir quando parar de guardar, e errar por 20% so muda o
        // numero de passos que cabem.
        std::size_t SnapshotBytes(const std::vector<SequencerBinding>& bindings) {
            std::size_t n = 0;

            for (const auto& b : bindings) {
                n += sizeof(SequencerBinding) + b.EntityName.size() + b.DisplayName.size();

                for (const auto& tr : b.Tracks) {
                    n += sizeof(SequencerTrack) + tr.TargetName.size();

                    for (const auto& sec : tr.Sections) {
                        n += sizeof(SequencerSection) + sec.SourceClipName.size();

                        for (const auto& ch : sec.Channels)
                            n += sizeof(SequencerChannel) +
                            ch.Keys.size() * sizeof(SequencerKey);
                    }
                }
            }
            return n;
        }

        // Teto de MEMORIA, e nao so de passos.
        //
        // Um clipe explodido produz dezenas de tracks com centenas de keys cada.
        // Guardar 64 snapshots disso passaria facil de 100 MB — e o usuario
        // descobriria isso como "o editor comeu a RAM", sem nenhuma ligacao
        // aparente com o Ctrl+Z.
        constexpr std::size_t kUndoBudgetBytes = 48u * 1024u * 1024u;
        constexpr std::size_t kUndoMaxEntries = 64;

    } // namespace

    SequencerWindow::Snapshot SequencerWindow::TakeSnapshot() const {
        Snapshot s;
        s.Bindings = m_Asset.GetBindings();
        s.Range = m_Asset.GetFrameRange();
        s.Fps = m_Asset.GetFps();
        return s;
    }

    void SequencerWindow::RestoreSnapshot(const Snapshot& s) {
        m_Asset.SetBindings(s.Bindings);
        m_Asset.SetFrameRange(s.Range);
        m_Asset.SetFps(s.Fps);
        m_Player.SetFps(s.Fps);
    }

    void SequencerWindow::TrackUndoState() {
        const std::uint64_t sig = SignatureOf(m_Asset);

        if (!m_HasLastState) {
            m_LastState = TakeSnapshot();
            m_LastSignature = sig;
            m_HasLastState = true;
            return;
        }

        if (sig != m_LastSignature) {
            // Primeira mudanca do gesto: o estado do frame ANTERIOR e para
            // onde o Ctrl+Z vai voltar.
            if (!m_UndoPendingOpen) {
                m_UndoPending = m_LastState;
                m_UndoPendingOpen = true;
            }

            // `m_LastState` NAO e refeito aqui de proposito. Durante um arrasto
            // isso seria uma copia da arvore inteira por frame, e ela nao serve
            // para nada: o ponto de retorno do gesto ja foi capturado acima. A
            // copia acontece uma vez so, quando o gesto fecha.
            m_LastSignature = sig;
            m_UndoIdleFrames = 0;
            return;
        }

        if (!m_UndoPendingOpen)
            return;

        // ── QUANDO O GESTO FECHA ─────────────────────────────────────────────
        //
        // Botao solto E assinatura parada por dois frames. Os dois testes
        // importam: sem o do mouse, uma pausa no meio de um arrasto lento
        // partiria o gesto em dois undos; sem os frames de folga, uma edicao por
        // teclado (que muda num frame e para) so fecharia no proximo evento.
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_UndoIdleFrames = 0;
            return;
        }

        if (++m_UndoIdleFrames < 2)
            return;

        m_UndoStack.push_back(std::move(m_UndoPending));
        m_UndoPendingOpen = false;
        m_UndoIdleFrames = 0;

        // Agora sim: o estado estavel de agora e o ponto de retorno do PROXIMO
        // gesto.
        m_LastState = TakeSnapshot();

        // Uma acao nova invalida o caminho para a frente. E o comportamento de
        // todo editor, e a alternativa (manter o redo) produziria um "refazer"
        // que reconstroi um estado que nunca existiu nesta linha do tempo.
        m_RedoStack.clear();

        while (m_UndoStack.size() > kUndoMaxEntries)
            m_UndoStack.erase(m_UndoStack.begin());

        std::size_t total = 0;
        for (const auto& s : m_UndoStack)
            total += SnapshotBytes(s.Bindings);

        while (total > kUndoBudgetBytes && m_UndoStack.size() > 1) {
            total -= SnapshotBytes(m_UndoStack.front().Bindings);
            m_UndoStack.erase(m_UndoStack.begin());
        }
    }

    void SequencerWindow::AfterHistoryJump() {
        // Indices guardados apontam para uma arvore que acabou de ser
        // substituida. Clampar em vez de zerar: voltar um passo e continuar
        // olhando a mesma track e o que se espera.
        const int bindingCount = static_cast<int>(m_Asset.GetBindingCount());

        if (m_SelectedBinding >= bindingCount) m_SelectedBinding = bindingCount - 1;
        if (m_SelectedBinding < 0) m_SelectedTrack = -1;

        if (m_SelectedTrack >= 0) {
            const auto* b = m_Asset.GetBinding(m_SelectedBinding);
            if (!b || m_SelectedTrack >= static_cast<int>(b->Tracks.size()))
                m_SelectedTrack = -1;
        }

        m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;

        // A selecao de keys e a pose pendente descrevem um estado que nao existe
        // mais. Manter qualquer uma das duas faria o proximo Delete (ou o
        // proximo K) escrever num lugar que o usuario nao esta vendo.
        ClearKeySelection();
        ClearPendingEdits();

        m_ViewportControlBinding = -1;
        m_ViewportControlName.clear();
        ClearTargetSelection();

        // O historico nao pode virar mudanca no historico.
        m_LastState = TakeSnapshot();
        m_LastSignature = SignatureOf(m_Asset);
        m_UndoPendingOpen = false;
        m_UndoIdleFrames = 0;

        if (m_PlayerStarted) {
            m_Player.SyncFrom(m_Asset);
            EvaluateAndApply();
        }
    }

    void SequencerWindow::Undo() {
        // Gesto ainda aberto conta como uma entrada. Sem isto, Ctrl+Z logo
        // depois de soltar o gizmo desfaria o passo ANTERIOR ao arrasto e
        // deixaria o arrasto de pe — o oposto do pedido.
        if (m_UndoPendingOpen) {
            m_UndoStack.push_back(std::move(m_UndoPending));
            m_UndoPendingOpen = false;
        }

        if (m_UndoStack.empty()) {
            AXE_EDITOR_INFO("Sequencer: nada a desfazer.");
            return;
        }

        m_RedoStack.push_back(TakeSnapshot());
        RestoreSnapshot(m_UndoStack.back());
        m_UndoStack.pop_back();

        AfterHistoryJump();
    }

    void SequencerWindow::Redo() {
        if (m_RedoStack.empty()) {
            AXE_EDITOR_INFO("Sequencer: nada a refazer.");
            return;
        }

        m_UndoStack.push_back(TakeSnapshot());
        RestoreSnapshot(m_RedoStack.back());
        m_RedoStack.pop_back();

        AfterHistoryJump();
    }

    // ============================================================
    // Copiar / recortar / colar keys
    // ============================================================

    void SequencerWindow::CopySelectedKeys(bool cut) {
        if (m_SelectedKeys.empty()) {
            AXE_EDITOR_INFO("Sequencer: nenhuma key selecionada.");
            return;
        }

        std::vector<KeyClip> clips;
        clips.reserve(m_SelectedKeys.size());

        float minFrame = std::numeric_limits<float>::max();

        for (const auto& r : m_SelectedKeys) {
            const auto* b = m_Asset.GetBinding(r.Binding);
            if (!b) continue;
            if (r.Track < 0 || r.Track >= static_cast<int>(b->Tracks.size())) continue;

            const auto& tr = b->Tracks[r.Track];
            if (r.Section < 0 || r.Section >= static_cast<int>(tr.Sections.size())) continue;

            const auto& sec = tr.Sections[r.Section];
            if (r.Channel < 0 || r.Channel >= static_cast<int>(sec.Channels.size())) continue;

            const auto& ch = sec.Channels[r.Channel];
            if (r.Key < 0 || r.Key >= static_cast<int>(ch.Keys.size())) continue;

            KeyClip c;
            c.Binding = r.Binding;
            c.TargetType = tr.TargetType;
            c.TargetName = tr.TargetName;
            c.Component = ch.Component;
            c.Key = ch.Keys[r.Key];
            c.FrameOffset = c.Key.Frame;   // vira relativo logo abaixo

            minFrame = std::min(minFrame, c.Key.Frame);
            clips.push_back(std::move(c));
        }

        if (clips.empty()) return;

        // Relativo ao INICIO do recorte, e nao ao playhead do momento da copia:
        // assim colar sempre poe a primeira key sob a agulha, independente de
        // onde ela estava quando a copia foi feita.
        for (auto& c : clips)
            c.FrameOffset -= minFrame;

        m_KeyClipboard = std::move(clips);

        if (cut) DeleteSelectedKeys();

        AXE_EDITOR_INFO("Sequencer: {} key(s) {}.",
            (int)m_KeyClipboard.size(), cut ? "recortada(s)" : "copiada(s)");
    }

    int SequencerWindow::PasteKeys(const std::vector<KeyClip>& clips) {
        if (clips.empty()) return 0;

        float base = m_Player.GetCurrentFrame();
        if (m_SnapEnabled && m_SnapFrame > 0)
            base = std::round(base / m_SnapFrame) * m_SnapFrame;

        const int rangeStart = m_Asset.GetFrameRange().Start;
        const int rangeEnd = m_Asset.GetFrameRange().End;

        std::vector<KeyRef> pasted;
        int written = 0;

        for (const auto& c : clips) {
            auto* b = m_Asset.GetBinding(c.Binding);
            if (!b) continue;

            // Track pelo ALVO, nunca por indice — ver a nota em KeyClip.
            int ti = -1;
            for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
                if (b->Tracks[i].TargetType != c.TargetType) continue;
                if (b->Tracks[i].TargetName != c.TargetName) continue;
                ti = i;
                break;
            }
            if (ti < 0) continue;
            if (b->Tracks[ti].Locked) continue;

            const float frame = std::clamp(base + c.FrameOffset,
                static_cast<float>(rangeStart), static_cast<float>(rangeEnd));

            // A section do FRAME DE DESTINO, e nao a do playhead: um recorte
            // longo pode atravessar a borda, e cravar tudo na section da agulha
            // poria keys fora do trecho que elas descrevem.
            int si = -1;
            for (int i = 0; i < static_cast<int>(b->Tracks[ti].Sections.size()); ++i) {
                if (b->Tracks[ti].Sections[i].ContainsFrame(frame)) { si = i; break; }
            }
            if (si < 0) si = SectionAtPlayhead(c.Binding, ti);
            if (si < 0) continue;

            const int ci = EnsureChannel(c.Binding, ti, si, c.Component);
            if (ci < 0) continue;

            SequencerKey k = c.Key;
            k.Frame = frame;

            const int ki = m_Asset.AddKey(c.Binding, ti, si, ci, k);
            if (ki < 0) continue;

            pasted.push_back(KeyRef{ c.Binding, ti, si, ci, ki });
            ++written;
        }

        // Selecionar o que acabou de ser colado e o que permite arrastar o
        // bloco inteiro em seguida — que e quase sempre o proximo gesto.
        m_SelectedKeys = std::move(pasted);

        if (!m_SelectedKeys.empty()) {
            const KeyRef& f = m_SelectedKeys.front();
            m_SelectedBinding = f.Binding;
            m_SelectedTrack = f.Track;
            m_SelectedSection = f.Section;
            m_SelectedChannel = f.Channel;
            m_SelectedKey = f.Key;
        }

        if (written > 0)
            AXE_EDITOR_INFO("Sequencer: {} key(s) colada(s) a partir do frame {:.0f}.",
                written, base);
        else
            AXE_EDITOR_WARN("Sequencer: nada colado - as tracks de origem nao "
                "existem mais (ou estao travadas).");

        return written;
    }

    void SequencerWindow::PasteClipboardAtPlayhead() {
        if (m_KeyClipboard.empty()) {
            AXE_EDITOR_INFO("Sequencer: area de transferencia vazia.");
            return;
        }
        PasteKeys(m_KeyClipboard);
    }

    void SequencerWindow::DuplicateSelectedKeys() {
        // NAO passa pela area de transferencia: duplicar nao deve apagar o que
        // o usuario copiou tres gestos atras.
        const std::vector<KeyClip> saved = m_KeyClipboard;

        CopySelectedKeys(false);
        const std::vector<KeyClip> dup = m_KeyClipboard;

        m_KeyClipboard = saved;

        PasteKeys(dup);
    }



    // ============================================================
    // Transform da propria entidade (props, portas, CAMERA)
    // ============================================================

    int SequencerWindow::CreateEntityTransformTrack(int bindingIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return -1;
        if (b->EntityName.empty()) return -1;

        // Uma so por binding: o binding E a entidade, e duas tracks escrevendo
        // no mesmo TransformComponent produziriam a disputa muda de sempre —
        // a ultima avaliada ganha.
        for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i)
            if (b->Tracks[i].TargetType == SequencerTargetType::Entity)
                return i;

        SequencerTrack tr;
        tr.Type = SequencerTrackType::TransformEntity;
        tr.TargetType = SequencerTargetType::Entity;

        // O NOME da entidade, e nao vazio: e o que faz o KeyClip, a pose
        // pendente e a multi-selecao casarem por nome como todos os outros
        // alvos. Um alvo sem nome seria o unico caso especial da lista.
        tr.TargetName = b->EntityName;

        // Section cobrindo o range, e canais VAZIOS — a mesma regra da track de
        // osso. Um canal com key de valor 0 no frame 0 teleportaria a entidade
        // para a origem no instante em que a track nasce.
        SequencerSection sec;
        sec.StartFrame = m_Asset.GetFrameRange().Start;
        sec.EndFrame = m_Asset.GetFrameRange().End;
        tr.Sections.push_back(sec);

        const int ti = m_Asset.AddTrack(bindingIndex, tr);
        EnsurePlayerStarted();

        AXE_EDITOR_INFO("Sequencer: track de transform da entidade '{}' criada.",
            b->EntityName);
        return ti;
    }

    // ── TRACK DE EVENTO ──────────────────────────────────────────────────────
    //
    // Uma track de evento nao anima nada: ela AVISA. Cada key e um instante em
    // que o playhead, tocando, entrega uma mensagem ao script da entidade do
    // binding — "atirou", "porta abriu", "corta para a proxima camera".
    //
    // ── POR QUE O NOME VAI NA TRACK, E NAO NA KEY ────────────────────────────
    //
    // Na key seria mais flexivel e pior de usar: a timeline mostraria uma
    // fileira de losangos identicos, e descobrir qual e "tiro" e qual e "passo"
    // exigiria clicar em cada um. Com o nome na track, a fileira JA e a
    // resposta — e repetir o mesmo evento em cinco frames vira cinco keys numa
    // linha so, que e o gesto normal (passos, rajada, piscar).
    //
    // Quem quiser dois eventos diferentes cria duas tracks, e a timeline passa
    // a mostrar as duas linhas. E a mesma escolha do AnimNotify do clipe.
    int SequencerWindow::CreateEventTrack(int bindingIndex, const std::string& eventName) {
        if (eventName.empty()) return -1;

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return -1;

        // Ja existe uma com este nome? Devolve a existente em vez de duplicar —
        // duas tracks com o mesmo nome disparariam o evento duas vezes, e a
        // causa seria invisivel no outliner.
        for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
            if (b->Tracks[i].Type == SequencerTrackType::Event &&
                b->Tracks[i].TargetName == eventName)
                return i;
        }

        SequencerTrack tr;
        tr.Type = SequencerTrackType::Event;

        // Null: a track nao endereca osso, controle nem entidade. Marcar
        // qualquer outra coisa faria o aplicador procurar um osso com o nome do
        // evento — exatamente o erro que o `continue` no Resample evita.
        tr.TargetType = SequencerTargetType::Null;
        tr.TargetName = eventName;

        SequencerSection sec;
        sec.StartFrame = m_Asset.GetFrameRange().Start;
        sec.EndFrame = m_Asset.GetFrameRange().End;

        // UM canal, vazio. Diferente das tracks de transform, aqui o canal nao
        // significa eixo nenhum — e so onde as keys moram. O `Value` de cada
        // key vira o parametro float que chega no script.
        SequencerChannel ch;
        ch.Component = SequencerChannelComponent::X;
        sec.Channels.push_back(ch);

        tr.Sections.push_back(sec);

        const int ti = m_Asset.AddTrack(bindingIndex, tr);
        EnsurePlayerStarted();

        AXE_EDITOR_INFO("Sequencer: track de evento '{}' criada.", eventName);
        return ti;
    }

    // ── TRACK DE CORTE DE CAMERA ─────────────────────────────────────────────
    //
    // `TargetName` e o nome da entidade de camera. Cada key diz "deste frame em
    // diante, esta camera" — e o corte vale ate a proxima key, de QUALQUER track
    // de corte da sequence.
    //
    // ── EM QUAL BINDING ELA MORA ─────────────────────────────────────────────
    //
    // No binding em que voce clicou, e isso e so onde ela fica estacionada: o
    // avaliador varre as tracks de corte de todos os bindings e compara os
    // frames entre si. Um corte e uma afirmacao sobre a SEQUENCE, nao sobre um
    // personagem.
    //
    // Ancorar no binding da propria camera seria mais bonito no outliner e
    // exigiria que toda camera cortavel fosse um binding — e cortar para uma
    // camera parada, que a sequence nao anima, e um caso perfeitamente normal.
    int SequencerWindow::CreateCameraCutTrack(int bindingIndex,
        const std::string& cameraName) {
        if (cameraName.empty()) return -1;

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return -1;

        for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
            if (b->Tracks[i].Type == SequencerTrackType::CameraCut &&
                b->Tracks[i].TargetName == cameraName)
                return i;
        }

        SequencerTrack tr;
        tr.Type = SequencerTrackType::CameraCut;

        // Null pelo mesmo motivo da track de evento: o alvo nao e osso,
        // controle nem a entidade DESTE binding.
        tr.TargetType = SequencerTargetType::Null;
        tr.TargetName = cameraName;

        SequencerSection sec;
        sec.StartFrame = m_Asset.GetFrameRange().Start;
        sec.EndFrame = m_Asset.GetFrameRange().End;

        SequencerChannel ch;
        ch.Component = SequencerChannelComponent::X;
        sec.Channels.push_back(ch);

        tr.Sections.push_back(sec);

        const int ti = m_Asset.AddTrack(bindingIndex, tr);
        EnsurePlayerStarted();

        AXE_EDITOR_INFO("Sequencer: track de corte para a camera '{}' criada.",
            cameraName);
        return ti;
    }

    // ── O VIEWPORT SEGUE O CORTE ─────────────────────────────────────────────
    //
    // O pulo do gato do "Ver": com uma track de corte, arrastar o playhead nao
    // so mexe a camera — TROCA de camera no frame do corte.
    //
    // So mexe se o usuario JA estiver pilotando. Ligar o modo sozinho porque a
    // sequence tem corte sequestraria o viewport de quem esta editando ossos.
    void SequencerWindow::SyncPilotToCameraCut() {
        if (!m_Context || !m_Context->ActiveScene || !m_Context->Viewport) return;
        if (m_Context->Viewport->PilotCamera == entt::null) return;

        const std::string& cut = m_Player.GetActiveCameraName();
        if (cut.empty()) return;

        const entt::entity e = m_Context->ActiveScene->FindByName(cut);
        if (e == entt::null) return;

        auto& reg = m_Context->ActiveScene->GetRegistry();
        if (!reg.valid(e) || !reg.try_get<CameraComponent>(e)) return;

        m_Context->Viewport->PilotCamera = e;
    }

    bool SequencerWindow::BindingHasEntityTrack(int bindingIndex) const {
        const auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return false;

        for (const auto& tr : b->Tracks)
            if (tr.TargetType == SequencerTargetType::Entity) return true;

        return false;
    }

    // ── APLICA O TRANSFORM AMOSTRADO NA ENTIDADE ─────────────────────────────
    //
    // ── POR QUE GUARDA O ORIGINAL ────────────────────────────────────────────
    //
    // Osso e socket voltam sozinhos: a pose e recomposta da bind pose todo
    // frame, e o preview do socket e uma entidade transiente que morre junto
    // com a janela. O TransformComponent de uma entidade da CENA nao — o que a
    // sequence escreve nele FICA.
    //
    // Sem guardar o original, fechar o Sequencer deixaria a camera (ou a porta,
    // ou o elevador) parada no frame em que o playhead estava. Pior: um Ctrl+S
    // na cena gravaria essa pose como se fosse a posicao autorada.
    //
    // E o equivalente do `PoseOverride` para entidades, e e devolvido no mesmo
    // lugar: ReleasePoseOverride.
    void SequencerWindow::ApplyEntityTransforms() {
        if (!m_Context || !m_Context->ActiveScene) return;

        auto& reg = m_Context->ActiveScene->GetRegistry();

        for (int bi = 0; bi < static_cast<int>(m_Asset.GetBindingCount()); ++bi) {
            const auto* b = m_Asset.GetBinding(bi);
            if (!b) continue;

            const entt::entity e = ResolveBindingEntity(*b);
            if (e == entt::null) continue;

            auto* tc = reg.try_get<TransformComponent>(e);
            if (!tc) continue;

            // Ha track de entidade NAO MUTADA neste binding?
            const SequencerTrack* track = nullptr;
            for (const auto& tr : b->Tracks) {
                if (tr.TargetType != SequencerTargetType::Entity) continue;
                if (tr.Muted) continue;
                track = &tr;
                break;
            }

            if (!track) continue;

            // Primeira vez que dirigimos esta entidade: guarda o que estava la.
            if (m_EntityRestore.find(e) == m_EntityRestore.end())
                m_EntityRestore[e] = tc->Data;

            // ── O ORIGINAL E A BASE, NAO O ZERO ──────────────────────────────
            //
            // Comeca do transform guardado e sobrepoe SO os canais que a
            // sequence anima. Assim animar apenas a altura da camera nao joga
            // a posicao horizontal dela na origem — o mesmo motivo pelo qual a
            // track de osso nasce sem canais.
            //
            // A conta (inclusive a fronteira graus/radianos e o UseWorldMatrix
            // desligado) mora em sequence_apply, e e a mesma que o Play usa.
            tc->Data = sequence::ApplyEntitySamples(
                m_EntityRestore[e], m_EffectiveSamples, bi);
        }
    }

    void SequencerWindow::RestoreEntityTransforms() {
        if (!m_Context || !m_Context->ActiveScene) {
            m_EntityRestore.clear();
            return;
        }

        auto& reg = m_Context->ActiveScene->GetRegistry();

        for (const auto& [e, saved] : m_EntityRestore) {
            if (!reg.valid(e)) continue;
            if (auto* tc = reg.try_get<TransformComponent>(e))
                tc->Data = saved;
        }

        m_EntityRestore.clear();
    }

    void SequencerWindow::ApplyGizmoToEntity(int bindingIndex, int trackIndex,
        const glm::mat4& world) {
        if (!m_Context || !m_Context->ActiveScene) return;

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;

        const entt::entity e = ResolveBindingEntity(*b);
        if (e == entt::null) return;

        auto& reg = m_Context->ActiveScene->GetRegistry();

        // ── MUNDO -> LOCAL AO PAI ────────────────────────────────────────────
        //
        // A mesma conversao que o gizmo de entidade do viewport faz (ver
        // ViewportRenderer::DrawGuizmo): uma entidade filha tem de gravar o
        // transform LOCAL, senao mover o pai deixaria a filha para tras.
        glm::mat4 local = world;

        if (const auto* rel = reg.try_get<RelationshipComponent>(e)) {
            if (rel->Parent != entt::null && reg.valid(rel->Parent)) {
                const glm::mat4 parentWorld =
                    m_Context->ActiveScene->GetWorldTransform(rel->Parent);
                local = glm::inverse(parentWorld) * world;
            }
        }

        glm::vec3 t, s, skew;
        glm::vec4 persp;
        glm::quat q;
        if (!glm::decompose(local, s, q, t, skew, persp)) return;

        if (!m_GroupDragActive && m_SelectedTargets.size() > 1) BeginGroupDrag();

        const int si = m_Recording ? SectionAtPlayhead(bindingIndex, trackIndex) : -1;
        if (m_Recording && si < 0) return;

        const ImGuizmo::OPERATION op = m_Context->Viewport
            ? m_Context->Viewport->GetGizmoOperation() : ImGuizmo::TRANSLATE;

        TargetLocal now;
        now.T = t; now.R = q; now.S = s;

        if (op == ImGuizmo::TRANSLATE) {
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::X, t.x);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::Y, t.y);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::Z, t.z);
            ApplyGroupDelta(now, GizmoChannel::Translate);
        }
        else if (op == ImGuizmo::ROTATE) {
            WriteRotation(bindingIndex, trackIndex, si, q);
            ApplyGroupDelta(now, GizmoChannel::Rotate);
        }
        else if (op == ImGuizmo::SCALE) {
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleX, s.x);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleY, s.y);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleZ, s.z);
            ApplyGroupDelta(now, GizmoChannel::Scale);
        }
    }

    // ============================================================
    // Selecao multipla de alvos + pivo individual
    // ============================================================

    bool SequencerWindow::IsTargetSelected(const TargetRef& t) const {
        for (const auto& s : m_SelectedTargets)
            if (s == t) return true;
        return false;
    }

    SequencerWindow::TargetRef SequencerWindow::ActiveTarget() const {
        if (m_SelectedTargets.empty()) return TargetRef{};
        return m_SelectedTargets.back();
    }

    void SequencerWindow::ClearTargetSelection() {
        m_SelectedTargets.clear();
        EndGroupDrag();
    }

    void SequencerWindow::SelectTarget(const TargetRef& t, bool additive) {
        if (t.Name.empty() || t.Binding < 0) return;

        // Um arrasto em curso mede o delta contra um instantaneo tirado no
        // comeco dele. Mudar o grupo no meio deixaria as duas listas com
        // tamanhos diferentes, e o alvo i passaria a seguir o instantaneo de
        // outro. Fechar o gesto e mais honesto que tentar remendar.
        EndGroupDrag();

        // A selecao mudou: o grafico precisa reenquadrar, senao as curvas novas
        // podem cair inteiras fora da tela.
        m_CurveFitPending = true;

        if (!additive) {
            m_SelectedTargets.clear();
            m_SelectedTargets.push_back(t);
            return;
        }

        for (std::size_t i = 0; i < m_SelectedTargets.size(); ++i) {
            if (!(m_SelectedTargets[i] == t)) continue;

            // Ja estava dentro. Se NAO era o ativo, vira o ativo — clicar de
            // novo num membro do grupo quase sempre quer dizer "agora mexe por
            // este". Se ja era, sai do grupo.
            if (i + 1 == m_SelectedTargets.size()) {
                m_SelectedTargets.pop_back();
            }
            else {
                const TargetRef moved = m_SelectedTargets[i];
                m_SelectedTargets.erase(m_SelectedTargets.begin() + i);
                m_SelectedTargets.push_back(moved);
            }
            return;
        }

        m_SelectedTargets.push_back(t);
    }

    void SequencerWindow::SyncActiveFromTargets() {
        const TargetRef act = ActiveTarget();

        if (act.Binding < 0 || act.Name.empty()) {
            m_SelectedTrack = -1;
            m_ViewportControlBinding = -1;
            m_ViewportControlName.clear();
            return;
        }

        m_SelectedBinding = act.Binding;
        m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;

        int ti = -1;
        if (const auto* b = m_Asset.GetBinding(act.Binding)) {
            for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
                if (b->Tracks[i].TargetType != act.Type) continue;
                if (b->Tracks[i].TargetName != act.Name) continue;
                ti = i;
                break;
            }
        }

        m_SelectedTrack = ti;

        // Sem track e sendo controle: o gizmo mira pelo NOME. Ver a nota de
        // precedencia em UpdateGizmo.
        if (ti < 0 && act.Type == SequencerTargetType::Control) {
            m_ViewportControlBinding = act.Binding;
            m_ViewportControlName = act.Name;
        }
        else {
            m_ViewportControlBinding = -1;
            m_ViewportControlName.clear();
        }
    }

    bool SequencerWindow::ReadTargetLocal(const TargetRef& t, TargetLocal& out) const {
        // ── UM ALVO SINTETICO ────────────────────────────────────────────────
        //
        // `CaptureChannelValue` pede uma SequencerTrack, mas so le tres campos
        // dela (Type, TargetType, TargetName) — e um alvo do grupo pode ainda
        // nao ter track nenhuma. Montar uma track de mentira aqui reusa a
        // captura JA testada, com as regras dela (o neutro do socket, o Value do
        // controle e nao o Current, a pose de trabalho do osso) em vez de
        // reescrever uma segunda versao que vai divergir.
        SequencerTrack tmp;
        tmp.TargetType = t.Type;
        tmp.TargetName = t.Name;

        switch (t.Type) {
        case SequencerTargetType::Control: tmp.Type = SequencerTrackType::TransformControl; break;
        case SequencerTargetType::Socket:  tmp.Type = SequencerTrackType::TransformSocket;  break;
        case SequencerTargetType::Entity:  tmp.Type = SequencerTrackType::TransformEntity;  break;
        default:                           tmp.Type = SequencerTrackType::TransformBone;    break;
        }

        float v[9] = { 0,0,0, 0,0,0, 1,1,1 };

        static const SequencerChannelComponent kAll[9] = {
            SequencerChannelComponent::X,      SequencerChannelComponent::Y,      SequencerChannelComponent::Z,
            SequencerChannelComponent::RotX,   SequencerChannelComponent::RotY,   SequencerChannelComponent::RotZ,
            SequencerChannelComponent::ScaleX, SequencerChannelComponent::ScaleY, SequencerChannelComponent::ScaleZ
        };

        for (int i = 0; i < 9; ++i) {
            float got = v[i];
            if (!CaptureChannelValue(t.Binding, tmp, kAll[i], got)) return false;
            v[i] = got;
        }

        out.T = glm::vec3(v[0], v[1], v[2]);
        out.R = glm::quat(glm::radians(glm::vec3(v[3], v[4], v[5])));   // graus na curva
        out.S = glm::vec3(v[6], v[7], v[8]);
        return true;
    }

    void SequencerWindow::WriteTargetLocal(const TargetRef& t, const TargetLocal& val,
        GizmoChannel op) {
        int ti = -1;

        auto* b = m_Asset.GetBinding(t.Binding);
        if (!b) return;

        for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
            if (b->Tracks[i].TargetType != t.Type) continue;
            if (b->Tracks[i].TargetName != t.Name) continue;
            ti = i;
            break;
        }

        if (ti < 0) {
            // Mesma regra do alvo unico: a track nasce na MANIPULACAO. So vale
            // para controle — osso, socket e entidade nao tem como estar no
            // grupo sem track, porque a unica forma de seleciona-los e pelo
            // outliner, que lista tracks.
            if (t.Type != SequencerTargetType::Control) return;

            ti = CreateControlTrack(t.Binding, t.Name);
            if (ti < 0) return;
        }

        if (b->Tracks[ti].Locked) return;

        const int si = m_Recording ? SectionAtPlayhead(t.Binding, ti) : -1;
        if (m_Recording && si < 0) return;

        switch (op) {
        case GizmoChannel::Translate:
            WriteChannelValue(t.Binding, ti, si, SequencerChannelComponent::X, val.T.x);
            WriteChannelValue(t.Binding, ti, si, SequencerChannelComponent::Y, val.T.y);
            WriteChannelValue(t.Binding, ti, si, SequencerChannelComponent::Z, val.T.z);
            break;

        case GizmoChannel::Rotate:
            WriteRotation(t.Binding, ti, si, val.R);
            break;

        case GizmoChannel::Scale:
            WriteChannelValue(t.Binding, ti, si, SequencerChannelComponent::ScaleX, val.S.x);
            WriteChannelValue(t.Binding, ti, si, SequencerChannelComponent::ScaleY, val.S.y);
            WriteChannelValue(t.Binding, ti, si, SequencerChannelComponent::ScaleZ, val.S.z);
            break;
        }
    }

    void SequencerWindow::BeginGroupDrag() {
        m_GroupStart.clear();
        m_GroupStart.reserve(m_SelectedTargets.size());

        for (const auto& t : m_SelectedTargets) {
            TargetLocal l;
            if (!ReadTargetLocal(t, l)) l = TargetLocal{};
            m_GroupStart.push_back(l);
        }

        m_GroupDragActive = true;
    }

    void SequencerWindow::ApplyGroupDelta(const TargetLocal& activeNow, GizmoChannel op) {
        if (!m_GroupDragActive) return;
        if (m_SelectedTargets.size() < 2) return;
        if (m_GroupStart.size() != m_SelectedTargets.size()) return;

        const TargetLocal& a0 = m_GroupStart.back();   // o ativo e o ultimo

        const glm::vec3 dT = activeNow.T - a0.T;
        const glm::quat dR = activeNow.R * glm::inverse(a0.R);

        // Razao, e nao diferenca: escala e multiplicativa, e somar deltas faria
        // um alvo que comeca em 2.0 crescer o dobro do que comeca em 1.0.
        // Denominador zero nao existe em escala util, mas um asset editado a mao
        // pode traze-lo — 1.0 ali quer dizer "nao mexe".
        const glm::vec3 dS(
            std::abs(a0.S.x) > 1e-6f ? activeNow.S.x / a0.S.x : 1.0f,
            std::abs(a0.S.y) > 1e-6f ? activeNow.S.y / a0.S.y : 1.0f,
            std::abs(a0.S.z) > 1e-6f ? activeNow.S.z / a0.S.z : 1.0f);

        // Todos MENOS o ultimo: o ativo ja foi escrito pelo ApplyGizmoTo* que
        // chamou esta funcao.
        for (std::size_t i = 0; i + 1 < m_SelectedTargets.size(); ++i) {
            TargetLocal v = m_GroupStart[i];

            switch (op) {
            case GizmoChannel::Translate: v.T = m_GroupStart[i].T + dT;       break;
            case GizmoChannel::Rotate:    v.R = dR * m_GroupStart[i].R;       break;
            case GizmoChannel::Scale:     v.S = m_GroupStart[i].S * dS;       break;
            }

            WriteTargetLocal(m_SelectedTargets[i], v, op);
        }
    }

    // ============================================================
    // Controles do rig no viewport
    // ============================================================

    int SequencerWindow::FindControlTrack(int bindingIndex,
        const std::string& controlName) const {
        const auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || controlName.empty()) return -1;

        for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
            if (b->Tracks[i].TargetType != SequencerTargetType::Control) continue;
            if (b->Tracks[i].TargetName == controlName) return i;
        }
        return -1;
    }

    bool SequencerWindow::ControlWorld(int bindingIndex, const std::string& controlName,
        glm::mat4& outWorld) const {
        if (!m_Context || !m_Context->ActiveScene) return false;

        const auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return false;

        auto it = m_Rigs.find(bindingIndex);
        if (it == m_Rigs.end() || !it->second.Ready) return false;

        // Revalidado contra o UUID, como todo uso de m_Rigs: remover um binding
        // desloca os indices, e uma copia presa ao indice antigo dirigiria o
        // personagem errado.
        if (it->second.SourceUUID != b->RigAssetUUID) return false;

        const int idx = it->second.Hierarchy.Find(controlName, RigElementType::Control);
        if (idx < 0) return false;

        const entt::entity e = ResolveBindingEntity(*b);
        if (e == entt::null) return false;

        outWorld = m_Context->ActiveScene->GetWorldTransform(e)
            * it->second.Hierarchy.GetGlobal(idx);
        return true;
    }

    void SequencerWindow::SelectControlFromViewport(int bindingIndex,
        const std::string& controlName, bool additive) {
        if (controlName.empty()) return;

        SelectTarget(TargetRef{ bindingIndex, SequencerTargetType::Control, controlName },
            additive);

        ClearKeySelection();
        SyncActiveFromTargets();

        // O grupo dobrado esconderia a linha que acabamos de selecionar.
        if (m_SelectedTrack >= 0)
            SetGroupOpen(bindingIndex, TrackGroup::Control, true);
    }

    void SequencerWindow::ApplyGizmoToControlByName(int bindingIndex,
        const std::string& controlName, const glm::mat4& world) {
        int ti = FindControlTrack(bindingIndex, controlName);

        if (ti < 0) {
            // ── A TRACK NASCE AQUI ───────────────────────────────────────────
            //
            // Nao no clique: selecionar e olhar, e olhar nao deve escrever no
            // asset. Chegar nesta funcao ja e uma manipulacao — o usuario
            // arrastou o gizmo.
            ti = CreateControlTrack(bindingIndex, controlName);
            if (ti < 0) return;

            m_SelectedBinding = bindingIndex;
            m_SelectedTrack = ti;
            m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;

            // A partir daqui a selecao e por TRACK. Limpar o nome e o que faz o
            // UpdateGizmo devolver o controle ao caminho normal no proximo
            // frame, sem nenhum ramo especial.
            m_ViewportControlBinding = -1;
            m_ViewportControlName.clear();

            SetGroupOpen(bindingIndex, TrackGroup::Control, true);

            AXE_EDITOR_INFO("Sequencer: track do controle '{}' criada na primeira "
                "manipulacao.", controlName);
        }

        ApplyGizmoToControl(bindingIndex, ti, world);
    }

    bool SequencerWindow::DrawViewportControls(const glm::vec2& boundsMin,
        const glm::vec2& boundsMax) {
        if (!m_IsOpen || !m_ShowViewportControls) return false;
        if (!m_Context || !m_Context->ActiveScene || !m_Context->Viewport) return false;

        const EditorCamera* cam = m_Context->Viewport->m_Camera.get();
        if (!cam) return false;

        const ImVec2 imgMin(boundsMin.x, boundsMin.y);
        const ImVec2 imgSize(boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y);
        if (imgSize.x <= 0.0f || imgSize.y <= 0.0f) return false;

        const glm::mat4 vp = cam->GetViewProjectionMatrix();

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── QUANDO NAO SE DEVE PEGAR O CLIQUE ────────────────────────────────
        //
        // Sobre o gizmo: a seta que o usuario quer arrastar costuma estar EM
        // CIMA do controle, e perder o arrasto para uma reselecao do mesmo
        // controle seria o pior tipo de conflito — o que parece aleatorio.
        //
        // Com Alt: e a camera. Orbitar por cima de um controle nao e clicar
        // nele.
        //
        // `ImGuizmo::IsOver()` reflete o frame ANTERIOR (o overlay desenha
        // antes do Manipulate). Um frame de atraso num teste de hover nao e
        // perceptivel, e a alternativa — desenhar depois — poria as formas por
        // cima do gizmo.
        const ImGuiIO& io = ImGui::GetIO();

        const bool canPick =
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !io.KeyAlt &&
            !ImGuizmo::IsOver() &&
            !ImGuizmo::IsUsing();

        bool consumed = false;

        const int bindingCount = static_cast<int>(m_Asset.GetBindingCount());

        for (int bi = 0; bi < bindingCount; ++bi) {
            const auto* b = m_Asset.GetBinding(bi);
            if (!b || b->RigAssetUUID.empty()) continue;

            auto it = m_Rigs.find(bi);
            if (it == m_Rigs.end() || !it->second.Ready) continue;
            if (it->second.SourceUUID != b->RigAssetUUID) continue;

            const entt::entity e = ResolveBindingEntity(*b);
            if (e == entt::null) continue;

            const RigHierarchy& h = it->second.Hierarchy;

            // Qual elemento esta destacado NESTE binding. Duas fontes, e a
            // ordem importa: a selecao por track e a que vale quando existe,
            // porque e ela que o resto da janela mostra.
            // O ATIVO e o ultimo do grupo — e o unico que o gizmo manipula
            // diretamente. Sem grupo, cai no comportamento antigo (a track
            // selecionada, ou o controle escolhido no viewport sem track).
            int selectedIdx = -1;
            {
                std::string name;

                const TargetRef act = ActiveTarget();

                if (act.Binding == bi && act.Type == SequencerTargetType::Control) {
                    name = act.Name;
                }
                else if (m_SelectedBinding == bi && m_SelectedTrack >= 0 &&
                    m_SelectedTrack < static_cast<int>(b->Tracks.size()) &&
                    b->Tracks[m_SelectedTrack].TargetType == SequencerTargetType::Control) {
                    name = b->Tracks[m_SelectedTrack].TargetName;
                }
                else if (m_ViewportControlBinding == bi) {
                    name = m_ViewportControlName;
                }

                if (!name.empty())
                    selectedIdx = h.Find(name, RigElementType::Control);
            }

            // Os DEMAIS do grupo, para acenderem junto. Reconstruido por frame:
            // sao poucos, e guardar indices entre frames os deixaria apontando
            // para uma hierarquia que pode ter sido reclonada.
            std::vector<int> groupIdx;
            groupIdx.reserve(m_SelectedTargets.size());

            for (const auto& t : m_SelectedTargets) {
                if (t.Binding != bi) continue;
                if (t.Type != SequencerTargetType::Control) continue;

                const int gi = h.Find(t.Name, RigElementType::Control);
                if (gi >= 0 && gi != selectedIdx) groupIdx.push_back(gi);
            }

            int prevHovered = -1;
            if (auto hv = m_ViewportHovered.find(bi); hv != m_ViewportHovered.end())
                prevHovered = hv->second;

            const glm::mat4 model = m_Context->ActiveScene->GetWorldTransform(e);

            const int hovered = ui::DrawRigControlGizmos(
                dl, h, vp, model, imgMin, imgSize, selectedIdx, prevHovered,
                groupIdx.empty() ? nullptr : &groupIdx);

            m_ViewportHovered[bi] = hovered;

            if (hovered >= 0) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                // Nome sob o cursor. Vinte controles com formas parecidas viram
                // um enigma sem isto — e "qual e este?" e a pergunta que faz o
                // animador voltar para a lista, que e justamente o que este
                // recurso existe para evitar.
                if (!consumed)
                    ImGui::SetTooltip("%s", h[hovered].Name.c_str());

                if (canPick && !consumed) {
                    const ImGuiIO& mio = ImGui::GetIO();
                    SelectControlFromViewport(bi, h[hovered].Name,
                        mio.KeyCtrl || mio.KeyShift);
                    consumed = true;
                }
            }
        }

        return consumed;
    }

    void SequencerWindow::UpdateViewportOverlay() {
        if (!m_Context || !m_Context->Viewport) return;

        if (!m_IsOpen || !m_ShowViewportControls) {
            m_Context->Viewport->ClearExternalOverlay();
            return;
        }

        ViewportRenderer::ExternalOverlay o;
        o.Active = true;
        o.OnDraw = [this](const glm::vec2& mn, const glm::vec2& mx) {
            return DrawViewportControls(mn, mx);
            };

        m_Context->Viewport->SetExternalOverlay(o);
    }

    void SequencerWindow::UpdateGizmo() {
        if (!m_Context || !m_Context->Viewport) return;

        // Sem alvo manipulavel: LIMPA o pedido. Nao basta "nao pedir" — o
        // viewport guarda o ultimo, e um gizmo esquecido continuaria movendo o
        // osso de uma track que o usuario ja desselecionou.
        auto clear = [&]() { m_Context->Viewport->ClearExternalGizmo(); };

        if (!m_IsOpen || !m_GizmoEnabled) { clear(); return; }

        // ── PRECEDENCIA: TRACK VENCE NOME ────────────────────────────────────
        //
        // `m_ViewportControlName` so vale enquanto NAO ha track selecionada.
        // Isto e o que faz clicar numa track no outliner tirar o gizmo do
        // controle escolhido no viewport, sem que nenhum dos ~6 pontos que
        // atribuem m_SelectedTrack precise saber que este estado existe.
        if (m_SelectedTrack >= 0 && !m_ViewportControlName.empty()) {
            m_ViewportControlBinding = -1;
            m_ViewportControlName.clear();
        }

        // ── CONTROLE SEM TRACK ───────────────────────────────────────────────
        //
        // O caminho normal parte de uma track. Um controle recem-clicado no
        // viewport ainda nao tem uma — e nao deve ter, porque selecionar nao
        // escreve no asset. Este ramo mira nele mesmo assim; a track nasce no
        // ApplyGizmoToControlByName, na primeira manipulacao.
        if (m_ViewportControlBinding >= 0 && !m_ViewportControlName.empty()) {
            glm::mat4 world(1.0f);

            if (ControlWorld(m_ViewportControlBinding, m_ViewportControlName, world)) {
                const int         cbi = m_ViewportControlBinding;
                const std::string cname = m_ViewportControlName;

                ViewportRenderer::ExternalGizmo g;
                g.Active = true;
                g.World = world;

                g.OnManipulate = [this, cbi, cname](const glm::mat4& m) {
                    ApplyGizmoToControlByName(cbi, cname, m);
                    };

                g.OnFinish = [this, cbi, cname]() {
                    EndGroupDrag();

                    // A track ja existe neste ponto (o OnManipulate a criou);
                    // se o usuario clicou sem arrastar, nao ha nada a ordenar.
                    const int ti = FindControlTrack(cbi, cname);
                    if (ti < 0) return;

                    auto* bb = m_Asset.GetBinding(cbi);
                    if (!bb) return;

                    auto& t = bb->Tracks[ti];
                    for (int si = 0; si < static_cast<int>(t.Sections.size()); ++si)
                        for (int ci = 0; ci < static_cast<int>(t.Sections[si].Channels.size()); ++ci)
                            SortChannelAndRemap(cbi, ti, si, ci);
                    };

                m_Context->Viewport->SetExternalGizmo(g);
                return;
            }

            // O controle sumiu (rig trocado, binding removido): esquece.
            m_ViewportControlBinding = -1;
            m_ViewportControlName.clear();
        }

        const int bi = m_SelectedBinding;
        const int ti = m_SelectedTrack;

        auto* b = m_Asset.GetBinding(bi);
        if (!b || ti < 0 || ti >= static_cast<int>(b->Tracks.size())) { clear(); return; }

        const SequencerTrack& tr = b->Tracks[ti];

        // Track travada nao recebe gizmo. Um cadeado que so protege contra o
        // Delete e meio cadeado.
        if (tr.Locked) { clear(); return; }

        glm::mat4 world(1.0f);
        if (!GizmoTargetWorld(bi, tr, world)) { clear(); return; }

        ViewportRenderer::ExternalGizmo g;
        g.Active = true;
        g.World = world;

        const SequencerTrackType type = tr.Type;

        g.OnManipulate = [this, bi, ti, type](const glm::mat4& m) {
            switch (type) {
            case SequencerTrackType::TransformBone:    ApplyGizmoToBone(bi, ti, m);    break;
            case SequencerTrackType::TransformSocket:  ApplyGizmoToSocket(bi, ti, m);  break;
            case SequencerTrackType::TransformControl: ApplyGizmoToControl(bi, ti, m); break;
            case SequencerTrackType::TransformEntity:  ApplyGizmoToEntity(bi, ti, m);  break;
            default: break;
            }
            };

        g.OnFinish = [this, bi, ti]() {
            EndGroupDrag();

            // Ordena os canais tocados: o arrasto cravou keys via AddKey, que
            // ja mantem ordem, mas uma key criada num frame anterior ao playhead
            // (scrub para tras no meio do gesto) pode ter entrado fora de lugar.
            auto* bb = m_Asset.GetBinding(bi);
            if (!bb || ti < 0 || ti >= static_cast<int>(bb->Tracks.size())) return;

            auto& t = bb->Tracks[ti];
            for (int si = 0; si < static_cast<int>(t.Sections.size()); ++si)
                for (int ci = 0; ci < static_cast<int>(t.Sections[si].Channels.size()); ++ci)
                    SortChannelAndRemap(bi, ti, si, ci);
            };

        m_Context->Viewport->SetExternalGizmo(g);
    }

    void SequencerWindow::ApplyGizmoToBone(int bindingIndex, int trackIndex,
        const glm::mat4& world) {
        if (!m_Context || !m_Context->ActiveScene) return;

        // Instantaneo do grupo ANTES de qualquer escrita. Ver ApplyGroupDelta.
        if (!m_GroupDragActive && m_SelectedTargets.size() > 1) BeginGroupDrag();

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;

        const std::string boneName = b->Tracks[trackIndex].TargetName;

        SkeletalMeshComponent* smc = nullptr;
        const Skeleton* skel = GetBindingSkeleton(bindingIndex, &smc);
        if (!skel || !smc) return;

        const int boneIdx = skel->FindBone(boneName);
        if (boneIdx < 0 || boneIdx >= static_cast<int>(smc->BoneGlobals.size())) return;

        const entt::entity e = ResolveBindingEntity(*b);
        if (e == entt::null) return;

        const glm::mat4 entityWorld = m_Context->ActiveScene->GetWorldTransform(e);

        // ── MUNDO -> LOCAL AO PAI ────────────────────────────────────────────
        //
        // A pose do Sequencer e LOCAL, e tem de ser: e o espaco local que faz
        // mover o ombro levar o braco junto. O gizmo devolve mundo, entao a
        // conversao passa pelo global do PAI — o mesmo que o BuildSkinningMatrices
        // usou para compor (`globals[i] = globals[pai] * local[i]`).
        const int parentIdx = skel->GetBones()[boneIdx].ParentIndex;

        const glm::mat4 parentWorld = (parentIdx >= 0 &&
            parentIdx < static_cast<int>(smc->BoneGlobals.size()))
            ? entityWorld * smc->BoneGlobals[parentIdx]
            : entityWorld;

        const glm::mat4 local = glm::inverse(parentWorld) * world;

        glm::vec3 t, s, skew;
        glm::vec4 persp;
        glm::quat q;
        if (!glm::decompose(local, s, q, t, skew, persp)) return;

        // ── SECTION SO QUANDO VAI VIRAR KEY ──────────────────────────────────
        //
        // Com REC desligado nao ha nada a gravar, e o SectionAtPlayhead CRIA
        // section numa track que ainda nao tem nenhuma. Chama-lo aqui faria uma
        // pose descartada deixar uma section vazia para tras — um efeito
        // colateral permanente de um gesto explicitamente temporario.
        const int si = m_Recording ? SectionAtPlayhead(bindingIndex, trackIndex) : -1;
        if (m_Recording && si < 0) return;

        // ── SO OS CANAIS DA OPERACAO ATIVA ───────────────────────────────────
        //
        // Keyar os nove a cada toque encheria a timeline de curvas retas de
        // escala e translacao que o animador nunca pediu — e cada uma delas
        // depois congela o osso caso o esqueleto mude. O gizmo esta em modo
        // rotacao? Entao so as tres rotacoes viram key.
        const ImGuizmo::OPERATION op = m_Context->Viewport
            ? m_Context->Viewport->GetGizmoOperation() : ImGuizmo::TRANSLATE;

        TargetLocal now;
        now.T = t; now.R = q; now.S = s;

        if (op == ImGuizmo::TRANSLATE) {
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::X, t.x);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::Y, t.y);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::Z, t.z);
            ApplyGroupDelta(now, GizmoChannel::Translate);
        }
        else if (op == ImGuizmo::ROTATE) {
            WriteRotation(bindingIndex, trackIndex, si, q);
            ApplyGroupDelta(now, GizmoChannel::Rotate);
        }
        else if (op == ImGuizmo::SCALE) {
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleX, s.x);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleY, s.y);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleZ, s.z);
            ApplyGroupDelta(now, GizmoChannel::Scale);
        }
    }

    void SequencerWindow::ApplyGizmoToSocket(int bindingIndex, int trackIndex,
        const glm::mat4& world) {
        if (!m_Context || !m_Context->ActiveScene) return;

        // Instantaneo do grupo ANTES de qualquer escrita. Ver ApplyGroupDelta.
        if (!m_GroupDragActive && m_SelectedTargets.size() > 1) BeginGroupDrag();

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b || trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;

        const std::string socketName = b->Tracks[trackIndex].TargetName;

        SkeletalMeshComponent* smc = nullptr;
        const Skeleton* skel = GetBindingSkeleton(bindingIndex, &smc);
        if (!skel || !smc || !smc->Asset) return;

        const auto* sock = smc->Asset->FindSocket(socketName);
        if (!sock) return;

        const int boneIdx = skel->FindBone(sock->BoneName);
        if (boneIdx < 0 || boneIdx >= static_cast<int>(smc->BoneGlobals.size())) return;

        const entt::entity e = ResolveBindingEntity(*b);
        if (e == entt::null) return;

        // A base e o SOCKET (osso + offset do .axeskel). O que a curva grava e o
        // que sobra em cima disso — mexer no gizmo nunca reescreve o `.axeskel`.
        const glm::mat4 socketWorld =
            m_Context->ActiveScene->GetWorldTransform(e)
            * smc->BoneGlobals[boneIdx]
            * smc->Asset->GetSocketLocalTransform(*sock);

        const glm::mat4 local = glm::inverse(socketWorld) * world;

        glm::vec3 t, s, skew;
        glm::vec4 persp;
        glm::quat q;
        if (!glm::decompose(local, s, q, t, skew, persp)) return;

        // ── SECTION SO QUANDO VAI VIRAR KEY ──────────────────────────────────
        //
        // Com REC desligado nao ha nada a gravar, e o SectionAtPlayhead CRIA
        // section numa track que ainda nao tem nenhuma. Chama-lo aqui faria uma
        // pose descartada deixar uma section vazia para tras — um efeito
        // colateral permanente de um gesto explicitamente temporario.
        const int si = m_Recording ? SectionAtPlayhead(bindingIndex, trackIndex) : -1;
        if (m_Recording && si < 0) return;

        const ImGuizmo::OPERATION op = m_Context->Viewport
            ? m_Context->Viewport->GetGizmoOperation() : ImGuizmo::TRANSLATE;

        TargetLocal now;
        now.T = t; now.R = q; now.S = s;

        if (op == ImGuizmo::TRANSLATE) {
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::X, t.x);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::Y, t.y);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::Z, t.z);
            ApplyGroupDelta(now, GizmoChannel::Translate);
        }
        else if (op == ImGuizmo::ROTATE) {
            WriteRotation(bindingIndex, trackIndex, si, q);
            ApplyGroupDelta(now, GizmoChannel::Rotate);
        }
        else if (op == ImGuizmo::SCALE) {
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleX, s.x);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleY, s.y);
            WriteChannelValue(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleZ, s.z);
            ApplyGroupDelta(now, GizmoChannel::Scale);
        }
    }

    // ============================================================
    // Selecao multipla de keys
    // ============================================================

    SequencerKey* SequencerWindow::ResolveKeyRef(const KeyRef& r) {
        auto* b = m_Asset.GetBinding(r.Binding);
        if (!b) return nullptr;
        if (r.Track < 0 || r.Track >= static_cast<int>(b->Tracks.size())) return nullptr;

        auto& tr = b->Tracks[r.Track];
        if (r.Section < 0 || r.Section >= static_cast<int>(tr.Sections.size())) return nullptr;

        auto& sec = tr.Sections[r.Section];
        if (r.Channel < 0 || r.Channel >= static_cast<int>(sec.Channels.size())) return nullptr;

        auto& ch = sec.Channels[r.Channel];
        if (r.Key < 0 || r.Key >= static_cast<int>(ch.Keys.size())) return nullptr;

        return &ch.Keys[r.Key];
    }

    bool SequencerWindow::IsKeySelected(const KeyRef& k) const {
        return std::find(m_SelectedKeys.begin(), m_SelectedKeys.end(), k)
            != m_SelectedKeys.end();
    }

    void SequencerWindow::ToggleKeySelection(const KeyRef& k) {
        auto it = std::find(m_SelectedKeys.begin(), m_SelectedKeys.end(), k);
        if (it != m_SelectedKeys.end()) m_SelectedKeys.erase(it);
        else m_SelectedKeys.push_back(k);
    }

    void SequencerWindow::SetSingleKeySelection(const KeyRef& k) {
        m_SelectedKeys.clear();
        m_SelectedKeys.push_back(k);
    }

    void SequencerWindow::ClearKeySelection() {
        m_SelectedKeys.clear();
    }

    void SequencerWindow::SelectAllKeys() {
        m_SelectedKeys.clear();

        for (int bi = 0; bi < static_cast<int>(m_Asset.GetBindingCount()); ++bi) {
            auto* b = m_Asset.GetBinding(bi);
            if (!b) continue;

            for (int ti = 0; ti < static_cast<int>(b->Tracks.size()); ++ti) {
                // Track TRAVADA fica de fora. Lock existe para proteger uma
                // performance ja aprovada enquanto se mexe no resto; um Ctrl+A
                // que a incluisse tornaria o cadeado decorativo — bastaria um
                // Delete distraido depois.
                if (b->Tracks[ti].Locked) continue;

                auto& tr = b->Tracks[ti];
                for (int si = 0; si < static_cast<int>(tr.Sections.size()); ++si) {
                    auto& sec = tr.Sections[si];
                    for (int ci = 0; ci < static_cast<int>(sec.Channels.size()); ++ci) {
                        auto& ch = sec.Channels[ci];
                        for (int ki = 0; ki < static_cast<int>(ch.Keys.size()); ++ki)
                            m_SelectedKeys.push_back(KeyRef{ bi, ti, si, ci, ki });
                    }
                }
            }
        }

        // A primaria (a que o painel de edicao mostra) passa a ser a primeira.
        if (!m_SelectedKeys.empty()) {
            const KeyRef& f = m_SelectedKeys.front();
            m_SelectedBinding = f.Binding;
            m_SelectedTrack = f.Track;
            m_SelectedSection = f.Section;
            m_SelectedChannel = f.Channel;
            m_SelectedKey = f.Key;
        }

        AXE_EDITOR_INFO("Sequencer: {} key(s) selecionada(s).",
            (int)m_SelectedKeys.size());
    }



    // ============================================================
    // Editor de curvas
    // ============================================================

    namespace {

        // Cor por EIXO, brilho por grupo. O olho separa os tres eixos por cor
        // (a convencao X/Y/Z = vermelho/verde/azul que o gizmo ja usa) e os tres
        // grupos por intensidade — assim RotX e X sao parentes visiveis sem
        // precisar de nove cores que ninguem decora.
        ImU32 CurveColor(SequencerChannelComponent c) {
            int axis = 0;
            float mul = 1.0f;

            switch (c) {
            case SequencerChannelComponent::X:      axis = 0; mul = 1.00f; break;
            case SequencerChannelComponent::Y:      axis = 1; mul = 1.00f; break;
            case SequencerChannelComponent::Z:      axis = 2; mul = 1.00f; break;
            case SequencerChannelComponent::RotX:   axis = 0; mul = 0.78f; break;
            case SequencerChannelComponent::RotY:   axis = 1; mul = 0.78f; break;
            case SequencerChannelComponent::RotZ:   axis = 2; mul = 0.78f; break;
            case SequencerChannelComponent::ScaleX: axis = 0; mul = 0.55f; break;
            case SequencerChannelComponent::ScaleY: axis = 1; mul = 0.55f; break;
            case SequencerChannelComponent::ScaleZ: axis = 2; mul = 0.55f; break;
            }

            const float base[3][3] = {
                { 1.00f, 0.35f, 0.35f },   // X
                { 0.40f, 0.95f, 0.40f },   // Y
                { 0.40f, 0.60f, 1.00f },   // Z
            };

            return ImGui::ColorConvertFloat4ToU32(ImVec4(
                base[axis][0] * mul, base[axis][1] * mul, base[axis][2] * mul, 1.0f));
        }

        // Passo "redondo" mais proximo de `target` unidades. Uma grade em
        // 0.37 em 0.37 nao ajuda ninguem a ler um valor.
        float NiceStep(float target) {
            if (target <= 0.0f) return 1.0f;

            const float mag = std::pow(10.0f, std::floor(std::log10(target)));
            const float norm = target / mag;

            if (norm < 1.5f) return 1.0f * mag;
            if (norm < 3.5f) return 2.0f * mag;
            if (norm < 7.5f) return 5.0f * mag;
            return 10.0f * mag;
        }

    } // namespace

    std::vector<SequencerWindow::CurveRef> SequencerWindow::CollectCurveChannels() const {
        std::vector<CurveRef> out;

        // ── QUAIS CURVAS APARECEM ────────────────────────────────────────────
        //
        // As dos alvos SELECIONADOS. Mostrar tudo transformaria o grafico numa
        // meada — um clipe explodido tem centenas de canais —, e mostrar so o
        // canal selecionado impediria de comparar X com Y, que e metade do
        // motivo de existir um grafico.
        //
        // A multi-selecao de alvos ja resolve a escolha: o animador seleciona a
        // cadeia que esta ajustando e ve as curvas dela.
        auto addTrack = [&](int bi, int ti) {
            const auto* b = m_Asset.GetBinding(bi);
            if (!b || ti < 0 || ti >= static_cast<int>(b->Tracks.size())) return;

            const auto& tr = b->Tracks[ti];

            for (int si = 0; si < static_cast<int>(tr.Sections.size()); ++si) {
                for (int ci = 0; ci < static_cast<int>(tr.Sections[si].Channels.size()); ++ci) {
                    const auto comp = tr.Sections[si].Channels[ci].Component;

                    const bool isT = (comp == SequencerChannelComponent::X ||
                        comp == SequencerChannelComponent::Y ||
                        comp == SequencerChannelComponent::Z);
                    const bool isR = (comp == SequencerChannelComponent::RotX ||
                        comp == SequencerChannelComponent::RotY ||
                        comp == SequencerChannelComponent::RotZ);

                    if (isT && !m_CurveShowT) continue;
                    if (isR && !m_CurveShowR) continue;
                    if (!isT && !isR && !m_CurveShowS) continue;

                    out.push_back(CurveRef{ bi, ti, si, ci });
                }
            }
            };

        if (!m_SelectedTargets.empty()) {
            for (const auto& t : m_SelectedTargets) {
                const auto* b = m_Asset.GetBinding(t.Binding);
                if (!b) continue;

                for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
                    if (b->Tracks[i].TargetType != t.Type) continue;
                    if (b->Tracks[i].TargetName != t.Name) continue;
                    addTrack(t.Binding, i);
                    break;
                }
            }
        }
        else if (m_SelectedBinding >= 0 && m_SelectedTrack >= 0) {
            addTrack(m_SelectedBinding, m_SelectedTrack);
        }

        return out;
    }

    void SequencerWindow::DrawCurveArea(ImDrawList* dl,
        const ImVec2& origin, const ImVec2& size,
        float areaTop, float areaBottom,
        float startFrame, float frameWidth,
        bool timelineHovered,
        const ImRect& boxRect, bool boxActive) {

        const std::vector<CurveRef> curves = CollectCurveChannels();

        const float midY = (areaTop + areaBottom) * 0.5f;

        auto frameToX = [&](float f) { return origin.x + (f - startFrame) * frameWidth; };
        auto xToFrame = [&](float x) { return startFrame + (x - origin.x) / frameWidth; };

        // ── ENQUADRAMENTO ────────────────────────────────────────────────────
        //
        // Cair num grafico com a curva fora da tela e a primeira impressao de
        // "isto nao funciona". Enquadra ao entrar no modo e ao trocar a
        // selecao, e nunca mais depois — dai em diante o zoom e do usuario.
        if (m_CurveFitPending) {
            float lo = std::numeric_limits<float>::max();
            float hi = std::numeric_limits<float>::lowest();

            for (const auto& c : curves) {
                const auto* b = m_Asset.GetBinding(c.Binding);
                if (!b) continue;
                const auto& ch = b->Tracks[c.Track].Sections[c.Section].Channels[c.Channel];

                for (const auto& k : ch.Keys) {
                    lo = std::min(lo, k.Value);
                    hi = std::max(hi, k.Value);
                }
            }

            if (lo <= hi) {
                const float pad = std::max(0.5f, (hi - lo) * 0.15f);
                lo -= pad; hi += pad;

                m_CurveCenter = (lo + hi) * 0.5f;
                m_CurvePixelsPerUnit = std::clamp(
                    (areaBottom - areaTop) / std::max(0.0001f, hi - lo),
                    0.01f, 10000.0f);
            }
            else {
                // Sem key nenhuma: uma escala neutra e melhor que herdar a
                // anterior, que pode estar em graus quando isto e metros.
                m_CurveCenter = 0.0f;
                m_CurvePixelsPerUnit = 8.0f;
            }

            m_CurveFitPending = false;
        }

        auto valueToY = [&](float v) {
            return midY - (v - m_CurveCenter) * m_CurvePixelsPerUnit;
            };
        auto yToValue = [&](float y) {
            return m_CurveCenter - (y - midY) / m_CurvePixelsPerUnit;
            };

        // ── ZOOM VERTICAL ────────────────────────────────────────────────────
        //
        // Roda sem modificador. No dope sheet ela rola as lanes; aqui nao ha
        // lanes, e o eixo do valor e justamente o que precisa de escala propria
        // — rotacao vive nas dezenas de graus, translacao nos decimos de metro.
        //
        // Ancorada no cursor, pelo mesmo motivo do zoom horizontal: o valor sob
        // o mouse continua sob o mouse.
        {
            const ImGuiIO& io = ImGui::GetIO();

            if (timelineHovered && io.MouseWheel != 0.0f && !io.KeyCtrl && !io.KeyShift) {
                const float valueUnderMouse = yToValue(io.MousePos.y);

                m_CurvePixelsPerUnit = std::clamp(
                    m_CurvePixelsPerUnit * std::pow(1.15f, io.MouseWheel), 0.01f, 10000.0f);

                // Reposiciona o centro para o valor sob o cursor nao sair do
                // lugar com a escala nova.
                m_CurveCenter = valueUnderMouse + (midY - io.MousePos.y) / m_CurvePixelsPerUnit;
            }
        }

        dl->PushClipRect(ImVec2(origin.x, areaTop),
            ImVec2(origin.x + size.x, areaBottom), true);

        // ── GRADE DE VALOR ───────────────────────────────────────────────────
        const float step = NiceStep(60.0f / m_CurvePixelsPerUnit);
        const float vTop = yToValue(areaTop);
        const float vBottom = yToValue(areaBottom);

        for (float v = std::floor(vBottom / step) * step; v <= vTop; v += step) {
            const float y = valueToY(v);
            if (y < areaTop - 1.0f || y > areaBottom + 1.0f) continue;

            const bool zero = (std::abs(v) < step * 0.01f);

            dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y),
                zero ? IM_COL32(120, 120, 130, 180) : IM_COL32(60, 60, 66, 160),
                zero ? 1.6f : 1.0f);

            char lbl[32];
            std::snprintf(lbl, sizeof(lbl), "%.3g", v);
            dl->AddText(ImVec2(origin.x + 4.0f, y - ImGui::GetTextLineHeight() - 1.0f),
                IM_COL32(150, 150, 160, 200), lbl);
        }

        if (curves.empty()) {
            dl->AddText(ImVec2(origin.x + 16.0f, midY - 8.0f),
                IM_COL32(160, 160, 170, 255),
                "Selecione um osso ou controle para ver as curvas dele.");
            dl->PopClipRect();
            return;
        }

        // ── AS CURVAS ────────────────────────────────────────────────────────
        //
        // Amostradas a cada 3px com o MESMO `SequencerPlayer::Interpolate` que
        // o player usa. E por isso que o grafico nao pode divergir do que toca:
        // nao ha uma segunda implementacao da curva para ficar desatualizada.
        const int   sampleStepPx = 3;
        const float x0 = origin.x;
        const float x1 = origin.x + size.x;

        for (const auto& c : curves) {
            const auto* b = m_Asset.GetBinding(c.Binding);
            if (!b) continue;

            const auto& tr = b->Tracks[c.Track];
            const auto& ch = tr.Sections[c.Section].Channels[c.Channel];
            if (ch.Keys.empty()) continue;

            const ImU32 col = CurveColor(ch.Component);
            const float thick = tr.Muted ? 1.0f : 2.0f;

            const ImU32 lineCol = tr.Muted
                ? (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(70) << IM_COL32_A_SHIFT)
                : col;

            std::vector<ImVec2> pts;
            pts.reserve(static_cast<std::size_t>((x1 - x0) / sampleStepPx) + 4);

            for (float x = x0; x <= x1; x += sampleStepPx) {
                const float f = xToFrame(x);

                const SequencerKey* l = nullptr;
                const SequencerKey* r = nullptr;
                if (!ch.FindBracketingKeys(f, l, r)) continue;

                pts.push_back(ImVec2(x, valueToY(SequencerPlayer::Interpolate(*l, *r, f))));
            }

            if (pts.size() >= 2)
                dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), lineCol, 0, thick);

            // ── AS KEYS ──────────────────────────────────────────────────────
            for (int ki = 0; ki < static_cast<int>(ch.Keys.size()); ++ki) {
                const auto& k = ch.Keys[ki];

                const float kx = frameToX(k.Frame);
                const float ky = valueToY(k.Value);

                if (kx < x0 - 20.0f || kx > x1 + 20.0f) continue;

                const KeyRef ref{ c.Binding, c.Track, c.Section, c.Channel, ki };

                if (boxActive && !tr.Locked &&
                    kx >= boxRect.Min.x && kx <= boxRect.Max.x &&
                    ky >= boxRect.Min.y && ky <= boxRect.Max.y) {
                    m_BoxHits.push_back(ref);
                }

                const bool isPrimary = (m_SelectedBinding == c.Binding &&
                    m_SelectedTrack == c.Track &&
                    m_SelectedSection == c.Section &&
                    m_SelectedChannel == c.Channel &&
                    m_SelectedKey == ki);
                const bool inSel = IsKeySelected(ref);

                const float r2 = 5.0f;

                ImGui::SetCursorScreenPos(ImVec2(kx - r2, ky - r2));
                char kid[80];
                std::snprintf(kid, sizeof(kid), "ck_%d_%d_%d_%d_%d",
                    c.Binding, c.Track, c.Section, c.Channel, ki);
                ImGui::InvisibleButton(kid, ImVec2(r2 * 2, r2 * 2));

                if (ImGui::IsItemActivated()) {
                    const ImGuiIO& io = ImGui::GetIO();

                    if (io.KeyCtrl || io.KeyShift) ToggleKeySelection(ref);
                    else if (!inSel)               SetSingleKeySelection(ref);

                    m_SelectedBinding = c.Binding;
                    m_SelectedTrack = c.Track;
                    m_SelectedSection = c.Section;
                    m_SelectedChannel = c.Channel;
                    m_SelectedKey = ki;

                    if (IsKeySelected(ref) && !tr.Locked) {
                        m_DraggingKey = true;
                        m_DragStartMouseX = ImGui::GetMousePos().x;
                        m_DragStartMouseY = ImGui::GetMousePos().y;
                        BeginKeyDrag();
                    }
                }

                const ImU32 fill = isPrimary ? IM_COL32(255, 200, 80, 255)
                    : inSel ? IM_COL32(120, 190, 255, 255)
                    : col;

                dl->AddRectFilled(ImVec2(kx - r2, ky - r2), ImVec2(kx + r2, ky + r2), fill, 1.5f);
                dl->AddRect(ImVec2(kx - r2, ky - r2), ImVec2(kx + r2, ky + r2),
                    IM_COL32(0, 0, 0, 220), 1.5f);

                // ── ALCAS DE TANGENTE ────────────────────────────────────────
                //
                // So nas keys SELECIONADAS e so no modo Bezier. Desenhar alca em
                // toda key transformaria a curva num arbusto — e e o padrao de
                // qualquer editor de curvas pelo mesmo motivo.
                if (!inSel || k.Interp != SequencerInterp::Bezier) continue;
                if (tr.Locked) continue;

                const bool hasPrev = (ki > 0);
                const bool hasNext = (ki + 1 < static_cast<int>(ch.Keys.size()));

                auto handle = [&](int side) {
                    // side 1 = sai desta key (usa o trecho ate a proxima)
                    // side 2 = chega nesta key (usa o trecho desde a anterior)
                    const float span = (side == 1)
                        ? (hasNext ? ch.Keys[ki + 1].Frame - k.Frame : 0.0f)
                        : (hasPrev ? k.Frame - ch.Keys[ki - 1].Frame : 0.0f);

                    if (span <= 0.0f) return;

                    const float w = (side == 1) ? k.TangentOutWeight : k.TangentInWeight;
                    const float tv = (side == 1) ? k.TangentOut : -k.TangentIn;

                    const float hf = (side == 1) ? k.Frame + w * span : k.Frame - w * span;
                    const float hx = frameToX(hf);
                    const float hy = valueToY(k.Value + tv);

                    dl->AddLine(ImVec2(kx, ky), ImVec2(hx, hy),
                        IM_COL32(230, 200, 120, 200), 1.4f);
                    dl->AddCircleFilled(ImVec2(hx, hy), 4.0f, IM_COL32(255, 210, 120, 255));
                    dl->AddCircle(ImVec2(hx, hy), 4.0f, IM_COL32(0, 0, 0, 200));

                    ImGui::SetCursorScreenPos(ImVec2(hx - 6.0f, hy - 6.0f));
                    char hid[90];
                    std::snprintf(hid, sizeof(hid), "ch_%d_%d_%d_%d_%d_%d",
                        c.Binding, c.Track, c.Section, c.Channel, ki, side);
                    ImGui::InvisibleButton(hid, ImVec2(12.0f, 12.0f));

                    if (ImGui::IsItemActivated()) {
                        m_DragHandleSide = side;
                        m_DragHandleKey = ref;
                    }
                    };

                handle(1);
                handle(2);
            }
        }

        // ── ARRASTO DA ALCA ──────────────────────────────────────────────────
        //
        // Fora do laco: a key da alca pode ter saido da vista no meio do gesto
        // (o usuario arrasta para fora da janela de zoom), e o arrasto tem de
        // continuar valendo.
        if (m_DragHandleSide != 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (SequencerKey* k = ResolveKeyRef(m_DragHandleKey)) {
                    const auto* b = m_Asset.GetBinding(m_DragHandleKey.Binding);
                    const auto& ch = b->Tracks[m_DragHandleKey.Track]
                        .Sections[m_DragHandleKey.Section]
                        .Channels[m_DragHandleKey.Channel];

                    const int ki = m_DragHandleKey.Key;
                    const bool hasPrev = (ki > 0);
                    const bool hasNext = (ki + 1 < static_cast<int>(ch.Keys.size()));

                    const float span = (m_DragHandleSide == 1)
                        ? (hasNext ? ch.Keys[ki + 1].Frame - k->Frame : 0.0f)
                        : (hasPrev ? k->Frame - ch.Keys[ki - 1].Frame : 0.0f);

                    if (span > 0.0f) {
                        const ImVec2 mp = ImGui::GetMousePos();

                        const float df = std::abs(xToFrame(mp.x) - k->Frame);
                        const float dv = yToValue(mp.y) - k->Value;

                        // Peso clampado em [0.01, 0.99]: sao os limites que
                        // mantem x(u) monotonico. Fora deles a curva dobraria
                        // no tempo — dois valores para o mesmo frame.
                        const float w = std::clamp(df / span, 0.01f, 0.99f);

                        if (m_DragHandleSide == 1) {
                            k->TangentOutWeight = w;
                            k->TangentOut = dv;
                        }
                        else {
                            k->TangentInWeight = w;
                            k->TangentIn = -dv;
                        }
                    }
                }
            }
            else {
                m_DragHandleSide = 0;
            }
        }

        dl->PopClipRect();
    }

    // ============================================================
    // Selecao em caixa
    // ============================================================

    void SequencerWindow::ApplyBoxSelection() {
        // Caixa vazia com clique simples: o gesto foi "clicar no vazio", que em
        // qualquer editor quer dizer desmarcar. So no modo Replace — com Shift
        // ou Ctrl o usuario esta somando/tirando, e zerar seria o contrario do
        // pedido.
        if (m_BoxMode == BoxMode::Replace)
            ClearKeySelection();

        for (const auto& r : m_BoxHits) {
            if (m_BoxMode == BoxMode::Remove) {
                for (std::size_t i = 0; i < m_SelectedKeys.size(); ++i) {
                    if (m_SelectedKeys[i] == r) {
                        m_SelectedKeys.erase(m_SelectedKeys.begin() + i);
                        break;
                    }
                }
                continue;
            }

            if (!IsKeySelected(r))
                m_SelectedKeys.push_back(r);
        }

        // A primaria (a que o painel edita) passa a ser a primeira da caixa.
        // Sem isto o painel continuaria mostrando uma key que pode nem estar
        // mais no grupo.
        if (!m_BoxHits.empty() && m_BoxMode != BoxMode::Remove) {
            const KeyRef& f = m_BoxHits.front();
            m_SelectedBinding = f.Binding;
            m_SelectedTrack = f.Track;
            m_SelectedSection = f.Section;
            m_SelectedChannel = f.Channel;
            m_SelectedKey = f.Key;
        }
        else if (m_SelectedKeys.empty()) {
            m_SelectedKey = -1;
        }
    }

    void SequencerWindow::SelectAllKeysInChannel(int bindingIndex, int trackIndex,
        int sectionIndex, int channelIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;

        auto& tr = b->Tracks[trackIndex];
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tr.Sections.size())) return;

        auto& sec = tr.Sections[sectionIndex];
        if (channelIndex < 0 || channelIndex >= static_cast<int>(sec.Channels.size())) return;

        ClearKeySelection();

        const auto& ch = sec.Channels[channelIndex];
        for (int ki = 0; ki < static_cast<int>(ch.Keys.size()); ++ki)
            m_SelectedKeys.push_back(KeyRef{ bindingIndex, trackIndex, sectionIndex,
                                             channelIndex, ki });

        if (!m_SelectedKeys.empty()) {
            m_SelectedBinding = bindingIndex;
            m_SelectedTrack = trackIndex;
            m_SelectedSection = sectionIndex;
            m_SelectedChannel = channelIndex;
            m_SelectedKey = 0;
        }
    }

    void SequencerWindow::SelectAllKeysInTrack(int bindingIndex, int trackIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;

        ClearKeySelection();

        auto& tr = b->Tracks[trackIndex];

        for (int si = 0; si < static_cast<int>(tr.Sections.size()); ++si)
            for (int ci = 0; ci < static_cast<int>(tr.Sections[si].Channels.size()); ++ci)
                for (int ki = 0; ki < static_cast<int>(tr.Sections[si].Channels[ci].Keys.size()); ++ki)
                    m_SelectedKeys.push_back(KeyRef{ bindingIndex, trackIndex, si, ci, ki });

        if (!m_SelectedKeys.empty()) {
            const KeyRef& f = m_SelectedKeys.front();
            m_SelectedBinding = f.Binding;
            m_SelectedTrack = f.Track;
            m_SelectedSection = f.Section;
            m_SelectedChannel = f.Channel;
            m_SelectedKey = f.Key;
        }
    }

    void SequencerWindow::DeleteSelectedKeys() {
        if (m_SelectedKeys.empty()) return;

        // ── ORDEM DECRESCENTE, E NAO A ORDEM DE SELECAO ──────────────────────
        //
        // RemoveKey desloca para tras todo indice DEPOIS do removido, no mesmo
        // canal. Apagando na ordem em que foram selecionadas, cada remocao
        // invalida as seguintes — e o resultado nao e um erro visivel, e o
        // apagamento das keys erradas. De tras para frente, nenhum indice ainda
        // pendente se move.
        std::vector<KeyRef> ordered = m_SelectedKeys;
        std::sort(ordered.begin(), ordered.end(), [](const KeyRef& a, const KeyRef& b) {
            if (a.Binding != b.Binding) return a.Binding > b.Binding;
            if (a.Track != b.Track)   return a.Track > b.Track;
            if (a.Section != b.Section) return a.Section > b.Section;
            if (a.Channel != b.Channel) return a.Channel > b.Channel;
            return a.Key > b.Key;
            });

        for (const auto& r : ordered)
            m_Asset.RemoveKey(r.Binding, r.Track, r.Section, r.Channel, r.Key);

        AXE_EDITOR_INFO("Sequencer: {} key(s) apagada(s).", (int)ordered.size());

        m_SelectedKeys.clear();
        m_SelectedKey = -1;
    }

    void SequencerWindow::BeginKeyDrag() {
        m_DragOriginalFrames.clear();
        m_DragOriginalFrames.reserve(m_SelectedKeys.size());

        // ── POR QUE GUARDAR O FRAME ORIGINAL DE CADA UMA ─────────────────────
        //
        // O arrasto e calculado como `original + delta`, sempre a partir do
        // valor de quando o gesto comecou. Somando o delta ao frame ATUAL, o
        // grupo se comprimiria: a cada quadro cada key partiria de um lugar
        // diferente, e as distancias entre elas encolheriam ate virarem uma so.
        m_DragOriginalValues.clear();
        m_DragOriginalValues.reserve(m_SelectedKeys.size());

        for (const auto& r : m_SelectedKeys) {
            const SequencerKey* k = ResolveKeyRef(r);
            m_DragOriginalFrames.push_back(k ? k->Frame : 0.0f);

            // O mesmo raciocinio do frame, no eixo do valor — usado so no modo
            // curva, mas guardado sempre: e uma copia de floats, e um `if` aqui
            // seria mais estado para manter em sincronia do que economia.
            m_DragOriginalValues.push_back(k ? k->Value : 0.0f);
        }
    }

    void SequencerWindow::SortChannelAndRemap(int bindingIndex, int trackIndex,
        int sectionIndex, int channelIndex) {
        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;

        auto& tr = b->Tracks[trackIndex];
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tr.Sections.size())) return;

        auto& sec = tr.Sections[sectionIndex];
        if (channelIndex < 0 || channelIndex >= static_cast<int>(sec.Channels.size())) return;

        auto& ch = sec.Channels[channelIndex];
        const int n = static_cast<int>(ch.Keys.size());
        if (n < 2) return;

        std::vector<int> order(n);
        for (int i = 0; i < n; ++i) order[i] = i;

        // stable_sort, e nao sort: duas keys no MESMO frame (acontece o tempo
        // todo ao arrastar um grupo contra uma parede) tem de manter a ordem
        // relativa, senao a permutacao deixa de ser reproduzivel e a selecao
        // remapeada aponta para a irma.
        std::stable_sort(order.begin(), order.end(), [&ch](int a, int bIdx) {
            return ch.Keys[a].Frame < ch.Keys[bIdx].Frame;
            });

        std::vector<int> newIndexOf(n);
        for (int p = 0; p < n; ++p) newIndexOf[order[p]] = p;

        std::vector<SequencerKey> sorted;
        sorted.reserve(n);
        for (int p = 0; p < n; ++p) sorted.push_back(ch.Keys[order[p]]);
        ch.Keys.swap(sorted);

        for (auto& r : m_SelectedKeys) {
            if (r.Binding == bindingIndex && r.Track == trackIndex &&
                r.Section == sectionIndex && r.Channel == channelIndex &&
                r.Key >= 0 && r.Key < n) {
                r.Key = newIndexOf[r.Key];
            }
        }

        if (m_SelectedBinding == bindingIndex && m_SelectedTrack == trackIndex &&
            m_SelectedSection == sectionIndex && m_SelectedChannel == channelIndex &&
            m_SelectedKey >= 0 && m_SelectedKey < n) {
            m_SelectedKey = newIndexOf[m_SelectedKey];
        }
    }

    // ============================================================
    // Sockets
    // ============================================================

    const SkeletalMeshAsset::Socket* SequencerWindow::FindBindingSocket(
        int bindingIndex, const std::string& socketName) const {
        if (socketName.empty()) return nullptr;

        SkeletalMeshComponent* smc = nullptr;
        if (!GetBindingSkeleton(bindingIndex, &smc)) return nullptr;

        // O Asset, e nao o Skeleton: sockets sao autoria e vivem no `.axeskel`.
        // O Skeleton e reconstruido do FBX a cada Resolve() e o FBX nao tem
        // sockets — procurar la nunca acharia nada. Esta distincao esta escrita
        // no proprio skeletal_mesh_asset.hpp e e facil de errar.
        if (!smc || !smc->Asset) return nullptr;

        return smc->Asset->FindSocket(socketName);
    }

    int SequencerWindow::CreateSocketTrack(int bindingIndex, const std::string& socketName) {
        if (socketName.empty()) return -1;

        if (!FindBindingSocket(bindingIndex, socketName)) {
            AXE_EDITOR_ERROR("Sequencer: socket '{}' nao existe no esqueleto deste "
                "binding.", socketName);
            return -1;
        }

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (!b) return -1;

        // Duas tracks para o mesmo socket disputariam o transform do mesmo
        // objeto anexado, e a ultima avaliada venceria — bug mudo. Devolve a
        // que ja existe.
        for (int i = 0; i < static_cast<int>(b->Tracks.size()); ++i) {
            if (b->Tracks[i].Type == SequencerTrackType::TransformSocket &&
                b->Tracks[i].TargetName == socketName) {
                return i;
            }
        }

        SequencerTrack tr;
        tr.Type = SequencerTrackType::TransformSocket;
        tr.TargetName = socketName;
        tr.TargetType = SequencerTargetType::Socket;

        SequencerSection sec;
        sec.StartFrame = m_Asset.GetFrameRange().Start;
        sec.EndFrame = m_Asset.GetFrameRange().End;
        // Canais VAZIOS, pela mesma razao da track de osso: um canal com key de
        // valor 0 no frame 0 nao e neutro aqui — ele CRAVA o offset em zero. O
        // neutro do socket ja e zero, entao ate daria certo por acidente para
        // translacao, mas Scale zero encolheria a arma ate sumir.
        tr.Sections.push_back(sec);

        const int ti = m_Asset.AddTrack(bindingIndex, tr);
        EnsurePlayerStarted();

        AXE_EDITOR_INFO("Sequencer: track do socket '{}' criada.", socketName);
        return ti;
    }

    // ── PREVIEW DO OBJETO ANEXADO ────────────────────────────────────────────
    //
    // Nao ha uma linha de renderer aqui, e isso e o ponto. O AXE ja sabe prender
    // uma entidade a um socket:
    //
    //   Sequencer   -> Pose -> BuildSkinningMatrices -> BoneGlobals
    //   AnimationWorld::UpdateSocketAttachments -> att._SocketWorld
    //   Scene::GetWorldTransform -> _SocketWorld * transform local
    //
    // O trabalho daqui e so manter, para cada track de socket com objeto
    // escolhido, UMA entidade com MeshComponent + SocketAttachmentComponent, e
    // escrever o offset autorado no TransformComponent dela. Todo o resto ja
    // rodava — inclusive para a arma de verdade que o gameplay anexa.
    //
    // EditorTransientComponent e o que impede essas entidades de vazarem para o
    // `.axescene` e para a Hierarchy Window.
    void SequencerWindow::SyncSocketPreviews() {
        if (!m_Context || !m_Context->ActiveScene) return;

        Scene* scene = m_Context->ActiveScene;
        auto& reg = scene->GetRegistry();

        // Chaves vistas neste frame. O que sobrar no mapa e preview orfa —
        // track apagada, socket removido, objeto desanexado.
        std::vector<std::string> alive;

        const int bindingCount = static_cast<int>(m_Asset.GetBindingCount());

        for (int bi = 0; bi < bindingCount; ++bi) {
            const auto* b = m_Asset.GetBinding(bi);
            if (!b) continue;

            const entt::entity target = ResolveBindingEntity(*b);
            if (target == entt::null) continue;

            for (const auto& tr : b->Tracks) {
                if (tr.Type != SequencerTrackType::TransformSocket) continue;
                if (tr.TargetName.empty()) continue;

                const std::string key = std::to_string(bi) + "|" + tr.TargetName;

                // Track mutada ou sem objeto: a entidade some de verdade, em vez
                // de ficar invisivel. Uma entidade vazia pendurada no registry
                // apareceria em qualquer varredura futura como um objeto sem
                // malha e sem dono.
                if (tr.Muted || tr.AttachedAssetUUID.empty()) continue;

                alive.push_back(key);
                SocketPreview& pv = m_SocketPreviews[key];

                if (pv.Entity == entt::null || !reg.valid(pv.Entity)) {
                    pv.Entity = scene->CreateEntity("SeqSocketPreview_" + tr.TargetName);
                    pv.AssetUUID.clear();
                    reg.emplace_or_replace<EditorTransientComponent>(
                        pv.Entity, EditorTransientComponent{ "Sequencer" });
                }

                // Anexo ao socket. Reescrito sempre porque o nome do socket da
                // track pode ter mudado, e o cache `_ResolvedFor` la dentro so
                // se invalida quando o nome muda — escrever o mesmo valor e
                // barato e nao invalida nada.
                auto& att = reg.get_or_emplace<SocketAttachmentComponent>(pv.Entity);
                att.Target = target;
                att.SocketName = tr.TargetName;

                // Recarrega SO quando o UUID muda: ResolveByUUID le do disco, e
                // fazer isso por frame transformaria o preview num leitor de FBX.
                if (pv.AssetUUID != tr.AttachedAssetUUID) {
                    pv.AssetUUID = tr.AttachedAssetUUID;

                    reg.remove<MeshComponent>(pv.Entity);
                    reg.remove<SkeletalMeshComponent>(pv.Entity);

                    const AssetRecord* rec =
                        AssetDatabase::Get().GetByUUID(tr.AttachedAssetUUID);

                    if (rec && rec->Type == AssetType::SkeletalMesh) {
                        // Personagem preso a socket (uma mao segurando outra
                        // coisa animada). Fica na bind pose: dar-lhe animacao
                        // propria e assunto de um binding proprio, nao deste.
                        auto asset = SkeletalMeshAsset::LoadFromFile(rec->FilePath);
                        if (asset && asset->Resolve()) {
                            auto& smc = reg.emplace<SkeletalMeshComponent>(pv.Entity);
                            smc.Asset = asset;
                            smc.Data = asset->GetMesh();
                            smc.AssetUUID = tr.AttachedAssetUUID;
                            smc.Clips = asset->GetClips();
                        }
                        else {
                            AXE_EDITOR_ERROR("Sequencer: falha ao resolver o skeletal "
                                "mesh anexado ao socket '{}'.", tr.TargetName);
                        }
                    }
                    else {
                        auto& mc = reg.emplace<MeshComponent>(pv.Entity);
                        mc.AssetUUID = tr.AttachedAssetUUID;
                        mc.Data = MeshFactory::ResolveByUUID(tr.AttachedAssetUUID);

                        if (!mc.Data) {
                            AXE_EDITOR_ERROR("Sequencer: nao consegui carregar a malha "
                                "anexada ao socket '{}'.", tr.TargetName);
                        }
                    }

                    AXE_EDITOR_INFO("Sequencer: socket '{}' agora segura '{}'.",
                        tr.TargetName, rec ? rec->Name : std::string("(desconhecido)"));
                }

                // Material so para a malha estatica, e so se ela nao tiver um.
                // O caminho deferido nao desenha sem material — o mesmo motivo
                // que o preview de socket do Animation Editor ja documenta.
                if (reg.all_of<MeshComponent>(pv.Entity) &&
                    !reg.all_of<MaterialComponent>(pv.Entity)) {
                    auto m = std::make_shared<Material>(nullptr, "SeqSocketPreview");
                    m->UsePBR = true;
                    m->Metallic = 0.0f;
                    m->Roughness = 0.5f;
                    m->Color = glm::vec4(0.80f, 0.80f, 0.84f, 1.0f);
                    reg.emplace<MaterialComponent>(pv.Entity, m);
                }

                // ── O OFFSET AUTORADO ────────────────────────────────────────
                //
                // Comeca NEUTRO todo frame: sem key, o objeto fica exatamente
                // onde o socket manda. Acumular o valor do frame anterior faria
                // apagar uma key nao ter efeito visivel.
                auto& tc = reg.get_or_emplace<TransformComponent>(pv.Entity);
                tc.Data.Position = glm::vec3(0.0f);
                tc.Data.Rotation = glm::vec3(0.0f);
                tc.Data.Scale = glm::vec3(1.0f);

                for (const auto& s : m_EffectiveSamples) {
                    if (s.BindingIndex != bi) continue;
                    if (s.TargetType != SequencerTargetType::Socket) continue;
                    if (s.TargetName != tr.TargetName) continue;

                    switch (s.Component) {
                    case SequencerChannelComponent::X:      tc.Data.Position.x = s.Value; break;
                    case SequencerChannelComponent::Y:      tc.Data.Position.y = s.Value; break;
                    case SequencerChannelComponent::Z:      tc.Data.Position.z = s.Value; break;
                        // GRAUS na curva, RADIANOS no Transform.
                        //
                        // `Transform::GetMatrix` faz `glm::quat(Rotation)`, que
                        // interpreta radianos. Todo o resto do Sequencer — painel
                        // de key, WriteComponent, as curvas de osso — trabalha em
                        // graus, que e o que o animador digita. A conversao
                        // acontece so aqui, na fronteira.
                    case SequencerChannelComponent::RotX:   tc.Data.Rotation.x = glm::radians(s.Value); break;
                    case SequencerChannelComponent::RotY:   tc.Data.Rotation.y = glm::radians(s.Value); break;
                    case SequencerChannelComponent::RotZ:   tc.Data.Rotation.z = glm::radians(s.Value); break;
                    case SequencerChannelComponent::ScaleX: tc.Data.Scale.x = s.Value; break;
                    case SequencerChannelComponent::ScaleY: tc.Data.Scale.y = s.Value; break;
                    case SequencerChannelComponent::ScaleZ: tc.Data.Scale.z = s.Value; break;
                    }
                }
            }
        }

        // Varre o mapa e mata o que nao apareceu neste frame.
        for (auto it = m_SocketPreviews.begin(); it != m_SocketPreviews.end(); ) {
            if (std::find(alive.begin(), alive.end(), it->first) != alive.end()) {
                ++it;
                continue;
            }
            if (it->second.Entity != entt::null && reg.valid(it->second.Entity))
                scene->DestroyEntity(it->second.Entity);
            it = m_SocketPreviews.erase(it);
        }
    }

    void SequencerWindow::ClearSocketPreviews() {
        if (m_SocketPreviews.empty()) return;

        if (m_Context && m_Context->ActiveScene) {
            Scene* scene = m_Context->ActiveScene;
            auto& reg = scene->GetRegistry();

            for (auto& [k, pv] : m_SocketPreviews) {
                if (pv.Entity != entt::null && reg.valid(pv.Entity))
                    scene->DestroyEntity(pv.Entity);
            }

            // Rede de seguranca: qualquer entidade transiente NOSSA que tenha
            // escapado do mapa (cena trocada com a janela aberta, crash de um
            // frame anterior) morre aqui. Sem isto ela ficaria na cena sem
            // aparecer na hierarquia — invisivel e imortal.
            std::vector<entt::entity> orphans;
            for (auto e : reg.view<EditorTransientComponent>()) {
                if (reg.get<EditorTransientComponent>(e).Owner == "Sequencer")
                    orphans.push_back(e);
            }
            for (auto e : orphans)
                if (reg.valid(e)) scene->DestroyEntity(e);
        }

        m_SocketPreviews.clear();
    }

    void SequencerWindow::EnsurePlayerStarted() {
        if (!m_PlayerStarted) {
            m_Player.OnStart(m_Asset);
            m_PlayerStarted = true;
        }
    }

    void SequencerWindow::StopPlayer() {
        if (!m_PlayerStarted) return;

        m_Player.OnStop();
        m_PlayerStarted = false;
        m_WorkPoses.clear();

        // Sem player nao ha EvaluateAndApply, e portanto ninguem reconstroi a
        // lista efetiva. Deixa-la para tras faria a barra de status do outliner
        // reportar os samples de uma sessao que acabou.
        m_EffectiveSamples.clear();
        ClearPendingEdits();

        // Devolve a pose ao AnimationWorld. `PreviewInEditor` nao e mais tocado
        // aqui: ele nunca foi o gate certo (so decide se o TEMPO avanca) e mexer
        // nele apagava a escolha do usuario no Inspector.
        ReleasePoseOverride();
    }

} // namespace axe