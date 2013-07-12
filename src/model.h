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

class Model;
class Bone;
Vec3D fixCoordSystem(Vec3D v);

#include "manager.h"
#include "mpq.h"

#include "modelheaders.h"
#include "quaternion.h"
#include "matrix.h"

#include "animated.h"
#include "particle.h"

#include "enums.h"

// This will be our animation manager
// instead of using a STL vector or list or table, etc.  
// Decided to just limit it upto 4 animations to loop through - for experimental testing.
// The second id and loop count will later be used for being able to have a primary and secondary animation.

// Currently, this is more of a "Wrapper" over the existing code
// but hopefully over time I can remove and re-write it so this is the core.
struct AnimInfo {
	short Loops;
	size_t AnimID;
};

class AnimManager {
	ModelAnimation *anims;
	
	bool Paused;
	bool AnimParticles;

	AnimInfo animList[4];

	size_t Frame;		// Frame number we're upto in the current animation
	size_t TotalFrames;

	ssize_t AnimIDSecondary;
	size_t FrameSecondary;
	size_t SecondaryCount;

	ssize_t AnimIDMouth;
	size_t FrameMouth;
	
	short Count;			// Total index of animations
	short PlayIndex;		// Current animation index we're upto
	short CurLoop;			// Current loop that we're upto.

	ssize_t TimeDiff;			// Difference in time between each frame

	float Speed;			// The speed of which to multiply the time given for Tick();
	float mouthSpeed;

public:
	AnimManager(ModelAnimation *anim);
	~AnimManager();
	
	void SetCount(int count);
	void AddAnim(unsigned int id, short loop); // Adds an animation to our array.
	void SetAnim(short index, unsigned int id, short loop); // sets one of the 4 existing animations and changes it (not really used currently)
	
	void SetSecondary(int id) {
		AnimIDSecondary = id;
		FrameSecondary = anims[id].timeStart;
	}
	void ClearSecondary() { AnimIDSecondary = -1; }
	ssize_t GetSecondaryID() { return AnimIDSecondary; }
	size_t GetSecondaryFrame() { return FrameSecondary; }
	void SetSecondaryCount(int count) {	SecondaryCount = count; }
	size_t GetSecondaryCount() { return SecondaryCount; }

	// For independent mouth movement.
	void SetMouth(int id) {
		AnimIDMouth = id;
		FrameMouth = anims[id].timeStart;
	}
	void ClearMouth() { AnimIDMouth = -1; }
	ssize_t GetMouthID() { return AnimIDMouth; }
	size_t GetMouthFrame() { return FrameMouth; }
	void SetMouthSpeed(float speed) {
		mouthSpeed = speed;
	}

	void Play(); // Players the animation, and reconfigures if nothing currently inputed
	void Stop(); // Stops and resets the animation
	void Pause(bool force = false); // Toggles 'Pause' of the animation, use force to pause the animation no matter what.
	
	void Next(); // Plays the 'next' animation or loop
	void Prev(); // Plays the 'previous' animation or loop

	int Tick(int time);

	size_t GetFrameCount();
	size_t GetFrame() {return Frame;}
	void SetFrame(size_t f);
	void SetSpeed(float speed) {Speed = speed;}
	float GetSpeed() {return Speed;}
	
	void PrevFrame();
	void NextFrame();

	void Clear();
	void Reset() { Count = 0; }

	bool IsPaused() { return Paused; }
	bool IsParticlePaused() { return !AnimParticles; }
	void AnimateParticles() { AnimParticles = true; }

	size_t GetAnim() { return animList[PlayIndex].AnimID; }

	ssize_t GetTimeDiff();
	void SetTimeDiff(ssize_t i);
};

class Bone {
public:
	Animated<Vec3D> trans;
	//Animated<Quaternion> rot;
	Animated<Quaternion, PACK_QUATERNION, Quat16ToQuat32> rot;
	Animated<Vec3D> scale;

	Vec3D pivot, transPivot;
	int16 parent;

	bool billboard;
	Matrix mat;
	Matrix mrot;

	ModelBoneDef boneDef;

	bool calc;
	Model *model;
	void calcMatrix(Bone* allbones, ssize_t anim, size_t time, bool rotate=true);
	void initV3(MPQFile &f, ModelBoneDef &b, uint32 *global, MPQFile *animfiles);
	void initV2(MPQFile &f, ModelBoneDef &b, uint32 *global);
};

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

struct ModelRenderPass {
	uint32 indexStart, indexCount, vertexStart, vertexEnd;
	//TextureID texture, texture2;
	int tex;
	bool useTex2, useEnvMap, cull, trans, unlit, noZWrite, billboard;
	float p;
	
	int16 texanim, color, opacity, blendmode;

	// Geoset ID
	int geoset;

	// texture wrapping
	bool swrap, twrap;

	// colours
	Vec4D ocol, ecol;

	bool init(Model *m);
	void deinit();

	bool operator< (const ModelRenderPass &m) const
	{
		// This is the old sort order method which I'm pretty sure is wrong - need to try something else.
		// Althogh transparent part should be displayed later, but don't know how to sort it
		// And it will sort by geoset id now.
		return geoset < m.geoset;
	}
};

struct ModelCamera {
	bool ok;

	Vec3D pos, target;
	float nearclip, farclip, fov;
	Animated<Vec3D> tPos, tTarget;
	Animated<float> rot;
	Vec3D WorldOffset;
	float WorldRotation;

	void init(MPQFile &f, ModelCameraDef &mcd, uint32 *global, wxString modelname);
	void initv10(MPQFile &f, ModelCameraDefV10 &mcd, uint32 *global, wxString modelname);
	void setup(size_t time=0);

	ModelCamera():ok(false) {}
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


struct ModelAttachment {
	int id;
	Vec3D pos;
	int bone;
	Model *model;

	void init(MPQFile &f, ModelAttachmentDef &mad, uint32 *global);
	void setup();
	void setupParticle();
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

class ModelManager: public SimpleManager {
public:
	int add(wxString name);

	ModelManager() : v(0) {}

	int v;

	void resetAnim();
	void updateEmitters(float dt);
	void clear();

};


#endif
