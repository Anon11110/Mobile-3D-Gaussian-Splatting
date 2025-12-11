# Hybrid Splat Renderer (CPU + GPU Sorting)

Design for a single example target under `examples/hybrid-splat-renderer` that merges `naive-splat-cpu` and `gpu-sorting-renderer` into one switchable app while keeping legacy examples intact for reference.

---

## Implementation Status

| Phase | Status | Date | Notes |
|-------|--------|------|-------|
| Phase 1: Foundation | **COMPLETE** | 2025-12-11 | GPU sorting baseline working |
| Phase 2: Backend Interface | Pending | - | - |
| Phase 3: GPU Backend | Pending | - | - |
| Phase 4: CPU Backend | Pending | - | - |
| Phase 5: Backend Switching | Pending | - | - |
| Phase 6: UI & Controls | Pending | - | - |
| Phase 7: Polish | Pending | - | - |

---

## 1. Objectives and Scope

### Goals
- One binary that can swap between CPU and GPU sorting at runtime without restarting.
- Preserve both baselines: CPU correctness path and GPU radix sorter (prescan and integrated scan) with verification and benchmarking toggles.
- Reuse the existing rendering path (splat raster shaders, descriptor layout, instanced quad draw) for comparable visuals.
- Provide controls and metrics to compare quality, correctness, and performance between backends.
- Location: `examples/hybrid-splat-renderer/`

### Non-goals
- No engine-level refactors beyond hosting both backends (keep `engine::Scene`, `GpuSplatSorter`, shader I/O, RHI APIs).
- Do not modify or delete `naive-splat-cpu` or `gpu-sorting-renderer`; they remain as references.
- No new shader work; reuse `splat_raster.vert/frag` and the current descriptor layout.

---

## 2. Key Design Decisions

These decisions were made after analyzing the existing implementations:

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **API Style** | Encapsulated execution | Each backend handles its own execution model internally (async thread vs command buffer). No command list parameter in interface. Cleaner abstraction. |
| **Buffer Ownership** | App owns sorted indices buffer | App creates the GPU buffer, passes it to backends. Backends write to the shared buffer. Unified resource management. |
| **Coordinate System** | Normalize at load time | Apply coordinate conversion (negate Z, adjust quaternion) once when loading PLY. Both backends use identical data. |

---

## 3. Architecture Overview

### Data Flow
```
PLY File
    │
    ▼
SplatLoader (with coordinate normalization)
    │
    ▼
Scene (GPU buffers: positions, covariances, colors, SH)
    │
    ├──────────────────────────────────────┐
    ▼                                      ▼
CpuSplatSortBackend                 GpuSplatSortBackend
    │                                      │
    │  (async thread + upload)             │  (compute dispatches)
    │                                      │
    └──────────────┬───────────────────────┘
                   ▼
         App-Owned Sorted Indices Buffer
                   │
                   ▼
         Descriptor Set Binding 5
                   │
                   ▼
         Instanced Quad Rendering
```

### Resource Ownership
- **App owns**: Sorted indices GPU buffer, descriptor set, graphics pipeline, UBO
- **Scene owns**: Position, covariance, color, SH GPU buffers
- **Backends own**: Internal sorting state (CPU: thread, double-buffer; GPU: GpuSplatSorter instance)

### Camera and Controls
- Perspective projection: ~45° FOV, near 0.1, far 1000
- Initial position: (0, 0, 5) looking at origin
- Controls: WASD movement + mouse look
- ESC exits application

### Minimized Window Guard
- Skip rendering when width/height == 0
- Prevents Vulkan validation errors and division by zero

---

## 4. Backend Interface Specification

### ISplatSortBackend Interface

