#include <msplat/core/log.h>
#include <msplat/core/math/math.h>
#include <msplat/engine/scene/scene.h>
#include <msplat/engine/sorting/cpu_splat_sorter.h>
#include <msplat/engine/splat/splat_math.h>

#include <cstring>

namespace
{
// IEEE 754 float32 to float16 conversion
inline uint16_t FloatToHalf(float value)
{
	uint32_t f;
	std::memcpy(&f, &value, sizeof(f));

	uint32_t sign = (f >> 31) & 1;
	int32_t  exp  = static_cast<int32_t>((f >> 23) & 0xFF) - 127;
	uint32_t mant = f & 0x7FFFFF;

	if (exp > 15)
	{
		return static_cast<uint16_t>((sign << 15) | 0x7BFF);        // Max finite half (±65504)
	}
	if (exp < -24)
	{
		return static_cast<uint16_t>(sign << 15);        // Signed zero
	}
	if (exp < -14)
	{
		// Denormalized half
		mant |= 0x800000;
		uint32_t shift = static_cast<uint32_t>(-1 - exp);
		mant >>= shift;
		return static_cast<uint16_t>((sign << 15) | (mant >> 13));
	}

	return static_cast<uint16_t>((sign << 15) | ((exp + 15) << 10) | (mant >> 13));
}

// Pack two float32 values into a single uint32 as two float16 values
// Low 16 bits = half(a), high 16 bits = half(b)
inline uint32_t PackHalf2x16(float a, float b)
{
	return static_cast<uint32_t>(FloatToHalf(a)) |
	       (static_cast<uint32_t>(FloatToHalf(b)) << 16);
}

// Number of packed uint32s per splat for interleaved SH coefficients
// 45 halfs + 3 padding = 48 halfs = 24 uints
static constexpr uint32_t SH_PACKED_UINTS_PER_SPLAT = 24;
static constexpr uint32_t SH_COEFFS_PER_CHANNEL     = 15;
}        // namespace

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
		gpuData.meshIndices   = nullptr;
		gpuData.modelMatrices = nullptr;
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
		gpuData.meshIndices   = nullptr;
		gpuData.modelMatrices = nullptr;
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

	container::vector<float>    positions;
	container::vector<float>    covariances;
	container::vector<uint32_t> colorsPacked;        // Half-precision: 2 packed half2 per splat
	container::vector<uint32_t> shRestPacked;        // Half-precision: 23 packed half2 per splat
	container::vector<uint32_t> meshIndices;
	container::vector<float>    modelMatrices;

	positions.reserve(totalSplatCount * 4);
	covariances.reserve(totalSplatCount * 8);        // 6 floats + 2 padding = 8 floats per splat
	colorsPacked.reserve(totalSplatCount * 2);
	shRestPacked.reserve(totalSplatCount * SH_PACKED_UINTS_PER_SPLAT);
	meshIndices.reserve(totalSplatCount);
	modelMatrices.reserve(meshes.size() * 16);        // 16 floats per mat4

	meshGpuRanges.clear();
	uint32_t currentSplatOffset = 0;
	uint32_t meshIndex          = 0;

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

		// Store the column-major model matrix for this mesh
		const math::mat4 &mat = mesh.GetModelMatrix();
		for (int col = 0; col < 4; ++col)
		{
			for (int row = 0; row < 4; ++row)
			{
				modelMatrices.push_back(mat[col][row]);
			}
		}

		// Pack attributes in SoA format
		for (uint32_t i = 0; i < count; ++i)
		{
			// Store mesh index for this splat
			meshIndices.push_back(meshIndex);
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
			    splatData->rotY[i],         // rot_1 (x) -> .x
			    splatData->rotZ[i],         // rot_2 (y) -> .y
			    splatData->rotW[i],         // rot_3 (z) -> .z
			    splatData->rotX[i]);        // rot_0 (w) -> .w
			rotation = math::Normalize(rotation);

			float cov[6];
			ComputeCovariance3D(scale, rotation, cov);

			// Pack as 2 vec3 (6 floats + 2 padding
			covariances.push_back(cov[0]);        // M11
			covariances.push_back(cov[1]);        // M12
			covariances.push_back(cov[2]);        // M13
			covariances.push_back(0.0f);          // padding
			covariances.push_back(cov[3]);        // M22
			covariances.push_back(cov[4]);        // M23
			covariances.push_back(cov[5]);        // M33
			covariances.push_back(0.0f);          // padding

			// Color: convert SH degree 0 and opacity, pack as 2 half2
			math::vec3 rgb   = ComputeSHDegree0Color(splatData->fDc0[i], splatData->fDc1[i], splatData->fDc2[i]);
			float      alpha = TransformOpacity(splatData->opacity[i]);

			colorsPacked.push_back(PackHalf2x16(rgb.x, rgb.y));
			colorsPacked.push_back(PackHalf2x16(rgb.z, alpha));
		}

		// SH coefficients: reorder from channel-contiguous to interleaved
		// and pack as half-precision into uint pairs
		{
			const uint32_t meshShPerSplat   = splatData->shCoeffsPerSplat;
			const uint32_t coeffsPerChannel = (meshShPerSplat > 0) ? meshShPerSplat / 3 : 0;

			for (uint32_t si = 0; si < count; ++si)
			{
				const uint32_t splatBase = si * meshShPerSplat;

				// Build 48 halfs in interleaved order (45 coefficients + 3 padding)
				uint16_t halfs[48] = {};
				for (uint32_t i = 0; i < SH_COEFFS_PER_CHANNEL; ++i)
				{
					float r          = (i < coeffsPerChannel) ? splatData->fRest[splatBase + i] : 0.0f;
					float g          = (i < coeffsPerChannel) ? splatData->fRest[splatBase + coeffsPerChannel + i] : 0.0f;
					float b          = (i < coeffsPerChannel) ? splatData->fRest[splatBase + 2 * coeffsPerChannel + i] : 0.0f;
					halfs[i * 3 + 0] = FloatToHalf(r);
					halfs[i * 3 + 1] = FloatToHalf(g);
					halfs[i * 3 + 2] = FloatToHalf(b);
				}

				// Pack pairs of halfs into uint32s (24 uints = 6 uint4)
				for (uint32_t u = 0; u < SH_PACKED_UINTS_PER_SPLAT; ++u)
				{
					shRestPacked.push_back(
					    static_cast<uint32_t>(halfs[u * 2]) |
					    (static_cast<uint32_t>(halfs[u * 2 + 1]) << 16));
				}
			}
		}

		currentSplatOffset += count;
		meshIndex++;
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
	    colorsPacked.data(),
	    colorsPacked.size() * sizeof(uint32_t));
	if (colorsFence)
	{
		uploadFences.push_back(colorsFence);
	}

	if (!shRestPacked.empty())
	{
		rhi::FenceHandle shFence = device->UploadBufferAsync(
		    gpuData.shRest.Get(),
		    shRestPacked.data(),
		    shRestPacked.size() * sizeof(uint32_t));
		if (shFence)
		{
			uploadFences.push_back(shFence);
		}
	}

	// Upload mesh indices
	if (!meshIndices.empty() && gpuData.meshIndices)
	{
		rhi::FenceHandle meshIndicesFence = device->UploadBufferAsync(
		    gpuData.meshIndices.Get(),
		    meshIndices.data(),
		    meshIndices.size() * sizeof(uint32_t));
		if (meshIndicesFence)
		{
			uploadFences.push_back(meshIndicesFence);
		}
	}

	// Upload model matrices
	if (!modelMatrices.empty() && gpuData.modelMatrices)
	{
		rhi::FenceHandle modelMatricesFence = device->UploadBufferAsync(
		    gpuData.modelMatrices.Get(),
		    modelMatrices.data(),
		    modelMatrices.size() * sizeof(float));
		if (modelMatricesFence)
		{
			uploadFences.push_back(modelMatricesFence);
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
	gpuData.meshIndices   = nullptr;
	gpuData.modelMatrices = nullptr;

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
		// Covariances3D buffer (6 floats per splat, packed as 2 float4 with padding)
		BufferDesc desc{};
		desc.usage            = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage    = ResourceUsage::Static;
		desc.size             = totalSplatCount * 8 * sizeof(float);        // 2 vec3 (padded) = 8 floats
		gpuData.covariances3D = device->CreateBuffer(desc);
	}

	{
		// Colors buffer (RGBA as 4 float16 packed into 2 uint32)
		BufferDesc desc{};
		desc.usage         = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage = ResourceUsage::Static;
		desc.size          = totalSplatCount * 2 * sizeof(uint32_t);        // 2 packed half2
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
			desc.size      = totalSplatCount * SH_PACKED_UINTS_PER_SPLAT * sizeof(uint32_t);
			gpuData.shRest = device->CreateBuffer(desc);
			LOG_INFO("Allocated SH buffer for {} splats with {} packed uints each ({:.2f} MB, interleaved half precision)",
			         totalSplatCount, SH_PACKED_UINTS_PER_SPLAT,
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

	// Per-splat mesh indices buffer (maps splat index -> mesh index for model matrix lookup)
	{
		BufferDesc desc{};
		desc.usage          = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage  = ResourceUsage::Static;
		desc.size           = totalSplatCount * sizeof(uint32_t);
		gpuData.meshIndices = device->CreateBuffer(desc);
	}

	// Per-mesh model matrices buffer
	{
		BufferDesc desc{};
		desc.usage            = BufferUsage::STORAGE | BufferUsage::TRANSFER_DST;
		desc.resourceUsage    = ResourceUsage::DynamicUpload;
		desc.size             = std::max(meshes.size(), size_t(1)) * sizeof(float) * 16;        // mat4 = 16 floats
		gpuData.modelMatrices = device->CreateBuffer(desc);
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

bool Scene::UpdateMeshTransform(SplatMesh::ID id, const math::mat4 &newTransform)
{
	std::lock_guard<std::mutex> lock(meshesMutex);

	auto meshIt = std::find_if(meshes.begin(), meshes.end(),
	                           [id](const SplatMesh &mesh) { return mesh.GetId() == id; });

	if (meshIt == meshes.end())
	{
		LOG_WARNING("UpdateMeshTransform: Mesh {} not found", id);
		return false;
	}

	meshIt->SetModelMatrix(newTransform);

	auto rangeIt = meshGpuRanges.find(id);
	if (rangeIt != meshGpuRanges.end())
	{
		rangeIt->second.transform = newTransform;
	}

	// Find the mesh index (position in meshes vector) for GPU buffer update
	size_t meshIndex = std::distance(meshes.begin(), meshIt);

	if (gpuData.modelMatrices)
	{
		container::vector<float> matrixData;
		matrixData.reserve(16);

		for (int col = 0; col < 4; ++col)
		{
			for (int row = 0; row < 4; ++row)
			{
				matrixData.push_back(newTransform[col][row]);
			}
		}

		size_t offset = meshIndex * 16 * sizeof(float);
		device->UploadBufferAsync(
		    gpuData.modelMatrices.Get(),
		    matrixData.data(),
		    matrixData.size() * sizeof(float),
		    offset);
	}

	return true;
}

size_t Scene::GetMeshCount() const
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	return meshes.size();
}

}        // namespace msplat::engine