#pragma once

#include "axe/core/types.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/events/event.hpp"
#include "axe/events/application_event.hpp"
#include "editor/axe_editor/editor_ui.hpp"

#include "axe/graphics/graphics_device.hpp"

#include "axe/layers/layer_stack.hpp"
#include "imgui_layer.hpp"

#include <functional>
#include <vector>
#include <filesystem>



namespace axe
{
	class EditorUI;

	class EditorApp
	{
	public:
		EditorApp();
		virtual ~EditorApp();

		void Run();
		void OnEvent(Event& e);
		void Close() { m_Running = false; }

		// Troca o projeto atual com segurança — usado pelo menu File >
		// "Abrir Projeto" de dentro do editor já em execução. Abre o novo
		// projeto e AGENDA a troca de EditorLayer (pop do atual + push de um
		// novo do zero) pro próximo frame, fora do loop de update atual —
		// mesmo mecanismo que já existia só pra transição do launcher pro
		// editor. Troca o EditorLayer inteiro (não tenta resetar o estado
		// do atual em cena) de propósito: garante que cena/seleção/undo
		// history/etc. do projeto antigo não sobrevivam por acidente.
		bool RequestReopenProject(const std::filesystem::path& projectFile);

		inline static EditorApp& Get() { return *s_Instance; }
		inline Window& GetWindow() { return *m_Window; }


	private:
		bool OnWindowClose(WindowCloseEvent& e);

		std::unique_ptr<Window> m_Window;
		std::unique_ptr<EditorUI> m_EditorUI;
		bool m_Running = true;

		static EditorApp* s_Instance;
		std::unique_ptr<GraphicsDevice> m_Graphics;

		LayerStack m_LayerStack;
		ImGuiLayer* m_ImGuiLayer{ nullptr };

		void ExecutePendingCommands();
		std::vector<std::function<void()>> m_PendingCommands;
	};

	// EditorApp* CreateEditorApp();
}