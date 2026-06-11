#include "editor_app.hpp"

#include "axe/log/log.hpp"
#include "axe/graphics/graphics_device.hpp"
#include "axe_editor/editor_layer.hpp"

#include "axe/core/timestep.hpp"
#include <GLFW/glfw3.h>


#include <imgui.h>
#include <ImGuizmo.h>

#include "axe/project/project_manager.hpp"
#include "project_selector_layer.hpp"

#include <stdlib.h>
#include <filesystem>

#include "axe/input/input.hpp"

namespace axe
{
#define BIND_EVENT_FN(x) std::bind(&EditorApp::x, this, std::placeholders::_1)

	EditorApp* EditorApp::s_Instance = nullptr;

	

	EditorApp::EditorApp()
	{		

		char* pgmptr = nullptr;
		_get_pgmptr(&pgmptr);
		if (pgmptr)
		{
			auto exeDir = std::filesystem::path(pgmptr).parent_path();
			std::filesystem::current_path(exeDir);
		}

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

		axe::Input::Init(m_Window.get());

		axe::ProjectManager::Get().LoadPreferences();

		// Decide qual layer colocar primeiro
		if (ProjectManager::Get().HasLastProject())
		{
			// Abre direto o editor
			ProjectManager::Get().OpenProject(
				ProjectManager::Get().GetLastProjectPath()
			);
			m_LayerStack.PushLayer(new EditorLayer());
		}
		else
		{
			// Mostra splash screen
			m_LayerStack.PushLayer(new ProjectSelectorLayer(
				[this](const std::filesystem::path& projectPath)
				{
					// Agenda a troca — não executa agora
					m_PendingCommands.push_back([this]()
						{
							// Remove ProjectSelectorLayer
							for (auto* layer : m_LayerStack)
							{
								if (layer->GetName() == "ProjectSelectorLayer")
								{
									m_LayerStack.PopLayer(layer);
									delete layer;
									break;
								}
							}
							// Adiciona EditorLayer
							auto* editorLayer = new EditorLayer();
							m_LayerStack.PushLayer(editorLayer);
						});
				}
			));
		}

		// ImGuiLayer sempre por último
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

			// Executa comandos pendentes ANTES de iterar as layers
			ExecutePendingCommands();

			m_Window->PollEvents();

			axe::Input::Update();

			for (Layer* layer : m_LayerStack)
				layer->OnUpdate((float)deltaTime);

			m_Graphics->BeginFrame();
			m_ImGuiLayer->Begin();

			for (Layer* layer : m_LayerStack)
				layer->OnRender();

			m_ImGuiLayer->End();
			m_Graphics->EndFrame();
			m_Window->SwapBuffers();
		}

		m_Graphics->Shutdown();
	}

	void EditorApp::ExecutePendingCommands()
	{
		for (auto& cmd : m_PendingCommands)
		{
			try { cmd(); }
			catch (const std::exception& e)
			{
				AXE_CORE_ERROR("ExecutePendingCommands: {}", e.what());
			}
		}
		m_PendingCommands.clear();
	}




	bool EditorApp::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}
}