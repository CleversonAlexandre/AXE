#include "opengl_scene_height_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>

#include <cmath>

namespace axe
{
    // O valor que significa "nao ha geometria nesta coluna". Precisa ser bem
    // mais negativo que qualquer altura plausivel de cena, e ao mesmo tempo
    // representavel com folga em float — dai 1e9 e nao o infinito, que
    // atrapalharia a interpolacao de textura se alguem ligar filtro linear.
    static constexpr float k_NoHeight = -1.0e9f;

    // O mesmo papel, para a semente: um XZ absurdo que nenhuma cena alcanca.
    static constexpr float k_NoSeed = 1.0e9f;

    // ── 1. A render de topo ──────────────────────────────────────────────────
    //
    // O fragment escreve a ALTURA DE MUNDO, e nao a profundidade do buffer. A
    // profundidade seria um numero em espaco de recorte, que o material teria
    // de desfazer com a matriz inversa; a altura ja e o dado util.
    //
    // ── SCENE_HEIGHT_V3 — o TOPO nao basta, e essa foi a falha do desenho ──
    //
    //  Guardar so a altura do topo nao distingue as duas coisas que precisam
    //  ser distinguidas: uma rocha que SAI da agua e um cubo que PAIRA sobre
    //  ela. Nas duas o topo esta acima da lamina. Filtrar por topo rejeita a
    //  margem de verdade junto com o objeto suspenso.
    //
    //  O que separa as duas e o FUNDO: a rocha desce ate abaixo da agua, o cubo
    //  suspenso nao. Entao a coluna guarda os DOIS extremos.
    //
    //  Os dois saem do mesmo desenho, sem depth test nenhum: o alvo do topo usa
    //  equacao de blend GL_MAX e o do fundo GL_MIN, e cada fragmento vai
    //  empurrando o extremo da sua coluna. Sai mais barato que o depth buffer
    //  que estava aqui antes — e o depth so sabia achar UM dos dois extremos.
    static const char* s_HeightVertSrc = R"(
        #version 460 core
        layout(location = 0) in vec3 a_Position;
        uniform mat4 u_TopDown;
        uniform mat4 u_Model;
        out float v_WorldY;
        void main()
        {
            vec4 world = u_Model * vec4(a_Position, 1.0);
            v_WorldY   = world.y;
            gl_Position = u_TopDown * world;
        }
    )";

    static const char* s_HeightFragSrc = R"(
        #version 460 core
        layout(location = 0) out float o_Top;
        layout(location = 1) out float o_Bottom;
        layout(location = 2) out float o_Above;
        in float v_WorldY;
        uniform float u_SliceY;
        void main()
        {
            o_Top    = v_WorldY;
            o_Bottom = v_WorldY;

            // ── SCENE_HEIGHT_V6 — PARIDADE ───────────────────────────────────
            //
            //  Conta as superficies desta coluna que estao ACIMA da fatia. O
            //  epsilon evita que uma face exatamente na altura da lamina entre
            //  ou saia da conta por arredondamento.
            //
            //  Numero impar = a fatia esta DENTRO da malha. E a regra de
            //  paridade da voxelizacao solida, a mesma do stencil de sombra —
            //  e responde exatamente o que o teste de intervalo errava.
            o_Above = (v_WorldY > u_SliceY + 0.0001) ? 1.0 : 0.0;
        }
    )";

    // ── Triangulo de tela cheia sem VBO ──────────────────────────────────────
    //
    // Tres vertices gerados de gl_VertexID cobrindo a tela inteira. Um
    // TRIANGULO, e nao dois: sem a diagonal no meio, nao ha a costura de
    // quantizacao que dois triangulos criam na interpolacao.
    static const char* s_FullscreenVertSrc = R"(
        #version 460 core
        out vec2 v_UV;
        void main()
        {
            vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
            v_UV = p;
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
    )";

    // ── 2. Semeadura ─────────────────────────────────────────────────────────
    //
    // Onde ha geometria, a semente e o proprio XZ de mundo daquele texel. Onde
    // nao ha, um XZ inalcancavel.
    //
    // O XZ vem de DESPROJETAR a UV pela inversa da matriz de topo, em vez de
    // reconstruir com centro e extensao. E menos coisa para manter em sincronia:
    // a matriz ja e a fonte da verdade da regiao coberta, inclusive do encaixe
    // na grade de texel.
    static const char* s_SeedInitFragSrc = R"(
        #version 460 core
        layout(location = 0) out vec4 o_Seed;
        in vec2 v_UV;
        uniform sampler2D u_HeightMap;
        uniform sampler2D u_BottomMap;
        uniform mat4      u_InvTopDown;
        uniform float     u_NoHeight;
        uniform float     u_NoSeed;
        uniform sampler2D u_AboveMap;
        void main()
        {
            float top = texture(u_HeightMap, v_UV).r;

            // Coluna vazia: base impossivelmente ALTA e topo impossivelmente
            // BAIXO. Qualquer teste de "atravessa esta altura?" falha nos dois
            // lados — o sentinela cai no lado seguro sozinho, sem o material
            // precisar saber que ele existe.
            if (top <= u_NoHeight * 0.5) { o_Seed = vec4(u_NoSeed, u_NoSeed, -u_NoHeight, u_NoHeight); return; }

            // ── SCENE_HEIGHT_V4 — a semente leva o INTERVALO da coluna ───────
            //
            //  Base no .z, topo no .w. Nao e um detalhe: BASE SOZINHA NAO
            //  RESPONDE A PERGUNTA.
            //
            //  "A base esta abaixo da lamina" e verdade em toda a pegada de um
            //  plano afundado, e nao so na ponta que ainda sai da agua — o
            //  material lia a malha INTEIRA como margem. E um cilindro apoiado
            //  sobre um bloco submerso herda a base do bloco, entao ele tambem
            //  passava.
            //
            //  O que separa e o INTERVALO: a coluna vira margem quando a
            //  geometria ATRAVESSA a lamina — base abaixo E topo acima. O .w
            //  ja existia no formato e ia com 1.0 fixo; agora carrega o dado
            //  que faltava, sem textura nova e sem passe novo.
            float bottom = texture(u_BottomMap, v_UV).r;

            // ── SCENE_HEIGHT_V6 — a peneira e PARIDADE, nao intervalo ───────
            //
            //  A V5 semeava a coluna quando o intervalo [base, topo] abrangia a
            //  fatia. Isso e verdade tambem quando ha um objeto pairando alto
            //  sobre outro totalmente submerso: o intervalo da coluna abrange a
            //  lamina sem que nada de fato a atravesse. Era o defeito que
            //  sobrou.
            //
            //  A paridade responde certo: conte quantas superficies daquela
            //  coluna estao acima da fatia. Impar = a fatia esta dentro de
            //  alguma malha, e so entao aquela coluna e margem.
            //
            //    cubo meio submerso   -> 1 face acima  -> impar  -> margem
            //    cubo todo submerso   -> 0 faces acima -> par    -> nao
            //    cilindro pairando    -> 2 faces acima -> par    -> nao
            //    cilindro + submerso  -> 2 faces acima -> par    -> nao
            //
            //  E o mesmo raciocinio da voxelizacao solida por paridade, e nao
            //  custa passe nenhum: um terceiro alvo com blend aditivo, no mesmo
            //  desenho que ja produzia topo e fundo.
            float above = texture(u_AboveMap, v_UV).r;
            if (mod(above, 2.0) < 0.5)
            {
                o_Seed = vec4(u_NoSeed, u_NoSeed, -u_NoHeight, u_NoHeight);
                return;
            }

            vec4 w = u_InvTopDown * vec4(v_UV * 2.0 - 1.0, 0.0, 1.0);
            o_Seed = vec4(w.xz / w.w, bottom, top);
        }
    )";

    // ── 3. Jump flood ────────────────────────────────────────────────────────
    //
    //  Passe por passe, cada texel olha oito vizinhos a uma distancia que
    //  COMECA GRANDE e cai pela metade — resolucao/2, /4, /8, ate 1 — e fica
    //  com a semente mais proxima que encontrar.
    //
    //  Em log2(resolucao) passes, a informacao de "onde esta a geometria mais
    //  perto" atravessa a textura inteira. Sao ~10 desenhos de tela cheia num
    //  mapa de 1024, contra os 1024 passes que uma propagacao de um texel por
    //  vez custaria.
    static const char* s_JumpFloodFragSrc = R"(
        #version 460 core
        layout(location = 0) out vec4 o_Seed;
        in vec2 v_UV;
        uniform sampler2D u_SeedMap;
        uniform mat4      u_InvTopDown;
        uniform float     u_NoSeed;
        uniform int       u_Step;
        uniform int       u_Resolution;

        vec2 worldOf(vec2 uv)
        {
            vec4 w = u_InvTopDown * vec4(uv * 2.0 - 1.0, 0.0, 1.0);
            return w.xz / w.w;
        }

        void main()
        {
            vec2  here     = worldOf(v_UV);
            vec4  best     = texture(u_SeedMap, v_UV);
            float bestDist = (best.x >= u_NoSeed * 0.5)
                           ? 3.4e38
                           : dot(here - best.xy, here - best.xy);

            float texel = 1.0 / float(u_Resolution);
            float k     = float(u_Step) * texel;

            for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0) continue;

                vec2 uv = v_UV + vec2(float(dx), float(dy)) * k;
                if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;

                vec4 s = texture(u_SeedMap, uv);
                if (s.x >= u_NoSeed * 0.5) continue;

                float d = dot(here - s.xy, here - s.xy);
                if (d < bestDist) { bestDist = d; best = s; }
            }

            o_Seed = best;
        }
    )";

    OpenGLSceneHeightPass::~OpenGLSceneHeightPass()
    {
        if (m_HeightFBO)  glDeleteFramebuffers(1, &m_HeightFBO);
        if (m_SeedFBO[0]) glDeleteFramebuffers(2, m_SeedFBO);
        if (m_HeightTex)  glDeleteTextures(1, &m_HeightTex);
        if (m_BottomTex)  glDeleteTextures(1, &m_BottomTex);
        if (m_AboveTex)   glDeleteTextures(1, &m_AboveTex);
        if (m_SeedTex[0]) glDeleteTextures(2, m_SeedTex);
        if (m_EmptyVAO)   glDeleteVertexArrays(1, &m_EmptyVAO);
    }

    void OpenGLSceneHeightPass::Initialize(uint32_t resolution)
    {
        m_Resolution = resolution > 0 ? resolution : 1;

        // ── Altura: R32F ─────────────────────────────────────────────────────
        //
        // R32F, e nao R16F: half tem 11 bits de mantissa, e uma cena de 500 m
        // ja perde precisao de centimetro nas alturas grandes. O mapa e o
        // fundamento de tudo que vem depois; economizar bit aqui aparece como
        // degrau na linha da praia.
        //
        // O formato nao esta no enum do Framebuffer da engine (que so tem ate
        // RGBA16F), entao a textura e criada por GL cru com DSA — mesmo caminho
        // que o passe de SSAO ja usa para o R16F dele.
        glCreateTextures(GL_TEXTURE_2D, 1, &m_HeightTex);
        glTextureStorage2D(m_HeightTex, 1, GL_R32F, m_Resolution, m_Resolution);
        glTextureParameteri(m_HeightTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_HeightTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_HeightTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_HeightTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // ── SCENE_HEIGHT_V3 — a segunda altura ───────────────────────────────
        //
        // Mesmo formato, mesma coluna, extremo oposto. Custa 4 MB a 1024^2 —
        // menos que o depth buffer de 24 bits que saiu daqui.
        glCreateTextures(GL_TEXTURE_2D, 1, &m_BottomTex);
        glTextureStorage2D(m_BottomTex, 1, GL_R32F, m_Resolution, m_Resolution);
        glTextureParameteri(m_BottomTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_BottomTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_BottomTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_BottomTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // SCENE_HEIGHT_V6 — a contagem de superficies acima da fatia. R32F
        // porque o valor e um CONTADOR inteiro somado por blend: um formato
        // normalizado saturaria em 1 na segunda face e a paridade morreria.
        glCreateTextures(GL_TEXTURE_2D, 1, &m_AboveTex);
        glTextureStorage2D(m_AboveTex, 1, GL_R32F, m_Resolution, m_Resolution);
        glTextureParameteri(m_AboveTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_AboveTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_AboveTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_AboveTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glCreateFramebuffers(1, &m_HeightFBO);
        glNamedFramebufferTexture(m_HeightFBO, GL_COLOR_ATTACHMENT0, m_HeightTex, 0);
        glNamedFramebufferTexture(m_HeightFBO, GL_COLOR_ATTACHMENT1, m_BottomTex, 0);
        glNamedFramebufferTexture(m_HeightFBO, GL_COLOR_ATTACHMENT2, m_AboveTex, 0);

        // Sem isto so o anexo 0 recebe escrita — o layout(location=1) do
        // fragment cairia no vazio silenciosamente.
        const GLenum drawBuffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                        GL_COLOR_ATTACHMENT2 };
        glNamedFramebufferDrawBuffers(m_HeightFBO, 3, drawBuffers);

        // ── Sementes: RGBA32F ────────────────────────────────────────────────
        //
        // Guarda COORDENADA DE MUNDO, nao distancia. Assim o material calcula
        // a distancia contra a posicao real do pixel e o resultado sai continuo
        // — uma distancia ja resolvida em textura ficaria presa ao tamanho do
        // texel e apareceria em degraus.
        //
        // ── SCENE_HEIGHT_V2 — o terceiro canal ───────────────────────────────
        //
        // Alem do XZ, a ALTURA daquela semente viaja junto pelo jump flood.
        //
        // Sem ela, o mapa responde "ha geometria a 2 metros daqui" e cala sobre
        // uma coisa decisiva: se essa geometria esta na agua ou pairando cinco
        // metros acima. Um cubo suspenso semeia a coluna dele igual a um bloco
        // que toca a lamina, e a espuma aparece embaixo de algo que nao encosta
        // na agua.
        //
        // Com o terceiro canal, quem decide o que conta como margem e o GRAFO:
        // ele compara essa altura com a da propria superficie. O passe continua
        // so relatando fatos.
        glCreateTextures(GL_TEXTURE_2D, 2, m_SeedTex);
        glCreateFramebuffers(2, m_SeedFBO);
        for (int i = 0; i < 2; ++i)
        {
            glTextureStorage2D(m_SeedTex[i], 1, GL_RGBA32F, m_Resolution, m_Resolution);
            glTextureParameteri(m_SeedTex[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(m_SeedTex[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(m_SeedTex[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(m_SeedTex[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glNamedFramebufferTexture(m_SeedFBO[i], GL_COLOR_ATTACHMENT0, m_SeedTex[i], 0);
        }

        glCreateVertexArrays(1, &m_EmptyVAO);

        m_HeightShader = Shader::Create(s_HeightVertSrc, s_HeightFragSrc);
        m_SeedInitShader = Shader::Create(s_FullscreenVertSrc, s_SeedInitFragSrc);
        m_JumpFloodShader = Shader::Create(s_FullscreenVertSrc, s_JumpFloodFragSrc);

        m_Initialized = m_HeightShader && m_SeedInitShader && m_JumpFloodShader;

        if (!m_Initialized)
        {
            // Sem isto, a falha aparece bem mais tarde como agua sem espuma —
            // e ninguem liga uma coisa na outra.
            AXE_CORE_ERROR("[SCENE_HEIGHT_V5] os shaders do passe de altura nao "
                "compilaram; o mapa de topo fica desligado.");
        }
    }

    void OpenGLSceneHeightPass::Begin(const glm::mat4& topDownMatrix)
    {
        if (!m_Initialized) return;

        m_Matrix = topDownMatrix;
        m_InvMatrix = glm::inverse(topDownMatrix);

        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_SavedFBO);
        glGetIntegerv(GL_VIEWPORT, m_SavedViewport);

        glViewport(0, 0, (GLsizei)m_Resolution, (GLsizei)m_Resolution);
        glBindFramebuffer(GL_FRAMEBUFFER, m_HeightFBO);

        // Cada alvo limpa no extremo OPOSTO ao que vai buscar: o do topo comeca
        // no fundo do mundo para que qualquer fragmento vença o GL_MAX, o do
        // fundo comeca no teto para que qualquer fragmento vença o GL_MIN.
        const float clearTop = k_NoHeight;
        const float clearBottom = -k_NoHeight;
        const float clearAbove = 0.0f;
        glClearNamedFramebufferfv(m_HeightFBO, GL_COLOR, 0, &clearTop);
        glClearNamedFramebufferfv(m_HeightFBO, GL_COLOR, 1, &clearBottom);
        glClearNamedFramebufferfv(m_HeightFBO, GL_COLOR, 2, &clearAbove);

        // ── SCENE_HEIGHT_V3 — os dois extremos numa passada so ───────────────
        //
        // glBlendEquationi da uma equacao POR ALVO. O fragmento escreve a mesma
        // altura nos dois; o hardware guarda a maior num e a menor no outro.
        // Nao ha ordenacao a respeitar — max e min sao comutativos —, entao o
        // depth test some junto com o depth buffer.
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);          // ignorado por MIN/MAX, exigido pelo estado
        glBlendEquationi(0, GL_MAX);
        glBlendEquationi(1, GL_MIN);
        glBlendEquationi(2, GL_FUNC_ADD);   // SCENE_HEIGHT_V6 — soma, e a soma e a contagem

        // Sem descarte de face: uma malha aberta, ou com a normal invertida,
        // ainda ocupa aquela coluna XZ. Aqui a pergunta e "ha geometria?", e
        // nao "estou vendo a frente dela?".
        glDisable(GL_CULL_FACE);

        m_HeightShader->Bind();
        m_HeightShader->SetMat4("u_TopDown", glm::value_ptr(m_Matrix));
        m_HeightShader->SetFloat("u_SliceY", m_SliceY);   // SCENE_HEIGHT_V6
    }

    void OpenGLSceneHeightPass::DrawMesh(const Mesh& mesh, const glm::mat4& model)
    {
        if (!m_Initialized) return;

        m_HeightShader->SetMat4("u_Model", glm::value_ptr(model));
        mesh.GetVertexArray()->Bind();
        glDrawElements(GL_TRIANGLES, (GLsizei)mesh.GetIndexCount(),
            GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLSceneHeightPass::End()
    {
        if (!m_Initialized) return;

        // A ordem importa: o jump flood le o height map, que so esta completo
        // depois do ultimo DrawMesh — e ele nao quer o blend de MIN/MAX ligado.
        glDisable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);   // devolve o estado que o resto da engine assume

        RunJumpFlood();

        glEnable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_SavedFBO);
        glViewport(m_SavedViewport[0], m_SavedViewport[1],
            m_SavedViewport[2], m_SavedViewport[3]);
    }

    void OpenGLSceneHeightPass::RunJumpFlood()
    {
        // Os passes de tela cheia nao tem nada a ver com profundidade: escrever
        // no depth aqui so poluiria o proximo frame.
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(m_EmptyVAO);

        // ── Semeadura, no alvo 0 ─────────────────────────────────────────────
        glBindFramebuffer(GL_FRAMEBUFFER, m_SeedFBO[0]);
        m_SeedInitShader->Bind();
        glBindTextureUnit(0, m_HeightTex);
        glBindTextureUnit(1, m_BottomTex);
        glBindTextureUnit(2, m_AboveTex);
        m_SeedInitShader->SetInt("u_HeightMap", 0);
        m_SeedInitShader->SetInt("u_BottomMap", 1);
        m_SeedInitShader->SetMat4("u_InvTopDown", glm::value_ptr(m_InvMatrix));
        m_SeedInitShader->SetFloat("u_NoHeight", k_NoHeight);
        m_SeedInitShader->SetFloat("u_NoSeed", k_NoSeed);
        m_SeedInitShader->SetInt("u_AboveMap", 2);                 // SCENE_HEIGHT_V6
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ── Propagacao ───────────────────────────────────────────────────────
        //
        // O salto comeca na METADE da resolucao e cai pela metade a cada passe.
        // Comecar grande e o que faz o algoritmo ser log: o primeiro passe ja
        // leva informacao de um lado ao outro da textura, e os seguintes so
        // refinam. Comecar pequeno seria a propagacao ingenua, texel a texel.
        m_JumpFloodShader->Bind();
        m_JumpFloodShader->SetMat4("u_InvTopDown", glm::value_ptr(m_InvMatrix));
        m_JumpFloodShader->SetFloat("u_NoSeed", k_NoSeed);
        m_JumpFloodShader->SetInt("u_Resolution", (int)m_Resolution);

        int src = 0;
        for (int step = (int)m_Resolution / 2; step >= 1; step /= 2)
        {
            const int dst = 1 - src;

            glBindFramebuffer(GL_FRAMEBUFFER, m_SeedFBO[dst]);
            glBindTextureUnit(0, m_SeedTex[src]);
            m_JumpFloodShader->SetInt("u_SeedMap", 0);
            m_JumpFloodShader->SetInt("u_Step", step);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            src = dst;
        }

        // Onde parou e o resultado. Guardar o indice em vez de copiar para uma
        // textura fixa economiza um blit por frame.
        m_SeedFront = src;

        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }
}