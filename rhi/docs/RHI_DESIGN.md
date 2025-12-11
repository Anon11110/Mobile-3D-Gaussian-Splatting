# Rendering Hardware Interface (RHI) Design Documentation

## Overview
The RHI is a thin, Vulkan-first abstraction used by the examples and engine code in this repository. It mirrors Vulkan concepts (command lists, explicit barriers, descriptor sets) and currently ships only with a Vulkan backend. Surfaces are created through GLFW, memory is allocated via Vulkan Memory Allocator (VMA), and rendering relies on Vulkan 1.3 dynamic rendering (or `VK_KHR_dynamic_rendering` when 1.3 is unavailable). There is no Metal backend today.

## Architecture

### Design Principles
- Vulkan-centric and explicit: queue selection, state transitions, descriptor bindings, and memory hints are caller driven.
- Lean abstractions: interfaces are close to Vulkan primitives; pipelines target a `RenderTargetSignature` instead of render passes, and viewport/scissor are dynamic.
- Intrusive handles: every interface derives from `IRefCounted` and is wrapped in `RefCntPtr`. GPU-completion tracking is not implemented yet; callers own lifetime guarantees.
- Queue awareness: command lists are created for a specific queue type (graphics/compute/transfer) and expose queue ownership transfer helpers.
- Modern feature set: dynamic rendering and descriptor set layouts/push constants are first-class; there are no legacy fixed-function paths.

### Layer Structure
```
Application
  | (include/rhi/rhi.h, rhi_types.h)
Vulkan Backend (rhi/src/backends/vulkan)
  | Vulkan 1.3 / VK_KHR_dynamic_rendering + VMA + GLFW surface creation
```
No other backend is present.

## API Surface

### Device (`IRHIDevice`)
- Resource factories: buffers, textures, texture views, samplers, shaders, graphics/compute pipelines, descriptor set layouts, descriptor sets (per-queue pool), command lists (per-queue), swapchains, semaphores, fences, composite fences.
- Buffer data helpers: `UpdateBuffer` maps host-visible buffers; device-local buffers must use `UploadBufferAsync`, which stages through the transfer queue and returns a fence (staging resources are freed only when the device is destroyed; `RetireCompletedFrame` is a stub).
- Submission: `SubmitCommandLists` has a simple overload and a `SubmitInfo` variant that wires wait semaphores with stage masks, signal semaphores, and an optional signal fence.
- Synchronization helpers: `WaitQueueIdle`/`WaitIdle` block until work completes.
- Native Vulkan accessors are exposed under `RHI_VULKAN` for integration (instance, physical device, device, graphics queue, queue family index).

### Resources

#### Buffers (`IRHIBuffer`)
- Usage flags: `VERTEX`, `INDEX`, `UNIFORM`, `STORAGE`, `TRANSFER_DST`, `TRANSFER_SRC`.
- `ResourceUsage` controls allocation strategy: `Static` (device local, staged if initial data is provided), `DynamicUpload` (host visible, sequential write), `Readback` (host visible, random read), `Transient` (device local, aliasing allowed). `AllocationHints` allow toggling device-local preference, persistent mapping, and dedicated allocations.
- Interface: `Map`/`Unmap`, `GetSize`. Index buffers remember the `IndexType` and the Vulkan backend uses it when binding.

#### Textures (`IRHITexture`)
- Implemented as 2D images; the backend creates `VK_IMAGE_TYPE_2D` images and `VK_IMAGE_VIEW_TYPE_2D` views. Array/cubemap/3D images are not supported yet.
- `isRenderTarget`/`isDepthStencil` toggle attachment usage bits. `ResourceUsage` maps to VMA similarly to buffers. Initial data uploads are only implemented for `Static`; providing initial data for other usages throws.

#### Texture Views (`IRHITextureView`)
- Supports format overrides, aspect masks, and mip/layer subranges, but views are always 2D. Width/height are derived from the base mip. Useful for depth-only or color-aspect selections.

#### Shaders and Samplers
- `ShaderDesc` expects SPIR-V bytecode plus an entry point (default `main`). `SamplerDesc` mirrors Vulkan sampler state options.

### Pipelines

#### Graphics Pipelines
- Described by `GraphicsPipelineDesc`: vertex/fragment shaders, vertex layout, topology (with optional primitive restart), rasterization/depth-stencil/multisample/color blend state, `RenderTargetSignature` (color formats list, optional depth format, sample count), descriptor set layouts, and push constant ranges.
- Uses Vulkan dynamic rendering (`vkCmdBeginRendering`/`vkCmdEndRendering`); viewport and scissor are dynamic. Tessellation/geometry stages are not implemented.

#### Compute Pipelines
- Single compute shader plus descriptor set layouts and push constant ranges.

### Command Lists (`IRHICommandList`)
- Lifecycle: `Begin` -> record -> `End`; `Reset` reuses the underlying command buffer. Recording must bracket dynamic rendering with `BeginRendering`/`EndRendering`. If render area is left zero, the backend infers it from the first attachment.
- Binding and draw/dispatch: `SetPipeline`, `SetVertexBuffer`, `BindIndexBuffer` (uses buffer `IndexType`), `BindDescriptorSet` (pipeline must be bound; supports dynamic offsets), `PushConstants`, `SetViewport`/`SetScissor`, `Draw`, `DrawIndexed`, `DrawIndexedInstanced`, `DrawIndexedIndirect`, `Dispatch`, `DispatchIndirect`.
- Copies and blits: `CopyBuffer`, `CopyTexture`, `BlitTexture` (linear/nearest). Caller is responsible for correct resource layouts.
- Barriers: `Barrier` accepts buffer/texture transitions plus optional memory barriers. Stage/access masks default from `ResourceState` and `PipelineScope` when set to `Auto`; if every span is empty, no barrier is emitted.
- Queue ownership: `ReleaseToQueue` and `AcquireFromQueue` emit queue-family ownership transfers based on the command list's queue and the target/source queue types.
- The backend does not retain resource references; applications must keep resources alive until work completes.

