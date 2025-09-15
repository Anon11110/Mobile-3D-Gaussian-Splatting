#pragma once

#include <future>
#include <memory>
#include <msplat/core/containers/filesystem.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>
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
		// Attribute Buffers
		container::unique_ptr<rhi::IRHIBuffer> positions;
		container::unique_ptr<rhi::IRHIBuffer> scales;
		container::unique_ptr<rhi::IRHIBuffer> rotations;        // quaternions
		container::unique_ptr<rhi::IRHIBuffer> colors;
		container::unique_ptr<rhi::IRHIBuffer> shRest;

		// Global Index Buffer (optional)
		container::unique_ptr<rhi::IRHIBuffer> indices;
	};

	explicit Scene(rhi::IRHIDevice *device);
	~Scene() = default;

	// Synchronously adds a mesh from already-loaded data
	SplatMesh::ID AddMesh(container::shared_ptr<SplatSoA> splatData,
	                      const math::mat4               &initialTransform = math::Identity());

	// Removes a mesh by ID. Returns true if the mesh was found and removed.
	bool RemoveMesh(SplatMesh::ID id);

	// Uploads all static attribute data for loaded meshes to GPU.
	// Call this once after all meshes are loaded if plan to update indices every frame.
	void AllocateGpuBuffers();

	// Uploads static attribute data (positions, scales, rotations, colors, SH) to GPU.
	// This should be called once after all meshes are loaded for GPU-sort pipeline.
	container::shared_ptr<rhi::IRHIFence> UploadAttributeData();

	// Updates the index buffer with sorted indices.
	// Can be called every frame with new sorted indices.
	// Requires AllocateGpuBuffers() or UploadAttributeData() to be called first.
	container::shared_ptr<rhi::IRHIFence> UpdateIndexBuffer(const uint32_t *indices, size_t count);

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

	void     AllocateGpuBuffersInternal(uint32_t totalSplats, uint32_t maxShCoeffsPerSplat);
	uint32_t CalculateMaxShCoeffsPerSplat() const;
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