#ifndef SHADERIO_H
#define SHADERIO_H

#ifdef __cplusplus
    // C++ types
    #include "core/math/math.h"
    using namespace msplat::math;
    #define SHADER_MAT4   mat4
    #define SHADER_VEC4   vec4
    #define SHADER_VEC2   vec2
    #define SHADER_FLOAT  float
    #define SHADER_INT    int
#else
    // HLSL types
    #define SHADER_MAT4   float4x4
    #define SHADER_VEC4   float4
    #define SHADER_VEC2   float2
    #define SHADER_FLOAT  float
    #define SHADER_INT    int
#endif

struct FrameUBO
{
    SHADER_MAT4 view;
    SHADER_MAT4 projection;

    SHADER_VEC4 cameraPos;
    SHADER_VEC2 viewport;
    SHADER_VEC2 focal;

    SHADER_FLOAT splatScale;
    SHADER_FLOAT alphaCullThreshold;

    SHADER_FLOAT maxSplatRadius;
    SHADER_INT   enableSplatFilter;
    SHADER_VEC4  screenRotation;
    SHADER_VEC2  basisViewport;
    SHADER_FLOAT inverseFocalAdj;
    SHADER_FLOAT _pad0;
};

// Pre-computed per-splat data
struct HWRasterSplat
{
    SHADER_VEC4 centerClip; // Clip-space center position (xyzw)
    SHADER_VEC2 ndcBasis1;  // Pre-rotated/scaled NDC basis vector 1
    SHADER_VEC2 ndcBasis2;  // Pre-rotated/scaled NDC basis vector 2
    SHADER_VEC4 color;      // Final RGBA
};

#endif // SHADERIO_H