### Synchronization and Submission
- `ResourceState` enumerates common usages (render target, depth read/write, shader read/write, copy src/dst, present, indirect, vertex/index/uniform). Optional `StageMask`/`AccessMask` can override automatic inference.
- `SemaphoreHandle`/`FenceHandle` represent Vulkan semaphores/fences. `CreateCompositeFence` wraps multiple fences and forwards `Wait`/`Reset`/`IsSignaled` to all of them.
- `SubmitInfo` wires wait semaphores with per-semaphore stage masks, signal semaphores, and an optional fence; queues are selected explicitly per submission.

### Descriptor Sets and Layouts
- `DescriptorType` covers uniform/storage buffers (static and dynamic), uniform/storage texel buffers, sampled/storage textures, and samplers.
- `DescriptorSetLayoutDesc` is a list of `DescriptorBinding` entries (binding index, type, array count, stage flags). The Vulkan backend caches pool sizes from the layout.
- `CreateDescriptorSet` requires the queue type to pick the correct descriptor pool. `IRHIDescriptorSet::BindBuffer`/`BindTexture` immediately call `vkUpdateDescriptorSets` for a single binding update.

### Presentation (`IRHISwapchain`)
- Created from a `SwapchainDesc` with a GLFW window handle, width/height, preferred format (defaults to BGRA8 UNORM), buffer count, and vsync toggle.
- Chooses the requested format when available; otherwise falls back to a compatible BGRA8 format. Present mode is FIFO unless vsync is false (tries MAILBOX then IMMEDIATE).
- `AcquireNextImage`/`Present` return `SwapchainStatus` (success/out-of-date/suboptimal/error). `Resize` recreates the swapchain and image views. `GetBackBuffer`/`GetBackBufferView` expose textures for rendering; `GetImageCount` reflects the current swapchain size.

### Resource Lifetime
- All interfaces are intrusive reference-counted via `RefCntPtr`, but GPU-safe retirement is not implemented. `RetireCompletedFrame` is currently a no-op, and command lists do not hold internal references. Keep resources alive until the GPU has finished using them (e.g., wait on fences from submissions or call `WaitQueueIdle`/`WaitIdle`). `UploadBufferAsync` staging buffers are only reclaimed when the device is destroyed.

## Vulkan Backend Notes
- Initialization: GLFW is initialized for surface creation; the first available physical device is selected. Validation layers are enabled when available in debug builds.
- Queue families: attempts to pick dedicated compute/transfer queues; falls back to the graphics family when needed. Separate command pools and descriptor pools exist per queue family.
- Dynamic rendering: enabled via Vulkan 1.3 core or `VK_KHR_dynamic_rendering`. Function pointers are cached during device creation and are required for command recording.
- Memory allocation: VMA is used for buffers/textures. `ResourceUsage` drives the VMA usage/flags (device-local for `Static`/`Transient`, host-visible with sequential or random access for `DynamicUpload`/`Readback`; transient render targets request lazy memory when available). Texture initial data uploads are implemented only for `Static` resources.
- Buffer uploads: `UpdateBuffer` enforces host-visible allocations; `UploadBufferAsync` stages through the transfer queue and signals a fence. Staging buffers/command buffers are retained in an internal list and freed when the device is destroyed.
- Swapchain: picks a format/present mode as described above and creates `Texture`/`TextureView` wrappers for back buffers. `GetFramebuffer` can build a framebuffer for a provided render pass when needed.

## Usage Notes

### Basic Rendering Loop (Vulkan backend)
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

// Ensure GPU completion before releasing resources until RetireCompletedFrame is implemented
device->WaitQueueIdle(QueueType::GRAPHICS);
```

### Cross-Queue Transfer Example
```cpp
TextureTransition toCompute{
    .texture = sharedTexture.Get(),
    .before = ResourceState::GeneralRead,
    .after = ResourceState::ShaderReadWrite
};

// Graphics queue releases ownership
graphicsCmd->ReleaseToQueue(QueueType::COMPUTE, {}, {toCompute});

// Compute queue acquires and uses the texture
computeCmd->AcquireFromQueue(QueueType::GRAPHICS, {}, {toCompute});
computeCmd->SetPipeline(computePipeline.Get());
computeCmd->Dispatch(groupsX, groupsY, groupsZ);
```

### Synchronization Reminder
Use `Barrier` to transition resources into the correct `ResourceState` before use and to order writes/reads. Provide explicit stage/access masks when needed; otherwise, the backend infers them from the requested state and pipeline scope. If you pass empty transition spans and no memory barriers, no synchronization command is emitted.

### Known Gaps
- GPU-safe resource retirement (`RetireCompletedFrame`, command-list-side handle retention) is not implemented.
- `UploadBufferAsync` staging resources are reclaimed only at device destruction.
- Only 2D textures and 2D views are supported; array/3D/cube paths are not implemented.
- Metal or other backends do not exist yet.
