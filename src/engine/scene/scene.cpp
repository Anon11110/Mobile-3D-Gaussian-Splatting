#include <msplat/core/log.h>
#include <msplat/core/math/math.h>
#include <msplat/engine/scene/scene.h>
#include <msplat/engine/sorting/cpu_splat_sorter.h>
#include <msplat/engine/splat/splat_math.h>

namespace msplat::engine
{

Scene::Scene(rhi::IRHIDevice *device) :
    device(device)
{
}

SplatMesh::ID Scene::AddMesh(container::shared_ptr<SplatSoA> splatData,
                             const math::mat4               &initialTransform)
{
	if (!splatData || splatData->empty())
	{
		LOG_ERROR("Cannot add mesh with empty splat data");
		return SplatMesh::ID(-1);
	}

	std::lock_guard<std::mutex> lock(meshesMutex);

	const bool needsReallocation = attributeDataUploaded;

	SplatMesh::ID meshId = nextMeshId++;
	meshes.emplace_back(meshId, std::move(splatData), initialTransform);

	totalSplatCount += meshes.back().GetSplatData()->numSplats;

	LOG_INFO("Added mesh {} with {} splats. Total: {} splats",
	         meshId, meshes.back().GetSplatData()->numSplats, totalSplatCount);

	// If GPU data was already uploaded, reallocate and re-upload everything
	if (needsReallocation)
	{
		LOG_INFO("Dynamic mesh addition, triggering ReallocateAndUpload()");

		gpuBuffersAllocated   = false;
		attributeDataUploaded = false;

		// Wait for GPU, release old buffers, reallocate, and re-upload
		device->WaitIdle();

		gpuData.positions     = nullptr;
		gpuData.covariances3D = nullptr;
		gpuData.colors        = nullptr;
		gpuData.shRest        = nullptr;
		gpuData.sortedIndices = nullptr;
		meshGpuRanges.clear();

		cpuSplatSorter.reset();
		splatPositions.clear();
		lastSortedIndices.clear();

		AllocateGpuBuffersInternal();
		rhi::FenceHandle fence = UploadAttributeDataInternal();
		if (fence)
		{
			fence->Wait(UINT64_MAX);
		}

		// Invoke callback to notify app of buffer changes
		if (bufferChangeCallback)
		{
			bufferChangeCallback(gpuData, totalSplatCount);
		}
	}

	return meshId;
}

bool Scene::RemoveMesh(SplatMesh::ID id)
{
	std::lock_guard<std::mutex> lock(meshesMutex);

	auto it = std::find_if(meshes.begin(), meshes.end(),
	                       [id](const SplatMesh &mesh) { return mesh.GetId() == id; });

	if (it == meshes.end())
	{
		LOG_WARNING("Mesh {} not found for removal", id);
		return false;
	}

	const bool needsReallocation = attributeDataUploaded;

	if (it->HasCpuData())
	{
		totalSplatCount -= it->GetSplatData()->numSplats;
	}

	// Remove mesh using swap-and-pop
	if (it != meshes.end() - 1)
	{
		std::swap(*it, meshes.back());
	}
	meshes.pop_back();

	LOG_INFO("Removed mesh {}. Total: {} splats", id, totalSplatCount);

	// If GPU data was already uploaded, reallocate and re-upload everything
	if (needsReallocation)
	{
		LOG_INFO("Dynamic mesh removal - triggering ReallocateAndUpload()");

		gpuBuffersAllocated   = false;
		attributeDataUploaded = false;

		// Wait for GPU, release old buffers, reallocate, and re-upload
		device->WaitIdle();

		gpuData.positions     = nullptr;
		gpuData.covariances3D = nullptr;
		gpuData.colors        = nullptr;
		gpuData.shRest        = nullptr;
		gpuData.sortedIndices = nullptr;
		meshGpuRanges.clear();

		cpuSplatSorter.reset();
		splatPositions.clear();
		lastSortedIndices.clear();

		if (totalSplatCount > 0)
		{
			AllocateGpuBuffersInternal();
			rhi::FenceHandle fence = UploadAttributeDataInternal();
			if (fence)
			{
				fence->Wait(UINT64_MAX);
			}
		}

		// Invoke callback to notify app of buffer changes
		if (bufferChangeCallback)
		{
			bufferChangeCallback(gpuData, totalSplatCount);
		}
	}

	return true;
}

rhi::FenceHandle Scene::UploadAttributeData()
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	return UploadAttributeDataInternal();
}

