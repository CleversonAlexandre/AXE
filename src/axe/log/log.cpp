#include "log.hpp"


#include "timestamp.hpp"

#include <simdjson.h>


#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#if defined _WIN32
	#include <spdlog/sinks/msvc_sink.h>
#endif

#if defined _WIN32

#include <windows.h>
#else
#include <unistd.h>
#endif

#include <unordered_map>
#include <vector>

#include <iostream>
namespace axe
{
	std::shared_ptr<spdlog::logger> InfoLog::s_CoreLogger;
	std::shared_ptr<spdlog::logger> InfoLog::s_EditorLogger;


	void InfoLog::Init()
	{
		InitializeLogSinks();
		EnableConsoleLogging(); // <-- aqui, antes de criar os loggers

		s_CoreLogger = makeLogger("axe");
		s_EditorLogger = makeLogger("editor");

		//s_CoreLogger->info("Core logger initialized");
		//s_EditorLogger->info("Editor logger initialized");
	}

	std::shared_ptr<spdlog::logger>& InfoLog::GetCoreLogger()
	{
		return s_CoreLogger;
	}

	std::shared_ptr<spdlog::logger>& InfoLog::GetEditorLogger()
	{
		return s_EditorLogger;
	}
	

	static auto GetLogLevelMap() -> std::unordered_map<std::string, std::string>&
	{
		static std::unordered_map<std::string, std::string> map;
		return map;
	}

	void ConfigureLogLevels(const std::vector<std::pair<std::string, std::string>>& nameLevelPairs)
	{
		std::unordered_map<std::string, std::string>& map = GetLogLevelMap();
		for (const auto& pair : nameLevelPairs)
		{
			map[pair.first] = pair.second;
		}

		for (const auto& pair : nameLevelPairs)
		{
			std::shared_ptr<spdlog::logger> logger = spdlog::get(pair.first);
			if (logger)
			{
				auto from_str = [](const std::string& name) -> spdlog::level::level_enum 
					{
					auto it = std::find(
						std::begin(spdlog::level::level_string_views),
						std::end(spdlog::level::level_string_views),
						name
					);
					if (it != std::end(spdlog::level::level_string_views))
					{
						return static_cast<spdlog::level::level_enum>(
							std::distance(std::begin(spdlog::level::level_string_views), it)
							);
					}
					if (name == "warn") return spdlog::level::warn;
					if (name == "err") return spdlog::level::err;
					return spdlog::level::err;
					};

				logger->set_level(from_str(pair.second));
			}
		}
	}

	void LoadLogConfiguration(const std::string& json_contents)
	{
		simdjson::ondemand::parser parser;
		simdjson::padded_string padded{ json_contents };
		simdjson::ondemand::document doc;
		simdjson::error_code error = parser.iterate(padded).get(doc);
		if(error)
		{
			return;
		}
		simdjson::ondemand::object obj;
		error = doc.get_object().get(obj);
		if (error)
		{
			return;
		}
		simdjson::ondemand::object loggers;
		error = obj["loggers"].get_object().get(loggers);
		if (error)
		{
			return;
		}
		std::vector<std::pair<std::string, std::string>> levels;
		for (auto logger_field : loggers)
		{
			std::string_view name;
			if (logger_field.unescaped_key().get(name) != simdjson::SUCCESS)
			{
				continue;
			}
			std::string_view level;
			if (logger_field.unescaped_key().get(level) != simdjson::SUCCESS)
			{
				continue;
			}
			levels.emplace_back(std::string{ name }, std::string{ level });
		}
		ConfigureLogLevels(levels);
		
	}


	void console_init()
	{


		

#if defined _WIN32
		HWND   hwnd = GetConsoleWindow();
		HICON  icon = LoadIcon(NULL, MAKEINTRESOURCE(32516));
		HANDLE hConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD  mode = 0;
		GetConsoleMode(hConsoleHandle, &mode);
		SetConsoleMode(hConsoleHandle, (mode & ~ENABLE_MOUSE_INPUT) | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);

		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)(icon));
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)(icon));

		std::setlocale(LC_CTYPE, ".UTF8");
		SetConsoleCP(CP_UTF8);
		SetConsoleOutputCP(CP_UTF8);

		
