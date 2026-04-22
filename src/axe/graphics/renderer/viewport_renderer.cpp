#include "axe/graphics/renderer/viewport_renderer.hpp"

#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"
#include "axe/graphics/editor_camera.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace axe
{
	void ViewportRenderer::Initialize()
	{
		m_CubeRenderer = std::make_unique<CubeRenderer>();
		m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 100.0f);



		//m_Camera->SetPosition(glm::vec3(0.0f, 0.0f, 3.0f));
		//m_Camera->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));
	}

	void ViewportRenderer::RenderToFramebuffer(Framebuffer& framebuffer, std::uint32_t width, std::uint32_t height, float timeSeconds)
	{
		framebuffer.Bind();

		glViewport(0, 0, width, height);
		glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (m_Camera && height > 0)
		{
			m_Camera->SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
			m_Camera->SetViewportSize((float)width, (float)height);

		}

		if (m_CubeRenderer && m_Camera)
		{
			m_CubeRenderer->Render(timeSeconds, *m_Camera);
		}

		framebuffer.Unbind();
	}

	void ViewportRenderer::OnMouseRotate(const glm::vec2& delta)
	{
		if (m_Camera)
			m_Camera->Rotate(delta);
	}

	void ViewportRenderer::OnMousePan(const glm::vec2& delta)
	{
		if (m_Camera)
			m_Camera->Pan(delta);
	}

	void ViewportRenderer::OnMouseZoom(float delta)
	{
		if (m_Camera)
			m_Camera->Zoom(delta);
	}
}