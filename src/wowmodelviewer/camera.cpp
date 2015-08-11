
//***********************************************************************//
//                                                                       //
//		$Author:		John Steele		darjk@wowmodelviewer.org		 //
//																		 //
//		$Program:		Camera Class									 //
//																		 //
//		$Description:	Just a standard Camera Class Object to simplify  //
//																		 //
//		$Date:			25/01/07										 //
//***********************************************************************//

#include "camera.h"


CCamera::CCamera()
{
	Reset();
}


// Sets up the camera into position for the OpenGL scene
void CCamera::Setup()
{
	//The point at which the camera looks:
	Vec3D viewPoint = m_vPosition + m_vViewDir;

	gluLookAt(m_vPosition.x, m_vPosition.y, m_vPosition.z,			// Specifies the position of the eye point.
               viewPoint.x, viewPoint.y, viewPoint.z,				// Specifies the position of the reference point.
               m_vUpVector.x, m_vUpVector.y, m_vUpVector.z);		// Specifies the direction of the up vector.
}

void CCamera::Reset()
{
	//Init with standard OGL values:
	m_vPosition = Vec3D(0.0f, 0.0f, 1.0f);
	m_vViewDir = Vec3D(0.0f, 0.0f, -1.0f);
	m_vRightVector = Vec3D(1.0f, 0.0f, 0.0f);
	m_vUpVector = Vec3D(0.0f, 1.0f, 0.0f);

	//Only to be sure:
	m_vRotation = Vec3D(0,0,0);
}
/***************************************************************************************/


void CCamera::Move(Vec3D vDirection)
{
	m_vPosition = m_vPosition + vDirection;
}

void CCamera::RotateX(float Angle)
{
	m_vRotation.x += Angle;
	
	//Rotate viewdir around the right vector:
	m_vViewDir = ((m_vViewDir * cosf(Angle*PIOVER180f)) + (m_vUpVector * sinf(Angle*PIOVER180f))).normalize();

	//now compute the new UpVector (by cross product)
	m_vUpVector = (m_vViewDir % m_vRightVector) * -1;
}

void CCamera::RotateY (float Angle)
{
	m_vRotation.y += Angle;
	
	//Rotate viewdir around the up vector:
	m_vViewDir = ((m_vViewDir * cosf(Angle*PIOVER180f)) - (m_vRightVector * sinf(Angle*PIOVER180f))).normalize();

	//now compute the new RightVector (by cross product)
	m_vRightVector = (m_vViewDir % m_vUpVector);
}

void CCamera::RotateZ(float Angle)
{
	m_vRotation.z += Angle;
	
	//Rotate viewdir around the right vector:
	m_vRightVector = (m_vRightVector*cosf(Angle*PIOVER180f) + m_vUpVector*sinf(Angle*PIOVER180f)).normalize();

	//now compute the new UpVector (by cross product)
	m_vUpVector = (m_vViewDir % m_vRightVector) * -1;
}


void CCamera::MoveForward(float Distance)
{
	m_vPosition += (m_vViewDir * -Distance);
}

void CCamera::Strafe(float Distance)
{
	m_vPosition += (m_vRightVector * Distance);
}

void CCamera::MoveUpward(float Distance)
{
	m_vPosition += (m_vUpVector*Distance);
}
//---
