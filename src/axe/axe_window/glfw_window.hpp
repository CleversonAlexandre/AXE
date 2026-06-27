#pragma once

#include "window.hpp"

#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

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

		// Tamanho do framebuffer em PIXELS (high-DPI). Mantém o
		// glfwGetFramebufferSize encapsulado aqui, dentro da impl de
		// janela — o GraphicsDevice consome via interface Window.
		void GetFramebufferSize(int& width, int& height) const override;

		inline void SetEventCallback(const EventCallbackFunc& callback) override { m_Data.EventCallback = callback; }

		void SetVSync(bool enabled) override;
		bool IsVSync() const override;
		void SetTitle(const std::string& title) override;
		float GetTime() const override;
		virtual void* GetNativeWindow() const override;

		void CaptureCursor(bool capture) override;

		bool      IsKeyDown(int keycode)        const override;
		glm::vec2 GetCursorPosition()           const override;
		void      SetCursorPosition(float x, float y) override;

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