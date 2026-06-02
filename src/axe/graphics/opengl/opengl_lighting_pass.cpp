#include "opengl_lighting_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>

namespace axe
{
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

    static const char* s_LightingFrag = R"(
    #version 460 core
    out vec4 FragColor;
    in vec2 v_TexCoord;

    // G-Buffer
    uniform sampler2D u_Position;
    uniform sampler2D u_Normal;
    uniform sampler2D u_Albedo;
    uniform sampler2D u_PBR;

    // SSAO
    uniform sampler2D u_SSAO;
    uniform int       u_HasSSAO;
    uniform int       u_SSAODebug;

    // Shadow
    uniform sampler2D u_ShadowMap;
    uniform mat4      u_LightSpaceMatrix;
    uniform int       u_HasShadowMap;

    // Luz direcional
    uniform vec3  u_LightDirection;
    uniform vec3  u_LightColor;
    uniform float u_LightIntensity;
    uniform float u_AmbientStrength;
    uniform vec3  u_CameraPosition;
    uniform int   u_HasLight;

    // IBL
    uniform samplerCube u_IrradianceMap;
    uniform samplerCube u_PrefilteredMap;
    uniform sampler2D   u_BRDFLut;
    uniform int         u_HasIBL;
    uniform float       u_IBLIntensity;

    // Point Lights
    struct PointLight {
        vec3  Position;
        vec3  Color;
        float Intensity;
        float Radius;
    };
    uniform PointLight u_PointLights[16];
    uniform int        u_NumPointLights;

    const float PI = 3.14159265359;

    float DistributionGGX(vec3 N, vec3 H, float roughness)
    {
        float a  = roughness * roughness;
        float a2 = a * a;
        float NdotH = max(dot(N, H), 0.0);
        float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
        return a2 / (PI * denom * denom);
    }

    float GeometrySchlickGGX(float NdotV, float roughness)
    {
        float r = roughness + 1.0;
        float k = (r * r) / 8.0;
        return NdotV / (NdotV * (1.0 - k) + k);
    }

    float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
    {
        return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
               GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
    }

    vec3 FresnelSchlick(float cosTheta, vec3 F0)
    {
        return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    }

