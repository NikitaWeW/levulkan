#include "engine/scene/Animation.hpp"
#include "engine/io/Logging.hpp"

#include <algorithm>

using namespace egn;

struct Keyframe {
    glm::vec3 position;
    glm::quat orientation;
    glm::vec3 scale;
};

static float getDurationSeconds(Animation const &animation) {
    float ticksPerSecond = animation.ticksPerSecond;
    assert(ticksPerSecond != 0);
    float animationDurationSeconds = animation.durationTicks / ticksPerSecond;
    animationDurationSeconds = glm::max(animationDurationSeconds, 0.0001f);
    return animationDurationSeconds;
}

static bool updateAnimation(CurrentAnimation &current, Animation const &animation, float deltatime) {
    current.time += (deltatime * current.speed) / getDurationSeconds(animation);

    bool exited = false;

    if(current.time <= 0 || current.time >= 1) {
        switch(current.repeat) {
            case CurrentAnimation::RepeatMode::LOOP: current.time = glm::mod(current.time, 1.0f); break;
            case CurrentAnimation::RepeatMode::EXIT: exited = true; break;
            case CurrentAnimation::RepeatMode::MIRROR: current.speed = -current.speed; break;

            default: current.time = 0; break;
        }
    }
    current.time = glm::clamp<float>(current.time, 0, 1);

    return exited;
}
static Keyframe interpolateKeyframes(Keyframe const &first, Keyframe const &second, float factor) {
    return Keyframe{ .position = glm::mix(first.position, second.position, factor),
        .orientation = glm::normalize(glm::slerp(first.orientation, second.orientation, factor)),
        .scale = glm::mix(first.scale, second.scale, factor) };
}

static glm::vec3 calculateInterpolatedPosition(Animation::Keyframes const &keyframes, float time) {
    auto const &keys = keyframes.positions;
    if(keys.empty())
        return { 0, 0, 0 };

    auto it = std::upper_bound(keys.begin(), keys.end(), time, [](float time, Animation::PositionKey const &keyframe) {
        return keyframe.timeTicks > time;
    });

    if(it >= keys.end() || it == keys.begin())
        return keys.back().value;

    auto const &second = *it;
    auto const &first = *(it - 1);

    float deltatime = second.timeTicks - first.timeTicks;
    float factor = (time - first.timeTicks) / deltatime;
    factor = glm::clamp<float>(factor, 0, 1);
    return glm::mix(first.value, second.value, factor);
}
static glm::quat calculateInterpolatedOrientation(Animation::Keyframes const &keyframes, float time) {
    auto const &keys = keyframes.orientations;
    if(keys.empty())
        return { 0, 0, 0, 1 };

    auto it = std::upper_bound(keys.begin(), keys.end(), time, [](float time, Animation::OrientationKey const &keyframe) {
        return keyframe.timeTicks > time;
    });

    if(it >= keys.end() || it == keys.begin())
        return keys.back().value;

    auto const &second = *it;
    auto const &first = *(it - 1);

    float deltatime = second.timeTicks - first.timeTicks;
    float factor = (time - first.timeTicks) / deltatime;
    factor = glm::clamp<float>(factor, 0, 1);
    return glm::normalize(glm::slerp(first.value, second.value, factor));
}
static glm::vec3 calculateInterpolatedScale(Animation::Keyframes const &keyframes, float time) {
    auto const &keys = keyframes.scales;
    if(keys.empty())
        return { 1, 1, 1 };

    auto it = std::upper_bound(keys.begin(), keys.end(), time, [](float time, Animation::ScaleKey const &keyframe) {
        return keyframe.timeTicks > time;
    });

    if(it >= keys.end() || it == keys.begin())
        return keys.back().value;

    auto const &second = *it;
    auto const &first = *(it - 1);

    float deltatime = second.timeTicks - first.timeTicks;
    float factor = (time - first.timeTicks) / deltatime;
    factor = glm::clamp<float>(factor, 0, 1);
    return glm::mix(first.value, second.value, factor);
}

