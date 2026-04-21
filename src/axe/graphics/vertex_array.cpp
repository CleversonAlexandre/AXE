#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/opengl/opengl_vertex_array.hpp"

namespace axe
{
	std::shared_ptr<VertexArray> VertexArray::Create()
	{
		return std::make_shared<OpenGLVertexArray>();
	}
}