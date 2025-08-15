#include "core/timer.h"

namespace msplat::timer
{

Timer::Timer() :
    m_running(false)
{
}

void Timer::start()
{
	m_startTime = std::chrono::high_resolution_clock::now();
	m_running   = true;
}

void Timer::stop()
{
	if (m_running)
	{
		m_stopTime = std::chrono::high_resolution_clock::now();
		m_running  = false;
	}
}

void Timer::reset()
{
	m_running = false;
}

double Timer::elapsed(TimeUnit unit) const
{
	auto endTime  = m_running ? std::chrono::high_resolution_clock::now() : m_stopTime;
	auto duration = endTime - m_startTime;

	switch (unit)
	{
		case TimeUnit::Nanoseconds:
			return std::chrono::duration<double, std::nano>(duration).count();
		case TimeUnit::Microseconds:
			return std::chrono::duration<double, std::micro>(duration).count();
		case TimeUnit::Milliseconds:
			return std::chrono::duration<double, std::milli>(duration).count();
		case TimeUnit::Seconds:
		default:
			return std::chrono::duration<double>(duration).count();
	}
}

double Timer::elapsedSeconds() const
{
	return elapsed(TimeUnit::Seconds);
}

double Timer::elapsedMilliseconds() const
{
	return elapsed(TimeUnit::Milliseconds);
}

double Timer::elapsedMicroseconds() const
{
	return elapsed(TimeUnit::Microseconds);
}

double Timer::elapsedNanoseconds() const
{
	return elapsed(TimeUnit::Nanoseconds);
}

bool Timer::isRunning() const
{
	return m_running;
}

FPSCounter::FPSCounter(double updateInterval) :
    m_updateInterval(updateInterval), m_frameCount(0), m_lastFPS(0.0), m_hasValidFPS(false)
{
	m_timer.start();
}

void FPSCounter::frame()
{
	++m_frameCount;
}

bool FPSCounter::shouldUpdate() const
{
	return m_timer.elapsedSeconds() >= m_updateInterval;
}

double FPSCounter::getFPS() const
{
	if (shouldUpdate() && m_frameCount > 0)
	{
		// Calculate current FPS
		double elapsed = m_timer.elapsedSeconds();
		return static_cast<double>(m_frameCount) / elapsed;
	}

	// Return last valid FPS if available
	return m_hasValidFPS ? m_lastFPS : 0.0;
}

void FPSCounter::reset()
{
	if (shouldUpdate())
	{
		// Update last valid FPS before reset
		if (m_frameCount > 0)
		{
			double elapsed = m_timer.elapsedSeconds();
			m_lastFPS      = static_cast<double>(m_frameCount) / elapsed;
			m_hasValidFPS  = true;
		}

		m_frameCount = 0;
		m_timer.reset();
		m_timer.start();
	}
}

}        // namespace msplat::timer