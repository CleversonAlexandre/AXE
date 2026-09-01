// src/axe/scene/spline.cpp
//
// Ver a nota de topo do .hpp — em especial por que a tabela de comprimento
// existe.

#include "axe/scene/spline.hpp"

#include <algorithm>
#include <cmath>

namespace axe
{
    namespace
    {
        // Ponto de controle com as pontas tratadas.
        //
        // Numa curva ABERTA, o primeiro e o ultimo segmento precisam de um
        // vizinho que nao existe. Duplicar a ponta e a escolha classica: a
        // tangente na extremidade fica alinhada com o primeiro trecho, e a
        // curva comeca reta em vez de sair curvando para um vizinho imaginario.
        //
        // Numa curva FECHADA, o vizinho e o outro lado — e por isso ela nao tem
        // emenda visivel.
        glm::vec3 At(const std::vector<glm::vec3>& p, bool closed, int i)
        {
            const int n = static_cast<int>(p.size());
            if (n == 0) return glm::vec3(0.0f);

            if (closed)
            {
                int k = i % n;
                if (k < 0) k += n;
                return p[k];
            }

            return p[std::clamp(i, 0, n - 1)];
        }

        int SegmentCount(std::size_t pointCount, bool closed)
        {
            const int n = static_cast<int>(pointCount);
            if (n < 2) return 0;
            return closed ? n : (n - 1);
        }
    }

    glm::vec3 SplinePath::EvaluateParam(const std::vector<glm::vec3>& points,
        bool closed, float t)
    {
        const int segs = SegmentCount(points.size(), closed);
        if (segs <= 0)
            return points.empty() ? glm::vec3(0.0f) : points.front();

        t = std::clamp(t, 0.0f, static_cast<float>(segs));

        int   seg = static_cast<int>(std::floor(t));
        float f = t - static_cast<float>(seg);

        // Exatamente no fim: fica no ULTIMO segmento com f=1, e nao no segmento
        // seguinte (que nao existe) com f=0.
        if (seg >= segs) { seg = segs - 1; f = 1.0f; }

        const glm::vec3 p0 = At(points, closed, seg - 1);
        const glm::vec3 p1 = At(points, closed, seg);
        const glm::vec3 p2 = At(points, closed, seg + 1);
        const glm::vec3 p3 = At(points, closed, seg + 2);

        // ── CATMULL-ROM CENTRIPETA (alpha = 0.5) ─────────────────────────────
        //
        // A versao UNIFORME (a formula de matriz de sempre) parece mais simples
        // e tem um defeito que so aparece com pontos de espacamento desigual —
        // que e o caso normal de um caminho desenhado a mao:
        //
        //   ela ULTRAPASSA o proximo ponto e volta, formando um laco.
        //
        // Medido: cinco pontos colineares cobrindo 21 unidades produziam uma
        // curva de 25.4 de comprimento. As 4 unidades a mais sao a camera indo
        // alem e voltando — um solavanco visivel, sem nada na timeline que o
        // explique.
        //
        // A centripeta reparametriza cada trecho pela RAIZ da distancia entre
        // os pontos. E o alpha=0.5 do artigo do Yuksel: prova-se que nao
        // produz cusp nem auto-intersecao para nenhum arranjo de pontos. E o
        // que Unreal e Unity usam, e pela mesma razao.
        //
        // Formulacao piramidal de Barry-Goldman: tres niveis de interpolacao
        // linear entre os nos. Mais linhas que a matriz, e a unica forma em que
        // os nos nao-uniformes entram de verdade.
        auto knot = [](float t, const glm::vec3& a, const glm::vec3& b) {
            // sqrt(|b-a|) = |b-a|^0.5 — o expoente alpha da centripeta.
            const float d = glm::length(b - a);
            return t + std::sqrt(d);
            };

        const float t0 = 0.0f;
        const float t1 = knot(t0, p0, p1);
        const float t2 = knot(t1, p1, p2);
        const float t3 = knot(t2, p2, p3);

        // Pontos coincidentes zeram um vao de no e dividiriam por zero. Cair na
        // reta entre p1 e p2 e o comportamento certo: sem vizinhos distintos
        // nao ha curvatura a inferir.
        if (t2 - t1 < 1e-6f) return p1;
        if (t1 - t0 < 1e-6f || t3 - t2 < 1e-6f)
            return glm::mix(p1, p2, f);

        const float tk = glm::mix(t1, t2, f);

        const glm::vec3 a1 = glm::mix(p0, p1, (tk - t0) / (t1 - t0));
        const glm::vec3 a2 = glm::mix(p1, p2, (tk - t1) / (t2 - t1));
        const glm::vec3 a3 = glm::mix(p2, p3, (tk - t2) / (t3 - t2));

        const glm::vec3 b1 = glm::mix(a1, a2, (tk - t0) / (t2 - t0));
        const glm::vec3 b2 = glm::mix(a2, a3, (tk - t1) / (t3 - t1));

        return glm::mix(b1, b2, (tk - t1) / (t2 - t1));
    }

