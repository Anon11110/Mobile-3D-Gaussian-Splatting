# Rendering Hardware Interface (RHI) Design Document

## Executive Summary

The Rendering Hardware Interface (RHI) provides a unified abstraction layer for GPU rendering, initially implementing a Vulkan backend that runs across all platforms (Windows natively, macOS via MoltenVK, Linux natively). This document outlines the architecture for a minimal, efficient RHI designed specifically for 3D Gaussian splatting applications. The interface is designed to accommodate future Metal backend implementation, but the initial focus is on Vulkan for cross-platform compatibility.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Design Principles](#design-principles)
3. [Core Components](#core-components)
4. [Resource Management](#resource-management)
5. [Command System](#command-system)
6. [Synchronization Model](#synchronization-model)
7. [Memory Management](#memory-management)
8. [Error Handling](#error-handling)
9. [Platform Abstraction Strategy](#platform-abstraction-strategy)
10. [Future Extensions](#future-extensions)

---

## Architecture Overview

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
│                  (3D Gaussian Splatting)                     │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      RHI Interface                           │
│                    (Abstract Classes)                        │
├───────────────────────────┬─────────────────────────────────┤
│      Vulkan Backend       │    Future: Metal Backend        │
│  (All Platforms via       │    (Native macOS/iOS)          │
│   Native or MoltenVK)     │                                 │
└───────────────────────────┴─────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                 Vulkan API / MoltenVK                        │
│            (Windows, macOS, Linux, Android)                  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Hardware / Drivers                        │
└─────────────────────────────────────────────────────────────┘
```

### Module Structure

```
rhi/
├── include/
│   ├── rhi.h                 # Core RHI interfaces
│   ├── vulkan.h             # Vulkan-specific declarations
│   ├── metal.h              # Metal-specific declarations (future)
│   └── common/
│       └── resource.h       # Shared resource definitions
├── src/
│   ├── rhi.cpp              # Common implementation
│   └── vulkan/
│       ├── vulkan_device.cpp
│       ├── vulkan_buffer.cpp
│       ├── vulkan_pipeline.cpp
│       ├── vulkan_shader.cpp
│       ├── vulkan_swapchain.cpp
│       └── vulkan_command.cpp
└── third-party/
    └── Vulkan-Headers/      # Vulkan SDK headers
```

Note: Metal backend implementation (`src/metal/`) is planned for future development but not included in the initial implementation.

---

## Design Principles

### 1. Minimal Surface Area
- Only expose functionality required for 3D Gaussian splatting
- Avoid feature creep and unnecessary abstractions
- Focus on performance-critical paths

### 2. Zero-Cost Abstraction
- Virtual function calls only at command buffer submission boundaries
- Inline platform-specific code where possible
- Compile-time platform selection when feasible

### 3. Explicit Resource Management
- No hidden allocations or automatic resource tracking
- Application controls lifetime of all GPU resources
- Clear ownership semantics

### 4. Modern API Patterns
- Command buffer recording model
- Explicit synchronization
- Pipeline state objects
- Descriptor set management

### 5. Platform Parity
- Consistent behavior across platforms
- Platform-specific optimizations hidden behind common interface
- Graceful fallbacks for missing features

---

## Core Components

### IRHIDevice

The central factory and management interface for all RHI operations.

```cpp
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;
    
    // Factory methods
    virtual std::unique_ptr<IRHIBuffer> CreateBuffer(const BufferDesc& desc) = 0;
    virtual std::unique_ptr<IRHITexture> CreateTexture(const TextureDesc& desc) = 0;
    virtual std::unique_ptr<IRHIPipeline> CreatePipeline(const PipelineDesc& desc) = 0;
    virtual std::unique_ptr<IRHIShader> CreateShader(const ShaderDesc& desc) = 0;
    virtual std::unique_ptr<IRHICommandList> CreateCommandList() = 0;
    
    // Swapchain management
    virtual std::unique_ptr<IRHISwapchain> CreateSwapchain(const SwapchainDesc& desc) = 0;
    
    // Queue submission
    virtual void SubmitCommandLists(IRHICommandList** lists, uint32_t count) = 0;
    virtual void WaitIdle() = 0;
    
    // Capabilities query
    virtual const DeviceCapabilities& GetCapabilities() const = 0;
};
```

### IRHIBuffer

Represents GPU-accessible memory buffers.

```cpp
class IRHIBuffer {
public:
    virtual ~IRHIBuffer() = default;
    
    virtual void* Map() = 0;
    virtual void Unmap() = 0;
    virtual size_t GetSize() const = 0;
    virtual BufferUsageFlags GetUsage() const = 0;
};

struct BufferDesc {
    size_t size;
    BufferUsageFlags usage;  // VERTEX | INDEX | UNIFORM | STORAGE
    MemoryType memoryType;   // GPU_ONLY | CPU_TO_GPU | GPU_TO_CPU
};
```

### IRHIPipeline

Encapsulates the complete GPU pipeline state.

```cpp
class IRHIPipeline {
public:
    virtual ~IRHIPipeline() = default;
    virtual PipelineType GetType() const = 0;  // GRAPHICS | COMPUTE
};

struct PipelineDesc {
    IRHIShader* vertexShader;
    IRHIShader* fragmentShader;
    IRHIShader* computeShader;  // For compute pipelines
    
    VertexLayout vertexLayout;
    PrimitiveTopology topology;
    RenderState renderState;     // Blend, depth, stencil
    
    std::vector<DescriptorSetLayout> descriptorLayouts;
};
```

### IRHICommandList

Records GPU commands for deferred execution.

```cpp
class IRHICommandList {
public:
    virtual ~IRHICommandList() = default;
    
    // Recording control
    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual void Reset() = 0;
    
    // Render commands
    virtual void BeginRenderPass(const RenderPassBeginInfo& info) = 0;
    virtual void EndRenderPass() = 0;
    virtual void SetPipeline(IRHIPipeline* pipeline) = 0;
    virtual void SetVertexBuffer(IRHIBuffer* buffer, uint32_t offset = 0) = 0;
    virtual void SetIndexBuffer(IRHIBuffer* buffer, IndexType type) = 0;
    virtual void SetDescriptorSet(uint32_t set, IRHIDescriptorSet* descriptors) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) = 0;
    
    // Compute commands
    virtual void Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) = 0;
    
    // Synchronization
    virtual void PipelineBarrier(const BarrierInfo& barrier) = 0;
};
```

### IRHISwapchain

Manages presentation surfaces and frame buffering.

```cpp
class IRHISwapchain {
public:
    virtual ~IRHISwapchain() = default;
    
    virtual uint32_t AcquireNextImage(IRHISemaphore* signalSemaphore) = 0;
    virtual void Present(uint32_t imageIndex, IRHISemaphore* waitSemaphore) = 0;
    virtual IRHITexture* GetBackBuffer(uint32_t index) = 0;
    virtual uint32_t GetImageCount() const = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
};
```

---

## Resource Management

### Resource Lifecycle

1. **Creation**: Resources created through IRHIDevice factory methods
2. **Initialization**: Initial data upload via staging buffers
3. **Usage**: Bound to pipeline via command lists
4. **Destruction**: Automatic cleanup via RAII (unique_ptr)

### Resource Types

#### Buffers
- **Vertex Buffers**: GPU-only, optimized for vertex fetch
- **Index Buffers**: GPU-only, 16/32-bit indices
- **Uniform Buffers**: CPU-writable, small frequent updates
- **Storage Buffers**: GPU read/write for compute

#### Textures
- **2D Textures**: Standard sampling textures
- **Render Targets**: Color attachments for rendering
- **Depth Buffers**: Depth/stencil attachments

#### Descriptor Sets
- Groups resources for shader access
- Platform-specific binding models unified

### Memory Allocation Strategy

```cpp
class IRHIMemoryAllocator {
public:
    // Suballocate from larger heap allocations
    virtual MemoryAllocation Allocate(const MemoryRequirements& reqs) = 0;
    virtual void Free(const MemoryAllocation& allocation) = 0;
    
    // Memory budget tracking
    virtual size_t GetUsedMemory() const = 0;
    virtual size_t GetAvailableMemory() const = 0;
};
```

---

## Command System

### Command Recording Model

1. **Command List Creation**: Allocate from thread-local pools
2. **Recording**: Build command stream on CPU
3. **Submission**: Queue for GPU execution
4. **Execution**: GPU processes commands asynchronously

### Multi-threaded Recording

```cpp
// Each thread can record independently
void RecordThread(IRHIDevice* device, SceneData* data) {
    auto cmdList = device->CreateCommandList();
    cmdList->Begin();
    
    // Record draw commands
    for (const auto& object : data->objects) {
        cmdList->SetPipeline(object.pipeline);
        cmdList->SetVertexBuffer(object.vertexBuffer);
        cmdList->DrawIndexed(object.indexCount);
    }
    
    cmdList->End();
    
    // Thread-safe submission
    device->SubmitCommandLists(&cmdList, 1);
}
```

### Command Buffer Pools

- Pre-allocated command buffers
- Reset and reuse each frame
- Separate pools per thread

---

## Synchronization Model

### GPU-GPU Synchronization

#### Semaphores
- Signal between queue submissions
- Frame synchronization with swapchain

```cpp
class IRHISemaphore {
public:
    virtual ~IRHISemaphore() = default;
};
```

#### Pipeline Barriers
- Memory access synchronization
- Image layout transitions

```cpp
struct BarrierInfo {
    PipelineStage srcStage;
    PipelineStage dstStage;
    AccessFlags srcAccess;
    AccessFlags dstAccess;
};
```

### CPU-GPU Synchronization

#### Fences
- CPU waits for GPU completion
- Frame pacing control

```cpp
class IRHIFence {
public:
    virtual void Wait(uint64_t timeout = UINT64_MAX) = 0;
    virtual void Reset() = 0;
    virtual bool IsSignaled() const = 0;
};
```

### Frame Synchronization Pattern

```cpp
void RenderFrame(IRHIDevice* device, IRHISwapchain* swapchain) {
    // Acquire next image
    uint32_t imageIndex = swapchain->AcquireNextImage(acquireSemaphore);
    
    // Record and submit commands
    auto cmdList = recordCommands(imageIndex);
    device->SubmitCommandLists(&cmdList, 1, 
                              waitSemaphore: acquireSemaphore,
                              signalSemaphore: renderSemaphore);
    
    // Present
    swapchain->Present(imageIndex, renderSemaphore);
}
```

---

## Memory Management

### Memory Types

1. **Device Local**: GPU-only, fastest access
2. **Host Visible**: CPU-writable, GPU-readable
3. **Host Cached**: CPU-cached for readback

### Allocation Strategies

#### Linear Allocator
- For per-frame temporary data
- Reset at frame boundaries
- No fragmentation

#### Pool Allocator
- Fixed-size blocks
- Fast allocation/deallocation
- Minimal fragmentation

#### Heap Allocator
- Variable-size allocations
- Best-fit or buddy system
- Defragmentation support

### Memory Budget Management

```cpp
struct MemoryBudget {
    size_t totalAvailable;
    size_t currentUsed;
    size_t peakUsed;
    
    // Per-heap breakdown
    std::vector<HeapInfo> heaps;
};
```

---

## Error Handling

### Error Categories

1. **Initialization Errors**: Device creation, API version mismatch
2. **Resource Errors**: Out of memory, invalid parameters
3. **Runtime Errors**: Device lost, surface lost
4. **Validation Errors**: API misuse (debug only)

### Error Reporting

```cpp
enum class RHIResult {
    Success,
    ErrorOutOfMemory,
    ErrorDeviceLost,
    ErrorInvalidParameter,
    ErrorUnsupportedFeature,
    ErrorInitializationFailed
};

// Error callback for detailed information
using ErrorCallback = std::function<void(RHIResult, const char* message)>;
```

### Validation Layers

- Vulkan: Validation layers in debug builds
- Metal: GPU validation and shader debugging
- Performance warnings and best practices

---

## Platform Abstraction Strategy

### Implementation Approach

```cpp
// Initial implementation: Vulkan only
std::unique_ptr<IRHIDevice> CreateRHIDevice() {
    return std::make_unique<VulkanDevice>();
}

// Future: Runtime or compile-time selection
std::unique_ptr<IRHIDevice> CreateRHIDevice(RHIBackend backend) {
    switch (backend) {
        case RHIBackend::Vulkan:
            return std::make_unique<VulkanDevice>();
        case RHIBackend::Metal:
            return std::make_unique<MetalDevice>();  // Future
        default:
            return std::make_unique<VulkanDevice>();
    }
}
```

### Cross-Platform Vulkan Support

#### Native Vulkan (Windows/Linux)
- Direct Vulkan API calls
- Full feature set available
- Optimal performance

#### MoltenVK (macOS/iOS)
- Vulkan-to-Metal translation layer
- Most Vulkan 1.2 features supported
- Minor performance overhead (~5-10%)
- Seamless integration

### Shader Strategy

```
GLSL/HLSL Source
      │
      ▼
   SPIR-V
      │
      ├── Native Vulkan (Windows/Linux)
      │
      └── MoltenVK → Metal (macOS/iOS)
```

---

## Future Extensions

### Phase 2: Extended Features
- Texture arrays and 3D textures
- Indirect drawing
- Query objects (occlusion, timestamp)
- Multiple render targets

### Phase 3: Compute Focus
- Shared memory
- Atomic operations
- Texture UAVs
- Async compute queues

### Phase 4: Mobile Optimizations
- Tile-based rendering hints
- Bandwidth reduction techniques
- Power management APIs
- Thermal throttling adaptation

### Phase 5: Advanced Features
- Ray tracing (optional)
- Mesh shaders
- Variable rate shading
- GPU-driven rendering

---

## Performance Considerations

### Optimization Guidelines

1. **Minimize State Changes**: Sort by pipeline, then material
2. **Batch Draw Calls**: Use instancing for similar objects
3. **Reduce Synchronization**: Pipeline work asynchronously
4. **Memory Access Patterns**: Optimize for cache locality
5. **Platform-Specific Features**: Use when beneficial

### Profiling Integration

```cpp
class IRHIProfiler {
public:
    virtual void BeginEvent(const char* name) = 0;
    virtual void EndEvent() = 0;
    virtual void SetMarker(const char* name) = 0;
    
    // GPU timing
    virtual uint64_t GetTimestamp() = 0;
    virtual float GetTimestampPeriod() = 0;
};
```

---

## Testing Strategy

### Unit Tests
- Resource creation/destruction
- Memory allocation
- Command recording

### Integration Tests
- Triangle rendering
- Texture sampling
- Compute dispatch

### Performance Tests
- Draw call throughput
- Memory bandwidth
- Synchronization overhead

### Platform Tests
- Windows 10/11 + Vulkan
- macOS 12+ + Metal
- Future: Android + Vulkan, iOS + Metal

---

## References

- [Vulkan Specification](https://www.khronos.org/vulkan/)
- [Metal Programming Guide](https://developer.apple.com/metal/)
- [GPU Architecture Considerations](https://gpuopen.com)
- [3D Gaussian Splatting Paper](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/)