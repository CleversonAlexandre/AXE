#include "material.hpp"

namespace axe
{
	Material::Material(std::shared_ptr<Shader> shader, const std::string& name)
		: m_Shader(shader), m_Name(name)
	{}

	void Material::Apply() const
	{
		//Bind do shader é feito pelo Pipeline - aqui só enviamos os parâmetros
		if (m_Shader)
			m_Shader->SetFloat4("u_Color", Color);	
	}
}//namespace axe