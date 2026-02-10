#include <stdexcept>

#include "metal_conversions.h"

namespace rhi::metal3
{

namespace
{

[[noreturn]] void ThrowUnsupported(const char *what)
{
	throw std::runtime_error(what);
}

}        // namespace

MTL::StorageMode SelectStorageMode(ResourceUsage usage, const AllocationHints &hints, bool unifiedMemory)
{
	if (hints.persistently_mapped)
	{
		return unifiedMemory ? MTL::StorageModeShared : MTL::StorageModeManaged;
	}

	switch (usage)
	{
		case ResourceUsage::DynamicUpload:
		case ResourceUsage::Readback:
			return unifiedMemory ? MTL::StorageModeShared : MTL::StorageModeManaged;
		case ResourceUsage::Static:
		case ResourceUsage::Transient:
			if (hints.prefer_device_local)
			{
				return MTL::StorageModePrivate;
			}
			return unifiedMemory ? MTL::StorageModeShared : MTL::StorageModeManaged;
	}

	return MTL::StorageModePrivate;
}

MTL::ResourceOptions MakeResourceOptions(ResourceUsage usage, const AllocationHints &hints, bool unifiedMemory)
{
	MTL::ResourceOptions options = MTL::ResourceOptionCPUCacheModeDefault;

	if (usage == ResourceUsage::DynamicUpload)
	{
		options = options | MTL::ResourceCPUCacheModeWriteCombined;
	}

	switch (SelectStorageMode(usage, hints, unifiedMemory))
	{
		case MTL::StorageModeShared:
			options = options | MTL::ResourceStorageModeShared;
			break;
		case MTL::StorageModeManaged:
			options = options | MTL::ResourceStorageModeManaged;
			break;
		case MTL::StorageModePrivate:
			options = options | MTL::ResourceStorageModePrivate;
			break;
		default:
			ThrowUnsupported("Unsupported Metal storage mode for buffer resource options");
	}

	return options;
}

MTL::TextureUsage MakeTextureUsage(const TextureDesc &desc)
{
	MTL::TextureUsage usage = MTL::TextureUsageShaderRead;

	if (desc.isStorageImage)
	{
		usage = usage | MTL::TextureUsageShaderWrite;
	}

	if (desc.isRenderTarget || desc.isDepthStencil)
	{
		usage = usage | MTL::TextureUsageRenderTarget;
	}

	usage = usage | MTL::TextureUsagePixelFormatView;
	return usage;
}

MTL::TextureType TextureTypeToMetal(TextureType type, bool isCubeMap)
{
	if (isCubeMap || type == TextureType::TEXTURE_CUBE)
	{
		return MTL::TextureTypeCube;
	}

	switch (type)
	{
		case TextureType::TEXTURE_2D:
			return MTL::TextureType2D;
		case TextureType::TEXTURE_2D_ARRAY:
			return MTL::TextureType2DArray;
		case TextureType::TEXTURE_CUBE:
			return MTL::TextureTypeCube;
		case TextureType::TEXTURE_3D:
			return MTL::TextureType3D;
	}

	return MTL::TextureType2D;
}

TextureType TextureTypeFromMetal(MTL::TextureType type)
{
	switch (type)
	{
		case MTL::TextureType2D:
			return TextureType::TEXTURE_2D;
		case MTL::TextureType2DArray:
			return TextureType::TEXTURE_2D_ARRAY;
		case MTL::TextureTypeCube:
		case MTL::TextureTypeCubeArray:
			return TextureType::TEXTURE_CUBE;
		case MTL::TextureType3D:
			return TextureType::TEXTURE_3D;
		default:
			return TextureType::TEXTURE_2D;
	}
}

MTL::PixelFormat TextureFormatToMetal(TextureFormat format)
{
	switch (format)
	{
		case TextureFormat::UNDEFINED:
			return MTL::PixelFormatInvalid;
		case TextureFormat::R8G8B8A8_UNORM:
			return MTL::PixelFormatRGBA8Unorm;
		case TextureFormat::R8G8B8A8_SRGB:
			return MTL::PixelFormatRGBA8Unorm_sRGB;
		case TextureFormat::B8G8R8A8_UNORM:
			return MTL::PixelFormatBGRA8Unorm;
		case TextureFormat::B8G8R8A8_SRGB:
			return MTL::PixelFormatBGRA8Unorm_sRGB;
		case TextureFormat::R32G32B32_FLOAT:
			return MTL::PixelFormatInvalid;
		case TextureFormat::D32_FLOAT:
			return MTL::PixelFormatDepth32Float;
		case TextureFormat::D24_UNORM_S8_UINT:
			return MTL::PixelFormatDepth24Unorm_Stencil8;
		case TextureFormat::R8_UNORM:
			return MTL::PixelFormatR8Unorm;
		case TextureFormat::R16_FLOAT:
			return MTL::PixelFormatR16Float;
		case TextureFormat::R32_FLOAT:
			return MTL::PixelFormatR32Float;
		case TextureFormat::RG8_UNORM:
			return MTL::PixelFormatRG8Unorm;
		case TextureFormat::RG16_FLOAT:
			return MTL::PixelFormatRG16Float;
		case TextureFormat::RG32_FLOAT:
			return MTL::PixelFormatRG32Float;
		case TextureFormat::RGBA16_FLOAT:
			return MTL::PixelFormatRGBA16Float;
		case TextureFormat::RGBA32_FLOAT:
			return MTL::PixelFormatRGBA32Float;
		case TextureFormat::R11G11B10_FLOAT:
			return MTL::PixelFormatRG11B10Float;
		case TextureFormat::ASTC_4x4_UNORM:
			return MTL::PixelFormatASTC_4x4_LDR;
		case TextureFormat::ASTC_4x4_SRGB:
			return MTL::PixelFormatASTC_4x4_sRGB;
		case TextureFormat::ASTC_6x6_UNORM:
			return MTL::PixelFormatASTC_6x6_LDR;
		case TextureFormat::ASTC_6x6_SRGB:
			return MTL::PixelFormatASTC_6x6_sRGB;
		case TextureFormat::ETC2_RGB8_UNORM:
			return MTL::PixelFormatETC2_RGB8;
		case TextureFormat::ETC2_RGB8_SRGB:
			return MTL::PixelFormatETC2_RGB8_sRGB;
		case TextureFormat::ETC2_RGBA8_UNORM:
			return MTL::PixelFormatETC2_RGB8A1;
		case TextureFormat::ETC2_RGBA8_SRGB:
			return MTL::PixelFormatETC2_RGB8A1_sRGB;
		case TextureFormat::BC1_RGB_UNORM:
			return MTL::PixelFormatBC1_RGBA;
		case TextureFormat::BC1_RGB_SRGB:
			return MTL::PixelFormatBC1_RGBA_sRGB;
		case TextureFormat::BC3_RGBA_UNORM:
			return MTL::PixelFormatBC3_RGBA;
		case TextureFormat::BC3_RGBA_SRGB:
			return MTL::PixelFormatBC3_RGBA_sRGB;
		case TextureFormat::BC5_RG_UNORM:
			return MTL::PixelFormatBC5_RGUnorm;
		case TextureFormat::BC7_RGBA_UNORM:
			return MTL::PixelFormatBC7_RGBAUnorm;
		case TextureFormat::BC7_RGBA_SRGB:
			return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
	}

	ThrowUnsupported("Unsupported RHI texture format for Metal backend");
}

TextureFormat TextureFormatFromMetal(MTL::PixelFormat format)
{
	switch (format)
	{
		case MTL::PixelFormatRGBA8Unorm:
			return TextureFormat::R8G8B8A8_UNORM;
		case MTL::PixelFormatRGBA8Unorm_sRGB:
			return TextureFormat::R8G8B8A8_SRGB;
		case MTL::PixelFormatBGRA8Unorm:
			return TextureFormat::B8G8R8A8_UNORM;
		case MTL::PixelFormatBGRA8Unorm_sRGB:
			return TextureFormat::B8G8R8A8_SRGB;
		case MTL::PixelFormatDepth32Float:
			return TextureFormat::D32_FLOAT;
		case MTL::PixelFormatDepth24Unorm_Stencil8:
			return TextureFormat::D24_UNORM_S8_UINT;
		case MTL::PixelFormatR8Unorm:
			return TextureFormat::R8_UNORM;
		case MTL::PixelFormatR16Float:
			return TextureFormat::R16_FLOAT;
		case MTL::PixelFormatR32Float:
			return TextureFormat::R32_FLOAT;
		case MTL::PixelFormatRG8Unorm:
			return TextureFormat::RG8_UNORM;
		case MTL::PixelFormatRG16Float:
			return TextureFormat::RG16_FLOAT;
		case MTL::PixelFormatRG32Float:
			return TextureFormat::RG32_FLOAT;
		case MTL::PixelFormatRGBA16Float:
			return TextureFormat::RGBA16_FLOAT;
		case MTL::PixelFormatRGBA32Float:
			return TextureFormat::RGBA32_FLOAT;
		case MTL::PixelFormatRG11B10Float:
			return TextureFormat::R11G11B10_FLOAT;
		default:
			return TextureFormat::UNDEFINED;
	}
}

bool IsDepthFormat(TextureFormat format)
{
	return format == TextureFormat::D32_FLOAT || format == TextureFormat::D24_UNORM_S8_UINT;
}

bool HasStencil(TextureFormat format)
{
	return format == TextureFormat::D24_UNORM_S8_UINT;
}

MTL::VertexFormat VertexFormatToMetal(VertexFormat format)
{
	switch (format)
	{
		case VertexFormat::R32_SFLOAT:
			return MTL::VertexFormatFloat;
		case VertexFormat::R32G32_SFLOAT:
			return MTL::VertexFormatFloat2;
		case VertexFormat::R32G32B32_SFLOAT:
			return MTL::VertexFormatFloat3;
		case VertexFormat::R32G32B32A32_SFLOAT:
			return MTL::VertexFormatFloat4;
		case VertexFormat::R16_SFLOAT:
			return MTL::VertexFormatHalf;
		case VertexFormat::R16G16_SFLOAT:
			return MTL::VertexFormatHalf2;
		case VertexFormat::R16G16B16_SFLOAT:
			return MTL::VertexFormatHalf3;
		case VertexFormat::R16G16B16A16_SFLOAT:
			return MTL::VertexFormatHalf4;
		case VertexFormat::R8_UNORM:
			return MTL::VertexFormatUCharNormalized;
		case VertexFormat::R8G8_UNORM:
			return MTL::VertexFormatUChar2Normalized;
		case VertexFormat::R8G8B8_UNORM:
			return MTL::VertexFormatUChar3Normalized;
		case VertexFormat::R8G8B8A8_UNORM:
			return MTL::VertexFormatUChar4Normalized;
		case VertexFormat::R8_UINT:
			return MTL::VertexFormatUChar;
		case VertexFormat::R8G8_UINT:
			return MTL::VertexFormatUChar2;
		case VertexFormat::R8G8B8_UINT:
			return MTL::VertexFormatUChar3;
		case VertexFormat::R8G8B8A32_UINT:
			return MTL::VertexFormatUChar4;
		case VertexFormat::R16_UINT:
			return MTL::VertexFormatUShort;
		case VertexFormat::R16G16_UINT:
			return MTL::VertexFormatUShort2;
		case VertexFormat::R16G16B16_UINT:
			return MTL::VertexFormatUShort3;
		case VertexFormat::R16G16B16A16_UINT:
			return MTL::VertexFormatUShort4;
		case VertexFormat::R32_UINT:
			return MTL::VertexFormatUInt;
		case VertexFormat::R32G32_UINT:
			return MTL::VertexFormatUInt2;
		case VertexFormat::R32G32B32_UINT:
			return MTL::VertexFormatUInt3;
		case VertexFormat::R32G32B32A32_UINT:
			return MTL::VertexFormatUInt4;
		default:
			ThrowUnsupported("Unsupported vertex format for Metal backend");
	}
}

MTL::PrimitiveType PrimitiveTopologyToMetal(PrimitiveTopology topology)
{
	switch (topology)
	{
		case PrimitiveTopology::POINT_LIST:
			return MTL::PrimitiveTypePoint;
		case PrimitiveTopology::LINE_LIST:
			return MTL::PrimitiveTypeLine;
		case PrimitiveTopology::LINE_STRIP:
			return MTL::PrimitiveTypeLineStrip;
		case PrimitiveTopology::TRIANGLE_LIST:
			return MTL::PrimitiveTypeTriangle;
		case PrimitiveTopology::TRIANGLE_STRIP:
			return MTL::PrimitiveTypeTriangleStrip;
	}

	return MTL::PrimitiveTypeTriangle;
}

MTL::PrimitiveTopologyClass PrimitiveTopologyClassToMetal(PrimitiveTopology topology)
{
	switch (topology)
	{
		case PrimitiveTopology::POINT_LIST:
			return MTL::PrimitiveTopologyClassPoint;
		case PrimitiveTopology::LINE_LIST:
		case PrimitiveTopology::LINE_STRIP:
			return MTL::PrimitiveTopologyClassLine;
		case PrimitiveTopology::TRIANGLE_LIST:
		case PrimitiveTopology::TRIANGLE_STRIP:
			return MTL::PrimitiveTopologyClassTriangle;
	}

	return MTL::PrimitiveTopologyClassUnspecified;
}

MTL::CullMode CullModeToMetal(CullMode cullMode)
{
	switch (cullMode)
	{
		case CullMode::NONE:
			return MTL::CullModeNone;
		case CullMode::FRONT:
			return MTL::CullModeFront;
		case CullMode::BACK:
			return MTL::CullModeBack;
	}

	return MTL::CullModeNone;
}

MTL::Winding FrontFaceToMetal(FrontFace frontFace)
{
	// 1:1 mapping: RHI front-face enum maps directly to Metal winding.
	switch (frontFace)
	{
		case FrontFace::COUNTER_CLOCKWISE:
			return MTL::WindingCounterClockwise;
		case FrontFace::CLOCKWISE:
			return MTL::WindingClockwise;
	}

	return MTL::WindingCounterClockwise;
}

MTL::TriangleFillMode PolygonModeToMetal(PolygonMode polygonMode)
{
	switch (polygonMode)
	{
		case PolygonMode::FILL:
			return MTL::TriangleFillModeFill;
		case PolygonMode::LINE:
			return MTL::TriangleFillModeLines;
		case PolygonMode::POINT:
			ThrowUnsupported("Metal backend does not support PolygonMode::POINT");
	}

	return MTL::TriangleFillModeFill;
}

MTL::CompareFunction CompareOpToMetal(CompareOp op)
{
	switch (op)
	{
		case CompareOp::NEVER:
			return MTL::CompareFunctionNever;
		case CompareOp::LESS:
			return MTL::CompareFunctionLess;
		case CompareOp::EQUAL:
			return MTL::CompareFunctionEqual;
		case CompareOp::LESS_OR_EQUAL:
			return MTL::CompareFunctionLessEqual;
		case CompareOp::GREATER:
			return MTL::CompareFunctionGreater;
		case CompareOp::NOT_EQUAL:
			return MTL::CompareFunctionNotEqual;
		case CompareOp::GREATER_OR_EQUAL:
			return MTL::CompareFunctionGreaterEqual;
		case CompareOp::ALWAYS:
			return MTL::CompareFunctionAlways;
	}

	return MTL::CompareFunctionAlways;
}

MTL::StencilOperation StencilOpToMetal(StencilOp op)
{
	switch (op)
	{
		case StencilOp::KEEP:
			return MTL::StencilOperationKeep;
		case StencilOp::ZERO:
			return MTL::StencilOperationZero;
		case StencilOp::REPLACE:
			return MTL::StencilOperationReplace;
		case StencilOp::INCREMENT_AND_CLAMP:
			return MTL::StencilOperationIncrementClamp;
		case StencilOp::DECREMENT_AND_CLAMP:
			return MTL::StencilOperationDecrementClamp;
		case StencilOp::INVERT:
			return MTL::StencilOperationInvert;
		case StencilOp::INCREMENT_AND_WRAP:
			return MTL::StencilOperationIncrementWrap;
		case StencilOp::DECREMENT_AND_WRAP:
			return MTL::StencilOperationDecrementWrap;
	}

	return MTL::StencilOperationKeep;
}

MTL::BlendFactor BlendFactorToMetal(BlendFactor factor)
{
	switch (factor)
	{
		case BlendFactor::ZERO:
			return MTL::BlendFactorZero;
		case BlendFactor::ONE:
			return MTL::BlendFactorOne;
		case BlendFactor::SRC_COLOR:
			return MTL::BlendFactorSourceColor;
		case BlendFactor::ONE_MINUS_SRC_COLOR:
			return MTL::BlendFactorOneMinusSourceColor;
		case BlendFactor::DST_COLOR:
			return MTL::BlendFactorDestinationColor;
		case BlendFactor::ONE_MINUS_DST_COLOR:
			return MTL::BlendFactorOneMinusDestinationColor;
		case BlendFactor::SRC_ALPHA:
			return MTL::BlendFactorSourceAlpha;
		case BlendFactor::ONE_MINUS_SRC_ALPHA:
			return MTL::BlendFactorOneMinusSourceAlpha;
		case BlendFactor::DST_ALPHA:
			return MTL::BlendFactorDestinationAlpha;
		case BlendFactor::ONE_MINUS_DST_ALPHA:
			return MTL::BlendFactorOneMinusDestinationAlpha;
		case BlendFactor::CONSTANT_COLOR:
			return MTL::BlendFactorBlendColor;
		case BlendFactor::ONE_MINUS_CONSTANT_COLOR:
			return MTL::BlendFactorOneMinusBlendColor;
		case BlendFactor::CONSTANT_ALPHA:
			return MTL::BlendFactorBlendAlpha;
		case BlendFactor::ONE_MINUS_CONSTANT_ALPHA:
			return MTL::BlendFactorOneMinusBlendAlpha;
		case BlendFactor::SRC_ALPHA_SATURATE:
			return MTL::BlendFactorSourceAlphaSaturated;
	}

	return MTL::BlendFactorOne;
}

MTL::BlendOperation BlendOpToMetal(BlendOp op)
{
	switch (op)
	{
		case BlendOp::ADD:
			return MTL::BlendOperationAdd;
		case BlendOp::SUBTRACT:
			return MTL::BlendOperationSubtract;
		case BlendOp::REVERSE_SUBTRACT:
			return MTL::BlendOperationReverseSubtract;
		case BlendOp::MIN:
			return MTL::BlendOperationMin;
		case BlendOp::MAX:
			return MTL::BlendOperationMax;
	}

	return MTL::BlendOperationAdd;
}

MTL::ColorWriteMask ColorWriteMaskToMetal(uint32_t mask)
{
	MTL::ColorWriteMask result = MTL::ColorWriteMaskNone;
	if ((mask & 0x1u) != 0)
	{
		result = result | MTL::ColorWriteMaskRed;
	}
	if ((mask & 0x2u) != 0)
	{
		result = result | MTL::ColorWriteMaskGreen;
	}
	if ((mask & 0x4u) != 0)
	{
		result = result | MTL::ColorWriteMaskBlue;
	}
	if ((mask & 0x8u) != 0)
	{
		result = result | MTL::ColorWriteMaskAlpha;
	}
	return result;
}

MTL::SamplerMinMagFilter FilterModeToMetal(FilterMode filter)
{
	switch (filter)
	{
		case FilterMode::NEAREST:
			return MTL::SamplerMinMagFilterNearest;
		case FilterMode::LINEAR:
			return MTL::SamplerMinMagFilterLinear;
	}

	return MTL::SamplerMinMagFilterLinear;
}

MTL::SamplerMipFilter MipmapModeToMetal(MipmapMode mode)
{
	switch (mode)
	{
		case MipmapMode::NEAREST:
			return MTL::SamplerMipFilterNearest;
		case MipmapMode::LINEAR:
			return MTL::SamplerMipFilterLinear;
	}

	return MTL::SamplerMipFilterLinear;
}

MTL::SamplerAddressMode SamplerAddressModeToMetal(SamplerAddressMode mode)
{
	switch (mode)
	{
		case SamplerAddressMode::REPEAT:
			return MTL::SamplerAddressModeRepeat;
		case SamplerAddressMode::MIRRORED_REPEAT:
			return MTL::SamplerAddressModeMirrorRepeat;
		case SamplerAddressMode::CLAMP_TO_EDGE:
			return MTL::SamplerAddressModeClampToEdge;
		case SamplerAddressMode::CLAMP_TO_BORDER:
			return MTL::SamplerAddressModeClampToBorderColor;
		case SamplerAddressMode::MIRROR_CLAMP_TO_EDGE:
			return MTL::SamplerAddressModeMirrorClampToEdge;
	}

	return MTL::SamplerAddressModeClampToEdge;
}

MTL::SamplerBorderColor BorderColorToMetal(BorderColor color)
{
	switch (color)
	{
		case BorderColor::FLOAT_TRANSPARENT_BLACK:
		case BorderColor::INT_TRANSPARENT_BLACK:
			return MTL::SamplerBorderColorTransparentBlack;
		case BorderColor::FLOAT_OPAQUE_BLACK:
		case BorderColor::INT_OPAQUE_BLACK:
			return MTL::SamplerBorderColorOpaqueBlack;
		case BorderColor::FLOAT_OPAQUE_WHITE:
		case BorderColor::INT_OPAQUE_WHITE:
			return MTL::SamplerBorderColorOpaqueWhite;
	}

	return MTL::SamplerBorderColorTransparentBlack;
}

MTL::LoadAction LoadOpToMetal(LoadOp op)
{
	switch (op)
	{
		case LoadOp::LOAD:
			return MTL::LoadActionLoad;
		case LoadOp::CLEAR:
			return MTL::LoadActionClear;
		case LoadOp::DONT_CARE:
			return MTL::LoadActionDontCare;
	}

	return MTL::LoadActionDontCare;
}

MTL::StoreAction StoreOpToMetal(StoreOp op)
{
	switch (op)
	{
		case StoreOp::STORE:
			return MTL::StoreActionStore;
		case StoreOp::DONT_CARE:
			return MTL::StoreActionDontCare;
	}

	return MTL::StoreActionStore;
}

MTL::IndexType IndexTypeToMetal(IndexType type)
{
	switch (type)
	{
		case IndexType::UINT16:
			return MTL::IndexTypeUInt16;
		case IndexType::UINT32:
			return MTL::IndexTypeUInt32;
	}

	return MTL::IndexTypeUInt32;
}

NS::UInteger SampleCountToUInt(SampleCount count)
{
	return static_cast<NS::UInteger>(count);
}

bool HasShaderStage(ShaderStageFlags flags, ShaderStageFlags stage)
{
	return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(stage)) != 0;
}

}        // namespace rhi::metal3
