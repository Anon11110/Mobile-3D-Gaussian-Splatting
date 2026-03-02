# Rendering Hardware Interface (RHI) Design Documentation

## Overview
The RHI is a thin, Vulkan-first abstraction used by the examples and engine code in this repository. It mirrors Vulkan concepts (command lists, explicit barriers, descriptor sets) and currently ships with a Vulkan backend. A Metal3 backend is in development for native Apple platform support. Surfaces are created from platform window handles (`GLFWwindow*` on desktop, `ANativeWindow*` on Android, `CAMetalLayer*` on Apple), memory is allocated via Vulkan Memory Allocator (VMA), and rendering relies on Vulkan 1.3 dynamic rendering (or `VK_KHR_dynamic_rendering` when 1.3 is unavailable).

## Architecture

### Design Principles
- Vulkan-centric and explicit: queue selection, state transitions, descriptor bindings, and memory hints are caller driven.
- Lean abstractions: interfaces are close to Vulkan primitives; pipelines target a `RenderTargetSignature` instead of render passes, and viewport/scissor are dynamic.
- Intrusive handles: every interface derives from `IRefCounted` and is wrapped in `RefCntPtr`. The backend only tracks internal async-upload staging cleanup (`RetireCompletedFrame`); callers still own resource lifetime guarantees for submitted GPU work.
- Queue awareness: command lists are created for a specific queue type (graphics/compute/transfer) and expose queue ownership transfer helpers.
- Modern feature set: dynamic rendering and descriptor set layouts/push constants are first-class; there are no legacy fixed-function paths.

### Layer Structure
```
Application
  | (include/rhi/rhi.h, rhi_types.h)
  |
  ├── Vulkan Backend (rhi/src/backends/vulkan)
  |     Vulkan 1.3 / VK_KHR_dynamic_rendering + synchronization2 + VMA
  |
  └── Metal3 Backend (rhi/src/backends/metal3) [in development]
        Metal 3 via metal-cpp bindings (Apple platforms only)
```

Backend selection is a CMake cache variable:
```cmake
set(RHI_BACKEND "VULKAN" CACHE STRING "RHI backend (VULKAN or METAL3)")
```

## API Surface

### Device (`IRHIDevice`)
- Resource factories: buffers, textures, texture views, samplers, shaders, graphics/compute pipelines, descriptor set layouts, descriptor sets (per-queue pool), command lists (per-queue), swapchains, semaphores, fences, composite fences, and query pools.
- Buffer data helpers: `UpdateBuffer` maps host-visible buffers; device-local buffers must use `UploadBufferAsync`, which stages through the transfer queue and returns a fence. Call `RetireCompletedFrame` periodically to reclaim completed staging buffers.
- Submission: `SubmitCommandLists` has a simple overload and a `SubmitInfo` variant that wires wait semaphores with per-semaphore stage masks (`SemaphoreWaitInfo`), signal semaphores, and an optional signal fence.
- Synchronization helpers: `WaitQueueIdle`/`WaitIdle` block until work completes.
- Query and profiling helpers: `GetTimestampPeriod`, `GetQueryPoolResults`, plus command-list query recording methods.
- Memory telemetry: `GetMemoryStats` returns VMA budget/usage aggregates for device-local, host-visible, and total heaps.
- Native Vulkan accessors are exposed under `RHI_VULKAN` for integration (instance, physical device, device, graphics queue, queue family index).

### Resources

#### Buffers (`IRHIBuffer`)
- Usage flags: `VERTEX`, `INDEX`, `UNIFORM`, `STORAGE`, `TRANSFER_DST`, `TRANSFER_SRC`, `INDIRECT`.
- `ResourceUsage` controls allocation strategy: `Static` (device local, staged if initial data is provided), `DynamicUpload` (host visible, sequential write), `Readback` (host visible, random read), `Transient` (device local, aliasing allowed). `AllocationHints` allow toggling device-local preference, persistent mapping, and dedicated allocations.
- Interface: `Map`/`Unmap`, `GetSize`. Index buffers remember the `IndexType` and the backend uses it when binding.

