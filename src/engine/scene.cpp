#include <msplat/core/log.h>
#include <msplat/core/math/math.h>
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

	positions.reserve(totalSplatCount * 3);
	scales.reserve(totalSplatCount * 3);
	rotations.reserve(totalSplatCount * 4);
	colors.reserve(totalSplatCount * 3);
	shRest.reserve(totalShCoeffs);

	// Consolidate all mesh data
	for (const auto &mesh : meshes)
	{
		auto splatData = mesh.GetSplatData();
		if (!splatData || splatData->empty())
			continue;

		uint32_t count = splatData->numSplats;

		for (uint32_t i = 0; i < count; ++i)
		{
			positions.push_back(splatData->posX[i]);
			positions.push_back(splatData->posY[i]);
			positions.push_back(splatData->posZ[i]);
		}

		for (uint32_t i = 0; i < count; ++i)
		{
			scales.push_back(splatData->scaleX[i]);
			scales.push_back(splatData->scaleY[i]);
			scales.push_back(splatData->scaleZ[i]);
		}

		for (uint32_t i = 0; i < count; ++i)
		{
			rotations.push_back(splatData->rotX[i]);
			rotations.push_back(splatData->rotY[i]);
			rotations.push_back(splatData->rotZ[i]);
			rotations.push_back(splatData->rotW[i]);
		}

		for (uint32_t i = 0; i < count; ++i)
		{
			colors.push_back(splatData->fDc0[i]);
			colors.push_back(splatData->fDc1[i]);
			colors.push_back(splatData->fDc2[i]);
		}

		for (const auto &coeff : splatData->fRest)
		{
			shRest.push_back(coeff);
		}
	}

	std::vector<rhi::FenceHandle> uploadFences;

	// Upload all attribute buffers asynchronously
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

	// Position buffer (x, y, z)
	{
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 3 * sizeof(float);
		gpuData.positions  = device->CreateBuffer(desc);
	}

	// Scale buffer (sx, sy, sz)
	{
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 3 * sizeof(float);
		gpuData.scales     = device->CreateBuffer(desc);
	}

	// Rotation buffer (quaternion: x, y, z, w)
	{
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 4 * sizeof(float);
		gpuData.rotations  = device->CreateBuffer(desc);
	}

	// Color buffer (r, g, b)
	{
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 3 * sizeof(float);
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

	gpuBuffersAllocated = true;
	LOG_INFO("Allocated GPU buffers for {} splats (CPU-sort pipeline mode)", totalSplatCount);
}

bool Scene::IsAttributeDataUploaded() const
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	return attributeDataUploaded;
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

}        // namespace msplat::engine