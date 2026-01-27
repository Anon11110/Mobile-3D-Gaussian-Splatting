# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## MCP Tool Usage Guidelines

* **`Serena`**: **If** you need to understand, navigate, or refactor existing code (like renaming a function or finding its references), use `Serena` for semantic analysis.

* **`Context7`**: **Whenever** you encounter an `import` statement or need to use a third-party library, use `Context7` to get its official, version-specific documentation.

* **`Magic`**: **When** the task is to generate or refine a UI component (like a button, form, or modal), **always** use `Magic`, especially with the `/ui` command.

* **`sequential-thinking`**: For any complex problem, system design task, or difficult debugging session, **always** use `sequential-thinking` first to break it down into logical steps.

* **`Playwright`**: **If** the task requires end-to-end testing, visual validation, or interacting with a live website, use `Playwright` to perform browser automation.

## Project Overview

### TL;DR

Mobile 3D Gaussian Splatting - A cross-platform implementation of 3D Gaussian splatting using a custom Rendering Hardware Interface (RHI) with Vulkan backend. The project targets both desktop (Windows, macOS via MoltenVK, Linux) and eventually mobile platforms (Android, iOS).

### Background
3D Gaussian Splatting (3DGS) has revolutionized real-time rendering and novel view synthesis, offering unprecedented quality and performance for photorealistic scene reconstruction. However, deploying these cutting-edge techniques on resource-constrained mobile devices presents significant challenges, from managing immense data loads to overcoming performance bottlenecks in the graphics pipeline. Furthermore, standard 3DGS implementations often suffer from visual artifacts, such as "popping" caused by incorrect transparency sorting, which can degrade the user experience.

This project, a mobile-first 3D Gaussian Splatting renderer, is designed to overcome these limitations. The project's core innovation lies in its architecture, which draws inspiration from two key sources: state-of-the-art research like "Efficient Perspective-Correct 3D Gaussian Splatting Using Hybrid Transparency" to achieve artifact-free, high-quality rendering, and proven production-grade systems like Unreal Engine's Nanite for virtualized geometry and GPU-driven performance.

The ultimate goal is to create a solution that not only renders complex 3DGS scenes in real-time on mobile devices but does so with a level of quality and performance that pushes the boundaries of what is possible in mobile graphics.

### Objectives
1. Build a Robust, Cross-Platform Application Framework: Create the foundational core and application libraries to support a modular architecture. The first priority is to rigorously validate the rhi with a comprehensive bootstrap example that proves its capability to handle advanced compute-driven rendering workflows.
2. Achieve a High-Quality and Correct Rendering Core: Develop the engine and render libraries to create a functional 3DGS viewer. This involves not just displaying splats, but implementing advanced techniques such as hybrid transparency to eliminate sorting-related popping artifacts and ray-based evaluation for perspective-correct splat rendering, ensuring visual fidelity.
3. Implement a Nanite-Inspired Virtualized Geometry Pipeline: Evolve the renderer from a simple, CPU-driven model to a high-performance, GPU-driven system. This tertiary priority focuses on virtualizing the splat geometry through hierarchical clustering (DAGs), and implementing GPU-side culling and Level-of-Detail (LOD) selection to render scenes of immense complexity at real-time frame rates.
4. Deliver Native, High-Performance Mobile Implementations: Port the final, optimized renderer to both Android (leveraging the Vulkan backend) and iOS (by creating a new Metal backend for the rhi), ensuring the architecture performs efficiently on mobile-specific GPU hardware.

## Build System

**IMPORTANT: Always use absolute paths and proper working directories for robust operations.**

### Cross-Platform Configuration & Building

The enhanced configure script provides unified cross-platform build management with auto-configuration capabilities:

```bash
# Cross-platform relative paths (when working from project root)
python3 scripts/configure.py --build-type Debug --validation
python3 scripts/configure.py build --target triangle --run

# Cross-platform absolute paths (recommended for Claude Code)
/usr/bin/python3 ~/work/projects/Mobile-3D-Gaussian-Splatting/scripts/configure.py --build-type Debug --validation
/usr/bin/python3 ~/work/projects/Mobile-3D-Gaussian-Splatting/scripts/configure.py build --target triangle --run

# Use verbose mode when debugging build issues
/usr/bin/python3 ~/work/projects/Mobile-3D-Gaussian-Splatting/scripts/configure.py --verbose --build-type Debug
/usr/bin/python3 ~/work/projects/Mobile-3D-Gaussian-Splatting/scripts/configure.py build --target triangle --verbose
```

