
#include "viewport_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"



#include "glad/glad.h"


namespace axe
{
	void ViewportRenderer::Initialize()
	{
		m_CubeRenderer = std::make_unique<CubeRenderer>();
	}

	void ViewportRenderer::RenderToFramebuffer(Framebuffer& framebuffer, uint32_t width, uint32_t height, float timeSeconds)
	{
		if (!m_CubeRenderer)
			return;

		framebuffer.Bind();

		glViewport(0, 0, width, height);
		glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (m_CubeRenderer)
		{
			const float aspect = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;
			m_CubeRenderer->Render(timeSeconds, aspect);
		}

		framebuffer.Unbind();
	}
}