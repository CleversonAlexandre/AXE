#pragma once

#include "axe/core/types.hpp"

namespace axe
{
	class AXE_API Window;

	class AXE_API GraphicsDevice
	{
	public:
		bool Initialize(Window* window);
		void Shutdown();

		void BeginFrame();
		void EndFrame();

		void SetClearColor(float r, float g, float b, float a);

	private:
		Window* m_Window = nullptr;

		float m_ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	};
}