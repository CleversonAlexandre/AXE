#include "window.hpp"

#include "glfw_window.hpp"

namespace axe 
{
	Window* Window::Create(const WindowProps& props)
	{

		return new WindowGlfw(props);
	}
}