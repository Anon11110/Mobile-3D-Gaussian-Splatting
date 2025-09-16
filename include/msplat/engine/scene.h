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
		std::unique_ptr<rhi::IRHIBuffer> positions;
		std::unique_ptr<rhi::IRHIBuffer> scales;
		std::unique_ptr<rhi::IRHIBuffer> rotations;        // quaternions
		std::unique_ptr<rhi::IRHIBuffer> colors;
		std::unique_ptr<rhi::IRHIBuffer> shRest;
	};

	explicit Scene(rhi::IRHIDevice *device);
	~Scene() = default;

	// Synchronously adds a mesh from already-loaded data
	SplatMesh::ID AddMesh(container::shared_ptr<SplatSoA> splatData,
	                      const math::mat4               &initialTransform = math::Identity());

	// Removes a mesh by ID. Returns true if the mesh was found and removed
	bool RemoveMesh(SplatMesh::ID id);

	// Allocates GPU buffers for attribute data
	void AllocateGpuBuffers();

	// Uploads static attribute data (positions, scales, rotations, colors, SH) to GPU
	std::shared_ptr<rhi::IRHIFence> UploadAttributeData();

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