### Common Build Workflows

```bash
# Quick start workflow
python3 scripts/configure.py
python3 scripts/configure.py build --target triangle --run

# Development workflow with auto-configuration
python3 scripts/configure.py build --target triangle --run  # Auto-configures if needed

# Testing workflow
python3 scripts/configure.py build --tests --run

# List available targets
python3 scripts/configure.py build --list-targets

# Clean rebuild
python3 scripts/configure.py --clean --build-type Debug
python3 scripts/configure.py build --target all  # Builds executable targets and RHI only

# Debug build issues with verbose output
python3 scripts/configure.py --verbose --build-type Debug --validation
python3 scripts/configure.py build --target triangle --verbose
```

### Available Build Targets

#### Executable Targets (included in --target all)
- `triangle` - Vulkan triangle example (main executable)
- `particles` - GPU particle simulation with compute shaders
- `splat-loader` - 3D Gaussian Splatting PLY file loader
- `scene-test` - Scene management system test example
- `hybrid-splat-renderer` - Gaussian splat renderer supporting both CPU and GPU sorting modes
- `unit-tests` - Core library unit tests
- `perf-tests` - Performance benchmarks
- `rhi-tests` - RHI unit tests (only available when configured with --tests)

#### Library Targets (not included in --target all)
- `RHI` - Rendering Hardware Interface with Vulkan backend (exception: included in --target all)
- `core` - Core static library (math, logging, timer, VFS, platform)
- `engine` - Engine static library (splat loading and data structures)
- `app` - Application framework library (IApplication interface, DeviceManager, Camera)

### Build Shortcuts & Features
- `--tests`: Builds unit-tests, perf-tests, and rhi-tests (if configured with --tests flag)
- `--target all`: Builds all executable targets from examples/ plus RHI library (filters out static libraries and shader targets)
- `--run`: Runs executable after building (single target only)
- `--verbose`: Shows detailed build output for debugging
- Auto-configuration: Automatically configures project if not already configured
- Cross-platform generators: Automatically selects appropriate generator per platform
- Enhanced target discovery: Intelligently discovers targets from CMakeLists.txt files
- Output filtering: Smart filtering of build output in non-verbose mode

### Target Filtering & Validation
- `--target all` automatically filters out static libraries (core, app, engine, glfw) and shader compilation targets (*_shaders)
- Target validation ensures only existing targets are built
- Clear error messages show available targets when invalid targets are specified
- `rhi-tests` is dynamically included when available (requires --tests during configuration)

### Build Output Control

The build system provides two output modes:

**Quiet Mode (Default):**
- Clean, minimal output showing progress and results
- Ideal for routine builds and CI/CD environments
- Example output: `🔨 Building target: triangle` → `✅ Built triangle in 2.3s`

**Verbose Mode (`--verbose` flag):**
- Shows complete build tool output (CMake, Xcode, MSBuild, Make, Ninja)
- Essential for debugging build failures or configuration issues
- Cross-platform: Works with all generators and build systems

**Error Handling:**
- **Errors are always displayed** regardless of verbose setting
- Build failures show complete error output even in quiet mode
- Ensures debugging information is never hidden

```bash
# Quiet mode (default) - clean output
python3 scripts/configure.py build --target triangle

# Verbose mode - full build details for debugging
python3 scripts/configure.py build --target triangle --verbose
python3 scripts/configure.py --verbose --build-type Debug --validation
```

### Platform-Specific Considerations

**Windows:**
```bash
# Uses Visual Studio 2022 by default
python3 scripts/configure.py --generator "Visual Studio 17 2022"
python3 scripts/configure.py --generator "Ninja" --build-type Debug  # Alternative
```

**macOS:**
```bash
# Uses Xcode by default
python3 scripts/configure.py --generator "Xcode"
python3 scripts/configure.py --generator "Unix Makefiles" --build-type Release  # Alternative
```

