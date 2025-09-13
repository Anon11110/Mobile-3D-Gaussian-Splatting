#include <msplat/core/containers/hash.h>
#include <msplat/core/containers/unordered_map.h>
#include <msplat/core/containers/unordered_set.h>
#include <msplat/core/containers/vector.h>
#include <msplat/engine/mesh_generator.h>

namespace msplat::engine
{

IcosphereMesh generateIcosphereWireframe(int subdivisions)
{
	IcosphereMesh mesh;

	// Icosahedron base geometry
	const float t = (1.0f + math::sqrt(5.0f)) / 2.0f;
	const float s = math::sqrt(1.0f + t * t);

	// 12 vertices of icosahedron (normalized to unit sphere)
	mesh.vertices.reserve(12);
	// XY plane rectangle
	mesh.vertices.push_back(math::vec3(-1.0f, t, 0.0f) / s);
	mesh.vertices.push_back(math::vec3(1.0f, t, 0.0f) / s);
	mesh.vertices.push_back(math::vec3(-1.0f, -t, 0.0f) / s);
	mesh.vertices.push_back(math::vec3(1.0f, -t, 0.0f) / s);
	// YZ plane rectangle
	mesh.vertices.push_back(math::vec3(0.0f, -1.0f, t) / s);
	mesh.vertices.push_back(math::vec3(0.0f, 1.0f, t) / s);
	mesh.vertices.push_back(math::vec3(0.0f, -1.0f, -t) / s);
	mesh.vertices.push_back(math::vec3(0.0f, 1.0f, -t) / s);
	// XZ plane rectangle
	mesh.vertices.push_back(math::vec3(t, 0.0f, -1.0f) / s);
	mesh.vertices.push_back(math::vec3(t, 0.0f, 1.0f) / s);
	mesh.vertices.push_back(math::vec3(-t, 0.0f, -1.0f) / s);
	mesh.vertices.push_back(math::vec3(-t, 0.0f, 1.0f) / s);

	struct Triangle
	{
		uint32_t v0, v1, v2;
	};

	// 20 triangular faces of icosahedron
	container::vector<Triangle> triangles;
	triangles.reserve(20);
	// 5 faces around vertex 0
	triangles.push_back({0, 11, 5});
	triangles.push_back({0, 5, 1});
	triangles.push_back({0, 1, 7});
	triangles.push_back({0, 7, 10});
	triangles.push_back({0, 10, 11});
	// 5 adjacent faces
	triangles.push_back({1, 5, 9});
	triangles.push_back({5, 11, 4});
	triangles.push_back({11, 10, 2});
	triangles.push_back({10, 7, 6});
	triangles.push_back({7, 1, 8});
	// 5 faces around vertex 3
	triangles.push_back({3, 9, 4});
	triangles.push_back({3, 4, 2});
	triangles.push_back({3, 2, 6});
	triangles.push_back({3, 6, 8});
	triangles.push_back({3, 8, 9});
	// 5 adjacent faces
	triangles.push_back({4, 9, 5});
	triangles.push_back({2, 4, 11});
	triangles.push_back({6, 2, 10});
	triangles.push_back({8, 6, 7});
	triangles.push_back({9, 8, 1});

	// Apply subdivision
	for (int level = 0; level < subdivisions; ++level)
	{
		container::vector<Triangle> newTriangles;
		newTriangles.reserve(triangles.size() * 4);

		// Map to cache midpoint vertices
		struct EdgeKey
		{
			uint32_t v0, v1;
			bool     operator==(const EdgeKey &other) const
			{
				return (v0 == other.v0 && v1 == other.v1) || (v0 == other.v1 && v1 == other.v0);
			}
		};

		struct EdgeHash
		{
			size_t operator()(const EdgeKey &edge) const
			{
				uint32_t min_v = (edge.v0 < edge.v1) ? edge.v0 : edge.v1;
				uint32_t max_v = (edge.v0 > edge.v1) ? edge.v0 : edge.v1;
				return container::hash<uint32_t>{}(min_v) ^ (container::hash<uint32_t>{}(max_v) << 1);
			}
		};

		container::unordered_map<EdgeKey, uint32_t, EdgeHash> edgeMidpoints;

		auto getMidpoint = [&](uint32_t v0, uint32_t v1) -> uint32_t {
			EdgeKey edge{v0, v1};
			auto    it = edgeMidpoints.find(edge);
			if (it != edgeMidpoints.end())
				return it->second;

			// Create new midpoint vertex on unit sphere
			math::vec3 mid      = math::normalize((mesh.vertices[v0] + mesh.vertices[v1]) * 0.5f);
			uint32_t   midIndex = static_cast<uint32_t>(mesh.vertices.size());
			mesh.vertices.push_back(mid);
			edgeMidpoints[edge] = midIndex;
			return midIndex;
		};

		// Subdivide each triangle into 4 smaller triangles
		for (const auto &tri : triangles)
		{
			uint32_t v01 = getMidpoint(tri.v0, tri.v1);
			uint32_t v12 = getMidpoint(tri.v1, tri.v2);
			uint32_t v20 = getMidpoint(tri.v2, tri.v0);

			newTriangles.push_back({tri.v0, v01, v20});
			newTriangles.push_back({tri.v1, v12, v01});
			newTriangles.push_back({tri.v2, v20, v12});
			newTriangles.push_back({v01, v12, v20});
		}

		triangles = newTriangles;
	}

	// Extract unique edges for wireframe using hash set
	struct Edge
	{
		uint16_t v0, v1;
		Edge(uint32_t a, uint32_t b) :
		    v0((a < b) ? a : b), v1((a < b) ? b : a)
		{}
		bool operator==(const Edge &other) const
		{
			return v0 == other.v0 && v1 == other.v1;
		}
	};

	struct EdgeHash
	{
		size_t operator()(const Edge &e) const
		{
			return container::hash<uint16_t>{}(e.v0) ^ (container::hash<uint16_t>{}(e.v1) << 1);
		}
	};

	container::unordered_set<Edge, EdgeHash> uniqueEdges;
	for (const auto &tri : triangles)
	{
		uniqueEdges.insert(Edge(tri.v0, tri.v1));
		uniqueEdges.insert(Edge(tri.v1, tri.v2));
		uniqueEdges.insert(Edge(tri.v2, tri.v0));
	}

	// Convert edges to indices for line rendering
	mesh.indices.reserve(uniqueEdges.size() * 2);
	for (const auto &edge : uniqueEdges)
	{
		mesh.indices.push_back(edge.v0);
		mesh.indices.push_back(edge.v1);
	}

	return mesh;
}

}        // namespace msplat::engine