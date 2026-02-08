#include <chrono>

#include "metal_backend.h"

namespace rhi::metal3
{

MetalFence::MetalFence(bool signaled) :
    signaled_(signaled)
{}

void MetalFence::Wait(uint64_t timeout)
{
	std::unique_lock<std::mutex> lock(mutex_);
	if (timeout == UINT64_MAX)
	{
		cv_.wait(lock, [this]() { return signaled_; });
		return;
	}

	cv_.wait_for(lock, std::chrono::nanoseconds(timeout), [this]() { return signaled_; });
}

void MetalFence::Reset()
{
	std::lock_guard<std::mutex> lock(mutex_);
	signaled_ = false;
}

bool MetalFence::IsSignaled() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return signaled_;
}

void MetalFence::Signal()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		signaled_ = true;
	}
	cv_.notify_all();
}

MetalCompositeFence::MetalCompositeFence(std::vector<FenceHandle> fences) :
    fences_(std::move(fences))
{}

void MetalCompositeFence::Wait(uint64_t timeout)
{
	using Clock         = std::chrono::steady_clock;
	const auto deadline = timeout == UINT64_MAX ? Clock::time_point::max() : Clock::now() + std::chrono::nanoseconds(timeout);

	for (const FenceHandle &fence : fences_)
	{
		if (fence.Get() == nullptr)
		{
			continue;
		}

		if (timeout == UINT64_MAX)
		{
			fence->Wait(UINT64_MAX);
			continue;
		}

		auto now = Clock::now();
		if (now >= deadline)
		{
			fence->Wait(0);
			continue;
		}

		const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
		fence->Wait(static_cast<uint64_t>(remaining));
	}
}

void MetalCompositeFence::Reset()
{
	for (const FenceHandle &fence : fences_)
	{
		if (fence.Get() != nullptr)
		{
			fence->Reset();
		}
	}
}

bool MetalCompositeFence::IsSignaled() const
{
	for (const FenceHandle &fence : fences_)
	{
		if (fence.Get() != nullptr && !fence->IsSignaled())
		{
			return false;
		}
	}
	return true;
}

}        // namespace rhi::metal3
