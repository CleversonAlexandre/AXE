//#pragma once
//
//
//#include <string>
//#include <functional> 
//
//#include "axe/core/types.hpp"
//
//namespace axe
//{
//	enum class EventType
//	{
//		None = 0,
//		WindowClose,
//		WindowResize,
//		KeyPressed,
//		KeyReleased,
//		MouseMove,
//		MouseButtonPressed,
//		MouseButtonReleased,
//	};
//
//	// --- Estruturas de dados de cada evento ---
//	// Cada struct carrega apenas o que faz sentido para aquele evento.
//
//	 // Teclado: qual tecla, quais modificadores (Ctrl, Shift, Alt), e se foi pressionada
//	struct KeyEvent
//	{
//		int key;		 // código GLFW da tecla, ex: GLFW_KEY_W = 87
//		int mods;		// bitmask: GLFW_MOD_SHIFT, GLFW_MOD_CONTROL, etc.
//		bool pressed;
//	};
//
//	// Movimento do mouse: posição absoluta na tela e delta (quanto moveu)
//	struct MouseMoveEvent
//	{
//		float x, y; //Posição atual em pixels
//		float dx, dy;   // Diferença desde o ultimo frame
//	};
//
//	//Botão do mouse: qual botão e se foi pressionado 
//	struct MouseButtonEvent
//	{
//		int button;     // GLFW_MOUSE_BUTTON_LEFT = 0, RIGHT = 1, MIDDLE = 2 <-- BOTÕES DO MOUSE
//		bool pressed;
//		int mods;
//	};
//
//	//Risize da janela: novo tamanho
//	struct WindowResizeEvent
//	{
//		int width;
//		int height;
//	};
//
//	// WindowClose não precisa de dados — o fato de existir já é a informação
//	struct WindowCloseEvent
//	{
//
//	};
//
//	// --- O evento em si ---
//	// Pensa nele como um envelope: o 'type' é o endereço escrito na frente,
//	// e o union é o conteúdo dentro — só um conteúdo por envelope.
//	struct AXE_API Event
//	{
//		EventType  type { EventType::None };
//		bool handled{ false }; //quando true, outros sistemas ignoram esse vento
//
//		union 
//		{
//			KeyEvent key;
//			MouseMoveEvent mouseMove;
//			MouseButtonEvent mouseButton;
//			WindowResizeEvent windowResize;
//			WindowCloseEvent windowClose;
//		};
//
//
//		Event() = default;
//		Event(const MouseMoveEvent& e) : type(EventType::MouseMove), mouseMove(e){}
//	};
//
//
//
//	// --- EventHandler ---
//	// É uma interface — qualquer sistema que queira receber eventos
//	// herda dessa classe e sobrescreve os métodos que lhe interessam.
//	// O 'virtual' significa que cada filho pode ter sua própria implementação.
//
//	class AXE_API  EventHandler
//	{
//	public:
//		virtual ~EventHandler() = default;
//
//		// Dispatch recebe o evento e chama o método correto baseado no type.
//	    // Retorna true se o evento foi consumido (handled).
//		virtual bool Dispatch(Event& e);
//
//
//		// Métodos que os filhos sobrescrevem seletivamente.
//		// O '= 0' tornaria obrigatório implementar — mas deixamos com
//		// retorno false como padrão para não forçar implementar tudo.
//
//		virtual bool OnKeyEvent(const Event&) { return false; }
//		virtual bool OnMouseMoveEvent(const Event&) { return false; }
//		virtual bool OnMouseButtonEvent(const Event&) { return false; }
//		virtual bool OnWindowResizeEvent(const Event&) { return false; }
//		virtual bool OnWindowCloseEvent(const Event&) { return false; }
//	};
//
//	using EventCallbackFn = std::function<void(Event&)>;
//
//}