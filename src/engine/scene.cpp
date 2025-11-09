#include <msplat/core/log.h>
#include <msplat/core/math/math.h>
#include <msplat/engine/cpu_splat_sorter.h>
#include <msplat/engine/scene.h>

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

	// Allow adding meshes as long as attribute data hasn't been uploaded
	if (attributeDataUploaded)
	{
		LOG_ERROR("Cannot add mesh after attribute data has been uploaded");
		return SplatMesh::ID(-1);
	}

	SplatMesh::ID meshId = nextMeshId++;
	meshes.emplace_back(meshId, std::move(splatData), initialTransform);

	totalSplatCount += meshes.back().GetSplatData()->numSplats;

	LOG_INFO("Added mesh {} with {} splats. Total: {} splats",
	         meshId, meshes.back().GetSplatData()->numSplats, totalSplatCount);

	return meshId;
}

bool Scene::RemoveMesh(SplatMesh::ID id)
{
	std::lock_guard<std::mutex> lock(meshesMutex);

	if (attributeDataUploaded)
	{
		LOG_ERROR("Cannot remove mesh after attribute data has been uploaded");
		return false;
	}

	auto it = std::find_if(meshes.begin(), meshes.end(),
	                       [id](const SplatMesh &mesh) { return mesh.GetId() == id; });

	if (it != meshes.end())
	{
		if (it->HasCpuData())
		{
			totalSplatCount -= it->GetSplatData()->numSplats;
		}

		if (it != meshes.end() - 1)
		{
			std::swap(*it, meshes.back());
		}
		meshes.pop_back();

		LOG_INFO("Removed mesh {}. Total: {} splats", id, totalSplatCount);
		return true;
	}

	return false;
}

rhi::FenceHandle Scene::UploadAttributeData()
{
	std::lock_guard<std::mutex> lock(meshesMutex);

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
	container::vector<float> scales;
	container::vector<float> rotations;
	container::vector<float> colors;
	container::vector<float> shRest;

	positions.reserve(totalSplatCount * 4);
	scales.reserve(totalSplatCount * 4);
	rotations.reserve(totalSplatCount * 4);
	colors.reserve(totalSplatCount * 4);
	shRest.reserve(totalShCoeffs);

	// Spherical harmonics constant for degree 0
	constexpr float shC0 = 0.28209479177387814f;

	// Consolidate all mesh data
	for (const auto &mesh : meshes)
	{
		auto splatData = mesh.GetSplatData();
		if (!splatData || splatData->empty())
			continue;

		uint32_t count = splatData->numSplats;

		// Pack attributes in SoA format
		for (uint32_t i = 0; i < count; ++i)
		{
			// Position
			positions.push_back(splatData->posX[i]);
			positions.push_back(splatData->posY[i]);
			positions.push_back(splatData->posZ[i]);
			positions.push_back(0.0f);        // padding

			// Scale - convert from log space
			float scaleX = math::Exp(splatData->scaleX[i]);
			float scaleY = math::Exp(splatData->scaleY[i]);
			float scaleZ = math::Exp(splatData->scaleZ[i]);
			scales.push_back(scaleX);
			scales.push_back(scaleY);
			scales.push_back(scaleZ);
			scales.push_back(0.0f);        // padding

			// Rotation
			rotations.push_back(splatData->rotX[i]);
			rotations.push_back(splatData->rotY[i]);
			rotations.push_back(splatData->rotZ[i]);
			rotations.push_back(splatData->rotW[i]);

			// Color - convert SH degree 0 and opacity
			float r = math::Clamp(shC0 * splatData->fDc0[i] + 0.5f, 0.0f, 1.0f);
			float g = math::Clamp(shC0 * splatData->fDc1[i] + 0.5f, 0.0f, 1.0f);
			float b = math::Clamp(shC0 * splatData->fDc2[i] + 0.5f, 0.0f, 1.0f);

			// Opacity is stored as a logit (pre-sigmoid), convert to alpha [0,1] with sigmoid
			float opacityLogit = splatData->opacity[i];
			float alpha        = math::Clamp(1.0f / (1.0f + math::Exp(-opacityLogit)), 0.0f, 1.0f);

			colors.push_back(r);
			colors.push_back(g);
			colors.push_back(b);
			colors.push_back(alpha);
		}

		// SH coefficients
		for (const auto &coeff : splatData->fRest)
		{
			shRest.push_back(coeff);
		}
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

	rhi::FenceHandle scalesFence = device->UploadBufferAsync(
	    gpuData.scales.Get(),
	    scales.data(),
	    scales.size() * sizeof(float));
	if (scalesFence)
	{
		uploadFences.push_back(scalesFence);
	}

	rhi::FenceHandle rotationsFence = device->UploadBufferAsync(
	    gpuData.rotations.Get(),
	    rotations.data(),
	    rotations.size() * sizeof(float));
	if (rotationsFence)
	{
		uploadFences.push_back(rotationsFence);
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

	LOG_INFO("Uploaded {} splats to GPU", totalSplatCount);

	// Create and return a composite fence that waits for all uploads
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

uint32_t Scene::GetTotalSplatCount() const
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	return totalSplatCount;
}

void Scene::AllocateGpuBuffers()
{
	std::lock_guard<std::mutex> lock(meshesMutex);

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
		// Scales buffer
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 4 * sizeof(float);
		gpuData.scales     = device->CreateBuffer(desc);
	}

	{
		// Rotations buffer
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 4 * sizeof(float);
		gpuData.rotations  = device->CreateBuffer(desc);
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
	if (maxShCoeffsPerSplat > 0)
	{
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * maxShCoeffsPerSplat * sizeof(float);
		gpuData.shRest     = device->CreateBuffer(desc);
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
	splatSorter = container::make_unique<CpuSplatSorter>(totalSplatCount);
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
	if (!splatSorter)
	{
		LOG_WARNING("CpuSplatSorter not initialized. Call AllocateGpuBuffers() first.");
		return;
	}

	UpdateSplatPositions();
	splatSorter->RequestSort(splatPositions, view_matrix);
}

rhi::FenceHandle Scene::ConsumeAndUploadSortedIndices()
{
	if (!splatSorter)
	{
		LOG_WARNING("CpuSplatSorter not initialized. Call AllocateGpuBuffers() first.");
		return nullptr;
	}

	if (!splatSorter->IsSortComplete())
	{
		// No new sorted data available yet
		return nullptr;
	}

	auto sortedIndices = splatSorter->GetSortedIndices();
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

}        // namespace msplat::engine