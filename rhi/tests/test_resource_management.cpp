#include "test_framework.h"
#include "../include/common/ref_count.h"
#include "../include/rhi/rhi.h"
#include "../include/rhi/rhi_types.h"
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <chrono>

namespace rhi {
namespace test {

// Global counters for tracking object lifecycle
static std::atomic<int> g_totalResourcesCreated{0};
static std::atomic<int> g_totalResourcesDestroyed{0};

// Base mock class for tracking resource lifecycle
class MockResource {
public:
    MockResource() {
        g_totalResourcesCreated++;
    }
    virtual ~MockResource() {
        g_totalResourcesDestroyed++;
    }
};

// Mock Device implementation
class MockDevice : public RefCounter<IRHIDevice>, public MockResource {
private:
    std::vector<RefCntPtr<IRefCounted>> m_pendingDeletion;
    bool m_supportsFrameRetirement;
    
public:
    explicit MockDevice(bool supportsFrameRetirement = true) 
        : m_supportsFrameRetirement(supportsFrameRetirement) {}
    
    // IRHIDevice interface implementation
    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    TextureHandle CreateTexture(const TextureDesc& desc) override;
    TextureViewHandle CreateTextureView(const TextureViewDesc& desc) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;
    CommandListHandle CreateCommandList(QueueType queueType = QueueType::GRAPHICS) override;
    SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) override;
    SemaphoreHandle CreateSemaphore() override;
    FenceHandle CreateFence(bool signaled = false) override;
    FenceHandle CreateCompositeFence(const std::vector<FenceHandle>& fences) override;
    DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) override;
    DescriptorSetHandle CreateDescriptorSet(IRHIDescriptorSetLayout* layout, QueueType queueType = QueueType::GRAPHICS) override;
    
    void UpdateBuffer(IRHIBuffer* buffer, const void* data, size_t size, size_t offset = 0) override {}
    
    FenceHandle UploadBufferAsync(IRHIBuffer* dstBuffer, const void* data, size_t size, size_t offset = 0) override {
        return CreateFence(true);
    }
    
    FenceHandle UploadBufferAsync(const BufferHandle& dstBuffer, const void* data, size_t size, size_t offset = 0) override {
        return CreateFence(true);
    }
    
    void SubmitCommandLists(std::span<IRHICommandList* const> cmdLists,
                           QueueType queueType = QueueType::GRAPHICS,
                           IRHISemaphore* waitSemaphore = nullptr,
                           IRHISemaphore* signalSemaphore = nullptr,
                           IRHIFence* signalFence = nullptr) override {}
    
    void SubmitCommandLists(std::span<IRHICommandList* const> cmdLists,
                           QueueType queueType,
                           const SubmitInfo& submitInfo) override {}
    
    void WaitQueueIdle(QueueType queueType) override {}
    void WaitIdle() override {}
    
    void RetireCompletedFrame() override {
        // Simulate frame retirement by clearing pending deletions
        m_pendingDeletion.clear();
    }
    
    // Helper for testing
    void AddPendingDeletion(RefCntPtr<IRefCounted> resource) {
        if (m_supportsFrameRetirement) {
            m_pendingDeletion.push_back(resource);
        }
    }
    
    size_t GetPendingDeletionCount() const {
        return m_pendingDeletion.size();
    }
};

// Mock Buffer implementation
class MockBuffer : public RefCounter<IRHIBuffer>, public MockResource {
private:
    size_t m_size;
    void* m_mappedData;
    
public:
    static std::atomic<int> s_bufferCount;
    
public:
    explicit MockBuffer(size_t size) : m_size(size), m_mappedData(nullptr) {
        s_bufferCount++;
    }
    
    ~MockBuffer() override {
        s_bufferCount--;
        if (m_mappedData) {
            delete[] static_cast<uint8_t*>(m_mappedData);
        }
    }
    
    void* Map() override {
        if (!m_mappedData) {
            m_mappedData = new uint8_t[m_size];
        }
        return m_mappedData;
    }
    
    void Unmap() override {
        // Keep mapped data for testing
    }
    
    size_t GetSize() const override {
        return m_size;
    }
    
    static int GetBufferCount() {
        return s_bufferCount.load();
    }
};

std::atomic<int> MockBuffer::s_bufferCount{0};

// Mock Texture implementation
class MockTexture : public RefCounter<IRHITexture>, public MockResource {
private:
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_depth;
    uint32_t m_mipLevels;
    uint32_t m_arrayLayers;
    TextureFormat m_format;
    
public:
    static std::atomic<int> s_textureCount;
    
public:
    MockTexture(uint32_t width, uint32_t height, TextureFormat format)
        : m_width(width), m_height(height), m_depth(1),
          m_mipLevels(1), m_arrayLayers(1), m_format(format) {
        s_textureCount++;
    }
    
    ~MockTexture() override {
        s_textureCount--;
    }
    
