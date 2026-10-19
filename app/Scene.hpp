#pragma once
#include "engine/dsa/ECS.hpp"

namespace scene {

class IScene {
public:
    virtual ~IScene();
    virtual void setup(egn::Registry &reg) = 0;
    virtual void update(egn::Registry &reg, float deltatime) = 0;
};

    

}