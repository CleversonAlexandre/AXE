#include "viewport_window.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/triangle_renderer.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>
#include "axe/graphics/renderer/viewport_renderer.hpp"

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

		m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 100.0f);

		AXE_CORE_INFO("Initializing ViewportWindow resources...");

		FramebufferSpecification spec;
		spec.Width = 1280;
		spec.Height = 720;
		spec.HDR = true;

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
		//ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.35f, 0.3f, 0.3f));
		if (!ImGui::Begin("Viewport"))
		{
			ImGui::End();
			//ImGui::PopStyleColor(1);
			return;
		}
		DrawToolbar();

		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		uint32_t width = static_cast<uint32_t>(viewportSize.x);
		uint32_t height = static_cast<uint32_t>(viewportSize.y);

		if (width > 0 && height > 0 && (width != m_Width || height != m_Height))
		{
			OnResize(width, height);
		}


		//m_IsHovered = ImGui::IsWindowHovered();
		m_IsFocused = ImGui::IsWindowFocused();

		ImVec2 mousePos = ImGui::GetMousePos();
		m_MousePosition = { mousePos.x, mousePos.y };
		m_MouseDelta = m_MousePosition - m_LastMousePosition;
		m_LastMousePosition = m_MousePosition;

		if (m_Initialized && m_Framebuffer)
		{
			ImTextureID textureID = GetTextureID();
			if (textureID != (ImTextureID)0)
			{
				ImVec2 imagePos = ImGui::GetCursorScreenPos();

				ImGui::Image(textureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));
				m_IsHovered = ImGui::IsItemHovered();

				m_BoundsMin = { imagePos.x, imagePos.y };
				m_BoundsMax = { imagePos.x + viewportSize.x, imagePos.y + viewportSize.y };

				if (m_GuizmoCallback)
				{
					m_GuizmoCallback(m_BoundsMin, m_BoundsMax);
				}
			}
		}
		else
		{
			ImGui::Text("Initializing viewport...");
			m_IsHovered = false;
		}

		// Drag and drop do Asset Browser para o viewport
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
			{
				const char* uuid = (const char*)payload->Data;
				if (m_AssetDropCallback)
				{
					ImVec2 mousePos = ImGui::GetMousePos();
					float localX = mousePos.x - m_BoundsMin.x;
					float localY = mousePos.y - m_BoundsMin.y;
					m_AssetDropCallback(std::string(uuid), localX, localY);
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::End();
		//ImGui::PopStyleColor(1);
	}

	void ViewportWindow::OnResize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0) return;
		m_Width = width;
		m_Height = height;
		if (m_Initialized && m_Framebuffer)
			m_Framebuffer->Resize(width, height);
		if (m_ViewportRenderer)
			m_ViewportRenderer->Resize(width, height);
	}

	ImTextureID ViewportWindow::GetTextureID() const
	{
		if (m_Framebuffer)
			return (ImTextureID)(uintptr_t)m_Framebuffer->GetColorAttachmentRendererID();
		return (ImTextureID)0;
	}

	void ViewportWindow::DrawToolbar()
	{
		if (!m_PlayStateCallback || !m_PlayActionCallback) return;

		int state = m_PlayStateCallback(); //0=Edit, 1=Play, 2=Pause

		//Posição da toolbar - centralizada no topo do viewport
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 wpos = ImGui::GetWindowPos();
		ImVec2 wsize = ImGui::GetWindowSize();

		float btnW = 60.0f;
		float btnH = 24.0f;
		float gap = 4.0f;
		float totalW = btnW * 3 + gap * 2;
		float startX = wpos.x + (wsize.x - totalW) * 0.5f;
		float startY = wpos.y + 28.0f; //Abaixo do titulo da janela

		//Fundo da toolbar 
		draw->AddRectFilled(
			ImVec2(startX - 6, startY - 4),
			ImVec2(startX + totalW + 6, startY + btnH + 4),
			IM_COL32(30, 30, 30, 200), 4.0f
		);

		//Botão Play
		ImGui::SetCursorScreenPos(ImVec2(startX, startY));
		if (state == 1) //play ativo
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
		else
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.24f, 0.25f, 1.0f));
		if (ImGui::Button("Play", ImVec2(btnW, btnH)))
		{
			if (state == 0) m_PlayActionCallback(0); // Edit → Play
			if (state == 2) m_PlayActionCallback(0); // Pause → Play
		}
		ImGui::PopStyleColor();


		// Botão Pause
		ImGui::SetCursorScreenPos(ImVec2(startX + btnW + gap, startY));
		if (state == 2)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.6f, 0.0f, 1.0f));
		else
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));

		if (ImGui::Button("Pause", ImVec2(btnW, btnH)) && state == 1)
			m_PlayActionCallback(1);

		ImGui::PopStyleColor();

		// Botão Stop
		ImGui::SetCursorScreenPos(ImVec2(startX + (btnW + gap) * 2, startY));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
		if (ImGui::Button("Stop", ImVec2(btnW, btnH)) && state != 0)
			m_PlayActionCallback(2);
		ImGui::PopStyleColor();

		// Controles de Grid e Snap — lado direito da toolbar
		if (m_ViewportRenderer)
		{
			float rightX = wpos.x + wsize.x - 220.0f;
			float rightY = startY;

			// Grid toggle
			ImGui::SetCursorScreenPos(ImVec2(rightX, rightY));
			bool& showGrid = m_ViewportRenderer->ShowGrid;
			if (showGrid)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
			else
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
			if (ImGui::Button("Grid", ImVec2(50, btnH)))
				showGrid = !showGrid;
			ImGui::PopStyleColor();

			// Snap toggle
			ImGui::SetCursorScreenPos(ImVec2(rightX + 54, rightY));
			bool& snapEnabled = m_ViewportRenderer->SnapEnabled;
			if (snapEnabled)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.5f, 0.1f, 1.0f));
			else
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
			if (ImGui::Button("Snap", ImVec2(50, btnH)))
				snapEnabled = !snapEnabled;
			ImGui::PopStyleColor();

			// Valor do snap
			if (snapEnabled)
			{
				ImGui::SetCursorScreenPos(ImVec2(rightX + 108, rightY));
				ImGui::SetNextItemWidth(100.0f);

				if (m_ViewportRenderer->m_GuizmoOperation == ImGuizmo::ROTATE)
					ImGui::DragFloat("##snap", &m_ViewportRenderer->SnapAngle, 1.0f, 1.0f, 90.0f, "%.0f°");
				else if (m_ViewportRenderer->m_GuizmoOperation == ImGuizmo::SCALE)
					ImGui::DragFloat("##snap", &m_ViewportRenderer->SnapScale, 0.05f, 0.05f, 2.0f, "%.2f");
				else
					ImGui::DragFloat("##snap", &m_ViewportRenderer->SnapValue, 0.1f, 0.1f, 10.0f, "%.1f");
			}
		}
	}




}