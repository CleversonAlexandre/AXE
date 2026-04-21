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
}