static Keyframe calculateInterpolatedKeyframe(Animation::Keyframes const &keyframes, float time) {
    return Keyframe{
        .position = calculateInterpolatedPosition(keyframes, time),
        .orientation = calculateInterpolatedOrientation(keyframes, time),
        .scale = calculateInterpolatedScale(keyframes, time),
    };
}
static bool boneHasKeyframes(size_t bone, Animation const &anim) {
    auto const &k = anim.bones[bone];
    return !(k.positions.empty() && k.orientations.empty() && k.scales.empty());
};

void egn::animate(RestrictedEntityAll<ModelInstance, CurrentAnimation> entity, float deltatime) {
    auto const &model = entity.get<ModelInstance>().eModel.get<Model>();
    auto &current = entity.get<CurrentAnimation>();
    auto animation = findAnimation(model, current.name);
    if(!animation) {
        LOG_ERROR("Invalid animation \"{}\" in model \"{}\"", current.name, model.path);
        assert(false && "Invalid animation");
        return;
    }

    if(updateAnimation(current, *animation, deltatime)) {
        entity.erase<CurrentAnimation>();
        return;
    }

    Animation const *secondAnimation = nullptr;
    if(entity.contains<AnimationTransition>()) {
        AnimationTransition &transition = entity.get<AnimationTransition>();

        secondAnimation = findAnimation(model, transition.to);
        if(!secondAnimation) {
            LOG_ERROR("Invalid animation transition destination \"{}\" (from \"{}\") in model \"{}\"",
                transition.to,
                model.path,
                current.name);
            assert(false && "Invalid animation");
            return;
        }

        transition.factor += transition.factorPerSecond * deltatime;
        if(transition.factor >= 1) {
            current.name = transition.to;
            entity.erase<AnimationTransition>();
        }

        calculateBoneMatrices(current.boneMatrices, model.skeleton, current.time, *animation, secondAnimation, transition);
    } else {
        calculateBoneMatrices(current.boneMatrices, model.skeleton, current.time, *animation);
    }
}
void egn::calculateBoneMatrices(std::vector<glm::mat4> &boneMatrices,
    Model::Skeleton const &skeleton,
    float normalizedTime,
    Animation const &animation,
    Animation const *secondAnimation,
    AnimationTransition transition) {
    size_t numBones = animation.bones.size();
    boneMatrices.resize(numBones);
    std::vector<glm::mat4> localBoneMatrices(numBones);

    for(size_t bone = 0; bone < numBones; ++bone) {
        glm::mat4 &transform = localBoneMatrices[bone] = skeleton.nodeTransform[bone];

        if(boneHasKeyframes(bone, animation)) {
            auto result = calculateInterpolatedKeyframe(animation.bones[bone], normalizedTime * animation.durationTicks);

            if(secondAnimation) {
                auto second = calculateInterpolatedKeyframe(secondAnimation->bones[bone], normalizedTime * secondAnimation->durationTicks);
                result = interpolateKeyframes(result, second, transition.easeFunction(transition.factor));
            }

            transform = glm::translate(glm::mat4{ 1.0f }, result.position) * glm::mat4_cast(result.orientation) *
                        glm::scale(glm::mat4{ 1.0f }, result.scale);
        }
    }

    for(size_t bone = 0; bone < numBones; ++bone) {
        int parent = skeleton.parents[bone];
        auto &matrix = boneMatrices[bone];
        auto const &local = localBoneMatrices[bone];
        if(parent == -1)
            matrix = local;
        else
            matrix = boneMatrices[parent] * local;
    }
    for(size_t bone = 0; bone < numBones; ++bone) {
        auto &matrix = boneMatrices[bone];
        matrix = skeleton.globalInverseTransform * matrix * skeleton.bindTransform[bone];
    }
}

Animation const *egn::findAnimation(Model const &model, std::string_view name) {
    auto animationIt = std::find_if(model.animations.begin(), model.animations.end(), [&](Animation const &animation) {
        return animation.name == name;
    });
    if(animationIt == model.animations.end())
        return nullptr;
    return &*animationIt;
}
