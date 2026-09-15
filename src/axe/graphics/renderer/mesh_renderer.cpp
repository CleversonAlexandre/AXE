#include "axe/utils/glm_config.hpp"
#include "mesh_renderer.hpp"

#include "axe/graphics/shader.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/log/log.hpp"
#include "axe/core/time.hpp"
namespace axe
{
    MeshRenderer::MeshRenderer()
    {
        const std::string vertexSrc = R"(
        #version 460 core

        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;
        layout(location = 2) in vec2 a_TexCoord;
        layout(location = 3) in vec3 a_Tangent;
        layout(location = 4) in vec3 a_Bitangent;

        uniform mat4 u_Model;
        uniform mat4 u_ViewProjection;
        uniform mat3 u_NormalMatrix;

        out vec3 v_Normal;
        out vec3 v_FragPos;
        out vec2 v_TexCoord;
        out vec3 v_Tangent;
        out vec3 v_Bitangent;

        void main()
        {
            
             vec4 worldPos  = u_Model * vec4(a_Position, 1.0);
            v_FragPos      = worldPos.xyz;
            v_Normal       = normalize(u_NormalMatrix * a_Normal);
            v_Tangent      = normalize(u_NormalMatrix * a_Tangent);
            v_Bitangent    = normalize(u_NormalMatrix * a_Bitangent);
            v_TexCoord     = a_TexCoord;
            gl_Position    = u_ViewProjection * worldPos;
            
            
        }
    )";

        const std::string fragmentSrc = R"(
        #version 460 core

        out vec4 FragColor;

        in vec3 v_Normal;
        in vec3 v_FragPos;
        in vec2 v_TexCoord;
        in vec3 v_Tangent;
        in vec3 v_Bitangent;

        // Material
        uniform vec4  u_Color;
        uniform float u_SpecularStrength;
        uniform float u_Shininess;
        uniform float u_Metallic;
        uniform float u_Roughness;
        uniform float u_AO;
        uniform int   u_UsePBR;
        
        // Shadow
        uniform sampler2D u_ShadowMap;
        uniform mat4      u_LightSpaceMatrix;
        uniform int       u_HasShadowMap;

        // Texturas
        uniform sampler2D u_AlbedoMap;
        uniform sampler2D u_NormalMap;
        uniform sampler2D u_RoughnessMap;
        uniform sampler2D u_MetallicMap;
        uniform sampler2D u_AOMap;

        uniform int u_HasAlbedoMap;
        uniform int u_HasNormalMap;
        uniform int u_HasRoughnessMap;
        uniform int u_HasMetallicMap;
        uniform int u_HasAOMap;

        // Luz
        uniform vec3  u_LightDirection;
        uniform vec3  u_LightColor;
        uniform float u_LightIntensity;
        uniform float u_AmbientStrength;
        uniform vec3  u_CameraPosition;
        uniform bool  u_HasLight;

        // IBL
        uniform samplerCube u_IrradianceMap;
        uniform samplerCube u_PrefilteredMap;
        uniform sampler2D   u_BRDFLut;
        uniform int         u_HasIBL;
        uniform float       u_IBLIntensity;

        const float PI = 3.14159265359;

        // --- PBR Functions ---

        float DistributionGGX(vec3 N, vec3 H, float roughness)
        {
            float a      = roughness * roughness;
            float a2     = a * a;
            float NdotH  = max(dot(N, H), 0.0);
            float NdotH2 = NdotH * NdotH;
            float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
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
            float NdotV = max(dot(N, V), 0.0);
            float NdotL = max(dot(N, L), 0.0);
            return GeometrySchlickGGX(NdotV, roughness) *
                   GeometrySchlickGGX(NdotL, roughness);
        }

        vec3 FresnelSchlick(float cosTheta, vec3 F0)
        {
            return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
        }

        float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
        {
            // Perspective divide
            vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
            projCoords = projCoords * 0.5 + 0.5;

            if (projCoords.z > 1.0) return 0.0;

            float closestDepth = texture(u_ShadowMap, projCoords.xy).r;
            float currentDepth = projCoords.z;

            // Bias adaptativo            
            float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);