rhi::FenceHandle Scene::UploadAttributeDataInternal()
{
	if (attributeDataUploaded)
	{
		LOG_WARNING("Attribute data has already been uploaded");
		return nullptr;
	}

	if (meshes.empty() || totalSplatCount == 0)
	{
		LOG_WARNING("No splat data to upload");
		return nullptr;
	}

	uint32_t maxShCoeffsPerSplat = CalculateMaxShCoeffsPerSplat();
	uint32_t totalShCoeffs       = 0;

	for (const auto &mesh : meshes)
	{
		if (mesh.HasCpuData())
		{
			auto splatData = mesh.GetSplatData();
			totalShCoeffs += splatData->numSplats * splatData->shCoeffsPerSplat;
		}
	}

	if (!gpuBuffersAllocated)
	{
		LOG_ERROR("GPU buffers not allocated. Call AllocateGpuBuffers() before UploadAttributeData()");
		return nullptr;
	}

	container::vector<float> positions;
	container::vector<float> covariances;
	container::vector<float> colors;
	container::vector<float> shRest;

	positions.reserve(totalSplatCount * 4);
	covariances.reserve(totalSplatCount * 8);        // 6 floats + 2 padding = 8 floats per splat
	colors.reserve(totalSplatCount * 4);
	shRest.reserve(totalShCoeffs);

	meshGpuRanges.clear();
	uint32_t currentSplatOffset = 0;

	// Consolidate all mesh data
	for (const auto &mesh : meshes)
	{
		auto splatData = mesh.GetSplatData();
		if (!splatData || splatData->empty())
			continue;

		uint32_t count = splatData->numSplats;

		// Record the GPU range for this mesh
		MeshGpuRange range;
		range.startIndex            = currentSplatOffset;
		range.splatCount            = count;
		range.transform             = mesh.GetModelMatrix();
		meshGpuRanges[mesh.GetId()] = range;

		// Pack attributes in SoA format
		for (uint32_t i = 0; i < count; ++i)
		{
			// Position
			positions.push_back(splatData->posX[i]);
			positions.push_back(splatData->posY[i]);
			positions.push_back(splatData->posZ[i]);
			positions.push_back(0.0f);        // padding

			// Compute 3D covariance from scale and rotation
			math::vec3 log_scale(splatData->scaleX[i], splatData->scaleY[i], splatData->scaleZ[i]);
			math::vec3 scale = TransformScale(log_scale);

			// Quaternion components: PLY stores as (w, x, y, z) in rot_0, rot_1, rot_2, rot_3
			// Our vec4 expects (x, y, z, w) so we need to remap
			math::vec4 rotation(
			    splatData->rotY[i],         // rot_1 (x) → .x
			    splatData->rotZ[i],         // rot_2 (y) → .y
			    splatData->rotW[i],         // rot_3 (z) → .z
			    splatData->rotX[i]);        // rot_0 (w) → .w
			rotation = math::Normalize(rotation);

			float cov[6];
			ComputeCovariance3D(scale, rotation, cov);

			// Pack as 2 vec3 (6 floats + 2 padding)
			covariances.push_back(cov[0]);        // M11
			covariances.push_back(cov[1]);        // M12
			covariances.push_back(cov[2]);        // M13
			covariances.push_back(0.0f);          // padding
			covariances.push_back(cov[3]);        // M22
			covariances.push_back(cov[4]);        // M23
			covariances.push_back(cov[5]);        // M33
			covariances.push_back(0.0f);          // padding

			// Color: convert SH degree 0 and opacity
			math::vec3 rgb   = ComputeSHDegree0Color(splatData->fDc0[i], splatData->fDc1[i], splatData->fDc2[i]);
			float      alpha = TransformOpacity(splatData->opacity[i]);

			colors.push_back(rgb.x);
			colors.push_back(rgb.y);
			colors.push_back(rgb.z);
			colors.push_back(alpha);
		}

		// SH coefficients
		for (const auto &coeff : splatData->fRest)
		{
			shRest.push_back(coeff);
		}

		currentSplatOffset += count;
	}

	std::vector<rhi::FenceHandle> uploadFences;

	// Upload SoA buffers
	rhi::FenceHandle positionsFence = device->UploadBufferAsync(
	    gpuData.positions.Get(),
	    positions.data(),
	    positions.size() * sizeof(float));
	if (positionsFence)
	{
		uploadFences.push_back(positionsFence);
	}

	rhi::FenceHandle covariancesFence = device->UploadBufferAsync(
	    gpuData.covariances3D.Get(),
	    covariances.data(),
	    covariances.size() * sizeof(float));
	if (covariancesFence)
	{
		uploadFences.push_back(covariancesFence);
	}

	rhi::FenceHandle colorsFence = device->UploadBufferAsync(
	    gpuData.colors.Get(),
	    colors.data(),
	    colors.size() * sizeof(float));
	if (colorsFence)
	{
		uploadFences.push_back(colorsFence);
	}

	if (!shRest.empty())
	{
		rhi::FenceHandle shFence = device->UploadBufferAsync(
		    gpuData.shRest.Get(),
		    shRest.data(),
		    shRest.size() * sizeof(float));
		if (shFence)
		{
			uploadFences.push_back(shFence);
		}
	}

	attributeDataUploaded = true;
	gpuBuffersAllocated   = true;

	LOG_INFO("Uploaded {} splats to GPU ({} meshes)", totalSplatCount, meshGpuRanges.size());

	if (!uploadFences.empty())
	{
		return device->CreateCompositeFence(uploadFences);
	}

	return nullptr;
}

