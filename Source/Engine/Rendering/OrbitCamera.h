#pragma once

#include "glm/glm.hpp"

/// @brief Orbit camera that revolves around a target point.
///
/// Controlled by yaw, pitch, and radius parameters.  The view matrix is
/// recomputed from these each time getViewMatrix() is called.
class OrbitCamera
{
public:
	OrbitCamera();

	/// @brief Compute the current view matrix from yaw, pitch, and radius.
	glm::mat4 getViewMatrix() const;

	/// @brief Reset all parameters to defaults.
	void reset();

	/// @brief Reset the camera to frame a model whose bounding box spans [zMin, zMax].
	/// @param zMin        Minimum Z of the bounding box.
	/// @param zMax        Maximum Z of the bounding box.
	/// @param fovDegrees  Vertical field-of-view in degrees.
	void resetFromBounds(float zMin, float zMax, float fovDegrees);

	/// @brief Set the camera world position directly.
	void setPosition(const glm::vec3& position);

	/// @brief Current camera world position.
	glm::vec3 position() const { return pos_; }

	/// @brief Camera right vector (perpendicular to view direction and up).
	glm::vec3 right() const { return right_; }

	/// @brief Set the yaw angle (horizontal rotation around the target).
	void setYaw(float yaw);

	/// @brief Set the pitch angle (vertical elevation).
	void setPitch(float pitch);

	/// @brief Set both yaw and pitch simultaneously.
	void setYawAndPitch(float yaw, float pitch);

	/// @brief Current yaw angle in radians.
	float yaw() const { return yaw_; }

	/// @brief Current pitch angle in radians.
	float pitch() const { return pitch_; }

	/// @brief Set the point the camera orbits around.
	void setLookAt(const glm::vec3& target);

	/// @brief Current orbit target position.
	glm::vec3 lookAt() const { return target_; }

	/// @brief Set the distance from the camera to the target.
	void setRadius(float radius);

	/// @brief Current orbit radius (distance to target).
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
