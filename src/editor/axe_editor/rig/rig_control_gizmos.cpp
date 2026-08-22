#include "editor/axe_editor/rig/rig_control_gizmos.hpp"

#include <algorithm>
#include <cmath>

namespace axe::ui
{
    int DrawRigControlGizmos(ImDrawList* dl,
        const RigHierarchy& h,
        const glm::mat4& viewProj,
        const glm::mat4& model,
        const ImVec2& imgMin,
        const ImVec2& imgSize,
        int active,
        int hoveredPrev,
        const std::vector<int>* group)
    {
        if (!dl || imgSize.x <= 0.0f || imgSize.y <= 0.0f)
            return -1;

        const ImVec2 mouse = ImGui::GetMousePos();

        int   hovered = -1;
        float hoveredDist = 1e9f;

        for (std::size_t i = 0; i < h.Size(); ++i)
        {
            const RigElement& e = h[(int)i];

            // Canal nao tem forma: e um valor, nao um objeto no espaco.
            if (e.Type != RigElementType::Control
                || e.ValueType != RigControlValue::Transform
                || !e.Visible)
                continue;

            // ── A MATRIZ COMPLETA DO DESENHO ─────────────────────────────
            //
            // Os pontos da forma vivem no espaco LOCAL do controle e sao
            // projetados um a um. Rotacao, escala e offset entram de graca,
            // porque estao todos nesta matriz.
            //
            // A versao antiga usava so a POSICAO e desenhava um circulo 2D
            // chapado — por isso girar o controle nao mudava nada na tela e a
            // escala do Shape offset nao fazia efeito.
            const glm::mat4 world = model * h.GetControlShapeMatrix((int)i);

            // Projeta um ponto local -> tela. Devolve false ATRAS da camera:
            // sem esse teste o ponto aparece espelhado do lado oposto.
            bool anyBehind = false;

            auto project = [&](const glm::vec3& local, ImVec2& out) -> bool
                {
                    const glm::vec4 clip = viewProj * world * glm::vec4(local, 1.0f);

                    if (clip.w <= 0.0001f)
                    {
                        anyBehind = true;
                        return false;
                    }

                    const glm::vec3 ndc = glm::vec3(clip) / clip.w;

                    out = ImVec2(
                        imgMin.x + (ndc.x * 0.5f + 0.5f) * imgSize.x,
                        imgMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * imgSize.y);

                    return true;
                };

            ImVec2 center;

            if (!project(glm::vec3(0.0f), center))
                continue;

            // Fora da tela com folga: nem desenha.
            if (center.x < imgMin.x - 400.0f || center.x > imgMin.x + imgSize.x + 400.0f ||
                center.y < imgMin.y - 400.0f || center.y > imgMin.y + imgSize.y + 400.0f)
                continue;

            const bool isActive = ((int)i == active);
            const bool isHovered = ((int)i == hoveredPrev);

            bool inGroup = false;
            if (group) {
                for (int g : *group) {
                    if (g == (int)i) { inGroup = true; break; }
                }
            }

            const bool isSelected = isActive || inGroup;

            const ImU32 col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(e.ShapeColor.r, e.ShapeColor.g, e.ShapeColor.b,
                    isSelected ? 1.0f : (isHovered ? 0.95f : 0.85f)));

            const float thick = isSelected ? 2.6f : (isHovered ? 2.2f : 1.6f);
            const float r = std::max(0.0001f, e.ShapeSize);

            // Quanto a forma ocupou na tela — e o raio do hit-test.
            float screenRadius = 6.0f;

            auto note = [&](const ImVec2& p)
                {
                    const float dx = p.x - center.x;
                    const float dy = p.y - center.y;
                    screenRadius = std::max(screenRadius, std::sqrt(dx * dx + dy * dy));
                };

            // Anel no plano definido por dois eixos locais.
            auto ring = [&](const glm::vec3& ax, const glm::vec3& ay, ImU32 c, float t)
                {
                    constexpr int kSeg = 28;

                    ImVec2 pts[kSeg];
                    bool ok = true;

                    for (int k = 0; k < kSeg; ++k)
                    {
                        const float a2 = (float)k / (float)kSeg * 6.2831853f;
                        const glm::vec3 lp = (ax * std::cos(a2) + ay * std::sin(a2)) * r;

                        if (!project(lp, pts[k])) { ok = false; break; }

                        note(pts[k]);
                    }

                    if (ok)
                        dl->AddPolyline(pts, kSeg, c, ImDrawFlags_Closed, t);
                };

            auto seg = [&](const glm::vec3& a2, const glm::vec3& b2, ImU32 c, float t)
                {
                    ImVec2 pa, pb;

                    if (!project(a2 * r, pa) || !project(b2 * r, pb))
                        return;

                    note(pa);
                    note(pb);

                    dl->AddLine(pa, pb, c, t);
                };

            const glm::vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);