const Scene::GpuData &Scene::GetGpuData() const
{
	return gpuData;
}

rhi::FenceHandle Scene::ReallocateAndUpload()
{
	std::lock_guard<std::mutex> lock(meshesMutex);

	// Wait for GPU, release old buffers, reallocate, and re-upload
	device->WaitIdle();

	gpuBuffersAllocated   = false;
	attributeDataUploaded = false;

	gpuData.positions     = nullptr;
	gpuData.covariances3D = nullptr;
	gpuData.colors        = nullptr;
	gpuData.shRest        = nullptr;
	gpuData.sortedIndices = nullptr;

	meshGpuRanges.clear();

	cpuSplatSorter.reset();
	splatPositions.clear();
	lastSortedIndices.clear();

	rhi::FenceHandle fence = nullptr;
	if (totalSplatCount > 0)
	{
		AllocateGpuBuffersInternal();
		fence = UploadAttributeDataInternal();
	}

	// Invoke callback to notify app of buffer changes
	if (bufferChangeCallback)
	{
		bufferChangeCallback(gpuData, totalSplatCount);
	}

	LOG_INFO("ReallocateAndUpload complete: {} splats", totalSplatCount);
	return fence;
}

void Scene::SetBufferChangeCallback(BufferChangeCallback callback)
{
	bufferChangeCallback = std::move(callback);
}

SplatMesh::ID Scene::GetMeshIDFromSplatIndex(uint32_t globalIndex) const
{
	std::lock_guard<std::mutex> lock(meshesMutex);

	for (const auto &[meshId, range] : meshGpuRanges)
	{
		if (globalIndex >= range.startIndex && globalIndex < range.startIndex + range.splatCount)
		{
			return meshId;
		}
	}

	return SplatMesh::ID(-1);        // Not found
}

const Scene::MeshGpuRange *Scene::GetMeshGpuRange(SplatMesh::ID id) const
{
	std::lock_guard<std::mutex> lock(meshesMutex);

	auto it = meshGpuRanges.find(id);
	if (it != meshGpuRanges.end())
	{
		return &it->second;
	}
	return nullptr;
}

uint32_t Scene::GetTotalSplatCount() const
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	return totalSplatCount;
}

void Scene::AllocateGpuBuffers()
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	AllocateGpuBuffersInternal();
}

