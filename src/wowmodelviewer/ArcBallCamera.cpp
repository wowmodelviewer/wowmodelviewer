/*
* ArcBallCamera.cpp
*
*  Created on: 18 may 2016
*
*/

#include "ArcBallCamera.h"

#include "logger/Logger.h"

#include "quaternion.h"

#include <math.h>

#define DISPLAY_ORIGIN 0
#define DISPLAY_LOOKAT 0

ArcBallCamera::ArcBallCamera()
{
  reset();
}

void ArcBallCamera::reset()
{
  m_distance = 5.;
  m_minZoomDistance = 1;
  m_maxZoomDistance = 50;
  m_lookAt.reset();
  m_sceneWidth = 0;
  m_sceneHeight = 0;
  m_transform = Matrix::identity();

  // set 90 rotation on Y by default (make models front to screen)
  m_transform.m[0][0] = 0;
  m_transform.m[0][2] = -1;
  m_transform.m[2][0] = 1;
  m_transform.m[2][2] = 0;
 
  m_lastRot = Matrix::identity();

  m_startVec.reset();
}

void ArcBallCamera::refreshSceneSize(const int width, const int height)
{
  m_sceneWidth = width;
  m_sceneHeight = height;
}

void ArcBallCamera::zoomOut(const float speedfactor)
{
  m_distance += ((m_distance - m_minZoomDistance) / 5.0 * speedfactor);

  if (m_distance > m_maxZoomDistance)
    m_distance = m_maxZoomDistance;
}

void ArcBallCamera::zoomIn(const float speedfactor)
{
  m_distance -= ((m_distance - m_minZoomDistance) / 5.0 * speedfactor);

  if (m_distance < (m_minZoomDistance+1.0))
    m_distance = m_minZoomDistance + 1.0;
}

void ArcBallCamera::pan(const float a_xVal, const float a_yVal)
{
  m_lookAt.x += a_xVal;
  m_lookAt.y += a_yVal;
}

void ArcBallCamera::setup()
{
  glLoadIdentity();

  // apply zoom
  glTranslatef(0, 0, -m_distance);

  // apply pan
  glTranslatef(-m_lookAt.x, -m_lookAt.y, -m_lookAt.z);

  // apply rotation
  Matrix m = m_transform;
  m.transpose();
  glMultMatrixf(m);

#if DISPLAY_ORIGIN > 0
  // Useful for debug : display a big coord system at world origin
  glPushMatrix();
 // glLoadIdentity();
  glBegin(GL_LINES);
    // red X axis
    glColor3f(1.0,0.0,0.0);
    glVertex3f(0.0,0.0,0.0);
    glVertex3f(3,0.0,0.0);

    // green Y axis
    glColor3f(0.0,1.0,0.0);
    glVertex3f(0.0,0.0,0.0);
    glVertex3f(0.0,3,0.0);

    // blue Z axis
    glColor3f(0.0,0.0,1.0);
    glVertex3f(0.0,0.0,0.0);
    glVertex3f(0.0,0.0,3);
  glEnd();
  glPopMatrix();
#endif  
#if DISPLAY_LOOKAT > 0
  // Useful for debug : display a big coord system at camera look at position
  glPushMatrix();
  glTranslatef(m_lookAt.x, m_lookAt.y, m_lookAt.z);

  glBegin(GL_LINES); 
    // X Axis
    glColor3f(0.5, 0.0, 1.0);
    glVertex3f(0.0, 0.0, 0.0);
    glVertex3f(1.5, 0.0, 0.0);

    // Y axis
    glColor3f(1.0, 1.0, 0.0);
    glVertex3f(0.0, 0.0, 0.0);
    glVertex3f(0.0, 1.5, 0.0);

    // Z axis
    glColor3f(0.0, 1.0, 1.0);
    glVertex3f(0.0, 0.0, 0.0);
    glVertex3f(0.0, 0.0, 1.5);
  glEnd();
  glPopMatrix();
#endif
}

Vec3D ArcBallCamera::mapToSphere(const int x, const int y)
{
  Vec3D v = Vec3D(1.0*x / m_sceneWidth * 2 - 1.0,
                          1.0*y / m_sceneHeight * 2 - 1.0,
                          0);

  float length = v.lengthSquared();

  if (length <= 1)
    v.z = sqrt(1 - length);  // Pythagore
  else
    v = v.normalize();  // nearest point
  return v;
}

void ArcBallCamera::setStartPos(const int x, const int y)
{
  m_startVec = mapToSphere(x, y);
  m_lastRot = m_transform;
}

void ArcBallCamera::updatePos(const int x, const int y)
{
  int l_xCurrent = x;
  int l_yCurrent = y;

  Vec3D newVec = mapToSphere(x, y);

  //Compute the vector perpendicular to the begin and end vectors
  Vec3D perp = m_startVec % newVec;

  //Compute the length of the perpendicular vector
  if (perp.length() > 0.001)    //if its non-zero
  {
    m_rotation.x = perp.x;
    m_rotation.y = -perp.y;  // invert rotation around y axis, when moving right, rotation must decrease
    m_rotation.z = perp.z;
    m_rotation.w = m_startVec * newVec;
  }
  else                                    //if its zero
  {
    //The begin and end vectors coincide, so return an identity transform
    m_rotation.x = 0.;
    m_rotation.y = 0.;
    m_rotation.z = 0.;
    m_rotation.w = 0.;
  }

  Matrix curRot;
  curRot.quaternionRotate(m_rotation);
  curRot *= m_lastRot;
  m_transform = curRot;
}


void ArcBallCamera::autofit(const Vec3D & minp, const Vec3D & maxp, const float fov)
{
  // center view point on center of object
  m_lookAt.x = (minp.z + maxp.z) / 2.;
  m_lookAt.y = (minp.y + maxp.y) / 2.;
  m_lookAt.z = (minp.x + maxp.x) / 2.;

  // adjust current zoom based on object size
  float maxsize = max(max(maxp.x - minp.x, maxp.y - minp.y), maxp.z - minp.z);
  m_distance = abs((maxsize /2.)  / sinf(fov/2.*PI/180)) * 1.2;

  // set min /max zoom bounds based on optimal zoom
  m_minZoomDistance = m_distance / 10;
  m_maxZoomDistance = m_distance * 10;
}
