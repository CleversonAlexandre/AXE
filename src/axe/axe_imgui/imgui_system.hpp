#pragma once

#include "axe/core/types.hpp"
#include "axe/events/application_event.hpp"
#include "axe/events/key_event.hpp"
#include "axe/events/mouse_event.hpp"



struct ImGuiContext;
struct GLFWwindow;

namespace axe
{
	class Window;
}

namespace axe
{
	class AXE_API ImGuiSystem
	{
	public:
		ImGuiSystem() = default;
		~ImGuiSystem() = default;

		bool Initialize(axe::Window* window);
		void Shutdown();

		void BeginFrame();
		void EndFrame();		

		void OnEvent(Event& event);

		void* GetContextRaw() const { return (void*)m_Context; }

		GLFWwindow* GetNativeWindow() const { return m_NativeWindow; }

	private:
		bool OnMouseButtonReleasedEvent(MouseButtonReleasedEvent& e);
		bool OnMouseMovedEvent(MouseMovedEvent& e);
		bool OnMouseScrolledEvent(MouseScrolledEvent& e);
		bool OnMouseButtonPressedEvent(MouseButtonPressedEvent& e);
		bool OnKeyPressedEvent(KeyPressedEvent& e);
		bool OnKeyReleasedEvent(KeyReleasedEvent& e);
		bool OnKeyTypedEvent(KeyTypedEvent& e);
		bool OnWindowResizedEvent(WindowResizeEvent& e);

		

	private:
		GLFWwindow* m_NativeWindow = nullptr;
		ImGuiContext* m_Context{ nullptr };

		float m_Time = 0.0f;
	};
}