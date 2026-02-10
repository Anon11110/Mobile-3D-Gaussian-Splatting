#pragma once

#include <Metal/Metal.hpp>

#include "../../../include/rhi/rhi_types.h"

namespace rhi::metal3
{

MTL::StorageMode     SelectStorageMode(ResourceUsage usage, const AllocationHints &hints, bool unifiedMemory);
MTL::ResourceOptions MakeResourceOptions(ResourceUsage usage, const AllocationHints &hints, bool unifiedMemory);

MTL::TextureUsage MakeTextureUsage(const TextureDesc &desc);
MTL::TextureType  TextureTypeToMetal(TextureType type, bool isCubeMap);
TextureType       TextureTypeFromMetal(MTL::TextureType type);

MTL::PixelFormat TextureFormatToMetal(TextureFormat format);
TextureFormat    TextureFormatFromMetal(MTL::PixelFormat format);
bool             IsDepthFormat(TextureFormat format);
bool             HasStencil(TextureFormat format);

MTL::VertexFormat VertexFormatToMetal(VertexFormat format);

MTL::PrimitiveType          PrimitiveTopologyToMetal(PrimitiveTopology topology);
MTL::PrimitiveTopologyClass PrimitiveTopologyClassToMetal(PrimitiveTopology topology);
MTL::CullMode               CullModeToMetal(CullMode cullMode);
MTL::Winding                FrontFaceToMetal(FrontFace frontFace);
MTL::TriangleFillMode       PolygonModeToMetal(PolygonMode polygonMode);

MTL::CompareFunction  CompareOpToMetal(CompareOp op);
MTL::StencilOperation StencilOpToMetal(StencilOp op);

MTL::BlendFactor    BlendFactorToMetal(BlendFactor factor);
MTL::BlendOperation BlendOpToMetal(BlendOp op);
MTL::ColorWriteMask ColorWriteMaskToMetal(uint32_t mask);

MTL::SamplerMinMagFilter FilterModeToMetal(FilterMode filter);
MTL::SamplerMipFilter    MipmapModeToMetal(MipmapMode mode);
MTL::SamplerAddressMode  SamplerAddressModeToMetal(SamplerAddressMode mode);
MTL::SamplerBorderColor  BorderColorToMetal(BorderColor color);

MTL::LoadAction  LoadOpToMetal(LoadOp op);
MTL::StoreAction StoreOpToMetal(StoreOp op);
MTL::IndexType   IndexTypeToMetal(IndexType type);

NS::UInteger SampleCountToUInt(SampleCount count);

bool HasShaderStage(ShaderStageFlags flags, ShaderStageFlags stage);

}        // namespace rhi::metal3
