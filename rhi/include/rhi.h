#pragma once
#include "rhi_types.h"

namespace RHI {

// Main device interface
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // Resource creation
    virtual std::unique_ptr<IRHIBuffer> CreateBuffer(const BufferDesc& desc) = 0;
    virtual std::unique_ptr<IRHIShader> CreateShader(const ShaderDesc& desc) = 0;
    virtual std::unique_ptr<IRHIPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual std::unique_ptr<IRHICommandList> CreateCommandList() = 0;
    virtual std::unique_ptr<IRHISwapchain> CreateSwapchain(const SwapchainDesc& desc) = 0;
    virtual std::unique_ptr<IRHISemaphore> CreateSemaphore() = 0;
    virtual std::unique_ptr<IRHIFence> CreateFence(bool signaled = false) = 0;

    // Queue operations
    virtual void SubmitCommandLists(
        IRHICommandList** cmdLists,
        uint32_t count,
        IRHISemaphore* waitSemaphore = nullptr,
        IRHISemaphore* signalSemaphore = nullptr,
        IRHIFence* signalFence = nullptr
    ) = 0;

    virtual void WaitIdle() = 0;
};

// Buffer interface
class IRHIBuffer {
public:
    virtual ~IRHIBuffer() = default;
    virtual void* Map() = 0;
    virtual void Unmap() = 0;
    virtual size_t GetSize() const = 0;
};

// Texture interface
class IRHITexture {
public:
    virtual ~IRHITexture() = default;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual TextureFormat GetFormat() const = 0;
};

// Shader interface
class IRHIShader {
public:
    virtual ~IRHIShader() = default;
    virtual ShaderStage GetStage() const = 0;
};

// Pipeline interface
class IRHIPipeline {
public:
    virtual ~IRHIPipeline() = default;
};

// Command list interface
class IRHICommandList {
public:
    virtual ~IRHICommandList() = default;

    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual void Reset() = 0;

    virtual void BeginRenderPass(const RenderPassBeginInfo& info) = 0;
    virtual void EndRenderPass() = 0;

    virtual void SetPipeline(IRHIPipeline* pipeline) = 0;
    virtual void SetVertexBuffer(uint32_t binding, IRHIBuffer* buffer, size_t offset = 0) = 0;
    virtual void SetViewport(float x, float y, float width, float height) = 0;
    virtual void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;

    virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
};

// Swapchain interface
class IRHISwapchain {
public:
    virtual ~IRHISwapchain() = default;

    virtual uint32_t AcquireNextImage(IRHISemaphore* signalSemaphore = nullptr) = 0;
    virtual void Present(uint32_t imageIndex, IRHISemaphore* waitSemaphore = nullptr) = 0;
    virtual IRHITexture* GetBackBuffer(uint32_t index) = 0;
    virtual uint32_t GetImageCount() const = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
};

// Synchronization primitives
class IRHISemaphore {
public:
    virtual ~IRHISemaphore() = default;
};

class IRHIFence {
public:
    virtual ~IRHIFence() = default;
    virtual void Wait(uint64_t timeout = UINT64_MAX) = 0;
    virtual void Reset() = 0;
    virtual bool IsSignaled() const = 0;
};

// Device creation function
std::unique_ptr<IRHIDevice> CreateRHIDevice();

} // namespace RHI