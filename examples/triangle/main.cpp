#include <GLFW/glfw3.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "rhi.h"

struct Vertex {
    float position[3];
    float color[3];
};

std::vector<uint8_t> LoadShaderCode(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }

    size_t fileSize = file.tellg();
    std::vector<uint8_t> code(fileSize);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), fileSize);
    return code;
}

int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Create window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Simple Triangle", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }

    try {
        // Create RHI device
        auto device = RHI::CreateRHIDevice();

        // Create swapchain
        RHI::SwapchainDesc swapchainDesc{};
        swapchainDesc.windowHandle = window;
        swapchainDesc.width = 800;
        swapchainDesc.height = 600;
        swapchainDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
        auto swapchain = device->CreateSwapchain(swapchainDesc);
        // Create vertex buffer
        Vertex vertices[] = {{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                             {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                             {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}};

        RHI::BufferDesc vbDesc{};
        vbDesc.size = sizeof(vertices);
        vbDesc.usage = RHI::BufferUsage::VERTEX;
        vbDesc.memoryType = RHI::MemoryType::CPU_TO_GPU;
        vbDesc.initialData = vertices;
        auto vertexBuffer = device->CreateBuffer(vbDesc);

        // Load and create shaders
        auto vertexCode = LoadShaderCode("shaders/compiled/triangle.vert.spv");
        auto fragmentCode = LoadShaderCode("shaders/compiled/triangle.frag.spv");

        RHI::ShaderDesc vsDesc{};
        vsDesc.stage = RHI::ShaderStage::VERTEX;
        vsDesc.code = vertexCode.data();
        vsDesc.codeSize = vertexCode.size();
        auto vertexShader = device->CreateShader(vsDesc);

        RHI::ShaderDesc fsDesc{};
        fsDesc.stage = RHI::ShaderStage::FRAGMENT;
        fsDesc.code = fragmentCode.data();
        fsDesc.codeSize = fragmentCode.size();
        auto fragmentShader = device->CreateShader(fsDesc);

        // Create pipeline
        RHI::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.vertexShader = vertexShader.get();
        pipelineDesc.fragmentShader = fragmentShader.get();

        // Vertex layout
        pipelineDesc.vertexLayout.attributes = {
            {0, 0, RHI::TextureFormat::R32G32B32_FLOAT, 0},  // position
            {1, 0, RHI::TextureFormat::R32G32B32_FLOAT, 12}  // color
        };
        pipelineDesc.vertexLayout.bindings = {{0, sizeof(Vertex), false}};

        pipelineDesc.topology = RHI::PrimitiveTopology::TRIANGLE_LIST;
        pipelineDesc.colorFormat = swapchainDesc.format;
        auto pipeline = device->CreateGraphicsPipeline(pipelineDesc);

        // Create synchronization objects
        auto imageAvailableSemaphore = device->CreateSemaphore();
        auto renderFinishedSemaphore = device->CreateSemaphore();
        auto inFlightFence = device->CreateFence(true);

        // Main loop
        auto startTime = std::chrono::high_resolution_clock::now();
        uint32_t frameCount = 0;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            // Wait for previous frame
            inFlightFence->Wait();
            inFlightFence->Reset();

            // Acquire next image
            uint32_t imageIndex = swapchain->AcquireNextImage(imageAvailableSemaphore.get());

            // Record commands
            auto cmdList = device->CreateCommandList();
            cmdList->Begin();

            // Begin render pass
            RHI::RenderPassBeginInfo rpInfo{};
            rpInfo.colorTarget = swapchain->GetBackBuffer(imageIndex);
            rpInfo.width = 800;
            rpInfo.height = 600;
            rpInfo.clearColor = {{0.1f, 0.1f, 0.1f, 1.0f}};
            rpInfo.shouldClearColor = true;
            cmdList->BeginRenderPass(rpInfo);

            // Set viewport and scissor
            cmdList->SetViewport(0, 0, 800, 600);
            cmdList->SetScissor(0, 0, 800, 600);

            // Draw triangle
            cmdList->SetPipeline(pipeline.get());
            cmdList->SetVertexBuffer(0, vertexBuffer.get());
            cmdList->Draw(3);

            cmdList->EndRenderPass();
            cmdList->End();

            // Submit
            RHI::IRHICommandList* cmdListPtr = cmdList.get();
            device->SubmitCommandLists(&cmdListPtr, 1, imageAvailableSemaphore.get(), renderFinishedSemaphore.get(),
                                       inFlightFence.get());

            // Present
            swapchain->Present(imageIndex, renderFinishedSemaphore.get());

            // FPS counter
            frameCount++;
            auto currentTime = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(currentTime - startTime).count();
            if (elapsed >= 1.0f) {
                std::cout << "FPS: " << frameCount << std::endl;
                frameCount = 0;
                startTime = currentTime;
            }
        }

        // Wait for GPU to finish
        device->WaitIdle();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Cleanup
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}