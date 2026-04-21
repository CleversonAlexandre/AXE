#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace axe
{
	extern std::shared_ptr<spdlog::logger> log_space_mouse;
	extern std::shared_ptr<spdlog::logger> log_window;
	extern std::shared_ptr<spdlog::logger> log_window_event;
	extern std::shared_ptr<spdlog::logger> log_renderdoc;

	void initializa_logging();
}