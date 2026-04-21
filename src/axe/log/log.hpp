#pragma once

#include "axe/core/types.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace axe
{
	static constexpr const char* const c_logging_configuration_file_path = "config/logging.json";

	AXE_API void console_init();
	AXE_API void InitializeLogSinks();
	AXE_API void LogToConsole();
	AXE_API void ConfigureLogLevels(const std::vector<std::pair<std::string, std::string>>& nameLevelPairs);
	AXE_API void LoadLogConfiguration(const std::string& jsonContent);

	AXE_API auto makeLogger(const std::string& name, bool tail = true) -> std::shared_ptr<spdlog::logger>;
	AXE_API auto makeFrameLogger(const std::string& name) -> std::shared_ptr<spdlog::logger>;
	AXE_API auto makeFileLogger(const std::string& name, const std::string& filepath) -> std::shared_ptr<spdlog::logger>;

	class AXE_API Entry
	{
	public:
		uint64_t serial{ 0 };
		bool selected{ false };
		std::string timestamp;
		std::string message;
		std::string logger;
		unsigned int repeatCount{ 0 };
		spdlog::level::level_enum level{ 2 };
	};

	class AXE_API StoreLogSink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		StoreLogSink();

		[[nodiscard]] auto getSerial() const -> uint64_t;
		void Trim(std::size_t trim_size);
		void AccessEntries(std::function<void(std::deque<Entry>& entries)> op);

		

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override;
		void flush_() override;

	private:
		uint64_t m_Serial{ 0 };
		std::deque<Entry> m_Enrtries;
	};

	[[nodiscard]] AXE_API auto get_tail_store_log() -> StoreLogSink&;
	[[nodiscard]] AXE_API auto get_frame_store_log() -> StoreLogSink&;
	[[nodiscard]] AXE_API auto get_groupname(const std::string& s) -> std::string;
	[[nodiscard]] AXE_API auto get_basename(const std::string& s) -> std::string;
	[[nodiscard]] AXE_API auto get_levelname(spdlog::level::level_enum level) -> std::string;

	class AXE_API InfoLog
	{
	public:
		static void Init();

		static std::shared_ptr<spdlog::logger>& GetCoreLogger();
		static std::shared_ptr<spdlog::logger>& GetEditorLogger();

		static void EnableConsoleLogging();

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_EditorLogger;
	};
}

#define AXE_CORE_TRACE(...) ::axe::InfoLog::GetCoreLogger()->trace(__VA_ARGS__)
#define AXE_CORE_INFO(...)  ::axe::InfoLog::GetCoreLogger()->info(__VA_ARGS__)
#define AXE_CORE_WARN(...)  ::axe::InfoLog::GetCoreLogger()->warn(__VA_ARGS__)
#define AXE_CORE_ERROR(...) ::axe::InfoLog::GetCoreLogger()->error(__VA_ARGS__)

#define AXE_EDITOR_TRACE(...) ::axe::InfoLog::GetEditorLogger()->trace(__VA_ARGS__)
#define AXE_EDITOR_INFO(...)  ::axe::InfoLog::GetEditorLogger()->info(__VA_ARGS__)
#define AXE_EDITOR_WARN(...)  ::axe::InfoLog::GetEditorLogger()->warn(__VA_ARGS__)
#define AXE_EDITOR_ERROR(...) ::axe::InfoLog::GetEditorLogger()->error(__VA_ARGS__)