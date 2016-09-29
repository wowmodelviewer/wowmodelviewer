/*
* ArcBallCamera.cpp
*
*  Created on: 18 may 2016
*
*/

#include "ArcBallCamera.h"

#include "logger/Logger.h"

#include "quaternion.h"

#define DISTANCE_MAX 50
#define DISTANCE_MIN 1

#define DISPLAY_ORIGIN 0
#define DISPLAY_LOOKAT 0

ArcBallCamera::ArcBallCamera()
{
  reset();
}

void ArcBallCamera::reset()
{
  m_distance = 5.;
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
}

void ArcBallCamera::refreshSceneSize(int width, int height)
{
  m_sceneWidth = width;
  m_sceneHeight = height;
}

void ArcBallCamera::zoomOut()
{
  if (m_distance < DISTANCE_MAX)
  {
    m_distance += (m_distance - DISTANCE_MIN) / 5.0;
  }
}

void ArcBallCamera::zoomIn()
{
  if (m_distance > DISTANCE_MIN)
  {
    m_distance -= (m_distance - DISTANCE_MIN) / 5.0;
  }
}

void ArcBallCamera::pan(float a_xVal, float a_yVal)
{
  m_lookAt.x += a_xVal;
  m_lookAt.y -= a_yVal;
}

void ArcBallCamera::setup()
{
  glLoadIdentity();

  glTranslatef(-m_lookAt.x, -m_lookAt.y, -m_distance);

  glTranslatef(m_lookAt.x, m_lookAt.y, m_lookAt.z);
  Matrix m = m_transform;
  m.transpose();
  glMultMatrixf(m);
  glTranslatef(-m_lookAt.x, -m_lookAt.y, -m_lookAt.z);

#if DISPLAY_LOOKAT > 0
  // Useful for debug : display a big coord system at camera look at position
  glPushMatrix();

  glTranslatef(m_lookAt.x,m_lookAt.y,m_lookAt.z);
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
#if DISPLAY_ORIGIN > 0
  // Useful for debug : display a big coord system at camera look at position
  glPushMatrix();
  glBegin(GL_LINES); 
    // X Axis
    glColor3f(1.0, 0.0, 0.0);
    glVertex3f(0.0, 0.0, 0.0);
    glVertex3f(1.5, 0.0, 0.0);

    // Y axis
    glColor3f(0.0, 1.0, 0.0);
    glVertex3f(0.0, 0.0, 0.0);
    glVertex3f(0.0, 1.5, 0.0);

    // Z axis
    glColor3f(0.0, 0.0, 1.0);
    glVertex3f(0.0, 0.0, 0.0);
    glVertex3f(0.0, 0.0, 1.5);
  glEnd();
  glPopMatrix();
#endif
}

Vec3D ArcBallCamera::mapToSphere(int x, int y)
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

void ArcBallCamera::setStartPos(int x, int y)
{
  m_startVec = mapToSphere(x, y);
  m_lastRot = m_transform;
}

void ArcBallCamera::updatePos(int x, int y)
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


void ArcBallCamera::autofit(const Vec3D & min, const Vec3D & max)
{
  m_lookAt.x = (min.x + max.x) / 2.;
  m_lookAt.y = (min.y + max.y) / 2.;
  m_lookAt.z = (min.z + max.z) / 2.;
}
