#pragma once

#include "axe/core/types.hpp"
#include <memory>
#include "renderer_api.hpp"


namespace axe
{
	class RendererAPI;
	class VertexArray;

	class AXE_API RenderCommand
	{
	public:
		static void Init();

		static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);
		static void SetPolygonMode(RendererAPI::PolygonMode mode);
		static void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, std::uint32_t vertexCount);
	private:
		static std::unique_ptr<RendererAPI> s_RendererAPI;
	};
}