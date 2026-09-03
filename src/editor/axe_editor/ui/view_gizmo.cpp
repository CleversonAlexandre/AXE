#include "view_gizmo.hpp"

#include "axe/graphics/editor_camera.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// ═══════════════════════════════════════════════════════════════════════════
//  VIEW_GIZMO_V1 — a UNICA unidade de compilacao que instancia a lib.
//
//  A ImViewGuizmo e header-only no estilo stb: o header traz declaracao E
//  implementacao, e a implementacao so entra onde este define existir. Se dois
//  .cpp definirem, o link quebra com simbolo duplicado.
//
//  Por isso o resto do editor inclui `view_gizmo.hpp`, nunca o header da lib.
// ═══════════════════════════════════════════════════════════════════════════
//  BUG DA LIB, contornado aqui: a implementacao usa std::array e std::sort e
//  NAO inclui <array> nem <algorithm> — ela so compila onde alguem ja os
//  arrastou por include transitivo. Incluir antes e o conserto de um lado so;
//  editar o vendor daria trabalho a cada atualizacao dele.
#include <array>
#include <algorithm>

#define IMVIEWGUIZMO_IMPLEMENTATION
#include <ImViewGuizmo.h>

namespace axe::ui
{
    namespace
    {
        // ── VIEW_GIZMO_V1b — OS TAMANHOS SAO DA LIB, NAO MEUS ─────────────
        //
        // Na primeira versao eu chutei "raio 44" e posicionei tudo a partir
        // disso. Errado: o gizmo da lib tem `bigCircleRadius = 80` (160 px de
        // diametro) e cada botao de ferramenta tem `toolButtonRadius = 25`
        // (50 px). Resultado na tela: o gizmo saindo pela borda direita e os
        // botoes de dolly e pan EMPILHADOS um sobre o outro, porque meu
        // espacamento de 24 px era menor que os 50 px de um botao.
        //
        // Agora tudo sai de ImViewGuizmo::GetStyle(), que e a fonte real.
        // Numero de layout so pode vir de onde o desenho acontece.
        constexpr float kMargin = 10.0f;   // recuo da borda do viewport
        constexpr float kToolGap = 8.0f;   // folga entre gizmo e botoes

        // Escala confortavel no viewport principal. O default da lib (1.0) da
        // 160 px de gizmo, que domina um painel de editor.
        constexpr float kDefaultScale = 0.55f;

        // ── VIEW_GIZMO_V1c — ESTADO QUE A LIB NAO TEM E PRECISA TER ───────
        //
        // O Context da ImViewGuizmo e um SINGLETON global, compartilhado por
        // todos os paineis, e ele NUNCA e limpo quando o gizmo deixa de ser
        // desenhado. Dai os dois travamentos:
        //
        //  (a) `hoveredAxisID` so e recalculado quando o gizmo desenha. Painel
        //      fechado, viewport em Play, ou meu early-return de tamanho
        //      deixavam o valor CONGELADO no ultimo eixo apontado — e o
        //      IsOver() ficava true para sempre. Como a guarda de clique usa
        //      isso, a selecao no viewport parava de funcionar de vez.
        //
        //  (b) O snap dispara em `IsMouseReleased(0)` com um eixo sob o mouse,
        //      SEM exigir que o clique tenha COMECADO no gizmo. Entao soltar o
        //      botao depois de orbitar com Alt, com o cursor por acaso sobre um
        //      eixo, saltava a camera para aquela vista. Era o "rotaciono com o
        //      mouse e ele volta para a vista do view gizmo".
        //
        // Estas duas flags sao o minimo para consertar os dois sem tocar no
        // vendor.
        bool  s_DrawnThisFrame = false;
        int   s_LastDrawFrame = -1;
        bool  s_PressStartedOnGizmo = false;
    }

