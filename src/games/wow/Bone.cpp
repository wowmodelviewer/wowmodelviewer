/*
 * Bone.cpp
 *
 *  Created on: 19 oct. 2013
 *
 */

#include "Bone.h"

#include "GL/glew.h"

#include "logger/Logger.h"

#define USE_ANIMATED 0

void Bone::calcMatrix(std::vector<Bone> & allbones, ssize_t anim, size_t time, bool rotate)
{
	if (calc)
		return;

	Matrix m;
  Quaternion q;

#if USE_ANIMATED > 0
	if (rotOld.uses(anim) || scale.uses(anim) || transOld.uses(anim) || billboard) {
#else
  if (rot.uses(anim) || scale.uses(anim) || trans.uses(anim) || billboard) {
#endif
		m.translation(pivot);

#if USE_ANIMATED > 0
    if (transOld.uses(anim))
    {
      Vec3D t = trans.getValue(anim, time);
#else
    if (trans.uses(anim))
    {
      Vec3D t = trans.getValue(anim, time);
      //t = transOld.getValue(anim, time);
#endif
      //LOG_INFO << boneDef.pivot.x << boneDef.pivot.y << boneDef.pivot.z << "-" << anim << time << "->" << q.x << q.y << q.z << q.w;
      m *= Matrix::newTranslation(t);
    }

#if USE_ANIMATED > 0
		if (rotOld.uses(anim) && rotate) {
      q = rotOld.getValue(anim, time);
#else
    if (rot.uses(anim) && rotate) {
      q = rot.getValue(anim, time);
#endif
     // LOG_INFO << boneDef.pivot.x << boneDef.pivot.y << boneDef.pivot.z << "-" << anim << time << "->" << q.x << q.y << q.z << q.w;
		  m *= Matrix::newQuatRotate(q);
		}

		if (scale.uses(anim))
			m *= Matrix::newScale(scale.getValue(anim, time));

		if (billboard) {
			float modelview[16];
			glGetFloatv(GL_MODELVIEW_MATRIX, modelview);

			Vec3D vRight = Vec3D(modelview[0], modelview[4], modelview[8]);
			Vec3D vUp = Vec3D(modelview[1], modelview[5], modelview[9]); // Spherical billboarding
			//Vec3D vUp = Vec3D(0,1,0); // Cylindrical billboarding
			vRight = vRight * -1;
			m.m[0][2] = vRight.x;
			m.m[1][2] = vRight.y;
			m.m[2][2] = vRight.z;
			m.m[0][1] = vUp.x;
			m.m[1][1] = vUp.y;
			m.m[2][1] = vUp.z;
		}

		m *= Matrix::newTranslation(pivot*-1.0f);

	} else m.unit();

	if (parent > -1) {
		allbones[parent].calcMatrix(allbones, anim, time, rotate);
		mat = allbones[parent].mat * m;
	} else mat = m;

	// transform matrix for normal vectors ... ??
	if (rot.uses(anim) && rotate) {
		if (parent>=0)
			mrot = allbones[parent].mrot * Matrix::newQuatRotate(q);
		else
			mrot = Matrix::newQuatRotate(q);
	} else mrot.unit();

	transPivot = mat * pivot;

	calc = true;
}

void Bone::initV3(GameFile & f, M2CompBone &b, const modelAnimData & data)
{
	calc = false;

	parent = b.parent;
	pivot = fixCoordSystem(b.pivot);
	billboard = (b.flags & MODELBONE_BILLBOARD) != 0;

	boneDef = b;
  LOG_INFO << "b.translation.seq" << b.translation.seq;
  LOG_INFO << "b.rotation.seq" << b.rotation.seq;
  LOG_INFO << "b.scaling.seq" << b.scaling.seq;

  transOld.init(b.translation, f, data);
  transOld.fix(fixCoordSystem);
  rotOld.init(b.rotation, f, data);
  rotOld.fix(fixCoordSystemQuat);

  trans.init(f, b.translation, data);
  rot.init(f, b.rotation, data);

  scale.init(b.scaling, f, data);
	scale.fix(fixCoordSystem2);
}

