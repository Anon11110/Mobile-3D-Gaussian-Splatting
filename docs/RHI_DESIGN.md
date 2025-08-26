# Rendering Hardware Interface (RHI) Design Documentation

## Overview

The RHI provides a modern, efficient abstraction layer over graphics APIs. The interface is designed primarily following Vulkan's architecture and concepts, with planned support for both Vulkan and Metal backends. Currently only the Vulkan backend is implemented.

## Architecture

### Core Design Principles

1. **Clean Abstraction**: Pure virtual interfaces separate API-agnostic code from backend implementations, ensuring application logic remains portable
2. **Modern GPU Features**: Built around modern GPU paradigms like command queues, parallel command recording, and explicit synchronization, rather than legacy state-machine concepts
3. **Zero-Cost Abstractions**: Minimal overhead for Vulkan backend while providing safety and convenience
4. **Explicit Control**: Expose direct control over the GPU. All synchronization, memory transitions, and state management are performed explicitly by the user
5. **Resource-Oriented**: All GPU entities (buffers, textures, pipelines) are treated as distinct objects with clear ownership and lifetimes (managed via RAII)

### Layer Structure

```
Application Code
       ↓
  RHI Interface (rhi.h, rhi_types.h)
       ↓
  Backend Implementation Layer
       ├── Vulkan Backend (implemented)
       └── Metal Backend (planned)
       ↓
  Native APIs (Vulkan/Metal)
```

## Core Components

### 1. Device Interface (`IRHIDevice`)

The central factory and management interface for all RHI resources.

**Key Responsibilities:**
- Resource creation (buffers, textures, pipelines, etc.)
- Command list allocation
- Queue submission and synchronization
- Device-wide operations

**Design Decisions:**
- Returns `std::unique_ptr` for clear ownership semantics
- Separate creation methods for each resource type
- Explicit queue type specification for command lists

### 2. Resource Management

#### Buffers (`IRHIBuffer`)

**Features:**
- Flexible usage flags (Vertex, Index, Uniform, Storage)
- Resource usage hints for optimal memory placement
- Map/Unmap interface for CPU access
- Size querying

**Memory Strategy:**
```cpp
enum class ResourceUsage {
    Static,         // Immutable GPU-only data
    DynamicUpload,  // CPU→GPU streaming
    Readback,       // GPU→CPU transfers
    Transient       // Per-frame temporaries
};
```

**Allocation Hints:**
```cpp
struct AllocationHints {
    bool prefer_device_local = true;
    bool persistently_mapped = false;
    bool allow_dedicated     = false;
};
```

#### Textures (`IRHITexture`)

**Features:**
- Multi-dimensional support (1D, 2D, 3D, arrays)
- Mip level management
- Format abstraction
- Render target and depth-stencil support

#### Texture Views (`IRHITextureView`)

**Purpose:** Provides reinterpretation of texture data
- Subresource selection (mip levels, array layers)
- Format casting
- Aspect mask control (color, depth, stencil)

### 3. Pipeline Management

#### Graphics Pipelines

**State Groups:**
- **Vertex Input**: Layout and binding descriptions
- **Rasterization**: Polygon mode, culling, depth bias
- **Depth-Stencil**: Test/write operations, stencil ops
- **Multisample**: Sample count and shading
- **Color Blend**: Per-attachment blend states

**Monolithic Design:**
```cpp
struct GraphicsPipelineDesc {
    IRHIShader* vertexShader;
    IRHIShader* fragmentShader;
    VertexLayout vertexLayout;
    RasterizationState rasterizationState;
    DepthStencilState depthStencilState;
    MultisampleState multisampleState;
    std::vector<ColorBlendAttachmentState> colorBlendAttachments;
    RenderTargetSignature targetSignature;
    // ... descriptor layouts and push constants
};
```

#### Compute Pipelines

Simplified pipeline for compute workloads:
- Single shader stage
- Descriptor set layouts
- Push constant ranges

### 4. Command Recording

