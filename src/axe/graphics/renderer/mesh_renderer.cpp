
#include "axe/utils/glm_config.hpp"
#include "mesh_renderer.hpp"

#include "axe/graphics/shader.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/log/log.hpp"
namespace axe
{
	MeshRenderer::MeshRenderer()
	{
		const std::string vertexSrc = R"(
		#version 460 core

		layout(location = 0) in vec3 a_Position;
		layout(location = 1) in vec3 a_Normal;
		layout(location = 2) in vec2 a_TexCoord;

		uniform mat4 u_Model;
		uniform mat4 u_ViewProjection;
		uniform mat3 u_NormalMatrix;

		out vec3 v_Normal;
		out vec3 v_FragPos;
		out vec2 v_TexCoord;

		void main()
		{
			
			vec4 worldPos  = u_Model * vec4(a_Position, 1.0);
			v_FragPos      = worldPos.xyz;
			v_Normal       = normalize(u_NormalMatrix * a_Normal);
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

		// Material
		uniform vec4  u_Color;
		uniform float u_SpecularStrength;
		uniform float u_Shininess;
		uniform float u_Metallic;
		uniform float u_Roughness;
		uniform float u_AO;
		uniform int   u_UsePBR;

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
				// Aplica normal em tangent space simplificado
				N = normalize(v_Normal + normalTex * 0.5);
			}

			if (u_UsePBR == 0 || !u_HasLight)
			{
				// --- Blinn-Phong (compatibilidade) ---
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
				vec3 result   = (ambient + diffuse + specular) * albedo;
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

			vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

			// Ambient com AO
			vec3 ambient = u_AmbientStrength * u_LightColor * albedo * ao;
			vec3 color   = ambient + Lo;

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
		const Material* material, const DirectionalLight* light)
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

		m_Pipeline->Bind();
		mesh.GetVertexArray()->Bind();

		// Matrizes
		shader->SetMat4("u_Model", glm::value_ptr(model));
		shader->SetMat4("u_ViewProjection", glm::value_ptr(m_ViewProjection));

		glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
		shader->SetMat3("u_NormalMatrix", glm::value_ptr(normalMatrix));

		shader->SetFloat3("u_CameraPosition", m_CameraPosition);

		// Material — base
		shader->SetFloat4("u_Color", mat->Color);
		shader->SetFloat("u_SpecularStrength", mat->SpecularStrength);
		shader->SetFloat("u_Shininess", mat->Shininess);
		shader->SetFloat("u_Metallic", mat->Metallic);
		shader->SetFloat("u_Roughness", mat->Roughness);
		shader->SetFloat("u_AO", mat->AO);
		shader->SetInt("u_UsePBR", mat->UsePBR ? 1 : 0);

		// Texturas
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

		// Luz
		if (light)
		{
			shader->SetBool("u_HasLight", true);
			shader->SetFloat3("u_LightDirection", light->Direction);
			shader->SetFloat3("u_LightColor", light->Color);
			shader->SetFloat("u_LightIntensity", light->Intensity);
			shader->SetFloat("u_AmbientStrength", light->AmbientStrength);
		}
		else
		{
			shader->SetBool("u_HasLight", false);
		}

		RenderCommand::DrawIndexed(mesh.GetVertexArray());
	}
	void MeshRenderer::End()
	{}
}