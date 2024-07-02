#pragma once

#include "glm/glm.hpp"

class WoWModel;

class OrbitCamera
{
public:
	OrbitCamera();

	glm::mat4 getViewMatrix() const;

	void reset(const WoWModel* m = nullptr);

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
