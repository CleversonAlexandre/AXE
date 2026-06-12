#include "opengl_shadow_map_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>

namespace axe
{
    static const char* s_DepthVertSrc = R"(
        #version 460 core
        layout(location = 0) in vec3 a_Position;
        uniform mat4 u_LightSpaceMatrix;
        uniform mat4 u_Model;
        void main()
        {
            gl_Position = u_LightSpaceMatrix * u_Model * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_DepthFragSrc = R"(
        #version 460 core
        void main() {}
    )";

    OpenGLShadowMapPass::~OpenGLShadowMapPass()
    {
        if (m_FBO)        glDeleteFramebuffers(1, &m_FBO);
        if (m_DepthMapID) glDeleteTextures(1, &m_DepthMapID);
    }

    void OpenGLShadowMapPass::Initialize(uint32_t resolution)
    {
        m_Resolution = resolution;

        // ✅ DSA puro — sem mistura com API antiga
        glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthMapID);
        glTextureStorage2D(m_DepthMapID, 1, GL_DEPTH_COMPONENT24, resolution, resolution);
        glTextureParameteri(m_DepthMapID, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_DepthMapID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_DepthMapID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTextureParameteri(m_DepthMapID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[] = { 1.f, 1.f, 1.f, 1.f };
        glTextureParameterfv(m_DepthMapID, GL_TEXTURE_BORDER_COLOR, border);

        // ✅ FBO também DSA puro
        glCreateFramebuffers(1, &m_FBO);
        glNamedFramebufferTexture(m_FBO, GL_DEPTH_ATTACHMENT, m_DepthMapID, 0);
        glNamedFramebufferDrawBuffer(m_FBO, GL_NONE);
        glNamedFramebufferReadBuffer(m_FBO, GL_NONE);

        GLenum status = glCheckNamedFramebufferStatus(m_FBO, GL_FRAMEBUFFER);
        //AXE_CORE_INFO("ShadowPass FBO: {}",
        //    status == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE");

        m_DepthShader = Shader::Create(s_DepthVertSrc, s_DepthFragSrc);

        m_Initialized = true;
        //AXE_CORE_INFO("OpenGLShadowMapPass initialized ({}x{})", resolution, resolution);
    }

    void OpenGLShadowMapPass::Begin(const glm::mat4& lightSpaceMatrix)
    {
        m_LightSpaceMatrix = lightSpaceMatrix;

        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_SavedFBO);
        glGetIntegerv(GL_VIEWPORT, m_SavedViewport);

        glViewport(0, 0, m_Resolution, m_Resolution);
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);

        m_DepthShader->Bind();
        m_DepthShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(lightSpaceMatrix));
    }

    void OpenGLShadowMapPass::DrawMesh(const Mesh& mesh, const glm::mat4& model)
    {
        m_DepthShader->SetMat4("u_Model", glm::value_ptr(model));
        mesh.GetVertexArray()->Bind();
        glDrawElements(GL_TRIANGLES, mesh.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLShadowMapPass::End()
    {
        glDisable(GL_POLYGON_OFFSET_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindFramebuffer(GL_FRAMEBUFFER, m_SavedFBO);
        glViewport(m_SavedViewport[0], m_SavedViewport[1],
            m_SavedViewport[2], m_SavedViewport[3]);
    }
}