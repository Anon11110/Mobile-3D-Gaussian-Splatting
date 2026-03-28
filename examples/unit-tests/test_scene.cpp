#include "test_framework.h"
#include "test_utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <msplat/core/log.h>
#include <msplat/core/math/matrix.h>
#include <msplat/core/timer.h>
#include <msplat/engine/scene/scene.h>
#include <msplat/engine/splat/splat_loader.h>
#include <rhi/rhi.h>
#include <thread>
#include <vector>

TEST(scene_loading)
{
	auto plyPath = FindTestPly();
	if (plyPath.empty())
	{
		LOG_WARNING("Skipping scene_loading: no PLY file found");
		return true;
	}

	rhi::DeviceHandle device = rhi::CreateRHIDevice();
	if (!device)
		return false;

	msplat::engine::Scene       scene(device.Get());
	msplat::engine::SplatLoader loader;

	auto future    = loader.Load(plyPath);
	auto splatData = future.get();

	if (!splatData || splatData->empty())
	{
		LOG_ERROR("Failed to load splat data from {}", plyPath.string());
		return false;
	}

	LOG_INFO("Loaded {} splats", splatData->numSplats);

	auto meshId = scene.AddMesh(splatData, msplat::math::Identity());
	if (scene.GetTotalSplatCount() != splatData->numSplats)
	{
		LOG_ERROR("Splat count mismatch: scene={}, expected={}", scene.GetTotalSplatCount(), splatData->numSplats);
		return false;
	}

	LOG_INFO("Added mesh ID {} with {} splats", meshId, scene.GetTotalSplatCount());
	return true;
}

TEST(scene_multiple_meshes)
{
	auto plyPath = FindTestPly();
	if (plyPath.empty())
	{
		LOG_WARNING("Skipping scene_multiple_meshes: no PLY file found");
		return true;
	}

	rhi::DeviceHandle device = rhi::CreateRHIDevice();
	if (!device)
		return false;

	msplat::engine::Scene       scene(device.Get());
	msplat::engine::SplatLoader loader;

	auto future        = loader.Load(plyPath);
	auto baseSplatData = future.get();

	if (!baseSplatData || baseSplatData->empty())
		return false;

	const int numCopies = 3;
	for (int i = 0; i < numCopies; ++i)
	{
		float offset    = static_cast<float>(i) * 2.0f;
		auto  transform = msplat::math::Translate(msplat::math::vec3(offset, 0, 0));
		scene.AddMesh(baseSplatData, transform);
	}

	uint32_t expectedTotal = baseSplatData->numSplats * numCopies;
	if (scene.GetTotalSplatCount() != expectedTotal)
	{
		LOG_ERROR("Splat count mismatch: scene={}, expected={}", scene.GetTotalSplatCount(), expectedTotal);
		return false;
	}

	LOG_INFO("{} meshes, {} total splats", numCopies, scene.GetTotalSplatCount());
	return true;
}

TEST(scene_gpu_upload)
{
	auto plyPath = FindTestPly();
	if (plyPath.empty())
	{
		LOG_WARNING("Skipping scene_gpu_upload: no PLY file found");
		return true;
	}

	rhi::DeviceHandle device = rhi::CreateRHIDevice();
	if (!device)
		return false;

	msplat::engine::Scene       scene(device.Get());
	msplat::engine::SplatLoader loader;

	auto future    = loader.Load(plyPath);
	auto splatData = future.get();

	if (!splatData || splatData->empty())
		return false;

	scene.AddMesh(splatData, msplat::math::Identity());
	scene.AllocateGpuBuffers();

	msplat::timer::Timer uploadTimer;
	uploadTimer.start();

	auto uploadFence = scene.UploadAttributeData();
	if (!uploadFence)
	{
		LOG_ERROR("Failed to create upload fence");
		return false;
	}

	uploadFence->Wait();
	uploadTimer.stop();

	LOG_INFO("GPU upload completed in {:.2f} ms", uploadTimer.elapsedMilliseconds());

	const auto &gpuData = scene.GetGpuData();
	if (!gpuData.positions || gpuData.positions->GetSize() == 0)
	{
		LOG_ERROR("GPU position buffer is empty");
		return false;
	}

	device->RetireCompletedFrame();
	return true;
}

TEST(scene_memory_release)
{
	auto plyPath = FindTestPly();
	if (plyPath.empty())
	{
		LOG_WARNING("Skipping scene_memory_release: no PLY file found");
		return true;
	}

	rhi::DeviceHandle device = rhi::CreateRHIDevice();
	if (!device)
		return false;

	msplat::engine::Scene       scene(device.Get());
	msplat::engine::SplatLoader loader;

	auto future    = loader.Load(plyPath);
	auto splatData = future.get();

	if (!splatData || splatData->empty())
		return false;

	scene.AddMesh(splatData, msplat::math::Identity());
	scene.AllocateGpuBuffers();

	auto uploadFence = scene.UploadAttributeData();
	if (!uploadFence)
		return false;

	uploadFence->Wait();
	device->RetireCompletedFrame();

	// Release CPU memory after upload
	scene.ForEachMesh([](msplat::engine::SplatMesh &mesh) {
		if (mesh.HasCpuData())
			mesh.ReleaseCpuData();
	});

	// Verify CPU data is released
	bool allReleased = true;
	scene.ForEachMesh([&allReleased](const msplat::engine::SplatMesh &mesh) {
		if (mesh.HasCpuData())
			allReleased = false;
	});

	if (!allReleased)
		LOG_ERROR("Some meshes still have CPU data after release");

	return allReleased;
}
