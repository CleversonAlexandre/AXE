#pragma once

#include "axe/core/types.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/texture.hpp"
#include <memory>


namespace axe
{
	class Shader;
	class VertexArray;

	class AXE_API TriangleRenderer
	{
	public:
		TriangleRenderer();

		void Render();

	private:
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<VertexArray> m_VertexArray;
		std::shared_ptr<Texture2D> m_Texture;
		std::shared_ptr<Pipeline> m_Pipeline;
	};
}