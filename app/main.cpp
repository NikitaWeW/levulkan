#include <bits/stdc++.h>
#include <signal.h>

#include "engine/renderer/ResourceManager.hpp"
#include "engine/scene/Animation.hpp"
#include "engine/dsa/ECS.hpp"
#include "cpptrace/from_current.hpp"
#include "engine/scene/Loaders.hpp"
#include "engine/vk/vk.hpp"

#include "engine/io/Logging.hpp"
#include "engine/renderer/Renderdoc.hpp"
#include "engine/renderer/RenderGraphRenderer.hpp"
#include "engine/renderer/RenderGraph.hpp"
#include "engine/scene/Resources.hpp"

#include "tracy/Tracy.hpp"

#include "Scene.hpp"

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

egn::Registry sReg;

////////////////////////////////////////////////////////////////

class VulkanInitializer {
private:
    egn::vk::Instance instance;
    egn::vk::Device device;
    VkSurfaceKHR surface = nullptr;
    bool success = false;
public:
    inline VulkanInitializer(GLFWwindow *window = nullptr) {
        if(!glfwVulkanSupported()) {
            LOG_ERROR("Vulkan is not supported!");
            return;
        }

        VkResult res = volkInitialize();
        if(res != VK_SUCCESS) {
            LOG_ERROR("Failed to init volk: {}!", string_VkResult(res));
            return;
        }

        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        egn::vk::InstanceCreateInfo instanceCI{
            .appName = "levulkan",
            .version = VK_API_VERSION_1_3,
            .instanceExtensions = std::vector<char const *>(glfwExtensions, glfwExtensions + glfwExtensionCount),
        };
        egn::vk::enableValidationLayers(instanceCI);
        instance = egn::vk::createInstance(instanceCI);
        if(!instance.valid) {
            LOG_ERROR("Failed to create vulkan instance!");
            return;
        }

        if(window) {
            VkResult res = glfwCreateWindowSurface(instance.instance, window, nullptr, &surface);
            if(res != VK_SUCCESS) {
                LOG_ERROR("Failed to create window surface!");
                return;
            }
        }

        egn::vk::DeviceCreateInfo deviceCI {
            .surface = surface,
            .deviceExtensions = { VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME },
            .queues = {VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_TRANSFER_BIT},
            .deviceFeatures = {
                .features = {
                    .geometryShader = true,
                    .shaderSampledImageArrayDynamicIndexing = true,
                },
                .vulkan12 = {
                    .descriptorIndexing = true,
                    .descriptorBindingPartiallyBound = true,
                    .descriptorBindingVariableDescriptorCount = true,
                    .runtimeDescriptorArray = true,
                    .bufferDeviceAddress = true,
                },
                .vulkan13 = {
                    .synchronization2 = true,
                    .dynamicRendering = true,
                },
                .vulkan14 = {
                },
            },
        };

        device = egn::vk::createDevice(instance, deviceCI);
        if(!device.valid) {
            LOG_ERROR("Failed to init vulkan!");
            return;
        }

        success = true;
    }

    inline bool Success() const {
        return success;
    }
    inline egn::vk::Instance const &Instance() const {
        return instance;
    }
    inline VkSurfaceKHR Surface() const {
        return surface;
    }
    inline egn::vk::Device const &Device() const {
        return device;
    }
};

static std::string getDateTime() {
    time_t timestamp = time(NULL);
    struct tm datetime = *localtime(&timestamp);
    std::string str = asctime(&datetime);
    str.pop_back(); // pop newline
    return str;
}
static egn::Entity makeWindow(egn::Registry &reg, std::string_view name) {
    auto eWindow = reg.create(egn::Window{ });
    eWindow.name("window_" + std::string(name));
    auto &window = eWindow.get<egn::Window>();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window.handle = glfwCreateWindow(800, 600, name.data(), nullptr, nullptr);
    glm::ivec2 size;
    glfwGetWindowSize(window.handle, &size.x, &size.y);
    glfwSwapInterval(1);
    if(glfwRawMouseMotionSupported())
        glfwSetInputMode(window.handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    egn::setCallbacks(eWindow);

    return eWindow;
}
static void makeWindowSwapchain(egn::RestrictedEntity<egn::Window> &window, VkDevice device, VkPhysicalDevice physicalDevice) {
    egn::Entity swapchain = window.reg().create(egn::vk::makeSwapchain({
            .alloc = {
                .device = device,
                .physicalDevice = physicalDevice,
                .surface = window->surface,
            },
            .size = {window->size.x, window->size.y},
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .registry = &sReg,
        }));
    swapchain.name("swapchain_" + std::string(window.name()));
    window->swapchain = swapchain;
}
static VkCommandPool createCommandPool(uint32_t index, VkDevice device) {
    VkCommandPoolCreateInfo commandPoolCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = index };
    VkCommandPool commandPool;
    vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool);
    return commandPool;
}

