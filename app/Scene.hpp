#pragma once
#include "Controller.hpp"
#include "engine/dsa/ECS.hpp"
#include "engine/renderer/IRenderer.hpp"
#include "engine/renderer/RenderGraph.hpp"
#include "engine/renderer/ResourceManager.hpp"

namespace scene {

class IScene {
public:
    virtual ~IScene() = default;
    virtual void setup(egn::Registry &reg) = 0;
    virtual void setupRenderer(egn::Registry &reg, egn::IRenderer **renderer, egn::vk::Device const &device) = 0;
    virtual void update(egn::Registry &reg, float deltatime) = 0;
};

class TestScene : public IScene {
protected:
    Controller mController;
    egn::Entity mEventListener;
    egn::Entity mCamera;
    VkCommandPool mCommandPool = nullptr;
    egn::vk::Device mDevice;
public:
    void setup(egn::Registry &reg) override;
    void setupRenderer(egn::Registry &reg, egn::IRenderer **renderer, egn::vk::Device const &device) override;
    void update(egn::Registry &reg, float deltatime) override;
};

}