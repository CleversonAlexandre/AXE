
#include "axe/utils/glm_config.hpp"
#include "mesh_renderer.hpp"

#include "axe/graphics/shader.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"

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

        // Normal matrix — corrige normais quando o objeto tem scale não-uniforme
        // É a transposta da inversa da parte 3x3 do model matrix
        uniform mat3 u_NormalMatrix;

        out vec3 v_Normal;
        out vec3 v_FragPos;

        void main()
        {
            vec4 worldPos = u_Model * vec4(a_Position, 1.0);

            v_FragPos  = worldPos.xyz;
            v_Normal   = normalize(u_NormalMatrix * a_Normal);

            gl_Position = u_ViewProjection * worldPos;
        }
    )";

		const std::string fragmentSrc = R"(
        #version 460 core

        layout(location = 0) out vec4 FragColor;

        in vec3 v_Normal;
        in vec3 v_FragPos;

        // Material
        uniform vec4  u_Color;
        uniform float u_SpecularStrength;
        uniform float u_Shininess;

        // Luz direcional
        uniform vec3  u_LightDirection;
        uniform vec3  u_LightColor;
        uniform float u_LightIntensity;
        uniform float u_AmbientStrength;

        // Câmera
        uniform vec3 u_CameraPosition;

        // Flag — se não há luz na cena, renderiza flat
        uniform bool u_HasLight;

        void main()
        {
            vec3 objectColor = u_Color.rgb;

            if (!u_HasLight)
            {
                FragColor = vec4(objectColor, u_Color.a);
                return;
            }

            vec3 normal   = normalize(v_Normal);
            vec3 lightDir = normalize(-u_LightDirection); // inverte — aponta da superfície para a luz

            // Ambient
            vec3 ambient = u_AmbientStrength * u_LightColor;

            // Diffuse
            float diff    = max(dot(normal, lightDir), 0.0);
            vec3  diffuse = diff * u_LightColor * u_LightIntensity;

            // Specular (Blinn-Phong)
            vec3  viewDir  = normalize(u_CameraPosition - v_FragPos);
            vec3  halfDir  = normalize(lightDir + viewDir);
            float spec     = pow(max(dot(normal, halfDir), 0.0), u_Shininess);
            vec3  specular = u_SpecularStrength * spec * u_LightColor;

            vec3 result = (ambient + diffuse + specular) * objectColor;
            FragColor   = vec4(result, u_Color.a);
        }
    )";

        m_Shader = Shader::Create(vertexSrc, fragmentSrc);

        PipelineSpecification spec;
        spec.Shader = m_Shader;
        spec.DepthTest = true;

        m_Pipeline = Pipeline::Create(spec);

        //Material padão - usado quando o objeto não tem material proprio
        m_DefaultMaterial = std::make_shared<Material>(m_Shader, "Default");
    }

    void MeshRenderer::Begin(const glm::mat4& viewProjection, const glm::vec3& cameraPosition)
    {
        m_ViewProjection = viewProjection;
        m_CameraPosition = cameraPosition;
    }

	void MeshRenderer::DrawMesh(const Mesh& mesh, const glm::mat4& model,
		const Material* material, const DirectionalLight* light)
	{
		m_Pipeline->Bind();
		mesh.GetVertexArray()->Bind();

		// Matrizes
		m_Shader->SetMat4("u_Model", glm::value_ptr(model));
		m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(m_ViewProjection));

		// Normal matrix — transposta da inversa da parte 3x3 do model
		// Necessário para scale não-uniforme não distorcer as normais
		glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
		m_Shader->SetMat3("u_NormalMatrix", glm::value_ptr(normalMatrix));

		// Câmera
		m_Shader->SetFloat3("u_CameraPosition", m_CameraPosition);

		// Material
		const Material* mat = material ? material : m_DefaultMaterial.get();
		m_Shader->SetFloat4("u_Color", mat->Color);
		m_Shader->SetFloat("u_SpecularStrength", mat->SpecularStrength);
		m_Shader->SetFloat("u_Shininess", mat->Shininess);

		// Luz
		if (light)
		{
			m_Shader->SetBool("u_HasLight", true);
			m_Shader->SetFloat3("u_LightDirection", light->Direction);
			m_Shader->SetFloat3("u_LightColor", light->Color);
			m_Shader->SetFloat("u_LightIntensity", light->Intensity);
			m_Shader->SetFloat("u_AmbientStrength", light->AmbientStrength);
		}
		else
		{
			m_Shader->SetBool("u_HasLight", false);
		}

		RenderCommand::DrawIndexed(mesh.GetVertexArray());
	}

    void MeshRenderer::End()
    {}
}