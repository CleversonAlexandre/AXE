#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/utils/glm_config.hpp"

#include <memory>
#include <string>

#include <map>
#include "axe/graphics/texture.hpp"


namespace axe
{
	class AXE_API Material
	{
	public:
		//Cria material com sjader e cor padrão
		//Material(std::shared_ptr<Shader> shader, const std::string& name = "Material");
		Material(std::shared_ptr<Shader> shader = nullptr, const std::string& name = "Material");
		//Nome para exibição no inspector
		const std::string& GetName() const { return m_Name; }
		std::shared_ptr<Shader> GetShader() const { return m_Shader; }
		void SetShader(std::shared_ptr<Shader> shader) { m_Shader = shader; }

		//Apilica uniforms no shader
		void Apply() const;

		// --- Parâmetros base (compatibilidade com Blinn-Phong) ---
		glm::vec4 Color{ 0.7f, 0.7f, 0.7f, 1.0f };
		float     SpecularStrength = 0.5f;
		float     Shininess = 32.0f;

		//--- Parâmetros PBR -- 
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float AO = 1.0f;
		bool UsePBR = false; // false = Blinn-Phong, true = PBR

		//Texturas PBR
		std::shared_ptr<Texture2D> AlbedoMap;
		std::shared_ptr<Texture2D> NormalMap;
		std::shared_ptr<Texture2D> RoughnessMap;
		std::shared_ptr<Texture2D> MetallicMap;
		std::shared_ptr<Texture2D> AOMap;

		//UUIDs para serialização
		std::string AlbedoUUID;
		std::string NormalUUID;
		std::string RoughnessUUID;
		std::string MetallicUUID;
		std::string AOUUID;

		bool HasAlbedoMap() const { return AlbedoMap && AlbedoMap->IsLoaded(); }
		bool HasNormalMap() const { return NormalMap && NormalMap->IsLoaded(); }
		bool HasRoughnessMap() const { return RoughnessMap && RoughnessMap->IsLoaded(); }
		bool HasMetallicMap()   const { return MetallicMap && MetallicMap->IsLoaded(); }
		bool HasAOMap()         const { return AOMap && AOMap->IsLoaded(); }


		std::shared_ptr<Shader> GetGeometryShader() const { return m_GeometryShader; }
		void SetGeometryShader(std::shared_ptr<Shader> shader) { m_GeometryShader = shader; }
		std::map<std::string, std::shared_ptr<Texture2D>> SamplerTextures;

		// true quando o pin "Opacity" do Material Output está conectado a
		// algo (não é só o valor neutro 1.0) — ver MaterialCompiler::Compile.
		// Materiais transparentes (vidro, etc.) são desenhados num forward
		// pass separado, depois do passe opaco/deferred, com blend
		// habilitado e sem depth-write — ver SceneRenderer::RenderDeferred.
		bool IsTransparent = false;

		// Emissive MÉDIO do material (cor x intensidade), avaliado pelo
		// editor no compile do grafo (MaterialCompiler::ComputeBakedEmissive:
		// renderiza o pin Emissive num FBO 8x8 varrendo UVs e tira a média).
		// Consumido pelo bake de GI e pela captura de reflection — probes
		// SH L1 são ultra-low-frequency, então a MÉDIA é exatamente o que
		// importa de uma superfície emissiva (a tela do arcade banhando a
		// parede), não os pixels dela. vec3(0) = material não emite.
		glm::vec3 BakedEmissive{ 0.0f };
	private:
		std::string m_Name;
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<Shader> m_GeometryShader;

	};
}//namespace axe