    uint32_t GetWidth() const override { return m_width; }
    uint32_t GetHeight() const override { return m_height; }
    uint32_t GetDepth() const override { return m_depth; }
    uint32_t GetMipLevels() const override { return m_mipLevels; }
    uint32_t GetArrayLayers() const override { return m_arrayLayers; }
    TextureFormat GetFormat() const override { return m_format; }
    
    static int GetTextureCount() {
        return s_textureCount.load();
    }
};

std::atomic<int> MockTexture::s_textureCount{0};

// Mock TextureView implementation
class MockTextureView : public RefCounter<IRHITextureView>, public MockResource {
private:
    IRHITexture* m_texture;
    TextureFormat m_format;
    
public:
    MockTextureView(IRHITexture* texture, TextureFormat format)
        : m_texture(texture), m_format(format) {}
    
    IRHITexture* GetTexture() override { return m_texture; }
    TextureFormat GetFormat() const override { return m_format; }
    uint32_t GetWidth() const override { return m_texture->GetWidth(); }
    uint32_t GetHeight() const override { return m_texture->GetHeight(); }
    uint32_t GetBaseMipLevel() const override { return 0; }
    uint32_t GetMipLevelCount() const override { return 1; }
    uint32_t GetBaseArrayLayer() const override { return 0; }
    uint32_t GetArrayLayerCount() const override { return 1; }
};

// Mock Shader implementation
class MockShader : public RefCounter<IRHIShader>, public MockResource {
private:
    ShaderStage m_stage;
    
public:
    explicit MockShader(ShaderStage stage) : m_stage(stage) {}
    
    ShaderStage GetStage() const override { return m_stage; }
};

// Mock Pipeline implementation
class MockPipeline : public RefCounter<IRHIPipeline>, public MockResource {
public:
    MockPipeline() = default;
};

// Mock CommandList with resource tracking
class MockCommandList : public RefCounter<IRHICommandList>, public MockResource {
private:
    std::vector<RefCntPtr<IRefCounted>> m_referencedResources;
    bool m_isRecording;
    
public:
    MockCommandList() : m_isRecording(false) {}
    
    void Begin() override { 
        m_isRecording = true;
        m_referencedResources.clear();
    }
    
    void End() override { 
        m_isRecording = false;
    }
    
    void Reset() override {
        m_isRecording = false;
        m_referencedResources.clear();
    }
    
    void BeginRendering(const RenderingInfo& info) override {}
    void EndRendering() override {}
    
    void SetPipeline(IRHIPipeline* pipeline) override {
        if (m_isRecording && pipeline) {
            m_referencedResources.push_back(RefCntPtr<IRefCounted>(pipeline));
        }
    }
    
    void SetVertexBuffer(uint32_t binding, IRHIBuffer* buffer, size_t offset = 0) override {
        if (m_isRecording && buffer) {
            m_referencedResources.push_back(RefCntPtr<IRefCounted>(buffer));
        }
    }
    
    void BindIndexBuffer(IRHIBuffer* buffer, size_t offset = 0) override {
        if (m_isRecording && buffer) {
            m_referencedResources.push_back(RefCntPtr<IRefCounted>(buffer));
        }
    }
    
    void BindDescriptorSet(uint32_t setIndex, IRHIDescriptorSet* descriptorSet,
                          std::span<const uint32_t> dynamicOffsets = {}) override {}
    
    void PushConstants(ShaderStageFlags stageFlags, uint32_t offset, 
                      std::span<const std::byte> data) override {}
    
    void SetViewport(float x, float y, float width, float height) override {}
    void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override {}
    
    void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) override {}
    void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0) override {}
    void DrawIndexedIndirect(IRHIBuffer* buffer, size_t offset, uint32_t drawCount, 
                            uint32_t stride = sizeof(DrawIndexedIndirectCommand)) override {}
    
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) override {}
    void DispatchIndirect(IRHIBuffer* buffer, size_t offset) override {}
    
    void CopyBuffer(IRHIBuffer* srcBuffer, IRHIBuffer* dstBuffer, 
                   std::span<const BufferCopy> regions) override {}
    
    void CopyTexture(IRHITexture* srcTexture, IRHITexture* dstTexture,
                    std::span<const TextureCopy> regions) override {}
    
    void BlitTexture(IRHITexture* srcTexture, IRHITexture* dstTexture,
                    std::span<const TextureBlit> regions, FilterMode filter = FilterMode::LINEAR) override {}
    
    void Barrier(PipelineScope src_scope, PipelineScope dst_scope,
                std::span<const BufferTransition> buffer_transitions,
                std::span<const TextureTransition> texture_transitions,
                std::span<const MemoryBarrier> memory_barriers = {}) override {}
    
    void ReleaseToQueue(QueueType dstQueue,
                       std::span<const BufferTransition> buffer_transitions,
                       std::span<const TextureTransition> texture_transitions) override {}
    
    void AcquireFromQueue(QueueType srcQueue,
                         std::span<const BufferTransition> buffer_transitions,
                         std::span<const TextureTransition> texture_transitions) override {}
    
    // Helper methods for testing
    size_t GetReferencedResourceCount() const {
        return m_referencedResources.size();
    }
    
    void SimulateGPUCompletion() {
        m_referencedResources.clear();
    }
};

