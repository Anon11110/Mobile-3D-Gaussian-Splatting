#include <filesystem>
#include <msplat/core/log.h>
#include <msplat/core/timer.h>
#include <msplat/engine/splat_loader.h>
#include <msplat/engine/scene.h>
#include <msplat/core/math/matrix.h>
#include <rhi/rhi.h>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>

void TestSceneLoading(rhi::IRHIDevice* device, const std::filesystem::path& plyPath)
{
	LOG_INFO("========================================");
	LOG_INFO("    Testing Scene Loading");
	LOG_INFO("========================================");
	LOG_INFO("");

	msplat::engine::Scene scene(device);
	msplat::engine::SplatLoader loader;

	LOG_INFO("Test 1: Loading single mesh");
	LOG_INFO("----------------------------");

	msplat::timer::Timer timer;
	timer.start();

	auto future = loader.Load(plyPath);
	LOG_INFO("Loading {} ...", plyPath.filename().string());

	while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
	{
		LOG_PROGRESS("");
	}

	auto splatData = future.get();
	timer.stop();

	if (!splatData || splatData->empty())
	{
		LOG_ERROR("Failed to load splat data from {}", plyPath.string());
		return;
	}

	LOG_INFO("Loaded {} splats in {:.2f} ms", splatData->numSplats, timer.elapsedMilliseconds());

	auto meshId = scene.AddMesh(splatData, msplat::math::Identity());
	LOG_INFO("Added mesh to scene with ID: {}", meshId);
	LOG_INFO("Total splats in scene: {}", scene.GetTotalSplatCount());
	LOG_INFO("");
}

void TestMultipleMeshes(rhi::IRHIDevice* device, const std::filesystem::path& plyPath)
{
	LOG_INFO("Test 2: Loading multiple meshes with transforms");
	LOG_INFO("------------------------------------------------");

	msplat::engine::Scene scene(device);
	msplat::engine::SplatLoader loader;

	// Load the same file multiple times with different transforms
	const int numCopies = 3;
	std::vector<std::shared_ptr<msplat::engine::SplatSoA>> loadedData;

	LOG_INFO("Loading base splat data...");
	auto future = loader.Load(plyPath);
	auto baseSplatData = future.get();

	if (!baseSplatData || baseSplatData->empty())
	{
		LOG_ERROR("Failed to load base splat data");
		return;
	}

	LOG_INFO("Base data loaded: {} splats", baseSplatData->numSplats);

	// Add multiple instances with different transforms
	for (int i = 0; i < numCopies; ++i)
	{
		float offset = static_cast<float>(i) * 2.0f;
		auto transform = msplat::math::Translate(msplat::math::vec3(offset, 0, 0));

		auto meshId = scene.AddMesh(baseSplatData, transform);
		LOG_INFO("Added mesh {} at offset ({}, 0, 0), ID: {}", i, offset, meshId);
	}

	LOG_INFO("Total meshes: {}", numCopies);
	LOG_INFO("Total splats in scene: {}", scene.GetTotalSplatCount());
	LOG_INFO("");
}

void TestGpuUpload(rhi::IRHIDevice* device, const std::filesystem::path& plyPath)
{
	LOG_INFO("Test 3: GPU Upload");
	LOG_INFO("------------------");

	msplat::engine::Scene scene(device);
	msplat::engine::SplatLoader loader;

	auto future = loader.Load(plyPath);
	auto splatData = future.get();

	if (!splatData || splatData->empty())
	{
		LOG_ERROR("Failed to load splat data");
		return;
	}

	scene.AddMesh(splatData, msplat::math::Identity());
	LOG_INFO("Added {} splats to scene", splatData->numSplats);

	LOG_INFO("Allocating GPU buffers...");
	scene.AllocateGpuBuffers();

	LOG_INFO("Uploading attribute data to GPU...");

	msplat::timer::Timer uploadTimer;
	uploadTimer.start();

	auto uploadFence = scene.UploadAttributeData();

	if (uploadFence)
	{
		LOG_INFO("Upload initiated, waiting for completion...");
		uploadFence->Wait();
		uploadTimer.stop();

		LOG_INFO("GPU upload completed in {:.2f} ms", uploadTimer.elapsedMilliseconds());

		size_t totalBytes = scene.GetTotalSplatCount() * sizeof(float) *
		                   (3 /*pos*/ + 3 /*scale*/ + 4 /*rot*/ + 3 /*color*/);

		// Add SH bytes separately since it can vary per mesh
		scene.ForEachMesh([&totalBytes](const msplat::engine::SplatMesh& mesh) {
			if (mesh.HasCpuData()) {
				auto data = mesh.GetSplatData();
				totalBytes += data->numSplats * data->shCoeffsPerSplat * sizeof(float);
			}
		});

		double bandwidthMBps = (totalBytes / (1024.0 * 1024.0)) / (uploadTimer.elapsedMilliseconds() / 1000.0);
		LOG_INFO("Upload bandwidth: {:.2f} MB/s", bandwidthMBps);
		LOG_INFO("Total data uploaded: {:.2f} MB", totalBytes / (1024.0 * 1024.0));

		const auto& gpuData = scene.GetGpuData();
		LOG_INFO("GPU buffers allocated:");
		LOG_INFO("  - Positions: {} bytes", gpuData.positions->GetSize());
		LOG_INFO("  - Scales: {} bytes", gpuData.scales->GetSize());
		LOG_INFO("  - Rotations: {} bytes", gpuData.rotations->GetSize());
		LOG_INFO("  - Colors: {} bytes", gpuData.colors->GetSize());
		if (gpuData.shRest)
		{
			LOG_INFO("  - SH Rest: {} bytes", gpuData.shRest->GetSize());
		}

		// Retire completed GPU operations (optional in test, but good practice)
		device->RetireCompletedFrame();
	}
	else
	{
		LOG_ERROR("Failed to create upload fence");
	}

	LOG_INFO("");
}

