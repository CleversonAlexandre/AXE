#include "opengl_geometry_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/material/material.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>

namespace axe
{
    static const char* s_GeomVert = R"(
    #version 460 core
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec3 a_Normal;
    layout(location = 2) in vec2 a_TexCoord;
    layout(location = 3) in vec3 a_Tangent;
    layout(location = 4) in vec3 a_Bitangent;

    uniform mat4 u_ViewProjection;
    uniform mat4 u_Model;

    out vec3 v_FragPos;
    out vec3 v_Normal;
    out vec2 v_TexCoord;
    out vec3 v_Tangent;
    out vec3 v_Bitangent;

    void main()
    {
        vec4 worldPos  = u_Model * vec4(a_Position, 1.0);
        v_FragPos      = worldPos.xyz;
        v_Normal       = normalize(mat3(transpose(inverse(u_Model))) * a_Normal);
        v_TexCoord     = a_TexCoord;
        v_Tangent      = normalize(mat3(u_Model) * a_Tangent);
        v_Bitangent    = normalize(mat3(u_Model) * a_Bitangent);
        gl_Position    = u_ViewProjection * worldPos;
    }
)";

    static const char* s_GeomFrag = R"(
    #version 460 core

    layout(location = 0) out vec3 g_Position;  // world space
    layout(location = 1) out vec3 g_Normal;    // world space
    layout(location = 2) out vec4 g_Albedo;
    layout(location = 3) out vec2 g_PBR;

    in vec3 v_FragPos;
    in vec3 v_Normal;
    in vec2 v_TexCoord;
    in vec3 v_Tangent;
    in vec3 v_Bitangent;

    uniform vec4      u_Color;
    uniform int       u_HasAlbedoMap;
    uniform sampler2D u_AlbedoMap;
    uniform int       u_HasNormalMap;
    uniform sampler2D u_NormalMap;
    uniform int       u_HasRoughnessMap;
    uniform sampler2D u_RoughnessMap;
    uniform float     u_Roughness;
    uniform float     u_Metallic;

    void main()
    {
        g_Position = v_FragPos;

        vec3 N = normalize(v_Normal);
        if (u_HasNormalMap == 1)
        {
            vec3 normalTex = texture(u_NormalMap, v_TexCoord).rgb * 2.0 - 1.0;
            vec3 T = normalize(v_Tangent);
            vec3 B = normalize(v_Bitangent);
            mat3 TBN = mat3(T, B, N);
            N = normalize(TBN * normalTex);
        }
        g_Normal = N;

        vec3 albedo = u_Color.rgb;
        if (u_HasAlbedoMap == 1)
            albedo = texture(u_AlbedoMap, v_TexCoord).rgb;
          //  albedo = pow(texture(u_AlbedoMap, v_TexCoord).rgb, vec3(2.2));

        float roughness = u_Roughness;
        if (u_HasRoughnessMap == 1)
            roughness = texture(u_RoughnessMap, v_TexCoord).r;

        g_Albedo = vec4(albedo, u_Metallic);
        g_PBR    = vec2(roughness, 1.0);
    }
)";
    void OpenGLGeometryPass::Initialize()
    {
        m_Shader = Shader::Create(s_GeomVert, s_GeomFrag);
        m_Initialized = true;
        AXE_CORE_INFO("OpenGLGeometryPass initialized");
    }

    void OpenGLGeometryPass::Begin(GBuffer& gbuffer,
        const glm::mat4& viewProjection,
        const glm::vec3& cameraPosition)
    {
        gbuffer.Bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(viewProjection));
    }

    void OpenGLGeometryPass::DrawMesh(const Mesh& mesh,
        const glm::mat4& model,
        const Material* material)
    {
        m_Shader->SetMat4("u_Model", glm::value_ptr(model));

        // Material
        if (material)
        {
            m_Shader->SetFloat4("u_Color", material->Color);
            m_Shader->SetFloat("u_Roughness", material->Roughness);
            m_Shader->SetFloat("u_Metallic", material->Metallic);

            if (material->AlbedoMap && material->AlbedoMap->IsLoaded())
            {
                glBindTextureUnit(0, material->AlbedoMap->GetRendererID());
                m_Shader->SetInt("u_AlbedoMap", 0);
                m_Shader->SetInt("u_HasAlbedoMap", 1);
            }
            else m_Shader->SetInt("u_HasAlbedoMap", 0);

            if (material->NormalMap && material->NormalMap->IsLoaded())
            {
                glBindTextureUnit(1, material->NormalMap->GetRendererID());
                m_Shader->SetInt("u_NormalMap", 1);
                m_Shader->SetInt("u_HasNormalMap", 1);
            }
            else m_Shader->SetInt("u_HasNormalMap", 0);
        }
        else
        {
            m_Shader->SetFloat4("u_Color", glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
            m_Shader->SetFloat("u_Roughness", 0.5f);
            m_Shader->SetFloat("u_Metallic", 0.0f);
            m_Shader->SetInt("u_HasAlbedoMap", 0);
            m_Shader->SetInt("u_HasNormalMap", 0);
        }

        mesh.GetVertexArray()->Bind();
        glDrawElements(GL_TRIANGLES, mesh.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLGeometryPass::End()
    {
        glBindVertexArray(0);
        glUseProgram(0);
    }
}