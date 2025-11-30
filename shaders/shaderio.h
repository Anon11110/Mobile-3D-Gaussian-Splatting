#ifndef SHADERIO_H
#define SHADERIO_H

#ifdef __cplusplus
#include "core/math/math.h"
using namespace msplat::math;
#endif

// Macro for FrameUBO fields
#define FRAMEUBO_FIELDS 	  \
	mat4 view;          	  \
	mat4 projection;    	  \
	                    	  \
	vec4 cameraPos;     	  \
	vec2 viewport;      	  \
	vec2 focal;         	  \
	                    	  \
	float splatScale;   	  \
	float alphaCullThreshold; \
	                    	  \
	float maxSplatRadius;     \
	int   enableSplatFilter;  \
	vec2  basisViewport;      \
	float inverseFocalAdj;    \

struct FrameUBO
{
	FRAMEUBO_FIELDS
};

#endif // SHADERIO_H
