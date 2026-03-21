#include "ViewportController.h"

#include "InputAction.h"
#include "OrbitCamera.h"

#include <glm/glm.hpp>

void ViewportController::apply(const InputState& input, OrbitCamera& camera)
{
    // Orbit — combine yaw and pitch for a single position update
    if (input.orbitYaw != 0.0f || input.orbitPitch != 0.0f)
    {
        camera.setYawAndPitch(camera.yaw() + input.orbitYaw,
                              camera.pitch() + input.orbitPitch);
    }

    // Zoom — adjust orbital radius
    if (input.zoom != 0.0f)
    {
        camera.setRadius(camera.radius() + input.zoom);
    }

    // Pan — lateral along camera right vector, vertical along world Z
    if (input.panX != 0.0f || input.panZ != 0.0f)
    {
        const auto look  = camera.lookAt();
        const auto right = camera.right();
        camera.setLookAt(glm::vec3(look.x + right.x * input.panX,
                                   look.y + right.y * input.panX,
                                   look.z + input.panZ));
    }
}
