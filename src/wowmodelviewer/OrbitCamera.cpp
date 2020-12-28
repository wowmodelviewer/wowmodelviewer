#include "OrbitCamera.h"

#include "glm/gtc/matrix_transform.hpp"


#include "WoWModel.h"
#include "video.h"

const float CAMERA_DEFAULT_YAW = 0.0f;
const float CAMERA_DEFAULT_PITCH = 90.0f;
const float CAMERA_DEFAULT_RADIUS = 5.0f;
const float CAMERA_MIN_RADIUS = 0.5f;
const float CAMERA_MAX_RADIUS = 80.0f;

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

  if (m != nullptr)
  {
    // adjust camera settings based on current model loaded
    // Look at is set to middle of z coords
    // zoom is adjusted to have model's z fitting screen
    float zmin = 0., zmax = 0.;

    for (const auto & v : m->origVertices)
    {
      if (v.pos.z < zmin)
        zmin = v.pos.z;

      if (v.pos.z > zmax)
        zmax = v.pos.z;
    }

    target_.z = (zmin + zmax) / 2.0f;
    setRadius((zmin + zmax) / 2.0f * 1.3f / sinf(glm::radians(video.fov / 2.0f)));
  }

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