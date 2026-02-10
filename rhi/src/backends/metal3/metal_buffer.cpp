#include <cstring>
#include <stdexcept>

#include "metal_backend.h"

namespace rhi::metal3
{

MetalBuffer::MetalBuffer(MTL::Buffer *buffer, size_t size, IndexType indexType, MTL::StorageMode storageMode) :
    buffer_(buffer), size_(size), indexType_(indexType), storageMode_(storageMode)
{
	if (buffer_ == nullptr)
	{
		throw std::invalid_argument("MetalBuffer requires a valid MTL::Buffer");
	}
}

MetalBuffer::~MetalBuffer()
{
	if (buffer_ != nullptr)
	{
		buffer_->release();
		buffer_ = nullptr;
	}
}

void *MetalBuffer::Map()
{
	if (!IsCpuVisible())
	{
		throw std::logic_error("Cannot map a private Metal buffer");
	}

	isMapped_ = true;
	return buffer_->contents();
}

void MetalBuffer::Unmap()
{
	if (!isMapped_)
	{
		return;
	}

	if (storageMode_ == MTL::StorageModeManaged)
	{
		buffer_->didModifyRange(NS::Range(0, size_));
	}

	isMapped_ = false;
}

size_t MetalBuffer::GetSize() const
{
	return size_;
}

void MetalBuffer::Update(const void *data, size_t size, size_t offset)
{
	if (!IsCpuVisible())
	{
		throw std::logic_error("Update requires CPU-visible Metal buffer (shared/managed)");
	}
	if (offset > size_ || size > (size_ - offset))
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

	std::memcpy(static_cast<std::byte *>(buffer_->contents()) + offset, data, size);
	if (storageMode_ == MTL::StorageModeManaged)
	{
		buffer_->didModifyRange(NS::Range(offset, size));
	}
}

MTL::Buffer *MetalBuffer::GetHandle() const
{
	return buffer_;
}

IndexType MetalBuffer::GetIndexType() const
{
	return indexType_;
}

MTL::StorageMode MetalBuffer::GetStorageMode() const
{
	return storageMode_;
}

bool MetalBuffer::IsCpuVisible() const
{
	return storageMode_ == MTL::StorageModeShared || storageMode_ == MTL::StorageModeManaged;
}

}        // namespace rhi::metal3
