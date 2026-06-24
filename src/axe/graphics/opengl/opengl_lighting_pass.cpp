#include "opengl_lighting_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>
#include <algorithm>

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
    uniform sampler2D u_Emissive;

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

    // Cookie da luz direcional — projeção planar (paralela), com tiling
    uniform sampler2D u_DirCookie;
    uniform int       u_HasDirCookie;
    uniform vec3      u_DirCookieRight;
    uniform vec3      u_DirCookieUp;
    uniform float     u_DirCookieScale;

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

        // Spot Light
        int   IsSpot;
        vec3  Direction;
        float InnerCutoff; // cos(InnerConeAngle), pré-calculado na CPU
        float OuterCutoff; // cos(OuterConeAngle)

        // Cookie — projeção cônica (perspectiva). Right/Up são a base
        // ortonormal perpendicular à Direction (mesma do gizmo do cone);
        // TanOuterAngle normaliza o offset pra -1..1 na borda do cone.
        // CookieIndex: -1 = sem cookie, 0..3 = slot em u_PointCookies.
        vec3  Right;
        vec3  Up;
        float TanOuterAngle;
        int   CookieIndex;
    };
    uniform PointLight u_PointLights[16];
    uniform int        u_NumPointLights;

    // Cookies de Point Light — limite de 4 simultâneas na cena (texturas
    // são caras de bindar; além desse número a luz funciona normal, só
    // sem o padrão projetado)
    uniform sampler2D u_PointCookies[4];

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

        // Atenuação do cone (Spot Light) — theta é o cosseno do ângulo
        // entre a direção da luz (apontando PARA o fragmento, por isso o
        // -L) e o eixo do cone. Fora do OuterCutoff = 0 (escuro); dentro
        // do InnerCutoff = 1 (intensidade máxima); entre os dois, suaviza.
        if (pl.IsSpot == 1)
        {
            float theta = dot(-L, normalize(pl.Direction));
            float epsilon = pl.InnerCutoff - pl.OuterCutoff;
            float coneAtt = clamp((theta - pl.OuterCutoff) / max(epsilon, 0.0001), 0.0, 1.0);
            att *= coneAtt;
        }

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

        vec3 V = normalize(u_CameraPosition - fragPos);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        // --- Luz direcional ---
        vec3 Lo = vec3(0.0);
        if (u_HasLight == 1)
        {
            vec3 L = normalize(-u_LightDirection);
            vec3 H = normalize(V + L);

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

            // Cookie — projeta fragPos no plano perpendicular à direção da
            // luz (paralela, então sem perspectiva — só tiling por escala)
            vec3 dirCookieTint = vec3(1.0);
            if (u_HasDirCookie == 1)
            {
                float cu = dot(fragPos, u_DirCookieRight) / u_DirCookieScale;
                float cv = dot(fragPos, u_DirCookieUp) / u_DirCookieScale;
                dirCookieTint = texture(u_DirCookie, fract(vec2(cu, cv))).rgb;
            }

            Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadow) * mix(1.0, ao, 0.5) * dirCookieTint;
        }

        // --- Point lights --- (independente da direcional)
        for (int i = 0; i < u_NumPointLights; i++)
        {
            vec3 contribution = CalcPointLight(u_PointLights[i], fragPos, N, V, albedo, metallic, roughness, F0);

            // Cookie — só Spot Light com CookieIndex válido. Projeção
            // cônica (perspectiva): divide pelo cosseno do ângulo em
            // relação ao eixo, igual a uma divisão de perspectiva real.
            if (u_PointLights[i].IsSpot == 1 && u_PointLights[i].CookieIndex >= 0)
            {
                vec3 toFrag = normalize(fragPos - u_PointLights[i].Position);
                vec3 dir = normalize(u_PointLights[i].Direction);
                float cosAngle = dot(toFrag, dir);

                if (cosAngle > 0.0001)
                {
                    vec3 perp = toFrag - dir * cosAngle;
                    vec2 cuv = vec2(dot(perp, u_PointLights[i].Right),
                                     dot(perp, u_PointLights[i].Up))
                               / (cosAngle * max(u_PointLights[i].TanOuterAngle, 0.0001));
                    cuv = cuv * 0.5 + 0.5;

                    if (cuv.x >= 0.0 && cuv.x <= 1.0 && cuv.y >= 0.0 && cuv.y <= 1.0)
                    {
                        // Índices sempre constantes (0/1/2/3) — evita
                        // indexação dinâmica de array de sampler, que não
                        // é garantida em todo hardware/driver.
                        vec3 cookieTint = vec3(1.0);
                        if (u_PointLights[i].CookieIndex == 0) cookieTint = texture(u_PointCookies[0], cuv).rgb;
                        else if (u_PointLights[i].CookieIndex == 1) cookieTint = texture(u_PointCookies[1], cuv).rgb;
                        else if (u_PointLights[i].CookieIndex == 2) cookieTint = texture(u_PointCookies[2], cuv).rgb;
                        else if (u_PointLights[i].CookieIndex == 3) cookieTint = texture(u_PointCookies[3], cuv).rgb;
                        contribution *= cookieTint;
                    }
                }
            }

            Lo += contribution;
        }

        // --- Ambient / IBL --- (independente da direcional)
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

            vec3 ibl = (kD_amb * diffuse_ibl + specular_ibl) * ao * u_IBLIntensity;
            vec3 flatAmbient = u_AmbientStrength * albedo * ao;
            ambient = ibl + flatAmbient;
        }
        else
        {
            ambient = u_AmbientStrength * (u_HasLight == 1 ? u_LightColor : vec3(1.0)) * albedo * ao;
        }

        vec3 color = ambient + Lo;
        color = max(color, albedo * 0.02);

        // Emissive — somado direto, sem ser afetado por luz/sombra/AO,
        // assim como no caminho forward (preview do material).
        color += texture(u_Emissive, v_TexCoord).rgb;

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
            //AXE_CORE_INFO("OpenGLLightingPass initialized");
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
        glDepthMask(GL_FALSE); // preserva o depth copiado pelo BlitDepth para o skybox
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

        // Emissive — slot 9 (0-3 = G-Buffer base, 4 = SSAO, 5 = shadow map,
        // 6-8 = IBL — ver binds abaixo)
        glBindTextureUnit(9, gbuffer.GetEmissiveID());
        m_Shader->SetInt("u_Emissive", 9);

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

            // Cookie — slot 10. Right/Up: base ortonormal perpendicular à
            // direção da luz, pra projetar fragPos num plano 2D.
            if (light->CookieTexture && light->CookieTexture->IsLoaded())
            {
                glm::vec3 dir = glm::length(light->Direction) > 0.0001f
                    ? glm::normalize(light->Direction) : glm::vec3(0, -1, 0);
                glm::vec3 up = (fabsf(dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 right = glm::normalize(glm::cross(dir, up));
                up = glm::normalize(glm::cross(right, dir));

                light->CookieTexture->Bind(10);
                m_Shader->SetInt("u_DirCookie", 10);
                m_Shader->SetInt("u_HasDirCookie", 1);
                m_Shader->SetFloat3("u_DirCookieRight", right);
                m_Shader->SetFloat3("u_DirCookieUp", up);
                m_Shader->SetFloat("u_DirCookieScale", std::max(light->CookieScale, 0.01f));
            }
            else m_Shader->SetInt("u_HasDirCookie", 0);
        }
        else
        {
            m_Shader->SetInt("u_HasLight", 0);
            m_Shader->SetFloat("u_IBLIntensity", 1.0f);
            m_Shader->SetFloat("u_AmbientStrength", 0.0f); // sem flat ambient sem luz
            m_Shader->SetInt("u_HasDirCookie", 0);
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

        // Cookies de Point Light — slots 11, 12, 13, 14 (limite de 4
        // simultâneas; a partir da 5ª, a luz funciona normal sem padrão)
        int nextCookieSlot = 0;
        const int cookieTextureUnits[4] = { 11, 12, 13, 14 };

        for (int i = 0; i < numLights; i++)
        {
            const auto& pl = pointLights[i];
            std::string base = "u_PointLights[" + std::to_string(i) + "]";
            m_Shader->SetFloat3(base + ".Position", pl.Position);
            m_Shader->SetFloat3(base + ".Color", pl.Color);
            m_Shader->SetFloat(base + ".Intensity", pl.Intensity);
            m_Shader->SetFloat(base + ".Radius", pl.Radius);

            m_Shader->SetInt(base + ".IsSpot", pl.IsSpot ? 1 : 0);
            m_Shader->SetFloat3(base + ".Direction", pl.Direction);
            // cos() pré-calculado aqui — mais barato que recalcular por
            // pixel no fragment shader, já que o ângulo é o mesmo para
            // todos os fragmentos afetados por esta luz no frame.
            m_Shader->SetFloat(base + ".InnerCutoff", cosf(glm::radians(pl.InnerConeAngle)));
            m_Shader->SetFloat(base + ".OuterCutoff", cosf(glm::radians(pl.OuterConeAngle)));

            // Cookie — base ortonormal (Right/Up) perpendicular à direção,
            // mesma usada no gizmo do cone, e TanOuterAngle pra normalizar
            // o offset projetado pra -1..1 na borda do cone.
            m_Shader->SetFloat(base + ".TanOuterAngle", tanf(glm::radians(pl.OuterConeAngle)));

            int cookieIndex = -1;
            if (pl.IsSpot && pl.CookieTexture && pl.CookieTexture->IsLoaded() && nextCookieSlot < 4)
            {
                glm::vec3 dir = glm::length(pl.Direction) > 0.0001f
                    ? glm::normalize(pl.Direction) : glm::vec3(0, -1, 0);
                glm::vec3 up = (fabsf(dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 right = glm::normalize(glm::cross(dir, up));
                up = glm::normalize(glm::cross(right, dir));

                m_Shader->SetFloat3(base + ".Right", right);
                m_Shader->SetFloat3(base + ".Up", up);

                cookieIndex = nextCookieSlot;
                int unit = cookieTextureUnits[nextCookieSlot];
                pl.CookieTexture->Bind(unit);
                m_Shader->SetInt("u_PointCookies[" + std::to_string(nextCookieSlot) + "]", unit);
                nextCookieSlot++;
            }
            m_Shader->SetInt(base + ".CookieIndex", cookieIndex);
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