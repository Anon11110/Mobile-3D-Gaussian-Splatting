#include "rhi/rhi.h"
#include "test_framework.h"
#include <chrono>
#include <msplat/core/log.h>
#include <thread>
#include <vector>

struct Vertex
{
	float position[3];
	float normal[3];
	float texCoord[2];
};

TEST(async_mesh_upload)
{
	auto device = rhi::CreateRHIDevice();

	std::vector<Vertex> vertices(100000);
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		vertices[i].position[0] = static_cast<float>(i);
		vertices[i].position[1] = static_cast<float>(i * 2);
		vertices[i].position[2] = static_cast<float>(i * 3);
	}

	rhi::BufferDesc vbDesc = {
	    .size          = vertices.size() * sizeof(Vertex),
	    .usage         = rhi::BufferUsage::VERTEX | rhi::BufferUsage::TRANSFER_DST,
	    .resourceUsage = rhi::ResourceUsage::Static,
	    .hints         = {.prefer_device_local = true}};
	auto vertexBuffer = device->CreateBuffer(vbDesc);

	auto start       = std::chrono::high_resolution_clock::now();
	auto uploadFence = device->UploadBufferAsync(
	    vertexBuffer.get(),
	    vertices.data(),
	    vbDesc.size);
	auto submitTime = std::chrono::high_resolution_clock::now();

	auto submitDuration = std::chrono::duration<double, std::milli>(submitTime - start).count();
	if (submitDuration >= 50.0)
	{
		LOG_ERROR("Submit took too long: {} ms", submitDuration);
		return false;
	}

	// Do some work while upload happens
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	// Wait for completion
	uploadFence->Wait(UINT64_MAX);

	auto end           = std::chrono::high_resolution_clock::now();
	auto totalDuration = std::chrono::duration<double, std::milli>(end - start).count();

	if (!uploadFence->IsSignaled())
	{
		LOG_ERROR("Fence not signaled after wait");
		return false;
	}

	LOG_INFO("Upload completed in {:.2f} ms (submit: {:.2f} ms)", totalDuration, submitDuration);
	return true;
}

TEST(streaming_data_upload)
{
	auto device = rhi::CreateRHIDevice();

	const size_t chunkSize = 1024 * 1024;
	const size_t numChunks = 10;

	rhi::BufferDesc bufferDesc = {
	    .size          = chunkSize * numChunks,
	    .usage         = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST,
	    .resourceUsage = rhi::ResourceUsage::Static};
	auto storageBuffer = device->CreateBuffer(bufferDesc);

	std::vector<std::shared_ptr<rhi::IRHIFence>> uploadFences;

	// Stream chunks asynchronously
	for (size_t i = 0; i < numChunks; ++i)
	{
		std::vector<uint8_t> chunkData(chunkSize, static_cast<uint8_t>(i));

		auto fence = device->UploadBufferAsync(
		    storageBuffer.get(),
		    chunkData.data(),
		    chunkSize,
		    i * chunkSize);

		uploadFences.push_back(std::move(fence));
	}

	// Wait for all uploads
	size_t    completedCount = 0;
	const int maxIterations  = 1000;
	int       iterations     = 0;

	while (completedCount < numChunks && iterations < maxIterations)
	{
		completedCount = 0;
		for (const auto &fence : uploadFences)
		{
			if (fence->IsSignaled())
			{
				completedCount++;
			}
		}

		if (completedCount < numChunks)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			iterations++;
		}
	}

	if (completedCount != numChunks)
	{
		LOG_ERROR("Only {} of {} chunks completed", completedCount, numChunks);
		return false;
	}

	LOG_INFO("Streamed {} chunks successfully", numChunks);
	return true;
}

TEST(update_buffer_validation)
{
	auto device = rhi::CreateRHIDevice();

	// Test 1: UpdateBuffer should work on mappable buffers
	rhi::BufferDesc uniformDesc = {
	    .size          = 256,
	    .usage         = rhi::BufferUsage::UNIFORM,
	    .resourceUsage = rhi::ResourceUsage::DynamicUpload,
	    .hints         = {.persistently_mapped = true}};
	auto uniformBuffer = device->CreateBuffer(uniformDesc);

	float mvpMatrix[16]   = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	bool  updateSucceeded = false;
	try
	{
		device->UpdateBuffer(uniformBuffer.get(), mvpMatrix, sizeof(mvpMatrix));
		updateSucceeded = true;
	}
	catch (...)
	{
		updateSucceeded = false;
	}

	if (!updateSucceeded)
	{
		LOG_ERROR("UpdateBuffer failed on mappable buffer");
		return false;
	}
	LOG_INFO("UpdateBuffer succeeded on mappable buffer");

	// Test 2: UpdateBuffer should fail on device-local buffers
	std::vector<float> largeVertexData(100000);
	rhi::BufferDesc    staticDesc = {
	       .size          = largeVertexData.size() * sizeof(float),
	       .usage         = rhi::BufferUsage::VERTEX | rhi::BufferUsage::TRANSFER_DST,
	       .resourceUsage = rhi::ResourceUsage::Static};
	auto staticBuffer = device->CreateBuffer(staticDesc);

	bool exceptionThrown = false;
	try
	{
		device->UpdateBuffer(staticBuffer.get(), largeVertexData.data(), 256);
	}
	catch (const std::logic_error &e)
	{
		exceptionThrown = true;
		LOG_INFO("Expected error caught: {}", e.what());
	}

	if (!exceptionThrown)
	{
		LOG_ERROR("UpdateBuffer should have thrown on device-local buffer");
		return false;
	}

	// Use UploadBufferAsync for device-local buffer (should succeed)
	auto fence = device->UploadBufferAsync(
	    staticBuffer.get(),
	    largeVertexData.data(),
	    staticDesc.size);
	fence->Wait(UINT64_MAX);

	if (!fence->IsSignaled())
	{
		LOG_ERROR("UploadBufferAsync failed for device-local buffer");
		return false;
	}
	LOG_INFO("UploadBufferAsync succeeded for device-local buffer");

	return true;
}

TEST(shared_fence_ownership)
{
	auto device = rhi::CreateRHIDevice();

	std::vector<uint8_t> data(1024 * 1024, 0x42);
	rhi::BufferDesc      bufferDesc = {
	         .size          = data.size(),
	         .usage         = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST,
	         .resourceUsage = rhi::ResourceUsage::Static};
	auto buffer = device->CreateBuffer(bufferDesc);

	// Test that fence remains valid after copies
	std::shared_ptr<rhi::IRHIFence> fence1, fence2;

	{
		auto fence = device->UploadBufferAsync(
		    buffer.get(),
		    data.data(),
		    data.size());

		fence1 = fence;        // Copy 1
		fence2 = fence;        // Copy 2

		// Original fence goes out of scope here
	}

	// Both copies should still be valid
	if (fence1 == nullptr || fence2 == nullptr)
	{
		LOG_ERROR("Fence copies became invalid");
		return false;
	}

	// Wait on one copy
	fence1->Wait(UINT64_MAX);

	// Both should report signaled
	if (!fence1->IsSignaled() || !fence2->IsSignaled())
	{
		LOG_ERROR("Fences not signaled after wait");
		return false;
	}

	LOG_INFO("Shared fence ownership working correctly");
	return true;
}

// Tests are automatically registered via TEST macro