// Mock Swapchain implementation
class MockSwapchain : public RefCounter<IRHISwapchain>, public MockResource {
public:
    SwapchainStatus AcquireNextImage(uint32_t& imageIndex, IRHISemaphore* signalSemaphore = nullptr) override {
        imageIndex = 0;
        return SwapchainStatus::SUCCESS;
    }
    
    SwapchainStatus Present(uint32_t imageIndex, IRHISemaphore* waitSemaphore = nullptr) override {
        return SwapchainStatus::SUCCESS;
    }
    
    IRHITexture* GetBackBuffer(uint32_t index) override { return nullptr; }
    IRHITextureView* GetBackBufferView(uint32_t index) override { return nullptr; }
    uint32_t GetImageCount() const override { return 3; }
    void Resize(uint32_t width, uint32_t height) override {}
};

// Mock Semaphore implementation
class MockSemaphore : public RefCounter<IRHISemaphore>, public MockResource {
public:
    MockSemaphore() = default;
};

// Mock Fence implementation
class MockFence : public RefCounter<IRHIFence>, public MockResource {
private:
    bool m_signaled;
    
public:
    explicit MockFence(bool signaled = false) : m_signaled(signaled) {}
    
    void Wait(uint64_t timeout = UINT64_MAX) override {
        // Simulate wait
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        m_signaled = true;
    }
    
    void Reset() override {
        m_signaled = false;
    }
    
    bool IsSignaled() const override {
        return m_signaled;
    }
};

// Mock Composite Fence implementation
class MockCompositeFence : public RefCounter<IRHIFence>, public MockResource {
private:
    std::vector<FenceHandle> m_fences;
    
public:
    explicit MockCompositeFence(std::vector<FenceHandle> fences)
        : m_fences(std::move(fences)) {}
    
    void Wait(uint64_t timeout = UINT64_MAX) override {
        for (const auto& fence : m_fences) {
            if (fence) {
                fence->Wait(timeout);
            }
        }
    }
    
    void Reset() override {
        for (const auto& fence : m_fences) {
            if (fence) {
                fence->Reset();
            }
        }
    }
    
    bool IsSignaled() const override {
        for (const auto& fence : m_fences) {
            if (fence && !fence->IsSignaled()) {
                return false;
            }
        }
        return true;
    }
    
    size_t GetFenceCount() const {
        return m_fences.size();
    }
};

// Mock DescriptorSetLayout implementation
class MockDescriptorSetLayout : public RefCounter<IRHIDescriptorSetLayout>, public MockResource {
public:
    MockDescriptorSetLayout() = default;
};

// Mock DescriptorSet implementation
class MockDescriptorSet : public RefCounter<IRHIDescriptorSet>, public MockResource {
public:
    void BindBuffer(uint32_t binding, const BufferBinding& bufferBinding) override {}
    void BindTexture(uint32_t binding, const TextureBinding& textureBinding) override {}
};

// Mock Sampler implementation
class MockSampler : public RefCounter<IRHISampler>, public MockResource {
public:
    MockSampler() = default;
};

// Device implementation methods
BufferHandle MockDevice::CreateBuffer(const BufferDesc& desc) {
    MockBuffer* buffer = new MockBuffer(desc.size);
    return RefCntPtr<IRHIBuffer>::Create(buffer);
}

TextureHandle MockDevice::CreateTexture(const TextureDesc& desc) {
    MockTexture* texture = new MockTexture(desc.width, desc.height, desc.format);
    return RefCntPtr<IRHITexture>::Create(texture);
}

TextureViewHandle MockDevice::CreateTextureView(const TextureViewDesc& desc) {
    MockTextureView* view = new MockTextureView(desc.texture, desc.format);
    return RefCntPtr<IRHITextureView>::Create(view);
}

SamplerHandle MockDevice::CreateSampler(const SamplerDesc& desc) {
    MockSampler* sampler = new MockSampler();
    return RefCntPtr<IRHISampler>::Create(sampler);
}

ShaderHandle MockDevice::CreateShader(const ShaderDesc& desc) {
    MockShader* shader = new MockShader(desc.stage);
    return RefCntPtr<IRHIShader>::Create(shader);
}

PipelineHandle MockDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    MockPipeline* pipeline = new MockPipeline();
    return RefCntPtr<IRHIPipeline>::Create(pipeline);
}

PipelineHandle MockDevice::CreateComputePipeline(const ComputePipelineDesc& desc) {
    MockPipeline* pipeline = new MockPipeline();
    return RefCntPtr<IRHIPipeline>::Create(pipeline);
}

CommandListHandle MockDevice::CreateCommandList(QueueType queueType) {
    MockCommandList* cmdList = new MockCommandList();
    return RefCntPtr<IRHICommandList>::Create(cmdList);
}

