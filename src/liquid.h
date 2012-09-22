#ifndef LIQUID_H
#define LIQUID_H

class Liquid;

#include <string>
#include "video.h"
#include "mpq.h"
#include "maptile.h"

const float LQ_DEFAULT_TILESIZE = CHUNKSIZE / 8.0f;

// handle liquids like oceans, lakes, rivers, slime, magma
class Liquid {

	int xtiles, ytiles;
	GLuint dlist;

	Vec3D pos;

	float tilesize;
	float ydir;
	float texRepeats;

	void initGeometry(MPQFile &f);
	void initTextures(wxString basename, int first, int last);

	int type;
	
	Vec3D col;
	int tmpflag;
	bool trans;

	int shader;

public:

	std::vector<GLuint> textures;

	Liquid(int x, int y, Vec3D base, float tilesize = LQ_DEFAULT_TILESIZE):
		xtiles(x), ytiles(y), pos(base), tilesize(tilesize), ydir(1.0f), shader(-1)
	{
	}
	~Liquid();

	//void init(MPQFile &f);
	void initFromTerrain(MPQFile &f, int flags);
	void initFromWMO(MPQFile &f, WMOMaterial &mat, bool indoor);

	void draw();


};



#endif