```cpp
// include/msplat/engine/splat_sort_backend.h

#pragma once

#include <msplat/core/math/types.h>
#include <rhi/rhi_types.h>

namespace msplat::engine {

// Forward declarations
class Scene;

/// Performance metrics returned by backends
struct SortMetrics {
    float sortDurationMs = 0.0f;      // Time spent sorting
    float uploadDurationMs = 0.0f;    // Time spent uploading (CPU backend only)
    bool sortComplete = true;          // False if CPU sort still in progress
};

/// Abstract interface for splat sorting backends
class ISplatSortBackend {
public:
    virtual ~ISplatSortBackend() = default;

    /// Initialize the backend
    /// @param device RHI device for GPU operations
    /// @param scene Scene containing splat data (positions, etc.)
    /// @param sortedIndicesBuffer App-owned buffer where sorted indices are written
    /// @param totalSplatCount Number of splats to sort
    /// @return true if initialization succeeded
    virtual bool Initialize(
        rhi::IRHIDevice* device,
        Scene* scene,
        rhi::BufferHandle sortedIndicesBuffer,
        uint32_t totalSplatCount
    ) = 0;

    /// Update sorting based on current view
    /// CPU backend: Triggers async sort, uploads when complete
    /// GPU backend: Records and submits compute dispatches
    /// @param viewMatrix Current camera view matrix
    /// @param cameraPos Current camera position in world space
    virtual void Update(
        const math::mat4& viewMatrix,
        const math::vec3& cameraPos
    ) = 0;

    /// Check if the most recent sort operation has completed
    /// CPU backend: May return false while async sort is in progress
    /// GPU backend: Always returns true (synchronous within frame)
    virtual bool IsSortComplete() const = 0;

    /// Get performance metrics from the last sort operation
    virtual SortMetrics GetMetrics() const = 0;

    /// Get human-readable backend name (e.g., "CPU", "GPU")
    virtual const char* GetName() const = 0;

    /// Trigger verification of sort correctness (optional)
    /// @return true if sort order is correct, false otherwise
    virtual bool VerifySort() { return true; }

    /// Set sorting method (GPU backend only: 0=Prescan, 1=IntegratedScan)
    virtual void SetSortMethod(int method) { (void)method; }

    /// Get current sorting method index
    virtual int GetSortMethod() const { return 0; }

    /// Get human-readable method name (e.g., "Prescan", "Integrated Scan")
    virtual const char* GetMethodName() const { return "Default"; }

    /// Check if comprehensive verification is available
    virtual bool HasComprehensiveVerification() const { return false; }

    /// Run comprehensive verification (GPU backend only)
    virtual bool RunComprehensiveVerification() { return true; }
};

} // namespace msplat::engine
```

### API Mismatch Resolution

The two underlying sorters have fundamentally different execution models:

| Aspect | CpuSplatSorter (via Scene) | GpuSplatSorter |
|--------|---------------------------|----------------|
| **Trigger** | `RequestSort(positions, view_matrix)` | `Sort(cmdList, scene, camera)` |
| **Execution** | Async background thread | Synchronous command buffer recording |
| **Completion** | Poll `IsSortComplete()` | Immediate after dispatch |
| **Buffer** | Double-buffered, swap on consume | Ping-pong per radix pass |
| **Duration** | `GetLastSortDuration()` | Not directly exposed |

The `ISplatSortBackend` interface abstracts these differences:
- **Encapsulated execution**: Backends manage their own execution internally
- **Unified completion check**: `IsSortComplete()` works for both models
- **App-owned target buffer**: Both backends write to the same destination

---

## 5. Backend Implementation Details

### CPU Backend (CpuSplatSortBackend)

