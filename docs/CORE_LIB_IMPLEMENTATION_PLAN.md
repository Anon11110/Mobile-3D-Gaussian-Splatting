# Core Library Implementation Plan

## Overview
Create a foundational `core` static library for the 3D graphics engine with no dependencies on other engine modules. The library will provide essential utilities including math (GLM wrapper), logging, timing, and virtual file system functionality.

## Project Structure
- **Headers:** `include/core/[submodule]/[component].h`
- **Source:** `src/core/[submodule]/[component].cpp`
- **Build:** `cmake/core.cmake` (included by root CMakeLists.txt)
- **Testing:** Use `examples/triangle/main.cpp` as integration test

## Implementation Phases

### Phase 1: Math Foundation (GLM Wrapper)
**Goal:** Create comprehensive math library wrapping GLM within `core::math` namespace
**Status:** Not Started

**Tasks:**
1. Set up GLM dependency and CMake configuration
2. Create individual math component headers:
   - `vector.h` - vec2, vec3, vec4 types and operations
   - `matrix.h` - mat3, mat4 types and transformations
   - `quaternion.h` - Rotation quaternions
   - `affine.h` - Affine transform class
   - `aabb.h` - Axis-aligned bounding boxes
   - `sphere.h` - Bounding spheres
   - `frustum.h` - View frustum and projection matrices
   - `color.h` - Color types and utilities
   - `basics.h` - Constants (Pi, etc.) and utilities
3. Create main `math.h` convenience header
4. Update triangle example to use core::math types

**Deliverables:**
- All math headers in `include/core/math/`
- Implementation files in `src/core/math/` (if needed)
- Working triangle example using new math types

**Success Criteria:**
- All math types properly wrapped in core::math namespace
- Triangle example compiles and runs correctly
- No direct GLM includes needed in client code

### Phase 2: Logging System
**Goal:** Implement flexible logging system with macro-based interface
**Status:** Not Started

**Tasks:**
1. Design logger interface and implementation
2. Create `log.h` with LOG_INFO, LOG_WARN, LOG_ERROR macros
3. Implement console output backend
4. Add log level filtering
5. Design extensibility points for future backends
6. Integrate logging into triangle example

**Deliverables:**
- `include/core/log.h` - Public logging interface
- `src/core/log.cpp` - Logger implementation
- Updated triangle example with logging

**Success Criteria:**
- Logging macros work correctly
- Different log levels display appropriately
- Triangle example demonstrates logging

### Phase 3: Timing Utilities
**Goal:** Create lightweight timing utilities using std::chrono
**Status:** Not Started

**Tasks:**
1. Design Timer class interface
2. Implement start(), stop(), elapsed methods
3. Support multiple time units (ms, us, ns, seconds)
4. Add FPS calculation utilities
5. Replace manual timing in triangle example

**Deliverables:**
- `include/core/timer.h` - Timer interface
- `src/core/timer.cpp` - Timer implementation
- Updated triangle example using Timer

**Success Criteria:**
- Timer accurately measures elapsed time
- Triangle example FPS counter uses Timer class
- Multiple time unit conversions work correctly

### Phase 4: Virtual File System
**Goal:** Implement basic VFS with physical filesystem pass-through
**Status:** Not Started

**Tasks:**
1. Design VFS interface for future extensibility
2. Implement ReadFile function
3. Create physical filesystem backend
4. Add path resolution utilities
5. Replace file loading in triangle example (shader loading)

**Deliverables:**
- `include/core/vfs.h` - VFS interface
- `src/core/vfs.cpp` - VFS implementation
- Updated triangle example using VFS

**Success Criteria:**
- Files load correctly through VFS
- Triangle example shaders load via VFS
- Design supports future archive backends

### Phase 5: Build System Integration
**Goal:** Create proper CMake configuration for core library
**Status:** Not Started

**Tasks:**
1. Create `cmake/core.cmake` with library definition
2. Set up proper include directories
3. Configure GLM dependency
4. Add to root CMakeLists.txt
5. Ensure proper static library generation

**Deliverables:**
- `cmake/core.cmake` - Core library CMake configuration
- Updated root CMakeLists.txt
- Static library builds correctly

**Success Criteria:**
- Core library builds as static library
- Examples link successfully
- No circular dependencies

### Phase 6: Integration and Testing
**Goal:** Fully integrate core library with triangle example
**Status:** Not Started

**Tasks:**
1. Remove all direct std includes for replaced functionality
2. Update all math operations to use core::math
3. Replace all file I/O with VFS
4. Add comprehensive logging throughout
5. Use Timer for all timing operations
6. Verify no regressions in functionality

**Deliverables:**
- Fully integrated triangle example
- All core functionality demonstrated
- Clean separation of concerns

**Success Criteria:**
- Triangle example works identically to original
- All core subsystems properly utilized
- Code is cleaner and more maintainable

## Technical Specifications

### Namespace Structure
```cpp
namespace core {
    namespace math { /* All math types */ }
    /* Other top-level components */
}
```

### CMake Configuration (cmake/core.cmake)
```cmake
# Core static library
add_library(core STATIC
    # Math sources
    # Logging sources  
    # Timer sources
    # VFS sources
)

target_include_directories(core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third-party/glm
)

target_compile_features(core PUBLIC cxx_std_20)
```

### Dependencies
- GLM (already in third-party/)
- No other external dependencies
- Standard library only (C++20)

## Implementation Notes
- Maintain clean separation from graphics/RHI code
- Use header-only where appropriate (math wrappers)
- Ensure thread-safety where needed
- Design for future mobile platform constraints
- Keep memory allocations minimal