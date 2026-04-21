#include "window_log.hpp"
#include "axe/log/log.hpp"

namespace axe
{
	 std::shared_ptr<spdlog::logger> log_space_mouse;
	 std::shared_ptr<spdlog::logger> log_window;
	 std::shared_ptr<spdlog::logger> log_window_event;
	 std::shared_ptr<spdlog::logger> log_renderdoc;

	 void initializa_logging()
	 {
		 log_space_mouse = makeLogger("axe.window.space_mouse");
		 log_window = makeLogger("axe.window.window");
		 log_window_event = makeLogger("axe.window.window_event");
		 log_renderdoc = makeLogger("axe.window.renderdoc");
	 }
}