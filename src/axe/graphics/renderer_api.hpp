#pragma once

#include "axe/core/types.hpp"
#include <memory>

namespace axe
{
	class VertexArray;

	class AXE_API RendererAPI
	{
	public:
		enum class API
		{
			None = 0,
			OpenGL = 1
		};

		enum class PolygonMode
		{
			Fill = 0,
			Line
		};

	public:
		virtual ~RendererAPI() = default;

		virtual void SetPolygonMode(PolygonMode mode) = 0;

		virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) = 0;
		virtual void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, std::uint32_t vertexCount) = 0;

		static API GetAPI() { return s_API; }
		static std::unique_ptr<RendererAPI> Create();

		//virtual void SetLineWidth(float width) = 0;

	private:
		static API s_API;
	};
}