```cpp
// src/engine/cpu_splat_sort_backend.cpp

class CpuSplatSortBackend : public ISplatSortBackend {
private:
    rhi::IRHIDevice* m_device = nullptr;
    Scene* m_scene = nullptr;
    rhi::BufferHandle m_targetBuffer;      // App-owned, we write here
    uint32_t m_splatCount = 0;

    // Timing
    float m_lastSortDurationMs = 0.0f;
    float m_lastUploadDurationMs = 0.0f;

public:
    bool Initialize(
        rhi::IRHIDevice* device,
        Scene* scene,
        rhi::BufferHandle sortedIndicesBuffer,
        uint32_t totalSplatCount
    ) override {
        m_device = device;
        m_scene = scene;
        m_targetBuffer = sortedIndicesBuffer;
        m_splatCount = totalSplatCount;
        return true;
    }

    void Update(const math::mat4& viewMatrix, const math::vec3& cameraPos) override {
        // Trigger async sort via scene
        m_scene->UpdateView(viewMatrix);

        // Check if sort completed and upload
        if (m_scene->ConsumeAndUploadSortedIndices(m_device, m_targetBuffer)) {
            // Update timing metrics from scene's sorter
            m_lastSortDurationMs = m_scene->GetLastSortDuration();
            m_lastUploadDurationMs = m_scene->GetLastUploadDuration();
        }
    }

    bool IsSortComplete() const override {
        return m_scene->IsSortComplete();
    }

    SortMetrics GetMetrics() const override {
        return {
            .sortDurationMs = m_lastSortDurationMs,
            .uploadDurationMs = m_lastUploadDurationMs,
            .sortComplete = IsSortComplete()
        };
    }

    const char* GetName() const override { return "CPU"; }
};
```

**Key Implementation Notes:**
- Scene's `UpdateView()` triggers `CpuSplatSorter::RequestSort()` internally
- `ConsumeAndUploadSortedIndices()` polls for completion and uploads when ready
- Must add `GetLastSortDuration()` and `GetLastUploadDuration()` to Scene if not present
- The backend doesn't own the sorted indices buffer; it writes to the app-provided buffer

### GPU Backend (GpuSplatSortBackend)

```cpp
// src/engine/gpu_splat_sort_backend.cpp

class GpuSplatSortBackend : public ISplatSortBackend {
private:
    rhi::IRHIDevice* m_device = nullptr;
    Scene* m_scene = nullptr;
    rhi::BufferHandle m_targetBuffer;
    uint32_t m_splatCount = 0;

    std::unique_ptr<GpuSplatSorter> m_sorter;
    int m_currentMethod = 1;  // Default: IntegratedScan

    // For verification
    bool m_verificationPending = false;

public:
    bool Initialize(
        rhi::IRHIDevice* device,
        Scene* scene,
        rhi::BufferHandle sortedIndicesBuffer,
        uint32_t totalSplatCount
    ) override {
        m_device = device;
        m_scene = scene;
        m_targetBuffer = sortedIndicesBuffer;
        m_splatCount = totalSplatCount;

        // Create GPU sorter
        m_sorter = std::make_unique<GpuSplatSorter>();
        if (!m_sorter->Initialize(device, totalSplatCount)) {
            LOG_ERROR("Failed to initialize GpuSplatSorter");
            return false;
        }
        m_sorter->SetSortMethod(
            static_cast<GpuSplatSorter::SortMethod>(m_currentMethod)
        );
        return true;
    }

    void Update(const math::mat4& viewMatrix, const math::vec3& cameraPos) override {
        // Create command list for compute work
        auto cmdList = m_device->CreateCommandList(rhi::CommandListType::COMPUTE);
        cmdList->Begin();

        // Execute GPU sort
        m_sorter->Sort(cmdList.Get(), m_scene, viewMatrix, cameraPos);

        // Copy result to app-owned buffer
        rhi::BufferHandle sorterOutput = m_sorter->GetSortedIndices();
        cmdList->CopyBuffer(sorterOutput.Get(), m_targetBuffer.Get(),
                           m_splatCount * sizeof(uint32_t));

        cmdList->End();

        // Submit and wait (or use fence for async)
        rhi::SubmitInfo submitInfo{};
        submitInfo.commandLists = {cmdList.Get()};
        m_device->SubmitCommandLists(rhi::QueueType::COMPUTE, submitInfo);
        m_device->WaitIdle();  // Could be optimized with fences
    }

    bool IsSortComplete() const override {
        return true;  // GPU sort is synchronous within Update()
    }

    SortMetrics GetMetrics() const override {
        return {
            .sortDurationMs = 0.0f,  // TODO: Add GPU timing queries
            .uploadDurationMs = 0.0f,
            .sortComplete = true
        };
    }

    const char* GetName() const override { return "GPU"; }

    void SetSortMethod(int method) override {
        m_currentMethod = method;
        if (m_sorter) {
            m_sorter->SetSortMethod(
                static_cast<GpuSplatSorter::SortMethod>(method)
            );
        }
    }

    int GetSortMethod() const override { return m_currentMethod; }

    const char* GetMethodName() const override {
        return m_currentMethod == 0 ? "Prescan" : "Integrated Scan";
    }

    bool HasComprehensiveVerification() const override { return true; }

    bool VerifySort() override {
        if (!m_sorter) return false;
        return m_sorter->VerifySortOrder();
    }

    bool RunComprehensiveVerification() override {
        if (!m_sorter) return false;
        m_sorter->PrepareVerification();
        // Results checked on next frame via CheckVerificationResults()
        return true;
    }
};
```

