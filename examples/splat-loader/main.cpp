#include <filesystem>
#include <msplat/core/log.h>
#include <msplat/core/timer.h>
#include <msplat/engine/splat_loader.h>

void LoadAndDisplaySplatFile(const std::filesystem::path &plyPath)
{
	LOG_INFO("Loading PLY file: {}", plyPath.string());
	LOG_INFO("===========================================");

	if (!std::filesystem::exists(plyPath))
	{
		auto absolutePath = std::filesystem::absolute(plyPath);
		throw std::runtime_error("PLY file does not exist: " + absolutePath.string());
	}

	msplat::timer::Timer timer;
	timer.start();

	msplat::engine::SplatLoader loader;
	auto                        future = loader.Load(plyPath);

	LOG_INFO("Loading file in progress...");
	LOG_INFO("");
	while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
	{
		LOG_PROGRESS("");
	}
	LOG_INFO("");

	auto splatData = future.get();
	timer.stop();

	LOG_INFO("Load Results:");
	LOG_INFO("-------------");
	LOG_INFO("File: {}", plyPath.filename().string());
	LOG_INFO("Splats loaded: {}", splatData->numSplats);
	LOG_INFO("SH degree: {}", splatData->shDegree);
	LOG_INFO("SH coefficients per splat: {}", splatData->shCoeffsPerSplat);
	LOG_INFO("Load time: {} ms", static_cast<int>(timer.elapsedMilliseconds()));

	size_t totalBytes = splatData->numSplats * sizeof(float) *
	                    (3 + 3 + 4 + 1 + 3 + splatData->shCoeffsPerSplat);
	LOG_INFO("Memory usage: {:.2f} MB", totalBytes / (1024.0 * 1024.0));

	double pointsPerSecond = (splatData->numSplats / timer.elapsedMilliseconds()) * 1000.0;
	LOG_INFO("Performance: {:.0f} splats/second", pointsPerSecond);

	if (splatData->numSplats > 0)
	{
		LOG_INFO("");
		LOG_INFO("Sample Data (first 5 splats):");
		LOG_INFO("------------------------------");

		uint32_t samplesToShow = std::min(5u, splatData->numSplats);
		for (uint32_t i = 0; i < samplesToShow; ++i)
		{
			LOG_INFO("Splat {}:", i);
			LOG_INFO("  Position: ({}, {}, {})", splatData->posX[i], splatData->posY[i], splatData->posZ[i]);
			LOG_INFO("  Scale: ({}, {}, {})", splatData->scaleX[i], splatData->scaleY[i], splatData->scaleZ[i]);
			LOG_INFO("  Rotation: ({}, {}, {}, {})", splatData->rotX[i], splatData->rotY[i], splatData->rotZ[i], splatData->rotW[i]);
			LOG_INFO("  Opacity: {}", splatData->opacity[i]);
			LOG_INFO("  DC: ({}, {}, {})", splatData->fDc0[i], splatData->fDc1[i], splatData->fDc2[i]);
			LOG_INFO("");
		}

		LOG_INFO("Data Range Analysis:");
		LOG_INFO("-------------------");

		float minX = splatData->posX[0], maxX = splatData->posX[0];
		float minY = splatData->posY[0], maxY = splatData->posY[0];
		float minZ = splatData->posZ[0], maxZ = splatData->posZ[0];

		for (uint32_t i = 1; i < splatData->numSplats; ++i)
		{
			minX = std::min(minX, splatData->posX[i]);
			maxX = std::max(maxX, splatData->posX[i]);
			minY = std::min(minY, splatData->posY[i]);
			maxY = std::max(maxY, splatData->posY[i]);
			minZ = std::min(minZ, splatData->posZ[i]);
			maxZ = std::max(maxZ, splatData->posZ[i]);
		}

		LOG_INFO("Position bounds:");
		LOG_INFO("  X: [{}, {}] (range: {})", minX, maxX, (maxX - minX));
		LOG_INFO("  Y: [{}, {}] (range: {})", minY, maxY, (maxY - minY));
		LOG_INFO("  Z: [{}, {}] (range: {})", minZ, maxZ, (maxZ - minZ));
	}
}

void TestErrorHandling()
{
	LOG_INFO("");
	LOG_INFO("Testing Error Handling:");
	LOG_INFO("======================");

	msplat::engine::SplatLoader loader;

	LOG_INFO("Testing non-existent file... ");
	try
	{
		auto future = loader.Load("non_existent_file.ply");
		auto result = future.get();
		LOG_INFO("[FAIL] Expected exception but got result");
	}
	catch (const std::exception &e)
	{
		LOG_INFO("[PASS] Caught expected exception: {}", e.what());
	}
}

int main(int argc, char *argv[])
{
	LOG_INFO("========================================");
	LOG_INFO("    Splat Loader Example");
	LOG_INFO("========================================");
	LOG_INFO("");

	std::filesystem::path plyPath;

	// Use command line argument if provided, otherwise use default asset
	if (argc >= 2)
	{
		plyPath = argv[1];
		LOG_INFO("Using specified PLY file: {}", plyPath.string());
	}
	else
	{
		// Default file should be in the same directory as the executable
		plyPath = "flowers_1.ply";

		// Convert to absolute path for better error reporting
		auto absolutePath = std::filesystem::absolute(plyPath);

		LOG_INFO("No PLY file specified. Using default: {}", plyPath.string());
		LOG_INFO("Looking for file at: {}", absolutePath.string());
		LOG_INFO("Usage: {} <path_to_ply_file>", argv[0]);
		LOG_INFO("");
	}

	try
	{
		LoadAndDisplaySplatFile(plyPath);
		TestErrorHandling();

		LOG_INFO("");
		LOG_INFO("========================================");
		LOG_INFO("    Example completed successfully!");
		LOG_INFO("========================================");
		return 0;
	}
	catch (const std::exception &e)
	{
		LOG_ERROR("");
		LOG_ERROR("========================================");
		LOG_ERROR("    Error: {}", e.what());
		LOG_ERROR("========================================");
		return 1;
	}
}