void TestMemoryRelease(rhi::IRHIDevice* device, const std::filesystem::path& plyPath)
{
	LOG_INFO("Test 4: CPU Memory Release");
	LOG_INFO("-----------------------------------------------");

	msplat::engine::Scene scene(device);
	msplat::engine::SplatLoader loader;

	auto future = loader.Load(plyPath);
	auto splatData = future.get();

	if (!splatData || splatData->empty())
	{
		LOG_ERROR("Failed to load splat data");
		return;
	}

	auto meshId = scene.AddMesh(splatData, msplat::math::Identity());
	uint32_t splatCount = splatData->numSplats;

	size_t cpuMemoryBytes = splatCount * sizeof(float) *
	                       (3 + 3 + 4 + 1 + 3 + splatData->shCoeffsPerSplat);
	LOG_INFO("Initial CPU memory usage: {:.2f} MB", cpuMemoryBytes / (1024.0 * 1024.0));

	// Allocate GPU buffers first
	scene.AllocateGpuBuffers();

	// Upload to GPU
	auto uploadFence = scene.UploadAttributeData();
	if (uploadFence)
	{
		uploadFence->Wait();
		LOG_INFO("Data uploaded to GPU");

		// Retire completed GPU operations
		device->RetireCompletedFrame();

		// Release CPU memory after upload
		scene.ForEachMesh([](msplat::engine::SplatMesh& mesh) {
			if (mesh.HasCpuData())
			{
				LOG_INFO("Releasing CPU data for mesh {}", mesh.GetId());
				mesh.ReleaseCpuData();
			}
		});

		// Verify CPU data is released
		bool allReleased = true;
		scene.ForEachMesh([&allReleased](const msplat::engine::SplatMesh& mesh) {
			if (mesh.HasCpuData())
			{
				allReleased = false;
			}
		});

		if (allReleased)
		{
			LOG_INFO("SUCCESS: All CPU memory released after GPU upload");
			LOG_INFO("CPU memory saved: {:.2f} MB", cpuMemoryBytes / (1024.0 * 1024.0));
		}
		else
		{
			LOG_ERROR("FAILED: Some meshes still have CPU data");
		}
	}

	LOG_INFO("");
}

int main(int argc, char* argv[])
{
	LOG_INFO("========================================");
	LOG_INFO("    Scene Management Test");
	LOG_INFO("========================================");
	LOG_INFO("");

	rhi::DeviceHandle device = rhi::CreateRHIDevice();
	if (!device)
	{
		LOG_ERROR("Failed to create RHI device");
		return 1;
	}

	std::filesystem::path plyPath;

	if (argc >= 2)
	{
		plyPath = argv[1];
		LOG_INFO("Using specified PLY file: {}", plyPath.string());
	}
	else
	{
		std::filesystem::path exePath = std::filesystem::path(argv[0]).parent_path();
		plyPath = exePath / "flowers_1.ply";

		LOG_INFO("No PLY file specified. Using default: flowers_1.ply");
		LOG_INFO("Looking for file at: {}", plyPath.string());
		LOG_INFO("Usage: {} <path_to_ply_file>", argv[0]);
	}
	LOG_INFO("");

	if (!std::filesystem::exists(plyPath))
	{
		LOG_ERROR("PLY file does not exist: {}", plyPath.string());
		LOG_INFO("");
		LOG_INFO("Please provide a valid PLY file path");
		return 1;
	}

	try
	{
		TestSceneLoading(device.Get(), plyPath);
		TestMultipleMeshes(device.Get(), plyPath);
		TestGpuUpload(device.Get(), plyPath);
		TestMemoryRelease(device.Get(), plyPath);

		LOG_INFO("========================================");
		LOG_INFO("    All tests completed successfully!");
		LOG_INFO("========================================");
		return 0;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("");
		LOG_ERROR("========================================");
		LOG_ERROR("    Error: {}", e.what());
		LOG_ERROR("========================================");
		return 1;
	}
}