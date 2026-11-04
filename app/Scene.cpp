#include "Scene.hpp"
#include "Controller.hpp"
#include "engine/renderer/ResourceManager.hpp"
#include "engine/scene/Loaders.hpp"
#include "engine/scene/Resources.hpp"

#include <filesystem>
#include <random>

struct CycleAnimations { };

static std::unordered_set<std::string> getSmoothShadedModelPaths() {
    std::ifstream smoothShadeFile("assets/models/smoothShade.txt");
    if(!smoothShadeFile.is_open()) {
        return { };
    }

    std::unordered_set<std::string> res;
    std::string line;
    while(std::getline(smoothShadeFile, line)) {
        if(!line.empty()) {
            res.emplace(std::move(line));
        }
    }

    LOG_TRACE("Smoothly shading {}", res);
    return res;
}
static std::string printTexture(egn::Entity e) {
    if(!e.valid() || !e.contains<egn::Texture2D>())
        return fmt::format("{}", e);
    auto const &texture2D = e.get<egn::Texture2D>();
    return fmt::format("{}, \"{:<30} {}x{}, {:>3} {} mips",
        e,
        texture2D.path + "\",",
        texture2D.bitmap.size.x,
        texture2D.bitmap.size.y,
        (texture2D.srgb ? "srgb" : "not srgb"),
        texture2D.numMipLevels);
}
[[maybe_unused]] [[nodiscard]] static std::string printModelData(egn::Entity e) {
    assert(e.valid() && e.contains<egn::Model>());
    egn::Model const &model = e.get<egn::Model>();
    std::stringstream ss;
    ss << "Model: " << e << ": \"" << model.path << "\"\n";
    ss << "Skeleton: \n";
    ss << "  Bone map size / number of bones: " << model.skeleton.boneMap.size() << "\n";
    if(model.skeleton.boneMap.size() <= 30)
        for(auto const &[name, id] : model.skeleton.boneMap)
            ss << "    [\"" << name << "\": " << id << "]\n";

    ss << "Animations: " << model.animations.size() << "\n";
    for(auto const &animation : model.animations) {
        ss << "-----------------\n";
        ss << "Animation: \"" << animation.name << "\"\n";
        ss << "  Duration: " << animation.durationTicks << " ticks, tps: " << animation.ticksPerSecond << "\n";
        ss << "  Number of bones: " << animation.bones.size() << "\n";
    }

    ss << "Meshes: " << model.meshes.size() << "\n";
    for(auto const &mesh : model.meshes) {
        ss << "-----------------\n";
        ss << "Geometry:\n";
        ss << "  Triangles:   " << mesh.geometry.indices.size() / 3 << "\n";
        ss << "  Indices:     " << mesh.geometry.indices.size() << "\n";
        ss << "  Positions:   " << mesh.geometry.positions.size() << "\n";
        ss << "  Attributes:  " << mesh.geometry.attributes.size() << "\n";
        ss << "Material:\n";
        ss << "  Textures:\n";
        ss << "    Albedo:       " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.albedo }) << "\n";
        ss << "    Metallic:     " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.metallic }) << "\n";
        ss << "    Roughness:    " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.roughness }) << "\n";
        ss << "    Ambient:      " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.ambient }) << "\n";
        ss << "    Normal:       " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.normal }) << "\n";
        ss << "    Displacement: " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.displacement }) << "\n";
        ss << "  Properties:\n";
        ss << "    Ambient:      " << mesh.material.properties.ambient << "\n";
        ss << "    Albedo:       " << mesh.material.properties.albedo << "\n";
        ss << "    Specular:     " << mesh.material.properties.specular << "\n";
        ss << "    Emission:     " << mesh.material.properties.emission << "\n";
        ss << "    Shininess:    " << mesh.material.properties.shininess << "\n";
        ss << "    Metallic:     " << mesh.material.properties.metallic << "\n";
        ss << "    IOR:          " << mesh.material.properties.ior << "\n";
    }

    return ss.str();
}
static egn::Entity loadTexture(egn::Registry &reg, std::string_view path, bool required = true, bool flip = false) {
    static egn::TextureLoader loader(reg.getReg());

    egn::TextureLoaderOptions options{
        .flip = flip,
        .desiredChannels = 4,
    };
    auto eTexture = egn::Entity{ &reg, loader.loadFromFile(path, options) };
    if(!required && !eTexture.valid())
        return eTexture;
    if(required && !eTexture.valid()) {
        LOG_ERROR("Failed to load {}, which is required!", path);
        assert(false);
        return eTexture;
    }

    // ...

    return eTexture;
}
static egn::Entity loadModel(egn::Registry &reg, std::string_view path, std::optional<egn::Material::Textures> textures = { }, bool required = true) {
    static egn::ModelLoader loader(reg.getReg());
    static auto smoothPaths = getSmoothShadedModelPaths();

    egn::ModelLoaderOptions options{
        .flipUVs = false,
        .flipHandiness = true, // ??
        .windingOrder = egn::ModelLoaderOptions::WindingOrder::CCW,
        .smoothNormalAngle = static_cast<float>(smoothPaths.contains(std::string(path)) ? 35 : 0),
    };
    auto eModel = egn::Entity{ &reg, loader.loadFromFile(path, options) };
    if(!required && !eModel.valid())
        return eModel;
    if(required && !eModel.valid()) {
        LOG_ERROR("Failed to load {}, which is required!", path);
        assert(false);
        return eModel;
    }
    auto &model = eModel.get<egn::Model>();

    if(textures.has_value()) {
        auto defaultMaterial = loader.getDefaultMaterial();
        if(textures->albedo == egn::INVALID_ENTITY)
            textures->albedo = defaultMaterial.textures.albedo;
        if(textures->metallic == egn::INVALID_ENTITY)
            textures->metallic = defaultMaterial.textures.metallic;
        if(textures->roughness == egn::INVALID_ENTITY)
            textures->roughness = defaultMaterial.textures.roughness;
        if(textures->ambient == egn::INVALID_ENTITY)
            textures->ambient = defaultMaterial.textures.ambient;
        if(textures->normal == egn::INVALID_ENTITY)
            textures->normal = defaultMaterial.textures.normal;
        if(textures->displacement == egn::INVALID_ENTITY)
            textures->displacement = defaultMaterial.textures.displacement;

        for(auto &mesh : model.meshes)
            mesh.material.textures = textures.value();
    }

    return eModel;
}
static bool
loadModelTo(egn::Registry &reg, std::string_view path, std::vector<egn::Entity> &to, std::optional<egn::Material::Textures> textures = { }) {
    auto e = loadModel(reg, path, textures, false);
    if(e) {
        to.emplace_back(e);
    }

    return e;
}
static void dumpModels(egn::Registry &reg, std::string_view dir) {
    std::ofstream modelStr(std::string(dir.data()) + "/models.txt");
    for(egn::RestrictedEntity<egn::Model> e : reg.view<egn::Model>()) {
        modelStr << printModelData(e) << "Geometry dumps:\n";
        for(uint i = 0; i < e->meshes.size(); ++i) {
            auto const &mesh = e->meshes[i].geometry;
            auto path = dir.data() + ("/" + std::filesystem::path(e->path).stem().string() + "_") + std::to_string(i) + ".obj";
            modelStr << "  " << path << "\n";
            std::ofstream s(path);
            if(!s) {
                continue;
            }

            for(auto const &position : mesh.positions) {
                s << "v " << position.x << " " << position.y << " " << position.z << "\n";
            }
            s << "\n";
            for(auto const &a : mesh.attributes) {
                s << "n " << a.normal.x << " " << a.normal.y << " " << a.normal.z << "\n";
            }
            s << "\n";
            for(auto const &a : mesh.attributes) {
                s << "t " << a.uv.x << " " << a.uv.y << "\n";
            }
            s << "\n";

            for(uint j = 0; j < mesh.indices.size(); j += 3) {
                s << "f " << mesh.indices[j + 0] + 1 << " " << mesh.indices[j + 1] + 1 << " " << mesh.indices[j + 2] + 1 << "\n";
            }
            s << "\n";
        }
    }
}
static egn::Transform lookat(glm::vec3 pos, glm::vec3 center) {
    auto dir = glm::normalize(center - pos);
    auto up = glm::abs(glm::dot(dir, { 0, 1, 0 })) > 0.999 ? glm::vec3{ 1, 0, 0 } : glm::vec3{ 0, 1, 0 };
    return { .position = pos, .orientation = glm::normalize(glm::quatLookAt(dir, up)) };
}
static void assetGrid(egn::Registry &reg, std::vector<egn::Entity> const &props, glm::uvec3 dimensions, glm::vec3 offset, float distance) {
    assert(!props.empty());
    static std::random_device dev;
    static std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> distP(0, props.size() - 1);
    std::uniform_int_distribution<std::mt19937::result_type> distX(0, dimensions.x - 1);
    std::uniform_int_distribution<std::mt19937::result_type> distY(0, dimensions.y - 1);
    std::uniform_int_distribution<std::mt19937::result_type> distZ(0, dimensions.z - 1);
    auto position = [&](float x, float y, float z) -> glm::vec3 { return (glm::vec3(x, y, z) - glm::vec3(dimensions) * 0.5f) * distance + offset; };

    for(uint x = 0; x < dimensions.x; ++x)
        for(uint y = 0; y < dimensions.y; ++y)
            for(uint z = 0; z < dimensions.z; ++z) {
                // x+y*dimensions.x+z*dimensions.x*dimensions.y;
                reg.create(egn::ModelInstance{ props[distP(rng)] }, lookat(position(x, y, z), position(distX(rng), distY(rng), distZ(rng))));
            }
}

