/*
* ArcBallCameraControl.cpp
*
*  Created on: 24 july 2016
*
*/

#include "ArcBallCameraControl.h"

#include "ArcBallCamera.h"
#include "quaternion.h"

#include <wx/event.h>

#include "logger/Logger.h"

ArcBallCameraControl::ArcBallCameraControl(ArcBallCamera & cam) :
m_camera(cam)
{

}

void ArcBallCameraControl::onKey(wxKeyEvent &event)
{
  if (event.GetKeyCode() == WXK_UP)
  {
    m_camera.pan(0, 0.1);
  }
  else if (event.GetKeyCode() == WXK_DOWN)
  {
    m_camera.pan(0, -0.1);
  }
  else if (event.GetKeyCode() == WXK_LEFT)
  {
    m_camera.pan(-0.1, 0);
  }
  else if (event.GetKeyCode() == WXK_RIGHT)
  {
    m_camera.pan(0.1, 0);
  }
}

void ArcBallCameraControl::onMouse(wxMouseEvent &event)
{
  int px = event.GetX();
  int py = event.GetY();
  int pz = event.GetWheelRotation();

  // mul = multiplier in which to multiply everything to achieve a sense of control over the amount to move stuff by
  float mul = 1.0f;
  if (event.m_shiftDown)
    mul /= 10;
  if (event.m_controlDown)
    mul *= 10;
  if (event.m_altDown)
    mul *= 50;

  // left button management
  if (event.LeftDown())
  {
    m_camera.setStartPos(px, py);
  }
  else if (event.Dragging() && event.LeftIsDown())   // rotation
  {
    m_camera.updatePos(px*mul, py*mul);
  }
  // right button management
  else if (event.RightDown())
  {
    m_xStart = px;
    m_yStart = py;
  }
  else if (event.Dragging() && event.RightIsDown())  // pan
  {
    m_camera.pan(((m_xStart - px) / 100.)*mul, -((m_yStart - py) / 100.)*mul);
    m_xStart = px;
    m_yStart = py;
  }
  else if (event.GetEventType() == wxEVT_MOUSEWHEEL) // zoom
  {
    if (pz > 0)
      m_camera.zoomIn(mul);
    else
      m_camera.zoomOut(mul);
  }
}
