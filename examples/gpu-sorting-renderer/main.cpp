#include "core/log.h"
#include "gpu_sorting_renderer_app.h"
#include "msplat/app/device_manager.h"

using namespace msplat;

int main()
{
	LOG_INFO("Starting GPU Sorting Renderer Example");

	GpuSortingRendererApp app;
	app::DeviceManager    deviceManager(&app);

	int result = deviceManager.Run(1920, 1080, "GPU Sorting Renderer Example");

	if (result != 0)
	{
		LOG_ERROR("Application failed with error code: {}", result);
		return result;
	}

	LOG_INFO("Application exited successfully");
	return 0;
}