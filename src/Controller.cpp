#include "Controller.hpp"
#include "Logging.hpp"

#define CAMERA_COMPONENTS Controller::Camera, EventListener

ecs::entity Controller::createCamera(ecs::registry &reg, glm::vec3 pos, glm::vec3 target)
{
    auto e = reg.create<CAMERA_COMPONENTS>();
    auto up = glm::abs(glm::dot(glm::normalize(target - pos), glm::vec3{0,1,0})) > 0.99 ? glm::vec3{1,0,0} : glm::vec3{0,1,0};
    reg.get<Controller::Camera>(e) = {
        .window = reg.view<Window>().at(0),
        .position = pos,
        .orientation = glm::quat_cast(glm::lookAt(pos, target, up)),
    };
    return e;
}

void Controller::update(ecs::registry &reg, float dt)
{
    for(ecs::entity e_camera : reg.view<CAMERA_COMPONENTS>())
    {
        auto &camera = reg.get<Controller::Camera>(e_camera);
        auto &listener = reg.get<EventListener>(e_camera);
        auto &window = reg.get<Window>(camera.window);

        glm::vec3 right   = glm::normalize(camera.orientation) * glm::vec3{1, 0, 0};
        glm::vec3 up      = glm::normalize(camera.orientation) * glm::vec3{0, 1, 0};
        glm::vec3 forward = glm::normalize(camera.orientation) * glm::vec3{0, 0,-1};

        ////////////////////////////////////////////////////////////////////////////////////////////////////

        auto velocity = glm::vec3{0, 0, 0};
        if(camera.locked)
        {
            if(glfwGetKey(window.handle, GLFW_KEY_W) == GLFW_PRESS) velocity += forward;
            if(glfwGetKey(window.handle, GLFW_KEY_S) == GLFW_PRESS) velocity -= forward;
            if(glfwGetKey(window.handle, GLFW_KEY_D) == GLFW_PRESS) velocity += right;
            if(glfwGetKey(window.handle, GLFW_KEY_A) == GLFW_PRESS) velocity -= right;
            if(glfwGetKey(window.handle, GLFW_KEY_E) == GLFW_PRESS) velocity += up;
            if(glfwGetKey(window.handle, GLFW_KEY_Q) == GLFW_PRESS) velocity -= up;
        }
        if(velocity != glm::vec3{0})
            velocity = {glm::normalize(velocity)};
        velocity *= camera.speed * (glfwGetKey(window.handle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? camera.boost : 1);
        camera.position += velocity * dt;

        for(; !listener.keyEvents.empty(); listener.keyEvents.pop())
        {
            auto const &event = listener.keyEvents.front();
            if(event.key == GLFW_KEY_ESCAPE && event.action == GLFW_PRESS)
            {
                camera.locked = !camera.locked;
                camera.firstTimeMovingMouse = true;
            }
        }
        glfwSetInputMode(window.handle, GLFW_CURSOR, camera.locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        for(; !listener.cursorPosEvents.empty(); listener.cursorPosEvents.pop())
        {
            auto const &event = listener.cursorPosEvents.front();
            glm::vec2 offset = glm::vec2(event.delta) * camera.sensitivity;

            if(!camera.firstTimeMovingMouse && camera.locked)
            {
                auto newOrientation = glm::normalize(
                    glm::angleAxis(glm::radians(offset.y), glm::vec3{1, 0, 0}) *
                    camera.orientation * 
                    glm::angleAxis(glm::radians(offset.x), glm::vec3{0, 1, 0})
                );
                if(glm::abs(glm::vec3(glm::inverse(glm::mat4_cast(newOrientation)) * glm::vec4{0,0,-1,0}).y) < 0.99)
                    camera.orientation = {newOrientation};
            }
            camera.firstTimeMovingMouse = false;
        }

        for(; !listener.scrollEvents.empty(); listener.scrollEvents.pop())
        {
            auto const &event = listener.scrollEvents.front();

            if(camera.locked)
            {
                camera.fov -= event.offset.y * 4;
                camera.fov = glm::clamp<float>(camera.fov, 0.2, 90);
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////

        camera.projMat = glm::perspective<float>(glm::radians(camera.fov), (float) window.size.x / (float) window.size.y, camera.znear, camera.zfar);
        camera.viewMat = glm::mat4_cast(glm::normalize(camera.orientation)) * glm::translate(glm::mat4(1.0f), -camera.position);

        // LOG_VAR(camera.position);
        // LOG_VAR(velocity);
        // LOG_VAR(glm::mat4_cast(glm::normalize(camera.orientation)));
        LOG_VAR(glm::mat4_cast(glm::normalize(camera.orientation)));
    }
}
