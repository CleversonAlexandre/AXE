#include "material.hpp"
#include "axe/utils/glm_config.hpp"

namespace axe
{
	Material::Material(std::shared_ptr<Shader> shader, const std::string& name)
		: m_Shader(shader), m_Name(name)
	{}

	void Material::Apply() const
	{
		//Bind do shader é feito pelo Pipeline - aqui só enviamos os parâmetros
		if (!m_Shader) return;

		m_Shader->SetFloat4("u_Color", Color);
		m_Shader->SetFloat("u_SpecularStrength", SpecularStrength);
		m_Shader->SetFloat("u_Shininess", Shininess);
		m_Shader->SetFloat("u_Metallic", Metallic);
		m_Shader->SetFloat("u_Roughness", Roughness);
		m_Shader->SetFloat("u_AO", AO);
		m_Shader->SetInt("u_UsePBR", UsePBR ? 1 : 0);

		// Texturas — slots fixos
		// 0 = Albedo, 1 = Normal, 2 = Roughness, 3 = Metallic, 4 = AO

		m_Shader->SetInt("u_AlbedoMap", 0);
		m_Shader->SetInt("u_NormalMap", 1);
		m_Shader->SetInt("u_RoughnessMap", 2);
		m_Shader->SetInt("u_MetallicMap", 3);
		m_Shader->SetInt("u_AOMap", 4);

		m_Shader->SetInt("u_HasAlbedoMap", HasAlbedoMap() ? 1 : 0);
		m_Shader->SetInt("u_HasNormalMap", HasNormalMap() ? 1 : 0);
		m_Shader->SetInt("u_HasRoughnessMap", HasRoughnessMap() ? 1 : 0);
		m_Shader->SetInt("u_HasMetallicMap", HasMetallicMap() ? 1 : 0);
		m_Shader->SetInt("u_HasAOMap", HasAOMap() ? 1 : 0);

		if (HasAlbedoMap())    AlbedoMap->Bind(0);
		if (HasNormalMap())    NormalMap->Bind(1);
		if (HasRoughnessMap()) RoughnessMap->Bind(2);
		if (HasMetallicMap())  MetallicMap->Bind(3);
		if (HasAOMap())        AOMap->Bind(4);
	}
}//namespace axe