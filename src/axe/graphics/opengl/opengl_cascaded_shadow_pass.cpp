#include "opengl_cascaded_shadow_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>

namespace axe
{
    static const char* s_VS = R"(
#version 460 core
layout(location = 0) in vec3 a_Position;
uniform mat4 u_LightSpaceMatrix;
uniform mat4 u_Model;
void main() { gl_Position = u_LightSpaceMatrix * u_Model * vec4(a_Position, 1.0); }
)";

    static const char* s_FS = R"(
#version 460 core
void main() {}
)";

    OpenGLCascadedShadowPass::~OpenGLCascadedShadowPass()
    {
        if (m_FBO)            glDeleteFramebuffers(1, &m_FBO);
        if (m_DepthArrayTex)  glDeleteTextures(1, &m_DepthArrayTex);
        if (m_CompareSampler) glDeleteSamplers(1, &m_CompareSampler);
        if (m_RawSampler)     glDeleteSamplers(1, &m_RawSampler);
    }

    void OpenGLCascadedShadowPass::Initialize(uint32_t resolution)
    {
        m_Resolution = resolution;

        // Carimbo de versao — conferir NO LOG antes de investigar a sombra.
        AXE_CORE_INFO("SHADOW_HWPCF_V1: cascades por esfera + snap + PCF de hardware ({}px)",
            resolution);

        // Texture array — uma layer por cascade
        glGenTextures(1, &m_DepthArrayTex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_DepthArrayTex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
            resolution, resolution, AXE_SHADOW_CASCADES,
            0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

        // ═════════════════════════════════════════════════════════════════
        //  SHADOW_HWPCF_V1 — O FILTRO QUE ESTAVA ERRADO
        //
        //  Aqui estava GL_LINEAR, e o shader lia com sampler2DArray + `.r`.
        //  Isso INTERPOLA PROFUNDIDADES CRUAS: a media entre 12 m e 40 m da
        //  26 m, que nao e onde ha superficie nenhuma. O valor devolvido nao
        //  descreve a cena, e o erro nao aparece como ruido — aparece como
        //  gradiente suave e errado, que e metade do "efeito de agua" que se
        //  ve no chao longe.
        //
        //  Filtrar so faz sentido DEPOIS da comparacao: "3 dos 4 texels
        //  vizinhos estao na frente" = 0.75 de sombra, e isso sim significa
        //  alguma coisa. E exatamente o que a unidade de textura faz quando
        //  o modo de comparacao esta ligado.
        //
        //  O default da TEXTURA vira NEAREST: e o que vale para quem bindar
        //  o array sem sampler object. Os dois modos de verdade vem dos
        //  sampler objects abaixo.
        // ═════════════════════════════════════════════════════════════════
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[] = { 1.f, 1.f, 1.f, 1.f };
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);

        // ── Sampler object 1: comparacao no hardware ──────────────────────
        //
        // LEQUAL devolve 1.0 quando a referencia esta ATRAS ou junto do que
        // foi gravado, ou seja 1.0 = ILUMINADO. O shader inverte.
        //
        // A borda branca (profundidade 1.0) faz todo ponto fora do mapa
        // passar no teste e sair iluminado — e o que impede a cascade de
        // pintar uma caixa escura em volta de si mesma.
        glGenSamplers(1, &m_CompareSampler);
        glSamplerParameteri(m_CompareSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_CompareSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_CompareSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_CompareSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glSamplerParameterfv(m_CompareSampler, GL_TEXTURE_BORDER_COLOR, border);
        glSamplerParameteri(m_CompareSampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_CompareSampler, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        // ── Sampler object 2: profundidade crua ───────────────────────────
        //
        // NEAREST de proposito, e sem comparacao: a busca de bloqueador do
        // PCSS le a profundidade como NUMERO (a que distancia esta quem
        // bloqueia), e interpolar esse numero e o erro que acabamos de tirar.
        glGenSamplers(1, &m_RawSampler);
        glSamplerParameteri(m_RawSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(m_RawSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(m_RawSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_RawSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glSamplerParameterfv(m_RawSampler, GL_TEXTURE_BORDER_COLOR, border);
        glSamplerParameteri(m_RawSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);

        glGenFramebuffers(1, &m_FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        m_Shader = Shader::Create(s_VS, s_FS);
        m_Initialized = true;

        AXE_CORE_INFO("OpenGLCascadedShadowPass: {} cascades @ {}x{}.",
            AXE_SHADOW_CASCADES, resolution, resolution);
    }

    // Calcula a matriz de luz que envolve o sub-frustum entre nearSplit e farSplit
    glm::mat4 OpenGLCascadedShadowPass::ComputeCascadeMatrix(
        const glm::vec3& lightDir,
        const glm::mat4& cameraView,
        const glm::mat4& cameraProj,
        float nearSplit, float farSplit,
        float* outTexelWorldSize,
        float* outDepthRange)
    {
        // Reconstrói a projeção só pra esta faixa de profundidade
        glm::mat4 proj = cameraProj;
        // Extrai fov/aspect da projeção original
        float invP00 = 1.0f / cameraProj[0][0];
        float invP11 = 1.0f / cameraProj[1][1];

        // 8 cantos do sub-frustum em NDC
        glm::mat4 invViewProj = glm::inverse(
            glm::perspective(2.0f * atan(invP11), invP11 / invP00, nearSplit, farSplit)
            * cameraView);

        std::vector<glm::vec4> corners;
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                for (int z = 0; z < 2; ++z)
                {
                    glm::vec4 pt = invViewProj * glm::vec4(
                        2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                    corners.push_back(pt / pt.w);
                }

        // ═════════════════════════════════════════════════════════════════
        //  CSM_STABLE_V1 — POR QUE A SOMBRA "NADAVA" AO GIRAR A CAMERA
        //
        //  O codigo anterior fazia o obvio: pegava os 8 cantos do sub-frustum,
        //  levava para o espaco da luz e ajustava uma CAIXA ALINHADA AOS EIXOS
        //  (AABB) em volta deles.
        //
        //  O problema e que essa caixa MUDA DE TAMANHO conforme a camera gira.
        //  Um frustum visto de um angulo tem AABB maior que o mesmo frustum
        //  visto de outro — e o shadow map tem resolucao fixa. Entao, a cada
        //  grau que a camera girava, cada texel da sombra passava a cobrir uma
        //  area diferente do mundo, e a borda da sombra se reorganizava. E isso
        //  que se ve como sombra "se mexendo sozinha", e e o que faz ela
        //  parecer artificial: sombra de verdade nao depende de onde voce esta.
        //
        //  ── AS DUAS CORRECOES, E POR QUE PRECISA DAS DUAS ─────────────────
        //
        //  1. ESFERA, e nao AABB. O raio da esfera que envolve o frustum e
        //     INVARIANTE A ROTACAO — gire a camera como quiser, a esfera tem o
        //     mesmo tamanho. Com extensao constante, a escala de mundo por
        //     texel para de mudar. Custa area: a esfera e maior que o AABB
        //     apertado, entao a resolucao efetiva cai um pouco. E o preco
        //     padrao de CSM estavel, e vale.
        //
        //  2. SNAP DE TEXEL. So a esfera nao basta: ao ANDAR, o centro se
        //     desloca continuamente e os texels escorregam por baixo da
        //     geometria — a borda "formiga". A correcao e quantizar a origem
        //     da projecao a incrementos de texel inteiro, para o mapa andar aos
        //     saltos de um texel em vez de deslizar.
        //
        //  Uma sem a outra deixa metade do problema: so esfera continua
        //  formigando ao andar; so snap continua nadando ao girar.
        // ═════════════════════════════════════════════════════════════════

        glm::vec3 center(0.0f);
        for (auto& c : corners) center += glm::vec3(c);
        center /= (float)corners.size();

        // Raio da esfera envolvente.
        float radius = 0.0f;
        for (auto& c : corners)
            radius = glm::max(radius, glm::length(glm::vec3(c) - center));

        // Quantizado: sem isto, uma variacao minuscula do raio entre frames
        // (o near/far do split muda com o aspect) faria a escala pulsar de
        // novo, so que de leve — o suficiente para o olho pegar.
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // SHADOW_ACNE_V1 — a caixa cobre 2*radius e tem m_Resolution texeis.
        if (outTexelWorldSize)
            *outTexelWorldSize = (2.0f * radius) / (float)m_Resolution;

        glm::vec3 dir = glm::normalize(lightDir);

        // Up degenerado: com o sol a pino (dir quase paralelo a (0,1,0)) o
        // lookAt produz uma matriz invalida e a cascade inteira some. Nao e
        // caso raro — e meio-dia no ciclo de Time of Day.
        glm::vec3 up = (glm::abs(dir.y) > 0.99f)
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);

        // A camera de luz recua o RAIO INTEIRO (mais a folga do zMult) para
        // que casters ATRAS do frustum — o predio que projeta sombra sobre a
        // rua que voce ve — ainda entrem no mapa.
        const float zMult = 5.0f;
        const float backOff = radius * zMult;

        glm::mat4 lightView = glm::lookAt(center - dir * backOff, center, up);

        // Caixa de extensao CONSTANTE: e aqui que a estabilidade acontece.
        // PCSS_V1 — o mesmo alcance que a ortho abaixo usa. Guardado porque a
        // profundidade lida do shadow map e 0..1 sobre ESTE intervalo, e o
        // PCSS precisa da distancia em METROS entre bloqueador e receptor.
        if (outDepthRange)
            *outDepthRange = backOff + radius;

        glm::mat4 lightProj = glm::ortho(
            -radius, radius,
            -radius, radius,
            0.0f, backOff + radius);

        // ── Snap de texel ─────────────────────────────────────────────────
        //
        // Leva a origem do mundo ao espaco da sombra, mede em TEXELS, arredonda
        // para o inteiro mais proximo, e devolve a sobra como translacao na
        // projecao. Depois disto o mapa so anda em multiplos de um texel.
        //
        // Z e W ficam de fora: mexer neles deslocaria a profundidade e trocaria
        // o formigamento por acne de sombra.
        {
            const float texelsPerSide = (float)m_Resolution;

            glm::mat4 shadowMatrix = lightProj * lightView;
            glm::vec4 origin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            origin *= texelsPerSide * 0.5f;

            glm::vec4 rounded = glm::round(origin);
            glm::vec4 offset = (rounded - origin) * (2.0f / texelsPerSide);
            offset.z = 0.0f;
            offset.w = 0.0f;

            lightProj[3] += offset;
        }

        return lightProj * lightView;
    }

    void OpenGLCascadedShadowPass::ComputeCascades(
        const glm::vec3& lightDir,
        const glm::mat4& cameraView,
        const glm::mat4& cameraProj,
        float cameraNear, float cameraFar)
    {
        // Practical Split Scheme (Zhang et al.) — mistura de log e uniforme
        float splits[AXE_SHADOW_CASCADES + 1];
        splits[0] = cameraNear;
        for (int i = 1; i <= AXE_SHADOW_CASCADES; ++i)
        {
            float p = (float)i / AXE_SHADOW_CASCADES;
            float logSplit = cameraNear * pow(cameraFar / cameraNear, p);
            float uniformSplit = cameraNear + (cameraFar - cameraNear) * p;
            splits[i] = m_CascadeSplitLambda * logSplit +
                (1.0f - m_CascadeSplitLambda) * uniformSplit;
        }

        for (int i = 0; i < AXE_SHADOW_CASCADES; ++i)
        {
            m_Cascades[i].LightSpaceMatrix = ComputeCascadeMatrix(
                lightDir, cameraView, cameraProj, splits[i], splits[i + 1],
                &m_Cascades[i].TexelWorldSize,
                &m_Cascades[i].DepthRange);
            m_Cascades[i].SplitDepth = splits[i + 1];
        }
    }

    void OpenGLCascadedShadowPass::Begin(int cascadeIndex)
    {
        // Salva o alvo atual pra devolver no End() — quem clobbera estado,
        // restaura estado (ver comentário no .hpp)
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_SavedFBO);
        glGetIntegerv(GL_VIEWPORT, m_SavedViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        // Anexa a layer específica do texture array
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            m_DepthArrayTex, 0, cascadeIndex);
        glViewport(0, 0, m_Resolution, m_Resolution);
        glClear(GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);

        // ═════════════════════════════════════════════════════════════════
        //  SHADOW_ACNE_V1 — POR QUE O CULL DE FRENTE SAIU
        //
        //  `glCullFace(GL_FRONT)` e o truque classico contra peter-panning: em
        //  malha FECHADA, desenhar so as faces de tras empurra a superficie de
        //  comparacao para dentro do objeto e a acne desaparece de graca.
        //
        //  A palavra que importa e FECHADA. Numa cena feita de PLANOS — chao,
        //  parede, telhado, que e do que esta cena e feita — a face virada para
        //  a luz e a UNICA que existe. Cortando ela, o plano nao se registra no
        //  shadow map, e a comparacao passa a ser contra o que estiver atras
        //  dele. O resultado sao as faixas claras onduladas na parede: acne
        //  pura, so que causada pelo remedio.
        //
        //  Sem culling, os dois lados entram. O peter-panning que o cull
        //  evitava passa a ser tratado pelo polygon offset abaixo e pelo
        //  normal offset no lighting pass — que sao as ferramentas certas para
        //  isso, e funcionam em malha fechada E em plano.
        // ═════════════════════════════════════════════════════════════════
        glDisable(GL_CULL_FACE);

        // Bias de PROFUNDIDADE proporcional a INCLINACAO do triangulo, aplicado
        // na hora de gravar o mapa.
        //
        // E o lugar certo para ele: o hardware conhece a derivada real da
        // profundidade naquele triangulo, coisa que o shader de iluminacao so
        // consegue estimar. Um bias constante no sample e sempre errado —
        // pouco na superficie de frente, muito na rasante.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);

        m_Shader->Bind();
        m_Shader->SetMat4("u_LightSpaceMatrix",
            glm::value_ptr(m_Cascades[cascadeIndex].LightSpaceMatrix));
    }

    void OpenGLCascadedShadowPass::DrawMesh(const Mesh& mesh, const glm::mat4& model)
    {
        m_Shader->SetMat4("u_Model", glm::value_ptr(model));
        mesh.GetVertexArray()->Bind();
        glDrawElements(GL_TRIANGLES, mesh.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLCascadedShadowPass::End()
    {
        // SHADOW_ACNE_V1 — quem liga, desliga. O polygon offset e estado
        // GLOBAL: deixado ligado, ele deslocaria a profundidade do G-Buffer no
        // passe seguinte, e o sintoma seria em outro lugar.
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_SavedFBO);
        glViewport(m_SavedViewport[0], m_SavedViewport[1],
            m_SavedViewport[2], m_SavedViewport[3]);
    }

} // namespace axe