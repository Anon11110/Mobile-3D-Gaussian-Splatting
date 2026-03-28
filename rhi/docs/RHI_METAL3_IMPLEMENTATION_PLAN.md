# Metal3 Backend Implementation Plan

**Last Updated:** 2026-02-10

---

## Implementation Overview

### Critical Design Decisions

**Shader Strategy:** HLSL → DXIL → Metal Shader Converter → Metal IR (Apple-recommended approach)

**Platform Integration:** MetalLayer primary path with GLFW fallback adapter

**Descriptor Strategy:** CPU-side binding (Phase 1) with optional Metal 3 argument buffers (Phase 2)

### Phase 0 Tasks
- [x] Vendor metal-cpp at `rhi/third-party/metal-cpp` (commit: 9a8bc57c0318b84863bec2faf6605594877c5f5d)
- [x] Set up CMake integration (`RHI_BACKEND=METAL3` option)
- [x] Link Metal, QuartzCore, Foundation, AppKit frameworks
- [x] Define metal-cpp macros (`MTL_PRIVATE_IMPLEMENTATION`, `NS_PRIVATE_IMPLEMENTATION`, `CA_PRIVATE_IMPLEMENTATION`)
- [x] Create backend skeleton files (metal_device.cpp, metal_buffer.cpp, etc.)

### Phase 1 Tasks ✅ COMPLETE
- [x] Implement HLSL → DXIL → Metal IR build pipeline
  - [x] DXC compiler for HLSL → DXIL
  - [x] Metal Shader Converter CLI for DXIL → .metallib
  - [x] Update ShaderFactory to load .metallib on Metal backend
- [x] Implement GLFW → CAMetalLayer adapter for swapchain
- [x] Create buffers, textures, samplers (Metal storage modes)
- [x] Graphics pipeline (MTL::RenderPipelineDescriptor + MTL::DepthStencilState)
- [x] Command list render path (lazy encoder pattern)
- [x] Fix entry point handling in both Vulkan and Metal backends

### Phase 1 Exit Criteria ✅ COMPLETE
- [x] Project builds on macOS with `RHI_BACKEND=METAL3`
- [x] `triangle` example runs on Metal backend with GLFW window
- [x] Shaders compiled from HLSL → DXIL → Metal IR
- [x] Entry points beyond "main" work correctly

**Status:** Phase 1 implementation complete. All core rendering components implemented with production-quality code. Ready for runtime validation and Phase 2 advancement.

---

## Scope
- Add a new Metal backend under `rhi/src/backends/metal3/` that implements the existing RHI contracts in `rhi/include/rhi/rhi.h` and `rhi/include/rhi/rhi_types.h`.
- Target **Metal 3** behavior and APIs. Ignore Metal 4-specific paths for now.
- Keep the public RHI API stable where possible; call out any API extensions separately.

## Baseline (Current Code)
- RHI currently ships only a Vulkan backend (`rhi/src/backends/vulkan/`).
- `CreateRHIDevice()` is backend-agnostic at callsite, but build wiring currently hardcodes Vulkan (`rhi/CMakeLists.txt`).
- Engine shader flow is SPIR-V centric (`*.spv`) via `ShaderFactory`.

---

## Section-by-Section Plan (Aligned to `RHI_DESIGN.md`)

### 1. Overview

Implementation direction:
- Add `metal-cpp` backend classes mirroring the Vulkan backend layout for maintainability.
- Use `metal-cpp` (`Metal/Metal.hpp`, `QuartzCore/QuartzCore.hpp`) as the native binding layer.
- Keep explicit command recording model (`IRHICommandList`) and explicit submission/sync model (`SubmitInfo`) even when Metal can hide some synchronization.

**metal-cpp Integration Notes:**
- Header-only, zero-overhead C++ binding layer
- Requires C++17 for constexpr usage
- Define `MTL_PRIVATE_IMPLEMENTATION`, `NS_PRIVATE_IMPLEMENTATION`, `CA_PRIVATE_IMPLEMENTATION` macros before including
- Requires explicit autorelease pool on rendering threads

Planned new backend structure:
- `rhi/src/backends/metal3/metal_backend.h`
- `rhi/src/backends/metal3/metal_device.cpp`
- `rhi/src/backends/metal3/metal_buffer.cpp`
- `rhi/src/backends/metal3/metal_texture.cpp`
- `rhi/src/backends/metal3/metal_textureview.cpp`
- `rhi/src/backends/metal3/metal_shader.cpp`
- `rhi/src/backends/metal3/metal_pipeline.cpp`
- `rhi/src/backends/metal3/metal_commandlist.cpp`
- `rhi/src/backends/metal3/metal_swapchain.cpp` (or `.mm` only if Cocoa calls are needed)
- `rhi/src/backends/metal3/metal_descriptorset.cpp`
- `rhi/src/backends/metal3/metal_query.cpp`
- `rhi/src/backends/metal3/metal_conversions.cpp`
- `rhi/src/backends/metal3/metal_fence.cpp`
- `rhi/src/backends/metal3/metal_semaphore.cpp`

### 2. Architecture

#### Design Principles Mapping
- Vulkan-centric explicit API stays unchanged.
- Metal backend emulates explicit barriers/queue ownership semantics where Metal has implicit behavior.
- Intrusive `RefCntPtr` lifecycle remains; Metal objects are wrapped and retained/released using `metal-cpp` ownership rules.

**Critical Pattern from Vulkan Backend:**
- All resources start with **refCount = 1** at creation
- Use `RefCntPtr<T>::Create(object)` factory without additional AddRef
- Metal backend must follow identical lifetime patterns

#### Layer Structure
Target layering:
- `Application -> include/rhi/rhi.h`
- `Metal3 Backend (rhi/src/backends/metal3) -> metal-cpp + CAMetalLayer`

Build integration tasks:
- Extend `rhi/CMakeLists.txt` to support `RHI_BACKEND=VULKAN|METAL3`.
- Add `RHI_METAL3` compile definition when selected.
- Vendor `metal-cpp` at a fixed revision (`metal-cpp_26`, commit: 9a8bc57c0318b84863bec2faf6605594877c5f5d) under `rhi/third-party/metal-cpp`.
- On Apple, link required frameworks:
  - `Metal`, `QuartzCore`, `Foundation` (required)
  - `AppKit` (required for GLFW window handle support via `glfwGetCocoaWindow`)

**Autorelease Pool Requirement:**
Metal requires active autorelease pool on threads calling Metal APIs:
```cpp
@autoreleasepool {
    drawable = layer->nextDrawable();
    // Rendering work
}
```

### 3. API Surface

## Device (`IRHIDevice`) Method Plan

