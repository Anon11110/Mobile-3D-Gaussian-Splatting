#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#ifdef _MSC_VER
#	define RHI_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#	define RHI_RESTRICT __restrict__
#else
#	define RHI_RESTRICT
#endif

namespace rhi
{

// Forward declarations
class IRHIDevice;
class IRHIBuffer;
class IRHITexture;
class IRHITextureView;
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

// Index buffer data type
enum class IndexType : uint8_t
{
	UINT16,        // 2 bytes per index, max 65,536 vertices
	UINT32         // 4 bytes per index, max 4,294,967,296 vertices
};

// Bitwise operators for BufferUsage
constexpr inline BufferUsage operator|(BufferUsage lhs, BufferUsage rhs)
{
	return static_cast<BufferUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr inline BufferUsage operator&(BufferUsage lhs, BufferUsage rhs)
{
	return static_cast<BufferUsage>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr inline BufferUsage operator~(BufferUsage usage)
{
	return static_cast<BufferUsage>(~static_cast<uint32_t>(usage));
}

inline BufferUsage &operator|=(BufferUsage &lhs, BufferUsage rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

inline BufferUsage &operator&=(BufferUsage &lhs, BufferUsage rhs)
{
	lhs = lhs & rhs;
	return lhs;
}

constexpr inline bool operator!(BufferUsage usage)
{
	return static_cast<uint32_t>(usage) == 0;
}

enum class ResourceUsage
{
	Static,               // Immutable after initialization (textures, static meshes)
	DynamicUpload,        // CPU->GPU frequent updates (uniform/vertex rings)
	Readback,             // GPU->CPU transfers
	Transient             // Per-frame scratch RT/UAV/temp buffers
};

struct AllocationHints
{
	bool prefer_device_local = true;         // for Static/Transient
	bool persistently_mapped = false;        // for DynamicUpload/Readback
	bool allow_dedicated     = false;        // let backend auto-decide for huge resources
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

enum class PolygonMode
{
	FILL,
	LINE,
	POINT
};

enum class CullMode
{
	NONE,
	FRONT,
	BACK
};

enum class FrontFace
{
	COUNTER_CLOCKWISE,
	CLOCKWISE
};

enum class CompareOp
{
	NEVER,
	LESS,
	EQUAL,
	LESS_OR_EQUAL,
	GREATER,
	NOT_EQUAL,
	GREATER_OR_EQUAL,
	ALWAYS
};

enum class StencilOp
{
	KEEP,
	ZERO,
	REPLACE,
	INCREMENT_AND_CLAMP,
	DECREMENT_AND_CLAMP,
	INVERT,
	INCREMENT_AND_WRAP,
	DECREMENT_AND_WRAP
};

enum class BlendFactor
{
	ZERO,
	ONE,
	SRC_COLOR,
	ONE_MINUS_SRC_COLOR,
	DST_COLOR,
	ONE_MINUS_DST_COLOR,
	SRC_ALPHA,
	ONE_MINUS_SRC_ALPHA,
	DST_ALPHA,
	ONE_MINUS_DST_ALPHA,
	CONSTANT_COLOR,
	ONE_MINUS_CONSTANT_COLOR,
	CONSTANT_ALPHA,
	ONE_MINUS_CONSTANT_ALPHA,
	SRC_ALPHA_SATURATE
};

enum class BlendOp
{
	ADD,
	SUBTRACT,
	REVERSE_SUBTRACT,
	MIN,
	MAX
};

enum class SampleCount
{
	COUNT_1  = 1,
	COUNT_2  = 2,
	COUNT_4  = 4,
	COUNT_8  = 8,
	COUNT_16 = 16,
	COUNT_32 = 32,
	COUNT_64 = 64
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

enum class VertexFormat
{
	UNDEFINED,
	R32_SFLOAT,
	R32G32_SFLOAT,
	R32G32B32_SFLOAT,
	R32G32B32A32_SFLOAT,
	R16_SFLOAT,
	R16G16_SFLOAT,
	R16G16B16_SFLOAT,
	R16G16B16A16_SFLOAT,
	R8_UNORM,
	R8G8_UNORM,
	R8G8B8_UNORM,
	R8G8B8A8_UNORM,
	R8_UINT,
	R8G8_UINT,
	R8G8B8_UINT,
	R8G8B8A32_UINT,
	R16_UINT,
	R16G16_UINT,
	R16G16B16_UINT,
	R16G16B16A16_UINT,
	R32_UINT,
	R32G32_UINT,
	R32G32B32_UINT,
	R32G32B32A32_UINT
};

enum class DescriptorType
{
	// Buffers
	UNIFORM_BUFFER,        // VK: UNIFORM_BUFFER
	STORAGE_BUFFER,        // VK: STORAGE_BUFFER

	// Texel buffer views
	UNIFORM_TEXEL_BUFFER,        // VK: UNIFORM_TEXEL_BUFFER
	STORAGE_TEXEL_BUFFER,        // VK: STORAGE_TEXEL_BUFFER

	// Textures
	SAMPLED_TEXTURE,        // VK: SAMPLED_IMAGE
	STORAGE_TEXTURE,        // VK: STORAGE_IMAGE

	// Samplers
	SAMPLER,        // VK: SAMPLER
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

enum class PipelineType
{
	GRAPHICS,
	COMPUTE
};

enum class SwapchainStatus
{
	SUCCESS,
	OUT_OF_DATE,
	SUBOPTIMAL,
	ERROR_OCCURRED
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

// Sampler filtering modes
enum class FilterMode
{
	NEAREST,
	LINEAR
};

// Sampler mipmap modes
enum class MipmapMode
{
	NEAREST,
	LINEAR
};

// Sampler addressing modes
enum class SamplerAddressMode
{
	REPEAT,
	MIRRORED_REPEAT,
	CLAMP_TO_EDGE,
	CLAMP_TO_BORDER,
	MIRROR_CLAMP_TO_EDGE
};

// Sampler border colors
enum class BorderColor
{
	FLOAT_TRANSPARENT_BLACK,
	INT_TRANSPARENT_BLACK,
	FLOAT_OPAQUE_BLACK,
	INT_OPAQUE_BLACK,
	FLOAT_OPAQUE_WHITE,
	INT_OPAQUE_WHITE
};

enum class TextureAspect : uint32_t
{
	COLOR   = 1 << 0,
	DEPTH   = 1 << 1,
	STENCIL = 1 << 2
};

// Bitwise operators for TextureAspect flags
constexpr inline TextureAspect operator|(TextureAspect lhs, TextureAspect rhs)
{
	return static_cast<TextureAspect>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr inline TextureAspect operator&(TextureAspect lhs, TextureAspect rhs)
{
	return static_cast<TextureAspect>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr inline TextureAspect operator~(TextureAspect aspect)
{
	return static_cast<TextureAspect>(~static_cast<uint32_t>(aspect));
}

inline TextureAspect &operator|=(TextureAspect &lhs, TextureAspect rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

inline TextureAspect &operator&=(TextureAspect &lhs, TextureAspect rhs)
{
	lhs = lhs & rhs;
	return lhs;
}

constexpr inline bool operator!(TextureAspect aspect)
{
	return static_cast<uint32_t>(aspect) == 0;
}

// Barrier and Synchronization types, defining the high-level intended usage of a resource
enum class ResourceState : uint8_t
{
	Undefined,          // Initial state, content can be discarded
	GeneralRead,        // Generic read-only state (vertex/index/uniform buffer, shader resource)
	CopySource,
	CopyDestination,
	ShaderReadWrite,        // Read/write storage (UAV/SSBO)
	RenderTarget,
	DepthStencilRead,
	DepthStencilWrite,
	ResolveSource,
	ResolveDestination,
	Present,                 // Ready for presentation to the screen
	IndirectArgument,        // Buffer used as indirect argument
	VertexBuffer,
	IndexBuffer,
	UniformBuffer,
};

// Pipeline execution scope where work occurs
enum class PipelineScope : uint8_t
{
	All,             // A catch-all, potentially less efficient
	Graphics,        // Graphics pipeline (vertex, fragment, etc.)
	Compute,         // Compute pipeline
	Copy,            // Transfer/blit engine
};

// --- Optional Fine-Grained Control (The "Pro Mode") ---

// Fine-grained pipeline execution stages
enum class StageMask : uint64_t
{
	Auto           = 0,
	DrawIndirect   = 1ull << 1,
	VertexInput    = 1ull << 2,
	VertexShader   = 1ull << 3,
	FragmentShader = 1ull << 4,
	DepthTests     = 1ull << 5,        // Early & Late
	RenderTarget   = 1ull << 6,
	ComputeShader  = 1ull << 7,
	Transfer       = 1ull << 8,
	Host           = 1ull << 9,
	AllGraphics    = 1ull << 10,
	AllCommands    = 1ull << 11,
};

// Fine-grained memory access types
enum class AccessMask : uint64_t
{
	Auto                = 0,
	IndirectRead        = 1ull << 1,
	IndexRead           = 1ull << 2,
	VertexAttributeRead = 1ull << 3,
	UniformRead         = 1ull << 4,
	ShaderRead          = 1ull << 5,
	ShaderWrite         = 1ull << 6,
	RenderTargetRead    = 1ull << 7,
	RenderTargetWrite   = 1ull << 8,
	DepthStencilRead    = 1ull << 9,
	DepthStencilWrite   = 1ull << 10,
	TransferRead        = 1ull << 11,
	TransferWrite       = 1ull << 12,
	HostRead            = 1ull << 13,
	HostWrite           = 1ull << 14,
	MemoryRead          = 1ull << 15,
	MemoryWrite         = 1ull << 16,
};

constexpr inline StageMask operator|(StageMask lhs, StageMask rhs)
{
	return static_cast<StageMask>(static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs));
}

constexpr inline StageMask operator&(StageMask lhs, StageMask rhs)
{
	return static_cast<StageMask>(static_cast<uint64_t>(lhs) & static_cast<uint64_t>(rhs));
}

inline StageMask &operator|=(StageMask &lhs, StageMask rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

constexpr inline AccessMask operator|(AccessMask lhs, AccessMask rhs)
{
	return static_cast<AccessMask>(static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs));
}

constexpr inline AccessMask operator&(AccessMask lhs, AccessMask rhs)
{
	return static_cast<AccessMask>(static_cast<uint64_t>(lhs) & static_cast<uint64_t>(rhs));
}

inline AccessMask &operator|=(AccessMask &lhs, AccessMask rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

enum class ResolveMode
{
	NONE,
	SAMPLE_ZERO,
	AVERAGE
};

enum class LoadOp
{
	LOAD,
	CLEAR,
	DONT_CARE
};

enum class StoreOp
{
	STORE,
	DONT_CARE
};

// Structures
struct BufferDesc
{
	size_t          size;
	BufferUsage     usage;
	ResourceUsage   resourceUsage = ResourceUsage::Static;
	IndexType       indexType     = IndexType::UINT32;        // Default to UINT32 for flexibility
	AllocationHints hints         = {};
	const void     *initialData   = nullptr;
};

struct TextureDesc
{
	uint32_t        width;
	uint32_t        height;
	uint32_t        depth       = 1;
	uint32_t        mipLevels   = 1;
	uint32_t        arrayLayers = 1;
	TextureFormat   format;
	ResourceUsage   resourceUsage   = ResourceUsage::Static;
	AllocationHints hints           = {};
	bool            isRenderTarget  = false;
	bool            isDepthStencil  = false;
	const void     *initialData     = nullptr;
	size_t          initialDataSize = 0;
};

struct TextureViewDesc
{
	IRHITexture  *texture;
	TextureFormat format          = TextureFormat::UNDEFINED;
	TextureAspect aspectMask      = TextureAspect::COLOR;
	uint32_t      baseMipLevel    = 0;
	uint32_t      mipLevelCount   = 1;
	uint32_t      baseArrayLayer  = 0;
	uint32_t      arrayLayerCount = 1;
};

struct ShaderDesc
{
	ShaderStage stage;
	const void *code;
	size_t      codeSize;
	const char *entryPoint = "main";
};

struct SamplerDesc
{
	FilterMode         magFilter               = FilterMode::LINEAR;
	FilterMode         minFilter               = FilterMode::LINEAR;
	MipmapMode         mipmapMode              = MipmapMode::LINEAR;
	SamplerAddressMode addressModeU            = SamplerAddressMode::REPEAT;
	SamplerAddressMode addressModeV            = SamplerAddressMode::REPEAT;
	SamplerAddressMode addressModeW            = SamplerAddressMode::REPEAT;
	float              mipLodBias              = 0.0f;
	bool               anisotropyEnable        = false;
	float              maxAnisotropy           = 1.0f;
	bool               compareEnable           = false;
	CompareOp          compareOp               = CompareOp::ALWAYS;
	float              minLod                  = 0.0f;
	float              maxLod                  = 1000.0f;
	BorderColor        borderColor             = BorderColor::FLOAT_TRANSPARENT_BLACK;
	bool               unnormalizedCoordinates = false;
};

struct VertexAttribute
{
	uint32_t     location;
	uint32_t     binding;
	VertexFormat format;
	uint32_t     offset;
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
	DescriptorType type    = DescriptorType::SAMPLED_TEXTURE;
};

struct BufferCopy
{
	uint64_t srcOffset = 0;
	uint64_t dstOffset = 0;
	uint64_t size      = 0;
};

struct TextureCopy
{
	uint32_t      srcMipLevel   = 0;
	uint32_t      srcArrayLayer = 0;
	uint32_t      srcX          = 0;
	uint32_t      srcY          = 0;
	uint32_t      srcZ          = 0;
	uint32_t      dstMipLevel   = 0;
	uint32_t      dstArrayLayer = 0;
	uint32_t      dstX          = 0;
	uint32_t      dstY          = 0;
	uint32_t      dstZ          = 0;
	uint32_t      width         = 0;
	uint32_t      height        = 0;
	uint32_t      depth         = 1;
	uint32_t      layerCount    = 1;
	TextureAspect aspectMask    = TextureAspect::COLOR;
};

struct TextureBlit
{
	uint32_t      srcMipLevel   = 0;
	uint32_t      srcArrayLayer = 0;
	uint32_t      srcX0         = 0;
	uint32_t      srcY0         = 0;
	uint32_t      srcZ0         = 0;
	uint32_t      srcX1         = 0;
	uint32_t      srcY1         = 0;
	uint32_t      srcZ1         = 1;
	uint32_t      dstMipLevel   = 0;
	uint32_t      dstArrayLayer = 0;
	uint32_t      dstX0         = 0;
	uint32_t      dstY0         = 0;
	uint32_t      dstZ0         = 0;
	uint32_t      dstX1         = 0;
	uint32_t      dstY1         = 0;
	uint32_t      dstZ1         = 1;
	uint32_t      layerCount    = 1;
	TextureAspect aspectMask    = TextureAspect::COLOR;
};

struct RasterizationState
{
	bool        depthClampEnable        = false;
	bool        rasterizerDiscardEnable = false;
	PolygonMode polygonMode             = PolygonMode::FILL;
	CullMode    cullMode                = CullMode::BACK;
	FrontFace   frontFace               = FrontFace::CLOCKWISE;
	bool        depthBiasEnable         = false;
	float       depthBiasConstantFactor = 0.0f;
	float       depthBiasClamp          = 0.0f;
	float       depthBiasSlopeFactor    = 0.0f;
};

struct StencilOpState
{
	StencilOp failOp      = StencilOp::KEEP;
	StencilOp passOp      = StencilOp::KEEP;
	StencilOp depthFailOp = StencilOp::KEEP;
	CompareOp compareOp   = CompareOp::ALWAYS;
	uint32_t  compareMask = ~0U;
	uint32_t  writeMask   = ~0U;
	uint32_t  reference   = 0;
};

struct DepthStencilState
{
	bool           depthTestEnable   = false;
	bool           depthWriteEnable  = false;
	CompareOp      depthCompareOp    = CompareOp::LESS;
	bool           stencilTestEnable = false;
	StencilOpState front             = {};
	StencilOpState back              = {};
};

struct ColorBlendAttachmentState
{
	bool        blendEnable         = false;
	BlendFactor srcColorBlendFactor = BlendFactor::ONE;
	BlendFactor dstColorBlendFactor = BlendFactor::ZERO;
	BlendOp     colorBlendOp        = BlendOp::ADD;
	BlendFactor srcAlphaBlendFactor = BlendFactor::ONE;
	BlendFactor dstAlphaBlendFactor = BlendFactor::ZERO;
	BlendOp     alphaBlendOp        = BlendOp::ADD;
	uint32_t    colorWriteMask      = 0xF;        // R | G | B | A
};

struct MultisampleState
{
	SampleCount rasterizationSamples  = SampleCount::COUNT_1;
	bool        sampleShadingEnable   = false;
	float       minSampleShading      = 1.0f;
	uint32_t    sampleMask            = ~0U;
	bool        alphaToCoverageEnable = false;
};

struct RenderTargetSignature
{
	std::vector<TextureFormat> colorFormats = {TextureFormat::R8G8B8A8_UNORM};
	TextureFormat              depthFormat  = TextureFormat::UNDEFINED;
	SampleCount                sampleCount  = SampleCount::COUNT_1;
};

struct GraphicsPipelineDesc
{
	IRHIShader                            *vertexShader;
	IRHIShader                            *fragmentShader;
	VertexLayout                           vertexLayout;
	PrimitiveTopology                      topology               = PrimitiveTopology::TRIANGLE_LIST;
	bool                                   primitiveRestartEnable = false;
	RasterizationState                     rasterizationState     = {};
	DepthStencilState                      depthStencilState      = {};
	MultisampleState                       multisampleState       = {};
	std::vector<ColorBlendAttachmentState> colorBlendAttachments  = {{}};
	float                                  blendConstants[4]      = {0, 0, 0, 0};
	RenderTargetSignature                  targetSignature;
	std::vector<IRHIDescriptorSetLayout *> descriptorSetLayouts;
	std::vector<PushConstantRange>         pushConstantRanges;
};

struct ComputePipelineDesc
{
	IRHIShader                            *computeShader;
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

struct ColorAttachment
{
	IRHITextureView *view          = nullptr;
	IRHITextureView *resolveTarget = nullptr;
	LoadOp           loadOp        = LoadOp::CLEAR;
	StoreOp          storeOp       = StoreOp::STORE;
	ResolveMode      resolveMode   = ResolveMode::NONE;
	ClearValue       clearValue    = {{0.0f, 0.0f, 0.0f, 1.0f}};
};

struct DepthStencilAttachment
{
	IRHITextureView *view           = nullptr;
	LoadOp           depthLoadOp    = LoadOp::CLEAR;
	StoreOp          depthStoreOp   = StoreOp::STORE;
	LoadOp           stencilLoadOp  = LoadOp::DONT_CARE;
	StoreOp          stencilStoreOp = StoreOp::DONT_CARE;
	ClearValue       clearValue     = {};
	bool             readOnly       = false;
};

struct RenderingInfo
{
	uint32_t                     renderAreaX      = 0;
	uint32_t                     renderAreaY      = 0;
	uint32_t                     renderAreaWidth  = 0;
	uint32_t                     renderAreaHeight = 0;
	uint32_t                     layerCount       = 1;
	std::vector<ColorAttachment> colorAttachments;
	DepthStencilAttachment       depthStencilAttachment;
};

struct TextureTransition
{
	IRHITexture  *texture = nullptr;
	ResourceState before  = ResourceState::Undefined;
	ResourceState after   = ResourceState::GeneralRead;

	// Subresource range (optional, defaults to entire resource)
	uint32_t baseMipLevel    = 0;
	uint32_t mipLevelCount   = ~0u;        // All remaining mips
	uint32_t baseArrayLayer  = 0;
	uint32_t arrayLayerCount = ~0u;        // All remaining layers

	// Optional stage and access flag
	StageMask  src_stages = StageMask::Auto;
	AccessMask src_access = AccessMask::Auto;
	StageMask  dst_stages = StageMask::Auto;
	AccessMask dst_access = AccessMask::Auto;
};

struct BufferTransition
{
	IRHIBuffer   *buffer = nullptr;
	ResourceState before = ResourceState::Undefined;
	ResourceState after  = ResourceState::GeneralRead;

	// Buffer range (optional, defaults to entire buffer)
	size_t offset = 0;
	size_t size   = ~0ull;        // Entire buffer

	// Optional stage and access flag
	StageMask  src_stages = StageMask::Auto;
	AccessMask src_access = AccessMask::Auto;
	StageMask  dst_stages = StageMask::Auto;
	AccessMask dst_access = AccessMask::Auto;
};

struct MemoryBarrier
{
	StageMask  src_stages = StageMask::Auto;
	AccessMask src_access = AccessMask::Auto;
	StageMask  dst_stages = StageMask::Auto;
	AccessMask dst_access = AccessMask::Auto;
};

struct DrawIndexedIndirectCommand
{
	uint32_t indexCount;
	uint32_t instanceCount;
	uint32_t firstIndex;
	int32_t  vertexOffset;
	uint32_t firstInstance;
};

struct DispatchIndirectCommand
{
	uint32_t x;
	uint32_t y;
	uint32_t z;
};

struct SemaphoreWaitInfo
{
	IRHISemaphore *semaphore;
	StageMask      waitStage;
};

struct SubmitInfo
{
	std::span<const SemaphoreWaitInfo> waitSemaphores;
	std::span<IRHISemaphore *const>    signalSemaphores;
	IRHIFence                         *signalFence = nullptr;
};

}        // namespace rhi