**Key Implementation Notes:**
- Creates internal command list for compute dispatches
- Copies sorter's internal result to app-owned buffer
- Method switching delegates to underlying `GpuSplatSorter`
- Verification uses existing `GpuSplatSorter` verification infrastructure

---

## 6. Descriptor Layout and Rendering Pipeline

### Descriptor Set Layout (6 Bindings)

| Binding | Type | Stage | Content | Owner |
|---------|------|-------|---------|-------|
| 0 | UNIFORM_BUFFER | ALL_GRAPHICS | FrameUBO | App |
| 1 | STORAGE_BUFFER | VERTEX | Positions (vec3, padded) | Scene |
| 2 | STORAGE_BUFFER | VERTEX | Covariances3D (2×vec3) | Scene |
| 3 | STORAGE_BUFFER | VERTEX | Colors (vec4 RGBA) | Scene |
| 4 | STORAGE_BUFFER | VERTEX | SH Rest (45 floats) | Scene |
| 5 | STORAGE_BUFFER | VERTEX | **Sorted Indices** | App |

### FrameUBO Structure

```cpp
// From shaders/shaderio.h
struct FrameUBO {
    mat4 view;                  // View matrix
    mat4 projection;            // Projection matrix (Y-flipped for Vulkan)

    vec4 cameraPos;             // Camera position + padding
    vec2 viewport;              // Window dimensions (width, height)
    vec2 focal;                 // Focal length in pixels (fx, fy)

    float splatScale;           // Global splat scale factor (default: 1.0)
    float alphaCullThreshold;   // Alpha cutoff (default: 1/255)
    float maxSplatRadius;       // Max radius in pixels (default: 2048)
    int   enableSplatFilter;    // EWA anti-aliasing toggle (default: 1)

    vec2  basisViewport;        // (1/width, 1/height) for NDC
    float inverseFocalAdj;      // FOV adjustment (default: 1.0)
    float _padding;
};
```

### Graphics Pipeline Configuration

```cpp
rhi::GraphicsPipelineDesc pipelineDesc{};
pipelineDesc.vertexShader = vertexShader;
pipelineDesc.fragmentShader = fragmentShader;
pipelineDesc.topology = rhi::PrimitiveTopology::TRIANGLE_STRIP;
pipelineDesc.rasterizationState.cullMode = rhi::CullMode::NONE;

// Alpha blending (over operator)
auto& blend = pipelineDesc.colorBlendAttachments[0];
blend.blendEnable = true;
blend.srcColorBlendFactor = rhi::BlendFactor::SRC_ALPHA;
blend.dstColorBlendFactor = rhi::BlendFactor::ONE_MINUS_SRC_ALPHA;
blend.srcAlphaBlendFactor = rhi::BlendFactor::ONE;
blend.dstAlphaBlendFactor = rhi::BlendFactor::ONE;
```

### Rendering Command

```cpp
// Instanced quad rendering
cmdList->BindIndexBuffer(quadIndexBuffer);  // [0, 1, 2, 3]
cmdList->DrawIndexedInstanced(
    4,                            // 4 vertices per quad
    scene->GetTotalSplatCount(),  // One instance per splat
    0, 0, 0
);
```

---

## 7. UI and Controls

### ImGui Overlay Panels

1. **Performance Panel**
   - FPS display with 120-frame history graph
   - Frame time (ms)
   - Backend-specific metrics (sort time, upload time)

