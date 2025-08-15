#include "core/log.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace msplat::log
{

// Default console backend implementation
class ConsoleBackend : public Logger::Backend
{
  public:
	void write(Severity severity, const std::string &message) override
	{
		auto now    = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		auto ms     = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
		          1000;

		std::ostringstream timestamp;
		timestamp << std::put_time(std::localtime(&time_t), "%H:%M:%S");
		timestamp << '.' << std::setfill('0') << std::setw(3) << ms.count();

		const char *severityStr = "";
		const char *colorCode   = "";
		const char *resetColor  = "\033[0m";

		switch (severity)
		{
			case Severity::Debug:
				severityStr = "DEBUG";
				colorCode   = "\033[36m";        // Cyan
				break;
			case Severity::Info:
				severityStr = "INFO";
				colorCode   = "\033[37m";        // White
				break;
			case Severity::Warning:
				severityStr = "WARN";
				colorCode   = "\033[33m";        // Yellow
				break;
			case Severity::Error:
				severityStr = "ERROR";
				colorCode   = "\033[31m";        // Red
				break;
			case Severity::Fatal:
				severityStr = "FATAL";
				colorCode   = "\033[35m";        // Magenta
				break;
			default:
				severityStr = "UNKNOWN";
				colorCode   = "";
				resetColor  = "";
				break;
		}

		std::ostream &output = (severity >= Severity::Error) ? std::cerr : std::cout;
		output << "[" << timestamp.str() << "] "
		       << colorCode << "[" << severityStr << "]" << resetColor
		       << " " << message << std::endl;
	}
};

Logger &Logger::getInstance()
{
	static Logger instance;
	// Initialize with default console backend if none set
	if (!instance.m_backend)
	{
		instance.m_backend = std::make_unique<ConsoleBackend>();

		// Set minimum severity based on build type
#ifdef NDEBUG
		// Release build - use Info level (hide Debug messages)
		instance.m_minimumSeverity = Severity::Info;
#else
		// Debug build - use Debug level (show all messages)
		instance.m_minimumSeverity = Severity::Debug;
#endif
	}
	return instance;
}

void Logger::setMinimumSeverity(Severity severity)
{
	m_minimumSeverity = severity;
}

Severity Logger::getMinimumSeverity() const
{
	return m_minimumSeverity;
}

void Logger::log(Severity severity, const std::string &message)
{
	// Filter based on minimum severity level
	if (severity < m_minimumSeverity || m_minimumSeverity == Severity::None)
	{
		return;
	}

	if (m_backend)
	{
		m_backend->write(severity, message);
	}
}

void Logger::setBackend(std::unique_ptr<Backend> backend)
{
	m_backend = std::move(backend);
}

}        // namespace msplat::log