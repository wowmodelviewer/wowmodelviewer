#ifndef _WOWMODEL_H
#define _WOWMODEL_H

// C++ files
#include <map>
#include <vector>
//#include <stdlib.h>
//#include <crtdbg.h>

#include <QString>

// Our files

#include "animated.h"
#include "AnimManager.h"
#include "Bone.h"
#include "CharDetails.h"
#include "CharTexture.h"
#include "displayable.h"
#include "matrix.h"
#include "Model.h"
#include "ModelAttachment.h"
#include "ModelCamera.h"
#include "ModelColor.h"
#include "modelheaders.h"
#include "ModelLight.h"
#include "ModelTransparency.h"
#include "particle.h"
#include "TabardDetails.h"
#include "TextureAnim.h"
#include "vec3d.h"
#include "wow_enums.h"
#include "WoWItem.h"

#include "metaclasses/Container.h"

class CASCFile;
class GameFile;
class ModelEvent;
class ModelRenderPass;

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

#define TEXTURE_MAX 32

class _WOWMODEL_API_ WoWModel : public ManagedItem, public Displayable, public Model, public Container<WoWItem>
{
  // VBO Data
  GLuint vbuf, nbuf, tbuf;
  size_t vbufsize;

  // Non VBO Data
  GLuint dlist;
  bool forceAnim;

  // ===============================
  // Texture data
  // ===============================
  std::vector<TextureID> textures;
  std::vector<int> specialTextures;
  std::vector<GLuint> replaceTextures;

  inline void drawModel();
  void initCommon(GameFile * f);
  bool isAnimated(GameFile * f);
  void initAnimated(GameFile * f);
  void initStatic(GameFile * f);

  void animate(ssize_t anim);
  void calcBones(ssize_t anim, size_t time);

  void lightsOn(GLuint lbase);
  void lightsOff(GLuint lbase);

  std::vector<uint16> boundTris;
  std::vector<Vec3D> bounds;

  void refreshMerging();
  std::set<WoWModel *> mergedModels;

  // raw values read from file (useful for merging)
  std::vector<ModelVertex> rawVertices;
  std::vector<uint32> rawIndices;
  std::vector<ModelRenderPass *> rawPasses;
  std::vector<ModelGeosetHD *> rawGeosets;

  void restoreRawGeosets();

public:
  bool animGeometry, animTextures, animBones;
  bool model24500; // flag for build 24500 model changes to anim chunking and other things

  ModelEvent		*events;
  GameFile * gamefile;

  std::vector<TextureAnim> texAnims;
  std::vector<ModelColor> colors;
  std::vector<ModelTransparency> transparency;
  std::vector<ModelLight> lights;
  std::vector<ParticleSystem> particleSystems;
  std::vector<RibbonEmitter> ribbons;

  std::vector<uint32> globalSequences;
  std::vector<uint> replacableParticleColorIDs;
  bool replaceParticleColors;

  // Start, Mid and End colours, for cases where the model's particle colours are
  // overridden by values from ParticleColor.dbc, indexed from CreatureDisplayInfo:
  typedef std::vector<Vec4D> particleColorSet;

  // The particle will get its replacement colour set from 0, 1 or 2,
  // depending on whether its ParticleColorIndex is set to 11, 12 or 13:
  std::vector<particleColorSet> particleColorReplacements;
  // Raw Data
  std::vector<ModelVertex> origVertices;

  Vec3D *normals;
  Vec2D *texCoords;
  Vec3D *vertices;
  std::vector<uint32> indices;
  // --

  WoWModel(GameFile * file, bool forceAnim = false);
  ~WoWModel();

  std::vector<ModelCamera> cam;
  std::string modelname;
  std::string lodname;

  std::vector<ModelRenderPass *> passes;
  std::vector<ModelGeosetHD *> geosets;

  // ===============================
  // Toggles
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
  std::vector<ModelAnimation> anims;
  std::vector<int16> animLookups;
  AnimManager *animManager;
  std::vector<Bone> bones;
  std::vector<GameFile *> animfiles;

  size_t currentAnim;
  bool animcalc;
  size_t anim, animtime;

  void reset()
  {
    animcalc = false;
  }

  void update(int dt);

  // -------------------------------

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
  int creatureGeosetData;
  bool bSheathe;

  friend class ModelRenderPass;

  WoWItem * getItem(CharSlots slot);
  void updateTextureList(GameFile * tex, int special);
  void displayHeader(ModelHeader & a_header);
  bool canSetTextureFromFile(int texnum);

  std::map<int, std::string> getAnimsMap();

  void save(QXmlStreamWriter &);
  void load(QString &);

  void computeMinMaxCoords(Vec3D & min, Vec3D & max);
  static QString getCGGroupName(CharGeosets cg);

  // @TODO use geoset id instead of geoset index in vector
  void showGeoset(uint geosetindex, bool value);
  bool isGeosetDisplayed(uint geosetindex);
  void setGeosetGroupDisplay(CharGeosets group, int val);
  void setCreatureGeosetData(int cgd);

  void mergeModel(QString & name);
  void mergeModel(WoWModel * model);
  void unmergeModel(QString & name);
  void unmergeModel(WoWModel * model);

  void refresh();

  QString getNameForTex(uint16 tex);
  GLuint getGLTexture(uint16 tex);
  void dumpTextureStatus();

};


#endif
