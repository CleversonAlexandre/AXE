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
        if (m_FBO)           glDeleteFramebuffers(1, &m_FBO);
        if (m_DepthArrayTex) glDeleteTextures(1, &m_DepthArrayTex);
    }

    void OpenGLCascadedShadowPass::Initialize(uint32_t resolution)
    {
        m_Resolution = resolution;

        // Texture array — uma layer por cascade
        glGenTextures(1, &m_DepthArrayTex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_DepthArrayTex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
            resolution, resolution, AXE_SHADOW_CASCADES,
            0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[] = { 1.f, 1.f, 1.f, 1.f };
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);

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
        float nearSplit, float farSplit)
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

        // Centro do frustum
        glm::vec3 center(0.0f);
        for (auto& c : corners) center += glm::vec3(c);
        center /= corners.size();

        // Câmera de luz olhando do centro na direção da luz
        glm::vec3 dir = glm::normalize(lightDir);
        glm::mat4 lightView = glm::lookAt(center - dir, center, glm::vec3(0, 1, 0));

        // AABB dos cantos no espaço da luz
        float minX = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = -std::numeric_limits<float>::max();
        for (auto& c : corners)
        {
            glm::vec4 lp = lightView * c;
            minX = glm::min(minX, lp.x); maxX = glm::max(maxX, lp.x);
            minY = glm::min(minY, lp.y); maxY = glm::max(maxY, lp.y);
            minZ = glm::min(minZ, lp.z); maxZ = glm::max(maxZ, lp.z);
        }

        // Puxa o near/far pra trás pra capturar casters fora do frustum
        float zMult = 5.0f;
        if (minZ < 0) minZ *= zMult; else minZ /= zMult;
        if (maxZ < 0) maxZ /= zMult; else maxZ *= zMult;

        glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
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
                lightDir, cameraView, cameraProj, splits[i], splits[i + 1]);
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
        // Front-face culling reduz peter-panning
        glCullFace(GL_FRONT);

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
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_SavedFBO);
        glViewport(m_SavedViewport[0], m_SavedViewport[1],
            m_SavedViewport[2], m_SavedViewport[3]);
    }

} // namespace axe