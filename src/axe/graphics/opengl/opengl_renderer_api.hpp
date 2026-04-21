#pragma once

#include "axe/core/types.hpp"
#include "axe/graphics/renderer_api.hpp"

namespace axe
{
	class AXE_API OpenGLRendererAPI final : public RendererAPI
	{
	public:
		void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) override;
	};
}