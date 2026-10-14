#pragma once
#include "engine/io/Window.hpp"
#include "GLFW/glfw3.h"
#include "glm/gtc/quaternion.hpp"
#include "engine/dsa/ECS.hpp"

class Controller {
public:
    struct Camera
    {
        egn::Entity window;
        glm::mat4 projMat{1.0f};
        glm::mat4 viewMat{1.0f};
        glm::vec3 position;
        glm::quat orientation;
        // glm::vec2 pitchYaw;

        float fov = 90;
        float speed = 5;
        float boost = 20;
        float sensitivity = 0.1;
        float znear = 0.01;
        float zfar = 1000;
        bool firstTimeMovingMouse = true;
        bool locked = true;
    };

    static egn::Entity createCamera(egn::Registry &reg, glm::vec3 pos = {0, 0, 0}, glm::vec3 target = {0, 0, -10});

    void update(egn::Registry &reg, float dt);
};