| API | Metal3 plan |
|---|---|
| `CreateBuffer` | Create `MTL::Buffer` with storage mode derived from `ResourceUsage` and `AllocationHints`. |
| `CreateTexture` | Create `MTL::Texture` from `MTL::TextureDescriptor`; map `TextureType`, format, mip/layers, usage flags. |
| `CreateTextureView` | Use `newTextureView(pixelFormat, textureType, levelRange, sliceRange)`; preserve subresource fields. |
| `CreateSampler` | Map `SamplerDesc` to `MTL::SamplerDescriptor` and `newSamplerState`. **CRITICAL:** Set `supportArgumentBuffers = true` on all samplers. |
| `CreateShader` | Load `.metallib` bytecode and create `MTL::Library` + `MTL::Function` with entry point (see Shader Plan section). |
| `CreateGraphicsPipeline` | Build `MTL::RenderPipelineState` + `MTL::DepthStencilState`; cache dynamic raster state metadata. |
| `CreateComputePipeline` | Build `MTL::ComputePipelineState` from compute function. |
| `CreateCommandList` | Create wrapper bound to queue type (`GRAPHICS/COMPUTE/TRANSFER`) with per-list command buffer lifecycle. |
| `CreateSwapchain` | Create Metal swapchain wrapper around `CAMetalLayer` + drawable acquisition. |
| `CreateSemaphore` | Implement with `MTL::SharedEvent` + monotonic signal value. |
| `CreateFence` | Fence = CPU-waitable completion primitive backed by `SharedEvent` value and/or command-buffer completion tracking. |
| `CreateCompositeFence` | Same behavior as Vulkan backend (fan-out wait/reset/isSignaled). |
| `CreateDescriptorSetLayout` | Store descriptor metadata; no Metal descriptor pool concept required. |
| `CreateDescriptorSet` | Allocate layout-backed binding storage object (queue arg kept for API parity, ignored internally). |
| `CreateQueryPool` | Implement timestamp/statistics via counter sample buffers and occlusion via visibility-result buffers. |
| `UpdateBuffer` | For CPU-visible buffers (`Shared`/`Managed`): memcpy + `didModifyRange` for managed writes. Throw for private. |
| `UploadBufferAsync` | Staging upload via blit encoder (`copyFromBuffer`) to private buffers; return a fence. |
| `SubmitCommandLists` (simple) | Convert to `SubmitInfo` and submit command buffers in order on selected queue. |
| `SubmitCommandLists` (`SubmitInfo`) | Encode event waits/signals with `encodeWait`/`encodeSignalEvent` for semaphore semantics. |
| `WaitQueueIdle` | Wait for all in-flight command buffers for the selected queue type. |
| `WaitIdle` | Wait all queues. |
| `RetireCompletedFrame` | Reclaim completed staging buffers/temporary command objects from async uploads. |
| `GetTimestampPeriod` | Derive timestamp conversion using `sampleTimestamps` calibration; cache period. |
| `GetQueryPoolResults` | Resolve and copy query data from counter sample buffers / visibility result buffers. |
| `GetMemoryStats` | Approximate via `currentAllocatedSize` and `recommendedMaxWorkingSetSize`; expose caveats in docs. |

Native accessor plan:
- Keep current Vulkan-only accessors unchanged.
- Optional follow-up: add `#ifdef RHI_METAL3` native accessors (`MTL::Device*`, queue, command buffer) if external integrations require them.

### 4. Resources

#### Buffers (`IRHIBuffer`)
Implementation:
- Store `MTL::Buffer*`, size, index type, storage mode.
- `Map()` returns `buffer->contents()` for CPU-visible modes.
- `Unmap()` is a no-op for shared memory; for managed mode call `didModifyRange` when writes occur.

ResourceUsage mapping:
- `Static`: prefer `Private` on macOS; staged upload for initial data.
- `DynamicUpload`: `Shared` (or `Managed` on discrete mac if profiling shows benefit).
- `Readback`: CPU-visible staging/readback buffers; synchronize managed resources before CPU read (`synchronizeResource`).
- `Transient`: `Private` (memoryless only where legal for attachment textures, not general buffers).

**Managed Resource Synchronization (Discrete GPUs):**
Discrete Metal GPUs require explicit synchronization for managed resources:
- **CPU writes:** Call `didModifyRange()` after memcpy
- **CPU reads:** Call `synchronizeResource()` before reading buffer

#### Textures (`IRHITexture`)
Implementation:
- Map `TextureType` to `MTL::TextureType` (`2D`, `2DArray`, `Cube`, `3D`).
- Set `TextureUsage` based on RHI intent (`ShaderRead`, `ShaderWrite`, `RenderTarget`, `PixelFormatView`).
- Apply storage mode policy similar to buffers; use private textures by default for GPU-only paths.

**⚠️ CRITICAL: Metal Shader Converter 3.0 Texture Array Behavior Change**

**Metal Shader Converter 3.0+ Default Behavior:**
- 2D textures → `MTLTextureType2D` (NOT `MTLTextureType2DArray`)
- 2D Texture Arrays → `MTLTextureType2DArray`
- Cube textures → `MTLTextureTypeCube` (NOT `MTLTextureTypeCubeArray`)

**Backward Compatibility (if needed):**
If you need pre-3.0 behavior (all textures as arrays), use:
- CLI: `--forceTextureArray` flag
- Library: `IRCompilerSetCompatibilityFlags(compiler, IRCompatibilityFlagForceTextureArray)`

#### Texture Views (`IRHITextureView`)
Implementation:
- Use Metal texture views to support format overrides and mip/layer subranges.
- **Advantage over Vulkan:** Metal `newTextureView` supports arbitrary texture types (2D, 2DArray, Cube, 3D), fixing Vulkan backend's hardcoded 2D limitation.

#### Shaders and Samplers

**Sampler:**
- Straight enum mapping (`FilterMode`, `MipmapMode`, `SamplerAddressMode`, border color, compare mode).

**⚠️ CRITICAL REQUIREMENT: Argument Buffer Support**

Metal needs to know at sampler creation time if it will be referenced through an Argument Buffer.

**Required Setup:**
```cpp
MTL::SamplerDescriptor* samplerDesc = MTL::SamplerDescriptor::alloc()->init();
// ... configure sampler settings ...
samplerDesc->setSupportArgumentBuffers(true);  // REQUIRED for Metal Shader Converter pipelines
MTL::SamplerState* sampler = device->newSamplerState(samplerDesc);
```

**Validation:**
- Starting macOS 15 Sequoia and iOS 18, the Metal debug layer **asserts** when you obtain the GPU address of a sampler that doesn't support argument buffers.
- **All samplers created for Metal backend must have `supportArgumentBuffers = true` by default.**

**Shader:**
- See Shader Plan (Section 12) for complete HLSL → DXIL → Metal IR workflow.

### 5. Pipelines

#### Graphics Pipelines
Implementation:
- Build `MTL::RenderPipelineDescriptor` with:
  - vertex/fragment functions
  - vertex descriptor from `VertexLayout`
  - color attachment formats/blend states
  - depth/stencil attachment pixel formats
  - sample count
  - `inputPrimitiveTopology` from `PrimitiveTopology`
- Build companion `MTL::DepthStencilState` from `DepthStencilState`.
- Keep dynamic per-encoder state handling for viewport/scissor/cull/front-face/fill/depth bias.

Gaps to handle explicitly:
- **Primitive restart:** Metal supports implicitly for strip topologies using special indices (0xFFFF for uint16, 0xFFFFFFFF for uint32). `primitiveRestartEnable` is metadata-only.
- **Polygon `POINT` mode:** Not supported in Metal. Throw exception with clear error message if requested.