    float ShadowCalculation(vec3 fragPos, vec3 normal, vec3 lightDir)
    {
        vec4 fragPosLS  = u_LightSpaceMatrix * vec4(fragPos, 1.0);
        vec3 projCoords = fragPosLS.xyz / fragPosLS.w;
        projCoords      = projCoords * 0.5 + 0.5;
        if (projCoords.z > 1.0) return 0.0;

        float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.005);
        float shadow = 0.0;
        vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
        for (int x = -2; x <= 2; x++)
            for (int y = -2; y <= 2; y++)
            {
                float pcf = texture(u_ShadowMap,
                    projCoords.xy + vec2(x, y) * texelSize).r;
                shadow += projCoords.z - bias > pcf ? 1.0 : 0.0;
            }
        return shadow / 25.0;
    }

    vec3 CalcPointLight(PointLight pl, vec3 fragPos, vec3 N, vec3 V,
                        vec3 albedo, float metallic, float roughness, vec3 F0)
    {
        vec3  L    = normalize(pl.Position - fragPos);
        vec3  H    = normalize(V + L);
        float dist = length(pl.Position - fragPos);

        // Atenuação física com smooth falloff no radius
        float att  = clamp(1.0 - (dist / pl.Radius), 0.0, 1.0);
        att        = att * att;
        vec3 radiance = pl.Color * pl.Intensity * att;

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        vec3  numerator   = NDF * G * F;
        float NdotL       = max(dot(N, L), 0.0);
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3  specular    = numerator / denominator;

        return (kD * albedo / PI + specular) * radiance * NdotL;
    }

    void main()
    {
        vec3 fragPos = texture(u_Position, v_TexCoord).rgb;
        vec3 N       = normalize(texture(u_Normal, v_TexCoord).rgb);

        vec4  albedoM   = texture(u_Albedo, v_TexCoord);
        vec3  albedo    = albedoM.rgb;
        float metallic  = albedoM.a;
        vec2  pbr       = texture(u_PBR, v_TexCoord).rg;
        float roughness = pbr.r;
        float matAO     = pbr.g;

        float ao = matAO;
        if (u_HasSSAO == 1)
            ao *= texture(u_SSAO, v_TexCoord).r;

        // Modo debug — mostra só a textura de oclusão em cinza
        // Se SSAO não está disponível, mostra vermelho como indicador
        if (u_SSAODebug == 1)
        {
            if (u_HasSSAO == 1)
            {
                float ssaoVal = texture(u_SSAO, v_TexCoord).r;
                FragColor = vec4(vec3(ssaoVal), 1.0);
            }
            else
            {
                FragColor = vec4(1.0, 0.0, 0.0, 1.0); // vermelho = SSAO não disponível
            }
            return;
        }

        if (u_HasLight == 0)
        {
            FragColor = vec4(albedo * ao, 1.0);
            return;
        }

        vec3 V = normalize(u_CameraPosition - fragPos);
        vec3 L = normalize(-u_LightDirection);
        vec3 H = normalize(V + L);

        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 radiance = u_LightColor * u_LightIntensity;

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3  numerator   = NDF * G * F;
        float NdotL       = max(dot(N, L), 0.0);
        float NdotV       = max(dot(N, V), 0.0);
        float denominator = 4.0 * NdotV * NdotL + 0.0001;
        vec3  specular    = numerator / denominator;

        float shadow = 0.0;
        if (u_HasShadowMap == 1)
            shadow = ShadowCalculation(fragPos, N, L);

        // Luz direcional — ao aplicado levemente para SSAO ser visível
        // mesmo em áreas iluminadas diretamente
        vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadow) * mix(1.0, ao, 0.5);

        // Point lights
        for (int i = 0; i < u_NumPointLights; i++)
            Lo += CalcPointLight(u_PointLights[i], fragPos, N, V, albedo, metallic, roughness, F0);

        vec3 ambient;
        if (u_HasIBL == 1)
        {
            vec3 F_amb  = FresnelSchlick(max(dot(N, V), 0.0), F0);
            vec3 kD_amb = (1.0 - F_amb) * (1.0 - metallic);
            vec3 irradiance  = texture(u_IrradianceMap, N).rgb;
            vec3 diffuse_ibl = irradiance * albedo;
            vec3 R = reflect(-V, N);
            vec3 prefilteredColor = textureLod(u_PrefilteredMap, R, roughness * 4.0).rgb;
            vec2 brdf = texture(u_BRDFLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
            vec3 specular_ibl = prefilteredColor * (F_amb * brdf.x + brdf.y);
            // AmbientStrength controla o quanto do IBL contribui para o ambient
            // permitindo escurecer o ambient mesmo com IBL ativo
            ambient = (kD_amb * diffuse_ibl + specular_ibl) * ao * u_IBLIntensity * u_AmbientStrength;
        }
        else
        {
            ambient = u_AmbientStrength * u_LightColor * albedo * ao;
        }

        vec3 color = ambient + Lo;
        color = max(color, albedo * 0.02);

        FragColor = vec4(color, 1.0);
    }
)";

    void OpenGLLightingPass::Initialize()
    {
        try
        {
            m_Shader = Shader::Create(s_QuadVert, s_LightingFrag);
            SetupQuad();
            m_Initialized = true;
            AXE_CORE_INFO("OpenGLLightingPass initialized");
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("OpenGLLightingPass shader error: {}", e.what());
        }
    }

    void OpenGLLightingPass::SetupQuad()
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

    void OpenGLLightingPass::Execute(const GBuffer& gbuffer,
        uint32_t ssaoTextureID,
        uint32_t shadowMapID,
        const glm::mat4& lightSpaceMatrix,
        const glm::vec3& cameraPosition,
        const DirectionalLight* light,
        const SceneEnvironment* environment,
        const std::vector<PointLight>& pointLights)
    {
        if (!m_Shader || !m_Initialized)
        {
            AXE_CORE_ERROR("LightingPass: não inicializado!");
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(m_QuadVAO);
        m_Shader->Bind();

        // G-Buffer — slots 0, 1, 2, 3
        glBindTextureUnit(0, gbuffer.GetPositionID());
        glBindTextureUnit(1, gbuffer.GetNormalID());
        glBindTextureUnit(2, gbuffer.GetAlbedoID());
        glBindTextureUnit(3, gbuffer.GetPBRID());
        m_Shader->SetInt("u_Position", 0);
        m_Shader->SetInt("u_Normal", 1);
        m_Shader->SetInt("u_Albedo", 2);
        m_Shader->SetInt("u_PBR", 3);

        // SSAO — slot 4
        if (ssaoTextureID != 0)
        {
            glBindTextureUnit(4, ssaoTextureID);
            m_Shader->SetInt("u_SSAO", 4);
            m_Shader->SetInt("u_HasSSAO", 1);
        }
        else
        {
            m_Shader->SetInt("u_HasSSAO", 0);
        }
        m_Shader->SetInt("u_SSAODebug", m_SSAODebug ? 1 : 0);

        // Shadow — slot 5
        if (shadowMapID != 0)
        {
            glBindTextureUnit(5, shadowMapID);
            m_Shader->SetInt("u_ShadowMap", 5);
            m_Shader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(lightSpaceMatrix));
            m_Shader->SetInt("u_HasShadowMap", 1);
        }
        else m_Shader->SetInt("u_HasShadowMap", 0);

        // Luz direcional
        if (light)
        {
            m_Shader->SetInt("u_HasLight", 1);
            m_Shader->SetFloat3("u_LightDirection", light->Direction);
            m_Shader->SetFloat3("u_LightColor", light->Color);
            m_Shader->SetFloat("u_LightIntensity", light->Intensity);
            m_Shader->SetFloat("u_AmbientStrength", light->AmbientStrength);
            m_Shader->SetFloat("u_IBLIntensity", light->IBLIntensity);
        }
        else
        {
            m_Shader->SetInt("u_HasLight", 0);
            m_Shader->SetFloat("u_IBLIntensity", 1.0f);
        }

        m_Shader->SetFloat3("u_CameraPosition", cameraPosition);

        // IBL — slots 6, 7, 8
        if (environment && environment->HasIBL())
        {
            environment->Skybox->BindIrradiance(6);
            environment->Skybox->BindPrefiltered(7);
            environment->Skybox->BindBRDFLut(8);
            m_Shader->SetInt("u_IrradianceMap", 6);
            m_Shader->SetInt("u_PrefilteredMap", 7);
            m_Shader->SetInt("u_BRDFLut", 8);
            m_Shader->SetInt("u_HasIBL", 1);
        }
        else m_Shader->SetInt("u_HasIBL", 0);

        // Point Lights
        int numLights = (int)std::min(pointLights.size(), (size_t)16);
        m_Shader->SetInt("u_NumPointLights", numLights);
        for (int i = 0; i < numLights; i++)
        {
            const auto& pl = pointLights[i];
            std::string base = "u_PointLights[" + std::to_string(i) + "]";
            m_Shader->SetFloat3(base + ".Position", pl.Position);
            m_Shader->SetFloat3(base + ".Color", pl.Color);
            m_Shader->SetFloat(base + ".Intensity", pl.Intensity);
            m_Shader->SetFloat(base + ".Radius", pl.Radius);
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Restaura estado
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindVertexArray(0);
        glUseProgram(0);
    }
} // namespace axe