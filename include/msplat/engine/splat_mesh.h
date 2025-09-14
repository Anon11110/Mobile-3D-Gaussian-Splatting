#pragma once

#include <cstdint>
#include <msplat/core/containers/memory.h>
#include <msplat/core/math/matrix.h>
#include <msplat/engine/splat_soa.h>

namespace msplat::engine
{

class SplatMesh
{
  public:
	using ID = uint32_t;

	SplatMesh(ID id, container::shared_ptr<SplatSoA> splatData, const math::mat4 &transform);

	ID                              GetId() const;
	const math::mat4               &GetModelMatrix() const;
	void                            SetModelMatrix(const math::mat4 &transform);
	container::shared_ptr<SplatSoA> GetSplatData() const;
	bool                            HasCpuData() const;
	void                            ReleaseCpuData();

  private:
	ID                              id;
	math::mat4                      modelMatrix;
	container::shared_ptr<SplatSoA> splatData;
};

}        // namespace msplat::engine