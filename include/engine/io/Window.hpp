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

namespace _impl {
    struct WindowPointer {
        std::unique_ptr<Entity> window;
        inline WindowPointer(Entity *e) {
            window = std::unique_ptr<Entity>(e);
        }
    };
}

inline void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    RestrictedEntity<Window> eWindow = *static_cast<Entity *>(glfwGetWindowUserPointer(window));
    for(RestrictedEntity<WindowEventListener> e : eWindow.reg().view<WindowEventListener>())
        e->keyEvents.emplace(eWindow, key, scancode, action, mods);
}
inline void cursorPosCallback(GLFWwindow *window, double xpos, double ypos) {
    RestrictedEntity<Window> eWindow = *static_cast<Entity *>(glfwGetWindowUserPointer(window));
    auto cursorPos = glm::dvec2{ xpos, ypos };
    for(RestrictedEntity<WindowEventListener> e : eWindow.reg().view<WindowEventListener>()) {
        glm::dvec2 delta{ 0 };
        auto &listener = *e;
        if(listener.prevCursorPos != glm::dvec2{ -1 })
            delta = cursorPos - listener.prevCursorPos;
        listener.prevCursorPos = cursorPos;
        listener.cursorPosEvents.emplace(eWindow, cursorPos, delta);
    }
}
inline void scrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
    RestrictedEntity<Window> eWindow = *static_cast<Entity *>(glfwGetWindowUserPointer(window));
    for(RestrictedEntity<WindowEventListener> e : eWindow.reg().view<WindowEventListener>())
        e->scrollEvents.emplace(eWindow, glm::dvec2{ xoffset, yoffset });
}
inline void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    RestrictedEntity<Window> eWindow = *static_cast<Entity *>(glfwGetWindowUserPointer(window));
    for(RestrictedEntity<WindowEventListener> e : eWindow.reg().view<WindowEventListener>())
        e->mouseButtonEvents.emplace(eWindow, button, action, mods);
}
inline void charCallback(GLFWwindow *window, unsigned int codepoint) {
    RestrictedEntity<Window> eWindow = *static_cast<Entity *>(glfwGetWindowUserPointer(window));
    for(RestrictedEntity<WindowEventListener> e : eWindow.reg().view<WindowEventListener>())
        e->textInputEvents.emplace(eWindow, codepoint);
}
inline void setCallbacks(RestrictedEntity<Window> window) {
    auto userPtr = window.reg().create<_impl::WindowPointer>(new Entity(window)).get<_impl::WindowPointer>().window.get();
    auto handle = window->handle;
    glfwSetWindowUserPointer(handle, userPtr);
    glfwSetKeyCallback(handle, keyCallback);
    glfwSetCursorPosCallback(handle, cursorPosCallback);
    glfwSetScrollCallback(handle, scrollCallback);
    glfwSetMouseButtonCallback(handle, mouseButtonCallback);
    glfwSetCharCallback(handle, charCallback);
}

}; // namespace io
