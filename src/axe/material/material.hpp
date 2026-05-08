#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/utils/glm_config.hpp"

#include <memory>
#include <string>

namespace axe
{
	class AXE_API Material
	{
	public:
		//Cria material com sjader e cor padrão
		Material(std::shared_ptr<Shader> shader, const std::string& name = "Material");

		//Nome para exibição no inspector
		const std::string& GetName() const { return m_Name; }

		//Shader
		glm::vec4 Color{ 0.7f, 0.7f, 0.7f, 1.0f };
		
		float     SpecularStrength = 0.5f;
		float     Shininess = 32.0f;

		//Aplica os parâmetros no shader
		//Chamado pelo MeshRenderer antes de  DrawIndexed
		void Apply() const;

	private:
		std::string m_Name;
		std::shared_ptr<Shader> m_Shader;

	};
}//namespace axe

