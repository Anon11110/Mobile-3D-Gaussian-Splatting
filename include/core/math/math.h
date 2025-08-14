#pragma once

// Core math library - GLM wrapper
// Provides comprehensive math functionality for 3D graphics applications

// Basic math utilities and constants
#include "basics.h"

// Vector types and operations
#include "vector.h"

// Matrix types and operations
#include "matrix.h"

// Quaternion operations for rotations
#include "quaternion.h"

// Affine transforms
#include "affine.h"

// Bounding volumes
#include "aabb.h"
#include "sphere.h"

// View frustum and projection matrices
#include "frustum.h"

// Color utilities
#include "color.h"

// Convenience namespace alias
namespace core
{
using namespace math;
}

// Common type aliases for convenience
using namespace core::math;