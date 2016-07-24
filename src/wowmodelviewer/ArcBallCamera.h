/*
* ArcBallCamera.h
*
*  Created on: 18 may 2016
*
*/

#ifndef _ARCBALLCAMERA_H_
#define _ARCBALLCAMERA_H_

#include "OpenGLHeaders.h"

#include "vec3d.h"

class ArcBallCamera
{
  public:
    ArcBallCamera();

    void setup();
    void reset();

    void updateAzimuth(float a_val);
    void updateInclination(float a_val);
    void rotateAroundViewDir(float a_val);

    void zoomIn();
    void zoomOut();
    void pan(float a_xVal, float a_yVal);

  protected:


  private:
    void updatePosition();

    // rotations
    float m_azimuth;
    float m_inclination;

    // zoom
    float m_distance;

    // camera definition
    Vec3D m_position;
    Vec3D m_lookAt;
    Vec3D m_up;

};

#endif