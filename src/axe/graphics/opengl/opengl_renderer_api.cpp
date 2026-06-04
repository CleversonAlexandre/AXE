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
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	void OpenGLRendererAPI::ClearColorDepth()
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

	void OpenGLRendererAPI::SetBlend(bool enabled)
	{
		if (enabled) glEnable(GL_BLEND);
		else         glDisable(GL_BLEND);
	}

	void OpenGLRendererAPI::SetBlendFunc(uint32_t src, uint32_t dst)
	{
		glBlendFunc(src, dst);
	}

	void OpenGLRendererAPI::SetDepthFunc(DepthFunc func)
	{
		switch (func)
		{
		case DepthFunc::Less:      glDepthFunc(GL_LESS);    break;
		case DepthFunc::LessEqual: glDepthFunc(GL_LEQUAL);  break;
		case DepthFunc::Always:    glDepthFunc(GL_ALWAYS);  break;
		}
	}

	void OpenGLRendererAPI::SetCullFace(bool enabled)
	{
		if (enabled) glEnable(GL_CULL_FACE);
		else         glDisable(GL_CULL_FACE);
	}

	void OpenGLRendererAPI::SetCullMode(bool frontFace)
	{
		glCullFace(frontFace ? GL_FRONT : GL_BACK);
	}

	void OpenGLRendererAPI::BindTextureUnit(uint32_t slot, uint32_t textureID)
	{
		glBindTextureUnit(slot, textureID);
	}

	void OpenGLRendererAPI::SetColorWrite(bool enabled)
	{
		GLboolean v = enabled ? GL_TRUE : GL_FALSE;
		glColorMask(v, v, v, v);
	}

	void OpenGLRendererAPI::SetStencilTest(bool enabled)
	{
		if (enabled) glEnable(GL_STENCIL_TEST);
		else         glDisable(GL_STENCIL_TEST);
	}

	void OpenGLRendererAPI::SetStencilWrite(uint32_t mask)
	{
		glStencilMask(mask);
	}

	void OpenGLRendererAPI::SetStencilFunc(uint32_t func, int ref, uint32_t mask)
	{
		glStencilFunc(func, ref, mask);
	}

	void OpenGLRendererAPI::SetStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass)
	{
		glStencilOp(fail, zfail, zpass);
	}

	void OpenGLRendererAPI::DrawIndexedCount(uint32_t indexCount)
	{
		glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
	}

	void OpenGLRendererAPI::BindFramebuffer(uint32_t id)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, id);
	}

	void OpenGLRendererAPI::ResetState()
	{
		glBindVertexArray(0);
		glUseProgram(0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	void OpenGLRendererAPI::BlitDepth(uint32_t srcFBO, uint32_t dstFBO,
		uint32_t width, uint32_t height)
	{
		// Copia depth do G-Buffer (DEPTH32F texture) para o HDR (DEPTH24STENCIL8 renderbuffer).
		// GL não permite blit entre formatos incompatíveis de depth diretamente,
		// mas GL_DEPTH_BUFFER_BIT funciona desde que ambos tenham depth attachment.
		// O stencil do HDR não vem do G-Buffer — é inicializado a 0 pelo Clear().
		glBlitNamedFramebuffer(srcFBO, dstFBO,
			0, 0, width, height,
			0, 0, width, height,
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}
}