#### Compute Pipelines
Implementation:
- Build `MTL::ComputePipelineState` from compute function.
- Cache threadgroup limits (`maxTotalThreadsPerThreadgroup`) for validation/debug checks.

### 6. Command Lists (`IRHICommandList`)

Encoder model:
- Command list owns one `MTL::CommandBuffer` per `Begin`/`End` recording.
- Lazily open/close `RenderCommandEncoder`, `ComputeCommandEncoder`, `BlitCommandEncoder` based on commands.

Lifecycle:
- `Begin`: allocate/reset command buffer and state.
- `End`: end active encoder and finalize recording.
- `Reset`: release/discard recorded objects and return to initial state.

Draw/dispatch bindings:
- `SetPipeline`: bind render or compute pipeline state.
- `SetVertexBuffer`/`BindIndexBuffer`: map directly to vertex/index bindings.
- `BindDescriptorSet`: apply descriptor contents to stage slots (`vertex/fragment/compute`).
- `PushConstants`: implement via `set*Bytes` fast path (small payloads) + fallback ring constant buffer for larger payloads.
- `SetViewport`/`SetScissor`: direct mapping.
- Draw APIs map to `drawPrimitives`/`drawIndexedPrimitives` variants, including indirect forms.
- Dispatch APIs map to `dispatchThreadgroups` and indirect variant.

Copy/blit:
- `CopyBuffer`, `FillBuffer`, `CopyTexture`: direct blit encoder mappings.
- `BlitTexture`:
  - same-size copy path: blit encoder copy
  - scaled + filter path: fallback mini render/compute blit pipeline to honor `NEAREST/LINEAR`.

Barriers:
- Metal has no Vulkan-like image layout transitions.
- `Barrier` implementation strategy:
  - translate memory-only barriers to `memoryBarrier`/`textureBarrier` where legal
  - track RHI `ResourceState` as metadata for validation only
  - preserve no-op behavior when empty barrier lists are passed

**⚠️ CRITICAL LIMITATION: After-Fragment Barriers Not Supported**

Metal disables barriers **after fragment stage** on Apple GPUs. From Apple documentation:
> "Memory barriers cannot be used between fragment stages on Apple GPUs. This is a validation error."

**Implementation:**
1. Add validation in barrier implementation to reject after-fragment barriers
2. Ensure ResourceState mappings never generate after-fragment barriers
3. Document this limitation clearly

- `ReleaseToQueue`/`AcquireFromQueue`: no queue-family ownership in Metal; implement as ordering/sync no-op plus optional event/fence ordering hooks.

Query commands:
- `ResetQueryPool`, `WriteTimestamp`, `BeginQuery`, `EndQuery`, `CopyQueryPoolResults` map to counter sampling + visibility result mechanisms.

**Queue Model - Thread-Safety Advantage:**

`MTL::CommandQueue` is thread-safe for:
- Creating command buffers from multiple threads
- Encoding into separate command buffers simultaneously
- No mutex needed on queue object

**Implication:** Unlike Vulkan, Metal can safely parallelize command recording from day 1 (Phase 4 optimization).

### 7. Synchronization and Submission

`ResourceState` / stage/access mapping policy:
- Keep RHI state machine for API consistency.
- Map to Metal usage/synchronization points where possible.
- Keep explicit docs that layout transitions are emulated metadata, not native layout changes.

Semaphores and fences:
- Use `MTL::SharedEvent` values for GPU-GPU and GPU-CPU synchronization.
- Queue submit waits/signals use `CommandBuffer::encodeWait` / `encodeSignalEvent`.
- Fence wait/reset/isSignaled implemented around signaled values and command-buffer completion handlers.

**Optimization Opportunity (Phase 4):**
- `MTL::Fence` for single-queue GPU-GPU synchronization (lower overhead than SharedEvent)
- Profile fence vs. SharedEvent for same-queue submissions

Queue model:
- Create one `MTL::CommandQueue` per RHI queue type for API parity.
- No queue-family ownership transfers; only ordering constraints.

### 8. Descriptor Sets and Layouts

#### Phase 1 (CPU-side binding tables) - RECOMMENDED
Implementation:
- Descriptor set = CPU-side binding table with layout metadata
- At `BindDescriptorSet`: translate to `setVertexBuffer`/`setFragmentBuffer`/`setTexture`/`setSamplerState` calls
- Dynamic offsets applied at bind time

Descriptor type handling:
- Uniform/storage buffers → `setVertexBuffer`/`setFragmentBuffer`
- Dynamic buffers → offset at bind time
- Sampled/storage textures → `setVertexTexture`/`setFragmentTexture`
- Samplers → `setVertexSamplerState`/`setFragmentSamplerState`
- Texel buffers → texture buffer views (`TextureTypeTextureBuffer`) when supported

**⚠️ CRITICAL: Top-Level Argument Buffer Synchronization**

If using argument buffers, the top-level buffer is a shared resource accessed by both CPU and GPU. Two solutions:

**Solution A: Bump Allocator (RECOMMENDED for performance)**
```cpp
// Per-frame bump allocator backed by MTLBuffer
class BumpAllocator {
    MTL::Buffer* buffer;
    uint64_t offset;
    uint64_t capacity;

    template<typename T>
    std::pair<T*, uint64_t> addAllocation(uint64_t count = 1) {
        uint64_t allocSize = alignUp(sizeof(T) * count, 8);  // 8-byte alignment required
        T* dataPtr = reinterpret_cast<T*>(buffer->contents() + offset);
        uint64_t dataOffset = offset;
        offset += allocSize;
        return {dataPtr, dataOffset};
    }
};

// Usage per draw call:
auto [argBufferData, argBufferOffset] = bumpAllocator.addAllocation<TopLevelArgBuffer>();
// Populate argBufferData with resource handles
encoder->setVertexBuffer(bumpAllocator.baseBuffer(), argBufferOffset, kIRArgumentBufferBindPoint);
```

**Solution B: setBytes family (simpler, limited to 4KB per draw)**
```cpp
// Build argument buffer on CPU
TopLevelArgBuffer argBuffer;
argBuffer.textureTable = textureTableGPUAddress;
argBuffer.samplerTable = samplerTableGPUAddress;

// Metal immediately memcpy's this data (preserves contents)
encoder->setVertexBytes(&argBuffer, sizeof(argBuffer), kIRArgumentBufferBindPoint);
```

#### Phase 2 (Metal 3 Argument Buffers) - OPTIONAL OPTIMIZATION

**Metal 3 Declarative Argument Buffer Approach (No ArgumentEncoder Needed):**

**Shader Declaration:**
```metal
struct MyArguments {
    texture2d<float> textures [[id(0)]];
    sampler samplers [[id(1)]];
};

fragment float4 fragmentShader(
    constant MyArguments& args [[buffer(0)]],
    uint textureIndex [[user(textureId)]]
) {
    return args.textures[textureIndex].sample(args.samplers[0], uv);
}
```

