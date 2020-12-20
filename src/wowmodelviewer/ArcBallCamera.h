/*
* ArcBallCamera.h
*
*  Created on: 18 may 2016
*
*/

#ifndef _ARCBALLCAMERA_H_
#define _ARCBALLCAMERA_H_

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

class ArcBallCamera
{
  public:
    ArcBallCamera();

    void setup();
    void reset();

    void refreshSceneSize(const int width, const int height);

    void zoomIn(const float speedfactor = 1.);
    void zoomOut(const float speedfactor = 1.);

    void pan(const float a_xVal, const float a_yVal);

    void setStartPos(const int x, const int y);
    void updatePos(const int x, const int y);

    void autofit(const glm::vec3 & min, const glm::vec3 & max, const float fov);
    
  protected:


  private:
    void updatePosition();
    glm::vec3 mapToSphere(const int x, const int y);

    // zoom
    float m_distance;
    float m_minZoomDistance;
    float m_maxZoomDistance;

    // camera definition
    glm::vec3 m_lookAt;
    glm::vec3 m_modelCenter;

    // scene size
    int m_sceneWidth;
    int m_sceneHeight;

    glm::vec3 m_startVec;
    glm::fquat m_rotation;
    glm::mat4 m_lastRot;
    glm::mat4 m_transform;

};

#endif