**Linux:**
```bash
# Uses Unix Makefiles by default
python3 scripts/configure.py --generator "Unix Makefiles" --build-type Debug
python3 scripts/configure.py --generator "Ninja" --build-type Release  # Alternative
```

### IDE Integration

The configure script supports seamless IDE integration across platforms:

**Windows - Visual Studio:**
```bash
# Generate Visual Studio solution (default)
python3 scripts/configure.py --build-type Debug --validation
start build/Mobile-3D-Gaussian-Splatting.sln  # triangle is set as startup project
```

**macOS - Xcode:**
```bash
# Generate Xcode project
python3 scripts/configure.py --generator "Xcode" --build-type Debug --validation
open build/Mobile-3D-Gaussian-Splatting.xcodeproj
```

**Linux/macOS - IDE with LSP:**
```bash
# Generate compile_commands.json for VS Code, CLion, etc.
python3 scripts/configure.py --generator "Unix Makefiles" --build-type Debug --validation
# compile_commands.json is automatically generated for Debug builds
```

### Alternative Build Methods

**Direct CMake Commands:**
```bash
# Multi-config generators (Visual Studio, Xcode)
cmake --build build --config Debug --target triangle --parallel

# Single-config generators (Ninja, Unix Makefiles)
cmake --build build --target triangle --parallel

# Manual CMake configuration (not recommended)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_VULKAN_VALIDATION=ON
cmake --build build --target triangle --parallel
```

**Note**: The configure script handles platform detection, dependency verification, and proper environment setup. Direct CMake usage requires manual configuration.

### Running Executables
The build script automatically handles proper working directories when using `--run`. For manual execution:

```bash
# The script handles this automatically, but for manual runs:
cd build/bin/Debug && ./triangle      # Debug builds
cd build/bin/Release && ./triangle    # Release builds
```

**Why proper working directories matter:**
- Executables find relative assets (shaders, configs, etc.)
- Consistent behavior across different terminal sessions
- No dependency on current working directory when launched
- Matches project's asset loading expectations

## Architecture

### Core Library (`/include/msplat/core/`, `/src/core/`)

Foundational static library providing essential utilities with no dependencies on graphics/RHI code:

- **Math Library**: GLM wrapper in `msplat::math` namespace
  - Header-only implementation in `/include/msplat/core/math/`
  - Provides `vec2`, `vec3`, `vec4`, `mat3`, `mat4`, `quat`, etc.
  - Includes specialized types: `Affine`, `AABB`, `Sphere`, `Frustum`, `Color`

- **Memory Management**: Advanced memory utilities in `/include/msplat/core/memory/`
  - `frame_arena.h` for frame-based memory allocation
  - Integration with mimalloc for high-performance allocation

- **Container Wrappers**: STL container wrappers in `/include/msplat/core/containers/`
  - `vector` with direct size constructors for pre-allocation (NEW)
  - `array`, `unordered_map`, `unordered_set`, `queue`, `string`
  - `hash` utilities with rapidhash integration
  - `filesystem` abstractions for path operations
  - `functional` and `memory` utilities

- **Virtual File System**: VFS abstraction in `msplat::core` namespace
  - `vfs.h/cpp` for unified file access across platforms
  - Support for asset loading from various sources

