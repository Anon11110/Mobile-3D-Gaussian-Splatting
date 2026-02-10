// This compute shader simulates particle physics for a dual-emitter system.
// It uses a "flow field" composed of cross-emitter attraction and curl noise
// to create an organic, swirling smoke effect between the two emitters.

// Particle data structure
struct Particle
{
	float3 position;
	float  padding1;
	float3 velocity;
	float  padding2;
};

// Uniform buffer with simulation parameters
struct SimulationParams
{
	float  deltaTime;
	float  time;        // Global time for animating noise
	float2 padding1;
	// Emitter Sphere 1
	float3 sphere1Pos;
	float  sphereRadius;
	float3 sphere1Vel;
	float  padding2;
	// Emitter Sphere 2
	float3 sphere2Pos;
	float  padding3;
	float3 sphere2Vel;
	float  padding4;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SimulationParams> params : register(b0);

// Input particle buffer (read-only)
[[vk::binding(1, 0)]]
StructuredBuffer<Particle> particlesIn : register(t0);

// Output particle buffer (write-only)
[[vk::binding(2, 0)]]
RWStructuredBuffer<Particle> particlesOut : register(u0);

// --- UTILITY FUNCTIONS ---

// PCG hash for pseudo-random number generation
uint pcg_hash(uint seed)
{
	uint state = seed * 747796405u + 2891336453u;
	uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

// Convert hash to a float in [-1, 1]
float hash_to_float(uint hash)
{
	return float(hash) * (1.0 / 2147483648.0) - 1.0;
}

// Convert hash to a float in [0, 1]
float hash_to_float01(uint hash)
{
	return float(hash) * (1.0 / 4294967296.0);
}

// Generates a 3D random vector using the hash function
float3 random_vec3(uint seed)
{
	return float3(
	    hash_to_float(pcg_hash(seed)),
	    hash_to_float(pcg_hash(seed + 1000u)),
	    hash_to_float(pcg_hash(seed + 2000u)));
}

// --- NOISE AND FORCE FUNCTIONS ---

// Simplex-like noise function for creating the base of the curl noise
float noise(float3 x)
{
	float3 p    = floor(x);
	float3 f    = frac(x);
	f           = f * f * (3.0 - 2.0 * f);
	uint seed   = uint(p.x) + uint(p.y * 157.0) + uint(p.z * 113.0);
	return lerp(lerp(lerp(hash_to_float01(pcg_hash(seed)), hash_to_float01(pcg_hash(seed + 1u)), f.x),
	                 lerp(hash_to_float01(pcg_hash(seed + 157u)), hash_to_float01(pcg_hash(seed + 158u)), f.x), f.y),
	            lerp(lerp(hash_to_float01(pcg_hash(seed + 113u)), hash_to_float01(pcg_hash(seed + 114u)), f.x),
	                 lerp(hash_to_float01(pcg_hash(seed + 270u)), hash_to_float01(pcg_hash(seed + 271u)), f.x), f.y),
	            f.z);
}

// Curl noise generates a divergence-free vector field, ideal for fluid-like motion.
// It's calculated by taking the curl of a noise field.
float3 curlNoise(float3 pos)
{
	const float e  = 0.1;
	float       n1 = noise(pos + float3(0.0, e, 0.0));
	float       n2 = noise(pos - float3(0.0, e, 0.0));
	float       n3 = noise(pos + float3(0.0, 0.0, e));
	float       n4 = noise(pos - float3(0.0, 0.0, e));
	float       n5 = noise(pos + float3(e, 0.0, 0.0));
	float       n6 = noise(pos - float3(e, 0.0, 0.0));

	float x = n1 - n2 - n3 + n4;
	float y = n3 - n4 - n5 + n6;
	float z = n5 - n6 - n1 + n2;

	return normalize(float3(x, y, z));
}

[numthreads(64, 1, 1)]
void main(uint3 globalId : SV_DispatchThreadID)
{
	uint index = globalId.x;
	uint particleCount;
	uint stride;
	particlesIn.GetDimensions(particleCount, stride);

	if (index >= particleCount)
	{
		return;
	}

	Particle p = particlesIn[index];

	// --- PARTICLE EMISSION AND RECYCLING ---

	// Assign particle to an emitter (0 or 1)
	float  selector       = float(index & 1u);
	float3 emissionCenter = lerp(params.sphere1Pos, params.sphere2Pos, selector);
	float3 sphereVelocity = lerp(params.sphere1Vel, params.sphere2Vel, selector);

	uint   seed    = index + uint(params.time * 1000.0);
	float3 randVec = random_vec3(seed);

	float distFromCenter = length(p.position - emissionCenter);

	// Recycle particle if it's too far from the center of the simulation,
	// has a very low velocity, or on the first frame.
	const float recycleDist  = 7.0;
	bool        needsRecycle = (distFromCenter > recycleDist) || (length(p.velocity) < 0.01 && distFromCenter > 1.0) || (params.time < 0.1);

	if (needsRecycle)
	{
		// Generate a random point on a slightly flattened sphere for emission
		float theta = randVec.x * 3.14159265;
		float phi   = acos(2.0 * randVec.y * 0.5 - 1.0);
		float3 spherePoint;
		spherePoint.x = sin(phi) * cos(theta);
		spherePoint.y = sin(phi) * sin(theta);
		spherePoint.z = cos(phi);
		p.position    = emissionCenter + spherePoint * params.sphereRadius;

		// Give it an initial velocity directed away from the center plus emitter velocity
		p.velocity = normalize(randVec + float3(0, 0.5, 0)) * 2.0 + sphereVelocity;
	}

	// --- SMOKE FLOW FORCE FIELD ---

	// 1. Cross-Attraction: Particles are attracted to the *other* sphere
	const float crossAttractionStrength = 0.8;
	float3      flowTarget              = lerp(params.sphere2Pos, params.sphere1Pos, selector);
	float3      toTarget                = flowTarget - p.position;
	float3      crossAttractionForce    = normalize(toTarget) * crossAttractionStrength;

	// 2. Curl Noise: Adds swirling, turbulent motion
	const float noiseFrequency = 0.5;
	const float noiseStrength  = 2.5;
	float3      noisePos       = p.position * noiseFrequency;
	noisePos.x += params.time * 0.1;        // Animate the noise field over time
	float3 noiseForce = curlNoise(noisePos) * noiseStrength;

	// 3. Center Attraction: A weak pull back to the simulation origin to keep it contained
	const float centerAttractionStrength = 0.1;
	float3      centerAttractionForce    = -p.position * centerAttractionStrength;

	// Combine all forces
	float3 totalForce = crossAttractionForce + noiseForce + centerAttractionForce;

	// --- INTEGRATION ---

	// Apply forces (Verlet integration)
	p.velocity += totalForce * params.deltaTime;

	// Apply damping (air resistance)
	const float damping = 0.98;
	p.velocity *= damping;

	// Clamp velocity to a maximum speed
	const float maxSpeed = 5.0;
	float       speed    = length(p.velocity);
	if (speed > maxSpeed)
	{
		p.velocity = p.velocity * (maxSpeed / speed);
	}

	// Update position
	p.position += p.velocity * params.deltaTime;

	// Write the updated particle data to the output buffer
	particlesOut[index] = p;
}
