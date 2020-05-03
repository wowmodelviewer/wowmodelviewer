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

void Bone::calcMatrix(std::vector<Bone> & allbones, ssize_t anim, size_t time)
{
	if (calc)
		return;

	Matrix m;
  Quaternion q;

#if USE_ANIMATED > 0
  const auto useRot = rotOld.uses(anim);
  const auto useTrans = transOld.uses(anim);
  const auto useScale = scaleOld.uses(anim);
#else
  const auto useRot = rot.uses(anim);
  const auto useTrans = trans.uses(anim);
  const auto useScale = scale.uses(anim);
#endif
  
  if (useRot || useScale || useTrans || billboard)
  {
		m.translation(pivot);

    if(useTrans)
    {
#if USE_ANIMATED > 0
      const auto t = transOld.getValue(anim, time);
#else
      const auto t = trans.getValue(anim, time);
#endif
      m *= Matrix::newTranslation(t);
    }

    if(useRot)
    {
#if USE_ANIMATED > 0
      q = rotOld.getValue(anim, time);
#else
      q = rot.getValue(anim, time);
#endif
      m *= Matrix::newQuatRotate(q);
    }

    if(useScale)
    {
#if USE_ANIMATED > 0
      const auto s = scaleOld.getValue(anim, time);
#else
      const auto s = scale.getValue(anim, time);
#endif
      m *= Matrix::newScale(s);
    }

		if (billboard)
    {
			float modelview[16];
			glGetFloatv(GL_MODELVIEW_MATRIX, modelview);

			const auto vRight = Vec3D(modelview[0], modelview[4], modelview[8]) * -1;
			const auto vUp = Vec3D(modelview[1], modelview[5], modelview[9]); // Spherical billboarding
			//const auto vUp = Vec3D(0,1,0); // Cylindrical billboarding
			m.m[0][2] = vRight.x;
			m.m[1][2] = vRight.y;
			m.m[2][2] = vRight.z;
			m.m[0][1] = vUp.x;
			m.m[1][1] = vUp.y;
			m.m[2][1] = vUp.z;
		}

		m *= Matrix::newTranslation(pivot*-1.0f);

	} else m.unit();

	if (parent > -1)
  {
		allbones[parent].calcMatrix(allbones, anim, time);
		mat = allbones[parent].mat * m;
	} else mat = m;

	// transform matrix for normal vectors ... ??
	if (useRot)
  {
    if (parent>=0)
      mrot = allbones[parent].mrot * Matrix::newQuatRotate(q);
    else
      mrot = Matrix::newQuatRotate(q);
  }
  else
  {
    mrot.unit();
  }

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

#if USE_ANIMATED > 0
  transOld.init(b.translation, f, data);
  transOld.fix(fixCoordSystem);
  rotOld.init(b.rotation, f, data);
  rotOld.fix(fixCoordSystemQuat);
  scaleOld.init(b.scaling, f, data);
  scaleOld.fix(fixCoordSystem2);
#else
  trans.init(f, b.translation, data);
  rot.init(f, b.rotation, data);
  scale.init(f, b.scaling, data);
#endif
}

