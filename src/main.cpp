#include <bits/stdc++.h>

#include "vk/vk.hpp"
#include "ECS.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include "Logging.hpp"
#include "IO.hpp"
#include "Controller.hpp"   
#include "ResourceProcessing.hpp"

extern Registry sReg;
Registry sReg;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

struct Transform
{
    glm::vec3 position{0};
    glm::quat orientation{1, 0, 0, 0};
    glm::vec3 scale{1};
    inline glm::mat4 getMat() const {
        return glm::translate(glm::mat4{1.0f}, position) * glm::mat4_cast(orientation) * glm::scale(glm::mat4{1.0f}, scale);
    };
};
struct ModelInstance
{
    ecs::entity eModel = 0;
};


inline Transform lookat(glm::vec3 pos, glm::vec3 center)
{
    auto dir = center - pos;
    auto up = glm::abs(glm::dot(dir, {0,1,0})) > 0.999 ? glm::vec3{1,0,0} : glm::vec3{0,1,0};
    return {
        .position = pos,
        .orientation = glm::normalize(glm::quatLookAt(dir, up))
    };
}
static bool init()
{
    if(!glfwInit())
    {
        LOG_ERROR("Failed to init glfw!");
        return false;
    }
    if(!glfwVulkanSupported())
    {
        LOG_ERROR("Vulkan is not supported!");
        return false;
    }

    VkResult res = volkInitialize();
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to init volk: {}!", string_VkResult(res));
        return false;
    }

    return true;
}

static Entity makeWindow(Registry &reg, std::string_view name)
{
    auto eWindow = reg.create<Window>();
    auto &window = eWindow.get<Window>();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window.handle = glfwCreateWindow(800, 600, name.data(), nullptr, nullptr);
    glfwGetWindowSize(window.handle, reinterpret_cast<int *>(&window.size.x), reinterpret_cast<int *>(&window.size.y));
    if(glfwRawMouseMotionSupported())
        glfwSetInputMode(window.handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    io::setCallbacks(window.handle, reg.getReg());

    return eWindow;
}


int main(int argc, char **argv)
{
    initLogger();
    if(!init())
    {
        LOG_ERROR("Failed to init!");
        return -1;
    }

    Window &window = makeWindow(sReg, "levulkan").get<Window>();

    vk::InitInfo initInfo{
        .appName = "levulkan",
        .window = window.handle,
        .version = VK_VERSION_1_3,
        .deviceFeatures = {
            .features = {
                .geometryShader = true,
                .shaderSampledImageArrayDynamicIndexing = true,
            },
            .vulkan12 = {
                .descriptorIndexing = true,
                .descriptorBindingVariableDescriptorCount = true,
                .runtimeDescriptorArray = true,
                .bufferDeviceAddress = true,
            },
            .vulkan13 = {
                .synchronization2 = true,
                .dynamicRendering = true,
            },
        }
    };

    vk::enableValidationLayers(initInfo);

    vk::InitResult initRes = vk::init(initInfo);

    if(!initRes.success)
    {
        LOG_ERROR("Failed to init vulkan!");
        return -1;
    }

    vk::ShaderCreateInfo shaderCI{
        .src = "shaders/basic.glsl",
        .bin = "shaders-bin/basic",
        .device = initRes.device
    };
    vk::Shader shader = vk::makeShader(shaderCI);

    if(!shader.valid)
    {
        LOG_ERROR("Failed to compile shader!");
        return -1;
    }

    auto suzanne = loadModel("assets/suzanne.glb");
    
    ResourceAllocator alloc;
    alloc.alloc = initRes.vma;
    for(auto eModel : sReg.view<Model>(exclude<VulkanModel>{}))
        processModel(alloc, eModel);


    std::vector<VkDescriptorImageInfo> imageInfos;
    for(auto eImage : alloc.images)
    {
        auto &image = eImage.get<vk::Image>();
        imageInfos.emplace_back(VkDescriptorImageInfo{
            .sampler = image.sampler,
            .imageView = image.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });
    }
    vk::GraphicsPipelineCreateInfo pipelineCI{
        .layout = {
            .descriptorWrites = {
                {{.set = 0, .binding = 0}, vk::PipelineLayoutCreateInfo::DescriptorWrite{
                    .
                }}
            },
            .framesInFlight = MAX_FRAMES_IN_FLIGHT,
        }
    };
    vk::Pipeline pipeline = vk::makePipeline(shader, pipelineCI);

    ////////////////////////////////////////////////////////////////

    for(auto e : sReg.view<VulkanModel>())
    {
        for(auto &mesh : e.get<VulkanModel>().meshes)
        {
            vk::destroy(mesh.buffers.pos);
            vk::destroy(mesh.buffers.uv);
            vk::destroy(mesh.buffers.norm);
            vk::destroy(mesh.buffers.tan);
            vk::destroy(mesh.buffers.idx);
        }
    }
    for(auto e : sReg.view<vk::Image>())
        vk::destroy(e.get<vk::Image>());
    for(auto e : sReg.view<vk::Buffer>())
        vk::destroy(e.get<vk::Buffer>());

    vk::destroy(pipeline);
    vk::destroy(shader);

    vmaDestroyAllocator(initRes.vma);
    vkDestroyDevice(initRes.device, nullptr);
    vkDestroySurfaceKHR(initRes.instance, initRes.surface, nullptr);
    vkDestroyDebugUtilsMessengerEXT(initRes.instance, initRes.debugMessenger, nullptr);
    vkDestroyInstance(initRes.instance, nullptr);

    LOG_INFO("Exiting");
}