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

    void refreshSceneSize(int width, int height);

    void zoomIn();
    void zoomOut();
    void pan(float a_xVal, float a_yVal);

    void setStartPos(int x, int y);
    void updatePos(int x, int y);

    void autofit(const Vec3D & min, const Vec3D & max);
    
  protected:


  private:
    void updatePosition();
    Vec3D mapToSphere(int x, int y);

    // zoom
    float m_distance;

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