            // PCF — suaviza as bordas da sombra
            float shadow = 0.0;
            vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
            for (int x = -2; x <= 2; x++)
            for (int y = -2; y <= 2; y++)
            {
                float pcfDepth = texture(u_ShadowMap,
                projCoords.xy + vec2(x, y) * texelSize).r;
                shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            }
            shadow /= 25.0;
            return shadow;
        }


        void main()
        {

            // --- Lê valores do material ---
            vec3 albedo = u_Color.rgb;
            if (u_HasAlbedoMap == 1)
                albedo = pow(texture(u_AlbedoMap, v_TexCoord).rgb, vec3(2.2));

            float roughness = u_Roughness;
            if (u_HasRoughnessMap == 1)
                roughness = texture(u_RoughnessMap, v_TexCoord).r;

            float metallic = u_Metallic;
            if (u_HasMetallicMap == 1)
                metallic = texture(u_MetallicMap, v_TexCoord).r;

            float ao = u_AO;
            if (u_HasAOMap == 1)
                ao = texture(u_AOMap, v_TexCoord).r;

            // Normal mapping
            vec3 N = normalize(v_Normal);
            if (u_HasNormalMap == 1)
            {
                vec3 normalTex = texture(u_NormalMap, v_TexCoord).rgb;
                normalTex = normalTex * 2.0 - 1.0;
                
                vec3 T = normalize(v_Tangent);
                vec3 B = normalize(v_Bitangent);
                vec3 Nm = normalize(v_Normal);
                mat3 TBN = mat3(T, B, Nm);
                N = normalize(TBN * normalTex);
            }

            // TWO_SIDED_V1 — ver o mesmo trecho no geometry pass.
            if (!gl_FrontFacing) N = -N;
           if (u_UsePBR == 0 || !u_HasLight)
            {
                if (!u_HasLight)
                {
                    FragColor = vec4(albedo, u_Color.a);
                    return;
                }

                vec3 lightDir = normalize(-u_LightDirection);
                vec3 ambient  = u_AmbientStrength * u_LightColor;
                float diff    = max(dot(N, lightDir), 0.0);
                vec3 diffuse  = diff * u_LightColor * u_LightIntensity;
                vec3 viewDir  = normalize(u_CameraPosition - v_FragPos);
                vec3 halfDir  = normalize(lightDir + viewDir);
                float spec    = pow(max(dot(N, halfDir), 0.0), u_Shininess);
                vec3 specular = u_SpecularStrength * spec * u_LightColor;

                // ✅ Shadow — usa lightDir (já definido aqui)
                float shadow = 0.0;
                if (u_HasShadowMap == 1)
                {
                    vec4 fragPosLightSpace = u_LightSpaceMatrix * vec4(v_FragPos, 1.0);
                    shadow = ShadowCalculation(fragPosLightSpace, N, lightDir);
                }

                // ✅ result declarado uma só vez, já com shadow
                vec3 result = (ambient + (diffuse + specular) * (1.0 - shadow)) * albedo;
                FragColor = vec4(result, u_Color.a);
                return;
            }

            // --- PBR Cook-Torrance ---
            vec3 V  = normalize(u_CameraPosition - v_FragPos);
            vec3 L  = normalize(-u_LightDirection);
            vec3 H  = normalize(V + L);

            // F0 — reflectância base
            vec3 F0 = vec3(0.04);
            F0 = mix(F0, albedo, metallic);

            // Radiância da luz
            vec3 radiance = u_LightColor * u_LightIntensity;

            // Cook-Torrance BRDF
            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 numerator   = NDF * G * F;
            float NdotL      = max(dot(N, L), 0.0);
            float NdotV      = max(dot(N, V), 0.0);
            float denominator = 4.0 * NdotV * NdotL + 0.0001;
            vec3 specular    = numerator / denominator;

            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        

            // Ambient com IBL ou fallback
            vec3 ambient;
            if (u_HasIBL == 1)
            {
                vec3 F_amb  = FresnelSchlick(max(dot(N, V), 0.0), F0);
                vec3 kD_amb = (1.0 - F_amb) * (1.0 - metallic);
                vec3 irradiance   = texture(u_IrradianceMap, N).rgb;
                vec3 diffuse_ibl  = irradiance * albedo;

                const float MAX_REFLECTION_LOD = 4.0;
                vec3 R = reflect(-V, N);
                vec3 prefilteredColor = textureLod(u_PrefilteredMap, R,
                                                    roughness * MAX_REFLECTION_LOD).rgb;
                vec2 brdf         = texture(u_BRDFLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
                vec3 specular_ibl = prefilteredColor * (F_amb * brdf.x + brdf.y);

                ambient = (kD_amb * diffuse_ibl + specular_ibl) * ao * u_IBLIntensity;
            }
            else
            {
                ambient = u_AmbientStrength * u_LightColor * albedo * ao;
            }

            // Shadow
           float shadow = 0.0;
            if (u_HasShadowMap == 1)
            {
                vec4 fragPosLightSpace = u_LightSpaceMatrix * vec4(v_FragPos, 1.0);
                shadow = ShadowCalculation(fragPosLightSpace, N, L);
            }
            vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadow);

            vec3 color = ambient + Lo;

            // Tone mapping + gamma
            color = color / (color + vec3(1.0));
            color = pow(color, vec3(1.0 / 2.2));

            FragColor = vec4(color, u_Color.a);
        }
    )";


        m_Shader = Shader::Create(vertexSrc, fragmentSrc);



        PipelineSpecification spec;
        spec.Shader = m_Shader;
        spec.DepthTest = true;
        spec.Cull = CullMode::None;
        m_Pipeline = Pipeline::Create(spec);

        // Pipeline usada para o forward pass de materiais transparentes
        // (vidro, etc.): blend habilitado, depth-write desligado (não
        // sobrescreve o depth do passe opaco — vários objetos transparentes
        // sobrepostos continuam visíveis uns através dos outros), mas
        // depth-test ligado (ainda é ocluído corretamente pela geometria
        // opaca já desenhada).
        PipelineSpecification transparentSpec = spec;
        transparentSpec.Blend = true;
        transparentSpec.DepthWrite = false;
        // Culla as back-faces: sem isso, a parede de TRÁS do vidro (virada
        // "pra dentro", normal na direção oposta à câmera) também é
        // desenhada, e como não há ordenação por triângulo (só por objeto),
        // pedaços da frente e do fundo se misturam em ordem arbitrária —
        // o efeito de manchas/remendos, como se a normal estivesse invertida
        // em partes aleatórias da superfície.
        transparentSpec.Cull = CullMode::Back;
        m_TransparentPipeline = Pipeline::Create(transparentSpec);

        // ── TWO_SIDED_V1 ─────────────────────────────────────────────────────
        //
        // A mesma pipeline translucida, sem descarte de face. O raciocinio de
        // cima vale para o VIDRO — um volume fechado, onde a parede de tras
        // desenhada fora de ordem vira remendo. Nao vale para uma FOLHA: um
        // plano de agua, uma folhagem, um pano. Ali nao existe face de tras
        // "por dentro" de nada; existe a mesma superficie vista do outro lado,
        // e descarta-la faz a agua sumir quando a camera desce para baixo dela.
        //
        // Quem escolhe e o material (Material::TwoSided), como na Unreal — nao
        // ha como a renderer adivinhar se aquela malha e um copo ou um lago.
        PipelineSpecification transparentTwoSidedSpec = transparentSpec;
        transparentTwoSidedSpec.Cull = CullMode::None;
        m_TransparentTwoSidedPipeline = Pipeline::Create(transparentTwoSidedSpec);

        m_DefaultMaterial = std::make_shared<Material>(m_Shader, "Default");
    }

    void MeshRenderer::Begin(const glm::mat4& viewProjection, const glm::vec3& cameraPosition)
    {
        //	AXE_CORE_INFO("MeshRenderer::Begin VP[3][3]={}", viewProjection[3][3]);
        m_ViewProjection = viewProjection;
        m_ViewProjection = viewProjection;
        m_CameraPosition = cameraPosition;
    }


    void MeshRenderer::DrawMesh(const Mesh& mesh, const glm::mat4& model,
        const Material* material, const DirectionalLight* light, bool transparent)
    {




        const Material* mat = material ? material : m_DefaultMaterial.get();

        //AXE_CORE_INFO("VP[0][0]={} VP[1][1]={} VP[2][2]={} VP[3][3]={}",
        //	m_ViewProjection[0][0], m_ViewProjection[1][1],
        //	m_ViewProjection[2][2], m_ViewProjection[3][3]);
        //AXE_CORE_INFO("VP[3][0]={} VP[3][1]={} VP[3][2]={}",
        //	m_ViewProjection[3][0], m_ViewProjection[3][1], m_ViewProjection[3][2]);


        //AXE_CORE_INFO("Model[3][0]={} Model[3][1]={} Model[3][2]={}",
        //	model[3][0], model[3][1], model[3][2]);

        // Usa o shader do material se tiver, senão usa o padrão
        auto shader = mat->GetShader() ? mat->GetShader() : m_Shader;

        // TWO_SIDED_V1 — a pipeline opaca ja e CullMode::None, entao so o
        // caminho translucido tem duas variantes.
        const auto& pipeline = transparent
            ? (mat->TwoSided ? m_TransparentTwoSidedPipeline : m_TransparentPipeline)
            : m_Pipeline;
        pipeline->Bind();
        shader->Bind();
        mesh.GetVertexArray()->Bind();

        // Matrizes
        shader->SetMat4("u_Model", glm::value_ptr(model));
        shader->SetMat4("u_ViewProjection", glm::value_ptr(m_ViewProjection));

        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        shader->SetMat3("u_NormalMatrix", glm::value_ptr(normalMatrix));

        shader->SetFloat3("u_CameraPosition", m_CameraPosition);

        // Tempo de execução, em segundos, desde a inicialização do GLFW.
        // Usado pelos nodes "Time" e "Panner" do Material Graph — ver
        // comentário equivalente em OpenGLGeometryPass.
        shader->SetFloat("u_Time", Time::Elapsed());

        // Material — base
        shader->SetFloat4("u_Color", mat->Color);
        shader->SetFloat("u_SpecularStrength", mat->SpecularStrength);
        shader->SetFloat("u_Shininess", mat->Shininess);
        shader->SetFloat("u_Metallic", mat->Metallic);
        shader->SetFloat("u_Roughness", mat->Roughness);
        shader->SetFloat("u_AO", mat->AO);
        shader->SetInt("u_UsePBR", mat->UsePBR ? 1 : 0);



        // Texturas — se material tem shader compilado do graph, usa SamplerTextures
        // Senão usa os slots fixos padrão
        if (mat->GetShader() && !mat->SamplerTextures.empty())
        {
            int slot = 0;
            for (auto& [samplerName, tex] : mat->SamplerTextures)
            {
                if (tex && tex->IsLoaded())
                {
                    RenderCommand::BindTextureUnit(slot, tex->GetRendererID());
                    shader->SetInt(samplerName, slot);
                    ++slot;
                }
            }
        }
        else
        {
            shader->SetInt("u_AlbedoMap", 0);
            shader->SetInt("u_NormalMap", 1);
            shader->SetInt("u_RoughnessMap", 2);
            shader->SetInt("u_MetallicMap", 3);
            shader->SetInt("u_AOMap", 4);

            shader->SetInt("u_HasAlbedoMap", mat->HasAlbedoMap() ? 1 : 0);
            shader->SetInt("u_HasNormalMap", mat->HasNormalMap() ? 1 : 0);
            shader->SetInt("u_HasRoughnessMap", mat->HasRoughnessMap() ? 1 : 0);
            shader->SetInt("u_HasMetallicMap", mat->HasMetallicMap() ? 1 : 0);
            shader->SetInt("u_HasAOMap", mat->HasAOMap() ? 1 : 0);

            if (mat->HasAlbedoMap())    mat->AlbedoMap->Bind(0);
            if (mat->HasNormalMap())    mat->NormalMap->Bind(1);
            if (mat->HasRoughnessMap()) mat->RoughnessMap->Bind(2);
            if (mat->HasMetallicMap())  mat->MetallicMap->Bind(3);
            if (mat->HasAOMap())        mat->AOMap->Bind(4);
        }

        // Luz
        if (light)
        {
            shader->SetBool("u_HasLight", true);
            shader->SetFloat3("u_LightDirection", light->Direction);
            shader->SetFloat3("u_LightColor", light->Color);
            shader->SetFloat("u_LightIntensity", light->Intensity);
            shader->SetFloat("u_AmbientStrength", light->AmbientStrength);
            shader->SetFloat("u_IBLIntensity", light->IBLIntensity);
        }
        else
        {
            shader->SetBool("u_HasLight", false);
            shader->SetFloat("u_IBLIntensity", 1.0f);
        }

        if (m_ShadowMapID != 0)
        {
            //AXE_CORE_INFO("Setando shadow - ID:{} LightSpaceMatrix[3][3]:{}",
            //	m_ShadowMapID, m_LightSpaceMatrix[3][3]);
            RenderCommand::BindTextureUnit(8, m_ShadowMapID);
            shader->SetInt("u_ShadowMap", 8);
            shader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(m_LightSpaceMatrix));
            shader->SetInt("u_HasShadowMap", 1);
        }


        // IBL — slots 5, 6, 7
        //
        // SKY_IBL_V1 — mesma troca do lighting pass deferred: a fonte agora e
        // IBLSource(). Sem isto, o preview do Material Editor (que roda neste
        // caminho forward) continuaria sem a luz do ceu procedural e nao
        // bateria mais com o viewport.
        //
        // O HasSkybox() saiu da condicao de proposito: ele testa o cubemap de
        // ARQUIVO, e a captura procedural nao passa por ele.
        const CubemapTexture* ibl = m_Environment ? m_Environment->IBLSource() : nullptr;
        const bool hasIBL = (ibl != nullptr);

        shader->SetInt("u_HasIBL", hasIBL ? 1 : 0);

        if (hasIBL)
        {
            ibl->BindIrradiance(5);
            ibl->BindPrefiltered(6);
            ibl->BindBRDFLut(7);

            shader->SetInt("u_IrradianceMap", 5);
            shader->SetInt("u_PrefilteredMap", 6);
            shader->SetInt("u_BRDFLut", 7);
        }


        // Shadow map — slot 8
        if (m_ShadowMapID != 0)
        {
            RenderCommand::BindTextureUnit(8, m_ShadowMapID);
            shader->SetInt("u_ShadowMap", 8);
            shader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(m_LightSpaceMatrix));
            shader->SetInt("u_HasShadowMap", 1);
        }
        else
        {
            shader->SetInt("u_HasShadowMap", 0);
        }

        // ── SCENE_DEPTH_SURFACE_V1 — a cena opaca atras, no slot 9 ──────────
        //
        // Slot 9 porque 0..4 sao os mapas do material, 5/6/7 o IBL e 8 o
        // shadow map. Ligado SO no passe transparente: no opaco esta mesma
        // textura e um attachment do FBO corrente, e ler dela ali e
        // comportamento indefinido.
        //
        // u_ScreenSize vai SEMPRE, mesmo sem a textura: um material que usa
        // Screen UV precisa do tamanho da tela para converter gl_FragCoord, e
        // uniform declarada e nao enviada vale zero em silencio — armadilha ja
        // anotada no projeto.
        shader->SetFloat2("u_ScreenSize", m_ScreenSize);

        // ── SCENE_HEIGHT_V1 — slots 10 e 11 ──────────────────────────────
        //
        // Vai para QUALQUER material, transparente ou nao: ao contrario do
        // G-Buffer logo abaixo, aqui nao ha ciclo de leitura e escrita — o mapa
        // de topo foi gerado num passe anterior, num alvo que ja terminou.
        if (m_SceneHeightID != 0 && m_SceneSeedID != 0)
        {
            RenderCommand::BindTextureUnit(10, m_SceneHeightID);
            RenderCommand::BindTextureUnit(11, m_SceneSeedID);
            shader->SetInt("u_SceneHeightMap", 10);
            shader->SetInt("u_SceneSeedMap", 11);
            shader->SetMat4("u_SceneHeightMatrix", glm::value_ptr(m_SceneHeightMatrix));
            shader->SetInt("u_HasSceneHeight", 1);
        }
        else
        {
            shader->SetInt("u_HasSceneHeight", 0);
        }

        if (transparent && m_ScenePositionID != 0)
        {
            RenderCommand::BindTextureUnit(9, m_ScenePositionID);
            shader->SetInt("u_ScenePosition", 9);
            shader->SetInt("u_HasSceneDepth", 1);
        }
        else
        {
            shader->SetInt("u_HasSceneDepth", 0);
        }

        // ── FORWARD_SHADOW_V1 — as cascatas, no slot 13 ──────────────────────
        //
        // UNIDADE 13, e a escolha e deliberada. Os slots 0-4 sao os mapas do
        // material, 5/6/7 o IBL, 8 a shadow map legada, 9 o G-Buffer e 10/11 o
        // mapa de altura. Sobrariam 12 — mas o lighting pass amarra um SAMPLER
        // OBJECT nas unidades 11 e 12, e sampler object fica presa a unidade
        // ate ser trocada. Um sampler com GL_TEXTURE_COMPARE_MODE ligado sobre
        // uma amostragem crua e comportamento indefinido.
        //
        // A unidade 13 nao e tocada por nenhum passe, entao a textura vale com
        // os PARAMETROS DELA MESMA — NEAREST e sem comparacao, que e
        // exatamente o que a amostragem crua daqui espera.
        if (m_CascadeCount > 0 && m_CascadeArrayID != 0)
        {
            RenderCommand::BindTextureUnit(13, m_CascadeArrayID);
            shader->SetInt("u_ForwardShadowArray", 13);
            shader->SetInt("u_ForwardCascadeCount", m_CascadeCount);
            shader->SetMat4("u_ForwardShadowView", glm::value_ptr(m_CascadeView));

            for (int i = 0; i < m_CascadeCount; ++i)
            {
                const std::string idx = "[" + std::to_string(i) + "]";
                shader->SetMat4(("u_ForwardCascadeMatrix" + idx).c_str(),
                    glm::value_ptr(m_CascadeMatrices[i]));
                shader->SetFloat(("u_ForwardCascadeSplit" + idx).c_str(),
                    m_CascadeSplits[i]);
                shader->SetFloat(("u_ForwardCascadeTexel" + idx).c_str(),
                    m_CascadeTexelWorld[i]);
            }
        }
        else
        {
            // Uniform declarada e nao enviada vale ZERO em silencio — e zero
            // aqui significaria "cascata 0 valida", que amostraria a unidade 13
            // com o que estivesse nela. Mandar o desligamento e obrigatorio.
            shader->SetInt("u_ForwardCascadeCount", 0);
        }

        RenderCommand::DrawIndexed(mesh.GetVertexArray());
    }

    void MeshRenderer::SetShadowMap(uint32_t depthMapID, const glm::mat4& lightSpaceMatrix)
    {
        m_ShadowMapID = depthMapID;
        m_LightSpaceMatrix = lightSpaceMatrix;
    }

    // ── FORWARD_SHADOW_V1 ────────────────────────────────────────────────────
    //
    // Ver a nota longa na declaracao. Copia por valor, na hora; ponteiro nulo
    // ou passe nao inicializado zera a contagem e o shader volta a nao ter
    // sombra, que e o comportamento anterior a esta rodada.
    void MeshRenderer::SetCascadedShadow(const CascadedShadowPass* csm, const glm::mat4& view)
    {
        if (!csm || !csm->IsInitialized() || csm->GetCascadeCount() <= 0)
        {
            m_CascadeCount = 0;
            m_CascadeArrayID = 0;
            return;
        }

        m_CascadeArrayID = csm->GetDepthArrayID();
        m_CascadeView = view;
        m_CascadeCount = csm->GetCascadeCount();
        if (m_CascadeCount > AXE_SHADOW_CASCADES) m_CascadeCount = AXE_SHADOW_CASCADES;

        const auto& cascades = csm->GetCascades();
        for (int i = 0; i < m_CascadeCount; ++i)
        {
            m_CascadeMatrices[i] = cascades[i].LightSpaceMatrix;
            m_CascadeSplits[i] = cascades[i].SplitDepth;
            m_CascadeTexelWorld[i] = cascades[i].TexelWorldSize;
        }
    }

    void MeshRenderer::SetSceneHeightSource(uint32_t heightMapID, uint32_t seedMapID,
        const glm::mat4& topDownMatrix)
    {
        m_SceneHeightID = heightMapID;
        m_SceneSeedID = seedMapID;
        m_SceneHeightMatrix = topDownMatrix;
    }

    void MeshRenderer::SetSceneDepthSource(uint32_t scenePositionTexID,
        uint32_t screenWidth, uint32_t screenHeight)
    {
        m_ScenePositionID = scenePositionTexID;

        // Piso em 1: o shader divide gl_FragCoord por isto, e um framebuffer de
        // altura zero num frame de redimensionamento daria divisao por zero —
        // que em GLSL nao e erro, e infinito que se espalha em silencio.
        m_ScreenSize = glm::vec2(
            (float)(screenWidth > 0 ? screenWidth : 1u),
            (float)(screenHeight > 0 ? screenHeight : 1u));
    }

    void MeshRenderer::End()
    {}
}