#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "spdlog/fmt/fmt.h"
#include "spdlog/sinks/callback_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

namespace msplat::log
{

// Severity levels matching spdlog
enum class Severity
{
	verbose = 0,
	debug,
	info,
	warning,
	error,
	critical,
	off
};

// Internal singleton logger class
class Logger
{
  private:
	std::shared_ptr<spdlog::logger> m_logger;
	std::mutex                      m_sinkMutex;        // Protects sink modifications

	// Private constructor for singleton
	Logger()
	{
		// Initialize with lambda for RAII
		static auto init = []() {
			auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");        // Skip logger name
			auto logger = std::make_shared<spdlog::logger>("default", console_sink);

			// Auto-flush on error
			logger->flush_on(spdlog::level::err);

			// Enable backtrace with last 32 messages
			logger->enable_backtrace(32);

// Set default level based on build config
#ifdef NDEBUG
			logger->set_level(spdlog::level::info);
#else
			logger->set_level(spdlog::level::debug);
#endif

			// Check environment variable override
			if (const char *env_level = std::getenv("LOG_LEVEL"))
			{
				// Parse and set level from: VERBOSE, DEBUG, INFO, WARNING, ERROR
				std::string level_str(env_level);
				std::transform(level_str.begin(), level_str.end(), level_str.begin(), ::toupper);

				if (level_str == "VERBOSE")
					logger->set_level(spdlog::level::trace);
				else if (level_str == "DEBUG")
					logger->set_level(spdlog::level::debug);
				else if (level_str == "INFO")
					logger->set_level(spdlog::level::info);
				else if (level_str == "WARNING")
					logger->set_level(spdlog::level::warn);
				else if (level_str == "ERROR")
					logger->set_level(spdlog::level::err);
			}

			return logger;
		}();
		m_logger = init;
	}

  public:
	// Thread-safe singleton access
	static Logger &getInstance()
	{
		static Logger instance;
		return instance;
	}

	// Get underlying spdlog logger
	std::shared_ptr<spdlog::logger> get()
	{
		return m_logger;
	}

	// Sink management (thread-safe)
	void setSink(spdlog::sink_ptr sink)
	{
		std::lock_guard<std::mutex> lock(m_sinkMutex);
		m_logger->sinks().clear();
		m_logger->sinks().push_back(sink);
	}

	void addSink(spdlog::sink_ptr sink)
	{
		std::lock_guard<std::mutex> lock(m_sinkMutex);
		m_logger->sinks().push_back(sink);
	}
};

// Template logging functions
template <typename... Args>
inline void log_verbose(const std::string &fmt, Args &&...args) noexcept
{
	try
	{
		auto &logger = Logger::getInstance();
		if constexpr (sizeof...(args) > 0)
		{
			auto formatted = fmt::vformat(fmt, fmt::make_format_args(args...));
			logger.get()->log(spdlog::level::trace, formatted);
		}
		else
		{
			logger.get()->log(spdlog::level::trace, fmt);
		}
	}
	catch (...)
	{}
}

template <typename... Args>
inline void log_debug(const std::string &fmt, Args &&...args) noexcept
{
	try
	{
		auto &logger = Logger::getInstance();
		if constexpr (sizeof...(args) > 0)
		{
			auto formatted = fmt::vformat(fmt, fmt::make_format_args(args...));
			logger.get()->log(spdlog::level::debug, formatted);
		}
		else
		{
			logger.get()->log(spdlog::level::debug, fmt);
		}
	}
	catch (...)
	{}
}

template <typename... Args>
inline void log_info(const std::string &fmt, Args &&...args) noexcept
{
	try
	{
		auto &logger = Logger::getInstance();
		if constexpr (sizeof...(args) > 0)
		{
			auto formatted = fmt::vformat(fmt, fmt::make_format_args(args...));
			logger.get()->log(spdlog::level::info, formatted);
		}
		else
		{
			logger.get()->log(spdlog::level::info, fmt);
		}
	}
	catch (...)
	{}
}

template <typename... Args>
inline void log_warning(const std::string &fmt, Args &&...args) noexcept
{
	try
	{
		auto &logger = Logger::getInstance();
		if constexpr (sizeof...(args) > 0)
		{
			auto formatted = fmt::vformat(fmt, fmt::make_format_args(args...));
			logger.get()->log(spdlog::level::warn, formatted);
		}
		else
		{
			logger.get()->log(spdlog::level::warn, fmt);
		}
	}
	catch (...)
	{}
}

template <typename... Args>
inline void log_error(const std::string &fmt, Args &&...args) noexcept
{
	try
	{
		auto &logger = Logger::getInstance();
		if constexpr (sizeof...(args) > 0)
		{
			auto formatted = fmt::vformat(fmt, fmt::make_format_args(args...));
			logger.get()->log(spdlog::level::err, formatted);
		}
		else
		{
			logger.get()->log(spdlog::level::err, fmt);
		}
		// Generate stack trace and abort
		spdlog::dump_backtrace();
		std::abort();
	}
	catch (...)
	{
		std::abort();
	}
}

template <typename... Args>
inline void log_critical(const std::string &fmt, Args &&...args) noexcept
{
	try
	{
		auto &logger = Logger::getInstance();
		if constexpr (sizeof...(args) > 0)
		{
			auto formatted = fmt::vformat(fmt, fmt::make_format_args(args...));
			logger.get()->log(spdlog::level::critical, formatted);
		}
		else
		{
			logger.get()->log(spdlog::level::critical, fmt);
		}
		// Generate stack trace and abort
		spdlog::dump_backtrace();
		std::abort();
	}
	catch (...)
	{
		std::abort();
	}
}

// Progress logging - uses direct stdout for progress indicators without newlines
template <typename... Args>
inline void log_progress(const std::string &fmt, Args &&...args) noexcept
{
	try
	{
		// Use a static mutex for thread safety
		static std::mutex           progress_mutex;
		std::lock_guard<std::mutex> lock(progress_mutex);

		if constexpr (sizeof...(args) > 0)
		{
			auto formatted = fmt::vformat(fmt, fmt::make_format_args(args...));
			std::cout << formatted << std::flush;
		}
		else
		{
			std::cout << fmt << std::flush;
		}
	}
	catch (...)
	{}
}

// Level control functions
inline void log_level_verbose()
{
	Logger::getInstance().get()->set_level(spdlog::level::trace);
}
inline void log_level_debug()
{
	Logger::getInstance().get()->set_level(spdlog::level::debug);
}
inline void log_level_info()
{
	Logger::getInstance().get()->set_level(spdlog::level::info);
}
inline void log_level_warning()
{
	Logger::getInstance().get()->set_level(spdlog::level::warn);
}
inline void log_level_error()
{
	Logger::getInstance().get()->set_level(spdlog::level::err);
}

// Sink management functions
inline void default_logger_set_sink(spdlog::sink_ptr sink)
{
	Logger::getInstance().setSink(sink);
}

inline void default_logger_add_sink(spdlog::sink_ptr sink)
{
	Logger::getInstance().addSink(sink);
}

inline spdlog::sink_ptr create_sink_with_callback(std::function<void(const spdlog::details::log_msg &)> callback)
{
	return std::make_shared<spdlog::sinks::callback_sink_mt>(callback);
}
}        // namespace msplat::log

