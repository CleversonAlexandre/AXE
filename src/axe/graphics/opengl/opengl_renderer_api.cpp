#include "opengl_renderer_api.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/buffer.hpp" 

#include <glad/glad.h>

namespace axe
{
	void OpenGLRendererAPI::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray)
	{
		glDrawElements(
			GL_TRIANGLES,
			vertexArray->GetIndexBuffer()->GetCount(),
			GL_UNSIGNED_INT,
			nullptr
		);
	}

	void OpenGLRendererAPI::SetPolygonMode(PolygonMode mode)
	{
		switch (mode)
		{
		case PolygonMode::Fill:
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			break;

		case PolygonMode::Line:
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			break;
		}
	}
	void OpenGLRendererAPI::DrawLines(const std::shared_ptr<VertexArray>& vertexArray, std::uint32_t vertexCount)
	{
		vertexArray->Bind();
		glDrawArrays(GL_LINES, 0, vertexCount);
	}
}