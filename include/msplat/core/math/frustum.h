#pragma once

#include "matrix.h"
#include "vector.h"

namespace msplat::math
{

// Forward declarations
class AABB;
class Sphere;

// Frustum plane
struct Plane
{
	vec3  normal{0.0f, 1.0f, 0.0f};
	float distance{0.0f};

	Plane() = default;
	Plane(const vec3 &normal, float distance) :
	    normal(normal), distance(distance)
	{}
	Plane(const vec3 &normal, const vec3 &point) :
	    normal(normal), distance(glm::dot(normal, point))
	{}
	Plane(const vec3 &a, const vec3 &b, const vec3 &c)
	{
		vec3 ab  = b - a;
		vec3 ac  = c - a;
		normal   = glm::normalize(glm::cross(ab, ac));
		distance = glm::dot(normal, a);
	}

	// Distance from point to plane (positive = in front)
	float distanceToPoint(const vec3 &point) const
	{
		return glm::dot(normal, point) - distance;
	}

	// Normalize the plane equation
	void normalize()
	{
		float len = glm::length(normal);
		normal /= len;
		distance /= len;
	}
};

// View frustum for culling
class Frustum
{
  public:
	enum PlaneIndex
	{
		PLANE_LEFT = 0,
		PLANE_RIGHT,
		PLANE_BOTTOM,
		PLANE_TOP,
		PLANE_NEAR,
		PLANE_FAR,
		PLANE_COUNT
	};

	Plane planes[PLANE_COUNT];

	// Construct frustum from view-projection matrix
	void fromMatrix(const mat4 &viewProjection)
	{
		// Extract frustum planes from view-projection matrix
		// Left plane
		planes[PLANE_LEFT].normal.x = viewProjection[0][3] + viewProjection[0][0];
		planes[PLANE_LEFT].normal.y = viewProjection[1][3] + viewProjection[1][0];
		planes[PLANE_LEFT].normal.z = viewProjection[2][3] + viewProjection[2][0];
		planes[PLANE_LEFT].distance = viewProjection[3][3] + viewProjection[3][0];

		// Right plane
		planes[PLANE_RIGHT].normal.x = viewProjection[0][3] - viewProjection[0][0];
		planes[PLANE_RIGHT].normal.y = viewProjection[1][3] - viewProjection[1][0];
		planes[PLANE_RIGHT].normal.z = viewProjection[2][3] - viewProjection[2][0];
		planes[PLANE_RIGHT].distance = viewProjection[3][3] - viewProjection[3][0];

		// Bottom plane
		planes[PLANE_BOTTOM].normal.x = viewProjection[0][3] + viewProjection[0][1];
		planes[PLANE_BOTTOM].normal.y = viewProjection[1][3] + viewProjection[1][1];
		planes[PLANE_BOTTOM].normal.z = viewProjection[2][3] + viewProjection[2][1];
		planes[PLANE_BOTTOM].distance = viewProjection[3][3] + viewProjection[3][1];

		// Top plane
		planes[PLANE_TOP].normal.x = viewProjection[0][3] - viewProjection[0][1];
		planes[PLANE_TOP].normal.y = viewProjection[1][3] - viewProjection[1][1];
		planes[PLANE_TOP].normal.z = viewProjection[2][3] - viewProjection[2][1];
		planes[PLANE_TOP].distance = viewProjection[3][3] - viewProjection[3][1];

		// Near plane
		planes[PLANE_NEAR].normal.x = viewProjection[0][3] + viewProjection[0][2];
		planes[PLANE_NEAR].normal.y = viewProjection[1][3] + viewProjection[1][2];
		planes[PLANE_NEAR].normal.z = viewProjection[2][3] + viewProjection[2][2];
		planes[PLANE_NEAR].distance = viewProjection[3][3] + viewProjection[3][2];

		// Far plane
		planes[PLANE_FAR].normal.x = viewProjection[0][3] - viewProjection[0][2];
		planes[PLANE_FAR].normal.y = viewProjection[1][3] - viewProjection[1][2];
		planes[PLANE_FAR].normal.z = viewProjection[2][3] - viewProjection[2][2];
		planes[PLANE_FAR].distance = viewProjection[3][3] - viewProjection[3][2];

		// Normalize all planes
		for (int i = 0; i < PLANE_COUNT; ++i)
		{
			planes[i].normalize();
		}
	}

	// Test if point is inside frustum
	bool containsPoint(const vec3 &point) const
	{
		for (int i = 0; i < PLANE_COUNT; ++i)
		{
			if (planes[i].distanceToPoint(point) < 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	// Test if sphere intersects frustum
	bool intersectsSphere(const Sphere &sphere) const
	{
		for (int i = 0; i < PLANE_COUNT; ++i)
		{
			if (planes[i].distanceToPoint(sphere.center) < -sphere.radius)
			{
				return false;
			}
		}
		return true;
	}

	// Test if AABB intersects frustum
	bool intersectsAABB(const AABB &aabb) const
	{
		for (int i = 0; i < PLANE_COUNT; ++i)
		{
			const Plane &plane = planes[i];

			// Get the positive vertex (farthest from plane)
			vec3 positiveVertex = aabb.min;
			if (plane.normal.x >= 0)
				positiveVertex.x = aabb.max.x;
			if (plane.normal.y >= 0)
				positiveVertex.y = aabb.max.y;
			if (plane.normal.z >= 0)
				positiveVertex.z = aabb.max.z;

			// If positive vertex is behind plane, AABB is outside
			if (plane.distanceToPoint(positiveVertex) < 0.0f)
			{
				return false;
			}
		}
		return true;
	}
};

// Projection matrix creation
inline mat4 Perspective(float fovy, float aspect, float nearPlane, float farPlane)
{
	return glm::perspective(fovy, aspect, nearPlane, farPlane);
}

inline mat4 PerspectiveInfinite(float fovy, float aspect, float nearPlane)
{
	return glm::infinitePerspective(fovy, aspect, nearPlane);
}

inline mat4 Ortho(float left, float right, float bottom, float top, float nearPlane, float farPlane)
{
	return glm::ortho(left, right, bottom, top, nearPlane, farPlane);
}

inline mat4 FrustumMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane)
{
	return glm::frustum(left, right, bottom, top, nearPlane, farPlane);
}

// View matrix creation
inline mat4 LookAt(const vec3 &eye, const vec3 &center, const vec3 &up)
{
	return glm::lookAt(eye, center, up);
}

inline mat4 LookAtLH(const vec3 &eye, const vec3 &center, const vec3 &up)
{
	return glm::lookAtLH(eye, center, up);
}

inline mat4 LookAtRH(const vec3 &eye, const vec3 &center, const vec3 &up)
{
	return glm::lookAtRH(eye, center, up);
}

}        // namespace msplat::math