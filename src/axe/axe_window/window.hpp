#pragma once

#include <string>
#include <functional> 

#include "axe/core/types.hpp"
#include "axe/events/event.hpp"
#include "axe/input/key_codes.hpp"
#include <glm/glm.hpp>


namespace axe
{


	struct WindowProps
	{
		std::string Title = "AXE Engine";
		int Width = 1280;
		int Height = 720;

	};

	class AXE_API Window
	{
	public:
		using EventCallbackFunc = std::function<void(Event&)>;
		virtual ~Window() = default;

		virtual void PollEvents() = 0;
		virtual bool ShouldClose() const = 0;
		virtual void SwapBuffers() = 0;

		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;

		// Tamanho do framebuffer em PIXELS (difere de GetWidth/GetHeight em
		// telas high-DPI). Usado pelo GraphicsDevice pra setar o viewport da
		// tela sem chamar glfwGetFramebufferSize direto.
		virtual void GetFramebufferSize(int& width, int& height) const = 0;

		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;

		virtual void SetEventCallback(const EventCallbackFunc& callback) = 0;
		virtual void* GetNativeWindow() const = 0;

		virtual void SetTitle(const std::string& title) = 0;
		virtual float GetTime() const = 0;

		static Window* Create(const WindowProps& props = WindowProps());

		virtual void CaptureCursor(bool capture) = 0;

		// Input
		virtual bool      IsKeyDown(int keycode) const = 0;
		virtual glm::vec2 GetCursorPosition()   const = 0;
		virtual void      SetCursorPosition(float x, float y) = 0;


	};
}