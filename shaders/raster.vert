#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 outColor;
layout(location = 1) noperspective out vec2 outUV;

layout(set = 0, binding = 0) uniform FrameUBO
{
	mat4 view;
	mat4 projection;
	vec4 cameraPos;
	vec2 viewport;
	vec2 focal;
}
ubo;

layout(set = 0, binding = 1, std430) readonly buffer PositionsBuffer
{
	vec3 positions[];
};

layout(set = 0, binding = 2, std430) readonly buffer ScalesBuffer
{
	vec3 scales[];
};

layout(set = 0, binding = 3, std430) readonly buffer RotationsBuffer
{
	vec4 rotations[];
};

layout(set = 0, binding = 4, std430) readonly buffer ColorsBuffer
{
	vec4 colors[];
};

layout(set = 0, binding = 5, std430) readonly buffer SHRestBuffer
{
	float shRest[];
};

layout(set = 0, binding = 6, std430) readonly buffer SortedIndicesBuffer
{
	uint indices[];
};

mat3 QuatToMat3(vec4 q)
{
	float x = q.x, y = q.y, z = q.z, w = q.w;
	return mat3(
		1.0 - 2.0*(y*y + z*z), 2.0*(x*y - w*z),       2.0*(x*z + w*y),
		2.0*(x*y + w*z),       1.0 - 2.0*(x*x + z*z), 2.0*(y*z - w*x),
		2.0*(x*z - w*y),       2.0*(y*z + w*x),       1.0 - 2.0*(x*x + y*y)
	);
}

void KillSplat()
{
	gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
	outColor = vec4(0.0);
	outUV = vec2(0.0);
}

void main()
{
	uint splatId = gl_VertexIndex / 4;
	uint vertexId = gl_VertexIndex % 4;

	uint splatIndex = indices[splatId];

	vec3  pos   = positions[splatIndex];
	vec3  scale = scales[splatIndex];
	vec4  rot   = rotations[splatIndex];
	vec4  color = colors[splatIndex];

	// Early alpha culling
	if (color.a < 1.0 / 255.0)
	{
		KillSplat();
		return;
	}

	// Transform to view space
	vec4 viewPos4 = ubo.view * vec4(pos, 1.0);
	vec3 viewPos = viewPos4.xyz;

	// Early depth culling
	if (viewPos.z > -0.1)
	{
		KillSplat();
		return;
	}

	// Build 3D covariance matrix
	mat3 R = QuatToMat3(rot);
	mat3 S = mat3(
		scale.x, 0.0, 0.0,
		0.0, scale.y, 0.0,
		0.0, 0.0, scale.z
	);
	mat3 M = R * S;
	mat3 Sigma = M * transpose(M);

	// Projective Jacobian
	float rz = 1.0 / viewPos.z;
	float rz2 = rz * rz;
	float fx = ubo.focal.x;
	float fy = ubo.focal.y;

	mat3 J = mat3(
		fx * rz,    0.0,       -(fx * viewPos.x) * rz2,
		0.0,        fy * rz,   -(fy * viewPos.y) * rz2,
		0.0,        0.0,       0.0
	);

	// Transform 3D covariance to 2D
	mat3 W = mat3(ubo.view);
	mat3 T = W * Sigma * transpose(W);
	mat3 cov2Dm = J * T * transpose(J);

	// Low-pass filter for anti-aliasing
	cov2Dm[0][0] += 0.1;
	cov2Dm[1][1] += 0.1;

	vec3 cov2d = vec3(cov2Dm[0][0], cov2Dm[0][1], cov2Dm[1][1]);

	// Eigendecomposition
	float det = cov2d.x * cov2d.z - cov2d.y * cov2d.y;

	if (det <= 1e-6)
	{
		cov2d.x += 1e-6;
		cov2d.z += 1e-6;
		det = cov2d.x * cov2d.z - cov2d.y * cov2d.y;
	}

	float mid = 0.5 * (cov2d.x + cov2d.z);
	float discriminant = max(0.0, mid * mid - det);
	float lambda1 = mid + sqrt(discriminant);
	float lambda2 = mid - sqrt(discriminant);
	lambda1 = max(lambda1, 1e-8);
	lambda2 = max(lambda2, 1e-8);

	float radius1 = 3.0 * sqrt(lambda1);
	float radius2 = 3.0 * sqrt(lambda2);
	float minPix = 0.5;
	radius1 = max(radius1, minPix);
	radius2 = max(radius2, minPix);

	// Vector-based eigendecomposition
	float a = cov2d.x, b = cov2d.y, c = cov2d.z;
	float two_b = 2.0 * b;
	float diff  = a - c;
	// sqrt(diff*diff + two_b*two_b)
	float r = length(vec2(two_b, diff));
	r = max(r, 1e-20);

	float cosT = sqrt( max(0.0, (r + diff) / (2.0 * r)) );
	float sinT = sqrt( max(0.0, (r - diff) / (2.0 * r)) );
	sinT = (b >= 0.0) ? sinT : -sinT;
	vec2 axis1 = vec2(cosT, sinT);
	vec2 axis2 = vec2(-axis1.y, axis1.x);

	// Frustum culling
	vec4 centerClip = ubo.projection * vec4(viewPos, 1.0);
	vec2 centerNDC = centerClip.xy / centerClip.w;

	float maxRadius = max(radius1, radius2);
	vec2 ndcMargin = (maxRadius / ubo.viewport) * 2.0;

	if (any(lessThan(centerNDC, vec2(-1.3) - ndcMargin)) ||
	    any(greaterThan(centerNDC, vec2(1.3) + ndcMargin)))
	{
		KillSplat();
		return;
	}

	vec2 cornerUnit;
	switch(vertexId)
	{
		case 0: cornerUnit = vec2(-1.0, -1.0); break;
		case 1: cornerUnit = vec2( 1.0, -1.0); break;
		case 2: cornerUnit = vec2( 1.0,  1.0); break;
		case 3: cornerUnit = vec2(-1.0,  1.0); break;
	}

	vec2 screenOffset = cornerUnit.x * radius1 * axis1 + cornerUnit.y * radius2 * axis2;
	vec2 offsetClip = (screenOffset / ubo.viewport) * 2.0 * centerClip.w;

	gl_Position = centerClip + vec4(offsetClip, 0.0, 0.0);

	outColor = color;
	outUV = cornerUnit;
}