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

	void OpenGLRendererAPI::DrawArraysStrip(uint32_t vertexCount)
	{
		glDrawArrays(GL_TRIANGLE_STRIP, 0, vertexCount);
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

	// Tradução BlendFactor (agnóstico) -> enum nativo do OpenGL. Fica
	// CONTIDA no back-end: nenhum hexadecimal de GL escapa pra engine.
	static GLenum ToGLBlendFactor(RendererAPI::BlendFactor f)
	{
		switch (f)
		{
		case RendererAPI::BlendFactor::Zero:             return GL_ZERO;
		case RendererAPI::BlendFactor::One:              return GL_ONE;
		case RendererAPI::BlendFactor::SrcColor:         return GL_SRC_COLOR;
		case RendererAPI::BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
		case RendererAPI::BlendFactor::SrcAlpha:         return GL_SRC_ALPHA;
		case RendererAPI::BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
		case RendererAPI::BlendFactor::DstAlpha:         return GL_DST_ALPHA;
		case RendererAPI::BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
		}
		return GL_ONE;
	}

	void OpenGLRendererAPI::SetBlendFunc(BlendFactor src, BlendFactor dst)
	{
		glBlendFunc(ToGLBlendFactor(src), ToGLBlendFactor(dst));
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

	uint32_t OpenGLRendererAPI::GetBoundFramebuffer()
	{
		GLint id = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &id);
		return (uint32_t)id;
	}

	RendererAPI::Viewport OpenGLRendererAPI::GetViewport()
	{
		GLint vp[4] = { 0, 0, 0, 0 };
		glGetIntegerv(GL_VIEWPORT, vp);
		return { (uint32_t)vp[0], (uint32_t)vp[1], (uint32_t)vp[2], (uint32_t)vp[3] };
	}

	void OpenGLRendererAPI::ResetState()
	{
		glBindVertexArray(0);
		glUseProgram(0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);

		// Depth: ligado, escrevendo, LESS.
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_LESS);

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		// Blend e Cull — as duas flags que ANTES o ResetState deixava
		// vazar (a "meia-verdade" que originou os bugs de cull e blend).
		// Agora o baseline é EXPLÍCITO e idêntico ao que
		// GraphicsDevice::Initialize estabelece no boot: blend ligado com
		// SRC_ALPHA/ONE_MINUS_SRC_ALPHA e cull desligado (a engine
		// renderiza double-sided por padrão; quem quer cull o liga no seu
		// pipeline/passe). Assim nenhum passe futuro herda lixo de estado.
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_CULL_FACE);

		// Stencil desligado por padrão.
		glDisable(GL_STENCIL_TEST);
		glStencilMask(0xFF);
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