#endif
	}

	StoreLogSink::StoreLogSink()
	{}

	auto StoreLogSink::getSerial() const->uint64_t
	{
		return m_Serial;
	}

	void StoreLogSink::AccessEntries(std::function<void(std::deque<Entry>& entries)> op)
	{
		std::lock_guard<std::mutex> lock{ mutex_ };
		op(m_Enrtries);
	}

	void StoreLogSink::Trim(const std::size_t trim_size)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (m_Enrtries.size() > trim_size)
		{
			const auto trimCount = m_Enrtries.size() - trim_size;
			m_Enrtries.erase(
				m_Enrtries.begin(),
				m_Enrtries.begin() + trimCount
			);
			assert(m_Enrtries.size() == trim_size);
		}
	}
	void StoreLogSink::sink_it_(const spdlog::details::log_msg& msg)
	{
		++m_Serial;
		m_Enrtries.push_back(
			Entry{
				.serial = m_Serial,
				.timestamp = axe::timestampShort(),
				.message = std::string{msg.payload.begin(), msg.payload.end()},
				.logger = std::string{msg.logger_name.begin(), msg.logger_name.end()},
				.repeatCount = 0,
				.level = msg.level,
			}
			);
	}

	void StoreLogSink::flush_()
	{}

	class LogSinks
	{
	public:
		static LogSinks& get_instance()
		{
			static LogSinks static_instance;
			return static_instance;
		}

#if defined _WIN32
		auto get_msvc_sink() -> spdlog::sinks::msvc_sink_mt& { return *m_sink_msvc.get(); }
#endif
			auto get_console_sink() -> spdlog::sinks::stdout_color_sink_mt& { return *m_sink_console.get(); }
			auto get_file_sink() -> spdlog::sinks::basic_file_sink_mt& { return *m_sink_log_file.get(); }
			auto get_tail_store_sink() -> StoreLogSink& { return *m_tail_store_log.get(); }
			auto get_frame_store_sink() -> StoreLogSink& { return *m_frame_store_log.get(); }
			auto get_log_to_console() const -> bool { return m_log_to_console; }
			void set_log_to_console(bool value) { m_log_to_console = value; }

			auto makeLogger(const std::string& name, bool tail = true) -> std::shared_ptr<spdlog::logger>
		{
			//AXE_VERIFY(!name.empty())

			std::string levelname;
			{
				const std::unordered_map<std::string, std::string>& levels = GetLogLevelMap();
				const auto it = levels.find(name);
				if (it != levels.end())
				{
					levelname = it->second;
				}
			}

			auto from_str = [](const std::string& name) -> spdlog::level::level_enum
				{
					auto it = std::find(std::begin(spdlog::level::level_string_views), std::end(spdlog::level::level_string_views), name);
					if (it != std::end(spdlog::level::level_string_views))
					{
						return static_cast<spdlog::level::level_enum>(std::distance(std::begin(spdlog::level::level_string_views), it));

						if (name == "warn")
						{
							return spdlog::level::warn;
						}
						if (name == "err")
						{
							return spdlog::level::err;
						}
						return spdlog::level::err;
					}
					return spdlog::level::info;
				};
			const spdlog::level::level_enum level_parsed = from_str(levelname);

			std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>(
				name,
				spdlog::sinks_init_list
				{
	#if defined _WIN32
					m_sink_msvc,
	#else
					m_sink_console,
	#endif
					//m_sink_console ficará temporariomente aqui 
					//m_sink_console,
					m_sink_log_file,
					
					tail ? m_tail_store_log : m_frame_store_log
				}
			);
			if (m_log_to_console)
			{
				logger->sinks().push_back(m_sink_console);
			}
				std::shared_ptr<spdlog::logger> logger_copy = logger;
				spdlog::register_logger(logger_copy);
				logger->set_level(level_parsed);
				logger->flush_on(spdlog::level::trace);
				return logger;
			}


			void create_sinks()
			{
				m_sink_log_file = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/log.txt", true);

				// If you get a crash here:
				// - Install / repair latest VC redistributable from
				//   https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170
				// - See also: https://github.com/gabime/spdlog/issues/3145
				//m_sink_log_file->set_pattern("[%H:%M:%S %z] [%n] [%L] [%t] %v");


				m_sink_log_file->set_pattern("[%H:%M:%S.%e] [%L] [%n] %v");

#if defined _WIN32
				m_sink_msvc = std::make_shared<spdlog::sinks::msvc_sink_mt>();
				m_sink_msvc->set_pattern("[%n] [%L] %v");
#endif
				m_sink_console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
				m_tail_store_log = std::make_shared<StoreLogSink>();
				m_frame_store_log = std::make_shared<StoreLogSink>();
			}

		private:
			LogSinks() 
			{
			}
			~LogSinks() 
			{
			}

#if defined _WIN32
			std::shared_ptr<spdlog::sinks::msvc_sink_mt> m_sink_msvc{};
#endif
			std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> m_sink_console{};
			std::shared_ptr<spdlog::sinks::basic_file_sink_mt> m_sink_log_file{};
			std::shared_ptr<StoreLogSink> m_tail_store_log{};
			std::shared_ptr<StoreLogSink> m_frame_store_log{};
			bool m_log_to_console{ false };
};