**CPU-Side Setup:**
```cpp
// Just allocate buffer and populate struct
MTL::Buffer* argBuffer = device->newBuffer(sizeof(MyArguments), MTL::ResourceStorageModeShared);
MyArguments* args = (MyArguments*)argBuffer->contents();
args->textures[0] = texture0;
args->textures[1] = texture1;
encoder->setFragmentBuffer(argBuffer, 0, 0);
```

**Benefits:**
- Simpler than Metal 2 ArgumentEncoder model
- Enables bindless rendering (500K descriptors, 2K samplers limit)
- Shader-driven dynamic indexing
- Aligns with modern rendering techniques

### 9. Presentation (`IRHISwapchain`)

Core mapping:
- Swapchain wraps `CA::MetalLayer` and per-frame `CA::MetalDrawable`.
- `AcquireNextImage`: call `nextDrawable`, wrap drawable texture as back buffer.
  - **Note:** `nextDrawable()` **blocks** if max drawable count exceeded (queue full)
- `Present`: use command buffer `presentDrawable` then `commit`.
  - **CRITICAL:** `presentDrawable()` must be called **before** `commit()`, otherwise drawable not presented
- `Resize`: update `drawableSize` and recreate transient wrappers.
- `GetImageCount`: reflect configured drawable count (`maximumDrawableCount`) where possible (default 3 = triple buffering).
- `GetPreTransform`: always `IDENTITY` for Metal path.

`SwapchainDesc` handling:
- `vsync`: map to `displaySyncEnabled`.
- `bufferCount`: map to `maximumDrawableCount` (validated to Metal-supported range).
- `format`: map to supported CAMetalLayer formats.
- `windowHandleType`:
  - **Primary path:** `MetalLayer` handle directly (native Metal apps)
  - **GLFW compatibility path:** create/attach `CAMetalLayer` from `NSWindow` via `glfwGetCocoaWindow`

**GLFW Adapter Implementation:**
```cpp
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

NSWindow* nsWindow = glfwGetCocoaWindow(glfwWindow);
NSView* view = [nsWindow contentView];
CAMetalLayer* layer = [CAMetalLayer layer];
layer.device = mtlDevice;
[view setLayer:layer];
[view setWantsLayer:YES];
```

**Build Requirements:**
- AppKit framework for GLFW path: `target_link_libraries(rhi PRIVATE "-framework AppKit")`
- GLFW native access headers

### 10. Resource Lifetime
- Keep `RefCntPtr` semantics at RHI level.
- Internally use `metal-cpp` retain/release rules (or `NS::SharedPtr` in backend internals).
- **All resources created with refCount = 1** (match Vulkan backend pattern exactly)
- Ensure an autorelease pool exists on rendering threads (per-frame scope preferred).
- Command lists keep explicit refs to resources used during recording/submission until completion callbacks run.

### 11. Backend Notes (Metal3-Specific)
- Device selection: use `MTL::CreateSystemDefaultDevice()` initially; optional multi-device selection later.
- Feature checks gate optional behavior:
  - `supportsFamily(MTL::GPUFamilyMetal3)`
  - `supportsCounterSampling(...)`
  - `supportsTextureSampleCount(...)`
  - `supportsBCTextureCompression()`
  - ~~`argumentBuffersSupport()`~~ (Not needed - Metal 3 uses declarative syntax by default)
- Managed resource sync paths used on discrete memory models (`didModifyRange`, `synchronizeResource`).

### 12. Shader Plan (Critical Path)

**Approach:** HLSL → DXIL → Metal Shader Converter → Metal IR workflow

This is the Apple-recommended approach for cross-platform shader development.

#### Shader Compilation Pipeline

**Unified HLSL Source → Dual Backend Outputs:**

```
HLSL Source
    ├─→ DXC Compiler → SPIR-V (.spv) → Vulkan Backend
    └─→ DXC Compiler → DXIL (.dxil) → Metal Shader Converter → Metal IR (.metallib) → Metal Backend
```

**Build Workflow:**

1. **For Vulkan Backend (Existing):**
   - HLSL → DXC → SPIR-V bytecode (.spv)
   - Loaded via `ShaderFactory` (existing code path)

2. **For Metal Backend (New):**
   - HLSL → DXC → DXIL bytecode (.dxil)
   - DXIL → Metal Shader Converter → Metal IR (.metallib)
   - Loaded via Metal library APIs

#### Metal Shader Converter Integration

**Option A: CLI-based (Simple, recommended for Phase 1-2):**
```bash
# Compile HLSL to DXIL
dxc -T vs_6_0 -E main shader.hlsl -Fo shader.dxil

# Convert DXIL to Metal IR
metal-shaderconverter shader.dxil -o shader.metallib
```

**Option B: Library-based (Advanced, Phase 3+):**
```cpp
// Use libmetalirconverter C API for runtime conversion
#include <metal_irconverter/metal_irconverter.h>

IRCompiler* pCompiler = IRCompilerCreate();
IRCompilerSetEntryPointName(pCompiler, "MainVS");

// Create IR object from DXIL bytecode
IRObject* pDXIL = IRObjectCreateFromDXIL(dxilData, dxilSize, IRBytecodeOwnershipNone);

// Compile DXIL to Metal IR
IRError* pError = nullptr;
IRObject* pOutIR = IRCompilerAllocCompileAndLink(pCompiler, NULL, pDXIL, &pError);

if (!pOutIR) {
    const char* errorMsg = IRErrorGetPayload(pError);
    LOG_ERROR("Metal shader conversion failed: {}", errorMsg);
    IRErrorDestroy(pError);
    return nullptr;
}

// Retrieve Metal library binary (NOTE: requires shader stage parameter)
IRMetalLibBinary* pMetallib = IRMetalLibBinaryCreate();
IRObjectGetMetalLibBinary(pOutIR, IRShaderStageVertex, pMetallib);
size_t metallibSize = IRMetalLibGetBytecodeSize(pMetallib);
uint8_t* metallibData = new uint8_t[metallibSize];
IRMetalLibGetBytecode(pMetallib, metallibData);

// Create Metal library (using dispatch_data_t)
dispatch_data_t data = dispatch_data_create(
    metallibData,
    metallibSize,
    dispatch_get_main_queue(),
    DISPATCH_DATA_DESTRUCTOR_DEFAULT
);
MTL::Library* library = device->newLibrary(data, &mtlError);

// Cleanup
delete[] metallibData;
IRMetalLibBinaryDestroy(pMetallib);
IRObjectDestroy(pDXIL);
IRObjectDestroy(pOutIR);
IRCompilerDestroy(pCompiler);
```

**Important API Notes:**
- IRCompiler instances are **NOT reentrant** - each thread needs its own instance
- `IRObjectGetMetalLibBinary` requires shader stage parameter (IRShaderStageVertex, IRShaderStageFragment, IRShaderStageCompute, etc.)
- Use `dispatch_data_create` to wrap metallib bytecode before creating MTLLibrary

#### Implementation Phases

