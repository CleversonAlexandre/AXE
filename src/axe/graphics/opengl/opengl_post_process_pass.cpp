#include "opengl_post_process_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>

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
        uniform int       u_HasBloom;

        vec3 ACESFilm(vec3 x)
        {
            float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
            return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
        }

        void main()
        {
            vec3 hdr = texture(u_HDRBuffer, v_TexCoord).rgb;
            if (u_HasBloom == 1)
                hdr += texture(u_BloomBuffer, v_TexCoord).rgb;

            // Reinhard — mais suave que ACES
            vec3 mapped = hdr * u_Exposure;
            mapped = mapped / (mapped + vec3(1.0));
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
    }

    void OpenGLPostProcessPass::Initialize(uint32_t width, uint32_t height)
    {
        m_Width = width;
        m_Height = height;

        SetupQuad();
        SetupBloomBuffers(width, height);

        m_TonemapShader = Shader::Create(s_QuadVert, s_TonemapFrag);
        m_BloomExtractShader = Shader::Create(s_QuadVert, s_BloomExtractFrag);
        m_BlurShader = Shader::Create(s_QuadVert, s_BlurFrag);

        m_Initialized = true;
        AXE_CORE_INFO("OpenGLPostProcessPass initialized ({}x{})", width, height);
    }

    void OpenGLPostProcessPass::Resize(uint32_t width, uint32_t height)
    {
        if (width == m_Width && height == m_Height) return;
        m_Width = width; m_Height = height;
        glDeleteFramebuffers(2, m_BloomFBO);
        glDeleteTextures(2, m_BloomColorTex);
        SetupBloomBuffers(width, height);
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

    void OpenGLPostProcessPass::Execute(uint32_t hdrColorID,
        const PostProcessSettings& settings)
    {
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(m_QuadVAO);

        uint32_t bloomTex = 0;

        GLint destFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destFBO);

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

        // --- Tone Mapping ---
        glBindFramebuffer(GL_FRAMEBUFFER, destFBO);
        glViewport(0, 0, m_Width, m_Height);
        m_TonemapShader->Bind();
        glBindTextureUnit(0, hdrColorID);
        m_TonemapShader->SetInt("u_HDRBuffer", 0);
        m_TonemapShader->SetFloat("u_Exposure", settings.Exposure);
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

        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);
    }
}