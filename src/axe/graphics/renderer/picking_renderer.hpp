#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include "axe/graphics/framebuffer.hpp"

namespace axe
{
	class Mesh;
	class Shader;
	class Pipeline;
	class Framebuffer;

	class AXE_API PickingRenderer
	{
	public:
		PickingRenderer();

		void Begin(const glm::mat4& viewProjection);
		void DrawMesh(const Mesh& mesh, const glm::mat4& model, std::uint32_t objectID);
		void DrawCube(const glm::mat4& model, std::uint32_t objectID);
		void End();

		//Lê o ID do objeto no pixel (x,y) do framebuffer de picking
		std::uint32_t ReadPixel(std::uint32_t x, std::uint32_t y) const;

		void Resize(std::uint32_t width, std::uint32_t height);

		std::uint32_t GetFramebufferHeight() const
		{
			return m_Framebuffer->GetSpecification().Height;
		}

	private:
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<Pipeline> m_Pipeline;
		std::shared_ptr<Framebuffer> m_Framebuffer;

		glm::mat4 m_ViewProjection{ 1.0f };

	};
}//namespace axe