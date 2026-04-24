#pragma once
#include "axe/utils/glm_config.hpp"
#include "axe/core/types.hpp"
#include <memory>
#include "axe/graphics/opengl/opengl_renderer_api.hpp"

namespace axe
{
	class Shader;
	class VertexArray;
	class Pipeline;
	class Texture2D;
	class PolyngMode;

	class AXE_API CubeRenderer
	{
	public:
		CubeRenderer();

		void DrawCubeWireframe(const glm::mat4& model);
		void Begin(const glm::mat4& viewProjection);
		void DrawCube(const glm::mat4& model, bool selected = false, const glm::vec4& color = { 1.0f, 0.85f, 0.1f, 1.0f });
		void End();
		

	private:

		glm::mat4 m_ViewProjection = glm::mat4(1.0f);
		
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<VertexArray> m_VertexArray;
		std::shared_ptr<Pipeline> m_Pipeline;
		std::shared_ptr<Texture2D> m_Texture;
	};
}