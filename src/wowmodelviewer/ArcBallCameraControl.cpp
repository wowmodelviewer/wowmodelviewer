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

}

void ArcBallCameraControl::onMouse(wxMouseEvent &event)
{
  int px = event.GetX();
  int py = event.GetY();
  int pz = event.GetWheelRotation();

  if (event.LeftDown())
  {
    m_camera.setStartPos(px, py);
  }
  else if (event.Dragging() && event.LeftIsDown())
  {
    m_camera.updatePos(px, py);
  }
  else if (event.GetEventType() == wxEVT_MOUSEWHEEL)
  {
    if (pz > 0)
      m_camera.zoomIn();
    else
      m_camera.zoomOut();
  }
}


/*

// from former control code in modelcanvas file, for reference until finalization

// mul = multiplier in which to multiply everything to achieve a sense of control over the amount to move stuff by
float mul = 1.0f;
if (event.m_shiftDown)
mul /= 10;
if (event.m_controlDown)
mul *= 10;
if (event.m_altDown)
mul *= 50;

if (wmo) {

//if (model->animManager)
//	mul *= model->animManager->GetSpeed(); //animSpeed;

if (event.ButtonDown()) {
mx = px;
my = py;

} else if (event.Dragging()) {
int dx = mx - px;
int dy = my - py;
mx = px;
my = py;

if (event.LeftIsDown() && event.RightIsDown()) {
wmo->viewpos.y -= dy*mul;
} else if (event.LeftIsDown()) {
wmo->viewrot.x -= dx*mul / 5;
wmo->viewrot.y -= dy*mul / 5;
} else if (event.RightIsDown()) {
wmo->viewrot.x -= dx*mul/5;
float f = cos(wmo->viewrot.y * piover180);
float sf = sin(wmo->viewrot.x * piover180);
float cf = cos(wmo->viewrot.x * piover180);
wmo->viewpos.x -= sf * mul * dy * f;
wmo->viewpos.z += cf * mul * dy * f;
wmo->viewpos.y += sin(wmo->viewrot.y * piover180) * mul * dy;
} else if (event.MiddleIsDown()) {
//?
}

} else if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
//?
}

} else if (model) {
if (model->animManager)
mul *= model->animManager->GetSpeed(); //animSpeed;

if (event.ButtonDown()) {
mx = px;
my = py;

vRot0 = model->rot;

vPos0 = model->pos;

} else if (event.Dragging()) {
int dx = mx - px;
int dy = my - py;

if (event.LeftIsDown()) {
model->rot.x = vRot0.x - (dy / 2.0f); // * mul);
model->rot.y = vRot0.y - (dx / 2.0f); // * mul);
} else if (event.RightIsDown()) {
mul /= 100.0f;

model->pos.x = vPos0.x - dx*mul;
model->pos.y = vPos0.y + dy*mul;
} else if (event.MiddleIsDown()) {
if (!event.m_altDown) {
mul = (mul / 20.0f) * dy;
Zoom(mul, false);
my = py;
} else {
mul = (mul / 1200.0f) * dy;
Zoom(mul, true);
my = py;
}
}

} else if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
if (pz != 0) {
mul = (mul / 120.0f) * pz;
if (!wxGetKeyState(WXK_ALT)) {
Zoom(mul, false);
} else {
mul /= 50.0f;
Zoom(mul, true);
}
}
}
} else if (adt) {
// Copied from WMO controls.

if (event.ButtonDown()) {
mx = px;
my = py;

} else if (event.Dragging()) {
int dx = mx - px;
int dy = my - py;
mx = px;
my = py;

if (event.LeftIsDown() && event.RightIsDown()) {
adt->viewpos.y -= dy*mul;
} else if (event.LeftIsDown()) {
adt->viewrot.x -= dx*mul/5;
adt->viewrot.y -= dy*mul/5;
} else if (event.RightIsDown()) {
adt->viewrot.x -= dx*mul/5;
float f = cos(adt->viewrot.y * piover180);
float sf = sin(adt->viewrot.x * piover180);
float cf = cos(adt->viewrot.x * piover180);
adt->viewpos.x -= sf * mul * dy * f;
adt->viewpos.z += cf * mul * dy * f;
adt->viewpos.y += sin(adt->viewrot.y * piover180) * mul * dy;
} else if (event.MiddleIsDown()) {
//?
}

} else if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
//?
}
}


//if (event.GetEventType() == wxEVT_ENTER_WINDOW)
//	SetFocus();
*/
