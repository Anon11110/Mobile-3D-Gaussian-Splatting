#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>

namespace RHI {

// Forward declarations
class IRHIDevice;
class IRHIBuffer;
class IRHITexture;
class IRHIPipeline;
class IRHIShader;
class IRHICommandList;
class IRHISwapchain;
class IRHISemaphore;
class IRHIFence;

// Enumerations
enum class BufferUsage : uint32_t {
    VERTEX   = 1 << 0,
    INDEX    = 1 << 1,
    UNIFORM  = 1 << 2,
    STORAGE  = 1 << 3
};

enum class MemoryType {
    GPU_ONLY,
    CPU_TO_GPU,
    GPU_TO_CPU
};

enum class ShaderStage {
    VERTEX,
    FRAGMENT,
    COMPUTE
};

enum class PrimitiveTopology {
    POINT_LIST,
    LINE_LIST,
    LINE_STRIP,
    TRIANGLE_LIST,
    TRIANGLE_STRIP
};

enum class TextureFormat {
    UNDEFINED,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_UNORM,
    B8G8R8A8_SRGB,
    R32G32B32_FLOAT,
    D32_FLOAT,
    D24_UNORM_S8_UINT
};

// Structures
struct BufferDesc {
    size_t size;
    BufferUsage usage;
    MemoryType memoryType;
    const void* initialData = nullptr;
};

struct ShaderDesc {
    ShaderStage stage;
    const void* code;
    size_t codeSize;
    const char* entryPoint = "main";
};

struct VertexAttribute {
    uint32_t location;
    uint32_t binding;
    TextureFormat format;
    uint32_t offset;
};

struct VertexBinding {
    uint32_t binding;
    uint32_t stride;
    bool perInstance = false;
};

struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    std::vector<VertexBinding> bindings;
};

struct GraphicsPipelineDesc {
    IRHIShader* vertexShader;
    IRHIShader* fragmentShader;
    VertexLayout vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST;
    TextureFormat colorFormat = TextureFormat::R8G8B8A8_UNORM;
    TextureFormat depthFormat = TextureFormat::UNDEFINED;
    bool depthTestEnable = false;
    bool depthWriteEnable = false;
};

struct SwapchainDesc {
    void* windowHandle;  // HWND on Windows, NSWindow* on macOS
    uint32_t width;
    uint32_t height;
    TextureFormat format = TextureFormat::B8G8R8A8_UNORM;
    uint32_t bufferCount = 2;
    bool vsync = true;
};

struct ClearValue {
    union {
        float color[4];
        struct {
            float depth;
            uint32_t stencil;
        };
    };
};

struct RenderPassBeginInfo {
    IRHITexture* colorTarget;
    IRHITexture* depthTarget = nullptr;
    uint32_t width;
    uint32_t height;
    ClearValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
    ClearValue clearDepth = {.depth = 1.0f, .stencil = 0};
    bool shouldClearColor = true;
    bool shouldClearDepth = true;
};

} // namespace RHI