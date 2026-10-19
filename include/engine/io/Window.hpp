#pragma once

#include <queue>

#include "glm/glm.hpp"
#include "GLFW/glfw3.h"

#include "engine/dsa/ECS.hpp"
#include "engine/vk/vulkan.h"
#include "engine/vk/Init.hpp"

namespace egn {

struct Window {
    GLFWwindow *handle = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    glm::uvec2 size;
    RestrictedEntity<vk::Swapchain> swapchain;
};

struct WindowEventListener {
    struct KeyEvent {
        RestrictedEntity<Window> window;
        int key;
        int scancode;
        int action;
        int mods;
    };
    struct CursorPosEvent {
        RestrictedEntity<Window> window;
        glm::dvec2 pos;
        glm::dvec2 delta;
    };
    struct ScrollEvent {
        RestrictedEntity<Window> window;
        glm::dvec2 offset;
    };
    struct MouseButtonEvent {
        RestrictedEntity<Window> window;
        int button;
        int action;
        int mods;
    };
    struct TextInputEvent {
        RestrictedEntity<Window> window;
        unsigned int codepoint; // Unicode
    };

    RestrictedEntity<Window> window;

    std::queue<KeyEvent> keyEvents;
    std::queue<CursorPosEvent> cursorPosEvents;
    std::queue<ScrollEvent> scrollEvents;
    std::queue<MouseButtonEvent> mouseButtonEvents;
    std::queue<TextInputEvent> textInputEvents;

    glm::dvec2 prevCursorPos{ -1 };
};

inline void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<WindowEventListener>())
        reg.get<WindowEventListener>(e).keyEvents.emplace(window, key, scancode, action, mods);
}
inline void cursorPosCallback(GLFWwindow *window, double xpos, double ypos) {
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    auto cursorPos = glm::dvec2{ xpos, ypos };
    for(auto e : reg.view<WindowEventListener>()) {
        glm::dvec2 delta{ 0 };
        auto &listener = reg.get<WindowEventListener>(e);
        if(listener.prevCursorPos != glm::dvec2{ -1 })
            delta = cursorPos - listener.prevCursorPos;
        listener.prevCursorPos = cursorPos;
        listener.cursorPosEvents.emplace(window, cursorPos, delta);
    }
}
inline void scrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<WindowEventListener>())
        reg.get<WindowEventListener>(e).scrollEvents.emplace(window, glm::dvec2{ xoffset, yoffset });
}
inline void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<WindowEventListener>())
        reg.get<WindowEventListener>(e).mouseButtonEvents.emplace(window, button, action, mods);
}
inline void charCallback(GLFWwindow *window, unsigned int codepoint) {
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<WindowEventListener>())
        reg.get<WindowEventListener>(e).textInputEvents.emplace(window, codepoint);
}
inline void setCallbacks(GLFWwindow *window, ecs::registry &registry) {
    glfwSetWindowUserPointer(window, &registry);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCharCallback(window, charCallback);
}

}; // namespace io