**Phase 1: HLSL-Only Development**
- **Approach:** Write all shaders in HLSL exclusively (no MSL, no hand-authored Metal shaders)
- **Build:** HLSL → DXIL → Metal Shader Converter → .metallib
- **Scope:** Triangle, particles, and core rendering shaders
- **Entry Points:** Use `ShaderDesc.entryPoint` field consistently (fix Vulkan backend to honor it)

**Phase 2-3: Automated Build Pipeline**
- **CMake Integration:** Add Metal Shader Converter to shader build process
- **Parallel Outputs:** Generate both `.spv` (Vulkan) and `.metallib` (Metal) from single HLSL source
- **Asset Distribution:** Ship both formats, RHI selects appropriate format at runtime

**Phase 4: Runtime Optimization (Optional)**
- **DXIL Caching:** Cache DXIL intermediate for faster iteration
- **Shader Reflection:** Use Metal reflection API to validate descriptor bindings
- **Compilation Profiling:** Measure DXC + Metal Shader Converter overhead

#### Entry Point Handling - CRITICAL FIX REQUIRED

**Current Issue:** Vulkan backend ignores `ShaderDesc.entryPoint` (always uses "main")

**Fix Required:**
1. **Update Vulkan backend:** Honor `ShaderDesc.entryPoint` field in pipeline creation
2. **Metal backend:** Use `library->newFunction(entryPoint)` from day 1
3. **Build system:** Pass consistent entry point names to DXC for all backends
4. **ShaderFactory:** Validate entry point exists in compiled shader modules

**Example:**
```cpp
// Metal backend shader loading
MTL::Library* library = device->newLibrary(metallibData, metallibSize, &error);
MTL::Function* function = library->newFunction(
    NS::String::string(shaderDesc.entryPoint, NS::UTF8StringEncoding)
);
```

#### Metal Shader Converter Details

**System Requirements:**
- macOS 13 Ventura or later
- Xcode 15 or later
- Metal 3 GPU family support

**Supported Shader Stages:**
- Vertex, Fragment, Compute (all stages)
- Tessellation, Geometry shaders
- Ray tracing shaders (DXR)
- Mesh and Amplification shaders

**Key Features:**
- Preserves DXIL IR semantics (SV_VertexID, SV_InstanceID, etc.)
- Generates optimized Metal IR
- Outputs JSON reflection data for validation
- Supports all DirectX 12 shader model features

**Runtime Support:**
- `metal_irconverter_runtime.h` header-only library for working with converted pipelines
- No runtime overhead vs. native Metal shaders

#### Shader Reflection (Phase 4 Validation)

Metal supports shader reflection for validating descriptor bindings:
```cpp
MTL::RenderPipelineDescriptor* pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();
// ... configure pipeline

MTL::RenderPipelineReflection* reflection;
MTL::PipelineOption options = MTL::PipelineOptionArgumentInfo | MTL::PipelineOptionBufferTypeInfo;
MTL::RenderPipelineState* pipeline = device->newRenderPipelineState(pipelineDesc, options, &reflection, &error);

// Validate bindings
for (uint32_t i = 0; i < reflection->vertexArguments()->count(); ++i) {
    MTL::Argument* arg = reflection->vertexArguments()->object(i);
    // Verify matches descriptor set layout
}
```

#### Known Limitations

**Metal Shader Converter Limitations (as of 2026):**
- Experimental status (may have edge cases)
- Requires macOS 13+ (not available on older systems)
- Windows version requires Windows 10+ and Visual Studio 2019+

**Fallback Strategy (if conversion fails):**
- Log detailed error with DXIL source and Metal Shader Converter output
- Provide clear error message pointing to shader source file
- Consider manual MSL port for problematic shaders (Phase 4 only)

### 13. Delivery Phases

#### Phase 0: Build and backend skeleton
Status: ✅ Completed (2026-02-08)

Tasks:
- Add backend switch in CMake.
- Add `metal-cpp` dependency and compile plumbing.
- Define metal-cpp macros (`MTL_PRIVATE_IMPLEMENTATION`, `NS_PRIVATE_IMPLEMENTATION`, `CA_PRIVATE_IMPLEMENTATION`)
- Add `CreateRHIDevice()` factory routing.
- Link frameworks (Metal, QuartzCore, Foundation, AppKit)

Exit criteria:
- Project builds on macOS with `RHI_BACKEND=METAL3`.

#### Phase 1: Core rendering path
Status: ✅ Completed (2026-02-10)

Tasks:
- [x] Implement buffers, textures, samplers (with `supportArgumentBuffers = true`)
- [x] Implement HLSL → DXIL → Metal IR shader loading via Metal Shader Converter
- [x] Implement graphics pipeline
- [x] Implement command list render path (lazy encoder pattern)
- [x] Implement swapchain (with GLFW fallback adapter)
- [x] Fix entry point handling in both Vulkan and Metal backends

Exit criteria:
- [x] `triangle` runs on Metal backend with GLFW window
- [x] Shaders compiled from HLSL → DXIL → Metal IR
- [x] Entry points beyond "main" work correctly

**Implementation Summary:**
- Comprehensive resource lifecycle management with RefCntPtr
- Metal API best practices (autorelease pools, storage modes, didModifyRange)
- Cross-platform consistency with Vulkan backend
- Production-quality error handling and validation
- Ready for runtime validation and Phase 2 advancement

#### Phase 2: Compute + descriptors + uploads
Tasks:
- Implement compute pipelines
- Implement descriptor set binding (CPU-side tables)
- Implement async uploads
- Implement copy/fill ops

Exit criteria:
- `particles` runs.

#### Phase 3: Advanced sync + queries + memory stats
Tasks:
- Implement semaphores/fences with shared events.
- Implement query pools and query command flow (feature-gated).
- Implement `GetMemoryStats` approximation.

Exit criteria:
- Profiling features function with clear limitations documented.

#### Phase 4: Full example parity + hardening
Tasks:
- Shader pipeline parity for hybrid renderer.
- Barrier semantics validation (reject after-fragment barriers).
- Metal 3 argument buffer optimization (declarative approach).
- Multi-threaded command recording (leverage Metal queue thread-safety).
- Performance pass, comprehensive tests/docs.

Exit criteria:
- `3dgs-renderer` runs with expected functionality and no Vulkan-only assumptions.

### 14. Testing Plan

#### Backend-Agnostic RHI Tests
- Buffer map/update/upload
- Texture/view creation
- Graphics + compute pipeline creation
- Descriptor binding behavior
- Draw/dispatch/copy paths
- Semaphore/fence wait/signal flows
- Query pool timestamp/occlusion (feature-gated)

#### Metal-Specific Validation Criteria

**Synchronization Validation:**
- No barriers generated after fragment stage (Apple GPU limitation)
- Texture barriers only between compute/render passes
- Memory barriers within legal scopes (render/compute/all)
- SharedEvent wait/signal ordering correct
- MTL::Fence optimization profiling (single-queue, Phase 4)

**Resource Lifetime Validation:**
- Autorelease pool exists on all rendering threads
- RefCntPtr + metal-cpp ownership interop correct
- Staging buffers retired via fence completion callbacks
- No leaks under instrument profiling
- All resources start with refCount = 1