SwapchainHandle MockDevice::CreateSwapchain(const SwapchainDesc& desc) {
    MockSwapchain* swapchain = new MockSwapchain();
    return RefCntPtr<IRHISwapchain>::Create(swapchain);
}

SemaphoreHandle MockDevice::CreateSemaphore() {
    MockSemaphore* semaphore = new MockSemaphore();
    return RefCntPtr<IRHISemaphore>::Create(semaphore);
}

FenceHandle MockDevice::CreateFence(bool signaled) {
    MockFence* fence = new MockFence(signaled);
    return RefCntPtr<IRHIFence>::Create(fence);
}

FenceHandle MockDevice::CreateCompositeFence(const std::vector<FenceHandle>& fences) {
    if (fences.empty()) {
        return nullptr;
    }
    MockCompositeFence* compositeFence = new MockCompositeFence(fences);
    return RefCntPtr<IRHIFence>::Create(compositeFence);
}

DescriptorSetLayoutHandle MockDevice::CreateDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
    MockDescriptorSetLayout* layout = new MockDescriptorSetLayout();
    return RefCntPtr<IRHIDescriptorSetLayout>::Create(layout);
}

DescriptorSetHandle MockDevice::CreateDescriptorSet(IRHIDescriptorSetLayout* layout, QueueType queueType) {
    MockDescriptorSet* set = new MockDescriptorSet();
    return RefCntPtr<IRHIDescriptorSet>::Create(set);
}

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test: All RHI resource types create and destroy properly
 * AC: All resources destroyed, created count == destroyed count, no memory leaks
 */
RHI_TEST(ResourceCreationAndDestruction) {
    g_totalResourcesCreated = 0;
    g_totalResourcesDestroyed = 0;
    
    {
        MockDevice* rawDevice = new MockDevice();
        DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
        RHI_ASSERT_EQ(1u, rawDevice->GetRefCount());
        
        // Create various resources
        BufferDesc bufferDesc{};
        bufferDesc.size = 1024;
        BufferHandle buffer = device->CreateBuffer(bufferDesc);
        RHI_ASSERT_NOT_NULL(buffer.Get());
        RHI_ASSERT_EQ(1u, buffer->GetRefCount());
        
        TextureDesc texDesc{};
        texDesc.width = 256;
        texDesc.height = 256;
        texDesc.format = TextureFormat::R8G8B8A8_UNORM;
        TextureHandle texture = device->CreateTexture(texDesc);
        RHI_ASSERT_NOT_NULL(texture.Get());
        RHI_ASSERT_EQ(1u, texture->GetRefCount());
        
        ShaderDesc shaderDesc{};
        shaderDesc.stage = ShaderStage::VERTEX;
        ShaderHandle shader = device->CreateShader(shaderDesc);
        RHI_ASSERT_NOT_NULL(shader.Get());
        RHI_ASSERT_EQ(1u, shader->GetRefCount());
        
        // Resources should exist
        RHI_ASSERT(g_totalResourcesCreated > 0);
    }
    
    // All resources should be destroyed
    RHI_ASSERT_EQ(g_totalResourcesCreated.load(), g_totalResourcesDestroyed.load());
    RHI_ASSERT_EQ(0, MockBuffer::GetBufferCount());
    RHI_ASSERT_EQ(0, MockTexture::GetTextureCount());
    
    return true;
}

/**
 * Test: RefCount behavior with multiple handles to RHI resources
 * AC: Correct refcounts through copy/assign/reset operations
 */
RHI_TEST(ReferenceCountingCorrectness) {
    MockBuffer::s_bufferCount = 0;
    
    {
        MockDevice* rawDevice = new MockDevice();
        DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
        
        BufferDesc desc{};
        desc.size = 2048;
        BufferHandle buffer1 = device->CreateBuffer(desc);
        RHI_ASSERT_EQ(1, MockBuffer::GetBufferCount());
        RHI_ASSERT_EQ(1u, buffer1->GetRefCount());
        
        // Copy handle should increase ref count
        BufferHandle buffer2 = buffer1;
        RHI_ASSERT_EQ(1, MockBuffer::GetBufferCount());  // Same object
        RHI_ASSERT_EQ(2u, buffer1->GetRefCount());
        RHI_ASSERT_EQ(2u, buffer2->GetRefCount());
        
        // Assignment should manage ref counts properly
        BufferHandle buffer3 = device->CreateBuffer(desc);
        RHI_ASSERT_EQ(2, MockBuffer::GetBufferCount());
        RHI_ASSERT_EQ(1u, buffer3->GetRefCount());
        
        buffer3 = buffer1;  // Should release old buffer3 and ref buffer1
        RHI_ASSERT_EQ(1, MockBuffer::GetBufferCount());
        RHI_ASSERT_EQ(3u, buffer1->GetRefCount());
        
        // Reset should decrease ref count
        buffer2.Clear();
        RHI_ASSERT_EQ(2u, buffer1->GetRefCount());
        
        buffer3.Clear();
        RHI_ASSERT_EQ(1u, buffer1->GetRefCount());
    }
    
    RHI_ASSERT_EQ(0, MockBuffer::GetBufferCount());
    
    return true;
}

