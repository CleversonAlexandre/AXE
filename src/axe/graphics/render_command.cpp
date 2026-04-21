#include "render_command.hpp"
#include "axe/graphics/renderer_api.hpp"

namespace axe
{
	std::unique_ptr<RendererAPI> RenderCommand::s_RendererAPI = nullptr;

	void RenderCommand::Init()
	{
		s_RendererAPI = RendererAPI::Create();
	}

	void RenderCommand::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray)
	{
		s_RendererAPI->DrawIndexed(vertexArray);
	}
}