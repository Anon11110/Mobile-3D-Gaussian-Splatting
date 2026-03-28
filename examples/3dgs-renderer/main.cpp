#include "3dgs_renderer_app.h"
#include "core/log.h"
#include "msplat/app/device_manager.h"

using namespace msplat;

int main()
{
	LOG_INFO("Starting 3DGS Renderer (CPU + GPU Sorting)");

	GaussianSplatRendererApp app;
	app::DeviceManager       deviceManager(&app);

	int result = deviceManager.Run(1920, 1080, "3DGS Renderer (CPU + GPU)");

	if (result != 0)
	{
		LOG_ERROR("Application failed with error code: {}", result);
		return result;
	}

	LOG_INFO("3DGS Renderer exited successfully");
	return 0;
}