void scene::TestScene::setupRenderer(egn::Registry &reg, egn::IRenderer **renderer, egn::vk::Device const &device) {
    // mRenderer = 
    // renderer = mRenderer.get();




}
void scene::TestScene::setup(egn::Registry &reg) {
    const auto prototypeTextures = egn::Material::Textures{ .albedo = loadTexture(reg, "assets/textures/prototype/texture_03.png", false) };
    std::vector<egn::Entity> props;
    loadModelTo(reg, "assets/models/suzanne.glb", props, prototypeTextures);
    loadModelTo(reg, "assets/models/cube.glb", props, prototypeTextures);
    loadModelTo(reg, "assets/models/sphere.glb", props, prototypeTextures);
    loadModelTo(reg, "assets/models/teapot.glb", props, prototypeTextures);

    const glm::uvec3 numProps = { 20, 6, 10 };
    const float distance = 2;
    const glm::vec3 offset = { 0, -1, -10 };
    assetGrid(reg, props, numProps, offset, distance);

    const glm::vec3 startPos(0, -4, 0);
    const glm::vec3 step(2, 0, 0);
    for(uint i = 0; i < props.size(); ++i) {
        reg.create(egn::ModelInstance{ props[i] }, egn::Transform{ .position = startPos + static_cast<float>(i) * step });
    }

    const auto cubes = loadModel(reg, "assets/models/deccer_cubes/SM_Deccer_Cubes_Textured_Complex.gltf", { });
    if(cubes) {
        reg.create(egn::ModelInstance{ cubes }, egn::Transform{ .position = { -10, 0, 10 } });
    }

    auto fox = loadModel(reg, "assets/models/_fox.glb");
    fox.emplace<CycleAnimations>();
    fox.emplace<egn::ModelInstance>(fox);

    dumpModels(reg, "tmp/models");

    mEventListener = reg.create<egn::WindowEventListener>();
    mCamera = Controller::createCamera(window, { 0, 2, 4 }, { 0, 0, 0 });
}
void scene::TestScene::update(egn::Registry &reg, float deltatime) {
    
    auto &listener = mEventListener.get<egn::WindowEventListener>();
    while(!listener.keyEvents.empty()) {
        auto event = listener.keyEvents.front();
        listener.keyEvents.pop();

        if(event.key == GLFW_KEY_R && event.action == GLFW_PRESS) {
            CHECK_VK_RES(vkDeviceWaitIdle(mDevice.device));
            for(auto e : reg.view<egn::vk::Pipeline, egn::vk::Shader>()) {
                auto &pipeline = e.get<egn::vk::Pipeline>();
                auto &shader = e.get<egn::vk::Shader>();

                auto newShader = egn::vk::makeShader(shader.createInfo);
                if(!newShader.valid)
                    continue;

                egn::vk::destroy(shader);
                shader = newShader;
                egn::vk::destroy(pipeline);
                switch(pipeline.type) {
                    case egn::vk::Pipeline::Type::Graphics:
                        pipeline = egn::vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.graphics);
                        break;

                    case egn::vk::Pipeline::Type::Compute:
                        pipeline = egn::vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.compute);
                        break;

                    case egn::vk::Pipeline::Type::RayTracing:
                        pipeline = egn::vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.raytracing);
                        break;

                    default: assert(false && "unknown pipeline");
                }
                egn::vk::allocateDescriptors(pipeline);

                e.emplace<egn::ResourceDirty>();
            }
        }
    }

    cameraController.update(reg, deltatime);
}
