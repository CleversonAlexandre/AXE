#pragma once

#include "axe/layers/layer.hpp"
#include "editor_ui.hpp"

#include "GLFW/glfw3.h"
#include <iostream>

#include "axe/graphics/renderer/viewport_renderer.hpp"


#include "axe/graphics/framebuffer.hpp"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>

#include "axe/graphics/editor_camera.hpp"


namespace axe
{
	class EditorCamera;
	// EditorLayer contém toda a UI do editor
	// É uma layer normal — fica abaixo do ImGuiLayer

	class EditorLayer : public Layer
	{
	public:
		EditorLayer()
			: Layer("EditorLayer"), m_EditorUI(std::make_unique<EditorUI>()) 
		{
			
		}

		void OnAttach() override
		{
			AXE_EDITOR_INFO("EditorLayer attached");

			// Inicializa os recursos OpenGL do viewport
			// OnAttach é chamado após o GraphicsDevice estar pronto
			ViewportWindow* viewport = m_EditorUI->GetViewport();
			if (viewport)
			{
				viewport->Initialize();

				viewport->SetGuizmoCallback([this](const glm::vec2& min, const glm::vec2& max)
					{
						m_ViewportRenderer->DrawGuizmo(min, max);
					});
			}
			else
			{
				AXE_EDITOR_ERROR("ViewportWindow is null during OnAttach!");
			}

			
			m_ViewportRenderer = std::make_unique<axe::ViewportRenderer>();
			m_ViewportRenderer->Initialize();

			m_EditorUI->SetViewportRenderer(m_ViewportRenderer.get());

			
		}

		void OnDetach() override
		{
			
			m_EditorUI.reset();

		}

		void OnUpdate(float deltaTime) override
		{			
			m_FPSAccumulator += 1.0f / deltaTime;
			m_FPSSamples++;

			if (m_FPSSamples >= 60)
			{
				m_FPS = m_FPSAccumulator / m_FPSSamples;
				m_FPSAccumulator = 0.0f;
				m_FPSSamples = 0;
			}		
		}


		void OnRender() override
		{
			
			if (m_EditorUI)
			{
				ViewportWindow* viewport = m_EditorUI->GetViewport();
				if (viewport && viewport->IsInitialized())
				{
					auto framebuffer = viewport->GetFramebuffer();
					if (framebuffer && viewport->GetWidth() > 0 && viewport->GetHeight() > 0)
					{
						m_ViewportRenderer->RenderToFramebuffer(
							*framebuffer,
							viewport->GetWidth(),
							viewport->GetHeight(),
							EditorApp::Get().GetWindow().GetTime()
						);
					}
									
				}
			}
			
			if (m_EditorUI)
			{
								
				ViewportWindow* viewport = m_EditorUI->GetViewport();
				
				if (viewport)
				{
					m_ViewportRenderer->DrawGuizmo(
						
						viewport->GetBoundsMin(),
						viewport->GetBoundsMax());
									
				}

			}
			
		
			//if (m_EditorUI)
				m_EditorUI->Draw();

			HandleViewportCameraInput();



			std::string title = "AXE Engine — " + std::to_string((int)m_FPS) + " FPS";
			EditorApp::Get().GetWindow().SetTitle(title);
		}


		void OnEvent(Event& e) override
		{
			
			
		}

	
		void HandleViewportCameraInput() 
		{
			if (!m_EditorUI)
				return;

			ViewportWindow* viewport = m_EditorUI->GetViewport();
			if (!viewport || !viewport->IsHovered())
				return;

			ImGuiIO& io = ImGui::GetIO();

			if (ImGui::IsKeyPressed(ImGuiKey_P))
			{
				m_ViewportRenderer->m_Camera->isPerspective = !m_ViewportRenderer->m_Camera->isPerspective;			
				if (!m_ViewportRenderer->m_Camera->isPerspective)
				{					
					m_ViewportRenderer->m_Camera->viewHeight = m_ViewportRenderer->m_Camera->viewWidth * io.DisplaySize.y / io.DisplaySize.x;
				}
				AXE_EDITOR_INFO("Perspective mode: {}", m_ViewportRenderer->m_Camera->isPerspective);
			}

			if (viewport->IsFocused())
			{
				if (ImGui::IsKeyPressed(ImGuiKey_R))
				{
					m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::ROTATE;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_S))
				{
					m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::SCALE;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_T))
				{
					m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::TRANSLATE;
				}
			}
				
			const bool alt = io.KeyAlt;
			if (!alt)
				return;

			glm::vec2 delta = viewport->GetMouseDelta();

			// Sensibilidade básica
			delta *= 0.003f;

			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				//AXE_CORE_INFO("Rotate input");
				m_ViewportRenderer->OnMouseRotate(delta);
			}
			else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			{
				//AXE_CORE_INFO("Pan input");
				m_ViewportRenderer->OnMousePan(delta);
			}
			else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
			{
				//AXE_CORE_INFO("Zoom input");
				m_ViewportRenderer->OnMouseZoom(delta.y * 10.0f);
			}

			if (io.MouseWheel != 0.0f)
			{
				//AXE_CORE_INFO("Scroll zoom: {}", io.MouseWheel);
				m_ViewportRenderer->OnMouseZoom(io.MouseWheel);
			}
		}

	private:		
		std::unique_ptr<axe::ViewportRenderer> m_ViewportRenderer;

	private:
		std::unique_ptr<EditorUI> m_EditorUI;
		float m_DeltaTime = 0.0f;
		float m_FPS = 0.0f;
		float m_FPSAccumulator = 0.0f;
		int m_FPSSamples = 0;
	};
}    