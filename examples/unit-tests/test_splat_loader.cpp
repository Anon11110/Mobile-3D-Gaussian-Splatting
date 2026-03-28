#include "test_framework.h"
#include "test_utils.h"

#include <chrono>
#include <filesystem>
#include <msplat/core/log.h>
#include <msplat/core/timer.h>
#include <msplat/engine/splat/splat_loader.h>

TEST(splat_loader_basic)
{
	auto plyPath = FindTestPly();
	if (plyPath.empty())
	{
		LOG_WARNING("Skipping splat_loader_basic: no PLY file found");
		return true;
	}

	msplat::engine::SplatLoader loader;
	msplat::timer::Timer        timer;
	timer.start();

	auto future    = loader.Load(plyPath);
	auto splatData = future.get();
	timer.stop();

	if (!splatData || splatData->empty())
	{
		LOG_ERROR("Failed to load splat data from {}", plyPath.string());
		return false;
	}

	LOG_INFO("Loaded {} splats (SH degree {}) in {:.0f} ms",
	         splatData->numSplats, splatData->shDegree, timer.elapsedMilliseconds());

	// Verify basic data integrity
	if (splatData->posX.size() != splatData->numSplats ||
	    splatData->scaleX.size() != splatData->numSplats ||
	    splatData->rotW.size() != splatData->numSplats ||
	    splatData->opacity.size() != splatData->numSplats)
	{
		LOG_ERROR("Array size mismatch: expected {} elements", splatData->numSplats);
		return false;
	}

	return true;
}

TEST(splat_loader_data_ranges)
{
	auto plyPath = FindTestPly();
	if (plyPath.empty())
	{
		LOG_WARNING("Skipping splat_loader_data_ranges: no PLY file found");
		return true;
	}

	msplat::engine::SplatLoader loader;
	auto                        splatData = loader.Load(plyPath).get();

	if (!splatData || splatData->empty())
		return false;

	// Verify positions are finite
	for (uint32_t i = 0; i < splatData->numSplats; ++i)
	{
		if (!std::isfinite(splatData->posX[i]) ||
		    !std::isfinite(splatData->posY[i]) ||
		    !std::isfinite(splatData->posZ[i]))
		{
			LOG_ERROR("Non-finite position at splat {}", i);
			return false;
		}
	}

	// Verify opacity logits are finite
	for (uint32_t i = 0; i < splatData->numSplats; ++i)
	{
		if (!std::isfinite(splatData->opacity[i]))
		{
			LOG_ERROR("Non-finite opacity logit at splat {}: {}", i, splatData->opacity[i]);
			return false;
		}
	}

	LOG_INFO("All {} splats have valid positions and opacity logits", splatData->numSplats);
	return true;
}

TEST(splat_loader_error_handling)
{
	msplat::engine::SplatLoader loader;

	try
	{
		auto future = loader.Load("non_existent_file.ply");
		auto result = future.get();
		LOG_ERROR("Expected exception for non-existent file but got result");
		return false;
	}
	catch (const std::exception &)
	{
		return true;
	}
}
