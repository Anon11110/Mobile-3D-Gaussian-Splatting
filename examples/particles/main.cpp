#include "core/log.h"
#include "msplat/app/device_manager.h"
#include "particles_app.h"

using namespace msplat;

int main()
{
	LOG_INFO("Starting Particles Example");

	// Create the application
	ParticlesApp app;

	// Create device manager and run the application
	app::DeviceManager deviceManager(&app);

	// Run the application (this starts the main loop)
	int result = deviceManager.Run(1200, 800, "Particle Simulation");

	if (result != 0)
	{
		LOG_ERROR("Application failed with error code: {}", result);
		return result;
	}

	LOG_INFO("Application exited successfully");
	return 0;
}