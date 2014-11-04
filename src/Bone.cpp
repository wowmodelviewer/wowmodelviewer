/*
 * Bone.cpp
 *
 *  Created on: 19 oct. 2013
 *
 */

#include "Bone.h"

#include "util.h" // fixCoordSystemX

#include "GL/glew.h"

void Bone::calcMatrix(Bone *allbones, ssize_t anim, size_t time, bool rotate)
{
	if (calc)
		return;

	Matrix m;
	Quaternion q;

	bool tr = rot.uses(anim) || scale.uses(anim) || trans.uses(anim) || billboard;
	if (tr) {
		m.translation(pivot);

		if (trans.uses(anim)) {
			Vec3D tr = trans.getValue(anim, time);
			m *= Matrix::newTranslation(tr);
		}

		if (rot.uses(anim) && rotate) {
			q = rot.getValue(anim, time);
			m *= Matrix::newQuatRotate(q);
		}

		if (scale.uses(anim)) {
			Vec3D sc = scale.getValue(anim, time);
			m *= Matrix::newScale(sc);
		}

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

void Bone::initV3(GameFile & f, ModelBoneDef &b, uint32 *global, std::vector<GameFile *> & animfiles)
{
	calc = false;

	parent = b.parent;
	pivot = fixCoordSystem(b.pivot);
	billboard = (b.flags & MODELBONE_BILLBOARD) != 0;
	//billboard = false;

	boneDef = b;

	//std::cout << __FUNCTION__ << "f.getBuffer() = " << f.getBuffer() << std::endl;

	trans.init(b.translation, f, global, animfiles);
	rot.init(b.rotation, f, global, animfiles);
	scale.init(b.scaling, f, global, animfiles);
	trans.fix(fixCoordSystem);
	rot.fix(fixCoordSystemQuat);
	scale.fix(fixCoordSystem2);
}

void Bone::initV2(GameFile * f, ModelBoneDef &b, uint32 *global)
{
	calc = false;

	parent = b.parent;
	pivot = fixCoordSystem(b.pivot);
	billboard = (b.flags & MODELBONE_BILLBOARD) != 0;
	//billboard = false;

	boneDef = b;

	trans.init(b.translation, f, global);
	rot.init(b.rotation, f, global);
	scale.init(b.scaling, f, global);
	trans.fix(fixCoordSystem);
	rot.fix(fixCoordSystemQuat);
	scale.fix(fixCoordSystem2);
}

