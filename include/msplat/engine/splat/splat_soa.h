#pragma once

#include <cstddef>
#include <cstdint>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>

namespace msplat::engine
{

struct SplatSoA
{
	container::vector<float> posX;
	container::vector<float> posY;
	container::vector<float> posZ;

	container::vector<float> scaleX;
	container::vector<float> scaleY;
	container::vector<float> scaleZ;

	container::vector<float> rotX;
	container::vector<float> rotY;
	container::vector<float> rotZ;
	container::vector<float> rotW;

	// Pre-computed 3D covariance (symmetric 3x3 matrix, upper triangle + diagonal)
	container::vector<float> cov3D_M11;
	container::vector<float> cov3D_M12;
	container::vector<float> cov3D_M13;
	container::vector<float> cov3D_M22;
	container::vector<float> cov3D_M23;
	container::vector<float> cov3D_M33;

	container::vector<float> opacity;

	container::vector<float> fDc0;
	container::vector<float> fDc1;
	container::vector<float> fDc2;

	container::vector<float> fRest;

	uint32_t numSplats        = 0;
	uint32_t shDegree         = 0;
	uint32_t shCoeffsPerSplat = 0;

#if defined(MSPLAT_USE_STD_CONTAINERS) || defined(__ANDROID__)
	// System STL uses std::vector which doesn't support custom allocators in this way
	SplatSoA() = default;
#else
	explicit SplatSoA(std::pmr::memory_resource *memres = nullptr) :
	    posX(memres), posY(memres), posZ(memres), scaleX(memres), scaleY(memres), scaleZ(memres), rotX(memres), rotY(memres), rotZ(memres), rotW(memres), cov3D_M11(memres), cov3D_M12(memres), cov3D_M13(memres), cov3D_M22(memres), cov3D_M23(memres), cov3D_M33(memres), opacity(memres), fDc0(memres), fDc1(memres), fDc2(memres), fRest(memres)
	{
	}
#endif

	void Resize(uint32_t count, uint32_t coeffsPerSplat)
	{
		numSplats        = count;
		shCoeffsPerSplat = coeffsPerSplat;

		if (coeffsPerSplat == 45)
		{
			shDegree = 3;
		}
		else if (coeffsPerSplat == 15)
		{
			shDegree = 2;
		}
		else if (coeffsPerSplat == 3)
		{
			shDegree = 1;
		}
		else
		{
			shDegree = 0;
		}

		posX.resize(count);
		posY.resize(count);
		posZ.resize(count);

		scaleX.resize(count);
		scaleY.resize(count);
		scaleZ.resize(count);

		rotX.resize(count);
		rotY.resize(count);
		rotZ.resize(count);
		rotW.resize(count);

		cov3D_M11.resize(count);
		cov3D_M12.resize(count);
		cov3D_M13.resize(count);
		cov3D_M22.resize(count);
		cov3D_M23.resize(count);
		cov3D_M33.resize(count);

		opacity.resize(count);

		fDc0.resize(count);
		fDc1.resize(count);
		fDc2.resize(count);

		fRest.resize(count * coeffsPerSplat);
	}

	void Clear() noexcept
	{
		posX.clear();
		posY.clear();
		posZ.clear();

		scaleX.clear();
		scaleY.clear();
		scaleZ.clear();

		rotX.clear();
		rotY.clear();
		rotZ.clear();
		rotW.clear();

		cov3D_M11.clear();
		cov3D_M12.clear();
		cov3D_M13.clear();
		cov3D_M22.clear();
		cov3D_M23.clear();
		cov3D_M33.clear();

		opacity.clear();

		fDc0.clear();
		fDc1.clear();
		fDc2.clear();

		fRest.clear();

		numSplats        = 0;
		shDegree         = 0;
		shCoeffsPerSplat = 0;
	}

	bool empty() const noexcept
	{
		return numSplats == 0;
	}

	// Returns the total CPU memory used by all vectors in bytes
	size_t GetCpuMemoryUsage() const noexcept
	{
		size_t total = 0;
		// Position: 3 vectors
		total += posX.capacity() * sizeof(float);
		total += posY.capacity() * sizeof(float);
		total += posZ.capacity() * sizeof(float);
		// Scale: 3 vectors
		total += scaleX.capacity() * sizeof(float);
		total += scaleY.capacity() * sizeof(float);
		total += scaleZ.capacity() * sizeof(float);
		// Rotation: 4 vectors
		total += rotX.capacity() * sizeof(float);
		total += rotY.capacity() * sizeof(float);
		total += rotZ.capacity() * sizeof(float);
		total += rotW.capacity() * sizeof(float);
		// Covariance: 6 vectors
		total += cov3D_M11.capacity() * sizeof(float);
		total += cov3D_M12.capacity() * sizeof(float);
		total += cov3D_M13.capacity() * sizeof(float);
		total += cov3D_M22.capacity() * sizeof(float);
		total += cov3D_M23.capacity() * sizeof(float);
		total += cov3D_M33.capacity() * sizeof(float);
		// Opacity: 1 vector
		total += opacity.capacity() * sizeof(float);
		// DC coefficients: 3 vectors
		total += fDc0.capacity() * sizeof(float);
		total += fDc1.capacity() * sizeof(float);
		total += fDc2.capacity() * sizeof(float);
		// SH Rest coefficients
		total += fRest.capacity() * sizeof(float);
		return total;
	}
};

}        // namespace msplat::engine