#include "core/log.h"
#include "hybrid_splat_renderer_app.h"
#include "msplat/app/device_manager.h"

using namespace msplat;

int main()
{
	LOG_INFO("Starting Hybrid Splat Renderer (CPU + GPU Sorting)");

	HybridSplatRendererApp app;
	app::DeviceManager     deviceManager(&app);

	int result = deviceManager.Run(1920, 1080, "Hybrid Splat Renderer (CPU + GPU)");

	if (result != 0)
	{
		LOG_ERROR("Application failed with error code: {}", result);
		return result;
	}

	LOG_INFO("Hybrid Splat Renderer exited successfully");
	return 0;
}
