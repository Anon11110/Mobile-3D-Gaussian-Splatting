#include "core/log.h"
#include "msplat/app/device_manager.h"
#include "naive_splat_cpu_app.h"

using namespace msplat;

int main()
{
	LOG_INFO("Starting Naive Splat CPU Sort Example");

	// Create the application
	NaiveSplatCpuApp app;

	// Create device manager and run the application
	app::DeviceManager deviceManager(&app);

	// Run the application (this starts the main loop)
	int result = deviceManager.Run(1200, 800, "Naive Gaussian Splatting (CPU Sort)");

	if (result != 0)
	{
		LOG_ERROR("Application failed with error code: {}", result);
		return result;
	}

	LOG_INFO("Application exited successfully");
	return 0;
}