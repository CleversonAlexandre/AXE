#pragma once
#include "axe/layers/layer.hpp"
#include "axe/axe_imgui/imgui_system.hpp"
#include "axe/axe_window/window.hpp"

#include <imgui.h>

namespace axe
{
	// ImGuiLayer é um Overlay — sempre no topo da pilha
	// Ela encapsula todo o ciclo de vida do ImGui:
	// inicialização, begin/end frame, e shutdown

	class ImGuiLayer : public Layer 
	{
	public:
		ImGuiLayer(Window* window)
			: Layer("ImGuiLayer"), m_Window(window) {}

		// OnAttach — inicializa o ImGui quando a layer entra na pilha
		void OnAttach() override
		{
			m_ImGui = std::make_unique<ImGuiSystem>();
			m_ImGui->Initialize(m_Window);

		}

		void OnUpdate(float deltatime) override
		{

		}

		// OnDetach — desliga o ImGui quando a layer sai da pilha
		void OnDetach() override
		{
			m_ImGui->Shutdown();
		}

		// OnRender — aqui a ImGuiLayer pode desenhar sua própria UI de debug
		void OnRender() override
		{
			// Por enquanto vazio — cada layer desenha a sua UI no próprio OnRender
		}

		// Begin/End são chamados pelo EditorApp para envolver o render das outras layers
		void Begin()
		{
			m_ImGui->BeginFrame();
		}

		void End()
		{
			m_ImGui->EndFrame();
		}

		


	private:
		Window* m_Window{ nullptr };
		std::unique_ptr<ImGuiSystem> m_ImGui;
	};
}