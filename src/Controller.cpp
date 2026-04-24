#include "Controller.hpp"
#include "Logging.hpp"

#define CAMERA_COMPONENTS Controller::Camera, io::EventListener

Entity Controller::createCamera(Registry &reg, glm::vec3 pos, glm::vec3 target)
{
    auto e = reg.create<CAMERA_COMPONENTS>();
    auto up = glm::abs(glm::dot(glm::normalize(target - pos), glm::vec3{0,1,0})) > 0.99 ? glm::vec3{1,0,0} : glm::vec3{0,1,0};
    auto q = glm::quat_cast(glm::lookAt(pos, target, up));
    e.get<Controller::Camera>() = {
        .window = reg.view<Window>().at(0),
        .position = pos,
        // .pitchYaw = glm::vec2(glm::degrees(glm::pitch(q)), glm::degrees(glm::yaw(q)))
        .orientation = q,
    };
    return e;
}
static void updateOrientation(glm::vec2 offset, glm::quat &q)
{
    auto newOrientation = glm::normalize(
        glm::angleAxis(glm::radians(offset.y), glm::vec3{1, 0, 0}) *
        q * 
        glm::angleAxis(glm::radians(offset.x), glm::vec3{0, 1, 0})
    );
    if(glm::abs(glm::vec3(glm::inverse(glm::mat4_cast(newOrientation)) * glm::vec4{0,0,-1,0}).y) < 0.99)
        q = {newOrientation};
}

void Controller::update(Registry &reg, float dt)
{
    for(Entity eCamera : reg.view<CAMERA_COMPONENTS>())
    {
        auto &camera = eCamera.get<Controller::Camera>();
        auto &listener = eCamera.get<io::EventListener>();
        auto &window = camera.window.get<Window>();

        auto invView = glm::mat3(glm::inverse(camera.viewMat));
        glm::vec3 right   = invView * glm::vec3{1, 0, 0};
        glm::vec3 up      = invView * glm::vec3{0, 1, 0};
        glm::vec3 forward = invView * glm::vec3{0, 0,-1};

        ////////////////////////////////////////////////////////////////////////////////////////////////////

        glm::vec3 velocity{0, 0, 0};
        glm::vec2 arrowOffset(0, 0);
        if(camera.locked)
        {
            if(glfwGetKey(window.handle, GLFW_KEY_W) == GLFW_PRESS) velocity += forward;
            if(glfwGetKey(window.handle, GLFW_KEY_S) == GLFW_PRESS) velocity -= forward;
            if(glfwGetKey(window.handle, GLFW_KEY_D) == GLFW_PRESS) velocity += right;
            if(glfwGetKey(window.handle, GLFW_KEY_A) == GLFW_PRESS) velocity -= right;
            if(glfwGetKey(window.handle, GLFW_KEY_E) == GLFW_PRESS) velocity += up;
            if(glfwGetKey(window.handle, GLFW_KEY_Q) == GLFW_PRESS) velocity -= up;

            if(glfwGetKey(window.handle, GLFW_KEY_UP   ) == GLFW_PRESS) arrowOffset -= glm::vec2{0,1} * camera.sensitivity * 0.5f;
            if(glfwGetKey(window.handle, GLFW_KEY_DOWN ) == GLFW_PRESS) arrowOffset += glm::vec2{0,1} * camera.sensitivity * 0.5f;
            if(glfwGetKey(window.handle, GLFW_KEY_RIGHT) == GLFW_PRESS) arrowOffset += glm::vec2{1,0} * camera.sensitivity * 0.5f;
            if(glfwGetKey(window.handle, GLFW_KEY_LEFT ) == GLFW_PRESS) arrowOffset -= glm::vec2{1,0} * camera.sensitivity * 0.5f;
        }
        if(velocity != glm::vec3{0})
            velocity = {glm::normalize(velocity)};
        velocity *= camera.speed * (glfwGetKey(window.handle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? camera.boost : 1);
        camera.position += velocity * dt;
        
        updateOrientation(arrowOffset, camera.orientation);

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
                updateOrientation(offset, camera.orientation);
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
        // camera.viewMat = 
        //     glm::rotate(glm::mat4{1.0f}, glm::radians(camera.pitchYaw.x), {1,0,0}) * 
        //     glm::rotate(glm::mat4{1.0f}, glm::radians(camera.pitchYaw.y), {0,1,0}) * 
        //     glm::translate(glm::mat4(1.0f), -camera.position);

        // LOG_VAR(camera.position);
        // LOG_VAR(velocity);
        // LOG_VAR(glm::mat4_cast(glm::normalize(camera.orientation)));
        // LOG_VAR(camera.viewMat);
    }
}
