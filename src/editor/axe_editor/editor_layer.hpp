#pragma once

#include "axe/layers/layer.hpp"
#include "editor_ui.hpp"

#include "GLFW/glfw3.h"
#include <iostream>
namespace axe
{
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

			

			std::string title = "AXE Engine — " + std::to_string((int)m_FPS) + " FPS";
			EditorApp::Get().GetWindow().SetTitle(title);
			

			if (m_EditorUI)
				m_EditorUI->Draw();
			else
				AXE_EDITOR_ERROR("EditorUI is null!");
		}

		void OnEvent(Event& e) override
		{
			
			
		}

	private:
		std::unique_ptr<EditorUI> m_EditorUI;
		float m_DeltaTime = 0.0f;
		float m_FPS = 0.0f;
		float m_FPSAccumulator = 0.0f;
		int m_FPSSamples = 0;
	};
}    