#### Command Lists (`IRHICommandList`)

**Lifecycle:**
```cpp
Begin() → Record Commands → End() → Submit
         ↓
      Reset() (for reuse)
```

**Key Operations:**
- Dynamic rendering (BeginRendering/EndRendering)
- Pipeline and descriptor binding
- Draw and dispatch calls (including indirect)
- Resource barriers and transitions
- Cross-queue synchronization


### 5. Synchronization

#### High-Level Resource States

Simplified state model for common cases:
```cpp
enum class ResourceState : uint8_t {
    Undefined,          // Initial/discardable
    GeneralRead,        // Generic read access
    ShaderReadWrite,    // UAV/SSBO access
    RenderTarget,       // Color attachment
    DepthStencilWrite,  // Depth buffer write
    Present,           // Ready for display
    // ... additional states
};
```

#### Fine-Grained Control

Optional explicit control for advanced use cases:
```cpp
enum class StageMask : uint64_t {
    Auto           = 0,
    VertexShader   = 1ull << 3,
    FragmentShader = 1ull << 4,
    ComputeShader  = 1ull << 7,
    // ... more stages
};

enum class AccessMask : uint64_t {
    Auto          = 0,
    ShaderRead    = 1ull << 5,
    ShaderWrite   = 1ull << 6,
    TransferRead  = 1ull << 11,
    TransferWrite = 1ull << 12,
    // ... more access types
};
```

#### Barrier System

Comprehensive barrier support for intra-queue synchronization:
```cpp
void Barrier(
    PipelineScope src_scope,
    PipelineScope dst_scope,
    std::span<const BufferTransition> buffer_transitions,
    std::span<const TextureTransition> texture_transitions,
    std::span<const MemoryBarrier> memory_barriers = {});
```

**Note:** The `Barrier` function is only for synchronization within the same queue. For inter-queue synchronization, use `ReleaseToQueue` and `AcquireFromQueue`.

#### Cross-Queue Transfers

Explicit queue ownership transfers:
```cpp
// Release from current queue
void ReleaseToQueue(
    QueueType dstQueue,
    std::span<const BufferTransition> buffer_transitions,
    std::span<const TextureTransition> texture_transitions);

// Acquire on destination queue
void AcquireFromQueue(
    QueueType srcQueue,
    std::span<const BufferTransition> buffer_transitions,
    std::span<const TextureTransition> texture_transitions);
```

### 6. Descriptor Management

#### Descriptor Sets and Layouts

Resource binding model:
```cpp
enum class DescriptorType {
    UNIFORM_BUFFER,
    STORAGE_BUFFER,
    SAMPLED_TEXTURE,
    STORAGE_TEXTURE,
    SAMPLER,
};

struct DescriptorBinding {
    uint32_t binding;
    DescriptorType type;
    uint32_t count = 1;
    ShaderStageFlags stageFlags;
};
```

### 7. Presentation

#### Swapchain (`IRHISwapchain`)

**Features:**
- Platform-agnostic window integration
- Dynamic resize support
- VSync control
- Multi-buffering configuration

**Operations:**
- `AcquireNextImage`: Get next available backbuffer
- `Present`: Queue presentation
- `Resize`: Handle window size changes

## Vulkan Backend Implementation

### Architecture

The Vulkan backend (`rhi::vulkan` namespace) provides concrete implementations of all RHI interfaces.

### Key Components

#### VulkanDevice

**Responsibilities:**
- Instance and device creation
- Queue family management (Graphics, Compute, Transfer)
- Extension loading (dynamic rendering, etc.)
- Memory allocator initialization

**Queue Architecture:**
```cpp
// Separate queues for different workloads
VkQueue graphicsQueue;
VkQueue computeQueue;
VkQueue transferQueue;

// Per-queue command pools
VkCommandPool graphicsCommandPool;
VkCommandPool computeCommandPool;
VkCommandPool transferCommandPool;
```

#### Memory Management (VMA Integration)

