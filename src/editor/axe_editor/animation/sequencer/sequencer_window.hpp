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
#include "axe/utils/glm_config.hpp"

#include <imgui.h>
// ImRect e SeparatorEx vivem em imgui_internal.h (mesmo padrao do AXE).
#include <imgui/imgui_internal.h>
#include <entt/entt.hpp>

#include <functional>
#include <memory>
#include <string>
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
        float m_DragOriginalFrame = 0.0f;

        // Drag state para playhead.
        bool  m_DraggingPlayhead = false;

        // Toolbar state.
        std::string m_LastLoadedPath;

        // --- Sub-rotinas de UI --------------------------------------------
        void DrawToolbar();
        void DrawOutliner();
        void DrawTimeline();
        void DrawTransportBar();

        void DrawBindingNode(int bindingIndex);
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

        // Devolve o PoseOverride das entidades ligadas, para o AnimationWorld
        // voltar a mandar na pose.
        void ReleasePoseOverride();

        // Popups de escolha (entidade / osso / clipe).
        void DrawAddBindingPopup();
        void DrawAddTrackPopup(int bindingIndex);
        void DrawAddClipPopup(int bindingIndex);

        // --- Estado de picker ----------------------------------------------
        bool m_OpenAddBindingPopup = false;
        int  m_AddTrackForBinding = -1;
        bool m_OpenAddTrackPopup = false;
        int  m_AddClipForBinding = -1;
        bool m_OpenAddClipPopup = false;
        char m_PickerFilter[64] = { 0 };

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