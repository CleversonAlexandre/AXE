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
        vec3 normal       = normalize(texture(u_Normal, v_TexCoord).rgb);
        vec3 noise        = normalize(texture(u_Noise, v_TexCoord * u_NoiseScale).rgb);

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

            // Profundidade real da geometria naquele pixel
            vec3 samplePosReal = texture(u_Position, offset.xy).rgb;

            // Compara distância à câmera
            float sampleDepth = length((u_View * vec4(samplePosReal, 1.0)).xyz);
            float fragDepth   = length((u_View * vec4(samplePos,     1.0)).xyz);

            float rangeCheck = smoothstep(0.0, 1.0,
                u_Radius / abs(length((u_View * vec4(fragPosWorld, 1.0)).xyz) - sampleDepth));

            occlusion += (sampleDepth >= fragDepth + u_Bias ? 0.0 : 1.0) * rangeCheck;
        }

        occlusion = 1.0 - (occlusion / float(u_KernelSize));
        FragColor = pow(occlusion, u_Power);
    }
)";

    static const char* s_BlurFrag = R"(
        #version 460 core
        out float FragColor;
        in vec2 v_TexCoord;
        uniform sampler2D u_Input;
        void main()
        {
            vec2 texelSize = 1.0 / vec2(textureSize(u_Input, 0));
            float result = 0.0;
            for (int x = -2; x <= 2; x++)
                for (int y = -2; y <= 2; y++)
                    result += texture(u_Input,
                        v_TexCoord + vec2(x, y) * texelSize).r;
            FragColor = result / 25.0;
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
        m_SSAOShader->SetFloat("u_Power", settings.Power);
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
        m_BlurShader->SetInt("u_Input", 0);
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