**Swapchain Validation:**
- Triple-buffering behavior correct (maximumDrawableCount=3)
- nextDrawable() blocking behavior under load
- presentDrawable() called before commit()
- Resize correctly updates drawableSize and back buffers
- No crashes on device rotation (iOS)

**Descriptor Binding Validation:**
- Dynamic offset updates apply correctly
- CPU-side descriptor table bindings match shader expectations
- Argument buffer limits respected (500K descriptors, 2K samplers)
- Shader reflection matches descriptor set layouts (Phase 4)
- All samplers have `supportArgumentBuffers = true`

**Shader Validation:**
- Entry point handling works for all shaders (including non-"main")
- HLSL → DXIL → Metal IR parity for identical rendering output
- Shader reflection reports expected bindings
- Push constants map to setVertexBytes/setFragmentBytes correctly

**Feature Capability Validation:**
- Metal 3 family support detection (supportsFamily)
- Counter sampling availability checked before query pool creation
- Texture sample count validation (supportsTextureSampleCount)
- BC texture compression detection (supportsBCTextureCompression)
- Memoryless storage only for render target textures

#### Platform Coverage
- Run examples on macOS (Apple Silicon + Intel if available).
- Add CI lane for Apple builds with Metal backend.

### 15. Known Semantic Mismatches (Must Be Documented)

1. **No Vulkan-style image layout transitions in Metal.**
   - Metal has no explicit layout concept
   - `ResourceState` transitions are metadata-only for validation

2. **No queue-family ownership transfer concept.**
   - `ReleaseToQueue`/`AcquireFromQueue` are ordering no-ops

3. **Descriptor pools are Vulkan-only; Metal descriptor allocation model differs.**
   - Metal has no descriptor pool concept
   - Descriptor sets are CPU-side binding tables

4. **Swapchain pre-transform is not equivalent; `GetPreTransform` should report identity.**
   - Metal has no transform concept
   - Always returns `SurfaceTransform::IDENTITY`

5. **Query capabilities vary by device/counter support.**
   - Feature checks required before query pool creation

6. **After-Fragment Barriers Not Supported**
   - Metal disables memory/texture barriers **after fragment stage** on Apple GPUs
   - Validation error in Metal debug layer
   - Barrier implementation must validate that no after-fragment barriers are generated

7. **Primitive Restart Behavior Difference**
   - **Vulkan:** Explicit `primitiveRestartEnable` flag
   - **Metal:** Implicit for strip topologies using special indices (0xFFFF/0xFFFFFFFF)
   - `primitiveRestartEnable` is metadata-only in Metal backend

8. **Managed Resource Synchronization (Discrete GPUs)**
   - Discrete Metal GPUs require explicit synchronization for managed resources
   - **CPU writes:** Call `didModifyRange()` after memcpy
   - **CPU reads:** Call `synchronizeResource()` before reading buffer

9. **Autorelease Pool Requirement**
   - Metal requires active autorelease pool on threads calling Metal APIs
   - Application or RHI must ensure per-thread or per-frame pools

10. **Polygon Point Mode Not Supported**
    - Metal does not support `PolygonMode::POINT` (render triangles as points)
    - Throw exception with clear error message if requested

11. **Metal Shader Converter 3.0 Texture Type Changes**
    - Pre-3.0: All textures forced to array types (2DArray, CubeArray)
    - v3.0+: Correct texture types (2D → MTLTextureType2D, Cube → MTLTextureTypeCube)
    - Use `--forceTextureArray` flag for backward compatibility if needed

12. **Sampler Argument Buffer Requirement**
    - All samplers must be created with `supportArgumentBuffers = true`
    - macOS 15/iOS 18+ debug layer asserts if missing

13. **Presentation Timing - nextDrawable() Blocking**
    - `nextDrawable()` **blocks** if max drawable count exceeded
    - Structure rendering loop to avoid blocking

14. **Presentation Order - presentDrawable() Must Precede commit()**
    - `presentDrawable()` only **schedules** presentation
    - Must be called **before** `commit()`, otherwise drawable not presented

### 16. Complete RHI API Coverage Matrix (`rhi/include/rhi/rhi.h`)

#### `IRHIDevice`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `CreateBuffer` | `MTL::Device::newBuffer`, map usage/hints to storage/cache modes. | 1 |
| `CreateTexture` | `MTL::Device::newTexture` with mapped usage/storage/type/format. | 1 |
| `CreateTextureView` | `MTL::Texture::newTextureView` with mip/layer ranges. | 1 |
| `CreateSampler` | `MTL::SamplerDescriptor` -> `newSamplerState` (with `supportArgumentBuffers = true`). | 1 |
| `CreateShader` | Load `.metallib` and create `MTL::Library` + `MTL::Function` with entry point. | 1 |
| `CreateGraphicsPipeline` | Build `MTL::RenderPipelineState` + `MTL::DepthStencilState`. | 1 |
| `CreateComputePipeline` | Build `MTL::ComputePipelineState`. | 2 |
| `CreateCommandList` | Allocate queue-typed command-list wrapper. | 1 |
| `CreateSwapchain` | Wrap `CAMetalLayer` + back-buffer wrappers (with GLFW adapter). | 1 |
| `CreateSemaphore` | Timeline primitive backed by `MTL::SharedEvent` + value. | 3 |
| `CreateFence` | CPU-waitable signal state (shared event or command completion). | 3 |
| `CreateCompositeFence` | Same fan-out wrapper behavior as existing backend. | 3 |
| `CreateDescriptorSetLayout` | Store descriptor metadata layout for bind-time translation. | 2 |
| `CreateDescriptorSet` | CPU-side binding table tied to layout (queue arg kept for parity). | 2 |
| `CreateQueryPool` | Counter sample buffers for timestamp/stats + visibility buffers for occlusion. | 3 |
| `UpdateBuffer` | CPU-visible memcpy path; managed-mode `didModifyRange` as needed. | 2 |
| `UploadBufferAsync(IRHIBuffer*)` | Stage upload + blit copy + fence return. | 2 |
| `UploadBufferAsync(BufferHandle)` | Forward to raw-pointer overload. | 2 |
| `SubmitCommandLists(simple)` | Build submit packet and forward to `SubmitInfo` path. | 2 |
| `SubmitCommandLists(SubmitInfo)` | Queue submit with `encodeWait`/`encodeSignalEvent` and optional fence signal. | 3 |
| `WaitQueueIdle` | Wait all pending command buffers for selected queue. | 2 |
| `WaitIdle` | Wait all queue types. | 2 |
| `RetireCompletedFrame` | Reclaim finished async staging/upload allocations. | 2 |
| `GetTimestampPeriod` | Device-calibrated conversion from counter ticks to ns. | 3 |
| `GetQueryPoolResults` | Resolve/copy query data from counter/visibility storage. | 3 |
| `GetMemoryStats` | Approximate using Metal memory telemetry fields. | 3 |
| `GetNativeInstance` (`#ifdef RHI_VULKAN`) | Not exposed in Metal build; remains Vulkan-only API. | N/A |
| `GetNativePhysicalDevice` (`#ifdef RHI_VULKAN`) | Not exposed in Metal build; remains Vulkan-only API. | N/A |
| `GetNativeDevice` (`#ifdef RHI_VULKAN`) | Not exposed in Metal build; remains Vulkan-only API. | N/A |
| `GetNativeGraphicsQueue` (`#ifdef RHI_VULKAN`) | Not exposed in Metal build; remains Vulkan-only API. | N/A |
| `GetGraphicsQueueFamily` (`#ifdef RHI_VULKAN`) | Not exposed in Metal build; remains Vulkan-only API. | N/A |

