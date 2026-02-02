#pragma once

#include <functional>
#include <future>
#include <memory>
#include <msplat/core/containers/filesystem.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/unordered_map.h>
#include <msplat/core/containers/vector.h>
#include <msplat/engine/sorting/cpu_splat_sorter.h>
#include <msplat/engine/splat/splat_loader.h>
#include <msplat/engine/splat/splat_mesh.h>
#include <mutex>
#include <rhi/rhi.h>

namespace msplat::engine
{

class Scene
{
  public:
	Scene(const Scene &)            = delete;
	Scene &operator=(const Scene &) = delete;
	Scene(Scene &&)                 = delete;
	Scene &operator=(Scene &&)      = delete;

	struct GpuData
	{
		// Splat attribute buffers
		rhi::BufferHandle positions;            // vec3 positions[]
		rhi::BufferHandle covariances3D;        // vec3[2] covariances[] (6 floats packed as 2 vec3)
		rhi::BufferHandle colors;               // vec4 colors[]
		rhi::BufferHandle shRest;               // float shRest[]

		// Sorted indices for rendering
		rhi::BufferHandle sortedIndices;
	};

	// Tracks which GPU buffer range belongs to each mesh
	struct MeshGpuRange
	{
		uint32_t   startIndex;        // First splat index in consolidated buffer
		uint32_t   splatCount;        // Number of splats for this mesh
		math::mat4 transform;         // Local transform
	};

	// Callback type for buffer change notifications
	// Called after ReallocateAndUpload() completes, allowing apps to rebind descriptors
	using BufferChangeCallback = std::function<void(const GpuData &, uint32_t newSplatCount)>;

	explicit Scene(rhi::IRHIDevice *device);
	~Scene() = default;

	// Synchronously adds a mesh from already-loaded data
	SplatMesh::ID AddMesh(container::shared_ptr<SplatSoA> splatData,
	                      const math::mat4               &initialTransform = math::Identity());

	// Removes a mesh by ID. Returns true if the mesh was found and removed
	bool RemoveMesh(SplatMesh::ID id);

	// Allocates GPU buffers for attribute data and sorted indices
	void AllocateGpuBuffers();

	// Uploads static attribute data (positions, covariances, colors, SH) to GPU
	rhi::FenceHandle UploadAttributeData();

	// Triggers a new sort based on the camera's view matrix
	void UpdateView(const math::mat4 &view_matrix);

	// Checks for sorted data and uploads it to the GPU if available
	rhi::FenceHandle ConsumeAndUploadSortedIndices();

	const GpuData &GetGpuData() const;
	uint32_t       GetTotalSplatCount() const;
	bool           IsAttributeDataUploaded() const;

	// Reallocates GPU buffers after mesh add/remove
	rhi::FenceHandle ReallocateAndUpload();

	// Set callback to be notified when GPU buffers change
	void SetBufferChangeCallback(BufferChangeCallback callback);

	// Helper to find which mesh owns a specific splat index
	SplatMesh::ID GetMeshIDFromSplatIndex(uint32_t globalIndex) const;

	// Get the GPU range for a specific mesh
	const MeshGpuRange *GetMeshGpuRange(SplatMesh::ID id) const;

	// CPU memory usage breakdown for profiling
	struct CpuMemoryInfo
	{
		size_t splatDataBytes;             // SplatSoA data in meshes
		size_t splatPositionsBytes;        // splatPositions vector
		size_t sortedIndicesBytes;         // lastSortedIndices vector
		size_t cpuSorterBytes;             // CpuSplatSorter buffers
	};
	CpuMemoryInfo GetCpuMemoryInfo() const;

	// Backend integration helpers (for CpuSplatSortBackend)
	bool                            IsCpuSortComplete() const;
	container::span<const uint32_t> GetCpuSortedIndices();

	bool VerifyCpuSortOrder(const math::mat4 &viewMatrix) const;

	template <typename Func>
	void ForEachMesh(Func &&f) const;

	template <typename Func>
	void ForEachMesh(Func &&f);

  private:
	rhi::IRHIDevice *device;
	GpuData          gpuData;

	mutable std::mutex           meshesMutex;
	container::vector<SplatMesh> meshes;
	uint32_t                     nextMeshId{0};
	uint32_t                     totalSplatCount{0};
	bool                         attributeDataUploaded{false};        // Track if attribute data has been uploaded
	bool                         gpuBuffersAllocated{false};          // Track if GPU buffers are allocated

	// CPU-side data for sorting
	container::vector<math::vec3>         splatPositions;
	container::unique_ptr<CpuSplatSorter> cpuSplatSorter;

	// Store last sorted indices for verification
	mutable container::vector<uint32_t> lastSortedIndices;

	// Dynamic scene management
	container::unordered_map<SplatMesh::ID, MeshGpuRange> meshGpuRanges;
	BufferChangeCallback                                  bufferChangeCallback;

	uint32_t CalculateMaxShCoeffsPerSplat() const;
	void     UpdateSplatPositions();

	// Internal versions without locking
	void             AllocateGpuBuffersInternal();
	rhi::FenceHandle UploadAttributeDataInternal();
};

template <typename Func>
void Scene::ForEachMesh(Func &&f) const
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	for (const auto &mesh : meshes)
	{
		f(mesh);
	}
}

template <typename Func>
void Scene::ForEachMesh(Func &&f)
{
	std::lock_guard<std::mutex> lock(meshesMutex);
	for (auto &mesh : meshes)
	{
		f(mesh);
	}
}

}        // namespace msplat::engine