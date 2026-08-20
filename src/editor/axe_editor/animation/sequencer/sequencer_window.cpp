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
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"
#include "editor/axe_editor/asset/asset_picker.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/mesh/mesh_factory.hpp"
// Auditoria estatica do grafo do rig: precisa enxergar RigNode_ItemArray para
// ler os itens de uma lista sem executar o grafo.
#include "axe/animation/rig/rig_nodes.hpp"
#include "axe/log/log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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

        // O pedido de gizmo e reposto TODO FRAME, e depois da avaliacao: a
        // matriz que ele entrega e a do osso NESTA pose, e a pose acabou de ser
        // recalculada. Pedir antes deixaria o gizmo um frame atrasado — visivel
        // como um tremor durante o arrasto.
        UpdateGizmo();

        // Quem aparece neste frame — grupos dobrados e filtro ja resolvidos.
        // UMA vez, antes do outliner e da timeline: os dois leem desta lista, e
        // e isso que garante que a linha no outliner e a lane na timeline
        // concordem sobre o que existe.
        RebuildLayout();

        ImGui::SetNextWindowSize(ImVec2(1100.0f, 520.0f), ImGuiCond_FirstUseEver);
        bool open = m_IsOpen;
        if (ImGui::Begin("Sequencer", &open)) {
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

                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
                    SelectAllKeys();

                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    ClearKeySelection();

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
            if (m_AddTrackForBinding >= 0)
                DrawAddTrackPopup(m_AddTrackForBinding);
            if (m_AddClipForBinding >= 0)
                DrawAddClipPopup(m_AddClipForBinding);
            if (m_AddSocketForBinding >= 0)
                DrawAddSocketPopup(m_AddSocketForBinding);
            if (m_AddControlForBinding >= 0)
                DrawAddControlPopup(m_AddControlForBinding);
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
                "Crie um abaixo — ele passa a existir no .axeskel e vale para\n"
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

        // ── ZOOM ─────────────────────────────────────────────────────────────
        //
        // Os botoes existem apesar de Ctrl+roda ser mais rapido: atalho e coisa
        // que se aprende, e um zoom que so tem atalho e um zoom que metade das
        // pessoas nao descobre que existe. O rotulo com o valor tambem responde
        // "por que a timeline esta assim" sem precisar experimentar.
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
            "T/R/S do viewport escolhem a operacao — e so a operacao ativa vira key.\n"
            "Desligado, o gizmo volta a ser o da entidade selecionada.")) {
            m_GizmoEnabled = !m_GizmoEnabled;
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

            if (!m_SelectedKeys.empty()) {
                ImGui::TextColored(ImVec4(0.47f, 0.75f, 1.0f, 1.0f),
                    "%d key(s) selecionada(s)  —  Del apaga, Esc limpa",
                    (int)m_SelectedKeys.size());
            }

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

        switch (track.Type) {
        case SequencerTrackType::AnimationClip:    return TrackGroup::Clip;
        case SequencerTrackType::TransformSocket:  return TrackGroup::Socket;
        case SequencerTrackType::TransformControl: return TrackGroup::Control;
        case SequencerTrackType::TransformBone:    return TrackGroup::Bone;
        default:                                   return TrackGroup::Other;
        }
    }

    const char* SequencerWindow::GroupLabel(TrackGroup g) {
        switch (g) {
        case TrackGroup::Clip:    return "Animacoes";
        case TrackGroup::Socket:  return "Sockets";
        case TrackGroup::Control: return "Controles";
        case TrackGroup::Bone:    return "Ossos";
        default:                  return "Outras";
        }
    }

    const char* SequencerWindow::GroupIcon(TrackGroup g) {
        switch (g) {
        case TrackGroup::Clip:    return ICON_FILM;
        case TrackGroup::Socket:  return ICON_LINK;
        case TrackGroup::Control: return ICON_CIRCLE_NODES;
        case TrackGroup::Bone:    return ICON_BONE;
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
                    ImGui::TextDisabled(ICON_BONE " %s — %d ossos",
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
                                "Sem controle posado, o rig deveria ser transparente —\n"
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
                                        "ela pula — entao isto nao e um atestado.\n\n"
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
                                    ImGui::TextDisabled("nenhum no copia LOCAL de controle — "
                                        "a hierarquia dos controles nao interfere");
                                else
                                    ImGui::TextDisabled("os %d controle(s) copiados em LOCAL "
                                        "espelham a hierarquia dos ossos",
                                        (int)localCopied.size());
                            }

                            ImGui::Separator();

                            if (m_RigPosedControls > 0) {
                                ImGui::TextDisabled("%d controle(s) posado(s) — parte do desvio "
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
                                ImGui::TextDisabled("na ordem do solve — quem mexeu em que:");
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
                                    "move — o efeito e o mesmo de deixar a opcao desligada.");
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

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ((manyTracks && !selected) ? 0 : ImGuiTreeNodeFlags_DefaultOpen) |
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
                        "Vale so para esta sequence — o socket continua sendo do\n"
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
            ImGui::TextDisabled("%d keys selecionadas — o painel edita a de cor "
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
        if (maxScroll > 0.0f && timelineHovered &&
            !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) m_TimelineScrollY -= wheel * lanePitch * 2.0f;
        }
        m_TimelineScrollY = std::clamp(m_TimelineScrollY, 0.0f, maxScroll);

        // Recorta as lanes na area delas: sem isto uma lane meio rolada desenha
        // por cima da regua de frames.
        dl->PushClipRect(ImVec2(origin.x, lanesTop),
            ImVec2(origin.x + size.x, lanesBottom), true);

        float laneY = lanesTop - m_TimelineScrollY;
        for (int bi = 0; bi < bindingCount; ++bi) {
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

                                if (ImGui::MenuItem(delLabel)) {
                                    if (selCount > 1) DeleteSelectedKeys();
                                    else {
                                        m_Asset.RemoveKey(bi, ti, si, ci, ki);
                                        ClearKeySelection();
                                        m_SelectedKey = -1;
                                    }
                                    ImGui::EndPopup();
                                    break;   // os indices deste canal mudaram
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

                for (std::size_t i = 0; i < n; ++i) {
                    if (SequencerKey* kk = ResolveKeyRef(m_SelectedKeys[i]))
                        kk->Frame = m_DragOriginalFrames[i] + deltaFrames;
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
        if (maxScroll > 0.0f && viewHeight > 0.0f) {
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

        // E o gizmo. Um pedido esquecido no viewport continuaria manipulando o
        // osso de uma sequence que nem esta mais aberta — com o agravante de
        // que o gizmo externo SUPRIME o de entidade, entao o usuario perderia o
        // gizmo normal sem entender por que.
        if (m_Context && m_Context->Viewport)
            m_Context->Viewport->ClearExternalGizmo();

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
            AXE_EDITOR_ERROR("Sequencer: clipe '{}' nao existe nesta entidade — "
                "nada a explodir.", clipName);
            return 0;
        }

        const Skeleton* skel = GetBindingSkeleton(bindingIndex);
        if (!skel) {
            AXE_EDITOR_ERROR("Sequencer: binding sem esqueleto resolvido — "
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
                "da bind pose — nenhuma track criada.",
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
                                "' — referenciais diferentes", true });
                        }

                        if (frameMismatch > 0) {
                            m_RigAudit.push_back({ where, n.Title,
                                "ponha o Space deste FK Chain em GLOBAL — em Local os "
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
                                    " — espacos diferentes", true });
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
                                            "') — referenciais diferentes. Ponha os dois em GLOBAL.",
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
                                "' — o solve vai girar '" + root + "'", true });
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
        for (const auto& s : m_Player.GetLastSamples()) {
            if (s.BindingIndex != bindingIndex) continue;
            if (s.TargetType != SequencerTargetType::Control) continue;

            const int idx = rt->Hierarchy.Find(s.TargetName, RigElementType::Control);
            if (idx < 0) continue;

            RigElement& el = rt->Hierarchy[idx];

            // ── CONTROLE DE CANAL (interruptor / slider) ─────────────────────
            //
            // BoolValue/FloatValue vivem FORA de Initial/Current e o
            // ResetToInitial nao os toca — de proposito, senao um interruptor
            // voltaria ao padrao a cada quadro. Entao escrever aqui basta.
            if (el.ValueType != RigControlValue::Transform) {
                el.FloatValue = s.Value;
                el.BoolValue = (s.Value >= 0.5f);
                continue;
            }

            // Euler em GRAUS na curva (e o que o painel de key mostra), quat no
            // BoneTransform. Mesma fronteira do socket.
            glm::vec3 euler = glm::degrees(glm::eulerAngles(el.Value.Rotation));

            switch (s.Component) {
            case SequencerChannelComponent::X:      el.Value.Translation.x = s.Value; break;
            case SequencerChannelComponent::Y:      el.Value.Translation.y = s.Value; break;
            case SequencerChannelComponent::Z:      el.Value.Translation.z = s.Value; break;
            case SequencerChannelComponent::ScaleX: el.Value.Scale.x = s.Value; break;
            case SequencerChannelComponent::ScaleY: el.Value.Scale.y = s.Value; break;
            case SequencerChannelComponent::ScaleZ: el.Value.Scale.z = s.Value; break;

            case SequencerChannelComponent::RotX:   euler.x = s.Value;
                el.Value.Rotation = glm::quat(glm::radians(euler)); break;
            case SequencerChannelComponent::RotY:   euler.y = s.Value;
                el.Value.Rotation = glm::quat(glm::radians(euler)); break;
            case SequencerChannelComponent::RotZ:   euler.z = s.Value;
                el.Value.Rotation = glm::quat(glm::radians(euler)); break;
            }
        }

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
        rc.DeltaTime = ImGui::GetIO().DeltaTime;
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
        rt->Hierarchy.SetValueFromGlobal(idx, glm::inverse(entityWorld) * world);

        const RigElement& el = rt->Hierarchy[idx];

        const int si = SectionAtPlayhead(bindingIndex, trackIndex);
        if (si < 0) return;

        const ImGuizmo::OPERATION op = m_Context->Viewport
            ? m_Context->Viewport->GetGizmoOperation() : ImGuizmo::TRANSLATE;

        if (op == ImGuizmo::TRANSLATE) {
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si,
                SequencerChannelComponent::X, el.Value.Translation.x);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si,
                SequencerChannelComponent::Y, el.Value.Translation.y);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si,
                SequencerChannelComponent::Z, el.Value.Translation.z);
        }
        else if (op == ImGuizmo::ROTATE) {
            SetRotationKeysAtPlayhead(bindingIndex, trackIndex, si, el.Value.Rotation);
        }
        else if (op == ImGuizmo::SCALE) {
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si,
                SequencerChannelComponent::ScaleX, el.Value.Scale.x);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si,
                SequencerChannelComponent::ScaleY, el.Value.Scale.y);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si,
                SequencerChannelComponent::ScaleZ, el.Value.Scale.z);
        }
    }

    // Picker de CONTROLE do rig ligado ao binding.
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

    void SequencerWindow::SetRotationKeysAtPlayhead(int bindingIndex, int trackIndex,
        int sectionIndex, const glm::quat& rotation) {
        // ── DESEMBRULHO CONTRA A KEY ANTERIOR ────────────────────────────────
        //
        // O mesmo problema do bake de clipe, agora ao vivo: `glm::eulerAngles`
        // devolve a forma canonica, e arrastar o gizmo por cima de um limite de
        // ramo cravaria uma key com -179 ao lado de uma de +179. Na reproducao,
        // o osso daria uma volta completa entre dois frames vizinhos.
        //
        // A referencia e a key de rotacao mais proxima ANTES do playhead. Sem
        // key anterior, a canonica serve — nao ha com o que ser continuo.
        glm::vec3 prev(0.0f);
        bool hasPrev = false;

        auto* b = m_Asset.GetBinding(bindingIndex);
        if (b && trackIndex >= 0 && trackIndex < static_cast<int>(b->Tracks.size())) {
            auto& tr = b->Tracks[trackIndex];
            if (sectionIndex >= 0 && sectionIndex < static_cast<int>(tr.Sections.size())) {
                auto& sec = tr.Sections[sectionIndex];
                const float now = m_Player.GetCurrentFrame();

                const SequencerChannelComponent rot[3] = {
                    SequencerChannelComponent::RotX,
                    SequencerChannelComponent::RotY,
                    SequencerChannelComponent::RotZ
                };

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
        }

        const glm::vec3 euler = ContinuousEuler(rotation, prev, hasPrev);

        SetChannelKeyAtPlayhead(bindingIndex, trackIndex, sectionIndex,
            SequencerChannelComponent::RotX, euler.x);
        SetChannelKeyAtPlayhead(bindingIndex, trackIndex, sectionIndex,
            SequencerChannelComponent::RotY, euler.y);
        SetChannelKeyAtPlayhead(bindingIndex, trackIndex, sectionIndex,
            SequencerChannelComponent::RotZ, euler.z);
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

    void SequencerWindow::UpdateGizmo() {
        if (!m_Context || !m_Context->Viewport) return;

        // Sem alvo manipulavel: LIMPA o pedido. Nao basta "nao pedir" — o
        // viewport guarda o ultimo, e um gizmo esquecido continuaria movendo o
        // osso de uma track que o usuario ja desselecionou.
        auto clear = [&]() { m_Context->Viewport->ClearExternalGizmo(); };

        if (!m_IsOpen || !m_GizmoEnabled) { clear(); return; }

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
            default: break;
            }
            };

        g.OnFinish = [this, bi, ti]() {
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

        const int si = SectionAtPlayhead(bindingIndex, trackIndex);
        if (si < 0) return;

        // ── SO OS CANAIS DA OPERACAO ATIVA ───────────────────────────────────
        //
        // Keyar os nove a cada toque encheria a timeline de curvas retas de
        // escala e translacao que o animador nunca pediu — e cada uma delas
        // depois congela o osso caso o esqueleto mude. O gizmo esta em modo
        // rotacao? Entao so as tres rotacoes viram key.
        const ImGuizmo::OPERATION op = m_Context->Viewport
            ? m_Context->Viewport->GetGizmoOperation() : ImGuizmo::TRANSLATE;

        if (op == ImGuizmo::TRANSLATE) {
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::X, t.x);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::Y, t.y);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::Z, t.z);
        }
        else if (op == ImGuizmo::ROTATE) {
            SetRotationKeysAtPlayhead(bindingIndex, trackIndex, si, q);
        }
        else if (op == ImGuizmo::SCALE) {
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleX, s.x);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleY, s.y);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleZ, s.z);
        }
    }

    void SequencerWindow::ApplyGizmoToSocket(int bindingIndex, int trackIndex,
        const glm::mat4& world) {
        if (!m_Context || !m_Context->ActiveScene) return;

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

        const int si = SectionAtPlayhead(bindingIndex, trackIndex);
        if (si < 0) return;

        const ImGuizmo::OPERATION op = m_Context->Viewport
            ? m_Context->Viewport->GetGizmoOperation() : ImGuizmo::TRANSLATE;

        if (op == ImGuizmo::TRANSLATE) {
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::X, t.x);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::Y, t.y);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::Z, t.z);
        }
        else if (op == ImGuizmo::ROTATE) {
            SetRotationKeysAtPlayhead(bindingIndex, trackIndex, si, q);
        }
        else if (op == ImGuizmo::SCALE) {
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleX, s.x);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleY, s.y);
            SetChannelKeyAtPlayhead(bindingIndex, trackIndex, si, SequencerChannelComponent::ScaleZ, s.z);
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
        for (const auto& r : m_SelectedKeys) {
            const SequencerKey* k = ResolveKeyRef(r);
            m_DragOriginalFrames.push_back(k ? k->Frame : 0.0f);
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

                for (const auto& s : m_Player.GetLastSamples()) {
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

        // Devolve a pose ao AnimationWorld. `PreviewInEditor` nao e mais tocado
        // aqui: ele nunca foi o gate certo (so decide se o TEMPO avanca) e mexer
        // nele apagava a escolha do usuario no Inspector.
        ReleasePoseOverride();
    }

} // namespace axe