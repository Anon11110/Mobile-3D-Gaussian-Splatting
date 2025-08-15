#include "core/log.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace msplat::log
{

// Default ANSI color scheme implementation
const char *Logger::DefaultColorScheme::getColorCode(Severity severity) const
{
	switch (severity)
	{
		case Severity::Debug:
			return "\033[36m";        // Cyan
		case Severity::Info:
			return "\033[37m";        // White
		case Severity::Warning:
			return "\033[33m";        // Yellow
		case Severity::Error:
			return "\033[31m";        // Red
		case Severity::Fatal:
			return "\033[35m";        // Magenta
		default:
			return "";
	}
}

const char *Logger::DefaultColorScheme::getResetCode() const
{
	return "\033[0m";
}

// ConsoleBackend implementation
Logger::ConsoleBackend::ConsoleBackend() :
    m_colorScheme(std::make_unique<Logger::DefaultColorScheme>())
{}

void Logger::ConsoleBackend::write(Severity severity, const std::string &message)
{
	auto now    = std::chrono::system_clock::now();
	auto time_t = std::chrono::system_clock::to_time_t(now);
	auto ms     = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
	          1000;

	std::ostringstream timestamp;
	timestamp << std::put_time(std::localtime(&time_t), "%H:%M:%S");
	timestamp << '.' << std::setfill('0') << std::setw(3) << ms.count();

	const char *severityStr = getSeverityString(severity);
	const char *colorCode   = m_colorScheme->getColorCode(severity);
	const char *resetColor  = m_colorScheme->getResetCode();

	std::ostream &output = (severity >= Severity::Error) ? std::cerr : std::cout;
	output << "[" << timestamp.str() << "] "
	       << colorCode << "[" << severityStr << "]" << resetColor
	       << " " << message << std::endl;
}

const char *Logger::ConsoleBackend::getSeverityString(Severity severity) const
{
	switch (severity)
	{
		case Severity::Debug:
			return "DEBUG";
		case Severity::Info:
			return "INFO";
		case Severity::Warning:
			return "WARN";
		case Severity::Error:
			return "ERROR";
		case Severity::Fatal:
			return "FATAL";
		default:
			return "UNKNOWN";
	}
}

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