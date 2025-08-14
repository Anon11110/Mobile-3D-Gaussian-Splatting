#pragma once

#include "vector.h"
#include <algorithm>
#include <limits>

namespace core
{
namespace math
{

// Axis-Aligned Bounding Box
class AABB
{
  public:
	vec3 min{std::numeric_limits<float>::max()};
	vec3 max{std::numeric_limits<float>::lowest()};

	// Constructors
	AABB() = default;
	AABB(const vec3 &minPoint, const vec3 &maxPoint) :
	    min(minPoint), max(maxPoint)
	{}
	AABB(const vec3 &center, float radius)
	{
		vec3 r(radius);
		min = center - r;
		max = center + r;
	}

	// Reset to invalid state
	void reset()
	{
		min = vec3(std::numeric_limits<float>::max());
		max = vec3(std::numeric_limits<float>::lowest());
	}

	// Check if valid (min <= max)
	bool isValid() const
	{
		return min.x <= max.x && min.y <= max.y && min.z <= max.z;
	}

	// Properties
	vec3 center() const
	{
		return (min + max) * 0.5f;
	}

	vec3 size() const
	{
		return isValid() ? (max - min) : vec3(0.0f);
	}

	vec3 extents() const
	{
		return size() * 0.5f;
	}

	float volume() const
	{
		vec3 s = size();
		return s.x * s.y * s.z;
	}

	float surfaceArea() const
	{
		vec3 s = size();
		return 2.0f * (s.x * s.y + s.y * s.z + s.z * s.x);
	}

	// Expand to include point
	void expandToInclude(const vec3 &point)
	{
		min = core::math::min(min, point);
		max = core::math::max(max, point);
	}

	// Expand to include another AABB
	void expandToInclude(const AABB &other)
	{
		if (!other.isValid())
			return;
		min = core::math::min(min, other.min);
		max = core::math::max(max, other.max);
	}

	// Expand by a margin
	void expand(float margin)
	{
		vec3 m(margin);
		min -= m;
		max += m;
	}

	void expand(const vec3 &margin)
	{
		min -= margin;
		max += margin;
	}

	// Contains tests
	bool contains(const vec3 &point) const
	{
		return point.x >= min.x && point.x <= max.x &&
		       point.y >= min.y && point.y <= max.y &&
		       point.z >= min.z && point.z <= max.z;
	}

	bool contains(const AABB &other) const
	{
		return contains(other.min) && contains(other.max);
	}

	// Intersection test
	bool intersects(const AABB &other) const
	{
		return max.x >= other.min.x && min.x <= other.max.x &&
		       max.y >= other.min.y && min.y <= other.max.y &&
		       max.z >= other.min.z && min.z <= other.max.z;
	}

	// Get intersection AABB
	AABB intersection(const AABB &other) const
	{
		if (!intersects(other))
			return AABB();        // Invalid AABB
		return AABB(core::math::max(min, other.min), core::math::min(max, other.max));
	}

	// Distance from point to AABB
	float distanceToPoint(const vec3 &point) const
	{
		vec3 closest = core::math::max(min, core::math::min(point, max));
		return core::math::distance(point, closest);
	}

	float distanceToPointSquared(const vec3 &point) const
	{
		vec3 closest = core::math::max(min, core::math::min(point, max));
		return core::math::distance2(point, closest);
	}

	// Get corner point (index 0-7)
	vec3 corner(int index) const
	{
		return vec3(
		    (index & 1) ? max.x : min.x,
		    (index & 2) ? max.y : min.y,
		    (index & 4) ? max.z : min.z);
	}

	// Get all 8 corners
	void getCorners(vec3 corners[8]) const
	{
		for (int i = 0; i < 8; ++i)
		{
			corners[i] = corner(i);
		}
	}

	// Operators
	AABB operator+(const vec3 &offset) const
	{
		return AABB(min + offset, max + offset);
	}

	AABB &operator+=(const vec3 &offset)
	{
		min += offset;
		max += offset;
		return *this;
	}

	bool operator==(const AABB &other) const
	{
		return core::math::equal(min, other.min) && core::math::equal(max, other.max);
	}

	bool operator!=(const AABB &other) const
	{
		return !(*this == other);
	}

	// Static constructors
	static AABB fromPoints(const vec3 *points, size_t count)
	{
		if (count == 0)
			return AABB();

		AABB result(points[0], points[0]);
		for (size_t i = 1; i < count; ++i)
		{
			result.expandToInclude(points[i]);
		}
		return result;
	}

	static AABB fromCenterAndSize(const vec3 &center, const vec3 &size)
	{
		vec3 halfSize = size * 0.5f;
		return AABB(center - halfSize, center + halfSize);
	}

	static AABB invalid()
	{
		return AABB();
	}

	static AABB infinite()
	{
		return AABB(
		    vec3(std::numeric_limits<float>::lowest()),
		    vec3(std::numeric_limits<float>::max()));
	}
};

}        // namespace math
}        // namespace core