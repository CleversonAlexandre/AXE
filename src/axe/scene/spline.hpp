#pragma once

// src/axe/scene/spline.hpp
//
// ═══════════════════════════════════════════════════════════════════════════
//  CAMINHO NO MUNDO — a curva que uma camera (ou qualquer coisa) percorre
// ═══════════════════════════════════════════════════════════════════════════
//
// Catmull-Rom pelos pontos de controle, com TABELA DE COMPRIMENTO.
//
// ── POR QUE CATMULL-ROM ─────────────────────────────────────────────────────
//
// Ela PASSA pelos pontos de controle. Bezier com alcas daria mais controle e
// exigiria o dobro da interface — e, mais importante, mudaria o que o usuario
// ve: com Bezier os pontos que ele coloca sao imas, nao lugares por onde a
// camera passa. Para desenhar um caminho no espaco, "a camera passa por aqui"
// e a promessa certa.
//
// As alcas podem vir depois, por ponto, sem quebrar arquivo nenhum: um ponto
// sem alca continua sendo Catmull-Rom.
//
// ── A TABELA DE COMPRIMENTO NAO E OTIMIZACAO ────────────────────────────────
//
// Este e o ponto que decide se a feature presta.
//
// O parametro natural de uma spline (o `t` de cada segmento) NAO anda em
// velocidade constante. Dois pontos de controle proximos e dois distantes
// consomem o MESMO pedaco de `t`, entao percorrer `t` linearmente faz a camera
// arrastar-se no trecho curto e disparar no trecho longo. O sintoma e um
// tranco a cada ponto de controle — e o animador, olhando a curva de
// velocidade lisa que ele mesmo desenhou na timeline, nao tem como adivinhar
// de onde vem.
//
// A tabela mede a curva de verdade e permite perguntar por DISTANCIA: "onde
// estou aos 40% do CAMINHO", e nao "aos 40% do parametro". Ai a curva que o
// animador desenha na timeline e exatamente a velocidade que ele ve.

#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include <vector>

namespace axe
{
    class AXE_API SplinePath
    {
    public:
        // Reconstroi a tabela. `samplesPerSegment` e quantas amostras por
        // trecho entre dois pontos de controle: 16 e generoso para caminho de
        // camera e barato (uma curva de 8 pontos vira 128 amostras).
        void Build(const std::vector<glm::vec3>& points, bool closed,
            int samplesPerSegment = 16);

        bool  IsValid() const { return m_Samples.size() >= 2; }
        float Length()  const { return m_Length; }

        std::size_t PointCount() const { return m_Points.size(); }
        bool IsClosed() const { return m_Closed; }

        // ── POR DISTANCIA (o que a animacao usa) ────────────────────────────
        //
        // `u` em [0,1] e fracao do COMPRIMENTO, nao do parametro. Fora da
        // faixa e grampeado — numa curva aberta passar do fim significa parar
        // no fim, e nao extrapolar para o vazio.
        glm::vec3 PositionAt(float u) const;

        // Direcao de avanco, normalizada. Em curva degenerada (todos os pontos
        // no mesmo lugar) devolve +X, que e a mesma convencao de frente da
        // GameCamera — ver GameCamera::ForwardFromYawPitch.
        glm::vec3 TangentAt(float u) const;

        // ── POR PARAMETRO (o que o DESENHO usa) ─────────────────────────────
        //
        // `t` global em [0, numSegmentos]. Existe porque desenhar a curva no
        // viewport quer amostras uniformes no parametro — sao mais baratas e
        // ninguem percebe a diferenca numa linha.
        static glm::vec3 EvaluateParam(const std::vector<glm::vec3>& points,
            bool closed, float t);

    private:
        struct Sample
        {
            glm::vec3 Pos{ 0.0f };
            float     Distance = 0.0f;   // acumulada desde o inicio
        };

        std::vector<glm::vec3> m_Points;
        std::vector<Sample>    m_Samples;
        float                  m_Length = 0.0f;
        bool                   m_Closed = false;

        // Indice da amostra imediatamente ANTES da distancia dada, e o quanto
        // falta dali ate a proxima, em [0,1].
        void Locate(float distance, std::size_t& outIndex, float& outFrac) const;
    };

} // namespace axe