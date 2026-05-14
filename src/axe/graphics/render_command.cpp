#include "render_command.hpp"


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
	void RenderCommand::SetPolygonMode(RendererAPI::PolygonMode mode)
	{
		s_RendererAPI->SetPolygonMode(mode);
	}

	void RenderCommand::DrawLines(const std::shared_ptr<VertexArray>& vertexArray, std::uint32_t vertexCount)
	{
		s_RendererAPI->DrawLines(vertexArray, vertexCount);
	}
	void RenderCommand::DrawIndexedCount(uint32_t indexCount)
	{
		s_RendererAPI->DrawIndexedCount(indexCount);
	}
}