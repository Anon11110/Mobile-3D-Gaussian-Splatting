# Core Library Implementation Plan

## Overview
Create a foundational `core` static library for the 3D graphics engine with no dependencies on other engine modules. The library will provide essential utilities including math (GLM wrapper), logging, timing, and virtual file system functionality.

### Current Status: **3/6 Phases Complete** 🔄
- ✅ **Phase 1**: Math Foundation - **COMPLETED**
- ✅ **Phase 2**: Logging System - **COMPLETED**
- ⏳ **Phase 3**: Timing Utilities - **Not Started**
- ⏳ **Phase 4**: Virtual File System - **Not Started**
- ✅ **Phase 5**: Build System Integration - **COMPLETED**
- 🔄 **Phase 6**: Integration and Testing - **Partially Complete**

## Project Structure
- **Headers:** `include/core/[submodule]/[component].h`
- **Source:** `src/core/[submodule]/[component].cpp`
- **Build:** `cmake/core.cmake` (included by root CMakeLists.txt)
- **Testing:** Use `examples/triangle/main.cpp` as integration test

## Implementation Phases

### Phase 1: Math Foundation (GLM Wrapper)
**Goal:** Create comprehensive math library wrapping GLM within `msplat::math` namespace
**Status:** ✅ **COMPLETED**

**Tasks:**
1. ✅ Set up GLM dependency and CMake configuration
2. ✅ Create individual math component headers:
   - ✅ `vector.h` - vec2, vec3, vec4 types and operations
   - ✅ `matrix.h` - mat3, mat4 types and transformations
   - ✅ `quaternion.h` - Rotation quaternions
   - ✅ `affine.h` - Affine transform class
   - ✅ `aabb.h` - Axis-aligned bounding boxes
   - ✅ `sphere.h` - Bounding spheres
   - ✅ `frustum.h` - View frustum and projection matrices
   - ✅ `color.h` - Color types and utilities
   - ✅ `basics.h` - Constants (Pi, etc.) and utilities
3. ✅ Create main `math.h` convenience header
4. ✅ Update triangle example to use msplat::math types

**Deliverables:**
- ✅ All math headers in `include/core/math/`
- ✅ Header-only implementation (no source files needed)
- ✅ Working triangle example using new math types

**Success Criteria:**
- ✅ All math types properly wrapped in msplat::math namespace
- ✅ Triangle example compiles and runs correctly using `math::vec3`, `math::mat4`, etc.
- ✅ No direct GLM includes needed in client code

### Phase 2: Logging System
**Goal:** Implement flexible logging system with severity-based macro interface within `msplat::log` namespace
**Status:** ✅ **COMPLETED**

**Tasks:**
1. ✅ Design logger interface with Severity enum (None, Debug, Info, Warning, Error, Fatal)
2. ✅ Create `log.h` with severity-matched LOG_* macros:
   - ✅ `LOG_DEBUG` (Severity::Debug)
   - ✅ `LOG_INFO` (Severity::Info)  
   - ✅ `LOG_WARNING` (Severity::Warning)
   - ✅ `LOG_ERROR` (Severity::Error)
   - ✅ `LOG_FATAL` (Severity::Fatal)
3. ✅ Implement console output backend with severity-based formatting
4. ✅ Add log level filtering based on minimum severity
5. ✅ Design extensibility points for future backends
6. ✅ Integrate logging into triangle example
7. ✅ Add automatic build-type detection for minimum severity level

**Deliverables:**
- ✅ `include/core/log.h` - Public logging interface with Severity enum
- ✅ `src/core/log.cpp` - Logger implementation with severity filtering
- ✅ Updated triangle example with severity-based logging

**Success Criteria:**
- ✅ All LOG_* macros work correctly with corresponding severity levels
- ✅ Severity-based filtering displays appropriate log levels
- ✅ Triangle example demonstrates different severity levels
- ✅ Automatic build-type detection: Debug builds show DEBUG messages, Release builds hide them

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
**Status:** ✅ **COMPLETED**

**Tasks:**
1. ✅ Create `cmake/core.cmake` with library definition
2. ✅ Set up proper include directories
3. ✅ Configure GLM dependency
4. ✅ Add to root CMakeLists.txt
5. ✅ Ensure proper static library generation

