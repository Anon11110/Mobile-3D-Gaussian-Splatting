#pragma once

#include <msplat/core/containers/vector.h>
#include <msplat/core/math/math.h>

namespace msplat::engine
{

/**
 * @brief Mesh data for an icosphere wireframe
 *
 * Contains vertices and indices for LINE_LIST rendering of a wireframe sphere
 * generated using icosahedron subdivision.
 */
struct IcosphereMesh
{
	container::vector<math::vec3> vertices;
	container::vector<uint16_t>   indices;        // Pairs of vertex indices for LINE_LIST
};

/**
 * @brief Generate an icosphere wireframe mesh using subdivision
 *
 * Creates a sphere approximation by subdividing an icosahedron. Each subdivision
 * level increases the triangle count by 4x, creating a smoother sphere.
 *
 * @param subdivisions Number of subdivision iterations (0-4 recommended)
 *                     0 = 20 triangles (icosahedron)
 *                     1 = 80 triangles
 *                     2 = 320 triangles
 *                     3 = 1280 triangles
 *                     4 = 5120 triangles
 *
 * @return IcosphereMesh containing vertices on unit sphere and edge indices
 */
IcosphereMesh generateIcosphereWireframe(int subdivisions = 2);

}        // namespace msplat::engine