void InfoLog::EnableConsoleLogging()
{
	LogSinks::get_instance().set_log_to_console(true);
}


auto get_tail_store_log() -> StoreLogSink&
{
	return LogSinks::get_instance().get_tail_store_sink();
}

auto get_frame_store_log() -> StoreLogSink&
{
	return LogSinks::get_instance().get_frame_store_sink();
}

void LogToConsole()
{
	LogSinks::get_instance().create_sinks();
}

void InitializeLogSinks()
{
	static bool initialized = false;
	if (initialized) return;
	initialized = true;

	LogSinks::get_instance().create_sinks();
}

auto get_groupname(const std::string& s) -> std::string
{
	std::string::size_type pos = s.find_last_of(".");
	if (pos != std::string::npos)
	{
		return s.substr(0, pos);
	}
	else
	{
		return std::string{};
	}
}

auto get_basename(const std::string& s) -> std::string
{
	std::string::size_type pos = s.find_last_of(".");
	if (pos != std::string::npos && ((pos + 1) < s.size()))
	{
		return s.substr(pos + 1);
	}
	else
	{
		return std::string{};
	}
}

auto get_levelname(spdlog::level::level_enum level) -> std::string
{
	const auto sv = spdlog::level::to_string_view(level);
	return std::string{ sv.begin(), sv.begin() + sv.size() };
}

auto makeFrameLogger(const std::string& name) -> std::shared_ptr<spdlog::logger>
{
	return makeLogger(name, false);
}

auto makeLogger(const std::string& name, const bool tail) -> std::shared_ptr<spdlog::logger>
{
	return LogSinks::get_instance().makeLogger(name, tail);
}

auto makeFileLogger(const std::string& name, const std::string& file_path) -> std::shared_ptr<spdlog::logger>
{
	// Compartilhar um único destino de arquivo por caminho para que vários registradores possam
	// direcionar para o mesmo arquivo de saída sem que cada um abra o arquivo no modo de truncamento e sobrescreva os outros.
	static std::mutex s_file_sink_mutex;
	static std::unordered_map<std::string, std::shared_ptr<spdlog::sinks::basic_file_sink_mt>> s_file_sinks;
	std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file_sink;
	{
		std::lock_guard<std::mutex> lock{ s_file_sink_mutex };
		auto it = s_file_sinks.find(file_path);
		if (it == s_file_sinks.end())
		{
			file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file_path, true);
			file_sink->set_pattern("[%H:%M:%S.%e] [%L] [%n] %v");
			s_file_sinks.emplace(file_path, file_sink);
		}
		else
		{
			file_sink = it->second;
		}
	}
	// Construa sobre o make_logger padrão para que o resultado tenha o conjunto completo
	// de sinks compartilhados (console / MSVC / log principal.txt / store). O
	// sink dedicado por arquivo é adicionado para que a saída vá para seu próprio
	// arquivo E para todos os lugares usuais.

	std::shared_ptr<spdlog::logger> logger = LogSinks::get_instance().makeLogger(name, /*tail=*/true);
	logger->sinks().push_back(file_sink);
	return logger;
}

}
