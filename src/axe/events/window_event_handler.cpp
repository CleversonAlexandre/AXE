//#include "axe/events/window_event_handler.hpp"
//
//namespace axe
//{
//	
//
//	bool EventHandler::Dispatch(Event& e)
//	{
//		// Baseado no tipo do evento, chama o método correto.
//	    // Se o método retornar true, marca o evento como handled
//	    // para que outros sistemas saibam que já foi processado.
//
//		switch (e.type)
//		{
//		case EventType::KeyPressed:
//		case EventType::KeyReleased:
//			e.handled = OnKeyEvent(e);
//			break;
//
//		case EventType::MouseMove:
//			e.handled = OnMouseMoveEvent(e);
//			break;
//
//		case EventType::MouseButtonPressed:
//		case EventType::MouseButtonReleased:
//			e.handled = OnMouseButtonEvent(e);
//			break;
//
//		case EventType::WindowResize:
//			e.handled = OnWindowResizeEvent(e);
//			break;
//
//		case EventType::WindowClose:
//			e.handled = OnWindowCloseEvent(e);
//			break;
//
//		default:
//			break;
//		}
//		return e.handled;
//	}
//}
//
