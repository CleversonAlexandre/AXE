#include "opengl_post_process_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/material/material_shader_cache.hpp"   // POSTPROCESS_DOMAIN_V1
#include "axe/log/log.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace axe
{
    // --- Shaders ---

    static const char* s_QuadVert = R"(
        #version 460 core
        layout(location = 0) in vec2 a_Position;
        layout(location = 1) in vec2 a_TexCoord;
        out vec2 v_TexCoord;
        void main()
        {
            v_TexCoord  = a_TexCoord;
            gl_Position = vec4(a_Position, 0.0, 1.0);
        }
    )";

    // Tone mapping ACES + gamma
    static const char* s_TonemapFrag = R"(
        #version 460 core
        in vec2 v_TexCoord;
        out vec4 FragColor;
        uniform sampler2D u_HDRBuffer;
        uniform sampler2D u_BloomBuffer;
        uniform float     u_Exposure;
        uniform float     u_BloomIntensity;
        uniform int       u_HasBloom;
        uniform int       u_ToneMapMode; // 0 = Reinhard, 1 = ACES

        vec3 ACESFilm(vec3 x)
        {
            float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
            return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
        }

        void main()
        {
            vec3 hdr = texture(u_HDRBuffer, v_TexCoord).rgb;
            if (u_HasBloom == 1)
                hdr += texture(u_BloomBuffer, v_TexCoord).rgb * u_BloomIntensity;

            hdr *= u_Exposure;

            vec3 mapped;
            if (u_ToneMapMode == 1)
                mapped = ACESFilm(hdr);
            else
                mapped = hdr / (hdr + vec3(1.0)); // Reinhard

            mapped = pow(mapped, vec3(1.0 / 2.2));
            FragColor = vec4(mapped, 1.0);
        }
    )";

    // Extrai pixels acima do threshold
    static const char* s_BloomExtractFrag = R"(
        #version 460 core
        in vec2 v_TexCoord;
        out vec4 FragColor;
        uniform sampler2D u_HDRBuffer;
        uniform float     u_Threshold;
        void main()
        {
            vec3 color = texture(u_HDRBuffer, v_TexCoord).rgb;
            float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
            FragColor = brightness > u_Threshold ? vec4(color, 1.0) : vec4(0.0);
        }
    )";

    // Blur gaussiano separável
    static const char* s_BlurFrag = R"(
        #version 460 core
        in vec2 v_TexCoord;
        out vec4 FragColor;
        uniform sampler2D u_Image;
        uniform bool      u_Horizontal;
        const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
        void main()
        {
            vec2 texOffset = 1.0 / textureSize(u_Image, 0);
            vec3 result = texture(u_Image, v_TexCoord).rgb * weight[0];
            if (u_Horizontal)
                for (int i = 1; i < 5; i++)
                {
                    result += texture(u_Image, v_TexCoord + vec2(texOffset.x * i, 0.0)).rgb * weight[i];
                    result += texture(u_Image, v_TexCoord - vec2(texOffset.x * i, 0.0)).rgb * weight[i];
                }
            else
                for (int i = 1; i < 5; i++)
                {
                    result += texture(u_Image, v_TexCoord + vec2(0.0, texOffset.y * i)).rgb * weight[i];
                    result += texture(u_Image, v_TexCoord - vec2(0.0, texOffset.y * i)).rgb * weight[i];
                }
            FragColor = vec4(result, 1.0);
        }
    )";

    // --- Implementação ---

    OpenGLPostProcessPass::~OpenGLPostProcessPass()
    {
        if (m_QuadVAO) glDeleteVertexArrays(1, &m_QuadVAO);
        if (m_QuadVBO) glDeleteBuffers(1, &m_QuadVBO);
        glDeleteFramebuffers(2, m_BloomFBO);
        glDeleteTextures(2, m_BloomColorTex);

        // POSTPROCESS_DOMAIN_V1
        if (m_UserFBO) glDeleteFramebuffers(1, &m_UserFBO);
        if (m_UserTex) glDeleteTextures(1, &m_UserTex);
    }

    void OpenGLPostProcessPass::Initialize(uint32_t width, uint32_t height)
    {
        m_Width = width;
        m_Height = height;

        SetupQuad();
        SetupBloomBuffers(width, height);
        SetupUserBuffer(width, height);   // POSTPROCESS_DOMAIN_V1

        // Carimbo de versao — conferir NO LOG antes de investigar a imagem.
        AXE_CORE_INFO("POSTPROCESS_SKY_V1: efeito de tela cheia com G-Buffer + raio de camera + sol (ceu autoravel no grafo)");

        m_TonemapShader = Shader::Create(s_QuadVert, s_TonemapFrag);
        m_BloomExtractShader = Shader::Create(s_QuadVert, s_BloomExtractFrag);
        m_BlurShader = Shader::Create(s_QuadVert, s_BlurFrag);

        m_Initialized = true;
        //AXE_CORE_INFO("OpenGLPostProcessPass initialized ({}x{})", width, height);
    }

    void OpenGLPostProcessPass::Resize(uint32_t width, uint32_t height)
    {
        if (width == m_Width && height == m_Height) return;
        m_Width = width; m_Height = height;
        glDeleteFramebuffers(2, m_BloomFBO);
        glDeleteTextures(2, m_BloomColorTex);
        SetupBloomBuffers(width, height);

        // POSTPROCESS_DOMAIN_V1
        if (m_UserFBO) glDeleteFramebuffers(1, &m_UserFBO);
        if (m_UserTex) glDeleteTextures(1, &m_UserTex);
        m_UserFBO = 0; m_UserTex = 0;
        SetupUserBuffer(width, height);
    }

    void OpenGLPostProcessPass::SetupQuad()
    {
        float verts[] = {
            // pos        // uv
            -1.f,  1.f,  0.f, 1.f,
            -1.f, -1.f,  0.f, 0.f,
             1.f, -1.f,  1.f, 0.f,
            -1.f,  1.f,  0.f, 1.f,
             1.f, -1.f,  1.f, 0.f,
             1.f,  1.f,  1.f, 1.f,
        };
        glCreateVertexArrays(1, &m_QuadVAO);
        glCreateBuffers(1, &m_QuadVBO);
        glNamedBufferStorage(m_QuadVBO, sizeof(verts), verts, 0);
        glVertexArrayVertexBuffer(m_QuadVAO, 0, m_QuadVBO, 0, 4 * sizeof(float));
        // a_Position
        glEnableVertexArrayAttrib(m_QuadVAO, 0);
        glVertexArrayAttribFormat(m_QuadVAO, 0, 2, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(m_QuadVAO, 0, 0);
        // a_TexCoord
        glEnableVertexArrayAttrib(m_QuadVAO, 1);
        glVertexArrayAttribFormat(m_QuadVAO, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
        glVertexArrayAttribBinding(m_QuadVAO, 1, 0);
    }

    void OpenGLPostProcessPass::SetupBloomBuffers(uint32_t width, uint32_t height)
    {
        glCreateFramebuffers(2, m_BloomFBO);
        glCreateTextures(GL_TEXTURE_2D, 2, m_BloomColorTex);
        for (int i = 0; i < 2; i++)
        {
            glTextureStorage2D(m_BloomColorTex[i], 1, GL_RGBA16F, width, height);
            glTextureParameteri(m_BloomColorTex[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(m_BloomColorTex[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(m_BloomColorTex[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(m_BloomColorTex[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glNamedFramebufferTexture(m_BloomFBO[i], GL_COLOR_ATTACHMENT0,
                m_BloomColorTex[i], 0);
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  POSTPROCESS_DOMAIN_V1 — o efeito de tela inteira do usuario
    // ═════════════════════════════════════════════════════════════════════════

    void OpenGLPostProcessPass::SetupUserBuffer(uint32_t width, uint32_t height)
    {
        // RGBA16F, e nao RGBA8, mesmo servindo tambem ao ponto LDR: este mesmo
        // alvo e usado no ponto BeforeTonemap, onde a cor passa de 1.0 o tempo
        // todo. Um alvo de 8 bits ali cortaria o brilho ANTES do tone mapping —
        // e o tone mapping existe justamente para tratar dele.
        glCreateFramebuffers(1, &m_UserFBO);
        glCreateTextures(GL_TEXTURE_2D, 1, &m_UserTex);
        glTextureStorage2D(m_UserTex, 1, GL_RGBA16F, width, height);
        glTextureParameteri(m_UserTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_UserTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_UserTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_UserTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_UserFBO, GL_COLOR_ATTACHMENT0, m_UserTex, 0);

        // ── A MESMA LICAO DO VIEWPORT_RESIZE_V1, QUE EU NAO APLIQUEI AQUI ────
        //
        // glTextureStorage2D ALOCA e nao INICIALIZA. No VIEWPORT_RESIZE_V1 isso
        // foi resolvido dentro da classe Framebuffer — mas este alvo e criado
        // com GL cru, entao nasceu fora daquela protecao e voltou a exibir
        // memoria de video nao inicializada.
        //
        // Basta UM frame em que o efeito nao escreva o alvo inteiro — shader
        // que nao resolveu, material trocado, o frame seguinte a um resize —
        // para o tone mapping ler esse lixo. Era esse o chuvisco que voltou.
        //
        // Limpar aqui custa um clear por resize e fecha a porta de vez.
        const GLfloat black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        glClearNamedFramebufferfv(m_UserFBO, GL_COLOR, 0, black);
    }

    std::shared_ptr<Shader> OpenGLPostProcessPass::ResolveUserShader(const std::string& uuid)
    {
        if (uuid.empty())
        {
            m_UserShader.reset();
            m_UserSamplers.clear();
            m_UserShaderUUID.clear();
            m_UserShaderGen = 0;
            return nullptr;
        }

        // Mesmo UUID E mesma geracao do cache = nada mudou desde a ultima vez.
        //
        // A geracao e o que faz o botao Compile do Material Editor ter efeito
        // imediato aqui: ele chama MaterialShaderCache::Invalidate, a geracao
        // sobe, e este cache de cima se rende no proximo frame. Sem isso, o
        // usuario recompilaria o efeito e continuaria vendo o shader antigo ate
        // reabrir a cena — o cache mentindo, que e o unico jeito de um cache
        // piorar as coisas.
        const std::uint64_t gen = MaterialShaderCache::Generation();
        if (uuid == m_UserShaderUUID && gen == m_UserShaderGen) return m_UserShader;

        m_UserShaderUUID = uuid;
        m_UserShaderGen = gen;
        m_UserShader.reset();
        m_UserSamplers.clear();

        // Falhar aqui NAO e fatal: material ausente, ou pacote sem o
        // `.axeshader` cozido, apenas deixa o efeito de fora. A imagem sai como
        // saia antes — e nao preta, que e o que um "return" cedo demais na
        // cadeia produziria.
        if (!MaterialShaderCache::Resolve(uuid, CookedMaterialDomain::PostProcess,
            m_UserShader, m_UserSamplers))
        {
            AXE_CORE_WARN("PostProcess: material '{}' nao resolveu — efeito ignorado.", uuid);
            m_UserShader.reset();
        }

        return m_UserShader;
    }

    void OpenGLPostProcessPass::DrawUserEffect(uint32_t srcTex,
        const PostProcessSettings& s, bool isHDR)
    {
        if (!m_UserShader) return;

        m_UserShader->Bind();

        glBindTextureUnit(0, srcTex);
        m_UserShader->SetInt("u_SceneColor", 0);
        m_UserShader->SetFloat2("u_ScreenSize", glm::vec2((float)m_Width, (float)m_Height));
        m_UserShader->SetFloat("u_Intensity", s.UserIntensity);
        m_UserShader->SetInt("u_IsHDR", isHDR ? 1 : 0);

        // ── BUG DA RODADA ANTERIOR, CORRIGIDO AQUI ───────────────────────────
        //
        // u_CameraPosition e u_Time eram DECLARADAS no shader gerado e nunca
        // enviadas. O node Scene Depth usa u_CameraPosition — com ela em
        // (0,0,0), a "distancia da camera" era na verdade distancia da ORIGEM
        // DO MUNDO. Deteccao de borda ainda funcionava (compara vizinhos), mas
        // o limiar relativo ficava errado, e o valor exposto ao usuario era
        // simplesmente falso.
        //
        // Uniform declarada e nao enviada nao da erro: o GLSL a inicializa com
        // zero e segue em frente. E por isso que este tipo de bug so aparece
        // quando alguem olha o numero.
        m_UserShader->SetFloat3("u_CameraPosition", m_Camera.CameraPosition);
        m_UserShader->SetFloat("u_Time", m_Camera.TimeSeconds);

        // POSTPROCESS_SKY_V1 — camera e sol, para o efeito poder desenhar o ceu.
        m_UserShader->SetMat4("u_InvViewProjection",
            glm::value_ptr(m_Camera.InvViewProjection));
        m_UserShader->SetFloat3("u_SunDirection", m_Camera.SunDirection);
        m_UserShader->SetFloat3("u_SunColor", m_Camera.SunColor);
        m_UserShader->SetFloat("u_SunIntensity", m_Camera.SunIntensity);

        // ── POSTPROCESS_GBUFFER_V1 — o G-Buffer nas unidades 1, 2 e 3 ────────
        //
        // Ficam em unidades FIXAS, e nao logo apos os samplers do grafo: o GLSL
        // gerado declara u_ScenePosition/u_SceneNormal/u_ScenePBR sempre, tenha
        // o material usado ou nao, e o numero de texturas do grafo varia por
        // material. Unidade fixa aqui e unidade fixa la, e nunca se encontram.
        //
        // Textura 0 (G-Buffer ausente) e um caso legitimo: o preview do
        // Material Editor roda FORWARD e nao tem G-Buffer. O bind de 0 devolve
        // preto na amostragem, e o node de cena ja compila com fallback — o
        // efeito aparece degradado no preview em vez de nao compilar.
        glBindTextureUnit(1, m_ScenePositionTex);
        glBindTextureUnit(2, m_SceneNormalTex);
        glBindTextureUnit(3, m_ScenePBRTex);
        m_UserShader->SetInt("u_ScenePosition", 1);
        m_UserShader->SetInt("u_SceneNormal", 2);
        m_UserShader->SetInt("u_ScenePBR", 3);
        m_UserShader->SetInt("u_HasSceneBuffers", m_SceneNormalTex != 0 ? 1 : 0);

        // Texturas do grafo (node Texture Sample dentro do efeito) comecam na
        // unidade 4 — 0 e a cor da cena, 1..3 sao o G-Buffer.
        uint32_t unit = 4;
        for (auto& kv : m_UserSamplers)
        {
            if (!kv.second) continue;
            kv.second->Bind(unit);
            m_UserShader->SetInt(kv.first, (int)unit);
            unit++;
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    void OpenGLPostProcessPass::Execute(uint32_t hdrColorID,
        const PostProcessSettings& settings)
    {

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(m_QuadVAO);

        uint32_t bloomTex = 0;

        GLint destFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destFBO);

        // ── DIAGNÓSTICO TEMPORÁRIO — remover depois ──
        //{
        //    static double s_LastLogTime = 0.0;
        //    double now = glfwGetTime();
        //    if (now - s_LastLogTime > 1.0)
        //    {
        //        s_LastLogTime = now;
        //        AXE_CORE_INFO("[DIAG-PP] hdrColorID={} destFBO={} m_Width={} m_Height={} exposure={}",
        //            hdrColorID, destFBO, m_Width, m_Height, settings.Exposure);
        //    }
        //}


        // --- Bloom ---
        if (settings.BloomEnabled)
        {
            // 1. Extrai brilho
            glBindFramebuffer(GL_FRAMEBUFFER, m_BloomFBO[0]);
            glViewport(0, 0, m_Width, m_Height);
            m_BloomExtractShader->Bind();
            glBindTextureUnit(0, hdrColorID);
            m_BloomExtractShader->SetInt("u_HDRBuffer", 0);
            m_BloomExtractShader->SetFloat("u_Threshold", settings.BloomThreshold);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            // 2. Blur ping-pong
            bool horizontal = true;
            m_BlurShader->Bind();
            for (int i = 0; i < settings.BloomBlurPasses * 2; i++)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, m_BloomFBO[horizontal ? 1 : 0]);
                m_BlurShader->SetBool("u_Horizontal", horizontal);
                glBindTextureUnit(0, m_BloomColorTex[horizontal ? 0 : 1]);
                m_BlurShader->SetInt("u_Image", 0);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                horizontal = !horizontal;
            }
            bloomTex = m_BloomColorTex[0];
        }

        // ── POSTPROCESS_DOMAIN_V1 — efeito do usuario ANTES do tone mapping ──
        //
        // A cor aqui ainda e HDR linear. O efeito le `hdrColorID`, escreve no
        // alvo intermediario, e a partir dai o tone mapping le O RESULTADO —
        // e nao o original. E o que faz um grading feito aqui ser respeitado
        // pela curva, em vez de ser desfeito por ela.
        uint32_t tonemapInput = hdrColorID;

        const bool hasUserEffect =
            ResolveUserShader(settings.UserMaterialUUID) != nullptr;

        if (hasUserEffect &&
            settings.UserBlendPoint == PostProcessBlendPoint::BeforeTonemap)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_UserFBO);
            glViewport(0, 0, m_Width, m_Height);
            DrawUserEffect(hdrColorID, settings, /*isHDR*/ true);
            tonemapInput = m_UserTex;
        }

        // ── Tone Mapping ─────────────────────────────────────────────────────
        //
        // O destino deixou de ser sempre `destFBO`: quando ha efeito DEPOIS do
        // tone mapping, o resultado tem de virar TEXTURA para o efeito poder
        // le-la. Ler e escrever no mesmo alvo e comportamento indefinido, e
        // `destFBO` pode nem ser uma textura que se possa amostrar.
        const bool userAfter = hasUserEffect &&
            settings.UserBlendPoint == PostProcessBlendPoint::AfterTonemap;

        glBindFramebuffer(GL_FRAMEBUFFER, userAfter ? m_UserFBO : (uint32_t)destFBO);
        glViewport(0, 0, m_Width, m_Height);
        m_TonemapShader->Bind();
        glBindTextureUnit(0, tonemapInput);
        m_TonemapShader->SetInt("u_HDRBuffer", 0);
        m_TonemapShader->SetFloat("u_Exposure", settings.Exposure);
        m_TonemapShader->SetFloat("u_BloomIntensity", settings.BloomIntensity);
        m_TonemapShader->SetInt("u_ToneMapMode", settings.ToneMapMode);
        if (bloomTex)
        {
            glBindTextureUnit(1, bloomTex);
            m_TonemapShader->SetInt("u_BloomBuffer", 1);
            m_TonemapShader->SetInt("u_HasBloom", 1);
        }
        else
        {
            m_TonemapShader->SetInt("u_HasBloom", 0);
        }
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // ── POSTPROCESS_DOMAIN_V1 — efeito do usuario DEPOIS do tone mapping ─
        //
        // A cor aqui e o pixel final, 0..1, ja em gamma. E o ponto do
        // estilizado: pixelizacao, posterizacao, scanline, vinheta. Este passe
        // e quem escreve em `destFBO`, fechando a cadeia.
        if (userAfter)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, destFBO);
            glViewport(0, 0, m_Width, m_Height);
            DrawUserEffect(m_UserTex, settings, /*isHDR*/ false);
        }

        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);
    }
}