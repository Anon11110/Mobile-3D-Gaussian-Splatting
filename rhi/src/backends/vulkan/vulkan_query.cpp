#include "vulkan_backend.h"

#include <stdexcept>

namespace rhi::vulkan
{

VulkanQueryPool::VulkanQueryPool(VkDevice device, const QueryPoolDesc &desc) :
    device(device),
    queryPool(VK_NULL_HANDLE),
    queryType(desc.queryType),
    queryCount(desc.queryCount),
    statisticsFlags(desc.statisticsFlags)
{
	VkQueryPoolCreateInfo createInfo{};
	createInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType  = QueryTypeToVulkan(desc.queryType);
	createInfo.queryCount = desc.queryCount;

	if (desc.queryType == QueryType::PIPELINE_STATISTICS)
	{
		createInfo.pipelineStatistics = PipelineStatisticFlagsToVulkan(desc.statisticsFlags);
	}

	if (vkCreateQueryPool(device, &createInfo, nullptr, &queryPool) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create Vulkan query pool");
	}
}

VulkanQueryPool::~VulkanQueryPool()
{
	if (queryPool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(device, queryPool, nullptr);
	}
}

VulkanQueryPool::VulkanQueryPool(VulkanQueryPool &&other) noexcept :
    device(other.device),
    queryPool(other.queryPool),
    queryType(other.queryType),
    queryCount(other.queryCount),
    statisticsFlags(other.statisticsFlags)
{
	other.queryPool = VK_NULL_HANDLE;
}

VulkanQueryPool &VulkanQueryPool::operator=(VulkanQueryPool &&other) noexcept
{
	if (this != &other)
	{
		if (queryPool != VK_NULL_HANDLE)
		{
			vkDestroyQueryPool(device, queryPool, nullptr);
		}

		device          = other.device;
		queryPool       = other.queryPool;
		queryType       = other.queryType;
		queryCount      = other.queryCount;
		statisticsFlags = other.statisticsFlags;

		other.queryPool = VK_NULL_HANDLE;
	}
	return *this;
}

// Utility conversion functions
VkQueryType QueryTypeToVulkan(QueryType type)
{
	switch (type)
	{
		case QueryType::TIMESTAMP:
			return VK_QUERY_TYPE_TIMESTAMP;
		case QueryType::PIPELINE_STATISTICS:
			return VK_QUERY_TYPE_PIPELINE_STATISTICS;
		case QueryType::OCCLUSION:
			return VK_QUERY_TYPE_OCCLUSION;
		default:
			return VK_QUERY_TYPE_TIMESTAMP;
	}
}

VkQueryPipelineStatisticFlags PipelineStatisticFlagsToVulkan(PipelineStatisticFlags flags)
{
	VkQueryPipelineStatisticFlags vkFlags = 0;
	uint32_t                      f       = static_cast<uint32_t>(flags);

	if (f & static_cast<uint32_t>(PipelineStatisticFlags::INPUT_ASSEMBLY_VERTICES))
		vkFlags |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
	if (f & static_cast<uint32_t>(PipelineStatisticFlags::INPUT_ASSEMBLY_PRIMITIVES))
		vkFlags |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT;
	if (f & static_cast<uint32_t>(PipelineStatisticFlags::VERTEX_SHADER_INVOCATIONS))
		vkFlags |= VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT;
	if (f & static_cast<uint32_t>(PipelineStatisticFlags::CLIPPING_INVOCATIONS))
		vkFlags |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
	if (f & static_cast<uint32_t>(PipelineStatisticFlags::CLIPPING_PRIMITIVES))
		vkFlags |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT;
	if (f & static_cast<uint32_t>(PipelineStatisticFlags::FRAGMENT_SHADER_INVOCATIONS))
		vkFlags |= VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
	if (f & static_cast<uint32_t>(PipelineStatisticFlags::COMPUTE_SHADER_INVOCATIONS))
		vkFlags |= VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

	return vkFlags;
}

VkQueryResultFlags QueryResultFlagsToVulkan(QueryResultFlags flags)
{
	VkQueryResultFlags vkFlags = 0;
	uint32_t           f       = static_cast<uint32_t>(flags);

	if (f & static_cast<uint32_t>(QueryResultFlags::WAIT))
		vkFlags |= VK_QUERY_RESULT_WAIT_BIT;
	if (f & static_cast<uint32_t>(QueryResultFlags::WITH_AVAILABILITY))
		vkFlags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
	if (f & static_cast<uint32_t>(QueryResultFlags::PARTIAL))
		vkFlags |= VK_QUERY_RESULT_PARTIAL_BIT;

	return vkFlags;
}

}        // namespace rhi::vulkan