**Deliverables:**
- ✅ `cmake/core.cmake` - Core library CMake configuration
- ✅ Updated root CMakeLists.txt
- ✅ Static library builds correctly

**Success Criteria:**
- ✅ Core library builds as static library
- ✅ Examples link successfully
- ✅ No circular dependencies

### Phase 6: Integration and Testing
**Goal:** Fully integrate core library with triangle example
**Status:** 🔄 **Partially Complete**

**Tasks:**
1. 🔄 Remove all direct std includes for replaced functionality (math and logging done)
2. ✅ Update all math operations to use msplat::math
3. ⏳ Replace all file I/O with VFS (pending Phase 4)
4. ✅ Add comprehensive logging throughout
5. ⏳ Use Timer for all timing operations (pending Phase 3)
6. ⏳ Verify no regressions in functionality (pending full integration)

**Deliverables:**
- ✅ Triangle example successfully uses msplat::math types
- ✅ Triangle example uses msplat::log for all output
- 🔄 Partial core functionality demonstrated (math and logging)
- 🔄 Clean separation of concerns (partially achieved)

**Success Criteria:**
- ✅ Triangle example works identically to original
- 🔄 Math and logging subsystems properly utilized (other subsystems pending)
- 🔄 Code is cleaner with proper logging and math operations (other areas pending)

## Technical Specifications

### Namespace Structure
```cpp
namespace msplat::math {
    /* All math types and operations */
    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using mat3 = glm::mat3;
    using mat4 = glm::mat4;
    using quat = glm::quat;
    /* ... etc. */
}

namespace msplat::log {
    /* Logging system */
    enum class Severity {
        None = 0,
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };
    
    /* LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL macros */
}

/* Future core components will be in msplat:: namespace */
```

### CMake Configuration (cmake/core.cmake)
```cmake
# ✅ IMPLEMENTED - Core static library
set(CORE_HEADERS
    # Math headers (all completed)
    ${CMAKE_SOURCE_DIR}/include/core/math/math.h
    ${CMAKE_SOURCE_DIR}/include/core/math/basics.h
    ${CMAKE_SOURCE_DIR}/include/core/math/vector.h
    ${CMAKE_SOURCE_DIR}/include/core/math/matrix.h
    ${CMAKE_SOURCE_DIR}/include/core/math/quaternion.h
    ${CMAKE_SOURCE_DIR}/include/core/math/affine.h
    ${CMAKE_SOURCE_DIR}/include/core/math/aabb.h
    ${CMAKE_SOURCE_DIR}/include/core/math/sphere.h
    ${CMAKE_SOURCE_DIR}/include/core/math/frustum.h
    ${CMAKE_SOURCE_DIR}/include/core/math/color.h
    
    # Logging headers (completed)
    ${CMAKE_SOURCE_DIR}/include/core/log.h
)

add_library(core STATIC
    # Logging sources
    ${CMAKE_SOURCE_DIR}/src/core/log.cpp
    ${CMAKE_SOURCE_DIR}/src/core/core.cpp  # Dummy source file
    ${CORE_HEADERS}
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

### ✅ Completed
- **Math Library**: Clean GLM wrapper in msplat::math namespace
- **Logging System**: Comprehensive logging with severity levels and build-type detection
- **Header-only Design**: Math components are header-only for optimal performance
- **Clean Separation**: Core library has no dependencies on graphics/RHI code
- **CMake Integration**: Proper static library configuration with cmake/core.cmake
- **Triangle Integration**: Example successfully uses math::vec3, math::mat4, and logging

### 🔄 In Progress
- **Namespace Migration**: Updated from nested to flat namespace (msplat::math, msplat::log)
- **Build System**: Core library builds with math and logging functionality

### ⏳ Remaining Tasks
- Create timing utilities using std::chrono (Phase 3)
- Build virtual file system with physical filesystem backend (Phase 4)
- Complete integration testing across all subsystems

### Technical Principles
- Maintain clean separation from graphics/RHI code ✅
- Use header-only where appropriate (math wrappers) ✅
- Ensure thread-safety where needed (not yet applicable)
- Design for future mobile platform constraints ✅
- Keep memory allocations minimal ✅