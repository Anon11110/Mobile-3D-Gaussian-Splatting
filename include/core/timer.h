#pragma once

#include <chrono>

namespace msplat::timer
{

enum class TimeUnit
{
	Nanoseconds,
	Microseconds,
	Milliseconds,
	Seconds
};

class Timer
{
  public:
	Timer();

	void start();
	void stop();
	void reset();

	// Get elapsed time in specified unit
	double elapsed(TimeUnit unit = TimeUnit::Seconds) const;

	// Get elapsed time in specific units (convenience methods)
	double elapsedSeconds() const;
	double elapsedMilliseconds() const;
	double elapsedMicroseconds() const;
	double elapsedNanoseconds() const;

	// Check if timer is currently running
	bool isRunning() const;

  private:
	std::chrono::high_resolution_clock::time_point m_startTime;
	std::chrono::high_resolution_clock::time_point m_stopTime;
	bool                                           m_running;
};

class FPSCounter
{
  public:
	FPSCounter(double updateInterval = 1.0);

	void   frame();
	bool   shouldUpdate() const;
	double getFPS() const;
	void   reset();

  private:
	Timer  m_timer;
	double m_updateInterval;
	int    m_frameCount;
	double m_lastFPS;
	bool   m_hasValidFPS;
};

}        // namespace msplat::timer