**Strategy:**
- Vulkan Memory Allocator (VMA) for efficient allocation
- Automatic memory type selection based on usage hints
- Support for dedicated allocations for large resources
- Persistent mapping for frequently updated buffers

**Resource Usage Mapping:**
```cpp
switch (desc.resourceUsage) {
    case ResourceUsage::Static:
        // Device local memory for best performance
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case ResourceUsage::DynamicUpload:
        // Host visible for CPU writes
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        break;
    case ResourceUsage::Readback:
        // Host visible for CPU reads
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        break;
    case ResourceUsage::Transient:
        // Allow aliasing for temporary resources
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
        break;
}
```

#### Dynamic Rendering

Modern rendering without traditional render passes:
- Uses Vulkan 1.3 or VK_KHR_dynamic_rendering extension
- Simplified API usage
- Direct BeginRendering/EndRendering calls
- Automatic attachment configuration

#### Barrier Implementation

Comprehensive synchronization:
```cpp
// Automatic stage and access deduction
void GetVulkanStagesAndAccess(
    ResourceState state,
    PipelineScope scope,
    VkPipelineStageFlags& stages,
    VkAccessFlags& access);

// Batch barrier submission
vkCmdPipelineBarrier(
    commandBuffer,
    srcStageFlags,
    dstStageFlags,
    0,
    memoryBarrierCount, pMemoryBarriers,
    bufferBarrierCount, pBufferBarriers,
    imageBarrierCount, pImageBarriers);
```

**Automatic Inference:** When buffer/texture transitions are empty, the barrier still executes with stage and access flags automatically inferred from the `PipelineScope` parameters. This allows for simple memory barriers without explicit resource transitions.

### Vulkan-Specific Optimizations

1. **Staging Buffers**: Automatic staging for static resource uploads
2. **Descriptor Set Pooling**: Per-queue pools for allocation efficiency
3. **Queue Family Transfers**: Proper ownership management for multi-queue

## Usage Patterns

### Basic Rendering Loop

```cpp
// Create and begin command list
auto cmdList = device->CreateCommandList(QueueType::GRAPHICS);
cmdList->Begin();

// Setup rendering
RenderingInfo renderInfo;
renderInfo.colorAttachments.push_back({
    .view = colorTarget,
    .loadOp = LoadOp::CLEAR,
    .storeOp = StoreOp::STORE,
    .clearValue = {{0.0f, 0.0f, 0.0f, 1.0f}}
});

// Record commands
cmdList->BeginRendering(renderInfo);
cmdList->SetPipeline(pipeline);
cmdList->SetVertexBuffer(0, vertexBuffer);
cmdList->BindDescriptorSet(0, descriptorSet);
cmdList->Draw(vertexCount);
cmdList->EndRendering();

// Submit
cmdList->End();
device->SubmitCommandLists({cmdList.get()}, QueueType::GRAPHICS);
```

### Resource Synchronization

```cpp
// Transition texture for shader read
TextureTransition transition{
    .texture = myTexture,
    .before = ResourceState::Undefined,
    .after = ResourceState::GeneralRead
};
cmdList->Barrier(
    PipelineScope::All,
    PipelineScope::Graphics,
    {},
    {transition}
);
```

### Cross-Queue Compute

```cpp
// Graphics queue: release texture to compute
graphicsCmdList->ReleaseToQueue(
    QueueType::COMPUTE,
    {},
    {textureTransition}
);

// Compute queue: acquire, process, and release back
computeCmdList->AcquireFromQueue(
    QueueType::GRAPHICS,
    {},
    {textureTransition}
);
computeCmdList->SetPipeline(computePipeline);
computeCmdList->Dispatch(groupX, groupY, groupZ);
computeCmdList->ReleaseToQueue(
    QueueType::GRAPHICS,
    {},
    {textureTransition}
);

// Graphics queue: reacquire texture
graphicsCmdList->AcquireFromQueue(
    QueueType::COMPUTE,
    {},
    {textureTransition}
);
```