#### `IRHIBuffer`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `Map` | Return `MTL::Buffer::contents()` for CPU-visible buffers; error for private. | 1 |
| `Unmap` | No-op for shared memory; flush managed writes when required. | 1 |
| `GetSize` | Return cached descriptor size. | 1 |

#### `IRHITexture`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `GetWidth` | Return cached texture width. | 1 |
| `GetHeight` | Return cached texture height. | 1 |
| `GetDepth` | Return cached depth/array-depth semantics by texture type. | 1 |
| `GetMipLevels` | Return cached mip level count. | 1 |
| `GetArrayLayers` | Return cached array layer count. | 1 |
| `GetFormat` | Return RHI texture format used for creation. | 1 |

#### `IRHITextureView`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `GetTexture` | Return owning texture pointer wrapper. | 1 |
| `GetFormat` | Return view format override (or parent format). | 1 |
| `GetWidth` | Compute width at base mip from parent texture. | 1 |
| `GetHeight` | Compute height at base mip from parent texture. | 1 |
| `GetBaseMipLevel` | Return cached base mip. | 1 |
| `GetMipLevelCount` | Return cached mip count. | 1 |
| `GetBaseArrayLayer` | Return cached base layer. | 1 |
| `GetArrayLayerCount` | Return cached layer count. | 1 |

#### `IRHIShader`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `GetStage` | Return cached `ShaderStage`. | 1 |

#### `IRHICommandList`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `Begin` | Create/reset `MTL::CommandBuffer` and clear transient state. | 1 |
| `End` | Close active encoder(s), finalize command recording state. | 1 |
| `Reset` | Drop command-buffer/encoder refs and descriptor/pipeline state cache. | 1 |
| `BeginRendering` | Open render encoder from dynamic render info attachments. | 1 |
| `EndRendering` | End render encoder. | 1 |
| `SetPipeline` | Bind render or compute pipeline state object. | 1 |
| `SetVertexBuffer` | `setVertexBuffer` with offset at binding slot. | 1 |
| `BindIndexBuffer` | Cache index buffer + offset + type for indexed draws. | 1 |
| `BindDescriptorSet` | Translate descriptor entries to Metal buffer/texture/sampler binds. | 2 |
| `PushConstants` | `set*Bytes` fast path or ring-buffer constant upload path. | 2 |
| `SetViewport` | `setViewport`. | 1 |
| `SetScissor` | `setScissorRect`. | 1 |
| `Draw` | `drawPrimitives`. | 1 |
| `DrawIndexed` | `drawIndexedPrimitives`. | 1 |
| `DrawIndexedIndirect` | `drawIndexedPrimitives` indirect variant. | 2 |
| `DrawIndexedInstanced` | indexed draw with explicit instance count/offset. | 1 |
| `Dispatch` | `dispatchThreadgroups`. | 2 |
| `DispatchIndirect` | indirect dispatch from buffer. | 2 |
| `CopyBuffer` | blit encoder `copyFromBuffer`. | 2 |
| `FillBuffer` | blit encoder `fillBuffer`. | 2 |
| `CopyTexture` | blit encoder texture copy region(s). | 2 |
| `BlitTexture` | same-size copy via blit; scaled/filter path via fallback render/compute pass. | 2 |
| `Barrier` | encode memory/texture barriers when available; validate no after-fragment barriers. | 4 |
| `ResetQueryPool` | clear/reset query backing storage. | 3 |
| `WriteTimestamp` | sample counters at supported stages. | 3 |
| `BeginQuery` | begin occlusion/statistics sampling scope. | 3 |
| `EndQuery` | end occlusion/statistics sampling scope. | 3 |
| `CopyQueryPoolResults` | encode resolve/copy into destination buffer. | 3 |
| `ReleaseToQueue` | no ownership transfer in Metal; preserve ordering contract via sync hooks. | 4 |
| `AcquireFromQueue` | no ownership transfer in Metal; preserve ordering contract via sync hooks. | 4 |
| `GetNativeCommandBuffer` (`#ifdef RHI_VULKAN`) | Not exposed in Metal build; remains Vulkan-only API. | N/A |

#### `IRHISwapchain`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `AcquireNextImage` | `CAMetalLayer::nextDrawable`, wrap drawable texture, return index/status. | 1 |
| `Present` | present acquired drawable on submitted graphics command buffer (before commit). | 1 |
| `GetBackBuffer` | return per-image texture wrapper. | 1 |
| `GetBackBufferView` | return per-image texture-view wrapper. | 1 |
| `GetImageCount` | report configured drawable count (default 3). | 1 |
| `Resize` | update drawable size and rebuild transient back-buffer wrappers. | 1 |
| `GetPreTransform` | return `SurfaceTransform::IDENTITY`. | 1 |

#### `IRHISemaphore`
- No methods on interface; implementation is consumed by submit/acquire/present paths.

#### `IRHIFence`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `Wait` | wait for signaled value/completion with timeout behavior. | 3 |
| `Reset` | advance/reset internal signaled state. | 3 |
| `IsSignaled` | compare completed value against target signal value. | 3 |

#### `IRHIDescriptorSet`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `BindBuffer` | update CPU descriptor entry for buffer binding. | 2 |
| `BindTexture` | update CPU descriptor entry for texture/sampler binding. | 2 |

#### `IRHIQueryPool`
| API | Metal3 implementation plan | Phase |
|---|---|---|
| `GetQueryType` | return cached query type. | 3 |
| `GetQueryCount` | return cached query count. | 3 |

---

## Key Design Decisions

### 1. Shader Strategy
**Approach:** HLSL → DXIL → Metal Shader Converter → Metal IR (Apple-recommended approach)

**Rationale:**
- Recommended by Apple contacts as best practice
- Single HLSL source for all platforms (no hand-authored MSL needed)
- DirectX Shader Compiler (DXC) produces DXIL intermediate representation
- Metal Shader Converter translates DXIL to optimized Metal IR (.metallib)
- Supports all shader stages including ray tracing, mesh/amplification shaders
- Proven workflow for AAA-grade content on Metal

**Build Pipeline:**
```
HLSL Source
    ├─→ DXC → SPIR-V (.spv) → Vulkan Backend
    └─→ DXC → DXIL (.dxil) → Metal Shader Converter → .metallib → Metal Backend
```

**Phase 1 Implementation:**
- HLSL-only shader development (no MSL required)
- CLI-based conversion: `dxc → metal-shaderconverter`
- Entry point handling fixed in both backends

See Section 12 for complete details.

---

