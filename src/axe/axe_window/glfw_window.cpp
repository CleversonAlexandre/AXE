#include "glfw_window.hpp"


#include "axe/events/application_event.hpp"
#include "axe/events/mouse_event.hpp"
#include "axe/events/key_event.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "axe/log/log.hpp"




#include <functional>


namespace axe
{
	static bool s_GLFWInitialized = false;

	WindowGlfw::WindowGlfw(const WindowProps& props)
	{

		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		if (!s_GLFWInitialized)
		{
			if (!glfwInit())
			{
				AXE_CORE_ERROR("Failed to initialize GLFW");
				return;
			}
			s_GLFWInitialized = true;
			AXE_CORE_INFO("GLFW initialized");
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);  // <-- ADICIONE ESTA LINHA

		m_Window = glfwCreateWindow(props.Width, props.Height, props.Title.c_str(), nullptr, nullptr);

	
		if (!m_Window)
		{
			AXE_CORE_ERROR("Failed to create GLFW window");
			return;
		}

		glfwSetWindowUserPointer(m_Window, &m_Data);
		

		// ADICIONE ESTA LINHA - FORÇA A JANELA A SER VISÍVEL
		//glfwShowWindow(m_Window);

		glfwMakeContextCurrent(m_Window);

		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		{
			AXE_CORE_ERROR("Failed to initialize OpenGL loader!");
			glfwDestroyWindow(m_Window);
			m_Window = nullptr;
			return;
		}
		SetVSync(true);

		AXE_CORE_INFO("Window created: {} ({}x{})", m_Data.Title, props.Width, props.Height);
		AXE_CORE_INFO("OpenGL loader initialized");



		/* Atualiza o redimensionamento da janela principal*/
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.Width = width;
			data.Height = height;

			WindowResizeEvent event(width, height);
			if (data.EventCallback)
			{
				data.EventCallback(event);
			}
			
			//AXE_CORE_INFO("WindowResizeEvent width {} height {}", width, height);

		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			WindowCloseEvent event;
			if (data.EventCallback)
			{
				data.EventCallback(event);
			}

			//AXE_CORE_INFO("Window Close: ");
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int modes)
		{
			WindowData & data = *(WindowData*)glfwGetWindowUserPointer(window);
			switch (action)
			{
			case GLFW_PRESS:
			{
				KeyPressedEvent event(key, 0);
				if (data.EventCallback)				
					data.EventCallback(event);
				
				//AXE_CORE_INFO(" GLFW_REPEAT: {} {} {}", key, scancode, action, modes);
				break;
			}
			case GLFW_RELEASE:
			{
				KeyReleasedEvent event(key);
				if (data.EventCallback)
				{
					data.EventCallback(event);
				}

				//AXE_CORE_INFO(" GLFW_RELEASE: {}", key);
				break;
			}
			case GLFW_REPEAT:
			{
				KeyPressedEvent event(key, 1);
				if (data.EventCallback)
				{
					data.EventCallback(event);
				}
				//AXE_CORE_INFO(" GLFW_REPEAT: {}", key);
				break;
			}
							
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseScrolledEvent event((float)xOffset, (float)yOffset);
			if (data.EventCallback)
			{
				data.EventCallback(event);
			}
			//AXE_CORE_INFO("MouseScrolledEvent: {} {}", xOffset, yOffset);
		});


		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			KeyTypedEvent event(keycode);
			if (data.EventCallback)
			{
				data.EventCallback(event);
			}
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int modes)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				switch (action)
				{
				case GLFW_PRESS:
				{
					MouseButtonPressedEvent event(button);
					if (data.EventCallback)
					{
						data.EventCallback(event);
					}
					//AXE_CORE_INFO(" GLFW_REPEAT: {} {} {}", button, action, modes);
					break;
				}
				case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent event(button);
					if (data.EventCallback)
					{
						data.EventCallback(event);
					}
					//AXE_CORE_INFO(" GLFW_RELEASE: {} {} {}", button, action, modes);
					break;
				}
				}
			});
		/* Atualiza a posição do mouse na tela */
		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			MouseMovedEvent event((float)xpos, (float)ypos);
			if (data.EventCallback)
			{
				data.EventCallback(event);
			}

			//AXE_CORE_INFO("XPos {} YPos {}", xpos, ypos);
		});

		glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths)
		{
			
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				//Suporta um arquivo por vez o primeiro da lista
				if (count > 0 && data.EventCallback)
				{
					FileDropEvent event(paths[0]);
					data.EventCallback(event);
				}
		});



		glfwSetCursorEnterCallback(m_Window, [](GLFWwindow* window, int entered)
			{
				// Necessário para o ImGui rastrear quando o cursor entra/sai
			});
	
	}

	WindowGlfw::~WindowGlfw()
	{
		glfwDestroyWindow(m_Window);
		glfwTerminate();

		AXE_CORE_INFO("Window detroyed");
	}

	void WindowGlfw::PollEvents()
	{
		glfwPollEvents();
	}
	bool WindowGlfw::ShouldClose() const
	{
		if (!m_Window) return true;

		bool shouldClose = glfwWindowShouldClose(m_Window);

		// Adicione este log para debug
		static int callCount = 0;
		if (callCount++ < 10)
		{
			AXE_CORE_INFO("ShouldClose called, returning: {}", shouldClose);
		}

		return shouldClose;
	}

	void WindowGlfw::SwapBuffers()
	{
		glfwSwapBuffers(m_Window);
	}
	void* WindowGlfw::GetNativeWindow() const
	{
		return m_Window;
	}

	void WindowGlfw::SetVSync(bool enabled)
	{
		if (enabled)
			glfwSwapInterval(1);
		else
			glfwSwapInterval(0);

		m_Data.VSync = enabled;
	}

	bool WindowGlfw::IsVSync() const
	{
		return m_Data.VSync;
	}

	void WindowGlfw::SetTitle(const std::string& title)
	{
		glfwSetWindowTitle(m_Window, title.c_str());
	}

	float WindowGlfw::GetTime() const
	{
		return (float)glfwGetTime(); // chamado dentro da DLL onde o GLFW vive
	}

	void WindowGlfw::CaptureCursor(bool capture)
	{
		if (capture)
			glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		else
			glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

		AXE_CORE_INFO("CaptureCursor: {} → modo={}", capture,
			glfwGetInputMode(m_Window, GLFW_CURSOR));
	}


	bool WindowGlfw::IsKeyDown(int keycode) const
	{
		return glfwGetKey(m_Window, keycode) == GLFW_PRESS;
	}

	glm::vec2 WindowGlfw::GetCursorPosition() const
	{
		double x, y;
		glfwGetCursorPos(m_Window, &x, &y);
		return { (float)x, (float)y };
	}

	void WindowGlfw::SetCursorPosition(float x, float y)
	{
		glfwSetCursorPos(m_Window, (double)x, (double)y);
	}
	
	
}
