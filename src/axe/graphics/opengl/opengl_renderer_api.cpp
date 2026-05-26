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

	void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		glViewport(x, y, width, height);
	}

	void OpenGLRendererAPI::SetClearColor(float r, float g, float b, float a)
	{
		glClearColor(r, g, b, a);
	}

	void OpenGLRendererAPI::Clear()
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void OpenGLRendererAPI::SetDepthTest(bool enabled)
	{
		if (enabled) glEnable(GL_DEPTH_TEST);
		else         glDisable(GL_DEPTH_TEST);
	}

	void OpenGLRendererAPI::SetDepthWrite(bool enabled)
	{
		glDepthMask(enabled ? GL_TRUE : GL_FALSE);
	}

	void OpenGLRendererAPI::SetDepthFunc(DepthFunc func)
	{
		switch (func)
		{
		case DepthFunc::Less:      glDepthFunc(GL_LESS);    break;
		case DepthFunc::LessEqual: glDepthFunc(GL_LEQUAL);  break;
		}
	}

	void OpenGLRendererAPI::SetCullFace(bool enabled)
	{
		if (enabled) glEnable(GL_CULL_FACE);
		else         glDisable(GL_CULL_FACE);
	}

	void OpenGLRendererAPI::DrawIndexedCount(uint32_t indexCount)
	{
		glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
	}

	void OpenGLRendererAPI::BindFramebuffer(uint32_t id)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, id);
	}
}