#### Textures (`IRHITexture`)
- Texture allocation supports `TEXTURE_2D`, `TEXTURE_2D_ARRAY`, `TEXTURE_3D`, and cube/cube-array modes (`TEXTURE_CUBE` or `isCubeMap`), with matching default Vulkan image/view types.
- Boolean flags control usage bits: `isRenderTarget`, `isDepthStencil`, `isStorageImage`, `isInputAttachment`. `ResourceUsage` maps to VMA similarly to buffers. Initial data uploads are only implemented for `Static`; providing initial data for other usages throws.

#### Texture Views (`IRHITextureView`)
- Supports format overrides, aspect masks (`COLOR`, `DEPTH`, `STENCIL`), and mip/layer subranges. Custom views created via `CreateTextureView` are currently always `VK_IMAGE_VIEW_TYPE_2D`; width/height are derived from the base mip.

#### Shaders and Samplers
- `ShaderDesc` expects SPIR-V bytecode and stage. The `entryPoint` field defaults to `"main"` and is forwarded to pipeline stage creation. `SamplerDesc` mirrors Vulkan sampler state options (filter modes, address modes, anisotropy, LOD clamping, border color).

### Pipelines

#### Graphics Pipelines
- Described by `GraphicsPipelineDesc`: vertex/fragment shaders, vertex layout, topology (with optional primitive restart), rasterization/depth-stencil/multisample/color blend state, `RenderTargetSignature` (color formats list, optional depth format, sample count), descriptor set layouts, push constant ranges, and per-stage specialization constants (`SpecializationInfo`).
- Uses dynamic rendering (`vkCmdBeginRendering`/`vkCmdEndRendering`); viewport and scissor are dynamic. Tessellation/geometry stages are not implemented.

#### Compute Pipelines
- Single compute shader plus descriptor set layouts, push constant ranges, and optional specialization constants.

#### Specialization Constants
- `SpecializationInfo` holds a list of `SpecializationConstant` entries (constant ID, offset, size) and a byte buffer of data. Helper `MakeSpecConstantU32(constantID, value)` creates a single `uint32_t` specialization. `GraphicsPipelineDesc` supports per-stage specialization (vertex and fragment), `ComputePipelineDesc` supports a single specialization.

### Command Lists (`IRHICommandList`)
- Lifecycle: `Begin` -> record -> `End`; `Reset` reuses the underlying command buffer. Recording must bracket dynamic rendering with `BeginRendering`/`EndRendering`. If render area is left zero, the backend infers it from the first attachment.
- Binding and draw/dispatch: `SetPipeline`, `SetVertexBuffer`, `BindIndexBuffer` (uses buffer `IndexType`), `BindDescriptorSet` (pipeline must be bound; supports dynamic offsets), `PushConstants`, `SetViewport`/`SetScissor`, `Draw`, `DrawIndexed`, `DrawIndexedInstanced`, `DrawIndexedIndirect`, `Dispatch`, `DispatchIndirect`.
- Buffer/image utility ops: `CopyBuffer`, `FillBuffer`, `CopyTexture`, `BlitTexture` (linear/nearest). Caller is responsible for correct resource states.
- Barriers: `Barrier` accepts buffer/texture transitions, optional memory barriers, and `DependencyFlags` (e.g. `BY_REGION` for tile-based GPUs). Stage/access masks default from `ResourceState` and `PipelineScope` when set to `Auto`; if every span is empty, no barrier is emitted.
- Queue ownership: `ReleaseToQueue` and `AcquireFromQueue` emit queue-family ownership transfers based on the command list's queue and the target/source queue types.
- Query ops are exposed on command lists: `ResetQueryPool`, `WriteTimestamp`, `BeginQuery`, `EndQuery`, and `CopyQueryPoolResults`.
- `GetNativeCommandBuffer` is available under `RHI_VULKAN`.
- The backend does not retain resource references; applications must keep resources alive until work completes.