- **Logging System**: Severity-based logging in `msplat::log` namespace
  - Macros: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`, `LOG_FATAL`
  - Automatic build-type detection (Debug shows all, Release hides DEBUG)
  - Extensible backend system with default `ConsoleBackend`

- **Timing Utilities**: High-resolution timing in `msplat::timer` namespace
  - `Timer` class with multiple time units (ns, μs, ms, seconds)
  - `FPSCounter` for smooth frame rate calculation

- **Platform Abstraction**: Cross-platform memory and system utilities in `msplat::core` namespace
  - `aligned_malloc/aligned_free` for aligned memory allocation
  - `get_page_size()` for system page size detection
  - `get_cache_line_size()` for CPU cache optimization
  - `get_backtrace()` for debug stack traces (debug builds only)
  - `windows_sanitized.h` for clean Windows header inclusion
  - Supports Windows, macOS, Linux, iOS, Android

### Engine Library (`/include/msplat/engine/`, `/src/engine/`)

High-level functionality for 3D Gaussian Splatting:

- **Splat Data Structures**: Structure-of-Arrays (SoA) optimized data layout in `msplat::engine` namespace
  - `SplatSoA` for efficient GPU processing
  - `SplatMesh` for mesh representation of splats
  - Memory-aligned storage for better cache performance

- **Splat Sorting**: GPU and CPU sorting implementations
  - `SplatSorter` abstract interface for sorting strategies
  - `GPUSplatSorter` for GPU-accelerated depth sorting
    - **NEW**: Hierarchical GPU radix sort implementation (5-pass algorithm)
    - Compute shader-based depth calculation (`depth_calc.comp`)
    - Hierarchical prefix scan for efficient parallel processing
    - Support for 10,000+ splats with verification system
  - Support for perspective-correct sorting algorithms

- **PLY Loading**: Asynchronous PLY file loading with `SplatLoader`
  - Multi-threaded loading for large datasets
  - Support for standard 3D Gaussian Splatting PLY format
  - Progress tracking and error handling

- **Scene Management**: Scene graph and mesh generation utilities
  - `Scene` class for managing 3D objects and transformations
  - `MeshGenerator` for procedural geometry creation
  - `ShaderFactory` for shader compilation and management

### App Library (`/include/msplat/app/`, `/src/app/`)

Application framework providing high-level abstractions:

- **Application Interface**: `IApplication` base class in `msplat::app` namespace
  - Lifecycle management (init, update, render, shutdown)
  - Event handling and input processing

- **Device Management**: `DeviceManager` for RHI device lifecycle
  - Device creation and configuration
  - Swapchain management
  - Resource allocation

- **Camera System**: `Camera` class for 3D view management
  - View and projection matrix generation
  - Input-based camera controls
  - Frustum culling support

### RHI (Rendering Hardware Interface)

Abstraction layer for GPU rendering located in `/rhi/`:
- Uses Vulkan as primary backend (including MoltenVK for macOS)
- Interface designed to support future Metal backend
- Key interfaces: `IRHIDevice`, `IRHIBuffer`, `IRHIPipeline`, `IRHICommandList`, `IRHISwapchain`
- **Reference Counting System**: COM-style intrusive reference counting with `IRefCounted` interface
  - All resources use `RefCounter<T>` CRTP mixin for implementations
  - `RefCntPtr<T>` smart pointers for automatic lifecycle management
  - Handle-based API (BufferHandle, TextureHandle, PipelineHandle, etc.)
- **Dynamic Descriptors**: Support for UNIFORM_BUFFER_DYNAMIC and STORAGE_BUFFER_DYNAMIC
  - Enables efficient batch updates of descriptor data
  - Used in GPU sorter for histogram binding optimization
- Comprehensive test suite in `/rhi/tests/` for validation
- Architecture documentation in `/rhi/docs/` (RHI_DESIGN.md, RHI_HANDLE_ARCHITECTURE.md)

### Project Structure

```
/include/msplat/core/   - Core library headers (math, logging, timer, platform, memory, containers)
/include/msplat/engine/ - Engine library headers (splat data structures, sorting, loader, scene)
/include/msplat/app/    - Application framework headers (IApplication, DeviceManager, Camera)
/src/core/              - Core library implementation
/src/engine/            - Engine library implementation (splat loading, sorting, scene management)
/src/app/               - Application framework implementation
/assets/                - Test assets and data files
  /splats/              - PLY files for testing Gaussian splatting
/rhi/                   - Rendering Hardware Interface implementation
  /include/             - Public RHI headers and interfaces
    /rhi/               - Main RHI interfaces
    /common/            - Reference counting infrastructure
  /src/
    /backends/vulkan/   - Vulkan backend implementation (14 files)
  /tests/               - RHI-specific unit tests
  /docs/                - RHI architecture documentation
  /third-party/         - External RHI dependencies
    /VMA/               - Vulkan Memory Allocator
    /Vulkan-Headers/    - Vulkan headers
/shaders/               - GLSL shaders (compiled to SPIR-V)
  /compiled/            - Compiled SPIR-V output
