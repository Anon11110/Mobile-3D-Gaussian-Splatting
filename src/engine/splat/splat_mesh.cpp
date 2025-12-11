#include <msplat/engine/splat/splat_mesh.h>

namespace msplat::engine
{

SplatMesh::SplatMesh(ID id, container::shared_ptr<SplatSoA> splatData, const math::mat4 &transform) :
    id(id), modelMatrix(transform), splatData(std::move(splatData))
{
}

SplatMesh::ID SplatMesh::GetId() const
{
	return id;
}

const math::mat4 &SplatMesh::GetModelMatrix() const
{
	return modelMatrix;
}

void SplatMesh::SetModelMatrix(const math::mat4 &transform)
{
	modelMatrix = transform;
}

container::shared_ptr<SplatSoA> SplatMesh::GetSplatData() const
{
	return splatData;
}

bool SplatMesh::HasCpuData() const
{
	return splatData != nullptr && !splatData->empty();
}

void SplatMesh::ReleaseCpuData()
{
	if (splatData)
	{
		splatData->Clear();
	}
}

}        // namespace msplat::engine