            switch (e.Shape)
            {
            case RigControlShape::Sphere:
                // TRES aneis ortogonais. Um circulo so nunca vai parecer uma
                // esfera.
                ring(X, Z, col, thick);
                ring(X, Y, col, thick);
                ring(Y, Z, col, thick);
                break;

            case RigControlShape::Box:
            {
                const glm::vec3 c8[8] = {
                    {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                    {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1} };

                const int edges[12][2] = {
                    {0,1},{1,2},{2,3},{3,0},
                    {4,5},{5,6},{6,7},{7,4},
                    {0,4},{1,5},{2,6},{3,7} };

                for (const auto& ed2 : edges)
                    seg(c8[ed2[0]], c8[ed2[1]], col, thick);

                break;
            }

            case RigControlShape::Diamond:
            {
                // Octaedro: 6 vertices, 12 arestas.
                const glm::vec3 v6[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };

                const int edges[12][2] = {
                    {0,2},{2,1},{1,3},{3,0},
                    {0,4},{4,1},{1,5},{5,0},
                    {2,4},{4,3},{3,5},{5,2} };

                for (const auto& ed2 : edges)
                    seg(v6[ed2[0]], v6[ed2[1]], col, thick);

                break;
            }

            case RigControlShape::Arrow:
            {
                seg(glm::vec3(0.0f), Y, col, thick);

                // Ponta em V nos dois planos, para a seta ser legivel de
                // qualquer angulo.
                seg(Y, glm::vec3(0.25f, 0.7f, 0.0f), col, thick);
                seg(Y, glm::vec3(-0.25f, 0.7f, 0.0f), col, thick);
                seg(Y, glm::vec3(0.0f, 0.7f, 0.25f), col, thick);
                seg(Y, glm::vec3(0.0f, 0.7f, -0.25f), col, thick);
                break;
            }

            default:
                // Circle: anel no plano XZ (horizontal no espaco do controle) —
                // o formato de cinto que se usa em quadril e peito.
                ring(X, Z, col, thick);
                break;
            }

            if (anyBehind)
                continue;

            // Anel so no ATIVO. Ver a nota na assinatura: cinco aneis nao
            // dizem qual deles o gizmo esta manipulando.
            if (isActive)
                dl->AddCircle(center, screenRadius + 5.0f, IM_COL32(255, 255, 255, 190), 0, 1.2f);
            else if (inGroup)
                dl->AddCircle(center, screenRadius + 4.0f, IM_COL32(120, 190, 255, 150), 0, 1.0f);

            // Hit-test pelo raio REAL que a forma ocupou na tela. Um numero
            // fixo errava feio em formas grandes ou vistas de perfil.
            const float dx = mouse.x - center.x;
            const float dy = mouse.y - center.y;
            const float d2 = dx * dx + dy * dy;

            const float pick = std::max(10.0f, screenRadius + 4.0f);

            if (d2 <= pick * pick && d2 < hoveredDist)
            {
                hoveredDist = d2;
                hovered = (int)i;
            }
        }

        return hovered;
    }

} // namespace axe::ui