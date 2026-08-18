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
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace axe {

    // ============================================================
    // ctor / dtor
    // ============================================================

    SequencerWindow::SequencerWindow() = default;

    SequencerWindow::~SequencerWindow() {
        StopPlayer();
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
        if (m_PlayerStarted) {
            // O ASSET e a fonte da verdade no editor; a copia viva do player
            // segue. Sincronizar aqui, uma vez por frame, elimina a classe
            // inteira de bugs "editei e o sample nao viu" — em vez de lembrar de
            // chamar sync em cada um dos ~8 pontos que mutam o asset (add key,
            // drag, delete, interp, valor, add/remove track, add/remove binding).
            // O custo e copiar alguns vetores pequenos por frame.
            m_Player.SyncFrom(m_Asset);
            EvaluateAndApply();
        }

        ImGui::SetNextWindowSize(ImVec2(1100.0f, 520.0f), ImGuiCond_FirstUseEver);
        bool open = m_IsOpen;
        if (ImGui::Begin("Sequencer", &open)) {
            DrawToolbar();

            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            float  transportHeight = 42.0f;
            float  mainHeight = contentSize.y - transportHeight;

            // Outliner (esquerda).
            ImGui::BeginChild("SequencerOutliner",
                ImVec2(m_OutlinerWidth, mainHeight), true);
            DrawOutliner();
            ImGui::EndChild();

            ImGui::SameLine();

            // Timeline (direita).
            ImGui::BeginChild("SequencerTimeline",
                ImVec2(0, mainHeight), true);
            DrawTimeline();
            ImGui::EndChild();

            DrawTransportBar();

            // Popups por ultimo: precisam estar no mesmo escopo de janela em que
            // o botao que os abriu vive, senao o ImGui nao acha o ID.
            DrawAddBindingPopup();
            if (m_AddTrackForBinding >= 0)
                DrawAddTrackPopup(m_AddTrackForBinding);
            if (m_AddClipForBinding >= 0)
                DrawAddClipPopup(m_AddClipForBinding);
        }
        ImGui::End();

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

        ImGui::TextDisabled("Entidades com SkeletalMeshComponent");
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
        auto view = reg.view<SkeletalMeshComponent>();

        for (auto entity : view) {
            auto* nm = reg.try_get<NameComponent>(entity);
            const std::string name = nm ? nm->Name : std::string("(sem nome)");

            if (m_PickerFilter[0] != '\0' &&
                name.find(m_PickerFilter) == std::string::npos)
                continue;

            auto& smc = view.get<SkeletalMeshComponent>(entity);
            const Skeleton* sk = smc.GetSkeleton();

            ImGui::PushID((int)entity);

            char label[192];
            std::snprintf(label, sizeof(label), ICON_PERSON " %s%s",
                name.c_str(),
                sk ? "" : "  (sem esqueleto carregado)");

            if (ImGui::Selectable(label, false, sk ? 0 : ImGuiSelectableFlags_Disabled)) {
                int idx = CreateBindingForEntity(entity);
                if (idx >= 0) {
                    m_SelectedBinding = idx;
                    m_SelectedTrack = m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
                }
                ImGui::CloseCurrentPopup();
            }

            if (sk && ImGui::IsItemHovered())
                ImGui::SetTooltip("%d ossos", (int)sk->GetBoneCount());

            ImGui::PopID();
            ++shown;
        }

        if (shown == 0)
            ImGui::TextDisabled("Nenhuma entidade com esqueleto na cena.");

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

    // ============================================================
    // Toolbar (com icones Font Awesome via ui::IconButton / AccentButton)
    // ============================================================

    void SequencerWindow::DrawToolbar() {
        // Save
        if (ui::IconButton(ICON_SAVE, "Salvar .axeseq (Ctrl+S)", ui::Accent::Primary)) {
            std::string path = m_LastLoadedPath.empty()
                ? "Assets/sequencer.axeseq" : m_LastLoadedPath;
            if (m_Asset.SaveToFile(path)) m_LastLoadedPath = path;
        }

        ImGui::SameLine();
        if (ui::IconButton(ICON_FOLDER_OPEN, "Carregar .axeseq")) {
            std::string path = m_LastLoadedPath.empty()
                ? "Assets/sequencer.axeseq" : m_LastLoadedPath;
            if (m_Asset.LoadFromFile(path)) {
                m_LastLoadedPath = path;
                StopPlayer();
            }
        }

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
        ImGui::Separator();
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
            const int samples = static_cast<int>(m_Player.GetLastSamples().size());

            const bool healthy = (bindings > 0 && m_AppliedBindings == bindings);

            ImGui::TextColored(healthy ? ImVec4(0.4f, 0.85f, 0.6f, 1.0f)
                : ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                ICON_CIRCLE_NODES " %d/%d binding(s) aplicado(s)",
                m_AppliedBindings, bindings);

            const int clips = static_cast<int>(m_Player.GetLastClipSamples().size());

            ImGui::SameLine();
            ImGui::TextDisabled("| %d sample(s) | %d clipe(s) | %d fps",
                samples, clips, m_Asset.GetFps());

            if (bindings > 0 && m_AppliedBindings == 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
                    "A entidade do binding nao foi encontrada na cena.");
            }
            else if (m_AppliedBindings > 0 && samples == 0 && clips == 0) {
                ImGui::TextDisabled("Sem clipe e sem key — a pose fica a de repouso.");
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
                const auto& ss = m_Player.GetLastSamples();
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

        for (int bi = 0; bi < bindingCount; ++bi) {
            DrawBindingNode(bi);
        }

        DrawSelectedKeyPanel();
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
                    ImGui::TextDisabled(ICON_BONE " %s — %d ossos",
                        b->EntityName.c_str(), (int)sk->GetBoneCount());
                }
            }

            // + Track — abre o picker de osso DESTE binding.
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
            // - Binding (icone trash, vermelho)
            if (ui::IconButton(ICON_TRASH, "Remover binding", ui::Accent::Danger)) {
                m_Asset.RemoveBinding(bindingIndex);
                m_SelectedBinding = m_SelectedTrack = -1;
                ImGui::TreePop();
                ImGui::PopID();
                return;
            }

            for (int ti = 0; ti < static_cast<int>(b->Tracks.size()); ++ti) {
                DrawTrackNode(bindingIndex, ti);
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
        ImGui::PushID(trackIndex | (bindingIndex << 16));

        const bool isClipTrack = (tr.Type == SequencerTrackType::AnimationClip);

        char label[256];
        if (isClipTrack) {
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
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_OpenOnArrow |
            (selected ? ImGuiTreeNodeFlags_Selected : 0);

        bool open = ImGui::TreeNodeEx(label, flags);
        if (ImGui::IsItemClicked()) {
            m_SelectedBinding = bindingIndex;
            m_SelectedTrack = trackIndex;
            m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
        }

        // Mute / Lock toggle buttons.
        ImGui::SameLine();
        if (ui::ToggleButton(ICON_EYE_SLASH " M", tr.Muted, "Mute track")) tr.Muted = !tr.Muted;
        ImGui::SameLine();
        if (ui::ToggleButton(tr.Locked ? ICON_LOCK " L" : ICON_UNLOCK " L", tr.Locked,
            tr.Locked ? "Locked" : "Lock")) tr.Locked = !tr.Locked;

        // Remover track. Vale para os dois tipos: uma track de clipe que aponta
        // para um clipe que sumiu so tem um uso — sair da lista.
        ImGui::SameLine();
        if (ui::IconButton(ICON_TRASH, "Remover track", ui::Accent::Danger)) {
            m_Asset.RemoveTrack(bindingIndex, trackIndex);
            if (m_SelectedBinding == bindingIndex && m_SelectedTrack == trackIndex)
                m_SelectedTrack = m_SelectedSection = m_SelectedChannel = m_SelectedKey = -1;
            if (open) ImGui::TreePop();
            ImGui::PopID();
            return;
        }

        if (open) {
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
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::DragInt("Inicio", &startF, 1.0f, 0, 100000)) {
                const int len = sec.EndFrame - sec.StartFrame;
                sec.StartFrame = startF;
                sec.EndFrame = startF + len;   // arrasta a section inteira
            }

            ImGui::SameLine();
            int offset = sec.ClipOffset;
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::DragInt("Offset", &offset, 1.0f, -100000, 100000))
                sec.ClipOffset = offset;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Frame do CLIPE em que a section comeca a ler.");

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
            if (ImGui::MenuItem(ICON_PLUS " Add Channel X")) {
                SequencerChannel ch; ch.Component = SequencerChannelComponent::X;
                m_Asset.AddChannel(bindingIndex, trackIndex, sectionIndex, ch);
            }
            if (ImGui::MenuItem(ICON_PLUS " Add Channel Y")) {
                SequencerChannel ch; ch.Component = SequencerChannelComponent::Y;
                m_Asset.AddChannel(bindingIndex, trackIndex, sectionIndex, ch);
            }
            if (ImGui::MenuItem(ICON_PLUS " Add Channel Z")) {
                SequencerChannel ch; ch.Component = SequencerChannelComponent::Z;
                m_Asset.AddChannel(bindingIndex, trackIndex, sectionIndex, ch);
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

        char label[64];
        std::snprintf(label, sizeof(label), "%s (%d keys)",
            SequencerChannelComponentToString(ch.Component),
            static_cast<int>(ch.Keys.size()));

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

        // Inline: Capture from Viewport (icone camera).
        ImGui::SameLine();
        if (ui::IconButton(ICON_CAMERA, "Capturar valor do bone no viewport")) {
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

        // Interp dropdown.
        ImGui::SetNextItemWidth(-1);
        int interp = static_cast<int>(k.Interp);
        const char* interpItems = "Step\0Linear\0CubicEaseIn\0CubicEaseOut\0Bezier\0";
        if (ImGui::Combo("Interp", &interp, interpItems)) {
            k.Interp = static_cast<SequencerInterp>(interp);
        }

        if (k.Interp == SequencerInterp::Bezier) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputFloat("Tangent In", &k.TangentIn, 0.1f, 1.0f, "%.3f");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputFloat("Tangent Out", &k.TangentOut, 0.1f, 1.0f, "%.3f");
        }

        ImGui::Spacing();
        if (ui::AccentButton(ICON_CAMERA " Capture from Viewport", ui::Accent::Primary)) {
            float v = 0.0f;
            if (CaptureBoneValue(m_SelectedBinding, tr.TargetName, ch.Component, v))
                k.Value = v;
        }
        ImGui::SameLine();
        if (ui::AccentButton(ICON_TRASH " Delete", ui::Accent::Danger)) {
            m_Asset.RemoveKey(m_SelectedBinding, m_SelectedTrack,
                m_SelectedSection, m_SelectedChannel, m_SelectedKey);
            m_SelectedKey = -1;
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
        const int   startFrame = m_Asset.GetFrameRange().Start;
        const int   endFrame = m_Asset.GetFrameRange().End;
        const int   totalFrames = std::max(1, endFrame - startFrame);

        // FrameWidth DINAMICO: preenche a largura total da timeline.
        // Padding de 20px a direita pra playhead nao ficar colada na borda.
        const float availableWidth = size.x - 20.0f;
        const float frameWidth = availableWidth / static_cast<float>(totalFrames);

        // Background.
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
            IM_COL32(28, 28, 30, 255));

        // Regua de frames.
        ImRect rulerRect(origin, ImVec2(origin.x + size.x, origin.y + rulerHeight));
        dl->AddRectFilled(rulerRect.Min, rulerRect.Max, IM_COL32(45, 45, 48, 255));

        // Ticks: major a cada 15 frames, minor a cada 5. Se a timeline for curta,
        // mostra major a cada 5 e minor a cada 1.
        int majorStep, minorStep;
        if (totalFrames <= 30) { majorStep = 5;  minorStep = 1; }
        else if (totalFrames <= 90) { majorStep = 15; minorStep = 5; }
        else if (totalFrames <= 300) { majorStep = 30; minorStep = 10; }
        else { majorStep = 60; minorStep = 15; }

        for (int f = startFrame; f <= endFrame; ++f) {
            float x = origin.x + (f - startFrame) * frameWidth;
            if (x > origin.x + size.x) break;

            bool major = ((f - startFrame) % majorStep == 0);
            bool minor = ((f - startFrame) % minorStep == 0);

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

        // Lanes (uma por track de cada binding).
        float laneY = origin.y + rulerHeight + 4.0f;
        int bindingCount = static_cast<int>(m_Asset.GetBindingCount());
        for (int bi = 0; bi < bindingCount; ++bi) {
            SequencerBinding* b = m_Asset.GetBinding(bi);
            if (!b) continue;
            for (int ti = 0; ti < static_cast<int>(b->Tracks.size()); ++ti) {
                auto& tr = b->Tracks[ti];

                ImRect laneRect(ImVec2(origin.x, laneY),
                    ImVec2(origin.x + size.x, laneY + laneHeight));

                bool laneSelected = (m_SelectedBinding == bi && m_SelectedTrack == ti);
                ImU32 laneBg = laneSelected ? IM_COL32(60, 60, 75, 255)
                    : IM_COL32(38, 38, 42, 255);
                dl->AddRectFilled(laneRect.Min, laneRect.Max, laneBg);
                dl->AddLine(laneRect.GetBL(), laneRect.GetBR(),
                    IM_COL32(20, 20, 20, 255));

                char laneLabel[256];
                std::snprintf(laneLabel, sizeof(laneLabel), "%s / %s",
                    b->DisplayName.c_str(), tr.TargetName.c_str());
                dl->AddText(ImVec2(laneRect.Min.x + 6, laneRect.Min.y + 7),
                    IM_COL32(220, 220, 220, 255), laneLabel);

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

                    dl->AddRectFilled(secRect.Min, secRect.Max, secColor);
                    dl->AddRect(secRect.Min, secRect.Max,
                        IM_COL32(255, 255, 255, 180), 1.0f);

                    // Keys dentro da section.
                    for (int ci = 0; ci < static_cast<int>(sec.Channels.size()); ++ci) {
                        auto& ch = sec.Channels[ci];
                        for (int ki = 0; ki < static_cast<int>(ch.Keys.size()); ++ki) {
                            auto& k = ch.Keys[ki];
                            float kx = origin.x + (k.Frame - startFrame) * frameWidth;
                            float ky = laneY + laneHeight * 0.5f;

                            const float r = 7.0f;
                            bool keySelected = (m_SelectedBinding == bi &&
                                m_SelectedTrack == ti &&
                                m_SelectedSection == si &&
                                m_SelectedChannel == ci &&
                                m_SelectedKey == ki);
                            ImU32 keyFill = keySelected ? IM_COL32(255, 200, 80, 255)
                                : IM_COL32(255, 255, 255, 220);

                            // Hit-test.
                            ImGui::SetCursorScreenPos(ImVec2(kx - r, ky - r));
                            char kid[64];
                            std::snprintf(kid, sizeof(kid), "key_%d_%d_%d_%d_%d",
                                bi, ti, si, ci, ki);
                            ImGui::InvisibleButton(kid, ImVec2(r * 2, r * 2));

                            if (ImGui::IsItemActivated()) {
                                m_SelectedBinding = bi;
                                m_SelectedTrack = ti;
                                m_SelectedSection = si;
                                m_SelectedChannel = ci;
                                m_SelectedKey = ki;
                                m_DraggingKey = true;
                                m_DragStartMouseX = ImGui::GetMousePos().x;
                                m_DragOriginalFrame = k.Frame;
                            }
                            if (m_DraggingKey &&
                                m_SelectedBinding == bi && m_SelectedTrack == ti &&
                                m_SelectedSection == si && m_SelectedChannel == ci &&
                                m_SelectedKey == ki) {
                                if (ImGui::IsItemActive()) {
                                    float dx = ImGui::GetMousePos().x - m_DragStartMouseX;
                                    float deltaFrames = dx / frameWidth;
                                    float newFrame = m_DragOriginalFrame + deltaFrames;
                                    if (m_SnapEnabled && m_SnapFrame > 0) {
                                        newFrame = std::round(newFrame / m_SnapFrame) * m_SnapFrame;
                                    }
                                    newFrame = std::clamp(newFrame,
                                        static_cast<float>(m_Asset.GetFrameRange().Start),
                                        static_cast<float>(m_Asset.GetFrameRange().End));
                                    k.Frame = newFrame;
                                }
                                else if (ImGui::IsItemDeactivated()) {
                                    ch.SortKeys();
                                    m_DraggingKey = false;
                                    for (int j = 0; j < static_cast<int>(ch.Keys.size()); ++j) {
                                        if (ch.Keys[j].Frame == k.Frame) {
                                            m_SelectedKey = j;
                                            break;
                                        }
                                    }
                                }
                            }

                            // Right-click: menu de contexto.
                            std::string keyPopup = "key_ctx_" + std::string(kid);
                            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                                ImGui::OpenPopup(keyPopup.c_str());
                            }
                            if (ImGui::BeginPopup(keyPopup.c_str())) {
                                m_SelectedBinding = bi;
                                m_SelectedTrack = ti;
                                m_SelectedSection = si;
                                m_SelectedChannel = ci;
                                m_SelectedKey = ki;
                                if (ImGui::MenuItem(ICON_TRASH " Delete")) {
                                    m_Asset.RemoveKey(bi, ti, si, ci, ki);
                                    m_SelectedKey = -1;
                                }
                                ImGui::SeparatorText("Interpolation");
                                if (ImGui::MenuItem("Step"))          k.Interp = SequencerInterp::Step;
                                if (ImGui::MenuItem("Linear"))        k.Interp = SequencerInterp::Linear;
                                if (ImGui::MenuItem("CubicEaseIn"))   k.Interp = SequencerInterp::CubicEaseIn;
                                if (ImGui::MenuItem("CubicEaseOut"))  k.Interp = SequencerInterp::CubicEaseOut;
                                if (ImGui::MenuItem("Bezier"))       k.Interp = SequencerInterp::Bezier;
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

                laneY += laneHeight + 2;
                if (laneY > origin.y + size.y) break;
            }
            if (laneY > origin.y + size.y) break;
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
                frameF = std::clamp(frameF,
                    static_cast<float>(startFrame),
                    static_cast<float>(endFrame));
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
            m_Player.Scrub(frameF);
            m_Player.SetMode(SequencerPlaybackMode::Scrubbing);
            EnsurePlayerStarted();
            m_Player.Resample();
            EvaluateAndApply();
            m_DraggingPlayhead = true;  // começa drag a partir do click
        }

        // Double-click na lane: adiciona key.
        ImRect lanesRect(ImVec2(origin.x, origin.y + rulerHeight),
            ImVec2(origin.x + size.x, laneY));
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

        // Escreve um componente escalar num BoneTransform LOCAL.
        //
        // Rotacao entra e sai em GRAUS de Euler: e o que o animador digita no painel
        // de key. A conversao para quaternion acontece so aqui, no ultimo momento —
        // guardar quaternion nas curvas tornaria as keys ilegiveis e a interpolacao
        // componente-a-componente sem sentido.
        void WriteComponent(BoneTransform& t, SequencerChannelComponent c, float v) {
            glm::vec3 euler = glm::degrees(glm::eulerAngles(t.Rotation));

            switch (c) {
            case SequencerChannelComponent::X:      t.Translation.x = v; return;
            case SequencerChannelComponent::Y:      t.Translation.y = v; return;
            case SequencerChannelComponent::Z:      t.Translation.z = v; return;
            case SequencerChannelComponent::ScaleX: t.Scale.x = v;       return;
            case SequencerChannelComponent::ScaleY: t.Scale.y = v;       return;
            case SequencerChannelComponent::ScaleZ: t.Scale.z = v;       return;
            case SequencerChannelComponent::RotX:   euler.x = v;         break;
            case SequencerChannelComponent::RotY:   euler.y = v;         break;
            case SequencerChannelComponent::RotZ:   euler.z = v;         break;
            }
            t.Rotation = glm::quat(glm::radians(euler));
        }

        // Rotacao dos TRES eixos de um osso, acumulada antes de virar quaternion.
        //
        // POR QUE ISTO NAO PODE SER FEITO CANAL A CANAL:
        //   `WriteComponent(RotX)` decompoe o quaternion, troca X e RECOMPOE.
        //   `WriteComponent(RotZ)` logo em seguida decompoe DE NOVO — e
        //   `glm::eulerAngles` nao devolve necessariamente o mesmo triplo que
        //   acabou de entrar: a mesma rotacao tem infinitas representacoes em Euler
        //   e a funcao escolhe a faixa canonica (Y em [-90,90], X e Z em
        //   [-180,180]). Quando a escolha muda de ramo, o segundo canal escreve por
        //   cima de um triplo diferente do que o primeiro montou, e a edicao do
        //   primeiro eixo some.
        //
        //   Juntando os tres canais do mesmo osso e recompondo UMA vez, a
        //   decomposicao acontece exatamente uma vez por osso por frame — e sempre
        //   sobre a pose de repouso, que e estavel.
        struct BoneEulerEdit {
            int       BoneIdx = -1;
            glm::vec3 Euler{ 0.0f };
        };

        // Devolve o eixo (0/1/2) se o componente for de rotacao.
        bool RotationAxisOf(SequencerChannelComponent c, int& outAxis) {
            switch (c) {
            case SequencerChannelComponent::RotX: outAxis = 0; return true;
            case SequencerChannelComponent::RotY: outAxis = 1; return true;
            case SequencerChannelComponent::RotZ: outAxis = 2; return true;
            default: return false;
            }
        }

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

    void SequencerWindow::EvaluateAndApply() {
        if (!m_Context || !m_Context->ActiveScene) return;

        auto& reg = m_Context->ActiveScene->GetRegistry();
        const auto& samples = m_Player.GetLastSamples();
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
            if (!skel || !smc) continue;

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
            rotEdits.clear();

            for (const auto& s : samples) {
                if (s.BindingIndex != bi) continue;

                // TransformControl e TransformSocket entram na Fase 3, quando o
                // caminho passar pelo RigHierarchy. Ignorar em silencio aqui e
                // melhor que aplicar como se fosse osso e mover a coisa errada.
                if (s.TargetType != SequencerTargetType::Bone) continue;

                const int boneIdx = skel->FindBone(s.TargetName);
                if (boneIdx < 0 || boneIdx >= (int)pose.Size()) continue;

                int axis = 0;
                if (RotationAxisOf(s.Component, axis)) {
                    BoneEulerEdit* ed = nullptr;
                    for (auto& e : rotEdits)
                        if (e.BoneIdx == boneIdx) { ed = &e; break; }

                    if (!ed) {
                        BoneEulerEdit fresh;
                        fresh.BoneIdx = boneIdx;
                        // Eixos NAO keyados mantem o valor de repouso do osso.
                        fresh.Euler = glm::degrees(glm::eulerAngles(pose[boneIdx].Rotation));
                        rotEdits.push_back(fresh);
                        ed = &rotEdits.back();
                    }

                    ed->Euler[axis] = s.Value;
                    continue;
                }

                WriteComponent(pose[boneIdx], s.Component, s.Value);
            }

            // 2b. Uma unica recomposicao de quaternion por osso rotacionado.
            for (const auto& e : rotEdits)
                pose[e.BoneIdx].Rotation = glm::quat(glm::radians(e.Euler));

            // 3. A janela e a dona da pose desta entidade enquanto estiver aberta.
            //    Sem isto o AnimationWorld reescreve o BonePalette no mesmo frame
            //    e nada do que fizemos acima aparece.
            smc->PoseOverride = true;

            // 4. Pose local -> matrizes de skinning. Aqui a hierarquia e composta:
            //    mover um osso leva os filhos junto, que e o comportamento que
            //    faltava.
            AnimationSampler::BuildSkinningMatrices(
                *skel, pose, smc->BonePalette,
                (smc->ShowSkeleton || smc->_WantsBoneGlobals) ? &smc->BoneGlobals : nullptr);

            m_PaletteStamp[bi] = PaletteStamp(smc->BonePalette);
            ++m_AppliedBindings;
        }

        m_PaletteStolen = m_PaletteStampValid && stolen;
        m_PaletteStampValid = (m_AppliedBindings > 0);
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
        if (!CaptureBoneValue(bindingIndex, tr.TargetName, ch.Component, value)) {
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

        auto* smc = reg.try_get<SkeletalMeshComponent>(entity);
        if (!smc) return -1;

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
        return ti;
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

        // Devolve a pose ao AnimationWorld. `PreviewInEditor` nao e mais tocado
        // aqui: ele nunca foi o gate certo (so decide se o TEMPO avanca) e mexer
        // nele apagava a escolha do usuario no Inspector.
        ReleasePoseOverride();
    }

} // namespace axe