#ifndef CAMERA_H
#define CAMERA_H

//***********************************************************************//
//                                                                       //
//    $Author:    John Steele    darjk@wowmodelviewer.org     //
//                                     //
//    $Program:    Camera Class                   //
//                                     //
//    $Description:  Just a standard Camera Class Object to simplify  //
//            the viewport                   //
//                                     //
//    $Date:      25/01/07                     //
//                                                                       //
//***********************************************************************//


//#include <gl\glut.h>    // Need to include it here because the GL* types are required

#include "glm/glm.hpp"

//Note: All angles in degrees

class CCamera {
  private:

  glm::vec3 m_vViewDir;
  glm::vec3 m_vRightVector;
  glm::vec3 m_vUpVector;
  glm::vec3 m_vPosition;

  glm::vec3 m_vRotation;

  public:
  CCamera();
  void Reset();  //inits the default values)
  void Setup();  // Puts the camera into place.

  void Move(glm::vec3 Direction);
  void RotateX(float Angle);
  void RotateY(float Angle);
  void RotateZ(float Angle);

  void MoveForward(float Distance);
  void MoveUpward(float Distance);
  void Strafe(float Distance);
};



#endif