### 2. Descriptor Strategy
**Approach:** Phased approach - CPU-side binding (Phase 1) → Metal 3 argument buffers (Phase 2)

**Rationale:**
- Rapid Phase 1 delivery with CPU-side descriptor tables
- Optimize with Metal 3 declarative argument buffers in Phase 2
- No Metal 2 ArgumentEncoder needed (simplified Metal 3 syntax)

**Phase 1:** CPU-side binding tables translated at `BindDescriptorSet`
**Phase 2:** Metal 3 declarative argument buffers (500K descriptors, 2K samplers)

See Section 8 for implementation details.

---

### 3. Platform Integration Strategy
**Approach:** Hybrid approach - MetalLayer primary + GLFW fallback adapter

**Rationale:**
- MetalLayer primary path for native Metal apps (iOS, macOS)
- GLFW adapter as fallback for desktop cross-platform apps
- Maximizes compatibility while maintaining clean API

**Implementation:**
```cpp
// Primary path: Native CAMetalLayer
SwapchainDesc desc;
desc.handleType = WindowHandleType::MetalLayer;
desc.metalLayer = nativeLayer;

// Fallback path: GLFW window adapter
desc.handleType = WindowHandleType::GLFW;
desc.glfwWindow = window;
// RHI creates CAMetalLayer from NSWindow internally
```

**Build Requirements:**
- AppKit framework for GLFW path
- `glfwGetCocoaWindow()` for NSWindow extraction

See Section 9 for details.

---

## References

### Core Metal API
- `metal-cpp` (`metal-cpp_26` branch, commit: 9a8bc57c0318b84863bec2faf6605594877c5f5d): https://github.com/bkaradzic/metal-cpp/tree/metal-cpp_26
- `metal-cpp` README (integration, ownership, macros): https://github.com/bkaradzic/metal-cpp/blob/metal-cpp_26/README.md
- Apple: Get started with metal-cpp: https://developer.apple.com/metal/cpp/
- Apple Metal Best Practices (Resource Options / storage modes): https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/ResourceOptions.html
- Apple Metal Programming Guide (CAMetalLayer/drawable presentation): https://developer-mdn.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Render-Ctx/Render-Ctx.html
- Apple Metal Programming Guide (heaps/fences): https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/ResourceHeaps/ResourceHeaps.html
- GLFW native access (`glfwGetCocoaWindow`): https://www.glfw.org/docs/3.3/group__native.html

### Metal Shader Converter (CRITICAL - PRIMARY REFERENCE)
- **Official Documentation PDF:** `third-party/metal_shader_converter_3.0/Metal_Shader_Converter_3.0.pdf` (LOCAL - USE THIS)
- Download Page: https://developer.apple.com/downloads?q=Shader%20Converter
- Install Location: `/opt/metal-shaderconverter` (contains CLI tools, dynamic libraries, headers, samples, docs)
- WWDC 2023: "Bring your game to Mac, Part 2: Compile your shaders": https://developer.apple.com/videos/play/wwdc2023/10124/
- Blog: "A note on Metal shader converter" by Raph Levien: https://raphlinus.github.io/gpu/2023/06/12/shader-converter.html
- Medium: "Unlocking Cross-Platform Shader Development": https://medium.com/@batuhanbozyel/unlocking-cross-platform-shader-development-the-power-of-metal-shader-converter-and-hlsl-6741277758d2
- GitHub: MetalShaderConverterBinary: https://github.com/MethanePowered/MetalShaderConverterBinary
- GitHub: saxaboom (Rust wrapper): https://github.com/Traverse-Research/saxaboom

### DirectX Shader Compiler (DXC)
- GitHub Repository: https://github.com/microsoft/DirectXShaderCompiler
- DXC API Usage Guide: https://simoncoenen.com/blog/programming/graphics/DxcCompiling
- "Using DXC In Practice": https://posts.tanki.ninja/2019/07/11/Using-DXC-In-Practice/

### Metal 3 API
- WWDC 2022: "Discover Metal 3": https://developer.apple.com/videos/play/wwdc2022/10066/
- WWDC 2022: "Go bindless with Metal 3": https://developer.apple.com/videos/play/wwdc2022/10101/

### metal-cpp Specific
- WWDC 2022: "Program Metal in C++ with metal-cpp": https://developer.apple.com/videos/play/wwdc2022/10160/

### Synchronization
- "Synchronizing Events Between GPU and CPU": https://developer.apple.com/documentation/metal/resource_synchronization/synchronizing_events_between_a_gpu_and_the_cpu
- "Synchronizing CPU and GPU Work": https://developer.apple.com/documentation/metal/resource_synchronization/synchronizing_cpu_and_gpu_work

### Argument Buffers
- "Managing Groups of Resources with Argument Buffers": https://developer.apple.com/documentation/metal/buffers/managing_groups_of_resources_with_argument_buffers/

### Tile-Based Rendering
- "Tailor Apps for Apple GPUs and Tile-Based Deferred Rendering": https://developer.apple.com/documentation/metal/tailor-your-apps-for-apple-gpus-and-tile-based-deferred-rendering

---

## Implementation Quick Start Guide

### Prerequisites
- macOS 13 Ventura or later
- Xcode 15 or later
- DirectX Shader Compiler (DXC): https://github.com/microsoft/DirectXShaderCompiler
- Metal Shader Converter: Included with Xcode 15+

### Build Commands (Phase 0-1)

**1. Configure with Metal backend:**
```bash
python scripts/configure.py --backend metal3
```

**2. Build triangle example:**
```bash
python scripts/configure.py build --target triangle --run
```

**3. Compile shader (HLSL → DXIL → Metal IR):**
```bash
# Compile HLSL to DXIL
dxc -T vs_6_0 -E main shaders/triangle.vert.hlsl -Fo shaders/triangle.vert.dxil

# Convert DXIL to Metal IR
metal-shaderconverter shaders/triangle.vert.dxil -o shaders/triangle.vert.metallib
```

**4. Verify Metal backend loaded:**
```bash
# Should see "RHI Backend: Metal3" in output
./build/bin/Debug/triangle
```

### Validation Checklist

**Phase 0 Validation:**
- [x] Project builds with `RHI_BACKEND=METAL3`
- [x] metal-cpp headers found and compiled
- [x] All frameworks linked correctly (Metal, QuartzCore, Foundation, AppKit)
- [x] No build errors or warnings (existing duplicate-library linker warning observed in triangle link step)

**Phase 1 Validation (Implementation Complete - Awaiting Runtime Testing):**
- [x] Triangle HLSL shader converts to .metallib (all 6 compiled outputs present)
- [x] ShaderFactory loads .metallib successfully (implementation verified)
- [x] GLFW window creates CAMetalLayer (adapter implemented)
- [x] Triangle renders on screen
- [x] Entry point "main" works correctly
- [x] Entry point "vs_main"/"fs_main" works correctly
- [x] All samplers created with `supportArgumentBuffers = true` (verified in code)
- [x] Autorelease pool exists on rendering thread (implemented in swapchain/commandlist)
- [x] No Metal validation errors in Xcode debugger

**Note:** Implementation complete. Runtime validation recommended before Phase 2.
