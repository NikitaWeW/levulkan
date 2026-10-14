#pragma once
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "engine/dsa/EaseFunctions.hpp"
#include "engine/dsa/ECS.hpp"
#include "Resources.hpp"

namespace egn {

struct CurrentAnimation {
    std::string name;
    enum class RepeatMode { EXIT, LOOP, MIRROR } repeat = RepeatMode::LOOP;
    float time = 0; // normalized
    float speed = 1;
    std::vector<glm::mat4> boneMatrices;
    // used to avoid unnecessary allocations to construct hierarchy-free boneMatrices, but can be used too.
    std::vector<glm::mat4> localBoneMatrices;
};
struct AnimationTransition {
    std::string to;
    float factor = 0;
    float factorPerSecond = 1; // how much does transition factor change per second
    ease::FuncPtr easeFunction = ease::linear;
};

void animate(RestrictedEntityAll<ModelInstance, CurrentAnimation> entity, float deltatime);
void calculateBoneMatrices(std::vector<glm::mat4> &boneMatrices, Model::Skeleton const &skeleton, float normalizedTime, Animation const &animation,
    Animation const *secondAnimation = nullptr, AnimationTransition transition = { });

// TODO: skeleton retargeting

Animation const *findAnimation(Model const &model, std::string_view name);

} // namespace egn
