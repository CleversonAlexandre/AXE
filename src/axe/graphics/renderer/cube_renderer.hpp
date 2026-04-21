#pragma once

#include "axe/core/types.hpp"
#include <memory>

namespace axe
{
	class Shader;
	class VertexArray;
	class Pipeline;
	class Texture2D;

	class AXE_API CubeRenderer
	{
	public:
		CubeRenderer();

		void Render(float timeSeconds, float aspecRatio);

	private:
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<VertexArray> m_VertexArray;
		std::shared_ptr<Pipeline> m_Pipeline;
		std::shared_ptr<Texture2D>m_Texture;
	};
}