2. **Backend Selection Panel**
   - Radio buttons or dropdown: CPU / GPU
   - Current backend status indicator
   - Sorting enabled/disabled checkbox

3. **GPU Options Panel** (visible when GPU backend active)
   - Method dropdown: Prescan / Integrated Scan
   - Verification buttons: Simple / Comprehensive
   - Verification result display

4. **Scene Info Panel**
   - Total splat count
   - Frame count
   - Asset path

5. **Asset Panel**
   - Quick-load buttons for common assets
   - Text input for custom path
   - "Generate Random Test Data" button

6. **Controls Help Panel**
   - Keyboard shortcut reference

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| SPACE | Toggle sorting on/off |
| M | Switch GPU method (Prescan ↔ Integrated) |
| V | Trigger verification |
| B | Toggle benchmark mode |
| C | Switch backend (CPU ↔ GPU) |
| H | Toggle ImGui visibility |
| ESC | Exit application |

---

## 8. Performance and Telemetry

### Metrics Tracked

| Metric | Source | Display |
|--------|--------|---------|
| FPS | `timer::FPSCounter` | Number + graph |
| Frame time | Delta time | Milliseconds |
| Sort duration | Backend metrics | Milliseconds |
| Upload duration | CPU backend only | Milliseconds |
| Total splats | Scene | Count |

### Benchmark Mode

When enabled:
- Skips `swapchain->Present()` to remove vsync bottleneck
- Measures true GPU/CPU sorting performance
- Uses fixed swapchain image index (0)
- Stats continue updating for analysis

### Logging

```cpp
// Periodic FPS logging (every 1 second)
LOG_INFO("FPS: {:.1f} | Backend: {} | Method: {} | Splats: {}",
         fps, backend->GetName(), backend->GetMethodName(), splatCount);
```

---

## 9. Verification Strategy

### Simple Verification (Both Backends)
- Check if sorted depth keys are in ascending order
- O(n) scan of readback buffer
- Reports first out-of-order pair if found

### Comprehensive Verification (GPU Only)
1. **Depth Calculation Check**: Compare GPU-computed depths with CPU reference
2. **Histogram Verification**: Validate bin counts for each radix pass
3. **Scatter Verification**: Ensure all indices present (permutation check)

### Cross-Backend Comparison (Optional)
- Run CPU sort once as reference
- Compare GPU sort output ordering
- Detect algorithm regressions

### Random Test Data Generation
- Generate 10M splats with random positions
- Include edge cases: min/max Z, near camera plane
- Seeded RNG for reproducibility

---

## 10. Error Handling

### Swapchain Management
```cpp
auto result = swapchain->AcquireNextImage(...);
if (result == rhi::SwapchainResult::OUT_OF_DATE ||
    result == rhi::SwapchainResult::SUBOPTIMAL) {
    RecreateSwapchain();
    return;  // Skip this frame
}
```

### Backend Initialization Failure
```cpp
if (!gpuBackend->Initialize(...)) {
    LOG_WARNING("GPU backend failed to initialize, falling back to CPU");
    m_activeBackend = m_cpuBackend.get();
    m_gpuBackendAvailable = false;
}
```

### Shutdown Sequence
1. `device->WaitIdle()` - Ensure all GPU work complete
2. Destroy backends (releases internal resources)
3. Destroy app resources (descriptor sets, pipelines, buffers)
4. Destroy scene (releases GPU buffers)

---

## 11. File Structure

### New Files to Create

| File | Purpose | Status |
|------|---------|--------|
| `examples/hybrid-splat-renderer/CMakeLists.txt` | Build configuration | **DONE** |
| `examples/hybrid-splat-renderer/main.cpp` | Application entry point | **DONE** |
| `examples/hybrid-splat-renderer/hybrid_splat_renderer_app.h` | App class declaration | **DONE** |
| `examples/hybrid-splat-renderer/hybrid_splat_renderer_app.cpp` | App implementation | **DONE** |
| `include/msplat/engine/splat_sort_backend.h` | Backend interface | Phase 2 |
| `src/engine/cpu_splat_sort_backend.cpp` | CPU backend implementation | Phase 4 |
| `src/engine/gpu_splat_sort_backend.cpp` | GPU backend implementation | Phase 3 |

