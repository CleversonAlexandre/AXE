#pragma once
// imgui_system — amarra o ImGui a janela GLFW e ao contexto OpenGL.
//
// ── POR QUE ISTO MORA NO RUNTIME, E NAO NO EDITOR ────────────────────────────
//
// O S0b tentou levar este arquivo para `src/editor/` junto com o resto da GUI.
// Duas coisas disseram que era o lado errado:
//
//   1. O `.cpp` inclui <glad/glad.h> e <GLFW/glfw3.h>. Ele e um BINDING de
//      backend — nenhum binding de ImGui existe sem esses headers — e a regra
//      da engine e que OpenGL nao sai do backend grafico. Movido, ele era o
//      UNICO arquivo de `src/editor/` a incluir GL/GLFW.
//
//   2. GLFW e Glad sao static libs linkadas na DLL E no exe: cada binario tem
//      a propria copia do estado global. Quem chama `glfwInit` e cria a janela
//      e a `axe.dll`. Um binding do outro lado conversa com a copia errada — e
//      foi exatamente assim que o editor passou a crashar no boot, dentro de
//      `ImGui_ImplGlfw_Init`.
//
// ── O QUE MANTEM A FRONTEIRA HONESTA ─────────────────────────────────────────
//
// Este HEADER e limpo: `types.hpp`, os eventos, e duas forward declarations
// (`ImGuiContext`, `GLFWwindow`) que nao arrastam header nenhum. O editor
// inclui isto e nao herda imgui, glad nem GLFW — a sujeira toda fica no `.cpp`,
// deste lado da fronteira.
//
// Se um dia GLFW e Glad virarem bibliotecas COMPARTILHADAS (uma instancia por
// processo, nao por binario), este arquivo pode ir para o editor sem drama. Ate
// la, o lugar dele e aqui.

#include "axe/core/types.hpp"
#include "axe/events/application_event.hpp"
#include "axe/events/key_event.hpp"
#include "axe/events/mouse_event.hpp"



struct ImGuiContext;
struct GLFWwindow;

namespace axe
{
	class Window;
}

namespace axe
{
	// AXE_API porque a implementacao mora no `.cpp`, compilado DENTRO da
	// axe.dll, e quem chama (a ImGuiLayer) vive no editor.exe.
	//
	// Contraste com o `CommandHistory`, que perdeu o macro no mesmo S0b: la a
	// classe e inteiramente inline num header, e exportar so criava a
	// dependencia de import que quebrou o link quando o ultimo consumidor do
	// runtime saiu. A regra e essa: implementacao na DLL -> exporta;
	// header-only -> nao exporta.
	class AXE_API ImGuiSystem
	{
	public:
		ImGuiSystem() = default;
		~ImGuiSystem() = default;

		bool Initialize(axe::Window* window);
		void Shutdown();

		void BeginFrame();
		void EndFrame();

		void OnEvent(Event& event);

		void* GetContextRaw() const { return (void*)m_Context; }

		GLFWwindow* GetNativeWindow() const { return m_NativeWindow; }

	private:
		bool OnMouseButtonReleasedEvent(MouseButtonReleasedEvent& e);
		bool OnMouseMovedEvent(MouseMovedEvent& e);
		bool OnMouseScrolledEvent(MouseScrolledEvent& e);
		bool OnMouseButtonPressedEvent(MouseButtonPressedEvent& e);
		bool OnKeyPressedEvent(KeyPressedEvent& e);
		bool OnKeyReleasedEvent(KeyReleasedEvent& e);
		bool OnKeyTypedEvent(KeyTypedEvent& e);
		bool OnWindowResizedEvent(WindowResizeEvent& e);



	private:
		GLFWwindow* m_NativeWindow = nullptr;
		ImGuiContext* m_Context{ nullptr };

		float m_Time = 0.0f;
	};
}