/**
 * Test: Deferred destruction via frame retirement mechanism
 * AC: Resources kept alive in pending deletion until RetireCompletedFrame()
 */
RHI_TEST(FrameRetirementBehavior) {
    MockDevice* rawDevice = new MockDevice(true);
    DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
    
    // Create resources
    BufferDesc bufferDesc{};
    bufferDesc.size = 512;
    BufferHandle buffer = device->CreateBuffer(bufferDesc);
    
    // Simulate resource being queued for deletion
    rawDevice->AddPendingDeletion(buffer);
    RHI_ASSERT_EQ(1u, rawDevice->GetPendingDeletionCount());
    RHI_ASSERT_EQ(2u, buffer->GetRefCount());  // Handle + pending deletion
    
    // Release application handle
    buffer.Clear();
    
    // Resource should still exist due to pending deletion
    RHI_ASSERT_EQ(1u, rawDevice->GetPendingDeletionCount());
    
    // Retire completed frame should release resources
    device->RetireCompletedFrame();
    RHI_ASSERT_EQ(0u, rawDevice->GetPendingDeletionCount());
    
    return true;
}

/**
 * Test: Factory Create() vs constructor AddRef patterns
 * AC: Factory uses Create (no AddRef), constructor calls AddRef
 */
RHI_TEST(ResourceFactoryPattern) {
    MockDevice* rawDevice = new MockDevice();
    DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
    
    // Factory methods should use Create pattern (no extra AddRef)
    BufferDesc desc{};
    desc.size = 1024;
    BufferHandle buffer = device->CreateBuffer(desc);
    RHI_ASSERT_EQ(1u, buffer->GetRefCount());  // Should be 1, not 2
    
    // Direct construction should AddRef
    IRHIBuffer* rawBuffer = buffer.Get();
    BufferHandle buffer2(rawBuffer);  // Constructor calls AddRef
    RHI_ASSERT_EQ(2u, buffer->GetRefCount());
    
    // Test Create method with a NEW raw pointer (proper usage)
    MockBuffer* newRawBuffer = new MockBuffer(2048);
    RHI_ASSERT_EQ(1u, newRawBuffer->GetRefCount());  // Starts at 1
    
    BufferHandle buffer3 = RefCntPtr<IRHIBuffer>::Create(newRawBuffer);
    RHI_ASSERT_EQ(1u, newRawBuffer->GetRefCount());  // Create doesn't AddRef
    
    // buffer3 now owns newRawBuffer
    RHI_ASSERT_EQ(2048u, buffer3->GetSize());
    
    // Original buffer still has correct ref count
    RHI_ASSERT_EQ(2u, buffer->GetRefCount());  // buffer and buffer2
    
    return true;
}

/**
 * Test: Handle assignment and move semantics for RHI resources
 * AC: Proper ownership transfer, self-assignment safe, move leaves source null
 */
RHI_TEST(HandleAssignmentAndConversion) {
    MockDevice* rawDevice = new MockDevice();
    DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
    
    // Test assignment operators
    BufferDesc desc{};
    desc.size = 256;
    BufferHandle buffer1 = device->CreateBuffer(desc);
    BufferHandle buffer2 = device->CreateBuffer(desc);
    
    RHI_ASSERT_NE(buffer1.Get(), buffer2.Get());
    
    buffer2 = buffer1;  // Assignment
    RHI_ASSERT_EQ(buffer1.Get(), buffer2.Get());
    RHI_ASSERT_EQ(2u, buffer1->GetRefCount());
    
    // Move semantics
    BufferHandle buffer3 = std::move(buffer1);
    RHI_ASSERT_NULL(buffer1.Get());
    RHI_ASSERT_EQ(buffer3.Get(), buffer2.Get());
    RHI_ASSERT_EQ(2u, buffer3->GetRefCount());
    
    // Self-assignment should be safe
    buffer3 = buffer3;
    RHI_ASSERT_NOT_NULL(buffer3.Get());
    RHI_ASSERT_EQ(2u, buffer3->GetRefCount());
    
    return true;
}

/**
 * Test: Command lists keep resources alive during GPU execution
 * AC: Resources survive scope exit until SimulateGPUCompletion()
 */
