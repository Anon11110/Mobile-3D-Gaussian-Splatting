#pragma once

#include "app/application.h"
#include "app/camera.h"
#include "core/containers/memory.h"
#include "core/containers/vector.h"
#include "core/math/math.h"
#include "core/timer.h"
#include <cstdlib>
#include <msplat/engine/shader_factory.h>

namespace rhi
{
class IRHIBuffer;
class IRHIShader;
class IRHIDescriptorSetLayout;
class IRHIDescriptorSet;
class IRHIPipeline;
class IRHISemaphore;
class IRHIFence;
class IRHICommandList;
}        // namespace rhi

namespace msplat
{
namespace app
{
class DeviceManager;
}        // namespace app

namespace engine
{
class Scene;
class GpuSplatSorter;
}        // namespace engine
}        // namespace msplat

using namespace msplat;

class GpuSortingRendererApp : public app::IApplication
{
  public:
	GpuSortingRendererApp();
	~GpuSortingRendererApp() override;

	GpuSortingRendererApp(GpuSortingRendererApp &&) noexcept;
	GpuSortingRendererApp &operator=(GpuSortingRendererApp &&) noexcept;
	GpuSortingRendererApp(const GpuSortingRendererApp &)            = delete;
	GpuSortingRendererApp &operator=(const GpuSortingRendererApp &) = delete;

	bool OnInit(app::DeviceManager *deviceManager) override;
	void OnUpdate(float deltaTime) override;
	void OnRender() override;
	void OnShutdown() override;
	void OnKey(int key, int action, int mods) override;
	void OnMouseButton(int button, int action, int mods) override;
	void OnMouseMove(double xpos, double ypos) override;

  private:
	void LoadSplatFile(const char *filepath);
	void CreateTestSplatData();

	app::DeviceManager *deviceManager = nullptr;

	app::Camera camera;

	container::unique_ptr<engine::Scene>          scene;
	container::unique_ptr<engine::GpuSplatSorter> sorter;

	container::unique_ptr<engine::ShaderFactory> shaderFactory;

	container::vector<rhi::SemaphoreHandle> imageAvailableSemaphores;
	container::vector<rhi::SemaphoreHandle> renderFinishedSemaphores;
	rhi::FenceHandle                        inFlightFence;

	container::vector<rhi::CommandListHandle> commandLists;

	timer::Timer      applicationTimer;
	timer::FPSCounter fpsCounter;
	bool              sortingEnabled           = true;
	bool              verifyNextSort           = false;
	bool              checkVerificationResults = false;
	bool              useSimpleVerification    = true;
	uint32_t          frameCount               = 0;

	container::vector<math::vec3> testSplatPositions;
};