/examples/
  /triangle/            - Vulkan triangle example
  /particles/           - GPU particle simulation
  /splat-loader/        - PLY file loading example
  /scene-test/          - Scene management test example
  /hybrid-splat-renderer/ - Gaussian splat renderer supporting both CPU and GPU sorting modes
  /unit-tests/          - Unit tests
  /perf-tests/          - Performance benchmarks
/docs/                  - Design and implementation documentation
/scripts/               - Build configuration and utilities
/third-party/           - External dependencies (GLFW, ImGui, GLM, miniply, mimalloc, spdlog, rapidhash, unordered_dense)
```

Current focus is on Phase 2 implementation with active GPU sorting development.

## Key Technical Decisions

- **Vulkan-only approach**: Using Vulkan on all platforms (MoltenVK for macOS/iOS) instead of separate Metal backend
- **SPIR-V shaders**: Single shader format that works across all platforms
- **Command buffer model**: Modern GPU API pattern for efficient rendering
- **Desktop-first development**: Rapid prototyping on desktop before mobile optimization

## Performance Targets

- Desktop: 500K Gaussians at 60 FPS
- Mobile: 100K Gaussians at 30 FPS
- Memory usage: < 200MB on mobile, < 1GB on desktop

## Code Integration Patterns

When working with this codebase:

1. **Use Core Library Types**: Always use `msplat::math::vec3`, `msplat::math::mat4`, etc. instead of direct GLM types
2. **Container Wrappers**: Use `msplat::core::vector`, `msplat::core::unordered_map`, etc. from `/include/msplat/core/containers/`
3. **Consistent Logging**: Use `LOG_INFO()`, `LOG_ERROR()`, etc. macros instead of `std::cout`
4. **Timing Measurements**: Use `msplat::timer::Timer` and `FPSCounter` for performance monitoring
5. **Platform Utilities**: Use `msplat::core::AlignedMalloc()`, `msplat::core::GetPageSize()`, etc. for cross-platform operations (Note: Uses CamelCase naming)
6. **Namespace Conventions**: Core utilities are in flat namespaces (`msplat::math`, `msplat::log`, `msplat::timer`, `msplat::core`, `msplat::app`)
7. **Resource Management**: All RHI resources use handle-based reference counting with `RefCntPtr<T>` smart pointers

## Key Implementation Files

- `/cmake/core.cmake` - Core library CMake configuration
- `/cmake/engine.cmake` - Engine library CMake configuration
- `/cmake/app.cmake` - Application framework CMake configuration
- `/examples/triangle/main.cpp` - Vulkan rendering example with RHI
- `/examples/particles/main.cpp` - GPU compute shader example
- `/examples/splat-loader/main.cpp` - PLY loading and analysis example
- `/examples/hybrid-splat-renderer/main.cpp` - Hybrid renderer with CPU/GPU sorting modes
- `/shaders/depth_calc.comp` - Compute shader for splat depth calculation
- `/shaders/radix_histogram.comp` - Histogram generation for radix sort
- `/shaders/radix_prefix_scan.comp` - Hierarchical prefix scan implementation
- `/shaders/radix_scatter_pairs.comp` - Key-value pair scattering for sort
- `/docs/CORE_LIBRARY_DESIGN.md` - Core library architecture documentation
- `/rhi/docs/RHI_DESIGN.md` - RHI design and architecture documentation
- `/scripts/utils/configure/CONFIGURE_MODULE_ARCHITECTURE.md` - Build system module architecture

## Dependencies

- **Vulkan SDK**: 1.3.250+ (includes MoltenVK for macOS)
- **GLFW**: 3.3.8+ for windowing
- **GLM**: Math library (wrapped in core library)
- **VMA (Vulkan Memory Allocator)**: Memory management for Vulkan (located in `rhi/third-party/VMA`)
- **ImGui**: Debug UI (in third-party)
- **mimalloc**: Microsoft's high-performance memory allocator (integrated in core)
- **spdlog**: Fast C++ logging library (integrated in core)
- **miniply**: Lightweight PLY file reader (integrated in engine)
- **rapidhash**: Fast hashing library for containers (in third-party)
- **unordered_dense**: High-performance unordered containers (in third-party)