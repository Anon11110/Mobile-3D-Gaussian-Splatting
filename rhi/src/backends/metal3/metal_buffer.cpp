#include <cstring>
#include <stdexcept>

#include "metal_backend.h"

namespace rhi::metal3
{

MetalBuffer::MetalBuffer(const BufferDesc &desc) :
    storage_(desc.size), indexType_(desc.indexType)
{}

void *MetalBuffer::Map()
{
	isMapped_ = true;
	if (storage_.empty())
	{
		return nullptr;
	}
	return storage_.data();
}

void MetalBuffer::Unmap()
{
	isMapped_ = false;
}

size_t MetalBuffer::GetSize() const
{
	return storage_.size();
}

void MetalBuffer::Update(const void *data, size_t size, size_t offset)
{
	if (offset > storage_.size() || size > (storage_.size() - offset))
	{
		throw std::out_of_range("MetalBuffer::Update range exceeds buffer size");
	}
	if (size > 0 && data == nullptr)
	{
		throw std::invalid_argument("MetalBuffer::Update requires non-null data for non-zero size");
	}
	if (size == 0)
	{
		return;
	}

	std::byte *dst = storage_.data() + offset;
	std::memcpy(dst, data, size);
}

IndexType MetalBuffer::GetIndexType() const
{
	return indexType_;
}

}        // namespace rhi::metal3
