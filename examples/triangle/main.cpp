#include "core/log.h"
#include "msplat/app/device_manager.h"
#include "triangle_app.h"

using namespace msplat;

int main()
{
	LOG_INFO("Starting Triangle Example");

	// Create the application
	TriangleApp app;

	// Create device manager and run the application
	app::DeviceManager deviceManager(&app);

	// Run the application (this starts the main loop)
	int result = deviceManager.run(800, 600, "Triangle Example");

	if (result != 0)
	{
		LOG_ERROR("Application failed with error code: {}", result);
		return result;
	}

	LOG_INFO("Application exited successfully");
	return 0;
}