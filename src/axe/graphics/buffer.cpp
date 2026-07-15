#include "axe/graphics/buffer.hpp"
#include "axe/graphics/opengl/opengl_buffer.hpp"

namespace axe
{

	std::shared_ptr<VertexBuffer> VertexBuffer::Create(const void* data, std::uint32_t size)
	{
		return std::make_shared<OpenGLVertexBuffer>(data, size);
	}

	std::shared_ptr<VertexBuffer> VertexBuffer::Create(std::uint32_t size)
	{
		return std::make_shared<OpenGLVertexBuffer>(size);
	}

	std::shared_ptr<IndexBuffer> IndexBuffer::Create(const std::uint32_t* data, std::uint32_t count)
	{
		return std::make_shared<OpenGLIndexBuffer>(data, count);
	}
}