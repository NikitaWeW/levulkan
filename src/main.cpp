#include <bits/stdc++.h>

#include "vk/vk.hpp"
#include "ECS.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include "Logging.hpp"
#include "IO.hpp"
#include "Controller.hpp"   
#include "resource/Resources.hpp"
#include "resource/Loaders.hpp"

static Registry sReg;
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

struct VulkanModel
{
    struct Mesh 
    {
        // Texture array indices
        struct Textures
        {
            uint32_t albedo;
            uint32_t metallic;
            uint32_t roughness;
            uint32_t ambient;
            uint32_t normal;
            uint32_t displacement;
        } textures;

        // TODO: https://www.youtube.com/watch?v=7bSzp-QildA
        struct Buffers
        {
            vk::Buffer pos;
            vk::Buffer uv;
            vk::Buffer norm;
            vk::Buffer tan;
            vk::Buffer idx;
        } buffers;
        size_t indexCount;
        size_t meshIndex;
    };

    // TODO: add animation support

    Entity eModel;
    std::vector<Mesh> meshes;
};
// Helps to form texture arrays
struct ResourceAllocator
{
    VmaAllocator alloc = VK_NULL_HANDLE;

    // Processed images
    std::vector<Entity> images;

    // Every processed texture has this component.
    // The index in the #images array
    struct ImageIndex
    {
        uint32_t index = 0;
    };
};

////////////////////////////////////////////////////////////////

inline Transform lookat(glm::vec3 pos, glm::vec3 center)
{
    auto dir = center - pos;
    auto up = glm::abs(glm::dot(dir, {0,1,0})) > 0.999 ? glm::vec3{1,0,0} : glm::vec3{0,1,0};
    return {
        .position = pos,
        .orientation = glm::normalize(glm::quatLookAt(dir, up))
    };
}

////////////////////////////////////////////////////////////////

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
        if(material->textures.ambient      == INVALID_ENTITY) material->textures.ambient      = defaultMaterial.textures.ambient;
        if(material->textures.normal       == INVALID_ENTITY) material->textures.normal       = defaultMaterial.textures.normal;
        if(material->textures.displacement == INVALID_ENTITY) material->textures.displacement = defaultMaterial.textures.displacement;

        for(auto &mesh : model.meshes)
            mesh.material = material.value();
    }

    return eModel;
}
template<typename T>
static VkFormat getBitmapFormat(Bitmap<T> const& bmp, bool srgb)
{
    if constexpr (std::is_same_v<T, uint8_t>)
    {
        switch (bmp.numComponents)
        {
            case 1: return srgb ? VK_FORMAT_R8_SRGB       : VK_FORMAT_R8_UNORM;
            case 2: return srgb ? VK_FORMAT_R8G8_SRGB     : VK_FORMAT_R8G8_UNORM;
            case 3: return srgb ? VK_FORMAT_R8G8B8_SRGB   : VK_FORMAT_R8G8B8_UNORM;
            case 4: return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        }
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        switch (bmp.numComponents)
        {
            case 1: return VK_FORMAT_R32_SFLOAT;
            case 2: return VK_FORMAT_R32G32_SFLOAT;
            case 3: return VK_FORMAT_R32G32B32_SFLOAT;
            case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        }
    }
    else if constexpr (std::is_same_v<T, uint16_t>)
    {
        switch (bmp.numComponents)
        {
            case 1: return VK_FORMAT_R16_UNORM;
            case 2: return VK_FORMAT_R16G16_UNORM;
            case 3: return VK_FORMAT_R16G16B16_UNORM;
            case 4: return VK_FORMAT_R16G16B16A16_UNORM;
        }
    }

    LOG_ERROR("Unsupported Bitmap format");
    return VK_FORMAT_UNDEFINED;
}
static uint32_t processImage(ResourceAllocator &alloc, Entity eImage)
{
    if(!eImage.has<vk::Image>())
    {
        auto &image = eImage.get<Texture>();

        if(image.bitmap.numComponents == 3)
            LOG_WARN("Making R32G32B32 texture \"{}\". Maybe change it to 32 bits or something...");

        vk::ImageCreateInfo ci{
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .allocInfo = {
                .allocator = alloc.alloc,
            },
            .format = getBitmapFormat(image.bitmap, image.srgb),
            .dimensions = {
                .width = image.bitmap.size.x,
                .height = image.bitmap.size.y,
                .mipLevels = image.numMipLevels
            },
            .data = image.bitmap.pixels.data()
        };
        eImage.emplace<vk::Image>(vk::makeImage(ci));
        eImage.emplace<ResourceAllocator::ImageIndex>(alloc.images.size());
        alloc.images.emplace_back(eImage);
    }

    return eImage.get<ResourceAllocator::ImageIndex>().index;
}
static void processModel(ResourceAllocator &alloc, Entity eModel)
{
    if(eModel.has<VulkanModel>())
        return;

    auto &model = eModel.get<Model>();
    eModel.emplace<VulkanModel>();
    auto &vulkanModel = eModel.get<VulkanModel>();
    for(uint i = 0; i < model.meshes.size(); ++i)
    {
        auto const &mesh = model.meshes[i];
        auto &vulkanMesh = vulkanModel.meshes.emplace_back();
        
        vulkanMesh.meshIndex = i;
        vulkanMesh.indexCount = mesh.geometry.indices.size();

        vulkanMesh.buffers = {
            .pos  = vk::makeBuffer(alloc.alloc, mesh.geometry.positions, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            .uv   = vk::makeBuffer(alloc.alloc, mesh.geometry.texCoords, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            .norm = vk::makeBuffer(alloc.alloc, mesh.geometry.normals,   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            .tan  = vk::makeBuffer(alloc.alloc, mesh.geometry.tangents,  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            .idx  = vk::makeBuffer(alloc.alloc, mesh.geometry.indices,   VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
        };

        vulkanMesh.textures = {
            .albedo       = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.albedo      }),
            .metallic     = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.metallic    }),
            .roughness    = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.roughness   }),
            .ambient      = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.ambient     }),
            .normal       = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.normal      }),
            .displacement = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.displacement}),
        };
    }
}

////////////////////////////////////////////////////////////////

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

    ////////////////////////////////////////////////////////////////

    auto suzanne = loadModel("assets/suzanne.glb");
    
    ResourceAllocator alloc;
    alloc.alloc = initRes.vma;
    for(auto eModel : sReg.view<Model>(exclude<VulkanModel>{}))
        processModel(alloc, eModel);

    ////////////////////////////////////////////////////////////////

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
                    .imageInfo = imageInfos
                }},
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