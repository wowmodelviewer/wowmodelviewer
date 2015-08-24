#ifndef _WOWMODEL_H
#define _WOWMODEL_H

// C++ files
#include <map>
#include <vector>
//#include <stdlib.h>
//#include <crtdbg.h>

// Our files
#include "AnimManager.h"



#include "Model.h"
#include "animated.h"
#include "AnimManager.h"
#include "CharDetails.h"
#include "CharTexture.h"
#include "displayable.h"
#include "matrix.h"
#include "modelheaders.h"
#include "ModelAttachment.h"
#include "ModelCamera.h"
#include "ModelRenderPass.h"
#include "particle.h"
#include "quaternion.h"
#include "TabardDetails.h"
#include "vec3d.h"
#include "video.h"
#include "wow_enums.h"
#include "WoWItem.h"
#include "metaclasses/Container.h"

class Bone;
struct ModelColor;
class ModelEvent;
struct ModelLight;
struct ModelTransparency;
class TextureAnim;
class GameFile;
class CASCFile;
class QXmlStreamWriter;
class QXmlStreamReader;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WOWMODEL_API_ __declspec(dllexport)
#    else
#        define _WOWMODEL_API_ __declspec(dllimport)
#    endif
#else
#    define _WOWMODEL_API_
#endif

class _WOWMODEL_API_ WoWModel: public ManagedItem, public Displayable, public Model, public Container<WoWItem>
{
	// VBO Data
	GLuint vbuf, nbuf, tbuf;
	size_t vbufsize;

	// Non VBO Data
	GLuint dlist;
	bool forceAnim;



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

	// Raw Data
	ModelVertex *origVertices;

	Vec3D *vertices, *normals;
	Vec2D *texCoords;
	uint16 *indices;
	uint32 nIndices;
	std::vector<std::string> TextureList;
	// --

	WoWModel(std::string name, bool forceAnim=false);
	~WoWModel();

	std::vector<ModelCamera> cam;
	std::string modelname;
	std::string lodname;
	
	std::vector<ModelRenderPass> passes;
	std::vector<ModelGeosetHD> geosets;

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
	CharTexture tex;
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
	CharDetails cd;
	TabardDetails td;
	ModelHeader header;
	TextureID charTex, hairTex, furTex, capeTex, gobTex;

	bool bSheathe;

	friend struct ModelRenderPass;

  WoWItem * getItem(CharSlots slot);
  void UpdateTextureList(std::string texName, int special);
  void displayHeader(ModelHeader & a_header);

  std::map<int, std::string> getAnimsMap();

  void save(QXmlStreamWriter &);
  void load(QXmlStreamReader &);

};


#endif
