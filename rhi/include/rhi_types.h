#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace RHI
{

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
class IRHIDescriptorSetLayout;
class IRHIDescriptorSet;
class IRHISampler;

// Enumerations
enum class BufferUsage : uint32_t
{
	VERTEX  = 1 << 0,
	INDEX   = 1 << 1,
	UNIFORM = 1 << 2,
	STORAGE = 1 << 3
};

enum class MemoryType
{
	GPU_ONLY,
	CPU_TO_GPU,
	GPU_TO_CPU
};

enum class ShaderStage
{
	VERTEX,
	FRAGMENT,
	COMPUTE
};

enum class PrimitiveTopology
{
	POINT_LIST,
	LINE_LIST,
	LINE_STRIP,
	TRIANGLE_LIST,
	TRIANGLE_STRIP
};

enum class TextureFormat
{
	UNDEFINED,
	R8G8B8A8_UNORM,
	R8G8B8A8_SRGB,
	B8G8R8A8_UNORM,
	B8G8R8A8_SRGB,
	R32G32B32_FLOAT,
	D32_FLOAT,
	D24_UNORM_S8_UINT
};

enum class DescriptorType
{
	UNIFORM_BUFFER,
	STORAGE_BUFFER,
	SAMPLER,
	TEXTURE,
	COMBINED_IMAGE_SAMPLER
};

enum class ShaderStageFlags : uint32_t
{
	VERTEX       = 1 << 0,
	FRAGMENT     = 1 << 1,
	COMPUTE      = 1 << 2,
	ALL_GRAPHICS = VERTEX | FRAGMENT
};

enum class QueueType
{
	GRAPHICS,
	COMPUTE,
	TRANSFER
};

enum class ImageLayout
{
	UNDEFINED,
	GENERAL,
	COLOR_ATTACHMENT,
	DEPTH_STENCIL_ATTACHMENT,
	DEPTH_STENCIL_READ_ONLY,
	SHADER_READ_ONLY,
	TRANSFER_SRC,
	TRANSFER_DST,
	PRESENT_SRC
};

// Structures
struct BufferDesc
{
	size_t      size;
	BufferUsage usage;
	MemoryType  memoryType;
	const void *initialData = nullptr;
};

struct ShaderDesc
{
	ShaderStage stage;
	const void *code;
	size_t      codeSize;
	const char *entryPoint = "main";
};

struct VertexAttribute
{
	uint32_t      location;
	uint32_t      binding;
	TextureFormat format;
	uint32_t      offset;
};

struct VertexBinding
{
	uint32_t binding;
	uint32_t stride;
	bool     perInstance = false;
};

struct VertexLayout
{
	std::vector<VertexAttribute> attributes;
	std::vector<VertexBinding>   bindings;
};

struct DescriptorBinding
{
	uint32_t         binding;
	DescriptorType   type;
	uint32_t         count = 1;
	ShaderStageFlags stageFlags;
};

struct DescriptorSetLayoutDesc
{
	std::vector<DescriptorBinding> bindings;
};

struct PushConstantRange
{
	ShaderStageFlags stageFlags;
	uint32_t         offset;
	uint32_t         size;
};

struct BufferBinding
{
	IRHIBuffer    *buffer;
	size_t         offset = 0;
	size_t         range  = 0;
	DescriptorType type   = DescriptorType::UNIFORM_BUFFER;
};

struct TextureBinding
{
	IRHITexture   *texture;
	IRHISampler   *sampler = nullptr;
	ImageLayout    layout  = ImageLayout::SHADER_READ_ONLY;
	DescriptorType type    = DescriptorType::COMBINED_IMAGE_SAMPLER;
};

struct GraphicsPipelineDesc
{
	IRHIShader                            *vertexShader;
	IRHIShader                            *fragmentShader;
	VertexLayout                           vertexLayout;
	PrimitiveTopology                      topology         = PrimitiveTopology::TRIANGLE_LIST;
	TextureFormat                          colorFormat      = TextureFormat::R8G8B8A8_UNORM;
	TextureFormat                          depthFormat      = TextureFormat::UNDEFINED;
	bool                                   depthTestEnable  = false;
	bool                                   depthWriteEnable = false;
	std::vector<IRHIDescriptorSetLayout *> descriptorSetLayouts;
	std::vector<PushConstantRange>         pushConstantRanges;
};

struct SwapchainDesc
{
	void         *windowHandle;        // HWND on Windows, NSWindow* on macOS
	uint32_t      width;
	uint32_t      height;
	TextureFormat format      = TextureFormat::B8G8R8A8_UNORM;
	uint32_t      bufferCount = 2;
	bool          vsync       = true;
};

struct ClearValue
{
	union
	{
		float color[4];
		struct
		{
			float    depth;
			uint32_t stencil;
		} depthStencil;
	};
};

struct RenderPassBeginInfo
{
	IRHITexture *colorTarget;
	IRHITexture *depthTarget = nullptr;
	uint32_t     width;
	uint32_t     height;
	ClearValue   clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
	ClearValue   clearDepth;
	bool         shouldClearColor = true;
	bool         shouldClearDepth = true;
};

}        // namespace RHI