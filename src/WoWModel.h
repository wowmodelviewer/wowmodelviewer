#ifndef _WOWMODEL_H
#define _WOWMODEL_H

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

#include "core/Model.h"

class Bone;
struct ModelColor;
class ModelEvent;
struct ModelLight;
struct ModelTransparency;
class TextureAnim;
class GameFile;
class CASCFile;

class WoWModel: public ManagedItem, public Displayable, public Model
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
	void initCommon(GameFile * f);
	bool isAnimated(GameFile * f);
	void initAnimated(GameFile * f);
	void initStatic(GameFile * f);

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
	WoWModel(wxString name, bool forceAnim=false);
	~WoWModel();

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
	bool isHD;

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
	std::vector<GameFile *> animfiles;

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
	void setLOD(GameFile * f, int index);

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
