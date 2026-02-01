# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Mobile 3D Gaussian Splatting - A cross-platform real-time 3D Gaussian Splatting renderer using a custom Rendering Hardware Interface (RHI) with Vulkan backend. Targets desktop (Windows, macOS via MoltenVK, Linux) and mobile (Android, iOS planned).

Key innovation: Hybrid transparency approach to eliminate sorting artifacts ("popping") combined with perspective-correct splat rendering.

## Build Commands

```bash
# Configure (auto-detects platform, uses Release by default)
python scripts/configure.py

# Configure Debug with Vulkan validation layers
python scripts/configure.py --clean --build-type Debug --validation

# Build and run a target
python scripts/configure.py build --target triangle --run
python scripts/configure.py build --target hybrid-splat-renderer --run

# Build and run all tests
python scripts/configure.py build --tests --run

# Build all executable targets
python scripts/configure.py build --target all

# List available targets
python scripts/configure.py build --list-targets

# Verbose mode for debugging build issues
python scripts/configure.py build --target triangle --verbose
```

### Android Build

```bash
# Build debug APK (auto-detects SDK/JDK from env vars or Android Studio)
python scripts/configure.py android --build-type debug

# With explicit paths
python scripts/configure.py android --sdk-path ~/Android/Sdk --jdk-path /path/to/jdk

# Install to device
adb install android/app/build/outputs/apk/debug/app-debug.apk
```

### Running Single Tests

```bash
# Build tests first
python scripts/configure.py build --tests

# Run with pattern matching
build/bin/Debug/unit-tests 'vector_*'
build/bin/Debug/perf-tests
```

## Architecture

### Four Static Libraries

1. **core** (`include/msplat/core/`, `src/core/`) - Foundational utilities with no graphics dependencies
   - Math: GLM wrapper providing `vec2/3/4`, `mat3/4`, `quat`, `AABB`, `Sphere`, `Frustum`, `Affine`, `Color`
   - PMR Containers: `msplat::core::vector`, `unordered_map`, `string` with mimalloc backend
   - Logging: `LOG_DEBUG/INFO/WARNING/ERROR/FATAL` macros via spdlog
   - VFS: `IBlob`, `IStream`, `IFileSystem` interfaces
   - Platform: `aligned_malloc`, `get_page_size`, `get_cache_line_size`, `get_backtrace`

2. **engine** (`include/msplat/engine/`, `src/engine/`) - 3DGS-specific functionality
   - `SplatSoA`: Struct-of-Arrays for GPU-efficient splat data
   - `SplatLoader`: Async PLY file loading via miniply
   - `CPUSplatSorter` / `GPUSplatSorter`: Depth sorting implementations
   - `Scene`, `MeshGenerator`, `ShaderFactory`

3. **app** (`include/msplat/app/`, `src/app/`) - Application framework
   - `IApplication`: Lifecycle interface (init, update, render, shutdown)
   - `DeviceManager`: RHI device lifecycle
   - `Camera`: View/projection with frustum culling
   - Platform adapters: `DesktopAdapter`, `AndroidAdapter`

4. **RHI** (`rhi/`) - Rendering Hardware Interface (Vulkan backend)
   - Core: `IRHIDevice`, `IRHIBuffer`, `IRHITexture`, `IRHIPipeline`, `IRHICommandList`
   - `RefCntPtr<T>` smart pointers for automatic resource lifecycle
   - Handle-based API (BufferHandle, TextureHandle, PipelineHandle)
   - VMA for GPU memory management

### Shader Pipeline

- **Language**: HLSL compiled to SPIR-V
- **Location**: `shaders/` with shared C++/HLSL structs in `shaderio.h`
- **Key shaders**:
  - `splat_raster.{vert,frag}.hlsl` - Gaussian splat rendering
  - `radix_*.comp.hlsl` - GPU radix sort (histogram, prefix scan, scatter)
  - `depth_calc.comp.hlsl` - Splat depth calculation

### Executable Targets

- `triangle` - Vulkan triangle (RHI validation)
- `particles` - GPU compute particle simulation
- `hybrid-splat-renderer` - Main 3DGS renderer with CPU/GPU sorting modes
- `splat-loader` - PLY file loader tool
- `scene-test` - Scene management testing
- `unit-tests`, `perf-tests`, `rhi-tests` - Test suites

## Code Patterns

### Use Project Types (Not Raw GLM/STL)

```cpp
#include <msplat/core/math/types.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>

using namespace msplat::math;
using namespace msplat::core;

vec3 position{1.0f, 2.0f, 3.0f};
mat4 transform = mat4(1.0f);
vector<uint32_t> indices;
LOG_INFO("Position: ({}, {}, {})", position.x, position.y, position.z);
```

### RHI Resource Management

```cpp
#include <rhi/rhi.h>

// Resources use RefCntPtr for automatic cleanup
BufferHandle buffer = device->CreateBuffer(bufferDesc);
PipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);

// No manual release needed - automatic when RefCntPtr goes out of scope
```

### PMR Container Construction

```cpp
// Use factory functions for PMR integration
auto vec = msplat::core::make_vector<float>();
auto map = msplat::core::make_unordered_map<uint32_t, SplatData>();
```

## Platform Notes

| Platform | Backend | Notes |
|----------|---------|-------|
| Windows | Vulkan | Visual Studio 2022 |
| macOS | MoltenVK | Xcode, Intel/Apple Silicon |
| Linux | Vulkan | GCC 9+, Clang 10+ |
| Android | Vulkan 1.0+ | NDK r27+, API 24+, dynamic function loading |
| iOS | Metal (planned) | Not yet implemented |

## Dependencies

Bundled in `third-party/`:
- glfw, glm, spdlog, mimalloc, miniply, rapidhash, imgui, unordered_dense

In `rhi/third-party/`:
- Vulkan-Headers, VMA (Vulkan Memory Allocator)

External:
- Vulkan SDK 1.3.250+
- CMake 3.20+
- Python 3.7+
