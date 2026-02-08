#include "metal_backend.h"

namespace rhi::metal3
{

MetalQueryPool::MetalQueryPool(const QueryPoolDesc &desc) :
    queryType_(desc.queryType), queryCount_(desc.queryCount)
{}

QueryType MetalQueryPool::GetQueryType() const
{
	return queryType_;
}

uint32_t MetalQueryPool::GetQueryCount() const
{
	return queryCount_;
}

}        // namespace rhi::metal3