RHI_TEST(CommandListResourceTracking) {
    MockDevice* rawDevice = new MockDevice();
    DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
    
    CommandListHandle cmdList = device->CreateCommandList();
    MockCommandList* mockCmd = static_cast<MockCommandList*>(cmdList.Get());
    
    cmdList->Begin();
    
    // Create resources in local scope
    {
        BufferDesc bufferDesc{};
        bufferDesc.size = 1024;
        BufferHandle vertexBuffer = device->CreateBuffer(bufferDesc);
        BufferHandle indexBuffer = device->CreateBuffer(bufferDesc);
        
        GraphicsPipelineDesc pipelineDesc{};
        PipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);
        
        // Bind resources to command list
        cmdList->SetVertexBuffer(0, vertexBuffer.Get());
        cmdList->BindIndexBuffer(indexBuffer.Get());
        cmdList->SetPipeline(pipeline.Get());
        
        // Command list should hold references
        RHI_ASSERT_EQ(3u, mockCmd->GetReferencedResourceCount());
        RHI_ASSERT_EQ(2u, vertexBuffer->GetRefCount());  // Local + cmdList
        RHI_ASSERT_EQ(2u, indexBuffer->GetRefCount());
        RHI_ASSERT_EQ(2u, pipeline->GetRefCount());
    }
    // Local handles destroyed, but resources should still be alive
    
    // Resources still referenced by command list
    RHI_ASSERT_EQ(3u, mockCmd->GetReferencedResourceCount());
    
    cmdList->End();
    
    // Simulate GPU completion
    mockCmd->SimulateGPUCompletion();
    RHI_ASSERT_EQ(0u, mockCmd->GetReferencedResourceCount());
    
    return true;
}

/**
 * Test: Composite fence aggregates multiple child fences
 * AC: Signals when all children signal, Reset() affects all children
 */
RHI_TEST(CompositeFencePattern) {
    MockDevice* rawDevice = new MockDevice();
    DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
    
    // Create individual fences
    FenceHandle fence1 = device->CreateFence(false);
    FenceHandle fence2 = device->CreateFence(false);
    FenceHandle fence3 = device->CreateFence(true);  // Pre-signaled
    
    RHI_ASSERT(!fence1->IsSignaled());
    RHI_ASSERT(!fence2->IsSignaled());
    RHI_ASSERT(fence3->IsSignaled());
    
    // Create composite fence
    std::vector<FenceHandle> fences = {fence1, fence2, fence3};
    FenceHandle compositeFence = device->CreateCompositeFence(fences);
    RHI_ASSERT_NOT_NULL(compositeFence.Get());
    
    MockCompositeFence* mockComposite = static_cast<MockCompositeFence*>(compositeFence.Get());
    RHI_ASSERT_EQ(3u, mockComposite->GetFenceCount());
    
    // Composite fence not signaled until all child fences are signaled
    RHI_ASSERT(!compositeFence->IsSignaled());
    
    // Signal individual fences
    fence1->Wait();
    RHI_ASSERT(fence1->IsSignaled());
    RHI_ASSERT(!compositeFence->IsSignaled());  // Still waiting for fence2
    
    fence2->Wait();
    RHI_ASSERT(fence2->IsSignaled());
    RHI_ASSERT(compositeFence->IsSignaled());  // All fences signaled
    
    // Reset should reset all child fences
    compositeFence->Reset();
    RHI_ASSERT(!fence1->IsSignaled());
    RHI_ASSERT(!fence2->IsSignaled());
    RHI_ASSERT(!fence3->IsSignaled());
    
    return true;
}

/**
 * Test: Concurrent refcount operations on RHI resources (8 threads × 1000 ops)
 * AC: No race conditions, final refcount == 1
 */
RHI_TEST(ThreadSafetyTest) {
    const int numThreads = 8;
    const int operationsPerThread = 1000;
    
    MockBuffer* rawBuffer = new MockBuffer(1024);
    BufferHandle buffer = RefCntPtr<IRHIBuffer>::Create(rawBuffer);
    RHI_ASSERT_EQ(1u, buffer->GetRefCount());
    
    std::vector<std::thread> threads;
    std::atomic<bool> startFlag{false};
    
    // Create threads that will manipulate ref counts
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&buffer, &startFlag, operationsPerThread]() {
            // Wait for start signal
            while (!startFlag.load()) {
                std::this_thread::yield();
            }
            
            // Perform many AddRef/Release operations
            for (int j = 0; j < operationsPerThread; ++j) {
                BufferHandle localHandle = buffer;  // AddRef
                // Just verify ref count is valid
                volatile auto refCount = localHandle->GetRefCount();
                (void)refCount;
                // localHandle destroyed, Release called
            }
        });
    }
    
    // Start all threads simultaneously
    startFlag = true;
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Reference count should be back to 1
    RHI_ASSERT_EQ(1u, buffer->GetRefCount());
    
    buffer.Clear();
    RHI_ASSERT_EQ(0, MockBuffer::GetBufferCount());
    
    return true;
}

/**
 * Test: Temporary resource lifetime in command lists (fire-and-forget)
 * AC: Resources destroyed only after GPU completion, not at scope exit
 */