////////////////////////////////////////////////////////////////

int app([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {
    scene::IScene *scene = nullptr;

    /////////////////////////////////////////////////////////

    scene::TestScene testScene;
    scene = &testScene;

    /////////////////////////////////////////////////////////

    if(!glfwInit()) {
        LOG_ERROR("Failed to init glfw!");
        return -1;
    }

    egn::RestrictedEntity<egn::Window> window = makeWindow(sReg, "levulkan");
    window.emplace<scene::MainWindow>();
    VulkanInitializer vulkanInitializer(window->handle);
    if(!vulkanInitializer.Success()) {
        return -1;
    }

    window->surface = vulkanInitializer.Surface();

    auto &instance = vulkanInitializer.Instance().instance;
    auto &physicalDevice = vulkanInitializer.Device().physicalDevice;
    auto &queueFamilies = vulkanInitializer.Device().queueFamilies;
    auto &vma = vulkanInitializer.Device().vma;
    auto &device = vulkanInitializer.Device().device;

    egn::vk::AllocationCreateInfo const ALLOCATION_INFO{
        .device = device,
        .physicalDevice = physicalDevice,
        .allocator = vma,
        .sharingMode = queueFamilies.uniqueFamilies.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
    };
    LOG_TRACE("SHARING_MODE={}", string_VkSharingMode(ALLOCATION_INFO.sharingMode));

    makeWindowSwapchain(window, device, physicalDevice);
    auto swapchain = window->swapchain;
    swapchain.emplace<egn::vk::Instance>(vulkanInitializer.Instance());
    swapchain.emplace<egn::vk::Device>(vulkanInitializer.Device());

    scene->setup(sReg);

    VkCommandPool commandPool = createCommandPool(queueFamilies.indices.at(VK_QUEUE_GRAPHICS_BIT), device);

    VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

    ////////////////////////////////////////////////////////////////

    // TODO: A better test render pipeline that uses all of the render graph features
    // Multiple queues
    // Complex pass dependencies
    // Resource aliasing
    // History resources
    // Per frame in flight resource descriptors
    // Compute buffers
    // ---
    // Draw scene to file as proof of concept.
    egn::IRenderer *renderer = nullptr;
    scene->setupRenderer(sReg, &renderer, swapchain.get<egn::vk::Device>());

    ////////////////////////////////////////////////////////////////

    VkQueue presentQueue;
    assert(queueFamilies.presentQueue.has_value());
    vkGetDeviceQueue(device, queueFamilies.presentQueue.value(), 0, &presentQueue);

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> presentSemaphores;
    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkSemaphoreCreateInfo semaphoreCI{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .flags = 0 };
        vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]);
    }

    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> fences;
    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkFenceCreateInfo fenceCI{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        CHECK_VK_RES(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
    }

    std::vector<VkSemaphore> renderSemaphores;
    renderSemaphores.resize(swapchain->images.size());
    for(auto &semaphore : renderSemaphores) {
        VkSemaphoreCreateInfo semaphoreCI{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore);
    }

    std::array<std::unordered_map<uint32_t, VkCommandBuffer>, MAX_FRAMES_IN_FLIGHT> commandBuffers;
    std::unordered_set<uint32_t> usedCommandBuffers;

    for(auto queue : queueFamilies.uniqueFamilies) {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            auto &cb = commandBuffers[i][queue];
            CHECK_VK_RES(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &cb));

            auto name = fmt::format("cb #{} {}", i, string_VkQueueFlags(queue));

            VkQueue physQueue;
            vkGetDeviceQueue(device, queue, 0, &physQueue);
            VkDebugUtilsObjectNameInfoEXT name_info{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = VK_OBJECT_TYPE_COMMAND_BUFFER,
                .objectHandle = (uint64_t) cb,
                .pObjectName = name.c_str(),
            };
            vkSetDebugUtilsObjectNameEXT(device, &name_info);
        }
    }

    LOG_INFO("Starting rendering.");

    bool shouldResize = false;
    uint64_t frameCount = 0;
    double runtime = 0;
    uint frameIndex = 0;
    uint imageIndex = 0;
    float deltatime = 1e-6;
    while(!glfwWindowShouldClose(window->handle)) {
        FrameMark;

        auto start = std::chrono::high_resolution_clock::now();
        // LOG_TRACE(frameIndex);

        // Poll events
        glfwPollEvents();
        auto prevSize = window->size;
        glfwGetWindowSize(window->handle, reinterpret_cast<int *>(&window->size.x), reinterpret_cast<int *>(&window->size.y));
        VkExtent2D windowExtent = { .width = window->size.x, .height = window->size.y };

        shouldResize = shouldResize || prevSize != window->size;

        // Resize swapchain
        if(shouldResize) {
            LOG_INFO("Resizing the swapchain and {} images to {}x{}",
                std::ranges::distance(sReg.view<egn::ResizeToTarget>()),
                windowExtent.width,
                windowExtent.height);
            CHECK_VK_RES(vkDeviceWaitIdle(device));

            egn::vk::resizeSwapchain(swapchain.getc(), windowExtent);

            shouldResize = false;
        }

        scene->update(sReg, deltatime);

        /////////////////////////////////////////////////////////////////////

        // Wait on fence
        CHECK_VK_RES(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
        CHECK_VK_RES(vkResetFences(device, 1, &fences[frameIndex]));

        // Acquire next image
        auto imageAcquireRes = vkAcquireNextImageKHR(device, swapchain->swapchain, UINT64_MAX, presentSemaphores[frameIndex], nullptr, &imageIndex);
        if(imageAcquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
            LOG_TRACE("VK_ERROR_OUT_OF_DATE_KHR");
            shouldResize = true;
            continue;
        } else if(imageAcquireRes == VK_SUBOPTIMAL_KHR) {
            // Ignore
            // FIXME: Infinite VK_SUBOPTIMAL_KHR (on wayland)
            // LOG_TRACE("VK_SUBOPTIMAL_KHR");
            // shouldResize = true;
        } else if(imageAcquireRes != VK_SUCCESS) {
            CHECK_VK_RES(imageAcquireRes);
            LOG_ERROR("Could not acquire the next swap chain image!");
            break;
        }

        if(std::ranges::distance(sReg.view<egn::ResourceDirty>()))
            LOG_TRACE("{} resources are dirty! {}", std::ranges::distance(sReg.view<egn::ResourceDirty>()), sReg.view<egn::ResourceDirty>());

        // FIXME: ResourceDirty flag removes too early for resources that are used in multiple frames. Writes are added only for frame 0.
        // FIX: Add a counter to the component.
        for(auto e : sReg.view<egn::ResourceDirty>()) {
            e.erase<egn::ResourceDirty>();
        }

        egn::CommandBuffers frameCommandBuffers{
            .commandBuffers = commandBuffers.at(frameIndex),
            .queueIndices = queueFamilies.indices,
        };

        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        for(auto const &[queue, cb] : frameCommandBuffers.commandBuffers) {
            vkBeginCommandBuffer(cb, &beginInfo);
        }

        // TODO: measure performance of rebuilding render graph every frame
        renderer->preFrame(swapchain->images[imageIndex], frameIndex);
        renderer->render(frameCommandBuffers);

        // Submit command buffer
        for(auto [queue, cb] : frameCommandBuffers.commandBuffers) {
            vkEndCommandBuffer(cb);

            if(!frameCommandBuffers.usedCommandBuffers.contains(queue)) {
                continue;
            }

            VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submitInfo{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &presentSemaphores[frameIndex],
                .pWaitDstStageMask = &waitStages,
                .commandBufferCount = 1,
                .pCommandBuffers = &cb,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &renderSemaphores[imageIndex],
            };

            VkQueue physQueue;
            vkGetDeviceQueue(device, queue, 0, &physQueue);
            CHECK_VK_RES(vkQueueSubmit(physQueue, 1, &submitInfo, fences[frameIndex]));
        }

        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;

        // Present image
        VkPresentInfoKHR presentInfo{ .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &swapchain->swapchain,
            .pImageIndices = &imageIndex };
        CHECK_VK_RES(vkQueuePresentKHR(presentQueue, &presentInfo));

        deltatime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1e-9f;
        runtime += deltatime;
        ++frameCount;
        // LOG_INFO("dt {:.2f}ms fps {:.2f}", deltatime * 1e3, 1 / deltatime);
    }

    CHECK_VK_RES(vkDeviceWaitIdle(device));

    ////////////////////////////////////////////////////////////////

    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(device, presentSemaphores[i], nullptr);
        vkDestroyFence(device, fences[i], nullptr);
    }
    for(auto &semaphore : renderSemaphores)
        vkDestroySemaphore(device, semaphore, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    for(auto e : sReg.view<egn::VulkanModel>()) {
        for(auto &mesh : e.get<egn::VulkanModel>().meshes) {
            egn::vk::destroy(mesh.buffers.pos);
            egn::vk::destroy(mesh.buffers.attr);
            egn::vk::destroy(mesh.buffers.idx);
        }
    }
    for(egn::RestrictedEntity<egn::vk::Image> e : sReg.view<egn::vk::Image>()) {
        if(e->owns) {
            LOG_TRACE("= Destroying {} {} (valid: {} owns: {})", e, e->createInfo.name, e->valid(), e->owns);
            egn::vk::destroy(*e);
            e.erase<decltype(e)::Component<0>>();
            if(e.empty())
                e.destroy();
        }
    }
    for(egn::RestrictedEntity<egn::vk::Buffer> e : sReg.view<egn::vk::Buffer>()) {
        if(e->owns) {
            LOG_TRACE("= Destroying {} {} (valid: {} owns: {})", e, e->createInfo.name, e->valid(), e->owns);
            egn::vk::destroy(*e);
            e.erase<decltype(e)::Component<0>>();
            if(e.empty())
                e.destroy();
        }
    }
    for(egn::RestrictedEntity<egn::vk::Pipeline> e : sReg.view<egn::vk::Pipeline>()) {
        LOG_TRACE("= Destroying {}", e);
        egn::vk::destroy(*e);
        e.erase<decltype(e)::Component<0>>();
        if(e.empty())
            e.destroy();
    }
    for(egn::RestrictedEntity<egn::vk::Shader> e : sReg.view<egn::vk::Shader>()) {
        LOG_TRACE("= Destroying {} {}", e, e->createInfo.src);
        egn::vk::destroy(*e);
        e.erase<decltype(e)::Component<0>>();
        if(e.empty())
            e.destroy();
    }
    for(egn::RestrictedEntity<egn::vk::Swapchain> e : sReg.view<egn::vk::Swapchain>()) {
        egn::vk::destroy(*e);
        e.erase<decltype(e)::Component<0>>();
        if(e.empty())
            e.destroy();
    }
    for(egn::RestrictedEntity<egn::Window> e : sReg.view<egn::Window>()) {
        vkDestroySurfaceKHR(instance, e->surface, nullptr);
    }
    for(egn::RestrictedEntity<egn::vk::Device> e : sReg.view<egn::vk::Device>()) {
        vmaDestroyAllocator(e->vma);
        vkDestroyDevice(e->device, nullptr);
    }
    for(egn::RestrictedEntity<egn::vk::Instance> e : sReg.view<egn::vk::Instance>()) {
        vkDestroyDebugUtilsMessengerEXT(e->instance, e->debugMessenger, nullptr);
        vkDestroyInstance(e->instance, nullptr);
    }

    LOG_INFO("Ran for {:.3}s avg deltatime: {:.3}ms ({:.3} fps)", runtime, runtime / frameCount * 1e3, frameCount / runtime);
    std::ofstream("tmp/runtime.txt", std::ios::out | std::ios::app)
        << getDateTime() << "    rtm " << std::setprecision(4) << runtime << " s    avg " << runtime / frameCount * 1e3 << "ms" << "    "
        << frameCount / runtime << "fps\n";

    LOG_INFO("Exiting");
    return 0;
}

int main(int argc, char **argv) {
    std::filesystem::create_directory("tmp/");

    time_t t = time(0);
    struct tm *now = localtime(&t);
    char buffer[80];
    strftime(buffer, 80, "%Y%m%d%H%M%S.log", now);
    initLogger("tmp/logs/main_" + std::string(buffer));

    CPPTRACE_TRY {
        return app(argc, argv);
    }
    CPPTRACE_CATCH(std::exception const &e) {
        LOG_ERROR("Exception: {}\n{}", e.what(), cpptrace::from_current_exception().to_string(true));
    }
}
