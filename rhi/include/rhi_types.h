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
	FILL
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

enum class SwapchainStatus
{
	SUCCESS,
	OUT_OF_DATE,
	SUBOPTIMAL,
	ERROR
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

enum class TextureAspect : uint32_t
{
	COLOR   = 1 << 0,
	DEPTH   = 1 << 1,
	STENCIL = 1 << 2
};

// Bitwise operators for TextureAspect flags
inline TextureAspect operator|(TextureAspect lhs, TextureAspect rhs)
{
	return static_cast<TextureAspect>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline TextureAspect operator&(TextureAspect lhs, TextureAspect rhs)
{
	return static_cast<TextureAspect>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

inline TextureAspect operator~(TextureAspect aspect)
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

inline bool operator!(TextureAspect aspect)
{
	return static_cast<uint32_t>(aspect) == 0;
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
	DescriptorType type    = DescriptorType::COMBINED_IMAGE_SAMPLER;
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

struct DrawIndexedIndirectCommand
{
	uint32_t indexCount;
	uint32_t instanceCount;
	uint32_t firstIndex;
	int32_t  vertexOffset;
	uint32_t firstInstance;
};

}        // namespace RHI