### Files to Modify

| File | Change | Status |
|------|--------|--------|
| `examples/CMakeLists.txt` | Add `add_subdirectory(hybrid-splat-renderer)` | **DONE** |
| `cmake/engine.cmake` | Add backend source files to engine library | Phase 2-4 |

### Reference Files (Copy Patterns From)

| Source File | What to Copy |
|-------------|--------------|
| `examples/gpu-sorting-renderer/gpu_sorting_renderer_app.cpp` | ImGui setup (~100 lines), FPS graph (~50 lines), verification UI (~50 lines), keyboard handling (~30 lines) |
| `examples/naive-splat-cpu/naive_splat_cpu_app.cpp` | Scene setup (~50 lines), descriptor layout (~40 lines), UBO management (~30 lines) |

---

## 12. CMakeLists.txt Template

```cmake
# examples/hybrid-splat-renderer/CMakeLists.txt

add_executable(hybrid-splat-renderer
    main.cpp
    hybrid_splat_renderer_app.cpp
    hybrid_splat_renderer_app.h
)

target_link_libraries(hybrid-splat-renderer PRIVATE
    RHI
    core
    engine
    app
    imgui
)

target_compile_features(hybrid-splat-renderer PRIVATE cxx_std_20)

# Windows macro fix
apply_windows_macro_fix(hybrid-splat-renderer)

# Set output directory
set_target_properties(hybrid-splat-renderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

# Copy assets (handled by parent CMakeLists.txt asset copy target)
```

---

## 13. Execution Plan

### Phase 1: Foundation (~2 hours) - COMPLETE

**Goal**: Basic app skeleton that builds and shows a window.

**Status**: COMPLETE (2025-12-11)

1. [x] Create `CMakeLists.txt` for hybrid-splat-renderer
2. [x] Add `add_subdirectory(hybrid-splat-renderer)` to `examples/CMakeLists.txt`
3. [x] Create `HybridSplatRendererApp` class skeleton extending `app::IApplication`
4. [x] Implement lifecycle methods: `OnInit`, `OnUpdate`, `OnRender`, `OnShutdown`
5. [x] Copy shared code from GPU renderer:
   - UBO creation and mapping
   - Descriptor layout (6 bindings)
   - Graphics pipeline creation
   - Quad index buffer creation
6. [x] Load PLY with coordinate normalization (Z negate, quaternion adjust)
7. [x] Set up camera with default parameters

**Implementation Notes:**
- Full GPU sorting functional via `GpuSplatSorter` (not yet abstracted behind `ISplatSortBackend`)
- ImGui integration complete with FPS graph and controls panel
- All keyboard shortcuts working (SPACE, V, M, T, B, H, ESC)
- Test data generation for 10M splats included
- Benchmark mode functional

**Files Created:**
| File | Lines |
|------|-------|
| `CMakeLists.txt` | 51 |
| `main.cpp` | 24 |
| `hybrid_splat_renderer_app.h` | 116 |
| `hybrid_splat_renderer_app.cpp` | 868 |

**Verification**: Build succeeds, window opens, splats render correctly with GPU sorting

### Phase 2: Backend Interface (~1 hour)

**Goal**: Define the abstraction layer.

1. Create `include/msplat/engine/splat_sort_backend.h` with full interface
2. Add `SortMetrics` struct
3. Update `cmake/engine.cmake` to include new header
4. Verify interface compiles correctly

**Verification**: Project compiles with new interface header

### Phase 3: GPU Backend (~2 hours)

**Goal**: Working GPU sorting through the abstraction.

1. Create `src/engine/gpu_splat_sort_backend.cpp`
2. Implement `Initialize()` - create `GpuSplatSorter` instance
3. Implement `Update()` - record compute dispatches, copy to target buffer
4. Implement method switching via `SetSortMethod()`
5. Implement verification delegation
6. Add to `cmake/engine.cmake`
7. Integrate into app (GPU backend only initially)

**Verification**: Renders correctly with GPU sorting, splats visible and properly ordered

### Phase 4: CPU Backend (~2 hours)

