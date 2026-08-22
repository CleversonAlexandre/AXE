#pragma once

// src/editor/axe_editor/rig/rig_control_gizmos.hpp
//
// ═══════════════════════════════════════════════════════════════════════════
//  FORMAS DOS CONTROLS DO RIG, DESENHADAS SOBRE UMA IMAGEM DE VIEWPORT
//
//  ── POR QUE ISTO SAIU DO rig_preview.cpp ─────────────────────────────────
//
//  O desenho nasceu dentro do `ControlRigWindow` e ficou preso la. O item
//  "clicar em controle do rig no viewport" esta aberto no handoff do
//  Sequencer desde o inicio exatamente por causa disso: a unica superficie do
//  editor que sabia desenhar um Control era a preview do editor de rig.
//
//  A regra de promocao do `editor_widgets.hpp` vale aqui — "ter dois usos
//  reais, nao um uso e uma suposicao". Agora sao dois: a preview do Control
//  Rig e o viewport principal, dirigido pelo Sequencer.
//
//  ── POR QUE ISTO NAO E CODIGO DE RENDERER ────────────────────────────────
//
//  Nao ha uma linha de OpenGL, nem malha, nem draw call. Cada forma vive em
//  espaco LOCAL do controle, e os pontos sao projetados um a um pela
//  view-projection e desenhados com `ImDrawList`, por cima da imagem que o
//  viewport ja produziu.
//
//  A alternativa — um renderer de linhas de verdade — daria teste de
//  profundidade (controle atras da perna ficaria escondido) e custaria a
//  separacao frontend/backend que o projeto protege. Enquanto a forma for uma
//  ajuda de autoria que deve ser SEMPRE clicavel, ficar por cima e o
//  comportamento certo, e nao uma limitacao.
//
//  ── PROJECAO E HIT-TEST ANDAM JUNTOS ─────────────────────────────────────
//
//  O raio de acerto sai do que a forma OCUPOU na tela, medido enquanto ela e
//  desenhada. Separar as duas coisas em duas funcoes obrigaria a projetar
//  tudo duas vezes e, pior, deixaria os dois numeros livres para divergir —
//  o sintoma seria uma forma grande de perfil que nao se consegue clicar.
// ═══════════════════════════════════════════════════════════════════════════

#include "axe/animation/rig/rig_hierarchy.hpp"
#include "axe/utils/glm_config.hpp"

#include <imgui.h>

#include <vector>

namespace axe::ui
{
    // Desenha os Controls de `h` e devolve o INDICE do elemento sob o mouse,
    // ou -1. Nao trata clique nem selecao: quem chama decide o que fazer com o
    // indice, porque "selecionar" quer dizer coisas diferentes no editor de
    // rig (escolher o elemento) e no Sequencer (escolher o alvo do gizmo).
    //
    //   viewProj  view * projection da camera que gerou a imagem
    //   model     transform do dono do rig (a entidade, ou o offset da preview)
    //   imgMin    canto superior esquerdo da imagem, em coordenadas de tela
    //   imgSize   tamanho da imagem em pixels
    //   active    o indice ATIVO (o do gizmo), ou -1. Ganha o anel branco.
    //   group      demais indices selecionados. Acendem, mas sem o anel: numa
    //             selecao de cinco, cinco aneis brancos nao dizem em qual deles
    //             o gizmo esta, que e a unica coisa que o anel existe para
    //             dizer.
    //   hovered   indice sob o mouse no frame ANTERIOR, ou -1. So muda a
    //             espessura do traco — o retorno e recalculado do zero.
    //
    // Controls de CANAL (`ValueType != Transform`) e invisiveis sao pulados:
    // um canal e um valor, nao um objeto no espaco, e desenhar um circulo para
    // ele so daria algo para clicar sem efeito.
    int DrawRigControlGizmos(ImDrawList* dl,
        const RigHierarchy& h,
        const glm::mat4& viewProj,
        const glm::mat4& model,
        const ImVec2& imgMin,
        const ImVec2& imgSize,
        int active,
        int hovered = -1,
        const std::vector<int>* group = nullptr);

} // namespace axe::ui