void Scene::AllocateGpuBuffersInternal()
{
	if (gpuBuffersAllocated)
	{
		LOG_WARNING("GPU buffers already allocated");
		return;
	}

	if (totalSplatCount == 0)
	{
		LOG_WARNING("No splats to allocate buffers for");
		return;
	}

	uint32_t maxShCoeffsPerSplat = CalculateMaxShCoeffsPerSplat();

	using namespace rhi;

	// Splat attribute buffers
	{
		// Positions buffer
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 4 * sizeof(float);
		gpuData.positions  = device->CreateBuffer(desc);
	}

	{
		// Covariances3D buffer (6 floats per splat, packed as 2 vec3)
		BufferDesc desc{};
		desc.usage            = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage    = ResourceUsage::Static;
		desc.size             = totalSplatCount * 8 * sizeof(float);        // 2 vec3 (padded) = 8 floats
		gpuData.covariances3D = device->CreateBuffer(desc);
	}

	{
		// Colors buffer
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 4 * sizeof(float);
		gpuData.colors     = device->CreateBuffer(desc);
	}

	// Spherical harmonics coefficients buffer
	// For standard 3DGS models with SH degree 3, we need exactly 45 coefficients per splat
	{
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;

		if (maxShCoeffsPerSplat > 0)
		{
			const uint32_t shCoeffsPerSplat = 45;
			desc.size                       = totalSplatCount * shCoeffsPerSplat * sizeof(float);
			gpuData.shRest                  = device->CreateBuffer(desc);
			LOG_INFO("Allocated SH buffer for {} splats with {} coeffs each ({} MB)",
			         totalSplatCount, shCoeffsPerSplat,
			         (desc.size / 1024.0f / 1024.0f));
		}
		else
		{
			// Create minimal placeholder buffer when no SH data exists
			desc.size      = 16;
			gpuData.shRest = device->CreateBuffer(desc);
			LOG_INFO("Allocated minimal SH placeholder buffer (no SH data)");
		}
	}

	// Sorted indices buffer for rendering order
	{
		BufferDesc desc{};
		desc.usage            = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage    = ResourceUsage::DynamicUpload;
		desc.size             = totalSplatCount * sizeof(uint32_t);
		gpuData.sortedIndices = device->CreateBuffer(desc);
	}

	// Initialize CPU-side sorting system
	cpuSplatSorter = container::make_unique<CpuSplatSorter>(totalSplatCount);
	splatPositions.reserve(totalSplatCount);

	gpuBuffersAllocated = true;
	LOG_INFO("Allocated GPU buffers for {} splats (CPU-sort pipeline mode)", totalSplatCount);
}

bool Scene::IsAttributeDataUploaded() const
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	return attributeDataUploaded;
}

void Scene::UpdateView(const math::mat4 &view_matrix)
{
	if (!cpuSplatSorter)
	{
		LOG_WARNING("CpuSplatSorter not initialized. Call AllocateGpuBuffers() first.");
		return;
	}

	UpdateSplatPositions();
	cpuSplatSorter->RequestSort(splatPositions, view_matrix);
}

rhi::FenceHandle Scene::ConsumeAndUploadSortedIndices()
{
	if (!cpuSplatSorter)
	{
		LOG_WARNING("CpuSplatSorter not initialized. Call AllocateGpuBuffers() first.");
		return nullptr;
	}

	if (!cpuSplatSorter->IsSortComplete())
	{
		// No new sorted data available yet
		return nullptr;
	}

	auto sortedIndices = cpuSplatSorter->GetSortedIndices();
	if (sortedIndices.empty())
	{
		return nullptr;
	}

	if (!gpuData.sortedIndices)
	{
		LOG_ERROR("Sorted indices GPU buffer not allocated");
		return nullptr;
	}

	// Upload the sorted indices to the GPU
	rhi::FenceHandle fence = device->UploadBufferAsync(
	    gpuData.sortedIndices.Get(),
	    sortedIndices.data(),
	    sortedIndices.size() * sizeof(uint32_t));

	return fence;
}

uint32_t Scene::CalculateMaxShCoeffsPerSplat() const
{
	uint32_t maxShCoeffsPerSplat = 0;
	for (const auto &mesh : meshes)
	{
		if (mesh.HasCpuData())
		{
			auto splatData      = mesh.GetSplatData();
			maxShCoeffsPerSplat = math::Max(maxShCoeffsPerSplat, splatData->shCoeffsPerSplat);
		}
	}
	return maxShCoeffsPerSplat;
}

