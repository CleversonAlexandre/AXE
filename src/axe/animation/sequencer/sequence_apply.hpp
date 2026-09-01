#pragma once

// src/axe/animation/sequencer/sequence_apply.hpp
//
// ═══════════════════════════════════════════════════════════════════════════
//  A MATEMATICA DE "APLICAR UM SAMPLE", NUM LUGAR SO
// ═══════════════════════════════════════════════════════════════════════════
//
// O SequencerPlayer sabe AMOSTRAR: dado um frame, ele devolve uma lista de
// SequencerSample (um escalar por canal). Quem transforma essa lista em pose,
// em transform de entidade e em Value de controle era, ate aqui, o
// SequencerWindow — e so ele.
//
// ── POR QUE ISTO PRECISOU SAIR DO EDITOR ────────────────────────────────────
//
// Uma cutscene que so toca com a janela do Sequencer aberta nao e uma
// cutscene. Para o jogo tocar a mesma sequence, alguem no runtime precisa
// fazer exatamente estas contas.
//
// "Exatamente" e a palavra que importa. Reimplementar do lado do runtime
// seria a receita conhecida deste projeto, e o sintoma seria o pior possivel
// para uma ferramenta de cutscene: a cena aprovada no editor tocando
// DIFERENTE no jogo. Nao um erro visivel — um deslocamento sutil, num eixo,
// que ninguem consegue reproduzir.
//
// Ja aconteceu aqui, com nome e sobrenome: `SnapControlsToCurrentBones` tinha
// um unico chamador, e esse chamador era uma janela do editor. O resultado
// foi o BP_Player torto ao mirar, com o mesmo rig que funcionava no
// Sequencer.
//
// ── O QUE ENTRA AQUI, E O QUE NAO ───────────────────────────────────────────
//
// Entra a MATEMATICA: como um escalar vira translacao, rotacao ou escala.
//
// Nao entra a ORQUESTRACAO — quem e dono da pose, o que precisa ser
// restaurado no fim, que diagnostico se coleta. Editor e runtime tem ciclos
// de vida legitimamente diferentes (o editor desfaz o que escreveu quando a
// janela fecha; o jogo nao fecha janela nenhuma), e forcar os dois no mesmo
// molde criaria um objeto que nao serve bem a nenhum.
//
// ── A FRONTEIRA DE UNIDADES, DITA UMA VEZ ───────────────────────────────────
//
// As curvas guardam rotacao em GRAUS de Euler — e o que o animador digita no
// painel de key, e o que torna uma curva de rotacao legivel.
//
// Do outro lado:
//   - `BoneTransform::Rotation` e QUATERNION;
//   - `Transform::Rotation` e vec3 em RADIANOS (`GetMatrix` faz
//     `glm::quat(Rotation)`, que interpreta radianos).
//
// A conversao acontece so aqui, no ultimo momento. Todo bug de "gira 57 vezes
// demais" neste motor nasceu de alguem atravessar essa fronteira em outro
// lugar.
// ═══════════════════════════════════════════════════════════════════════════

#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include "axe/animation/pose.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/rig/rig_hierarchy.hpp"
#include "axe/animation/sequencer/sequencer_track.hpp"
#include "axe/scene/transform.hpp"

#include <vector>

namespace axe::sequence {

    // ── ROTACAO DOS TRES EIXOS, ACUMULADA ANTES DE VIRAR QUATERNION ─────────
    //
    // POR QUE ISTO NAO PODE SER FEITO CANAL A CANAL:
    //
    //   `WriteComponent(RotX)` decompoe o quaternion, troca X e RECOMPOE.
    //   `WriteComponent(RotZ)` logo em seguida decompoe DE NOVO — e
    //   `glm::eulerAngles` nao devolve necessariamente o mesmo triplo que
    //   acabou de entrar: a mesma rotacao tem infinitas representacoes em
    //   Euler, e a funcao escolhe a faixa canonica (Y em [-90,90], X e Z em
    //   [-180,180]). Quando a escolha muda de ramo, o segundo canal escreve
    //   por cima de um triplo diferente do que o primeiro montou, e a edicao
    //   do primeiro eixo some.
    //
    //   Juntando os tres canais do mesmo osso e recompondo UMA vez, a
    //   decomposicao acontece exatamente uma vez por osso por frame — e sempre
    //   sobre a pose de repouso, que e estavel.
    struct BoneEulerEdit {
        int       BoneIdx = -1;
        glm::vec3 Euler{ 0.0f };
    };

    // Escreve um componente escalar num BoneTransform LOCAL.
    AXE_API void WriteComponent(BoneTransform& t, SequencerChannelComponent c, float v);

    // Devolve o eixo (0/1/2) se o componente for de rotacao.
    AXE_API bool RotationAxisOf(SequencerChannelComponent c, int& outAxis);

    // ── ENTIDADE ────────────────────────────────────────────────────────────
    //
    // `base` e o transform ORIGINAL da entidade, e nao o zero. Sobrepor so os
    // canais animados e o que permite animar apenas a altura da camera sem
    // jogar a posicao horizontal dela na origem — o mesmo motivo pelo qual uma
    // track de osso nasce sem canais.
    //
    // `UseWorldMatrix` sai desligado: a sequence autora Position/Rotation/
    // Scale, e uma matriz de mundo pendurada por um gizmo anterior venceria
    // tudo isso em silencio.
    AXE_API Transform ApplyEntitySamples(const Transform& base,
        const std::vector<SequencerSample>& samples,
        int bindingIndex);

    // ── OSSOS ───────────────────────────────────────────────────────────────
    //
    // Sobrepoe na pose LOCAL os canais de osso do binding. `scratch` e
    // reaproveitado entre chamadas so para nao alocar por frame; o conteudo
    // dele nao atravessa a chamada.
    AXE_API void ApplyBoneSamples(Pose& pose, const Skeleton& skel,
        const std::vector<SequencerSample>& samples,
        int bindingIndex,
        std::vector<BoneEulerEdit>& scratch);

    // ── CONTROLES DO RIG ────────────────────────────────────────────────────
    //
    // Escreve os canais de controle no `Value` da hierarquia. Tem de rodar
    // ANTES do `ResetToInitial`, e nao depois: e o proprio ResetToInitial que
    // resolve `Current = Initial * Value`. Na ordem invertida, o valor escrito
    // num frame so aparece no frame SEGUINTE — um quadro de atraso permanente
    // entre a key e a pose, invisivel parado e visivel como arrasto no scrub.
    //
    // ── POR QUE ZERA ANTES DE ESCREVER ──────────────────────────────────────
    //
    // `RigElement::Value` e o unico estado do solve que NAO e recomposto do
    // zero a cada frame — de proposito, senao um interruptor voltaria ao
    // padrao a cada quadro. Sem zerar, uma pose descartada ficaria pendurada
    // no controle para sempre, sem key nenhuma explicando de onde veio, e o
    // `.axerig` recebe Value no save: a pose vazaria para o asset.
    //
    // So os controles que TEM track neste binding sao zerados. Controle sem
    // track nao e dirigido pela sequence, e mexer nele aqui apagaria o que o
    // proprio grafo tiver posto la.
    AXE_API void ApplyControlSamples(RigHierarchy& hierarchy,
        const SequencerBinding& binding,
        const std::vector<SequencerSample>& samples,
        int bindingIndex);

} // namespace axe::sequence