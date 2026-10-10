#ifndef _ORBITCAMERA_H_
#define _ORBITCAMERA_H_

#include "glm/glm.hpp"

class WoWModel;

class OrbitCamera
{
public:
  OrbitCamera();

  glm::mat4 getViewMatrix() const;

  void reset(const WoWModel * m = nullptr);

  // Frame an arbitrary bounding sphere (center + radius, in render space). Used for WMOs,
  // which aren't WoWModels and span far more than the default view, so the camera distance is
  // set so the whole sphere fits the vertical field of view.
  void frameBounds(const glm::vec3 & center, float boundingRadius);

  void setPosition(const glm::vec3& position);
  glm::vec3 position() const { return pos_; }

  glm::vec3 right() const { return right_; }

  void setYaw(float yaw);
  void setPitch(float pitch);
  void setYawAndPitch(float yaw, float pitch);
  float yaw() const { return yaw_; }
  float pitch() const { return pitch_; }

  void setLookAt(const glm::vec3& target);
  glm::vec3 lookAt() const { return target_; }

  void setRadius(float radius);
  float radius() const { return radius_; }

private:
  void updatePosition();

  glm::vec3 pos_;
  glm::vec3 target_;
  glm::vec3 up_;
  glm::vec3 right_;

  float yaw_;
  float pitch_;
  float radius_;
};
#endif // _ORBITCAMERA_H_