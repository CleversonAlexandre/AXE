#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <functional>

namespace axe
{
	class Framebuffer;
	class TriangleRenderer;

	class AXE_API ViewportWindow
	{
	public:
		ViewportWindow();
		
		~ViewportWindow();

		void Initialize();
		
		
		void Draw();


		// Resize viewport
		void OnResize(uint32_t width, uint32_t height);

		// Check if viewport is hovered
		bool IsHovered() const { return m_IsHovered; }

		// Get framebuffer texture ID for ImGui
		void* GetTextureID() const;

		std::shared_ptr<Framebuffer> GetFramebuffer() const { return m_Framebuffer; }
		bool IsInitialized() const { return m_Initialized; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

	private:
		std::shared_ptr<Framebuffer> m_Framebuffer;		
		bool m_IsHovered = false;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_Initialized = false;
	};
}