#pragma once

#include "axe/core/types.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/events/event.hpp"
#include "axe/events/application_event.hpp"
#include "editor/axe_editor/editor_ui.hpp"

#include "axe/layers/layer_stack.hpp"
#include "imgui_layer.hpp"

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

			inline static EditorApp& Get() { return *s_Instance; }

			inline Window& GetWindow() { return *m_Window; }

	private:
		bool OnWindowClose(WindowCloseEvent& e);

		std::unique_ptr<Window> m_Window;
		std::unique_ptr<EditorUI> m_EditorUI;
		bool m_Running = true;
		
		static EditorApp* s_Instance;

		LayerStack m_LayerStack;
		ImGuiLayer* m_ImGuiLayer{ nullptr };
	};

	// EditorApp* CreateEditorApp();
}