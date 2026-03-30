#pragma once

#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include <queue>
#include "nicecs/ecs.hpp"

struct Window
{
    glm::uvec2 size;
    GLFWwindow *handle;
};

namespace io
{

struct EventListener
{
    struct KeyEvent
    {
        GLFWwindow *window;
        int key;
        int scancode;
        int action;
        int mods;
    };
    struct CursorPosEvent
    {
        GLFWwindow *window;
        glm::dvec2 pos;
        glm::dvec2 delta;
    };
    struct ScrollEvent
    {
        GLFWwindow *window;
        glm::dvec2 offset;
    };
    struct MouseButtonEvent
    {
        GLFWwindow *window;
        int button;
        int action;
        int mods;
    };
    struct TextInputEvent
    {
        GLFWwindow* window;
        unsigned int codepoint; // Unicode
    };
    std::queue<KeyEvent> keyEvents;
    std::queue<CursorPosEvent> cursorPosEvents;
    std::queue<ScrollEvent> scrollEvents;
    std::queue<MouseButtonEvent> mouseButtonEvents;
    std::queue<TextInputEvent> textInputEvents;

    glm::dvec2 prevCursorPos{-1};
};

inline void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<EventListener>())
        reg.get<EventListener>(e).keyEvents.emplace(window, key, scancode, action, mods);
}
inline void cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    auto cursorPos = glm::dvec2{xpos, ypos};
    for(auto e : reg.view<EventListener>())
    {
        glm::dvec2 delta{0};
        auto &listener = reg.get<EventListener>(e);
        if(listener.prevCursorPos != glm::dvec2{-1})
            delta = cursorPos - listener.prevCursorPos;
        listener.prevCursorPos = cursorPos;
        listener.cursorPosEvents.emplace(window, cursorPos, delta);
    }
}
inline void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<EventListener>())
        reg.get<EventListener>(e).scrollEvents.emplace(window, glm::dvec2{xoffset, yoffset});
}
inline void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<EventListener>())
        reg.get<EventListener>(e).mouseButtonEvents.emplace(window, button, action, mods);
}
inline void charCallback(GLFWwindow* window, unsigned int codepoint)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<EventListener>())
        reg.get<EventListener>(e).textInputEvents.emplace(window, codepoint);
}
inline void setCallbacks(GLFWwindow *window, ecs::registry &registry)
{
    glfwSetWindowUserPointer(window, &registry);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCharCallback(window, charCallback);
}

}; // namespace io
