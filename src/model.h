#ifndef MODEL_H
#define MODEL_H

// C++ files
#include <vector>
//#include <stdlib.h>
//#include <crtdbg.h>

// Our files
#include "video.h"
#include "displayable.h"

#include "vec3d.h"

#include <wx/txtstrm.h>


#include "AnimManager.h"

#include "manager.h"
#include "mpq.h"

#include "modelheaders.h"
#include "quaternion.h"
#include "matrix.h"
#include "ModelAttachment.h"
#include "ModelCamera.h"
#include "ModelRenderPass.h"

#include "animated.h"
#include "particle.h"

#include "enums.h"

class Bone;


class TextureAnim {
public:
	Animated<Vec3D> trans, rot, scale;

	Vec3D tval, rval, sval;

	void calc(ssize_t anim, size_t time);
	void init(MPQFile &f, ModelTexAnimDef &mta, uint32 *global);
	void setup(ssize_t anim);
};

struct ModelColor {
	Animated<Vec3D> color;
	AnimatedShort opacity;

	void init(MPQFile &f, ModelColorDef &mcd, uint32 *global);
};

struct ModelTransparency {
	AnimatedShort trans;

	void init(MPQFile &f, ModelTransDef &mtd, uint32 *global);
};




struct ModelLight {
	ssize_t type;		// Light Type. MODELLIGHT_DIRECTIONAL = 0 or MODELLIGHT_POINT = 1
	ssize_t parent;		// Bone Parent. -1 if there isn't one.
	Vec3D pos, tpos, dir, tdir;
	Animated<Vec3D> diffColor, ambColor;
	Animated<float> diffIntensity, ambIntensity, AttenStart, AttenEnd;
	Animated<int> UseAttenuation;

	void init(MPQFile &f, ModelLightDef &mld, uint32 *global);
	void setup(size_t time, GLuint l);
};


class ModelEvent {
	ModelEventDef def;
public:
	void init(MPQFile &f, ModelEventDef &mad, uint32 *global);

	friend std::ostream& operator<<(std::ostream& out, ModelEvent& v)
	{
		out << "		<id>" << v.def.id[0] << v.def.id[1] << v.def.id[2] << v.def.id[3] << "</id>" << endl;
		out << "		<dbid>" << v.def.dbid << "</dbid>" << endl;
		out << "		<bone>" << v.def.bone << "</bone>" << endl;
		out << "		<pos>" << v.def.pos << "</pos>" << endl;
		out << "		<type>" << v.def.type << "</type>" << endl;
		out << "		<seq>" << v.def.seq << "</seq>" << endl;
		out << "		<nTimes>" << v.def.nTimes << "</nTimes>" << endl;
		out << "		<ofsTimes>" << v.def.ofsTimes << "</ofsTimes>" << endl;
		return out;
	}
};

class Model: public ManagedItem, public Displayable
{
	// VBO Data
	GLuint vbuf, nbuf, tbuf;
	size_t vbufsize;

	// Non VBO Data
	GLuint dlist;
	bool forceAnim;

	void init(MPQFile &f);
	void displayHeader(ModelHeader & a_header);

	inline void drawModel();
	void initCommon(MPQFile &f);
	bool isAnimated(MPQFile &f);
	void initAnimated(MPQFile &f);
	void initStatic(MPQFile &f);

	void animate(ssize_t anim);
	void calcBones(ssize_t anim, size_t time);

	void lightsOn(GLuint lbase);
	void lightsOff(GLuint lbase);

	uint16 *boundTris;

public:
	bool animGeometry,animTextures,animBones;

	TextureAnim		*texAnims;
	uint32			*globalSequences;
	ModelColor		*colors;
	ModelTransparency *transparency;
	ModelLight		*lights;
	ParticleSystem	*particleSystems;
	RibbonEmitter	*ribbons;
	ModelEvent		*events;
	Vec3D *bounds;

public:
	// Raw Data
	ModelVertex *origVertices;
	size_t *IndiceToVerts;

	Vec3D *vertices, *normals;
	Vec2D *texCoords;
	uint16 *indices;
	uint32 nIndices;
	wxArrayString TextureList;
	// --

public:
	Model(wxString name, bool forceAnim=false);
	~Model();

	ModelHeader header;
	std::vector<ModelCamera> cam;
	wxString modelname;
	wxString lodname;
	
	std::vector<ModelRenderPass> passes;
	std::vector<ModelGeoset> geosets;

	// ===============================
	// Toggles
	bool *showGeosets;
	bool showBones;
	bool showBounds;
	bool showWireframe;
	bool showParticles;
	bool showModel;
	bool showTexture;
	float alpha;

	// Position and rotation vector
	Vec3D pos;
	Vec3D rot;

	//
	bool ok;
	bool ind;
	bool hasCamera;
	bool hasParticles;
	bool isWMO;
	bool isMount;
	bool animated;

	// Misc values
	float rad;
	float trans;
	// -------------------------------

	// ===============================
	// Bone & Animation data
	// ===============================
	ModelAnimation *anims;
	int16 *animLookups;
	AnimManager *animManager;
	Bone *bones;
	MPQFile *animfiles;

	size_t currentAnim;
	bool animcalc;
	size_t anim, animtime;

	void reset() { 
		animcalc = false; 
	}

	
	void update(int dt) {  // (float dt)
		if (animated)
			animManager->Tick(dt);

		updateEmitters((dt/1000.0f)); 
	};
	// -------------------------------

	// ===============================
	// Texture data
	// ===============================
	TextureID *textures;
	int specialTextures[TEXTURE_MAX];
	GLuint replaceTextures[TEXTURE_MAX];
	bool useReplaceTextures[TEXTURE_MAX];
	// -------------------------------

	// ===============================
	// 

	// ===============================
	// Rendering Routines
	// ===============================
	void drawBones();
	void drawBoundingVolume();
	void drawParticles();
	void draw();
	// -------------------------------
	
	void updateEmitters(float dt);
	void setLOD(MPQFile &f, int index);

	void setupAtt(int id);
	void setupAtt2(int id);

	std::vector<ModelAttachment> atts;
	static const size_t ATT_MAX = 60;
	int16 attLookup[ATT_MAX];
	int16 keyBoneLookup[BONE_MAX];

	ModelType modelType;
	CharModelDetails charModelDetails;

	friend struct ModelRenderPass;
};


#endif
