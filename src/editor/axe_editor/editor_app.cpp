#include "editor_app.hpp"

#include "axe/log/log.hpp"
#include "axe/graphics/graphics_device.hpp"
#include "axe_editor/editor_layer.hpp"

#include "axe/core/timestep.hpp"
#include <GLFW/glfw3.h>


//#include <imgui.h>

namespace axe
{
#define BIND_EVENT_FN(x) std::bind(&EditorApp::x, this, std::placeholders::_1)

	EditorApp* EditorApp::s_Instance = nullptr;

	

	EditorApp::EditorApp()
	{		
		console_init();
		InfoLog::EnableConsoleLogging();
		InfoLog::Init();

		AXE_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;

		AXE_EDITOR_INFO("Creating window...");
		m_Window = std::unique_ptr<Window>(Window::Create());

		if (!m_Window || !m_Window->GetNativeWindow())
		{
			AXE_CORE_ERROR("Failed to create window in constructor");
			return;
		}
		m_Graphics = std::make_unique<GraphicsDevice>();
		if (!m_Graphics->Initialize(m_Window.get()))
		{
			AXE_CORE_ERROR("Failed to initialize GraphicsDevice");
			return;
		}		
		m_Graphics->SetClearColor(0.05f, 0.05f, 0.08f, 1.0f);

		m_Window->SetEventCallback(BIND_EVENT_FN(OnEvent));

		// EditorLayer — layer normal, fica na base da pilha
		m_LayerStack.PushLayer(new EditorLayer());

		// ImGuiLayer — overlay, sempre no topo
		m_ImGuiLayer = new ImGuiLayer(m_Window.get());
		m_LayerStack.PushOverlay(m_ImGuiLayer);




		AXE_EDITOR_INFO("EditorApp created successfully");
	}

	EditorApp::~EditorApp() = default;

	void EditorApp::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(OnWindowClose));

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); it++)
		{
			if(e.Handled) break; //Pra quando alguém consumir o evento
			(*it)->OnEvent(e);
		}
	}

	void EditorApp::Run()
	{		
	
		
		

		m_Graphics->SetClearColor(0.05f, 0.05f, 0.08f, 1.0f);

		AXE_EDITOR_INFO("Editor started");

		float lastFrameTime = m_Window->GetTime();

		while (m_Running)
		{
			float currentTime = m_Window->GetTime();
			TimeStamp deltaTime = currentTime - lastFrameTime;
			lastFrameTime = currentTime;

			

			m_Window->PollEvents();

			for (Layer* layer : m_LayerStack)
				layer->OnUpdate((float)deltaTime);

			//Render - ImGui envolve o render de todas as layers
			m_Graphics->BeginFrame();
			m_ImGuiLayer->Begin();      // abre o frame do ImGui

			for (Layer* layer : m_LayerStack)
				layer->OnRender();   	// cada layer desenha a sua UI
		
			//m_EditorUI->Draw();
			m_ImGuiLayer->End();        // fecha e renderiza o ImGui
			m_Graphics->EndFrame();
			m_Window->SwapBuffers();
		}
		
		m_Graphics->Shutdown();
	}

	bool EditorApp::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}
}