void Scene::UpdateSplatPositions()
{
	std::lock_guard<std::mutex> lock(meshesMutex);

	splatPositions.clear();
	splatPositions.reserve(totalSplatCount);

	for (const auto &mesh : meshes)
	{
		if (!mesh.HasCpuData())
			continue;

		auto           splatData   = mesh.GetSplatData();
		const auto    &modelMatrix = mesh.GetModelMatrix();
		const uint32_t count       = splatData->numSplats;

		for (uint32_t i = 0; i < count; ++i)
		{
			math::vec3 localPos(splatData->posX[i], splatData->posY[i], splatData->posZ[i]);
			math::vec4 worldPos = modelMatrix * math::vec4(localPos, 1.0f);
			splatPositions.push_back(math::vec3(worldPos));
		}
	}
}

bool Scene::IsCpuSortComplete() const
{
	if (!cpuSplatSorter)
	{
		return true;        // No sorter = always "complete"
	}
	return cpuSplatSorter->IsSortComplete();
}

container::span<const uint32_t> Scene::GetCpuSortedIndices()
{
	if (!cpuSplatSorter)
	{
		return {};
	}
	auto indices = cpuSplatSorter->GetSortedIndices();
	if (!indices.empty())
	{
		// Store a copy for verification
		lastSortedIndices.assign(indices.begin(), indices.end());
	}
	return indices;
}

bool Scene::VerifyCpuSortOrder(const math::mat4 &viewMatrix) const
{
	LOG_INFO("=== CPU Sort Order Verification ===");

	if (splatPositions.empty())
	{
		LOG_WARNING("Cannot verify CPU sort - no positions");
		return false;
	}

	if (lastSortedIndices.empty())
	{
		LOG_WARNING("Cannot verify CPU sort - no sorted indices available (sort may not have completed yet)");
		return false;
	}

	const auto &sortedIndices = lastSortedIndices;

	LOG_INFO("Checking if {} depths are sorted in ascending order...", sortedIndices.size());

	// Compute depths and verify they are non-decreasing
	float prevDepth      = -std::numeric_limits<float>::max();
	bool  allCorrect     = true;
	int   errorCount     = 0;
	int   firstErrorPos  = -1;
	float firstErrorPrev = 0.0f;
	float firstErrorCurr = 0.0f;

	for (size_t i = 0; i < sortedIndices.size(); ++i)
	{
		uint32_t idx = sortedIndices[i];
		if (idx >= splatPositions.size())
		{
			LOG_ERROR("Invalid index {} at position {} (max: {})", idx, i, splatPositions.size() - 1);
			return false;
		}

		float depth = ComputeViewSpaceDepth(splatPositions[idx], viewMatrix);

		if (depth < prevDepth)
		{
			if (firstErrorPos < 0)
			{
				firstErrorPos  = static_cast<int>(i);
				firstErrorPrev = prevDepth;
				firstErrorCurr = depth;
			}
			errorCount++;
			allCorrect = false;
		}
		prevDepth = depth;
	}

	if (allCorrect)
	{
		LOG_INFO("CPU sort verification PASSED - {} indices in correct order", sortedIndices.size());
	}
	else
	{
		LOG_ERROR("  Out of order at position {}: depth[{}]={:.6f} > depth[{}]={:.6f}",
		          firstErrorPos, firstErrorPos - 1, firstErrorPrev, firstErrorPos, firstErrorCurr);
		LOG_ERROR("CPU sort verification FAILED - {} total errors", errorCount);
	}

	return allCorrect;
}

Scene::CpuMemoryInfo Scene::GetCpuMemoryInfo() const
{
	std::lock_guard<std::mutex> lock(meshesMutex);

	CpuMemoryInfo info{};

	// Sum up SplatSoA memory from all meshes
	for (const auto &mesh : meshes)
	{
		if (mesh.HasCpuData())
		{
			info.splatDataBytes += mesh.GetSplatData()->GetCpuMemoryUsage();
		}
	}

	// splatPositions vector
	info.splatPositionsBytes = splatPositions.capacity() * sizeof(math::vec3);

	// lastSortedIndices vector
	info.sortedIndicesBytes = lastSortedIndices.capacity() * sizeof(uint32_t);

	// CpuSplatSorter buffers
	if (cpuSplatSorter)
	{
		info.cpuSorterBytes = cpuSplatSorter->GetMemoryUsage();
	}

	return info;
}

}        // namespace msplat::engine