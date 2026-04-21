#pragma once

#include "window.hpp"

#include "axe/core/types.hpp"

struct GLFWwindow;

namespace axe
{
	class WindowGlfw : public Window
	{

		

	public:
		WindowGlfw(const WindowProps& props);
		~WindowGlfw();

		void PollEvents() override;
		bool ShouldClose() const override;
		void SwapBuffers() override;

		inline unsigned int GetWidth() const override { return m_Data.Width; }
		inline unsigned int GetHeight() const override { return m_Data.Height; }

		inline void SetEventCallback(const EventCallbackFunc& callback) override { m_Data.EventCallback = callback; }

		void SetVSync(bool enabled) override;
		bool IsVSync() const override;
		void SetTitle(const std::string& title) override;
		float GetTime() const override;
		virtual void* GetNativeWindow() const override;



	private:
		struct WindowData
		{
			std::string Title;
			unsigned int Width, Height;
			bool VSync;

			EventCallbackFunc EventCallback;			
		};

		GLFWwindow* m_Window = nullptr;
		WindowData m_Data;
	};
}