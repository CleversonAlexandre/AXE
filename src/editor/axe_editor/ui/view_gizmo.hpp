#pragma once
#include <imgui.h>

namespace axe { class EditorCamera; }

namespace axe::ui
{
    // ═══════════════════════════════════════════════════════════════════════
    //  VIEW_GIZMO_V1 — o widget de navegacao no canto do viewport
    //
    //  Envolve a ImViewGuizmo (MIT, header-only, ja usa glm) — o gizmo de
    //  eixos do canto, como em Blender e Godot: arrastar orbita, clicar num
    //  eixo salta para a vista, e ha botoes de dolly e pan.
    //
    //  ── POR QUE UM ADAPTADOR, E NAO CHAMAR A LIB DIRETO ────────────────────
    //
    //  As duas falam linguagens diferentes de camera:
    //
    //    ImViewGuizmo  — posicao + quaternion (camera livre)
    //    EditorCamera  — foco + distancia + yaw + pitch (camera ORBITAL),
    //                    com a posicao DERIVADA desses quatro
    //
    //  Escrever posicao e quaternion direto na EditorCamera nao existe: nao ha
    //  onde guardar. A conversao de volta e que fecha o circuito, e ela ja
    //  existia: `EditorCamera::PointAt(posicao, frente)`, escrita justamente
    //  para "fique AQUI olhando para ALI".
    //
    //  E ela serve as TRES ferramentas sem caso especial:
    //
    //    Rotate — orbita mantendo a distancia ao pivo. Como o PointAt poe o
    //             foco adiante na distancia atual, e a frente aponta para o
    //             pivo, o foco cai exatamente de volta no pivo.
    //    Zoom   — anda ao longo da frente. O foco acompanha, que e o que um
    //             dolly faz (diferente de aproximar do pivo).
    //    Pan    — anda lateralmente. O foco acompanha junto.
    //
    //  ── POR QUE UM ARQUIVO SO PARA ISTO ───────────────────────────────────
    //
    //  O gizmo e para o viewport E para todos os previews (material, script
    //  graph, e o que vier). Cada painel tem a PROPRIA EditorCamera. Repetir
    //  a conversao em cada um seria a mesma armadilha das listas de tipo que
    //  ja mordeu tres vezes nesta engine: N copias de uma regra, e o
    //  compilador nao confere nenhuma.
    //
    //  Aqui a regra vive uma vez e cada painel chama uma linha.
    // ═══════════════════════════════════════════════════════════════════════

    struct ViewGizmoResult
    {
        bool Changed = false;  // a camera se moveu neste frame
        bool Hovered = false;  // o mouse esta sobre o gizmo

        // Verdadeiro enquanto uma ferramenta esta ativa (arrastando).
        //
        // E o campo que importa para quem chama: o painel tem de ENGOLIR o
        // mouse enquanto isto for true, senao o arrasto no gizmo tambem
        // orbita a camera pelo caminho normal e o movimento sai dobrado.
        bool Using = false;
    };

    // Desenha o gizmo no canto superior direito da area dada.
    //
    // `viewportMin/Max` sao os cantos da imagem do viewport EM COORDENADAS DE
    // TELA (ImGui::GetItemRectMin/Max logo apos o ImGui::Image), e nao o
    // retangulo da janela: em painel com barra de ferramentas ou aba, os dois
    // diferem, e o gizmo apareceria fora da imagem.
    //
    // `showTools` liga os botoes de dolly e pan abaixo do gizmo. Em preview
    // pequeno eles roubam area util — por isso e opcional.
    //
    // `scale` — VIEW_GIZMO_V1b. O default da lib (1.0) desenha um gizmo de
    // 160 px, que domina um painel de editor. 0 usa o padrao daqui (0.55).
    // E o valor pedido e apenas um TETO: se o painel nao comportar o
    // footprint, a funcao encolhe sozinha, e abaixo de um minimo util nao
    // desenha nada — gizmo vazando pela borda fica inclicavel, que e pior
    // que gizmo pequeno.
    ViewGizmoResult DrawViewGizmo(EditorCamera& camera,
        const ImVec2& viewportMin,
        const ImVec2& viewportMax,
        bool showTools = true,
        float scale = 0.0f);

    // O mouse pertence ao gizmo neste frame?
    //
    // Existe para quem precisa DESISTIR do clique: sem isto, clicar num eixo
    // do gizmo tambem seleciona o objeto que estiver atras dele, e arrastar
    // orbita duas vezes (pelo gizmo e pelo caminho normal).
    //
    // Le o estado VIVO da lib, e nao uma flag guardada do frame anterior — um
    // frame de atraso aqui vaza exatamente o PRIMEIRO clique, que e o que o
    // usuario percebe.
    bool ViewGizmoCapturesMouse();
}