### Synchronization and Submission
- `ResourceState` enumerates common usages (including resolve src/dst, render target, depth read/write, shader read/write, copy src/dst, present, indirect, vertex/index/uniform). `RenderingLocalRead` is a Vulkan-specific state for reading a color attachment as an input attachment within the same dynamic rendering pass. Optional `StageMask`/`AccessMask` can override automatic inference.
- `SemaphoreHandle`/`FenceHandle` represent GPU semaphores/fences. `CreateCompositeFence` wraps multiple fences and forwards `Wait`/`Reset`/`IsSignaled` to all of them.
- `SubmitInfo` wires wait semaphores (via `SemaphoreWaitInfo` which pairs a semaphore with its wait stage mask), signal semaphores, and an optional fence; queues are selected explicitly per submission.

### Descriptor Sets and Layouts
- `DescriptorType` covers uniform/storage buffers (static and dynamic), uniform/storage texel buffers, sampled/storage textures, samplers, combined image samplers, and input attachments.
- `DescriptorSetLayoutDesc` is a list of `DescriptorBinding` entries (binding index, type, array count, stage flags). The Vulkan backend caches pool sizes from the layout.
- `CreateDescriptorSet` takes the layout and an optional queue type to pick the correct descriptor pool. `IRHIDescriptorSet::BindBuffer`/`BindTexture` immediately call `vkUpdateDescriptorSets` for a single binding update. Texture binding currently uses the texture's default image view (no descriptor-time `IRHITextureView` override).

### Dynamic Rendering Local Read
- `RenderingInfo` supports local read within a dynamic rendering pass via `enableLocalRead`. When enabled, `colorAttachmentLocations` maps fragment output location *i* and `input_attachment_index` *i* to the corresponding color attachment index. `depthInputAttachmentIndex` and `stencilInputAttachmentIndex` allow reading depth/stencil as input attachments. Color attachments used for local read should set their layout to `ImageLayout::RENDERING_LOCAL_READ`.

### Depth/Stencil Attachments
- `DepthStencilAttachment` supports separate load/store ops for depth and stencil (`depthLoadOp`/`depthStoreOp`, `stencilLoadOp`/`stencilStoreOp`), a `readOnly` flag, and a clear value.

### Presentation (`IRHISwapchain`)
- Created from a `SwapchainDesc` with a native `windowHandle`, `windowHandleType` (GLFW/AndroidNative/MetalLayer), width/height, preferred format (defaults to BGRA8 UNORM), buffer count, vsync toggle, and `disablePreRotation`.
- Chooses the requested format when available; otherwise falls back to a compatible BGRA8 format. Present mode is FIFO unless vsync is false (tries MAILBOX then IMMEDIATE).
- `AcquireNextImage`/`Present` return `SwapchainStatus` (success/out-of-date/suboptimal/error). `Resize` recreates the swapchain and image views. `GetBackBuffer`/`GetBackBufferView` expose textures for rendering; `GetImageCount` reflects the current swapchain size.
- `GetPreTransform` exposes the surface transform applied by the swapchain (identity or rotation/mirror), which is useful for pre-rotation aware rendering on Android.

### Resource Lifetime
- All interfaces are intrusive reference-counted via `RefCntPtr`. `RetireCompletedFrame` polls fences from `UploadBufferAsync` calls and reclaims staging buffers/command buffers whose transfers have completed. Call it periodically (e.g., once per frame) to avoid accumulating staging memory. Command lists do not hold internal references; keep resources alive until the GPU has finished using them (e.g., wait on fences from submissions or call `WaitQueueIdle`/`WaitIdle`).