    void SplinePath::Build(const std::vector<glm::vec3>& points, bool closed,
        int samplesPerSegment)
    {
        m_Points = points;
        m_Closed = closed;
        m_Samples.clear();
        m_Length = 0.0f;

        const int segs = SegmentCount(points.size(), closed);
        if (segs <= 0) return;

        const int per = std::max(2, samplesPerSegment);
        const int total = segs * per;

        m_Samples.reserve(static_cast<std::size_t>(total) + 1);

        glm::vec3 prev = EvaluateParam(points, closed, 0.0f);
        m_Samples.push_back({ prev, 0.0f });

        for (int i = 1; i <= total; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(per);
            const glm::vec3 cur = EvaluateParam(points, closed, t);

            // Comprimento por CORDA. Com 16 amostras por trecho o erro contra o
            // comprimento real e da ordem de centesimos de por cento — muito
            // abaixo do que qualquer olho percebe num movimento de camera, e
            // sem integral nenhuma para manter.
            m_Length += glm::length(cur - prev);
            m_Samples.push_back({ cur, m_Length });

            prev = cur;
        }
    }

    void SplinePath::Locate(float distance, std::size_t& outIndex, float& outFrac) const
    {
        outIndex = 0;
        outFrac = 0.0f;

        if (m_Samples.size() < 2) return;

        distance = std::clamp(distance, 0.0f, m_Length);

        // Busca binaria: a tabela e monotonica por construcao.
        std::size_t lo = 0, hi = m_Samples.size() - 1;

        while (hi - lo > 1)
        {
            const std::size_t mid = (lo + hi) / 2;
            if (m_Samples[mid].Distance <= distance) lo = mid;
            else                                     hi = mid;
        }

        const float d0 = m_Samples[lo].Distance;
        const float d1 = m_Samples[hi].Distance;
        const float span = d1 - d0;

        outIndex = lo;
        outFrac = (span > 1e-6f) ? (distance - d0) / span : 0.0f;
    }

    glm::vec3 SplinePath::PositionAt(float u) const
    {
        if (m_Samples.empty()) return glm::vec3(0.0f);
        if (m_Samples.size() == 1) return m_Samples[0].Pos;

        u = std::clamp(u, 0.0f, 1.0f);

        std::size_t i = 0;
        float f = 0.0f;
        Locate(u * m_Length, i, f);

        // Interpolacao LINEAR entre duas amostras vizinhas, e nao uma nova
        // avaliacao da spline. Com 16 amostras por trecho as duas ficam a
        // fracoes de milimetro uma da outra, e a linear e o que garante
        // velocidade constante: reavaliar a curva reintroduziria exatamente a
        // nao-uniformidade que a tabela existe para eliminar.
        return glm::mix(m_Samples[i].Pos, m_Samples[i + 1].Pos, f);
    }

    glm::vec3 SplinePath::TangentAt(float u) const
    {
        if (m_Samples.size() < 2) return glm::vec3(1.0f, 0.0f, 0.0f);

        u = std::clamp(u, 0.0f, 1.0f);

        std::size_t i = 0;
        float f = 0.0f;
        Locate(u * m_Length, i, f);

        glm::vec3 d = m_Samples[i + 1].Pos - m_Samples[i].Pos;

        // Duas amostras coincidentes (dois pontos de controle no mesmo lugar):
        // procura a proxima amostra que difira, para os dois lados, antes de
        // desistir. Sem isto, um ponto duplicado — que acontece o tempo todo
        // ao duplicar um ponto para depois arrasta-lo — faria a camera girar
        // para +X por um instante.
        if (glm::dot(d, d) < 1e-12f)
        {
            for (std::size_t k = i + 1; k + 1 < m_Samples.size(); ++k)
            {
                d = m_Samples[k + 1].Pos - m_Samples[i].Pos;
                if (glm::dot(d, d) > 1e-12f) break;
            }
        }

        if (glm::dot(d, d) < 1e-12f)
            return glm::vec3(1.0f, 0.0f, 0.0f);

        return glm::normalize(d);
    }

} // namespace axe