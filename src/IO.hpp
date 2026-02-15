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
    std::queue<KeyEvent> keyEvents;
    std::queue<CursorPosEvent> cursorPosEvents;
    std::queue<ScrollEvent> scrollEvents;

    glm::dvec2 prevCursorPos{-1};
};