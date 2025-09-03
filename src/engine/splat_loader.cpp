#include <msplat/core/containers/memory.h>
#include <msplat/core/log.h>
#include <msplat/engine/splat_loader.h>

#include <miniply.h>

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace msplat::engine
{

struct LoadTask
{
	std::filesystem::path                   path;
	std::promise<std::shared_ptr<SplatSoA>> promise;
};

class SplatLoaderException : public std::runtime_error
{
  public:
	explicit SplatLoaderException(const std::string &message) :
	    std::runtime_error(message)
	{}
};

class SplatLoader::Impl
{
  private:
	std::thread                workerThread;
	std::atomic<bool>          shouldStop{false};
	std::queue<LoadTask>       taskQueue;
	std::mutex                 queueMutex;
	std::condition_variable    queueCV;
	std::pmr::memory_resource *memoryResource;

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

	std::shared_ptr<SplatSoA> InnerLoad(const std::filesystem::path &path)
	{
		miniply::PLYReader reader(path.string().c_str());

		if (!reader.valid())
		{
			throw SplatLoaderException("Failed to open PLY file: " + path.string());
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

		auto data = std::make_shared<SplatSoA>(memoryResource);
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
			std::vector<uint32_t> restIndices(shCoeffsPerSplat);
			for (uint32_t i = 0; i < shCoeffsPerSplat; ++i)
			{
				std::stringstream propName;
				propName << "f_rest_" << i;
				restIndices[i] = reader.find_property(propName.str().c_str());
				if (restIndices[i] == miniply::kInvalidIndex)
				{
					throw SplatLoaderException("Failed to find property: " + propName.str());
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
		         numSplats, data->shDegree, path.string());

		return data;
	}

  public:
	explicit Impl() :
	    memoryResource(msplat::container::pmr::GetUpstreamAllocator()),
	    workerThread(&Impl::WorkerLoop, this)
	{
	}

	~Impl()
	{
		shouldStop.store(true);
		queueCV.notify_all();
		if (workerThread.joinable())
		{
			workerThread.join();
		}
	}

	std::future<std::shared_ptr<SplatSoA>> Load(const std::filesystem::path &path)
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
    pimpl(std::make_unique<Impl>())
{
}

SplatLoader::~SplatLoader() = default;

std::future<std::shared_ptr<SplatSoA>> SplatLoader::Load(const std::filesystem::path &path)
{
	return pimpl->Load(path);
}

}        // namespace msplat::engine