RHI_TEST(FireAndForgetPattern) {
    MockDevice* rawDevice = new MockDevice();
    DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
    
    CommandListHandle cmdList = device->CreateCommandList();
    MockCommandList* mockCmd = static_cast<MockCommandList*>(cmdList.Get());
    
    cmdList->Begin();
    
    // Create temporary resources in local scope
    {
        // Fire-and-forget pattern: create, use, and forget
        BufferDesc tempBufferDesc{};
        tempBufferDesc.size = 256;
        BufferHandle tempBuffer = device->CreateBuffer(tempBufferDesc);
        
        // Use resource - CommandList keeps it alive
        cmdList->SetVertexBuffer(0, tempBuffer.Get());
        cmdList->Draw(100);
        
        RHI_ASSERT_EQ(2u, tempBuffer->GetRefCount());
        // tempBuffer goes out of scope here
    }
    
    // Resource still alive due to command list reference
    RHI_ASSERT_EQ(1u, mockCmd->GetReferencedResourceCount());
    
    cmdList->End();
    
    // Simulate GPU completion - resource can now be destroyed
    mockCmd->SimulateGPUCompletion();
    RHI_ASSERT_EQ(0u, mockCmd->GetReferencedResourceCount());
    
    return true;
}

/**
 * Test: All resources are properly cleaned up
 * AC: created count == destroyed count, all type-specific counters == 0
 */
RHI_TEST(MemoryLeakDetection) {
    g_totalResourcesCreated = 0;
    g_totalResourcesDestroyed = 0;
    MockBuffer::s_bufferCount = 0;
    MockTexture::s_textureCount = 0;
    
    {
        MockDevice* rawDevice = new MockDevice();
        DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
        
        // Create many resources
        std::vector<BufferHandle> buffers;
        std::vector<TextureHandle> textures;
        
        for (int i = 0; i < 10; ++i) {
            BufferDesc bufferDesc{};
            bufferDesc.size = 1024 * (i + 1);
            buffers.push_back(device->CreateBuffer(bufferDesc));
            
            TextureDesc texDesc{};
            texDesc.width = 128 * (i + 1);
            texDesc.height = 128 * (i + 1);
            texDesc.format = TextureFormat::R8G8B8A8_UNORM;
            textures.push_back(device->CreateTexture(texDesc));
        }
        
        RHI_ASSERT_EQ(10, MockBuffer::GetBufferCount());
        RHI_ASSERT_EQ(10, MockTexture::GetTextureCount());
        
        // Clear some resources
        buffers.erase(buffers.begin(), buffers.begin() + 5);
        RHI_ASSERT_EQ(5, MockBuffer::GetBufferCount());
        
        textures.clear();
        RHI_ASSERT_EQ(0, MockTexture::GetTextureCount());
    }
    
    // All resources should be cleaned up
    RHI_ASSERT_EQ(0, MockBuffer::GetBufferCount());
    RHI_ASSERT_EQ(0, MockTexture::GetTextureCount());
    RHI_ASSERT_EQ(g_totalResourcesCreated.load(), g_totalResourcesDestroyed.load());
    
    return true;
}

/**
 * Test: Operations on null handles are safe
 * AC: No crashes, comparisons work, Reset() on null is safe
 */
RHI_TEST(NullHandleOperations) {
    // Default construction creates null handle
    BufferHandle nullBuffer;
    RHI_ASSERT_NULL(nullBuffer.Get());
    RHI_ASSERT_EQ(0u, nullBuffer.Clear());  // Reset on null is safe
    
    // Assignment from nullptr
    BufferHandle buffer = nullptr;
    RHI_ASSERT_NULL(buffer.Get());
    
    // Comparison with nullptr
    RHI_ASSERT(nullBuffer == nullptr);
    RHI_ASSERT(nullptr == nullBuffer);
    
    // Create valid handle then nullify
    {
        MockBuffer* raw = new MockBuffer(512);
        buffer = RefCntPtr<IRHIBuffer>::Create(raw);
        RHI_ASSERT_NOT_NULL(buffer.Get());
        RHI_ASSERT_EQ(1u, buffer->GetRefCount());
        
        buffer = nullptr;
        RHI_ASSERT_NULL(buffer.Get());
        // Buffer should be destroyed
    }
    
    RHI_ASSERT_EQ(0, MockBuffer::GetBufferCount());
    
    return true;
}

/**
 * Test: Base/derived handle conversions for RHI types
 * AC: Proper refcount management across type conversions
 */
RHI_TEST(CrossTypeHandleConversions) {
    // Test base to derived conversions
    MockBuffer* rawBuffer = new MockBuffer(1024);
    BufferHandle bufferHandle = RefCntPtr<IRHIBuffer>::Create(rawBuffer);
    
    // IRHIBuffer to IRefCounted (base class)
    RefCntPtr<IRefCounted> baseHandle(bufferHandle);
    RHI_ASSERT_EQ(2u, rawBuffer->GetRefCount());
    RHI_ASSERT_EQ(bufferHandle.Get(), baseHandle.Get());
    
    // Can't convert back without casting
    IRHIBuffer* bufferPtr = static_cast<IRHIBuffer*>(baseHandle.Get());
    RHI_ASSERT_EQ(rawBuffer, bufferPtr);
    
    baseHandle.Clear();
    RHI_ASSERT_EQ(1u, rawBuffer->GetRefCount());
    
    bufferHandle.Clear();
    RHI_ASSERT_EQ(0, MockBuffer::GetBufferCount());
    
    return true;
}

