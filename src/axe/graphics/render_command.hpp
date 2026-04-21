#pragma once

#include "axe/core/types.hpp"
#include <memory>

namespace axe
{
	class RendererAPI;
	class VertexArray;

	class AXE_API RenderCommand
	{
	public:
		static void Init();

		static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);

	private:
		static std::unique_ptr<RendererAPI> s_RendererAPI;
	};
}