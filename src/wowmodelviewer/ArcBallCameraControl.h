/*
* ArcBallCameraControl.h
*
*  Created on: 24 july 2016
*
*/

#ifndef _ARCBALLCAMERACONTROL_H_
#define _ARCBALLCAMERACONTROL_H_

class ArcBallCamera;

class wxKeyEvent;
class wxMouseEvent;

class ArcBallCameraControl
{
  public:
    ArcBallCameraControl(ArcBallCamera &);

    void onMouse(wxMouseEvent& event);
    void onKey(wxKeyEvent &event);

  private:
    ArcBallCamera & m_camera;

    int m_xStart;
    int m_yStart;
};

#endif
