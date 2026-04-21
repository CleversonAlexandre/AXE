#pragma once

#include "axe/core/types.hpp"
#include <memory>

namespace axe
{
	class Shader;
	class VertexArray;
	class Pipeline;
	class Texture2D;

	class AXE_API QuadRenderer
	{
	public:
		QuadRenderer();

		void Render(float timeSeconds = 0.0f);

	private:
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<VertexArray> m_VertexArray;
		std::shared_ptr<Pipeline> m_Pipeline;
		std::shared_ptr<Texture2D> m_Texture;
	};
}