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

ArcBallCamera::ArcBallCamera()
{
  reset();
}

void ArcBallCamera::reset()
{
  m_position.reset();
  m_up.reset();
  m_up.y = 1.;
  m_distance = 5.;
  m_lookAt.reset();
  m_azimuth = 90.;
  m_inclination = 90.;
  updatePosition();
}

void ArcBallCamera::zoomOut()
{
  if (m_distance < DISTANCE_MAX)
  {
    m_distance += (m_distance - DISTANCE_MIN) / 5.0;
    updatePosition();
  }
}

void ArcBallCamera::zoomIn()
{
  if (m_distance > DISTANCE_MIN)
  {
    m_distance -= (m_distance - DISTANCE_MIN) / 5.0;
    updatePosition();
  }
}

void ArcBallCamera::pan(float a_xVal, float a_yVal)
{
  m_lookAt.x += a_xVal;
  m_lookAt.y -= a_yVal;
}


void ArcBallCamera::updatePosition()
{
  Vec3D viewDir = m_position - m_lookAt;

  viewDir.x = sinf(m_inclination * PI / 180.) * sinf(m_azimuth * PI / 180.);
  viewDir.y = cosf(m_inclination * PI / 180.);
  viewDir.z = sinf(m_inclination * PI / 180.) * cosf(m_azimuth * PI / 180.);
  viewDir.normalize();

  m_position = viewDir * m_distance + m_lookAt;
}

void ArcBallCamera::updateAzimuth(float a_val)
{
  m_azimuth += (-a_val); // invert sign => when moving right, azimuth decrease

  m_azimuth = (int)m_azimuth % 360;   // lock angle between 0 and 2 PI
  updatePosition();
}

void ArcBallCamera::updateInclination(float a_val)
{
  m_inclination += (-a_val); // invert sign => when moving down, inclination increase

  // lock movement between -179 and +179 degrees
  if (m_inclination < -179)
    m_inclination = 180;
  else if (m_inclination > 179)
    m_inclination = -180;

  // update up camera vector depending on azimuth value
  if (m_inclination < 1 || m_inclination > 179)
    m_up.y = -1;
  else
    m_up.y = 1;
 
  updatePosition();
}

void ArcBallCamera::rotateAroundViewDir(float a_val)
{
  // not yet implemented
  //	m_angleAroundViewDir += a_val;
  //	updatePosition();
}

void ArcBallCamera::setup()
{
  glLoadIdentity();

  /*LOG_INFO << __FUNCTION__;
  LOG_INFO << "m_position = " << m_position.x << " " << m_position.y << " " << m_position.z;
  LOG_INFO << "m_lookAt = " << m_lookAt.x << " " << m_lookAt.y << " " << m_lookAt.z;
  LOG_INFO << "m_up = " << m_up.x << " " << m_up.y << " " << m_up.z;*/

  gluLookAt(m_position.x, m_position.y, m_position.z,
            m_lookAt.x, m_lookAt.y, m_lookAt.z,
            m_up.x, m_up.y, m_up.z);

  
  //Usefull for debug : display a big coord system at camera look at position
  /*
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
  */
}