    ViewGizmoResult DrawViewGizmo(EditorCamera& camera,
        const ImVec2& viewportMin,
        const ImVec2& viewportMax,
        bool showTools,
        float scale)
    {
        ViewGizmoResult res;

        // Novo frame do ImGui: zera o "desenhou". Cada painel que desenhar
        // liga de volta. Sem isto, o estado do frame anterior sobrevive.
        const int frame = ImGui::GetFrameCount();
        if (frame != s_LastDrawFrame)
        {
            s_LastDrawFrame = frame;
            s_DrawnThisFrame = false;
        }

        const float w = viewportMax.x - viewportMin.x;
        const float h = viewportMax.y - viewportMin.y;
        if (w <= 0.0f || h <= 0.0f) { ImViewGuizmo::EndInteraction(); return res; }

        // ── VIEW_GIZMO_V1c — o clique comecou SOBRE o gizmo? ──────────────
        //
        // Lido no frame do PRESS, usando o hover que a lib calculou no frame
        // anterior — que e exatamente o estado sob o cursor quando o botao
        // desceu.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            s_PressStartedOnGizmo = ImViewGuizmo::IsOver();

        // Soltar o botao de um arrasto que NAO comecou no gizmo: pula a
        // chamada inteira neste frame.
        //
        // O gatilho do snap dentro da lib e o evento de release, que so existe
        // num frame; nao chamando, ele nunca acontece e nenhuma animacao
        // comeca. O custo e um unico frame sem o gizmo desenhado, no instante
        // do mouse-up — invisivel — e em troca orbitar com Alt para de saltar
        // para a vista de um eixo por acidente.
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !s_PressStartedOnGizmo)
            return res;

        auto& style = ImViewGuizmo::GetStyle();

        if (scale <= 0.0f) scale = kDefaultScale;

        // ── Cabe? Se nao, encolhe em vez de vazar ─────────────────────────
        //
        // Preview pequeno (particle, anim graph) nao tem 160 px de sobra. Sem
        // isto o gizmo simplesmente sai pela borda e fica inclicavel — que e
        // pior que um gizmo pequeno.
        const float baseR = style.bigCircleRadius;   // 80 na lib
        const float toolR = style.toolButtonRadius;  // 25 na lib

        float needW = 2.0f * baseR * scale + 2.0f * kMargin;
        float needH = 2.0f * baseR * scale + 2.0f * kMargin;
        if (showTools)
            needH += kToolGap + 2.0f * (2.0f * toolR * scale) + kToolGap;

        if (needW > w || needH > h)
        {
            const float fitW = (w - 2.0f * kMargin) / (2.0f * baseR);
            const float fitH = showTools
                ? (h - 2.0f * kMargin - 2.0f * kToolGap) / (2.0f * baseR + 4.0f * toolR)
                : (h - 2.0f * kMargin) / (2.0f * baseR);
            scale = glm::min(scale, glm::max(glm::min(fitW, fitH), 0.0f));
        }

        // Abaixo disto o gizmo vira um borrao sem alvo clicavel — melhor nao
        // desenhar do que desenhar algo que o usuario nao consegue usar.
        //
        // VIEW_GIZMO_V1d — ao desistir de desenhar, SOLTA o gizmo. Sem isto um
        // arrasto interrompido pelo painel encolher deixaria a ferramenta
        // ativa, e na proxima vez que este painel desenhasse a camera passaria
        // a seguir o mouse sozinha.
        if (scale < 0.18f) { ImViewGuizmo::EndInteraction(); return res; }

        // A Style e GLOBAL e a API e imediata: escrever aqui vale so para as
        // chamadas abaixo, e o proximo painel escreve a sua.
        style.scale = scale;

        s_DrawnThisFrame = true;   // VIEW_GIZMO_V1c

        // Rotulo legivel: o default da lib e quase preto sobre circulos
        // coloridos e escuros, e as letras somem.
        style.labelColor = IM_COL32(245, 246, 248, 255);

        const float r = baseR * scale;
        const ImVec2 center(viewportMax.x - r - kMargin,
            viewportMin.y + r + kMargin);

        // ── O estado da camera, na linguagem da lib ───────────────────────
        //
        // A EditorCamera e orbital; a lib fala posicao + quaternion. Estes
        // tres getters ja existem e sao a traducao completa.
        glm::vec3 pos = camera.GetPosition();
        glm::quat rot = camera.GetOrientation();
        glm::vec3 pivot = camera.GetFocalPoint();

        bool changed = ImViewGuizmo::Rotate(pos, rot, pivot, center);

        if (showTools)
        {
            const float tr = toolR * scale;
            const float firstY = center.y + r + kToolGap + tr;

            const glm::vec3 beforeTools = pos;

            changed |= ImViewGuizmo::Dolly(pos, rot, ImVec2(center.x, firstY));
            const bool panned = ImViewGuizmo::Pan(pos, rot,
                ImVec2(center.x, firstY + 2.0f * tr + kToolGap));
            changed |= panned;

            // PAN move o ALVO junto; dolly nao. Sem isto, arrastar o pan
            // afastaria a camera do foco em vez de deslizar a vista — e o
            // proximo Rotate orbitaria em volta de um ponto que ficou para
            // tras. Dolly e pan nunca acontecem no mesmo frame (e um arrasto
            // de cada vez), entao a diferenca e atribuivel.
            if (panned) pivot += (pos - beforeTools);
        }

