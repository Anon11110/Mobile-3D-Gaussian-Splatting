#include <chrono>
#include <filesystem>
#include <iostream>
#include <msplat/core/log.h>
#include <msplat/engine/splat_loader.h>

void LoadAndDisplaySplatFile(const std::filesystem::path &plyPath)
{
	std::cout << "Loading PLY file: " << plyPath << std::endl;
	std::cout << "===========================================" << std::endl;

	if (!std::filesystem::exists(plyPath))
	{
		throw std::runtime_error("PLY file does not exist: " + plyPath.string());
	}

	auto startTime = std::chrono::high_resolution_clock::now();

	msplat::engine::SplatLoader loader;
	auto                        future = loader.Load(plyPath);

	std::cout << "Loading in progress";
	while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
	{
		std::cout << "." << std::flush;
	}
	std::cout << " done!" << std::endl;

	auto splatData = future.get();
	auto endTime   = std::chrono::high_resolution_clock::now();
	auto duration  = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

	std::cout << std::endl;
	std::cout << "Load Results:" << std::endl;
	std::cout << "-------------" << std::endl;
	std::cout << "File: " << plyPath.filename() << std::endl;
	std::cout << "Splats loaded: " << splatData->numSplats << std::endl;
	std::cout << "SH degree: " << splatData->shDegree << std::endl;
	std::cout << "SH coefficients per splat: " << splatData->shCoeffsPerSplat << std::endl;
	std::cout << "Load time: " << duration.count() << " ms" << std::endl;

	size_t totalBytes = splatData->numSplats * sizeof(float) *
	                    (3 + 3 + 4 + 1 + 3 + splatData->shCoeffsPerSplat);
	std::cout << "Memory usage: " << totalBytes / (1024.0 * 1024.0) << " MB" << std::endl;

	double pointsPerSecond = (splatData->numSplats / static_cast<double>(duration.count())) * 1000.0;
	std::cout << "Performance: " << pointsPerSecond << " splats/second" << std::endl;

	if (splatData->numSplats > 0)
	{
		std::cout << std::endl;
		std::cout << "Sample Data (first 5 splats):" << std::endl;
		std::cout << "------------------------------" << std::endl;

		uint32_t samplesToShow = std::min(5u, splatData->numSplats);
		for (uint32_t i = 0; i < samplesToShow; ++i)
		{
			std::cout << "Splat " << i << ":" << std::endl;
			std::cout << "  Position: (" << splatData->posX[i] << ", "
			          << splatData->posY[i] << ", " << splatData->posZ[i] << ")" << std::endl;
			std::cout << "  Scale: (" << splatData->scaleX[i] << ", "
			          << splatData->scaleY[i] << ", " << splatData->scaleZ[i] << ")" << std::endl;
			std::cout << "  Rotation: (" << splatData->rotX[i] << ", "
			          << splatData->rotY[i] << ", " << splatData->rotZ[i] << ", "
			          << splatData->rotW[i] << ")" << std::endl;
			std::cout << "  Opacity: " << splatData->opacity[i] << std::endl;
			std::cout << "  DC: (" << splatData->fDc0[i] << ", "
			          << splatData->fDc1[i] << ", " << splatData->fDc2[i] << ")" << std::endl;
			std::cout << std::endl;
		}

		std::cout << "Data Range Analysis:" << std::endl;
		std::cout << "-------------------" << std::endl;

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

		std::cout << "Position bounds:" << std::endl;
		std::cout << "  X: [" << minX << ", " << maxX << "] (range: " << (maxX - minX) << ")" << std::endl;
		std::cout << "  Y: [" << minY << ", " << maxY << "] (range: " << (maxY - minY) << ")" << std::endl;
		std::cout << "  Z: [" << minZ << ", " << maxZ << "] (range: " << (maxZ - minZ) << ")" << std::endl;
	}
}

void TestErrorHandling()
{
	std::cout << std::endl;
	std::cout << "Testing Error Handling:" << std::endl;
	std::cout << "======================" << std::endl;

	msplat::engine::SplatLoader loader;

	std::cout << "Testing non-existent file... ";
	try
	{
		auto future = loader.Load("non_existent_file.ply");
		auto result = future.get();
		std::cout << "[FAIL] Expected exception but got result" << std::endl;
	}
	catch (const std::exception &e)
	{
		std::cout << "[PASS] Caught expected exception: " << e.what() << std::endl;
	}
}

int main(int argc, char *argv[])
{
	std::cout << "========================================" << std::endl;
	std::cout << "    Splat Loader Example" << std::endl;
	std::cout << "========================================" << std::endl;
	std::cout << std::endl;

	std::filesystem::path plyPath;

	// Use command line argument if provided, otherwise use default asset
	if (argc >= 2)
	{
		plyPath = argv[1];
	}
	else
	{
		plyPath = "flowers_1.ply";
		std::cout << "No PLY file specified. Using default: " << plyPath << std::endl;
		std::cout << "Usage: " << argv[0] << " <path_to_ply_file>" << std::endl;
		std::cout << std::endl;
	}

	try
	{
		LoadAndDisplaySplatFile(plyPath);
		TestErrorHandling();

		std::cout << std::endl;
		std::cout << "========================================" << std::endl;
		std::cout << "    Example completed successfully!" << std::endl;
		std::cout << "========================================" << std::endl;
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << std::endl;
		std::cerr << "========================================" << std::endl;
		std::cerr << "    Error: " << e.what() << std::endl;
		std::cerr << "========================================" << std::endl;
		return 1;
	}
}