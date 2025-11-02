#pragma once

#include <future>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/math/math.h>

namespace msplat::engine
{

class CpuSplatSorter
{
  public:
	// Pre-allocates internal buffers for a given number of splats.
	explicit CpuSplatSorter(uint32_t max_splats);
	~CpuSplatSorter();        // Destructor will handle joining the worker thread.

	// Triggers a new sort on the background thread. This is a non-blocking call.
	// It takes the consolidated positions of all splats and the camera's view matrix.
	void RequestSort(const container::vector<math::vec3> &splat_positions, const math::mat4 &view_matrix);

	// Checks if a new sorted index buffer is ready.
	bool IsSortComplete() const;

	// Swaps the internal double buffer and returns the latest sorted indices.
	// Returns an empty span if no new data is ready.
	container::span<const uint32_t> GetSortedIndices();

  private:
	CpuSplatSorter(const CpuSplatSorter &)            = delete;
	CpuSplatSorter &operator=(const CpuSplatSorter &) = delete;

	class Impl;
	container::unique_ptr<Impl> p_impl;
};

}        // namespace msplat::engine