        res.Changed = changed;
        res.Hovered = ImViewGuizmo::IsOver();
        res.Using = ImViewGuizmo::IsUsing();

        if (!changed) return res;

        glm::vec3 forward = glm::rotate(rot, glm::vec3(0.0f, 0.0f, -1.0f));

        // Direcao degenerada nunca deveria sair de um quaternion unitario, mas
        // um NaN vindo de fora viraria uma camera perdida sem pista da causa —
        // e o PointAt faz asin/atan2 em cima disso.
        if (glm::any(glm::isnan(forward)) || glm::length(forward) < 1e-4f)
            return res;
        forward = glm::normalize(forward);

        // ═════════════════════════════════════════════════════════════════
        //  VIEW_GIZMO_V1b — TRAVA DE GIMBAL AO SALTAR PARA UM EIXO
        //
        //  Clicar em Y salta para a vista de topo, com a frente em (0,-1,0).
        //  Nessa direcao a camera orbital DEGENERA:
        //
        //    PointAt faz  pitch = asin(-f.y) = +-90 graus
        //                 yaw   = atan2(f.x, -f.z) = atan2(0, 0)  -> indefinido
        //
        //  e o MouseRotate escolhe o sinal do yaw por `GetUpDirection().y < 0`,
        //  que em 90 graus exatos e ZERO e oscila. Na pratica: depois de clicar
        //  num eixo, Alt+arrastar para de girar direito — que foi exatamente o
        //  relato.
        //
        //  A saida e a de sempre em camera orbital: nunca chegar aos 90 exatos.
        //  0.2 grau de folga e invisivel na imagem e devolve um yaw definido.
        // ═════════════════════════════════════════════════════════════════
        constexpr float kMaxVertical = 0.99999f;  // ~89.8 graus
        if (std::fabs(forward.y) > kMaxVertical)
        {
            const float sign = forward.y > 0.0f ? 1.0f : -1.0f;
            const float horiz = std::sqrt(1.0f - kMaxVertical * kMaxVertical);

            // Mantem o azimute atual se houver; senao escolhe um estavel, para
            // a vista de topo nao girar sozinha a cada clique.
            glm::vec2 dirXZ(forward.x, forward.z);
            if (glm::length(dirXZ) < 1e-5f) dirXZ = glm::vec2(0.0f, -1.0f);
            dirXZ = glm::normalize(dirXZ) * horiz;

            forward = glm::vec3(dirXZ.x, sign * kMaxVertical, dirXZ.y);
        }

        // ═════════════════════════════════════════════════════════════════
        //  VIEW_GIZMO_V1b — POR QUE PointAt SOZINHO NAO BASTAVA
        //
        //  PointAt poe o foco em `posicao + frente * distancia_atual`. Isso
        //  acerta num frame isolado, mas o salto para um eixo e ANIMADO
        //  (style.animateSnap, ~0.5 s): a lib devolve uma pose nova a cada
        //  frame, e eu devolvia um FOCO novo junto. No frame seguinte esse
        //  foco deslocado virava o `pivot` que eu passava de volta para a lib
        //  — realimentacao. O alvo caminhava a cada frame e a camera ia
        //  embora. Era o "no anim graph a camera fica distante".
        //
        //  Agora o pivo e determinado AQUI, deterministicamente (fixo no
        //  Rotate e no Dolly, deslocado pelo Pan), e escrito de volta com o
        //  SetView. O PointAt fica so com o que ele faz bem: derivar yaw e
        //  pitch da direcao.
        //
        //  A ordem importa: PointAt primeiro (fixa a orientacao), SetView
        //  depois (fixa foco e distancia). Como a posicao e derivada de
        //  `foco - frente * distancia`, ela cai de volta exatamente em `pos`.
        // ═════════════════════════════════════════════════════════════════
        const float dist = glm::length(pos - pivot);
        if (!std::isfinite(dist) || dist < 1e-4f) return res;

        camera.PointAt(pos, forward);
        camera.SetView(pivot, dist);

        return res;
    }

    bool ViewGizmoCapturesMouse()
    {
        // VIEW_GIZMO_V1c — sem o gizmo na tela nao ha o que capturar.
        //
        // O IsOver() da lib le um `hoveredAxisID` que fica CONGELADO quando o
        // gizmo nao e desenhado (painel fechado, viewport em Play, preview
        // pequeno demais). Era isso que travava a selecao no viewport de vez:
        // a guarda de clique acreditava, para sempre, que o mouse estava sobre
        // um eixo que nem estava na tela.
        if (!s_DrawnThisFrame) return false;

        return ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing();
    }
}