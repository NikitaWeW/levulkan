#pragma once
#include "engine/dsa/ECS.hpp"
#include <glm/glm.hpp>

namespace egn {

/// Wh.. thats IT?
struct Camera {
    glm::mat4 projMat;
    glm::mat4 viewMat;
};

};