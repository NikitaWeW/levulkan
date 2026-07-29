#include "Logging.hpp"
#include "resource/Resources.hpp"
#include "resource/Loaders.hpp"

#include "vk/vk.hpp"
#include "ECS.hpp"


struct Transform {
    glm::vec3 position{0};
    glm::quat orientation{1, 0, 0, 0};
    glm::vec3 scale{1};
    inline glm::mat4 getMat() const {
        return glm::translate(glm::mat4{1.0f}, position) * glm::mat4_cast(orientation) * glm::scale(glm::mat4{1.0f}, scale);
    };
};
struct ModelInstance {
    Entity eModel;
};

struct VulkanMaterial {
    ::Material::Properties properties;
    // Texture2D array indices
    struct Textures
    {
        uint32_t albedo;
        uint32_t metallic;
        uint32_t roughness;
        uint32_t ambient;
        uint32_t normal;
        uint32_t displacement;
    } textures;
};
struct VulkanModel {
    struct Mesh 
    {
        VulkanMaterial material;
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
struct MatrixData {
    struct CameraData {
        glm::mat4 projMat;
        glm::mat4 viewMat;
    } camera;
    struct ModelData {
        glm::mat4 modelMat;
        glm::mat4 normMat;
    } model;
};

struct UniformBuffer {
    VulkanMaterial uMaterial;
    MatrixData uMatrixData;
};
struct ResizeToSwapchain {};

/// Every processed image has this component.
struct ImageIndex {
    uint32_t index = 0;
};
class ResourceAllocator {
private:
    vk::AllocationCreateInfo mAllocInfo;
    VkCommandBuffer mCommandBuffer = nullptr;
    VkFence mFence = nullptr;
    VkQueue mQueue = nullptr;
    std::vector<Entity> mProcessedImages;
public:
    /// @brief The index in the #images array
    /// Every processed image has this component.
    struct ImageIndex {
        uint32_t index = 0;
    };
    ResourceAllocator() = default;
    ResourceAllocator(vk::AllocationCreateInfo const &allocInfo, VkCommandPool commandPool, VkQueue queue);
    ~ResourceAllocator();
    inline std::vector<Entity> getProcessedImages() const { return mProcessedImages; }

    /// Adds a vk::Image component.
    /// @returns The image index
    uint32_t processImage(Entity eImage);
    /// Adds a vk::Buffer component.
    void processModel(Entity eModel);
    void begin();
    void end();
};

// "Simple" means that some arguments are deduced automatically
// E.g. more convenient

struct SimpleShaderCreateInfo {
    std::string srcPrefix = "shaders";
    std::string binPrefix = "shaders-bin";
    std::vector<std::string> includeDirs;
    std::vector<std::string> systemIncludeDirs;
    std::vector<std::pair<std::string, std::string>> definitions;

    bool debugInfo = true;
    vk::ShaderCreateInfo::Optimization optimization = vk::ShaderCreateInfo::Optimization::Default;
    bool obfuscate = false;

    uint32_t targetVersion = VK_API_VERSION_1_3;
    vk::SpirvVersion spirvVersion = vk::SpirvVersion::SpirvVersion_1_6;
};
struct SimplePipeline {
    vk::Pipeline::Type type = vk::Pipeline::Type::Invalid;
    std::vector<VkDynamicState> dynamicState = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineCreateFlags flags = 0;
    struct {
        std::unordered_map<std::string, Entity> descriptors;
    } layout;
    struct {
        vk::GraphicsPipelineCreateInfo::Input         input;
        vk::GraphicsPipelineCreateInfo::DepthStencil  depthStencil;
        vk::GraphicsPipelineCreateInfo::Rasterization rasterization;
        vk::GraphicsPipelineCreateInfo::Multisample   multisample;
        VkPipelineColorBlendAttachmentState           blending;
    } graphics;
};
struct SimpleRenderPass {
    std::string                                     name;
    std::string                                     shader;
    std::vector<vk::RenderPass::ResourceDependency> reads;
    std::vector<vk::RenderPass::ResourceWrite>      writes;
    SimplePipeline                                  pipeline;
    std::function<vk::RenderPass::callback_t>       callback;
};
class RenderManager {
    SimpleShaderCreateInfo mShaderInfo;
    vk::AllocationCreateInfo mAllocInfo;
    vk::RenderGraph mRenderGraph;
    Entity mSwapchain;

    VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;

    Entity addImageResource(std::string_view name, glm::uvec2 size, vk::ImageCreateInfo ci);
    vk::Shader makeShader(std::string_view name);
    vk::Pipeline makePipeline(SimpleRenderPass const &pass, VkQueueFlagBits queue);
public:
    RenderManager() = default;
    RenderManager(vk::AllocationCreateInfo allocInfo, SimpleShaderCreateInfo shaderInfo, REntity<vk::Swapchain> swapchain, VkPhysicalDevice device);
    ~RenderManager();

    Entity addColorResource(std::string_view name, glm::uvec2 size = {0, 0});
    Entity addDepthStencilResource(std::string_view name, glm::uvec2 size = {0, 0});
    Entity addBufferResource(std::string_view name, uint32_t size, void const *data = nullptr, VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    void addResource(RAnyEntity<vk::Image, vk::Buffer> eResource);

    void addPass(vk::RenderPass const &pass);
    void addPass(SimpleRenderPass const &pass);
};
