#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <string>

namespace axe
{

	struct AXE_API SceneEnvironment
	{
		std::shared_ptr<CubemapTexture> Skybox;
		std::string                     SkyboxPath;
		bool                            ShowSkybox = true;
		float                           SkyboxRotation = 0.0f; // graus em Y

		bool HasSkybox() const { return Skybox && Skybox->IsLoaded(); }

		void LoadHDRI(const std::string& filepath)
		{
			Skybox = CubemapTexture::CreateFromHDRI(filepath);
			SkyboxPath = filepath;
		}

		// ── SKY_IBL_V1 ───────────────────────────────────────────────────────
		//
		// Cubemap de iluminacao gerado do CEU PROCEDURAL. Publicado a cada
		// frame pelo SceneRenderer a partir do SkyboxRenderer — que e quem
		// sabe desenhar o ceu e quando ele mudou.
		//
		// Slot SEPARADO do Skybox de proposito: o Skybox e o HDRI de ARQUIVO,
		// tem SkyboxPath, e comparado com EnvironmentComponent::HDRIPath no
		// load da cena. Escrever a captura procedural ali dentro faria o
		// caminho do HDRI se confundir com a propria captura.
		//
		// `mutable` porque isto e CACHE, e nao estado da cena: quase todo
		// mundo segura o SceneEnvironment por const*, e trocar a constness de
		// meia duzia de assinaturas para publicar um cache seria pior.
		mutable std::shared_ptr<CubemapTexture> SkyIBL;

		// Quem fornece irradiance/prefiltered/BRDF neste frame.
		//
		// O ceu procedural tem PRIORIDADE quando existe: se ele esta ligado, e
		// ele que esta na tela, e a luz de ambiente tem de ser a do que se ve.
		// Sem ele, cai no HDRI — o comportamento de sempre.
		const CubemapTexture* IBLSource() const
		{
			if (SkyIBL && SkyIBL->HasIBL()) return SkyIBL.get();
			if (Skybox && Skybox->HasIBL()) return Skybox.get();
			return nullptr;
		}

		bool HasIBL() const
		{
			return IBLSource() != nullptr;
		}

		// Retorna a view do skybox com rotação aplicada
		glm::mat4 GetSkyboxView(const glm::mat4& view) const
		{
			glm::mat4 rotY = glm::rotate(glm::mat4(1.0f),
				glm::radians(SkyboxRotation), glm::vec3(0, 1, 0));
			return glm::mat4(glm::mat3(view)) * rotY;
		}
	};

} // namespace axe