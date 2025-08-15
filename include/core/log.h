#pragma once

#include <memory>
#include <string>

namespace msplat::log
{

enum class Severity
{
	None = 0,        // No logging
	Debug,           // Debug information
	Info,            // General information
	Warning,         // Warning conditions
	Error,           // Error conditions
	Fatal            // Fatal errors
};

class Logger
{
  public:
	static Logger &getInstance();

	void     setMinimumSeverity(Severity severity);
	Severity getMinimumSeverity() const;

	void log(Severity severity, const std::string &message);

	// Color management for console output
	class ColorScheme
	{
	  public:
		virtual ~ColorScheme()                                    = default;
		virtual const char *getColorCode(Severity severity) const = 0;
		virtual const char *getResetCode() const                  = 0;
	};

	// Default ANSI color scheme
	class DefaultColorScheme : public ColorScheme
	{
	  public:
		const char *getColorCode(Severity severity) const override;
		const char *getResetCode() const override;
	};

	// Extensibility points for future backends
	class Backend
	{
	  public:
		virtual ~Backend()                                                = default;
		virtual void write(Severity severity, const std::string &message) = 0;
	};

	void setBackend(std::unique_ptr<Backend> backend);

	// Default console backend implementation
	class ConsoleBackend : public Backend
	{
	  public:
		ConsoleBackend();
		void write(Severity severity, const std::string &message) override;

	  private:
		const char                  *getSeverityString(Severity severity) const;
		std::unique_ptr<ColorScheme> m_colorScheme;
	};

  private:
	Logger()                          = default;
	~Logger()                         = default;
	Logger(const Logger &)            = delete;
	Logger &operator=(const Logger &) = delete;

	Severity                 m_minimumSeverity = Severity::Info;
	std::unique_ptr<Backend> m_backend;
};

// Convenience macros for logging with severity levels
#define LOG_DEBUG(message) \
	msplat::log::Logger::getInstance().log(msplat::log::Severity::Debug, message)

#define LOG_INFO(message) \
	msplat::log::Logger::getInstance().log(msplat::log::Severity::Info, message)

#define LOG_WARNING(message) \
	msplat::log::Logger::getInstance().log(msplat::log::Severity::Warning, message)

#define LOG_ERROR(message) \
	msplat::log::Logger::getInstance().log(msplat::log::Severity::Error, message)

#define LOG_FATAL(message) \
	msplat::log::Logger::getInstance().log(msplat::log::Severity::Fatal, message)

}        // namespace msplat::log