## Vulkan Backend Notes
- Initialization: GLFW is initialized on desktop for surface creation; Android uses native surfaces directly. The backend prefers discrete GPUs when selecting a physical device. Validation layers are enabled when available in debug builds.
- Queue families: attempts to pick dedicated compute/transfer queues; falls back to the graphics family when needed. Separate command pools and descriptor pools exist per queue family.
- Dynamic rendering: enabled via Vulkan 1.3 core or `VK_KHR_dynamic_rendering`. Synchronization2 functions are also loaded. Rendering command recording relies on these dynamic-rendering entry points.
- Memory allocation: VMA is used for buffers/textures. `ResourceUsage` drives the VMA usage/flags (device-local for `Static`/`Transient`, host-visible with sequential or random access for `DynamicUpload`/`Readback`; transient render targets request lazy memory when available). Texture initial data uploads are implemented only for `Static` resources.
- Buffer uploads: `UpdateBuffer` enforces host-visible allocations; `UploadBufferAsync` stages through the transfer queue and signals a fence. `RetireCompletedFrame` polls these fences and reclaims staging buffers/command buffers once their transfers complete.
- Query support: timestamp, occlusion, and pipeline-statistics query pools are exposed; pipeline statistics are optional and may be unavailable (for example on MoltenVK).
- Swapchain: picks a format/present mode as described above, handles optional pre-rotation disable, and creates `Texture`/`TextureView` wrappers for back buffers.

## Usage Notes

### Basic Rendering Loop
```cpp
DeviceHandle device = CreateRHIDevice();
SwapchainHandle swapchain = device->CreateSwapchain(swapchainDesc);

BufferDesc vbDesc{...};
BufferHandle vertexBuffer = device->CreateBuffer(vbDesc);

GraphicsPipelineDesc pipelineDesc{...};
PipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);

CommandListHandle cmd = device->CreateCommandList(QueueType::GRAPHICS);

uint32_t imageIndex = 0;
swapchain->AcquireNextImage(imageIndex);

cmd->Begin();
RenderingInfo rendering{};
rendering.colorAttachments.push_back({
    .view = swapchain->GetBackBufferView(imageIndex),
    .loadOp = LoadOp::CLEAR,
    .storeOp = StoreOp::STORE,
    .clearValue = {{0.f, 0.f, 0.f, 1.f}}
});
cmd->BeginRendering(rendering);
cmd->SetPipeline(pipeline.Get());
cmd->SetViewport(0, 0, swapchainWidth, swapchainHeight);
cmd->SetScissor(0, 0, swapchainWidth, swapchainHeight);
cmd->SetVertexBuffer(0, vertexBuffer.Get());
cmd->Draw(vertexCount);
cmd->EndRendering();
cmd->End();

IRHICommandList *lists[] = {cmd.Get()};
device->SubmitCommandLists(lists, QueueType::GRAPHICS);
swapchain->Present(imageIndex);

device->RetireCompletedFrame();
```

### Cross-Queue Transfer Example
```cpp
BufferTransition toCompute{
    .buffer = sharedBuffer.Get(),
    .before = ResourceState::GeneralRead,
    .after = ResourceState::ShaderReadWrite
};

// Graphics queue releases ownership
graphicsCmd->ReleaseToQueue(QueueType::COMPUTE, {&toCompute, 1}, {});

// Compute queue acquires and uses the buffer
computeCmd->AcquireFromQueue(QueueType::GRAPHICS, {&toCompute, 1}, {});
computeCmd->SetPipeline(computePipeline.Get());
computeCmd->Dispatch(groupsX, groupsY, groupsZ);
```

### Synchronization Reminder
Use `Barrier` to transition resources into the correct `ResourceState` before use and to order writes/reads. Provide explicit stage/access masks when needed; otherwise, the backend infers them from the requested state and pipeline scope. If you pass empty transition spans and no memory barriers, no synchronization command is emitted.

### Known Gaps
- Command-list-side handle retention is not implemented; callers must keep resources alive until GPU work completes.
- `CreateTextureView` is currently hardcoded to a 2D view type in the Vulkan backend, so non-2D custom view creation paths are not exposed yet.
- The Metal3 backend (`rhi/src/backends/metal3/`) is in development and not yet functional.
