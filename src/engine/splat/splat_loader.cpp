#include <msplat/core/containers/filesystem.h>
#include <msplat/core/containers/queue.h>
#include <msplat/core/containers/string.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>
#include <msplat/engine/splat/splat_loader.h>

#include <miniply.h>

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace msplat::engine
{

struct LoadTask
{
	container::filesystem::path                   path;
	std::promise<container::shared_ptr<SplatSoA>> promise;
};

class SplatLoaderException : public std::runtime_error
{
  public:
	explicit SplatLoaderException(const container::string &message) :
	    std::runtime_error(container::to_std_string(message))
	{}
};

class SplatLoader::Impl
{
  private:
	container::queue<LoadTask> taskQueue;
#if !defined(MSPLAT_USE_SYSTEM_STL) && !defined(__ANDROID__)
	std::pmr::memory_resource *memoryResource;
#endif

	std::thread             workerThread;
	std::atomic<bool>       shouldStop{false};
	std::mutex              queueMutex;
	std::condition_variable queueCV;

	void WorkerLoop()
	{
		while (!shouldStop.load())
		{
			LoadTask task;

			{
				std::unique_lock<std::mutex> lock(queueMutex);
				queueCV.wait(lock, [this] {
					return !taskQueue.empty() || shouldStop.load();
				});

				if (shouldStop.load() && taskQueue.empty())
				{
					break;
				}

				if (!taskQueue.empty())
				{
					task = std::move(taskQueue.front());
					taskQueue.pop();
				}
			}

			try
			{
				auto data = InnerLoad(task.path);
				task.promise.set_value(data);
			}
			catch (...)
			{
				task.promise.set_exception(std::current_exception());
			}
		}
	}

	container::shared_ptr<SplatSoA> InnerLoad(const container::filesystem::path &path)
	{
		auto               pathStr = container::to_pmr_string(path.string());
		miniply::PLYReader reader(pathStr.c_str());

		if (!reader.valid())
		{
			throw SplatLoaderException(container::string("Failed to open PLY file: ") + pathStr);
		}

		// Find vertex element
		uint32_t vertexElementIdx = reader.find_element("vertex");
		if (vertexElementIdx == miniply::kInvalidIndex)
		{
			throw SplatLoaderException("PLY file does not contain vertex element");
		}

		miniply::PLYElement *vertexElement = reader.get_element(vertexElementIdx);
		if (!vertexElement)
		{
			throw SplatLoaderException("Failed to get vertex element");
		}

		uint32_t numSplats = vertexElement->count;
		if (numSplats == 0)
		{
			throw SplatLoaderException("PLY file contains no vertices");
		}

		// Detect SH coefficients
		uint32_t shCoeffsPerSplat = 0;
		if (vertexElement->find_property("f_rest_44") != miniply::kInvalidIndex)
		{
			shCoeffsPerSplat = 45;
		}
		else if (vertexElement->find_property("f_rest_14") != miniply::kInvalidIndex)
		{
			shCoeffsPerSplat = 15;
		}
		else if (vertexElement->find_property("f_rest_2") != miniply::kInvalidIndex)
		{
			shCoeffsPerSplat = 3;
		}
		else
		{
			shCoeffsPerSplat = 0;
		}

#if defined(MSPLAT_USE_SYSTEM_STL) || defined(__ANDROID__)
		auto data = std::make_shared<SplatSoA>();
#else
		auto data = std::make_shared<SplatSoA>(memoryResource);
#endif
		data->Resize(numSplats, shCoeffsPerSplat);

		// Move to vertex element and load it
		while (reader.has_element())
		{
			if (reader.element_is("vertex"))
			{
				if (!reader.load_element())
				{
					throw SplatLoaderException("Failed to load vertex element");
				}
				break;
			}
			reader.next_element();
		}

		// Find property indices
		uint32_t posIndices[3];
		uint32_t scaleIndices[3];
		uint32_t rotIndices[4];
		uint32_t opacityIndex;
		uint32_t dcIndices[3];

		if (!reader.find_properties(posIndices, 3, "x", "y", "z"))
		{
			throw SplatLoaderException("Failed to find position properties");
		}

		if (!reader.find_properties(scaleIndices, 3, "scale_0", "scale_1", "scale_2"))
		{
			throw SplatLoaderException("Failed to find scale properties");
		}

		if (!reader.find_properties(rotIndices, 4, "rot_0", "rot_1", "rot_2", "rot_3"))
		{
			throw SplatLoaderException("Failed to find rotation properties");
		}

		opacityIndex = reader.find_property("opacity");
		if (opacityIndex == miniply::kInvalidIndex)
		{
			throw SplatLoaderException("Failed to find opacity property");
		}

		if (!reader.find_properties(dcIndices, 3, "f_dc_0", "f_dc_1", "f_dc_2"))
		{
			throw SplatLoaderException("Failed to find DC coefficient properties");
		}

		bool loadSuccess = true;

		// Extract positions
		loadSuccess &= reader.extract_properties(&posIndices[0], 1, miniply::PLYPropertyType::Float, data->posX.data());
		loadSuccess &= reader.extract_properties(&posIndices[1], 1, miniply::PLYPropertyType::Float, data->posY.data());
		loadSuccess &= reader.extract_properties(&posIndices[2], 1, miniply::PLYPropertyType::Float, data->posZ.data());

		// Extract scales
		loadSuccess &= reader.extract_properties(&scaleIndices[0], 1, miniply::PLYPropertyType::Float, data->scaleX.data());
		loadSuccess &= reader.extract_properties(&scaleIndices[1], 1, miniply::PLYPropertyType::Float, data->scaleY.data());
		loadSuccess &= reader.extract_properties(&scaleIndices[2], 1, miniply::PLYPropertyType::Float, data->scaleZ.data());

		// Extract rotations
		loadSuccess &= reader.extract_properties(&rotIndices[0], 1, miniply::PLYPropertyType::Float, data->rotX.data());
		loadSuccess &= reader.extract_properties(&rotIndices[1], 1, miniply::PLYPropertyType::Float, data->rotY.data());
		loadSuccess &= reader.extract_properties(&rotIndices[2], 1, miniply::PLYPropertyType::Float, data->rotZ.data());
		loadSuccess &= reader.extract_properties(&rotIndices[3], 1, miniply::PLYPropertyType::Float, data->rotW.data());

		// Extract opacity
		loadSuccess &= reader.extract_properties(&opacityIndex, 1, miniply::PLYPropertyType::Float, data->opacity.data());

		// Extract DC coefficients
		loadSuccess &= reader.extract_properties(&dcIndices[0], 1, miniply::PLYPropertyType::Float, data->fDc0.data());
		loadSuccess &= reader.extract_properties(&dcIndices[1], 1, miniply::PLYPropertyType::Float, data->fDc1.data());
		loadSuccess &= reader.extract_properties(&dcIndices[2], 1, miniply::PLYPropertyType::Float, data->fDc2.data());

		// Extract SH rest coefficients if present
		if (shCoeffsPerSplat > 0)
		{
			container::vector<uint32_t> restIndices;
			restIndices.resize(shCoeffsPerSplat);
			for (uint32_t i = 0; i < shCoeffsPerSplat; ++i)
			{
				container::string propName = container::string("f_rest_") + std::to_string(i).c_str();
				restIndices[i]             = reader.find_property(propName.c_str());
				if (restIndices[i] == miniply::kInvalidIndex)
				{
					throw SplatLoaderException(container::string("Failed to find property: ") + propName);
				}
			}

			// Extract all rest coefficients at once with stride
			for (uint32_t i = 0; i < shCoeffsPerSplat; ++i)
			{
				loadSuccess &= reader.extract_properties_with_stride(
				    &restIndices[i], 1,
				    miniply::PLYPropertyType::Float,
				    data->fRest.data() + i,
				    shCoeffsPerSplat * sizeof(float));
			}
		}

		if (!loadSuccess)
		{
			throw SplatLoaderException("Failed to extract all required properties from PLY file");
		}

		LOG_INFO("Loaded {} splats with SH degree {} from {}",
		         numSplats, data->shDegree, container::to_std_string(pathStr));

		return data;
	}

  public:
#if defined(MSPLAT_USE_SYSTEM_STL) || defined(__ANDROID__)
	explicit Impl()
	{
		// Start worker thread after all members are initialized
		workerThread = std::thread(&Impl::WorkerLoop, this);
	}
#else
	explicit Impl() :
	    memoryResource(container::pmr::GetUpstreamAllocator())
	{
		// Start worker thread after all members are initialized
		workerThread = std::thread(&Impl::WorkerLoop, this);
	}
#endif

	~Impl()
	{
		shouldStop.store(true);
		queueCV.notify_all();
		if (workerThread.joinable())
		{
			workerThread.join();
		}
	}

	std::future<container::shared_ptr<SplatSoA>> Load(const container::filesystem::path &path)
	{
		LoadTask task;
		task.path   = path;
		auto future = task.promise.get_future();

		{
			std::unique_lock<std::mutex> lock(queueMutex);
			taskQueue.push(std::move(task));
		}
		queueCV.notify_one();

		return future;
	}
};

SplatLoader::SplatLoader() :
    pimpl(container::make_unique<Impl>())
{
}

SplatLoader::~SplatLoader() = default;

std::future<container::shared_ptr<SplatSoA>> SplatLoader::Load(const container::filesystem::path &path)
{
	return pimpl->Load(path);
}

}        // namespace msplat::engine