/**
 * Test: Device manages resource lifetime correctly
 * AC: Resources can outlive device if held externally
 */
RHI_TEST(DeviceLifetimeManagement) {
    g_totalResourcesCreated = 0;
    g_totalResourcesDestroyed = 0;
    
    // Test that device properly manages resource lifetime
    BufferHandle persistentBuffer;
    
    {
        MockDevice* rawDevice = new MockDevice();
        DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
        
        BufferDesc desc{};
        desc.size = 2048;
        persistentBuffer = device->CreateBuffer(desc);
        RHI_ASSERT_EQ(1u, persistentBuffer->GetRefCount());
        
        // Create more resources
        TextureHandle texture = device->CreateTexture(TextureDesc{256, 256, 1, 1, 1, TextureFormat::R8G8B8A8_UNORM});
        ShaderHandle shader = device->CreateShader(ShaderDesc{ShaderStage::FRAGMENT, nullptr, 0});
        
        // Device going out of scope
    }
    
    // Buffer should still exist due to persistentBuffer handle
    RHI_ASSERT_NOT_NULL(persistentBuffer.Get());
    RHI_ASSERT_EQ(1u, persistentBuffer->GetRefCount());
    
    persistentBuffer.Clear();
    
    // Now all resources should be destroyed
    RHI_ASSERT_EQ(g_totalResourcesCreated.load(), g_totalResourcesDestroyed.load());
    
    return true;
}

/**
 * Test: Swap() and Detach() methods on RHI handles
 * AC: Correct pointer exchange, Detach returns ownership without Release
 */
RHI_TEST(SwapAndDetachOperations) {
    MockBuffer* raw1 = new MockBuffer(512);
    MockBuffer* raw2 = new MockBuffer(1024);
    
    BufferHandle buffer1 = RefCntPtr<IRHIBuffer>::Create(raw1);
    BufferHandle buffer2 = RefCntPtr<IRHIBuffer>::Create(raw2);
    
    RHI_ASSERT_EQ(512u, buffer1->GetSize());
    RHI_ASSERT_EQ(1024u, buffer2->GetSize());
    
    // Test Swap
    buffer1.Swap(buffer2);
    RHI_ASSERT_EQ(1024u, buffer1->GetSize());
    RHI_ASSERT_EQ(512u, buffer2->GetSize());
    RHI_ASSERT_EQ(1u, buffer1->GetRefCount());
    RHI_ASSERT_EQ(1u, buffer2->GetRefCount());
    
    // Test Detach
    IRHIBuffer* detached = buffer1.Detach();
    RHI_ASSERT_NULL(buffer1.Get());
    RHI_ASSERT_NOT_NULL(detached);
    RHI_ASSERT_EQ(1u, detached->GetRefCount());
    
    // Clean up detached pointer manually
    detached->Release();
    
    buffer2.Clear();
    RHI_ASSERT_EQ(0, MockBuffer::GetBufferCount());
    
    return true;
}

/**
 * Test: Texture/TextureView relationships without circular refs
 * AC: No circular dependencies, proper cleanup of dependent resources
 */
RHI_TEST(ComplexResourceDependencies) {
    MockDevice* rawDevice = new MockDevice();
    DeviceHandle device = RefCntPtr<IRHIDevice>::Create(rawDevice);
    
    // Create texture and texture view
    TextureDesc texDesc{};
    texDesc.width = 512;
    texDesc.height = 512;
    texDesc.format = TextureFormat::R8G8B8A8_UNORM;
    TextureHandle texture = device->CreateTexture(texDesc);
    
    TextureViewDesc viewDesc{};
    viewDesc.texture = texture.Get();
    viewDesc.format = TextureFormat::R8G8B8A8_UNORM;
    TextureViewHandle textureView = device->CreateTextureView(viewDesc);
    
    // TextureView holds raw pointer to texture, not a RefCntPtr
    // This is by design to avoid circular references
    RHI_ASSERT_EQ(texture.Get(), textureView->GetTexture());
    RHI_ASSERT_EQ(1u, texture->GetRefCount());  // Only the handle
    
    // Create descriptor set that references the view
    DescriptorSetLayoutDesc layoutDesc{};
    DescriptorSetLayoutHandle layout = device->CreateDescriptorSetLayout(layoutDesc);
    DescriptorSetHandle descriptorSet = device->CreateDescriptorSet(layout.Get());
    
    // All resources properly managed
    texture.Clear();
    textureView.Clear();
    layout.Clear();
    descriptorSet.Clear();
    
    // All cleaned up
    RHI_ASSERT_EQ(0, MockTexture::GetTextureCount());
    
    return true;
}

} // namespace test
} // namespace rhi