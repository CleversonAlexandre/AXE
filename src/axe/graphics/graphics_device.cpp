#include "axe/graphics/graphics_device.hpp"

#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace axe
{
	bool GraphicsDevice::Initialize(Window* window)
	{
		m_Window = window;

		if (m_Window == nullptr)
		{
			AXE_CORE_ERROR("GraphicsDevice received null window");
			return false;
		}

		if (m_Window->GetNativeWindow() == nullptr)
		{
			AXE_CORE_ERROR("GraphicsDevice received invalid native window");
			return false;
		}

		AXE_CORE_INFO("GraphicsDevice initialized");
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_DEPTH_TEST);

		// Mostrar informações do OpenGL (opcional, só para debug)
		AXE_CORE_INFO("OpenGL Version: {}", (const char*)glGetString(GL_VERSION));
		AXE_CORE_INFO("GLSL Version: {}", (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
		AXE_CORE_INFO("Renderer: {}", (const char*)glGetString(GL_RENDERER));

		AXE_CORE_INFO("GraphicsDevice Initialized successfully");
		return true;
	}

	void GraphicsDevice::Shutdown()
	{
		AXE_CORE_INFO("GraphicsDevice shutdown");
	}

	void GraphicsDevice::BeginFrame()
	{
		GLFWwindow* native = static_cast<GLFWwindow*>(m_Window->GetNativeWindow());

		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(native, &width, &height);

		glViewport(0, 0, width, height);
		glClearColor(
			m_ClearColor[0],
			m_ClearColor[1],
			m_ClearColor[2],
			m_ClearColor[3]
		);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void GraphicsDevice::EndFrame()
	{
		// Por enquanto vazio.
		// Depois pode ter flush, stats, render passes, debug markers, etc.
	}

	void GraphicsDevice::SetClearColor(float r, float g, float b, float a)
	{
		m_ClearColor[0] = r;
		m_ClearColor[1] = g;
		m_ClearColor[2] = b;
		m_ClearColor[3] = a;
	}
}