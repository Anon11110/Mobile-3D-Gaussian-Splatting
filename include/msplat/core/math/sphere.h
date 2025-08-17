#pragma once

#include "aabb.h"
#include "basics.h"
#include "vector.h"
#include <algorithm>

namespace msplat::math
{

// Bounding Sphere
class Sphere
{
  public:
	vec3  center{0.0f};
	float radius{0.0f};

	// Constructors
	Sphere() = default;
	Sphere(const vec3 &center, float radius) :
	    center(center), radius(radius)
	{}

	// Properties
	bool isValid() const
	{
		return radius >= 0.0f;
	}

	float volume() const
	{
		return (4.0f / 3.0f) * PI * radius * radius * radius;
	}

	float surfaceArea() const
	{
		return 4.0f * PI * radius * radius;
	}

	// Expand to include point
	void expandToInclude(const vec3 &point)
	{
		float dist = glm::distance(center, point);
		if (dist > radius)
		{
			radius = dist;
		}
	}

	// Expand to include another sphere
	void expandToInclude(const Sphere &other)
	{
		float dist      = glm::distance(center, other.center);
		float newRadius = std::max(radius, dist + other.radius);

		if (newRadius > radius)
		{
			// Need to adjust center and radius
			if (dist + other.radius > radius && dist + radius > other.radius)
			{
				// Neither sphere contains the other
				vec3 direction = glm::normalize(other.center - center);
				vec3 point1    = center - direction * radius;
				vec3 point2    = other.center + direction * other.radius;
				center         = (point1 + point2) * 0.5f;
				radius         = glm::distance(center, point1);
			}
			else if (dist + other.radius > radius)
			{
				// This sphere is contained in other
				center = other.center;
				radius = other.radius;
			}
			// Otherwise other sphere is contained in this one, no change needed
		}
	}

	// Expand by margin
	void expand(float margin)
	{
		radius += margin;
	}

	// Contains tests
	bool contains(const vec3 &point) const
	{
		return distance2(center, point) <= radius * radius;
	}

	bool contains(const Sphere &other) const
	{
		return glm::distance(center, other.center) + other.radius <= radius;
	}

	// Intersection test
	bool intersects(const Sphere &other) const
	{
		float dist = glm::distance(center, other.center);
		return dist <= (radius + other.radius);
	}

	bool intersects(const AABB &aabb) const
	{
		return aabb.distanceToPointSquared(center) <= radius * radius;
	}

	// Distance from point to sphere surface
	float distanceToPoint(const vec3 &point) const
	{
		return glm::distance(center, point) - radius;
	}

	// Get point on sphere surface
	vec3 pointOnSurface(const vec3 &direction) const
	{
		return center + glm::normalize(direction) * radius;
	}

	// Get AABB that contains this sphere
	AABB getAABB() const
	{
		vec3 r(radius);
		return AABB(center - r, center + r);
	}

	// Operators
	Sphere operator+(const vec3 &offset) const
	{
		return Sphere(center + offset, radius);
	}

	Sphere &operator+=(const vec3 &offset)
	{
		center += offset;
		return *this;
	}

	bool operator==(const Sphere &other) const
	{
		return glm::all(glm::epsilonEqual(center, other.center, glm::epsilon<float>())) &&
		       std::abs(radius - other.radius) < glm::epsilon<float>();
	}

	bool operator!=(const Sphere &other) const
	{
		return !(*this == other);
	}

	// Static constructors
	static Sphere fromPoints(const vec3 *points, size_t count)
	{
		if (count == 0)
			return Sphere();
		if (count == 1)
			return Sphere(points[0], 0.0f);

		// Simple approach: use AABB center and radius to farthest point
		AABB aabb   = AABB::fromPoints(points, count);
		vec3 center = aabb.center();

		float maxDistSq = 0.0f;
		for (size_t i = 0; i < count; ++i)
		{
			maxDistSq = std::max(maxDistSq, distance2(center, points[i]));
		}

		return Sphere(center, std::sqrt(maxDistSq));
	}

	static Sphere fromAABB(const AABB &aabb)
	{
		if (!aabb.isValid())
			return Sphere();
		vec3  center = aabb.center();
		float radius = glm::length(aabb.extents());
		return Sphere(center, radius);
	}

	static Sphere invalid()
	{
		return Sphere(vec3(0.0f), -1.0f);
	}
};

}        // namespace msplat::math