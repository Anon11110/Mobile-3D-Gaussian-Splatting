#pragma once

#include <future>
#include <memory>
#include <msplat/core/containers/filesystem.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/engine/cpu_splat_sorter.h>
#include <msplat/engine/splat_loader.h>
#include <msplat/engine/splat_mesh.h>
#include <mutex>
#include <rhi/rhi.h>

namespace msplat::engine
{

class Scene
{
  public:
	struct GpuData
	{
		// Splat attribute buffers
		rhi::BufferHandle positions;        // vec3 positions[]
		rhi::BufferHandle scales;           // vec3 scales[]
		rhi::BufferHandle rotations;        // vec4 rotations[]
		rhi::BufferHandle colors;           // vec4 colors[]
		rhi::BufferHandle shRest;           // float shRest[]

		// Sorted indices for rendering
		rhi::BufferHandle sortedIndices;
	};

	explicit Scene(rhi::IRHIDevice *device);
	~Scene() = default;

	// Synchronously adds a mesh from already-loaded data
	SplatMesh::ID AddMesh(container::shared_ptr<SplatSoA> splatData,
	                      const math::mat4               &initialTransform = math::Identity());

	// Removes a mesh by ID. Returns true if the mesh was found and removed
	bool RemoveMesh(SplatMesh::ID id);

	// Allocates GPU buffers for attribute data and sorted indices
	void AllocateGpuBuffers();

	// Uploads static attribute data (positions, scales, rotations, colors, SH) to GPU
	rhi::FenceHandle UploadAttributeData();

	// Triggers a new sort based on the camera's view matrix
	void UpdateView(const math::mat4 &view_matrix);

	// Checks for sorted data and uploads it to the GPU if available
	rhi::FenceHandle ConsumeAndUploadSortedIndices();

	const GpuData &GetGpuData() const;
	uint32_t       GetTotalSplatCount() const;
	bool           IsAttributeDataUploaded() const;

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
	container::unique_ptr<CpuSplatSorter> splatSorter;

	uint32_t CalculateMaxShCoeffsPerSplat() const;
	void     UpdateSplatPositions();
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