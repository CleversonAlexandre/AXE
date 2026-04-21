#pragma once

#include <string>
#include <functional> 

#include "axe/core/types.hpp"
#include "axe/events/event.hpp"


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

		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
	
		virtual void SetEventCallback(const EventCallbackFunc& callback) = 0;
		virtual void* GetNativeWindow() const = 0;

		virtual void SetTitle(const std::string& title) = 0;
		virtual float GetTime() const = 0;

		static Window* Create(const WindowProps& props = WindowProps());
		
	
	};
}