#include "viewport_window.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/triangle_renderer.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>

namespace axe
{
	ViewportWindow::ViewportWindow()
	{
		AXE_CORE_INFO("ViewportWindow created (no GPU resources yet)");
	}

	ViewportWindow::~ViewportWindow()
	{
		AXE_CORE_INFO("ViewportWindow destroyed");
	}

	void ViewportWindow::Initialize()
	{
		if (m_Initialized)
		{
			AXE_CORE_WARN("Viewport already initialized");
			return;
		}

		AXE_CORE_INFO("Initializing ViewportWindow resources...");

		FramebufferSpecification spec;
		spec.Width = 1280;
		spec.Height = 720;

		m_Framebuffer = Framebuffer::Create(spec);

		if (!m_Framebuffer)
		{
			AXE_CORE_ERROR("Failed to create framebuffer");
			return;
		}

		m_Initialized = true;

		AXE_CORE_INFO("ViewportWindow initialized successfully");
	}

		

	void ViewportWindow::Draw()
	{
		if (!ImGui::Begin("Viewport"))
		{
			ImGui::End();
			return;
		}

		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		uint32_t width = static_cast<uint32_t>(viewportSize.x);
		uint32_t height = static_cast<uint32_t>(viewportSize.y);

		if (width > 0 && height > 0 && (width != m_Width || height != m_Height))
		{
			OnResize(width, height);
		}

		m_IsHovered = ImGui::IsWindowHovered();

		if (m_Initialized && m_Framebuffer)
		{
			void* textureID = GetTextureID();
			if (textureID)
			{
				ImGui::Image(textureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));
			}
		}
		else
		{
			ImGui::Text("Initializing viewport...");
		}
		ImGui::End();
	}

	void ViewportWindow::OnResize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;

		m_Width = width;
		m_Height = height;

		if (m_Initialized && m_Framebuffer)
		{
			m_Framebuffer->Resize(width, height);
			AXE_CORE_INFO("Viewport resized to {}x{}", width, height);
		}
	}

	void* ViewportWindow::GetTextureID() const
	{
		if (m_Framebuffer)
		{
			return reinterpret_cast<ImTextureID>(
				static_cast<uintptr_t>(m_Framebuffer->GetColorAttachmentRendererID())
				);
		}
		return nullptr;
	}
}