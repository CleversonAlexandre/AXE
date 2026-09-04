#include "opengl_ssao_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>
#include <random>

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

    static const char* s_SSAOFrag = R"(
    #version 460 core
    out float FragColor;
    in vec2 v_TexCoord;

    uniform sampler2D u_Position;   // world space
    uniform sampler2D u_Normal;     // world space
    uniform sampler2D u_Noise;
    uniform mat4      u_Projection;
    uniform mat4      u_View;       // world → view para projetar amostras
    uniform vec3      u_Samples[64];
    uniform int       u_KernelSize;
    uniform float     u_Radius;
    uniform float     u_Bias;
    uniform float     u_Power;
    uniform vec2      u_NoiseScale;

    void main()
    {
        vec3 fragPosWorld = texture(u_Position, v_TexCoord).rgb;
        vec3 rawNormal    = texture(u_Normal, v_TexCoord).rgb;

        // ── SSAO_ROBUST_V1 — PIXEL SEM GEOMETRIA ──────────────────────────
        //
        // No G-Buffer, ceu e vazio tem normal (0,0,0). `normalize` de vetor
        // nulo e INDEFINIDO em GLSL: na pratica sai NaN, e a partir dai o TBN,
        // as amostras e a oclusao viram lixo — que o blur depois espalha por
        // cima da silhueta dos objetos. E parte dos "pequenos artefatos" que
        // sobraram: eles moram na borda entre objeto e ceu.
        //
        // 1.0 = sem oclusao, que e a resposta certa para "aqui nao ha
        // superficie para ocluir".
        if (dot(rawNormal, rawNormal) < 0.01)
        {
            FragColor = 1.0;
            return;
        }

        vec3 normal = normalize(rawNormal);
        vec3 noise  = normalize(texture(u_Noise, v_TexCoord * u_NoiseScale).rgb);

        // TBN em world space
        vec3 tangent   = normalize(noise - normal * dot(noise, normal));
        vec3 bitangent = cross(normal, tangent);
        mat3 TBN       = mat3(tangent, bitangent, normal);

        float occlusion = 0.0;
        for (int i = 0; i < u_KernelSize; i++)
        {
            // Amostra em world space
            vec3 samplePos = fragPosWorld + TBN * u_Samples[i] * u_Radius;

            // Converte para clip space para fazer o lookup no G-Buffer
            vec4 offset = u_Projection * u_View * vec4(samplePos, 1.0);
            offset.xyz /= offset.w;
            offset.xyz  = offset.xyz * 0.5 + 0.5;

            // ── SSAO_ROBUST_V1 — AMOSTRA FORA DA TELA ─────────────────────
            //
            // Amostra que cai fora do viewport lia o texel da borda (clamp) e
            // devolvia uma profundidade que nao tem nada a ver com aquele
            // ponto — oclusao inventada nas bordas do viewport, que APARECE E
            // SOME conforme a camera se move.
            if (offset.x < 0.0 || offset.x > 1.0 ||
                offset.y < 0.0 || offset.y > 1.0)
                continue;

            // Profundidade real da geometria naquele pixel
            vec3 samplePosReal = texture(u_Position, offset.xy).rgb;

            // Amostra que caiu no CEU nao oclui nada. Sem isto, o (0,0,0) do
            // fundo era lido como um ponto na origem do mundo — muitas vezes
            // mais perto que o fragmento — e produzia uma auréola escura em
            // volta de tudo que se recorta contra o ceu.
            vec3 sampleNormal = texture(u_Normal, offset.xy).rgb;
            if (dot(sampleNormal, sampleNormal) < 0.01)
                continue;

            // ── SSAO_QUALITY_V2 — profundidade em Z DE VIEW ────────────────
            //
            // Era `length(viewPos)`, a distancia radial ate a camera. Para um
            // mesmo plano, essa distancia CRESCE do centro para as bordas da
            // tela: o range check ficava mais frouxo nos cantos e mais
            // apertado no meio, e a oclusao do mesmo chao mudava conforme a
            // camera girava. Z de view e constante para um plano paralelo ao
            // filme, que e a comparacao que o algoritmo pressupoe.
            float sampleDepth = -(u_View * vec4(samplePosReal, 1.0)).z;
            float fragDepth   = -(u_View * vec4(samplePos,     1.0)).z;
            float centerDepth = -(u_View * vec4(fragPosWorld,  1.0)).z;

            // Bias proporcional a distancia. Bias fixo em metros e cego a
            // perspectiva: o valor que evita acne a dois metros e insuficiente
            // a trinta, onde um texel de profundidade cobre muito mais mundo.
            // O termo constante mantem o comportamento de quem ja calibrou o
            // slider; o proporcional e o piso que impede acne com bias 0.
            float bias = u_Bias + centerDepth * 0.002;

            float rangeCheck = smoothstep(0.0, 1.0,
                u_Radius / max(abs(centerDepth - sampleDepth), 1e-4));

            occlusion += (sampleDepth >= fragDepth + bias ? 0.0 : 1.0) * rangeCheck;
        }

        // ── SSAO_QUALITY_V2 — SEM pow aqui. Ver a nota no blur. ────────────
        FragColor = 1.0 - (occlusion / float(u_KernelSize));
    }
)";

    // ═══════════════════════════════════════════════════════════════════════
    //  SSAO_QUALITY_V2 — o blur, e a ORDEM que estava invertida
    //
    //  ── O DEFEITO ──────────────────────────────────────────────────────────
    //
    //  O `pow(occlusion, u_Power)` era aplicado no passe de AO, ANTES do blur.
    //  Com Power alto isso e desastroso: pow(0.9, 8) = 0.43 e pow(0.8, 8) =
    //  0.17. Dois pixels vizinhos que diferiam 0.1 por puro ruido de amostragem
    //  passavam a diferir 0.26 — e o blur so podia espalhar essa diferenca ja
    //  amplificada, nunca desfaze-la. Era por isso que subir a intensidade
    //  fazia aparecerem manchas em vez de simplesmente escurecer.
    //
    //  Denoise primeiro, amplificar depois. A mesma imagem, com o mesmo Power,
    //  fica limpa — porque o expoente age sobre a media, e nao sobre o ruido.
    //
    //  ── E O BLUR PASSOU A RESPEITAR SILHUETA ──────────────────────────────
    //
    //  Uma caixa 5x5 cega mistura a oclusao de um objeto com a do chao atras
    //  dele. Com raio grande isso vira um halo de sujeira em volta de tudo. O
    //  peso agora cai quando a profundidade do vizinho se afasta da do centro:
    //  o borrao para na silhueta, que e onde o olho mais repara.
    // ═══════════════════════════════════════════════════════════════════════
    static const char* s_BlurFrag = R"(
        #version 460 core
        out float FragColor;
        in vec2 v_TexCoord;

        uniform sampler2D u_Input;
        uniform sampler2D u_Position;   // G-Buffer, em WORLD space
        uniform mat4      u_View;
        uniform float     u_Power;

        void main()
        {
            vec2 texelSize = 1.0 / vec2(textureSize(u_Input, 0));

            vec3 centerWorld = texture(u_Position, v_TexCoord).rgb;
            float centerZ = -(u_View * vec4(centerWorld, 1.0)).z;

            // Tolerancia proporcional a distancia: o mesmo desnivel em metros
            // significa coisas diferentes a dois e a trinta metros da camera.
            float tolerance = max(centerZ * 0.02, 0.02);

            float result = 0.0;
            float weightSum = 0.0;

            for (int x = -2; x <= 2; x++)
            {
                for (int y = -2; y <= 2; y++)
                {
                    vec2 uv = v_TexCoord + vec2(x, y) * texelSize;

                    vec3 sampleWorld = texture(u_Position, uv).rgb;
                    float sampleZ = -(u_View * vec4(sampleWorld, 1.0)).z;

                    // Peso 1 enquanto o vizinho esta no mesmo plano, caindo a 0
                    // quando ele pertence a outra superficie.
                    float w = 1.0 - smoothstep(0.0, tolerance, abs(sampleZ - centerZ));

                    result += texture(u_Input, uv).r * w;
                    weightSum += w;
                }
            }

            // weightSum nunca e zero — o proprio centro entra com peso 1 — mas
            // a guarda fica: um NaN aqui pinta a tela inteira de preto, e ja
            // custou uma sessao nesta engine.
            float ao = (weightSum > 1e-4) ? (result / weightSum)
                                          : texture(u_Input, v_TexCoord).r;

            FragColor = pow(clamp(ao, 0.0, 1.0), u_Power);
        }
    )";

    // --- Implementação ---

    OpenGLSSAOPass::~OpenGLSSAOPass()
    {
        glDeleteFramebuffers(1, &m_SSAOFBO);
        glDeleteFramebuffers(1, &m_BlurFBO);
        glDeleteTextures(1, &m_OcclusionTex);
        glDeleteTextures(1, &m_OcclusionRawTex);
        glDeleteTextures(1, &m_NoiseTex);
        glDeleteVertexArrays(1, &m_QuadVAO);
        glDeleteBuffers(1, &m_QuadVBO);
    }

    void OpenGLSSAOPass::Initialize(uint32_t width, uint32_t height)
    {
        m_Width = width;
        m_Height = height;

        SetupKernel();
        SetupNoiseTex();
        SetupFBOs(width, height);
        SetupQuad();

        m_SSAOShader = Shader::Create(s_QuadVert, s_SSAOFrag);
        m_BlurShader = Shader::Create(s_QuadVert, s_BlurFrag);

        m_Initialized = true;
        //AXE_CORE_INFO("OpenGLSSAOPass initialized ({}x{})", width, height);
    }

    void OpenGLSSAOPass::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;
        if (width == m_Width && height == m_Height) return;
        m_Width = width;
        m_Height = height;

        glDeleteFramebuffers(1, &m_SSAOFBO);
        glDeleteFramebuffers(1, &m_BlurFBO);
        glDeleteTextures(1, &m_OcclusionTex);
        glDeleteTextures(1, &m_OcclusionRawTex);
        SetupFBOs(width, height);
    }

    void OpenGLSSAOPass::SetupKernel()
    {
        std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
        std::default_random_engine gen;

        m_Kernel.resize(64);
        for (int i = 0; i < 64; i++)
        {
            glm::vec3 sample(
                rnd(gen) * 2.0f - 1.0f,
                rnd(gen) * 2.0f - 1.0f,
                rnd(gen));
            sample = glm::normalize(sample);
            sample *= rnd(gen);

            // Acelera interpolação — mais amostras perto da origem
            float scale = (float)i / 64.0f;
            scale = Lerp(0.1f, 1.0f, scale * scale);
            m_Kernel[i] = sample * scale;
        }
    }

    void OpenGLSSAOPass::SetupNoiseTex()
    {
        std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
        std::default_random_engine gen;

        std::vector<glm::vec3> noise(16);
        for (auto& n : noise)
            n = glm::vec3(rnd(gen) * 2.0f - 1.0f,
                rnd(gen) * 2.0f - 1.0f,
                0.0f);

        glCreateTextures(GL_TEXTURE_2D, 1, &m_NoiseTex);
        glTextureStorage2D(m_NoiseTex, 1, GL_RGB16F, 4, 4);
        glTextureSubImage2D(m_NoiseTex, 0, 0, 0, 4, 4,
            GL_RGB, GL_FLOAT, noise.data());
        glTextureParameteri(m_NoiseTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_NoiseTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_NoiseTex, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(m_NoiseTex, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    void OpenGLSSAOPass::SetupFBOs(uint32_t width, uint32_t height)
    {
        // SSAO raw
        glCreateTextures(GL_TEXTURE_2D, 1, &m_OcclusionRawTex);
        glTextureStorage2D(m_OcclusionRawTex, 1, GL_R16F, width, height);
        glTextureParameteri(m_OcclusionRawTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_OcclusionRawTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glCreateFramebuffers(1, &m_SSAOFBO);
        glNamedFramebufferTexture(m_SSAOFBO, GL_COLOR_ATTACHMENT0, m_OcclusionRawTex, 0);

        // SSAO blur
        glCreateTextures(GL_TEXTURE_2D, 1, &m_OcclusionTex);
        glTextureStorage2D(m_OcclusionTex, 1, GL_R16F, width, height);
        glTextureParameteri(m_OcclusionTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_OcclusionTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glCreateFramebuffers(1, &m_BlurFBO);
        glNamedFramebufferTexture(m_BlurFBO, GL_COLOR_ATTACHMENT0, m_OcclusionTex, 0);
    }

    void OpenGLSSAOPass::SetupQuad()
    {
        float verts[] = {
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
        glEnableVertexArrayAttrib(m_QuadVAO, 0);
        glVertexArrayAttribFormat(m_QuadVAO, 0, 2, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(m_QuadVAO, 0, 0);
        glEnableVertexArrayAttrib(m_QuadVAO, 1);
        glVertexArrayAttribFormat(m_QuadVAO, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
        glVertexArrayAttribBinding(m_QuadVAO, 1, 0);
    }

    void OpenGLSSAOPass::Execute(const GBuffer& gbuffer,
        const glm::mat4& projection,
        const glm::mat4& view,
        const SSAOSettings& settings)
    {
        if (!settings.Enabled) return;



        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(m_QuadVAO);

        // --- SSAO ---
        glBindFramebuffer(GL_FRAMEBUFFER, m_SSAOFBO);
        glViewport(0, 0, m_Width, m_Height);
        glClear(GL_COLOR_BUFFER_BIT);

        m_SSAOShader->Bind();
        glBindTextureUnit(0, gbuffer.GetPositionID());
        glBindTextureUnit(1, gbuffer.GetNormalID());
        glBindTextureUnit(2, m_NoiseTex);

        m_SSAOShader->SetInt("u_Position", 0);
        m_SSAOShader->SetInt("u_Normal", 1);
        m_SSAOShader->SetInt("u_Noise", 2);
        m_SSAOShader->SetMat4("u_Projection", glm::value_ptr(projection));
        m_SSAOShader->SetMat4("u_View", glm::value_ptr(view));
        m_SSAOShader->SetInt("u_KernelSize", settings.KernelSize);
        m_SSAOShader->SetFloat("u_Radius", settings.Radius);
        m_SSAOShader->SetFloat("u_Bias", settings.Bias);
        m_SSAOShader->SetFloat4("u_NoiseScale",
            glm::vec4((float)m_Width / 4.0f, (float)m_Height / 4.0f, 0.0f, 0.0f));

        // Kernel
        for (int i = 0; i < settings.KernelSize && i < (int)m_Kernel.size(); i++)
        {
            std::string name = "u_Samples[" + std::to_string(i) + "]";
            m_SSAOShader->SetFloat3(name, m_Kernel[i]);
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);

        // --- Blur ---
        glBindFramebuffer(GL_FRAMEBUFFER, m_BlurFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        m_BlurShader->Bind();
        glBindTextureUnit(0, m_OcclusionRawTex);
        glBindTextureUnit(1, gbuffer.GetPositionID());
        m_BlurShader->SetInt("u_Input", 0);
        m_BlurShader->SetInt("u_Position", 1);
        m_BlurShader->SetMat4("u_View", glm::value_ptr(view));

        // SSAO_QUALITY_V2 — o Power mudou de passe: agora ele age sobre a
        // media ja filtrada, e nao sobre o ruido bruto.
        m_BlurShader->SetFloat("u_Power", settings.Power);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindVertexArray(0);
        glUseProgram(0);

        // Limpa texture units usadas
        for (int i = 0; i < 3; i++)
        {
            glBindTextureUnit(i, 0);
        }
    }
}