**Goal**: Working CPU sorting through the abstraction.

1. Create `src/engine/cpu_splat_sort_backend.cpp`
2. Implement `Initialize()` - store references
3. Implement `Update()` - trigger scene sort, poll completion, upload
4. Add timing metric extraction from Scene
5. Handle async completion properly
6. Add to `cmake/engine.cmake`
7. Integrate into app as second backend option

**Verification**: Can switch to CPU backend, renders correctly

### Phase 5: Backend Switching (~1 hour)

**Goal**: Runtime switching without reloads.

1. Add backend selection state to app
2. Implement `SwitchBackend()` method
3. Preserve camera state across switches
4. Add keyboard shortcut (C key)
5. Verify descriptor binding updates correctly

**Verification**: Can switch between CPU and GPU at runtime, no visual glitches

### Phase 6: UI & Controls (~2 hours)

**Goal**: Full ImGui interface.

1. Copy ImGui initialization from GPU renderer
2. Implement performance panel (FPS, metrics)
3. Implement backend selection panel
4. Implement GPU options panel (method, verification)
5. Implement scene info panel
6. Implement asset panel with quick-load buttons
7. Implement controls help panel
8. Wire all keyboard shortcuts

**Verification**: All UI elements functional, shortcuts work

### Phase 7: Polish (~1 hour)

**Goal**: Production-quality implementation.

1. Add error handling for backend init failures
2. Implement benchmark mode (skip present)
3. Add cross-backend verification (optional)
4. Copy random test scene generator from GPU renderer
5. Test window resize/minimize handling
6. Add comprehensive logging
7. Final code cleanup

**Verification**: All edge cases handled, no crashes on resize/minimize

---

## 14. Testing Checklist

### Functional Tests
- [x] GPU backend renders correctly with `flowers_1.ply` *(Phase 1)*
- [ ] CPU backend renders correctly with `flowers_1.ply`
- [ ] Visual parity between backends on same asset
- [ ] Runtime switching works without crashes
- [ ] Camera state preserved across backend switches
- [x] GPU method switching (Prescan ↔ Integrated) works *(Phase 1)*
- [x] Sorting on/off toggle maintains stability *(Phase 1)*
- [x] Verification modes report correctly (GPU) *(Phase 1)*

### Performance Tests
- [x] Benchmark mode works (skips present, shows true perf) *(Phase 1)*
- [x] FPS graph updates correctly *(Phase 1)*
- [ ] Metrics display accurate timing data

### UI Tests
- [x] All keyboard shortcuts functional *(Phase 1 - SPACE, V, M, T, B, H, ESC)*
- [x] ImGui panels render correctly *(Phase 1)*
- [ ] Backend selector changes active backend
- [x] Method selector changes GPU method *(Phase 1)*
- [ ] Asset quick-load buttons work

### Edge Cases
- [x] Window minimize handled gracefully (no render) *(Phase 1)*
- [ ] Window resize recreates swapchain correctly
- [x] Invalid asset path shows error (no crash) - falls back to test data *(Phase 1)*
- [ ] GPU backend failure falls back to CPU
- [x] Random test data generation works (10M splats) *(Phase 1)*

---

## 15. Estimated Effort

| Phase | Time | Complexity |
|-------|------|------------|
| Phase 1: Foundation | ~2 hours | Medium |
| Phase 2: Backend Interface | ~1 hour | Low |
| Phase 3: GPU Backend | ~2 hours | Medium |
| Phase 4: CPU Backend | ~2 hours | Medium |
| Phase 5: Backend Switching | ~1 hour | Low |
| Phase 6: UI & Controls | ~2 hours | Low |
| Phase 7: Polish | ~1 hour | Low |
| **Total** | **~11 hours** | |

### Code Estimates

| Component | New Lines | Reused Lines |
|-----------|-----------|--------------|
| Interface header | ~80 | - |
| CPU backend | ~150 | - |
| GPU backend | ~200 | - |
| App implementation | ~500 | ~400 (from GPU renderer) |
| CMakeLists.txt | ~30 | - |
| **Total** | **~960** | **~400** |
