#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <glm/glm.hpp>
#include "axe/lighting/directional_light.hpp"



namespace axe
{
	class Mesh;
	class Shader;
	class Pipeline;
	class Material;

	class AXE_API MeshRenderer
	{
	public:
		MeshRenderer();

		void Begin(const glm::mat4& viewProjection, const glm::vec3& cameraPosition);
		void DrawMesh(const Mesh& mesh, const glm::mat4& model, const Material* material = nullptr,
																const DirectionalLight* light = nullptr);		
		void End();
		std::shared_ptr<Shader> GetDefaultShader() const { return m_Shader; }
	private:
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<Pipeline> m_Pipeline;
		std::shared_ptr<Material> m_DefaultMaterial;

		glm::mat4 m_ViewProjection{ 1.0f };
		glm::vec3 m_CameraPosition{ 0.0f };
	};
}