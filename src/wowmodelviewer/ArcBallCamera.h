/*
* ArcBallCamera.h
*
*  Created on: 18 may 2016
*
*/

#ifndef _ARCBALLCAMERA_H_
#define _ARCBALLCAMERA_H_

#include "OpenGLHeaders.h"

#include "matrix.h"
#include "vec3d.h"

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

    void autofit(const Vec3D & min, const Vec3D & max, const float fov);
    
  protected:


  private:
    void updatePosition();
    Vec3D mapToSphere(const int x, const int y);

    // zoom
    float m_distance;
    float m_minZoomDistance;
    float m_maxZoomDistance;

    // camera definition
    Vec3D m_lookAt;

    // scene size
    int m_sceneWidth;
    int m_sceneHeight;

    Vec3D m_startVec;
    Quaternion m_rotation;
    Matrix m_lastRot;
    Matrix m_transform;

};

#endif