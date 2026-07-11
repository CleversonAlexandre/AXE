#pragma once
#include "axe/graphics/renderer/point_shadow_pass.hpp"

namespace axe
{
	class Shader;

	class OpenGLPointShadowPass : public PointShadowPass
	{
	public:
		~OpenGLPointShadowPass() override;

		void Initialize(uint32_t resolution) override;
		bool IsInitialized() const override { return m_Initialized; }

		void RenderLightShadow(const RenderQueue& queue, int layer,
			const glm::vec3& lightPos, float farPlane) override;

		uint32_t GetTextureID() const override { return m_CubeArray; }

	private:
		bool     m_Initialized = false;
		uint32_t m_Resolution = 512;
		uint32_t m_CubeArray = 0; // GL_TEXTURE_CUBE_MAP_ARRAY, R32F
		uint32_t m_DepthRBO = 0;
		uint32_t m_FBO = 0;
		std::shared_ptr<Shader> m_DepthShader;

		// Estado salvo no início do RenderLightShadow e restaurado no fim
		// — quem clobbera estado, restaura estado (lição do CSM: o End()
		// sem restore apagou TODOS os previews do editor uma vez).
		int m_SavedFBO = 0;
		int m_SavedViewport[4] = { 0, 0, 0, 0 };
	};
}