// Basic logging macros
#define LOG_VERBOSE(fmt, ...) \
	msplat::log::log_verbose(fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) \
	msplat::log::log_debug(fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
	msplat::log::log_info(fmt, ##__VA_ARGS__)

#define LOG_WARNING(fmt, ...) \
	msplat::log::log_warning(fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
	msplat::log::log_error(fmt, ##__VA_ARGS__)

#define LOG_CRITICAL(fmt, ...) \
	msplat::log::log_critical(fmt, ##__VA_ARGS__)

#define LOG_FATAL(fmt, ...) \
	msplat::log::log_critical(fmt, ##__VA_ARGS__)

#define LOG_PROGRESS(fmt, ...) \
	msplat::log::log_progress(fmt, ##__VA_ARGS__)

// Location-aware variants
#define LOG_VERBOSE_WITH_LOCATION(fmt, ...) \
	msplat::log::log_verbose(fmt " [{}:{}]", ##__VA_ARGS__, __FILE__, __LINE__)

#define LOG_DEBUG_WITH_LOCATION(fmt, ...) \
	msplat::log::log_debug(fmt " [{}:{}]", ##__VA_ARGS__, __FILE__, __LINE__)

#define LOG_INFO_WITH_LOCATION(fmt, ...) \
	msplat::log::log_info(fmt " [{}:{}]", ##__VA_ARGS__, __FILE__, __LINE__)

#define LOG_WARNING_WITH_LOCATION(fmt, ...) \
	msplat::log::log_warning(fmt " [{}:{}]", ##__VA_ARGS__, __FILE__, __LINE__)

#define LOG_ERROR_WITH_LOCATION(fmt, ...) \
	msplat::log::log_error(fmt " [{}:{}]", ##__VA_ARGS__, __FILE__, __LINE__)

// Assert macros
#define LOG_ASSERT(condition, ...)                                                              \
	do                                                                                          \
	{                                                                                           \
		if (!(condition))                                                                       \
		{                                                                                       \
			constexpr bool has_message = sizeof(#__VA_ARGS__) > 1;                              \
			if constexpr (has_message)                                                          \
			{                                                                                   \
				msplat::log::log_error("Assertion failed: {} - {}", #condition, ##__VA_ARGS__); \
			}                                                                                   \
			else                                                                                \
			{                                                                                   \
				msplat::log::log_error("Assertion failed: {}", #condition);                     \
			}                                                                                   \
		}                                                                                       \
	} while (0)

#ifdef DEBUG
#	define LOG_DEBUG_ASSERT(condition, ...) LOG_ASSERT(condition, ##__VA_ARGS__)
#else
#	define LOG_DEBUG_ASSERT(condition, ...) ((void) 0)
#endif