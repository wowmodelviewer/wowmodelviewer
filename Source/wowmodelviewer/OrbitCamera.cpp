#include "OrbitCamera.h"

#include "glm/gtc/matrix_transform.hpp"


#include "WoWModel.h"
#include "video.h"

const float CAMERA_DEFAULT_YAW = 0.0f;
const float CAMERA_DEFAULT_PITCH = 90.0f;
const float CAMERA_DEFAULT_RADIUS = 5.0f;
const float CAMERA_MIN_RADIUS = 0.5f;
// Large enough to frame whole WMOs/ADTs (buildings, zone pieces span hundreds-thousands of
// units) while staying inside the projection's far plane (~6400). The old 150 cap meant big
// WMOs could never be zoomed out far enough to see.
const float CAMERA_MAX_RADIUS = 5000.0f;

OrbitCamera::OrbitCamera()
  : pos_(glm::vec3(0.0f)),
  target_(glm::vec3(0.0f)),
  up_(glm::vec3(0.0f)),
  right_(glm::vec3(0.0f)),
  yaw_(0.0f),
  pitch_(0.0f),
  radius_(0.0f)
{
  reset();
}

glm::mat4 OrbitCamera::getViewMatrix()const
{
  return glm::lookAt(pos_, target_, up_);
}

void OrbitCamera::reset(const WoWModel * m)
{
  pos_ = glm::vec3(0.0f, 0.0f, 0.0f);
  target_ = glm::vec3(0.0f, 0.0f, 0.0f);
  up_ = glm::vec3(0.0f, 0.0f, 1.0f);
  yaw_ = CAMERA_DEFAULT_YAW;
  pitch_ = CAMERA_DEFAULT_PITCH;
  radius_ = CAMERA_DEFAULT_RADIUS;

  if (m != nullptr && !m->origVertices.empty())
  {
    // Auto-fit the whole model in view: take the axis-aligned bounds of the base mesh across ALL
    // axes and frame its bounding sphere. (The old code measured only the Z/height extent, so
    // anything wider or longer than it was tall -- quadrupeds, mounts, spread poses -- spilled out
    // the sides.) frameBounds() looks at the box centre and sizes the distance to the FOV.
    glm::vec3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
    for (const auto & v : m->origVertices)
    {
      // component-wise min/max (glm::min/max clash with the windows.h min/max macros)
      if (v.pos.x < mn.x) mn.x = v.pos.x;
      if (v.pos.y < mn.y) mn.y = v.pos.y;
      if (v.pos.z < mn.z) mn.z = v.pos.z;
      if (v.pos.x > mx.x) mx.x = v.pos.x;
      if (v.pos.y > mx.y) mx.y = v.pos.y;
      if (v.pos.z > mx.z) mx.z = v.pos.z;
    }
    const glm::vec3 center = (mn + mx) * 0.5f;
    const glm::vec3 d = mx - center;
    frameBounds(center, sqrtf(d.x * d.x + d.y * d.y + d.z * d.z));
    return;
  }

  updatePosition();
}


void OrbitCamera::frameBounds(const glm::vec3 & center, float boundingRadius)
{
  up_ = glm::vec3(0.0f, 0.0f, 1.0f);
  yaw_ = CAMERA_DEFAULT_YAW;
  pitch_ = CAMERA_DEFAULT_PITCH;
  target_ = center;

  // Distance at which a sphere of this radius fills the vertical FOV, plus a small margin.
  const float s = sinf(glm::radians(video.fov / 2.0f));
  setRadius((s > 0.0001f ? boundingRadius / s : boundingRadius) * 1.15f); // setRadius() clamps
  updatePosition();
}

void OrbitCamera::setLookAt(const glm::vec3& target)
{
  target_ = target;
  updatePosition();
}

void OrbitCamera::setRadius(float radius)
{
  radius_ = glm::clamp(radius, CAMERA_MIN_RADIUS, CAMERA_MAX_RADIUS);
  updatePosition();
}

void OrbitCamera::setPosition(const glm::vec3& position)
{
  pos_ = position;
}

void OrbitCamera::setYawAndPitch(float yaw, float pitch)
{
  yaw_ = yaw;

  if (yaw_ > 360.0f)
    yaw_ -= 360.0f;

  if (yaw_ < 0.0)
    yaw_ = 360.0f - yaw_;

  setPitch(pitch);
  updatePosition();
}

void OrbitCamera::setYaw(float yaw)
{
  yaw_ = yaw;
  updatePosition();
}

void OrbitCamera::setPitch(float pitch)
{
  pitch_ = glm::clamp(pitch, CAMERA_DEFAULT_PITCH - 90.0f + 0.1f, CAMERA_DEFAULT_PITCH + 90.0f - 0.1f);
  updatePosition();
}

void OrbitCamera::updatePosition()
{
  pos_.x = target_.x + radius_ * sinf(glm::radians(pitch_)) * cosf(glm::radians(yaw_));
  pos_.y = target_.y + radius_ * sinf(glm::radians(pitch_)) * sinf(glm::radians(yaw_));
  pos_.z = target_.z + radius_ * cosf(glm::radians(pitch_));
  right_ = glm::normalize(glm::cross(target_ - pos_, up_));
}