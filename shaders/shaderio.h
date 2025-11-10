#ifndef SHADERIO_H
#define SHADERIO_H

#ifdef __cplusplus
#include "core/math/math.h"
using namespace msplat::math;
#endif

struct FrameUBO
{
	mat4 view;
	mat4 projection;
	vec4 cameraPos;
	vec2 viewport;
	vec2 focal;
};

#endif // SHADERIO_H
