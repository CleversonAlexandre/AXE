#include "axe/axe_imgui/imgui_system.hpp"

#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"

#include "editor/axe_editor/editor_app.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include "glad/glad.h"
#include "GLFW/glfw3.h"

#include "axe/core/types.hpp"

namespace axe
{


	 

	bool ImGuiSystem::Initialize(axe::Window* window)
	{
		


		IMGUI_CHECKVERSION();
		m_Context = ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		
	


		m_NativeWindow = static_cast<GLFWwindow*>(window->GetNativeWindow());

		
		if (!ImGui_ImplGlfw_InitForOpenGL(m_NativeWindow, true))
		{
			AXE_CORE_ERROR("Failed to initialize ImGui GLFW backend");
			return false;
		}

		if (!ImGui_ImplOpenGL3_Init("#version 450"))
		{
			AXE_CORE_ERROR("Failed to initialize ImGui OpenGL3 backend");
			return false;
		}

		AXE_CORE_INFO("ImGui initialized");
		return true;
	}



	void ImGuiSystem::Shutdown()
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		AXE_CORE_INFO("ImGui shutdown");
	}

	void ImGuiSystem::BeginFrame()
	{

		ImGuiIO& io = ImGui::GetIO();
		
	
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();

		
	}

	

	void ImGuiSystem::EndFrame()
	{
		// Pega o tamanho direto do GLFW, sem precisar do EditorApp
		int width, height;
		glfwGetFramebufferSize(m_NativeWindow, &width, &height);

		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)width, (float)height);

		float time = (float)glfwGetTime();
		io.DeltaTime = m_Time > 0.0f ? (time - m_Time) : (1.0f / 60.0f);
		m_Time = time;

		ImGui::Render();

		ImDrawData* drawData = ImGui::GetDrawData();
		if (drawData && drawData->Valid)
		{
			ImGui_ImplOpenGL3_RenderDrawData(drawData);
		}
	}
	
	void ImGuiSystem::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<MouseButtonPressedEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnMouseButtonPressedEvent));
		dispatcher.Dispatch<MouseButtonReleasedEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnMouseButtonReleasedEvent));
		dispatcher.Dispatch<MouseMovedEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnMouseMovedEvent));
		dispatcher.Dispatch<MouseScrolledEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnMouseScrolledEvent));
		dispatcher.Dispatch<KeyPressedEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnKeyPressedEvent));
		dispatcher.Dispatch<KeyReleasedEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnKeyReleasedEvent));
		dispatcher.Dispatch<KeyTypedEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnKeyTypedEvent));
		dispatcher.Dispatch<WindowResizeEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnWindowResizedEvent));
		dispatcher.Dispatch<MouseButtonPressedEvent>(AXE_BIND_EVENT_FN(ImGuiSystem::OnMouseButtonPressedEvent));
	}

	bool ImGuiSystem::OnMouseButtonPressedEvent(MouseButtonPressedEvent& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDown[e.GetMouseButton()] = true;

		return false;
	}

	bool ImGuiSystem::OnMouseButtonReleasedEvent(MouseButtonReleasedEvent& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDown[e.GetMouseButton()] = false;

		return false;
	}

	bool ImGuiSystem::OnMouseMovedEvent(MouseMovedEvent& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = ImVec2(e.GetX(), e.GetY());

		return false;
	}
	bool ImGuiSystem::OnMouseScrolledEvent(MouseScrolledEvent& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MouseWheelH += e.GetXOffset();
		io.MouseWheel += e.GetYOffset();

		return false;
	}

	bool ImGuiSystem::OnKeyPressedEvent(KeyPressedEvent& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.KeysDown[e.GetKeyCode()] = true;

		io.KeyCtrl = io.KeysDown[GLFW_KEY_LEFT_CONTROL] || io.KeysDown[GLFW_KEY_RIGHT_CONTROL];
		io.KeyShift = io.KeysDown[GLFW_KEY_LEFT_SHIFT] || io.KeysDown[GLFW_KEY_RIGHT_SHIFT];
		io.KeyAlt = io.KeysDown[GLFW_KEY_LEFT_ALT] || io.KeysDown[GLFW_KEY_RIGHT_ALT];
		io.KeySuper = io.KeysDown[GLFW_KEY_LEFT_SUPER] || io.KeysDown[GLFW_KEY_RIGHT_SUPER];

		return false;
	}

	bool ImGuiSystem::OnKeyReleasedEvent(KeyReleasedEvent& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.KeysDown[e.GetKeyCode()] = false;

		return false;
	}

	bool ImGuiSystem::OnKeyTypedEvent(KeyTypedEvent& e)
	{

		ImGuiIO& io = ImGui::GetIO();
		unsigned int keycode = e.GetKeyCode();

		if (keycode > 0 && keycode < 0x10000)
			io.AddInputCharacter((unsigned short)keycode);

		return false;
	}
	bool ImGuiSystem::OnWindowResizedEvent(WindowResizeEvent& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(e.GetWidth(), e.GetHeight());
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

		glViewport(0, 0, e.GetWidth(), e.GetHeight());

		return false;
	}
}