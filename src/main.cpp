#include <bits/stdc++.h>

#include "ECS.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include "vk/vk.hpp"
#include "Logging.hpp"
#include "resource/Model.hpp"
#include "resource/Loaders.hpp"
#include "IO.hpp"
#include "Controller.hpp"   

Registry sReg;

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

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<EventListener>())
        reg.get<EventListener>(e).keyEvents.emplace(window, key, scancode, action, mods);
}
static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    auto cursorPos = glm::dvec2{xpos, ypos};
    for(auto e : reg.view<EventListener>())
    {
        glm::dvec2 delta{0};
        auto &listener = reg.get<EventListener>(e);
        if(listener.prevCursorPos != glm::dvec2{-1})
            delta = cursorPos - listener.prevCursorPos;
        listener.prevCursorPos = cursorPos;
        listener.cursorPosEvents.emplace(window, cursorPos, delta);
    }
}
static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<EventListener>())
        reg.get<EventListener>(e).scrollEvents.emplace(window, glm::dvec2{xoffset, yoffset});
}



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

    auto res = volkInitialize();
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to init volk: {}!", string_VkResult(res));
        return false;
    }

    return true;
}

static Entity loadModel(std::string_view path, ModelLoaderOptions options = {}, std::optional<Material> material = {})
{
    static ModelLoader loader(sReg.getReg());
    
    auto eModel = Entity{&sReg, loader.loadFromFile(path, options)};
    auto &model = eModel.get<Model>();
    
    if(material.has_value())
    {
        auto defaultMaterial = loader.getDefaultMaterial();
        if(material->textures.albedo       == INVALID_ENTITY) material->textures.albedo       = defaultMaterial.textures.albedo;
        if(material->textures.metallic     == INVALID_ENTITY) material->textures.metallic     = defaultMaterial.textures.metallic;
        if(material->textures.roughness    == INVALID_ENTITY) material->textures.roughness    = defaultMaterial.textures.roughness;
        if(material->textures.normal       == INVALID_ENTITY) material->textures.normal       = defaultMaterial.textures.normal;
        if(material->textures.displacement == INVALID_ENTITY) material->textures.displacement = defaultMaterial.textures.displacement;

        for(auto &mesh : model.meshes)
            mesh.material = material.value();
    }

    return eModel;
}

static void processModels()
{
    for(auto eModel : sReg.view<Model>(exclude<vk::VulkanModel>{}))
        eModel.emplace<vk::VulkanModel>(vk::processModel(eModel.get<Model>()));
}

extern Registry sReg;

int main(int argc, char **argv)
{
    initLogger();
    if(!init())
    {
        LOG_ERROR("Failed to init!");
        return -1;
    }

    Window &window = sReg.create<Window>().get<Window>();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window.handle = glfwCreateWindow(800, 600, "levulkan", nullptr, nullptr);
    glfwGetWindowSize(window.handle, reinterpret_cast<int *>(&window.size.x), reinterpret_cast<int *>(&window.size.y));
    if(glfwRawMouseMotionSupported())
        glfwSetInputMode(window.handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetWindowUserPointer(window.handle, &sReg);
    glfwSetKeyCallback(window.handle, keyCallback);
    glfwSetCursorPosCallback(window.handle, cursorPosCallback);
    glfwSetScrollCallback(window.handle, scrollCallback);

    vk::InitInfo initInfo{
        .appName = "levulkan",
        .window = window.handle,
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

    vk::PipelineCreateInfo pipelineCI{
        .type = vk::Pipeline::Type::GRAPHICS,
        .graphics = {
        }
    };
    vk::Pipeline pipeline = vk::makePipeline(shader, pipelineCI);

    // auto suzanne = loadModel("assets/suzanne.glb");

    // processModels();

    // for(auto e : sReg.view<vk::VulkanModel>())
    // {
    //     vk::destroy(e.get<vk::VulkanModel>());
    // }

    vk::destroy(pipeline);
    vk::destroy(shader);

    vmaDestroyAllocator(initRes.vma);
    vkDestroyDevice(initRes.device, nullptr);
    vkDestroySurfaceKHR(initRes.instance, initRes.surface, nullptr);
    vkDestroyDebugUtilsMessengerEXT(initRes.instance, initRes.debugMessenger, nullptr);
    vkDestroyInstance(initRes.instance, nullptr);

    LOG_INFO("Exiting");
}