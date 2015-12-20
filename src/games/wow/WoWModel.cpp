#include "WoWModel.h"

#include <cassert>
#include <algorithm>
#include <iostream>

#include "Attachment.h"
#include "GlobalSettings.h"
#include "Bone.h"
#include "CASCFile.h"
#include "GameDatabase.h"
#include "Game.h"
#include "globalvars.h"
#include "ModelColor.h"
#include "ModelEvent.h"
#include "ModelLight.h"
#include "ModelTransparency.h"
#include "TextureAnim.h"
#include "logger/Logger.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

enum TextureFlags {
	TEXTURE_WRAPX=1,
	TEXTURE_WRAPY
};

void
glGetAll()
{
	GLint bled;
	LOG_INFO << "glGetAll Information";
	LOG_INFO << "GL_ALPHA_TEST:" << glIsEnabled(GL_ALPHA_TEST);
	LOG_INFO << "GL_BLEND:" << glIsEnabled(GL_BLEND);
	LOG_INFO << "GL_CULL_FACE:" << glIsEnabled(GL_CULL_FACE);
	glGetIntegerv(GL_FRONT_FACE, &bled);
	if (bled == GL_CW) {
		LOG_INFO << "glFrontFace: GL_CW";
	}
	else if (bled == GL_CCW) {
		LOG_INFO << "glFrontFace: GL_CCW";
	}
	LOG_INFO << "GL_DEPTH_TEST:" << glIsEnabled(GL_DEPTH_TEST);
	LOG_INFO << "GL_DEPTH_WRITEMASK:" << glIsEnabled(GL_DEPTH_WRITEMASK);
	LOG_INFO << "GL_COLOR_MATERIAL:" << glIsEnabled(GL_COLOR_MATERIAL);
	LOG_INFO << "GL_LIGHT0:" << glIsEnabled(GL_LIGHT0);
	LOG_INFO << "GL_LIGHT1:" << glIsEnabled(GL_LIGHT1);
	LOG_INFO << "GL_LIGHT2:" << glIsEnabled(GL_LIGHT2);
	LOG_INFO << "GL_LIGHT3:" << glIsEnabled(GL_LIGHT3);
	LOG_INFO << "GL_LIGHTING:" << glIsEnabled(GL_LIGHTING);
	LOG_INFO << "GL_TEXTURE_2D:" << glIsEnabled(GL_TEXTURE_2D);
	glGetIntegerv(GL_BLEND_SRC, &bled);
	LOG_INFO << "GL_BLEND_SRC:" << bled;
	glGetIntegerv(GL_BLEND_DST, &bled);
	LOG_INFO << "GL_BLEND_DST:" << bled;
}

void glInitAll()
{
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_COLOR_MATERIAL);
	//glEnable(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LIGHT0);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, Vec4D(1.0f, 1.0f, 1.0f, 1.0f));
        glLightfv(GL_LIGHT0, GL_AMBIENT, Vec4D(1.0f, 1.0f, 1.0f, 1.0f));
        glLightfv(GL_LIGHT0, GL_SPECULAR, Vec4D(1.0f, 1.0f, 1.0f, 1.0f));
	glDisable(GL_LIGHT1);
	glDisable(GL_LIGHT2);
	glDisable(GL_LIGHT3);
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glBlendFunc(GL_ONE, GL_ZERO);
	glFrontFace(GL_CCW);
	//glDepthMask(GL_TRUE);
	glDepthFunc(GL_NEVER);
}

WoWModel::WoWModel(GameFile * file, bool forceAnim) :
    ManagedItem(""),
    forceAnim(forceAnim)
{
  ok = false;
	if (!file)
		return;

	setItemName(file->fullname());

	// replace .MDX with .M2
	QString tempname = file->fullname();
	tempname.replace(".mdx",".m2");

	// Initiate our model variables.
	trans = 1.0f;
	rad = 1.0f;
	pos = Vec3D(0.0f, 0.0f, 0.0f);
	rot = Vec3D(0.0f, 0.0f, 0.0f);

	for (size_t i=0; i<TEXTURE_MAX; i++) {
		specialTextures[i] = -1;
		replaceTextures[i] = 0;
		useReplaceTextures[i] = false;
	}

	for (size_t i=0; i<ATT_MAX; i++) 
		attLookup[i] = -1;

	for (size_t i=0; i<BONE_MAX; i++) 
		keyBoneLookup[i] = -1;


	dlist = 0;
	bounds = 0;
	boundTris = 0;
	showGeosets = 0;

	hasCamera = false;
	hasParticles = false;
	isWMO = false;
	isMount = false;

	showModel = false;
	showBones = false;
	showBounds = false;
	showWireframe = false;
	showParticles = false;
	showTexture = true;

	charModelDetails.Reset();
	
	vbuf = nbuf = tbuf = 0;
	
	origVertices = 0;
	vertices = 0;
	normals = 0;
	texCoords = 0;
	indices = 0;
	
	animtime = 0;
	anim = 0;
	anims = 0;
	animLookups = 0;
	animManager = 0;
	bones = 0;
	bounds = 0;
	boundTris = 0;
	currentAnim = 0;
	colors = 0;
	globalSequences = 0;
	lights = 0;
	particleSystems = 0;
	ribbons = 0;
	texAnims = 0;
	textures = 0;
	transparency = 0;
	events = 0;
	modelType = MT_NORMAL;
	attachment = 0;

	// --
	ok = false;


	if (!file->open() || file->isEof() || (file->getSize() < sizeof(ModelHeader)))
	{
		LOG_ERROR << "Unable to load model:" << tempname;
		file->close();
		return;
	}

	ok = true;
	
	memcpy(&header, file->getBuffer(), sizeof(ModelHeader));

	LOG_INFO << "Loading model:" << tempname << "size:" << file->getSize();

	//displayHeader(header);

	if (header.id[0] != 'M' && header.id[1] != 'D' && header.id[2] != '2' && header.id[3] != '0') {
		LOG_ERROR << "Invalid model!  May be corrupted.";
		ok = false;
		file->close();
		return;
	}

	animated = isAnimated(file) || forceAnim;  // isAnimated will set animGeometry and animTextures

	modelname = tempname.toStdString();
	QStringList list = tempname.split("\\");
	setName(list[list.size()-1].replace(".m2",""));
	if (header.nameOfs != 304 && header.nameOfs != 320)
	{
	  LOG_ERROR << "Invalid model nameOfs=" << header.nameOfs << "/" << sizeof(ModelHeader) << "! May be corrupted.";
	  file->close();
	  return;
	}

	// Error check
	// 10 1 0 0 = WoW 5.0 models (as of 15464)
	// 10 1 0 0 = WoW 4.0.0.12319 models
	// 9 1 0 0 = WoW 4.0 models
	// 8 1 0 0 = WoW 3.0 models
	// 4 1 0 0 = WoW 2.0 models
	// 0 1 0 0 = WoW 1.0 models
	if (header.version[0] != 4 && header.version[1] != 1 && header.version[2] != 0 && header.version[3] != 0) {
		LOG_ERROR << "Model version is incorrect! Make sure you are loading models from World of Warcraft 2.0.1 or newer client.";
		ok = false;
		file->close();

		/*
		 @TODO : replace with exceptions
		if (header.version[0] == 0)
			wxMessageBox(wxString::Format(wxT("An error occured while trying to load the model %s.\nWoW Model Viewer 0.5.x only supports loading WoW 2.0 models\nModels from WoW 1.12 or earlier are not supported"), tempname.c_str()), wxT("Error: Unable to load model"), wxICON_ERROR);
		 */
		return;
	}

	if (file->getSize() < header.ofsParticleEmitters) {
		LOG_ERROR << "Unable to load the Model \"" << tempname << "\", appears to be corrupted.";
		file->close();
		return;
	}
	
	if (header.nGlobalSequences) {
		globalSequences = new uint32[header.nGlobalSequences];
		memcpy(globalSequences, (file->getBuffer() + header.ofsGlobalSequences), header.nGlobalSequences * sizeof(uint32));
	}

	if (forceAnim)
		animBones = true;

	if (animated)
		initAnimated(file);
	else
		initStatic(file);

	file->close();
	
	// Ready to render.
	showModel = true;
	if (hasParticles)
		showParticles = true;
	alpha = 1.0f;

}

WoWModel::~WoWModel()
{
	if (ok)
	{
		if(attachment)
			attachment->setModel(0);

		// There is a small memory leak somewhere with the textures.
		// Especially if the texture was built into the model.
		// No matter what I try though I can't find the memory to unload.
		if (header.nTextures)
		{
			// For character models, the texture isn't loaded into the texture manager, manually remove it
			glDeleteTextures(1, &replaceTextures[1]);

			delete [] textures; textures = 0;
			delete [] globalSequences; globalSequences = 0;
			delete [] bounds; bounds = 0;
			delete [] boundTris; boundTris = 0;
			delete [] showGeosets; showGeosets = 0;

			delete animManager; animManager = 0;

			if (animated)
			{
				// unload all sorts of crap
				// Need this if statement because VBO supported
				// cards have already deleted it.
				if(video.supportVBO)
				{
					glDeleteBuffersARB(1, &nbuf);
					glDeleteBuffersARB(1, &vbuf);
					glDeleteBuffersARB(1, &tbuf);

					vertices = NULL;
				}

				delete [] normals; normals = 0;
				delete [] vertices; normals = 0;
				delete [] texCoords; texCoords = 0;

				delete [] indices; indices = 0;
				delete [] anims; anims = 0;
				delete [] animLookups; animLookups = 0;
				delete [] origVertices; origVertices = 0;

				delete [] bones; bones = 0;
				delete [] texAnims; texAnims = 0;
				delete [] colors; colors = 0;
				delete [] transparency; transparency = 0;
				delete [] lights; lights = 0;
				delete [] events; events = 0;
				delete [] particleSystems; particleSystems = 0;
				delete [] ribbons; ribbons = 0;
			}
			else
			{
				glDeleteLists(dlist, 1);
			}
		}
	}
}


void WoWModel::displayHeader(ModelHeader & a_header)
{
	LOG_INFO << "id:" << a_header.id[0] << a_header.id[1] << a_header.id[2] << a_header.id[3];
	LOG_INFO << "version:" << (int)a_header.version[0] << (int)a_header.version[1] << (int)a_header.version[2] << (int)a_header.version[3];
	LOG_INFO << "nameLength:" << a_header.nameLength;
	LOG_INFO << "nameOfs:" << a_header.nameOfs;
	LOG_INFO << "GlobalModelFlags:" << a_header.GlobalModelFlags;
	LOG_INFO << "nGlobalSequences:" << a_header.nGlobalSequences;
	LOG_INFO << "ofsGlobalSequences:" << a_header.ofsGlobalSequences;
	LOG_INFO << "nAnimations:" << a_header.nAnimations;
	LOG_INFO << "ofsAnimations:" << a_header.ofsAnimations;
	LOG_INFO << "nAnimationLookup:" << a_header.nAnimationLookup;
	LOG_INFO << "ofsAnimationLookup:" << a_header.ofsAnimationLookup;
	LOG_INFO << "nBones:" << a_header.nBones;
	LOG_INFO << "ofsBones:" << a_header.ofsBones;
	LOG_INFO << "nKeyBoneLookup:" << a_header.nKeyBoneLookup;
	LOG_INFO << "ofsKeyBoneLookup:" << a_header.ofsKeyBoneLookup;
	LOG_INFO << "nVertices:" << a_header.nVertices;
	LOG_INFO << "ofsVertices:" << a_header.ofsVertices;
	LOG_INFO << "nViews:" << a_header.nViews;
	LOG_INFO << "nColors:" << a_header.nColors;
	LOG_INFO << "ofsColors:" << a_header.ofsColors;
	LOG_INFO << "nTextures:" << a_header.nTextures;
	LOG_INFO << "ofsTextures:" << a_header.ofsTextures;
	LOG_INFO << "nTransparency:" << a_header.nTransparency;
	LOG_INFO << "ofsTransparency:" << a_header.ofsTransparency;
	LOG_INFO << "nTexAnims:" << a_header.nTexAnims;
	LOG_INFO << "ofsTexAnims:" << a_header.ofsTexAnims;
	LOG_INFO << "nTexReplace:" << a_header.nTexReplace;
	LOG_INFO << "ofsTexReplace:" << a_header.ofsTexReplace;
	LOG_INFO << "nTexFlags:" << a_header.nTexFlags;
	LOG_INFO << "ofsTexFlags:" << a_header.ofsTexFlags;
	LOG_INFO << "nBoneLookup:" << a_header.nBoneLookup;
	LOG_INFO << "ofsBoneLookup:" << a_header.ofsBoneLookup;
	LOG_INFO << "nTexLookup:" << a_header.nTexLookup;
	LOG_INFO << "ofsTexLookup:" << a_header.ofsTexLookup;
	LOG_INFO << "nTexUnitLookup:" << a_header.nTexUnitLookup;
	LOG_INFO << "ofsTexUnitLookup:" << a_header.ofsTexUnitLookup;
	LOG_INFO << "nTransparencyLookup:" << a_header.nTransparencyLookup;
	LOG_INFO << "ofsTransparencyLookup:" << a_header.ofsTransparencyLookup;
	LOG_INFO << "nTexAnimLookup:" << a_header.nTexAnimLookup;
	LOG_INFO << "ofsTexAnimLookup:" << a_header.ofsTexAnimLookup;

//	LOG_INFO << "collisionSphere :";
//	displaySphere(a_header.collisionSphere);
//	LOG_INFO << "boundSphere :";
//	displaySphere(a_header.boundSphere);

	LOG_INFO << "nBoundingTriangles:" << a_header.nBoundingTriangles;
	LOG_INFO << "ofsBoundingTriangles:" << a_header.ofsBoundingTriangles;
	LOG_INFO << "nBoundingVertices:" << a_header.nBoundingVertices;
	LOG_INFO << "ofsBoundingVertices:" << a_header.ofsBoundingVertices;
	LOG_INFO << "nBoundingNormals:" << a_header.nBoundingNormals;
	LOG_INFO << "ofsBoundingNormals:" << a_header.ofsBoundingNormals;

	LOG_INFO << "nAttachments:" << a_header.nAttachments;
	LOG_INFO << "ofsAttachments:" << a_header.ofsAttachments;
	LOG_INFO << "nAttachLookup:" << a_header.nAttachLookup;
	LOG_INFO << "ofsAttachLookup:" << a_header.ofsAttachLookup;
	LOG_INFO << "nEvents:" << a_header.nEvents;
	LOG_INFO << "ofsEvents:" << a_header.ofsEvents;
	LOG_INFO << "nLights:" << a_header.nLights;
	LOG_INFO << "ofsLights:" << a_header.ofsLights;
	LOG_INFO << "nCameras:" << a_header.nCameras;
	LOG_INFO << "ofsCameras:" << a_header.ofsCameras;
	LOG_INFO << "nCameraLookup:" << a_header.nCameraLookup;
	LOG_INFO << "ofsCameraLookup:" << a_header.ofsCameraLookup;
	LOG_INFO << "nRibbonEmitters:" << a_header.nRibbonEmitters;
	LOG_INFO << "ofsRibbonEmitters:" << a_header.ofsRibbonEmitters;
	LOG_INFO << "nParticleEmitters:" << a_header.nParticleEmitters;
	LOG_INFO << "ofsParticleEmitters:" << a_header.ofsParticleEmitters;
}


bool WoWModel::isAnimated(GameFile * f)
{
	// see if we have any animated bones
	ModelBoneDef *bo = (ModelBoneDef*)(f->getBuffer() + header.ofsBones);

	animGeometry = false;
	animBones = false;
	ind = false;

	ModelVertex *verts = (ModelVertex*)(f->getBuffer() + header.ofsVertices);
	for (size_t i=0; i<header.nVertices && !animGeometry; i++) {
		for (size_t b=0; b<4; b++) {
			if (verts[i].weights[b]>0) {
				ModelBoneDef &bb = bo[verts[i].bones[b]];
				if (bb.translation.type || bb.rotation.type || bb.scaling.type || (bb.flags & MODELBONE_BILLBOARD)) {
					if (bb.flags & MODELBONE_BILLBOARD) {
						// if we have billboarding, the model will need per-instance animation
						ind = true;
					}
					animGeometry = true;
					break;
				}
			}
		}
	}

	if (animGeometry) 
		animBones = true;
	else {
		for (size_t i=0; i<header.nBones; i++) {
			ModelBoneDef &bb = bo[i];
			if (bb.translation.type || bb.rotation.type || bb.scaling.type) {
				animBones = true;
				animGeometry = true;
				break;
			}
		}
	}

	animTextures = header.nTexAnims > 0;

	bool animMisc = header.nCameras>0 || // why waste time, pretty much all models with cameras need animation
					header.nLights>0 || // same here
					header.nParticleEmitters>0 ||
					header.nRibbonEmitters>0;

	if (animMisc) 
		animBones = true;

	// animated colors
	if (header.nColors) {
		ModelColorDef *cols = (ModelColorDef*)(f->getBuffer() + header.ofsColors);
		for (size_t i=0; i<header.nColors; i++) {
			if (cols[i].color.type!=0 || cols[i].opacity.type!=0) {
				animMisc = true;
				break;
			}
		}
	}

	// animated opacity
	if (header.nTransparency && !animMisc) {
		ModelTransDef *trs = (ModelTransDef*)(f->getBuffer() + header.ofsTransparency);
		for (size_t i=0; i<header.nTransparency; i++) {
			if (trs[i].trans.type!=0) {
				animMisc = true;
				break;
			}
		}
	}

	// guess not...
	return animGeometry || animTextures || animMisc;
}

void WoWModel::initCommon(GameFile * f)
{
	// assume: origVertices already set

	// This data is needed for both VBO and non-VBO cards.
	vertices = new Vec3D[header.nVertices];
	normals = new Vec3D[header.nVertices];

	// Correct the data from the model, so that its using the Y-Up axis mode.
	for (size_t i=0; i<header.nVertices; i++) {
		origVertices[i].pos = fixCoordSystem(origVertices[i].pos);
		origVertices[i].normal = fixCoordSystem(origVertices[i].normal);

		// Set the data for our vertices, normals from the model data
		vertices[i] = origVertices[i].pos;
		normals[i] = origVertices[i].normal.normalize();

		float len = origVertices[i].pos.lengthSquared();
		if (len > rad){ 
			rad = len;
		}
	}

	// model vertex radius
	rad = sqrtf(rad);

	// bounds
	if (header.nBoundingVertices > 0) {
		bounds = new Vec3D[header.nBoundingVertices];
		Vec3D *b = (Vec3D*)(f->getBuffer() + header.ofsBoundingVertices);
		for (size_t i=0; i<header.nBoundingVertices; i++) {
			bounds[i] = fixCoordSystem(b[i]);
		}
	}
	if (header.nBoundingTriangles > 0) {
		boundTris = new uint16[header.nBoundingTriangles];
		memcpy(boundTris, f->getBuffer() + header.ofsBoundingTriangles, header.nBoundingTriangles*sizeof(uint16));
	}

	// textures
	ModelTextureDef *texdef = (ModelTextureDef*)(f->getBuffer() + header.ofsTextures);
	if (header.nTextures) {
		textures = new TextureID[header.nTextures];
		for (size_t i=0; i<header.nTextures; i++) {

			// Error check
			if (i > TEXTURE_MAX-1) {
				LOG_ERROR << "Model Texture" << header.nTextures << "over" << TEXTURE_MAX;
				break;
			}
			/*
			Texture Types
			Texture type is 0 for regular textures, nonzero for skinned textures (filename not referenced in the M2 file!) 
			For instance, in the NightElfFemale model, her eye glow is a type 0 texture and has a file name, 
			the other 3 textures have types of 1, 2 and 6. The texture filenames for these come from client database files:

			DBFilesClient\CharSections.dbc
			DBFilesClient\CreatureDisplayInfo.dbc
			DBFilesClient\ItemDisplayInfo.dbc
			(possibly more)
				
			0	 Texture given in filename
			1	 Body + clothes
			2	Cape
			6	Hair, beard
			8	Tauren fur
			11	Skin for creatures #1
			12	Skin for creatures #2
			13	Skin for creatures #3

			Texture Flags
			Value	 Meaning
			1	Texture wrap X
			2	Texture wrap Y
			*/

			if (texdef[i].type == TEXTURE_FILENAME) {
				QString texname((char*)(f->getBuffer()+texdef[i].nameOfs));
				textures[i] = texturemanager.add(GAMEDIRECTORY.getFile(texname));
				TextureList.push_back(texname);
				LOG_INFO << "Added" << texname << "to the TextureList[" << TextureList.size() << "]";
			} else {
				// special texture - only on characters and such...
				textures[i] = 0;
				//while (texdef[i].type < TEXTURE_MAX && specialTextures[texdef[i].type]!=-1) texdef[i].type++;
				//if (texdef[i].type < TEXTURE_MAX)specialTextures[texdef[i].type] = (int)i;
				specialTextures[i] = texdef[i].type;

				QString tex = QString("Special_%1").arg(texdef[i].type);

				if (modelType == MT_NORMAL){
					if (texdef[i].type == TEXTURE_HAIR)
						tex = "Hair.blp";
					else if(texdef[i].type == TEXTURE_BODY)
						tex = "Body.blp";
					else if(texdef[i].type == TEXTURE_FUR)
						tex = "Fur.blp";
				}

				LOG_INFO << "Added" << tex << "to the TextureList[" << TextureList.size() << "] via specialTextures. Type:" << texdef[i].type;
				TextureList.push_back(tex);

				if (texdef[i].type < TEXTURE_MAX)
					useReplaceTextures[texdef[i].type] = true;

				if (texdef[i].type == TEXTURE_ARMORREFLECT) {
					// a fix for weapons with type-3 textures.
					replaceTextures[texdef[i].type] = texturemanager.add(GAMEDIRECTORY.getFile("Item\\ObjectComponents\\Weapon\\ArmorReflect4.BLP"));
				}
			}
		}
	}
	/*
	// replacable textures - it seems to be better to get this info from the texture types
	if (header.nTexReplace) {
		size_t m = header.nTexReplace;
		if (m>16) m = 16;
		int16 *texrep = (int16*)(f->getBuffer() + header.ofsTexReplace);
		for (size_t i=0; i<m; i++) specialTextures[i] = texrep[i];
	}
	*/

	// attachments
	if (header.nAttachments) {
		ModelAttachmentDef *attachments = (ModelAttachmentDef*)(f->getBuffer() + header.ofsAttachments);
		for (size_t i=0; i<header.nAttachments; i++) {
			ModelAttachment att;
			att.model = this;
			att.init(f, attachments[i], globalSequences);
			atts.push_back(att);
		}
	}

	if (header.nAttachLookup) {
		int16 *p = (int16*)(f->getBuffer() + header.ofsAttachLookup);
		if (header.nAttachLookup > ATT_MAX)
			LOG_ERROR << "Model AttachLookup" << header.nAttachLookup << "over" << ATT_MAX;
		for (size_t i=0; i<header.nAttachLookup; i++) {
			if (i>ATT_MAX-1)
				break;
			attLookup[i] = p[i];
		}
	}


	// init colors
	if (header.nColors) {
		colors = new ModelColor[header.nColors];
		ModelColorDef *colorDefs = (ModelColorDef*)(f->getBuffer() + header.ofsColors);
		for (size_t i=0; i<header.nColors; i++) 
			colors[i].init(f, colorDefs[i], globalSequences);
	}

	// init transparency
	if (header.nTransparency) {
		transparency = new ModelTransparency[header.nTransparency];
		ModelTransDef *trDefs = (ModelTransDef*)(f->getBuffer() + header.ofsTransparency);
		for (size_t i=0; i<header.nTransparency; i++) 
			transparency[i].init(f, trDefs[i], globalSequences);
	}

	if (header.nViews) {
		// just use the first LOD/view
		// First LOD/View being the worst?
		// TODO: Add support for selecting the LOD.
		// int viewLOD = 0; // sets LOD to worst
		// int viewLOD = header.nViews - 1; // sets LOD to best
		setLOD(f, 0); // Set the default Level of Detail to the best possible.
	}
}

void WoWModel::initStatic(GameFile * f)
{
	origVertices = (ModelVertex*)(f->getBuffer() + header.ofsVertices);

	initCommon(f);

	dlist = glGenLists(1);
	glNewList(dlist, GL_COMPILE);

    drawModel();

	glEndList();

	// clean up vertices, indices etc
	delete [] vertices; vertices = 0;
	delete [] normals; normals = 0;
	delete [] indices; indices = 0;

	delete [] colors; colors = 0;
	delete [] transparency; transparency = 0;
}

void WoWModel::initAnimated(GameFile * f)
{
	if (origVertices) {
		delete [] origVertices;
		origVertices = NULL;
	}

	origVertices = new ModelVertex[header.nVertices];
	memcpy(origVertices, f->getBuffer() + header.ofsVertices, header.nVertices * sizeof(ModelVertex));

	initCommon(f);

	if (header.nAnimations > 0)
	{
		anims = new ModelAnimation[header.nAnimations];

		ModelAnimationWotLK animsWotLK;
		QString tempname;

		//std::cout << "header.nAnimations = " << header.nAnimations << std::endl;

		for(size_t i=0; i<header.nAnimations; i++)
		{
		  memcpy(&animsWotLK, f->getBuffer() + header.ofsAnimations + i*sizeof(ModelAnimationWotLK), sizeof(ModelAnimationWotLK));
		  anims[i].animID = animsWotLK.animID;
		  anims[i].timeStart = 0;
		  anims[i].timeEnd = animsWotLK.length;
		  anims[i].moveSpeed = animsWotLK.moveSpeed;
		  anims[i].flags = animsWotLK.flags;
		  anims[i].probability = animsWotLK.probability;
		  anims[i].d1 = animsWotLK.d1;
		  anims[i].d2 = animsWotLK.d2;
		  anims[i].playSpeed = animsWotLK.playSpeed;
		  anims[i].boundSphere.min = animsWotLK.boundSphere.min;
		  anims[i].boundSphere.max = animsWotLK.boundSphere.max;
		  anims[i].boundSphere.radius = animsWotLK.boundSphere.radius;
		  anims[i].NextAnimation = animsWotLK.NextAnimation;
		  anims[i].Index = animsWotLK.Index;

		  tempname = QString::fromStdString(modelname).replace(".m2","");
		  tempname = QString("%1%2-%3.anim").arg(tempname).arg(anims[i].animID,4,10,QChar('0')).arg(animsWotLK.subAnimID,2,10,QChar('0'));

		  GameFile * anim = GAMEDIRECTORY.getFile(tempname);
		  if(anim)
		    animfiles.push_back(new CASCFile(anim->fullname()));
		  else
		    animfiles.push_back(NULL);
		}

		animManager = new AnimManager(anims);
	}
	
	if (animBones) {
		// init bones...
		bones = new Bone[header.nBones];
		ModelBoneDef *mb = (ModelBoneDef*)(f->getBuffer() + header.ofsBones);
		for (size_t i=0; i<header.nBones; i++) {
			//if (i==0) mb[i].rotation.ofsRanges = 1.0f;
				bones[i].model = this;
				bones[i].initV3(*f, mb[i], globalSequences, animfiles);
		}

		// Block keyBoneLookup is a lookup table for Key Skeletal Bones, hands, arms, legs, etc.
		if (header.nKeyBoneLookup < BONE_MAX) {
			memcpy(keyBoneLookup, f->getBuffer() + header.ofsKeyBoneLookup, sizeof(int16)*header.nKeyBoneLookup);
		} else {
			memcpy(keyBoneLookup, f->getBuffer() + header.ofsKeyBoneLookup, sizeof(int16)*BONE_MAX);
			LOG_ERROR << "KeyBone number" << header.nKeyBoneLookup << "over" << BONE_MAX;
		}
	}

	// free MPQFile
	if (header.nAnimations > 0) {
	  for(size_t i=0; i<header.nAnimations; i++) {
	    if(animfiles[i] && (animfiles[i]->getSize() > 0))
	      animfiles[i]->close();
	  }
	}

	// Index at ofsAnimations which represents the animation in AnimationData.dbc. -1 if none.
	if (header.nAnimationLookup > 0) {
		animLookups = new int16[header.nAnimationLookup];
		memcpy(animLookups, f->getBuffer() + header.ofsAnimationLookup, sizeof(int16)*header.nAnimationLookup);
	}
	
	const size_t size = (header.nVertices * sizeof(float));
	vbufsize = (3 * size); // we multiple by 3 for the x, y, z positions of the vertex

	texCoords = new Vec2D[header.nVertices];
	for (size_t i=0; i<header.nVertices; i++) 
		texCoords[i] = origVertices[i].texcoords;

	if (video.supportVBO) {
		// Vert buffer
		glGenBuffersARB(1,&vbuf);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, vertices, GL_STATIC_DRAW_ARB);
		delete [] vertices; vertices = 0;
		
		// Texture buffer
		glGenBuffersARB(1,&tbuf);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, tbuf);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, 2*size, texCoords, GL_STATIC_DRAW_ARB);
		delete [] texCoords; texCoords = 0;
		
		// normals buffer
		glGenBuffersARB(1,&nbuf);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, nbuf);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, normals, GL_STATIC_DRAW_ARB);
		delete [] normals; normals = 0;
		
		// clean bind
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	}

	if (animTextures) {
		texAnims = new TextureAnim[header.nTexAnims];
		ModelTexAnimDef *ta = (ModelTexAnimDef*)(f->getBuffer() + header.ofsTexAnims);
		for (size_t i=0; i<header.nTexAnims; i++)
			texAnims[i].init(f, ta[i], globalSequences);
	}

	if (header.nEvents) {
		ModelEventDef *edefs = (ModelEventDef *)(f->getBuffer()+header.ofsEvents);
		events = new ModelEvent[header.nEvents];
		for (size_t i=0; i<header.nEvents; i++) {
			events[i].init(f, edefs[i], globalSequences);
		}
	}

	// particle systems
	if (header.nParticleEmitters) {
		if (header.version[0] >= 0x10) {
			ModelParticleEmitterDefV10 *pdefsV10 = (ModelParticleEmitterDefV10 *)(f->getBuffer() + header.ofsParticleEmitters);
			ModelParticleEmitterDef *pdefs;
			particleSystems = new ParticleSystem[header.nParticleEmitters];
			hasParticles = true;
			for (size_t i=0; i<header.nParticleEmitters; i++) {
				pdefs = (ModelParticleEmitterDef *) &pdefsV10[i];
				particleSystems[i].model = this;
				particleSystems[i].init(f, *pdefs, globalSequences);
			}
		} else {
			ModelParticleEmitterDef *pdefs = (ModelParticleEmitterDef *)(f->getBuffer() + header.ofsParticleEmitters);
			particleSystems = new ParticleSystem[header.nParticleEmitters];
			hasParticles = true;
			for (size_t i=0; i<header.nParticleEmitters; i++) {
				particleSystems[i].model = this;
				particleSystems[i].init(f, pdefs[i], globalSequences);
			}
		}
	}

	// ribbons
	if (header.nRibbonEmitters) {
		ModelRibbonEmitterDef *rdefs = (ModelRibbonEmitterDef *)(f->getBuffer() + header.ofsRibbonEmitters);
		ribbons = new RibbonEmitter[header.nRibbonEmitters];
		for (size_t i=0; i<header.nRibbonEmitters; i++) {
			ribbons[i].model = this;
			ribbons[i].init(f, rdefs[i], globalSequences);
		}
	}

	// Cameras
	if (header.nCameras>0) {
		if (header.version[0] <= 9){
			ModelCameraDef *camDefs = (ModelCameraDef*)(f->getBuffer() + header.ofsCameras);
			for (size_t i=0;i<header.nCameras;i++){
				ModelCamera a;
				a.init(f, camDefs[i], globalSequences, modelname);
				cam.push_back(a);
			}
		}else if (header.version[0] <= 16){
			ModelCameraDefV10 *camDefs = (ModelCameraDefV10*)(f->getBuffer() + header.ofsCameras);
			for (size_t i=0;i<header.nCameras;i++){
				ModelCamera a;
				a.initv10(f, camDefs[i], globalSequences, modelname);
				cam.push_back(a);
			}
		}
		if (cam.size() > 0){
			hasCamera = true;
		}
	}

	// init lights
	if (header.nLights) {
		lights = new ModelLight[header.nLights];
		ModelLightDef *lDefs = (ModelLightDef*)(f->getBuffer() + header.ofsLights);
		for (size_t i=0; i<header.nLights; i++) {
			lights[i].init(f, lDefs[i], globalSequences);
		}
	}

	animcalc = false;
}

void WoWModel::setLOD(GameFile * f, int index)
{
	// Texture definitions
	ModelTextureDef *texdef = (ModelTextureDef*)(f->getBuffer() + header.ofsTextures);

	// Transparency
	int16 *transLookup = (int16*)(f->getBuffer() + header.ofsTransparencyLookup);

	// I thought the view controlled the Level of detail,  but that doesn't seem to be the case.
	// Seems to only control the render order.  Which makes this function useless and not needed :(

	// remove suffix .M2
	QString tmpname = QString::fromStdString(modelname).replace(".m2","", Qt::CaseInsensitive);
	lodname = QString("%1%2.skin").arg(tmpname).arg(index,2,10,QChar('0')).toStdString(); // Lods: 00, 01, 02, 03

	GameFile * g = GAMEDIRECTORY.getFile(lodname.c_str());

	if(!g || !g->open())
	{
	  LOG_ERROR << "Unable to load Lods:" << lodname.c_str();
	  return;
	}

	if (g->isEof()) {
		LOG_ERROR << "Unable to load Lods:" << lodname.c_str();
		g->close();
		return;
	}

	ModelView *view = (ModelView*)(g->getBuffer());

	if (view->id[0] != 'S' || view->id[1] != 'K' || view->id[2] != 'I' || view->id[3] != 'N') {
		LOG_ERROR << "Unable to load Lods:" << lodname.c_str();
		g->close();
		return;
	}

	// Indices,  Triangles
	uint16 *indexLookup = (uint16*)(g->getBuffer() + view->ofsIndex);
	uint16 *triangles = (uint16*)(g->getBuffer() + view->ofsTris);
	nIndices = view->nTris;
	delete [] indices; indices = 0;
	indices = new uint16[nIndices];
	for (size_t i = 0; i<nIndices; i++) {
        indices[i] = indexLookup[triangles[i]];
	}

	// render ops
	ModelGeoset *ops = (ModelGeoset*)(g->getBuffer() + view->ofsSub);
	ModelTexUnit *tex = (ModelTexUnit*)(g->getBuffer() + view->ofsTex);
	ModelRenderFlags *renderFlags = (ModelRenderFlags*)(f->getBuffer() + header.ofsTexFlags);
	uint16 *texlookup = (uint16*)(f->getBuffer() + header.ofsTexLookup);
	uint16 *texanimlookup = (uint16*)(f->getBuffer() + header.ofsTexAnimLookup);
	int16 *texunitlookup = (int16*)(f->getBuffer() + header.ofsTexUnitLookup);

	delete [] showGeosets;

	showGeosets = new bool[view->nSub];

	uint32 start = 0;
	for (size_t i=0; i<view->nSub; i++) {
	  ModelGeosetHD hdgeo(ops[i]);
	  hdgeo.istart = start;
	  start += hdgeo.icount;
		geosets.push_back(hdgeo);
		showGeosets[i] = true;
	}

	passes.clear();
	for (size_t j = 0; j<view->nTex; j++) {
		ModelRenderPass pass;

		pass.useTex2 = false;
		pass.useEnvMap = false;
		pass.cull = false;
		pass.trans = false;
		pass.unlit = false;
		pass.noZWrite = false;
		pass.billboard = false;
		pass.texanim = -1; // no texture animation

		//pass.texture2 = 0;
		size_t geoset = tex[j].op;
		
		pass.geoset = (int)geoset;

		pass.indexStart = geosets[geoset].istart;
		pass.indexCount = geosets[geoset].icount;
		pass.vertexStart = geosets[geoset].vstart;
		pass.vertexEnd = pass.vertexStart + geosets[geoset].vcount;

		//TextureID texid = textures[texlookup[tex[j].textureid]];
		//pass.texture = texid;
		pass.tex = texlookup[tex[j].textureid];

		// TODO: figure out these flags properly -_-
		ModelRenderFlags &rf = renderFlags[tex[j].flagsIndex];
		
		pass.blendmode = rf.blend;
		//if (rf.blend == 0) // Test to disable/hide different blend types
		//	continue;

		pass.color = tex[j].colorIndex;
		pass.opacity = transLookup[tex[j].transid];

		pass.unlit = (rf.flags & RENDERFLAGS_UNLIT) != 0;

		pass.cull = (rf.flags & RENDERFLAGS_TWOSIDED) == 0;

		pass.billboard = (rf.flags & RENDERFLAGS_BILLBOARD) != 0;

		// Use environmental reflection effects?
		pass.useEnvMap = (texunitlookup[tex[j].texunit] == -1) && pass.billboard && rf.blend>2; //&& rf.blend<5;

		// Disable environmental mapping if its been unchecked.
		if (pass.useEnvMap && !video.useEnvMapping)
			pass.useEnvMap = false;

		pass.noZWrite = (rf.flags & RENDERFLAGS_ZBUFFERED) != 0;

		// ToDo: Work out the correct way to get the true/false of transparency
		pass.trans = (pass.blendmode>0) && (pass.opacity>0);	// Transparency - not the correct way to get transparency

		pass.p = ops[geoset].BoundingBox[0].z;

		// Texture flags
		pass.swrap = (texdef[pass.tex].flags & TEXTURE_WRAPX) != 0; // Texture wrap X
		pass.twrap = (texdef[pass.tex].flags & TEXTURE_WRAPY) != 0; // Texture wrap Y
		
		// tex[j].flags: Usually 16 for static textures, and 0 for animated textures.	
		if (animTextures && (tex[j].flags & TEXTUREUNIT_STATIC) == 0) {
			pass.texanim = texanimlookup[tex[j].texanimid];
		}

		passes.push_back(pass);
	}

	g->close();
	// transparent parts come later
	std::sort(passes.begin(), passes.end());
}

void WoWModel::calcBones(ssize_t anim, size_t time)
{
	// Reset all bones to 'false' which means they haven't been animated yet.
	for (size_t i=0; i<header.nBones; i++) {
		bones[i].calc = false;
	}

	// Character specific bone animation calculations.
	if (charModelDetails.isChar) {	

		// Animate the "core" rotations and transformations for the rest of the model to adopt into their transformations
		if (keyBoneLookup[BONE_ROOT] > -1)	{
			for (int i=0; i<=keyBoneLookup[BONE_ROOT]; i++) {
				bones[i].calcMatrix(bones, anim, time);
			}
		}

		// Find the close hands animation id
		int closeFistID = 0;
		/*
		for (size_t i=0; i<header.nAnimations; i++) {
			if (anims[i].animID==15) {  // closed fist
				closeFistID = i;
				break;
			}
		}
		*/
		// Alfred 2009.07.23 use animLookups to speedup
		if (header.nAnimationLookup >= ANIMATION_HANDSCLOSED && animLookups[ANIMATION_HANDSCLOSED] > 0) // closed fist
			closeFistID = animLookups[ANIMATION_HANDSCLOSED];

		// Animate key skeletal bones except the fingers which we do later.
		// -----
		size_t a, t;

		// if we have a "secondary animation" selected,  animate upper body using that.
		if (animManager->GetSecondaryID() > -1) {
			a = animManager->GetSecondaryID();
			t = animManager->GetSecondaryFrame();
		} else {
			a = anim;
			t = time;
		}

		for (size_t i=0; i<animManager->GetSecondaryCount(); i++) { // only goto 5, otherwise it affects the hip/waist rotation for the lower-body.
			if (keyBoneLookup[i] > -1)
				bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
		}

		if (animManager->GetMouthID() > -1) {
			// Animate the head and jaw
			if (keyBoneLookup[BONE_HEAD] > -1)
					bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, animManager->GetMouthID(), animManager->GetMouthFrame());
			if (keyBoneLookup[BONE_JAW] > -1)
					bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, animManager->GetMouthID(), animManager->GetMouthFrame());
		} else {
			// Animate the head and jaw
			if (keyBoneLookup[BONE_HEAD] > -1)
					bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, a, t);
			if (keyBoneLookup[BONE_JAW] > -1)
					bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, a, t);
		}

		// still not sure what 18-26 bone lookups are but I think its more for things like wrist, etc which are not as visually obvious.
		for (size_t i=BONE_BTH; i<BONE_MAX; i++) {
			if (keyBoneLookup[i] > -1)
				bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
		}
		// =====

		if (charModelDetails.closeRHand) {
			a = closeFistID;
			t = anims[closeFistID].timeStart+1;
		} else {
			a = anim;
			t = time;
		}

		for (size_t i=0; i<5; i++) {
			if (keyBoneLookup[BONE_RFINGER1 + i] > -1) 
				bones[keyBoneLookup[BONE_RFINGER1 + i]].calcMatrix(bones, a, t);
		}

		if (charModelDetails.closeLHand) {
			a = closeFistID;
			t = anims[closeFistID].timeStart+1;
		} else {
			a = anim;
			t = time;
		}

		for (size_t i=0; i<5; i++) {
			if (keyBoneLookup[BONE_LFINGER1 + i] > -1)
				bones[keyBoneLookup[BONE_LFINGER1 + i]].calcMatrix(bones, a, t);
		}
	} else {
		for (ssize_t i=0; i<keyBoneLookup[BONE_ROOT]; i++) {
			bones[i].calcMatrix(bones, anim, time);
		}

		// The following line fixes 'mounts' in that the character doesn't get rotated, but it also screws up the rotation for the entire model :(
		//bones[18].calcMatrix(bones, anim, time, false);

		// Animate key skeletal bones except the fingers which we do later.
		// -----
		size_t a, t;

		// if we have a "secondary animation" selected,  animate upper body using that.
		if (animManager->GetSecondaryID() > -1) {
			a = animManager->GetSecondaryID();
			t = animManager->GetSecondaryFrame();
		} else {
			a = anim;
			t = time;
		}

		for (size_t i=0; i<animManager->GetSecondaryCount(); i++) { // only goto 5, otherwise it affects the hip/waist rotation for the lower-body.
			if (keyBoneLookup[i] > -1)
				bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
		}

		if (animManager->GetMouthID() > -1) {
			// Animate the head and jaw
			if (keyBoneLookup[BONE_HEAD] > -1)
					bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, animManager->GetMouthID(), animManager->GetMouthFrame());
			if (keyBoneLookup[BONE_JAW] > -1)
					bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, animManager->GetMouthID(), animManager->GetMouthFrame());
		} else {
			// Animate the head and jaw
			if (keyBoneLookup[BONE_HEAD] > -1)
					bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, a, t);
			if (keyBoneLookup[BONE_JAW] > -1)
					bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, a, t);
		}

		// still not sure what 18-26 bone lookups are but I think its more for things like wrist, etc which are not as visually obvious.
		for (size_t i=BONE_ROOT; i<BONE_MAX; i++) {
			if (keyBoneLookup[i] > -1)
				bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
		}
	}

	// Animate everything thats left with the 'default' animation
	for (size_t i=0; i<header.nBones; i++) {
		bones[i].calcMatrix(bones, anim, time);
	}
}

void WoWModel::animate(ssize_t anim)
{
	size_t t=0;
	
	ModelAnimation &a = anims[anim];
	int tmax = (a.timeEnd-a.timeStart);
	if (tmax==0) 
		tmax = 1;

	if (isWMO == true) {
		t = globalTime;
		t %= tmax;
		t += a.timeStart;
	} else
		t = animManager->GetFrame();
	
	this->animtime = t;
	this->anim = anim;

	if (animBones) // && (!animManager->IsPaused() || !animManager->IsParticlePaused()))
		calcBones(anim, t);

	if (animGeometry) {

		if (video.supportVBO)	{
			glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
			glBufferDataARB(GL_ARRAY_BUFFER_ARB, 2*vbufsize, NULL, GL_STREAM_DRAW_ARB);
			vertices = (Vec3D*)glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY);

			// Something has been changed in the past couple of days that is causing nasty bugs
			// this is an extra error check to prevent the program from crashing.
			if (!vertices) {
				LOG_ERROR << "void Model::animate(int anim), Vertex Buffer is null";
				return;
			}
		}

		// transform vertices
		ModelVertex *ov = origVertices;
		for (size_t i=0; i<header.nVertices; ++i,++ov) { //,k=0
			Vec3D v(0,0,0), n(0,0,0);

			for (size_t b=0; b<4; b++) {
				if (ov->weights[b]>0) {
					Vec3D tv = bones[ov->bones[b]].mat * ov->pos;
					Vec3D tn = bones[ov->bones[b]].mrot * ov->normal;
					v += tv * ((float)ov->weights[b] / 255.0f);
					n += tn * ((float)ov->weights[b] / 255.0f);
				}
			}

			vertices[i] = v;
			if (video.supportVBO)
				vertices[header.nVertices + i] = n.normalize(); // shouldn't these be normal by default?
			else
				normals[i] = n;
		}

		// clear bind
		if (video.supportVBO) {
			glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
		}
	}

	for (size_t i=0; i<header.nLights; i++) {
		if (lights[i].parent>=0) {
			lights[i].tpos = bones[lights[i].parent].mat * lights[i].pos;
			lights[i].tdir = bones[lights[i].parent].mrot * lights[i].dir;
		}
	}

	for (size_t i=0; i<header.nParticleEmitters; i++) {
		// random time distribution for teh win ..?
		//int pt = a.timeStart + (t + (int)(tmax*particleSystems[i].tofs)) % tmax;
		particleSystems[i].setup(anim, t);
	}

	for (size_t i=0; i<header.nRibbonEmitters; i++) {
		ribbons[i].setup(anim, t);
	}

	if (animTextures) {
		for (size_t i=0; i<header.nTexAnims; i++) {
			texAnims[i].calc(anim, t);
		}
	}
}







inline void WoWModel::drawModel()
{
	// assume these client states are enabled: GL_VERTEX_ARRAY, GL_NORMAL_ARRAY, GL_TEXTURE_COORD_ARRAY
	if (video.supportVBO && animated)	{
		// bind / point to the vertex normals buffer
		if (animGeometry) {
			glNormalPointer(GL_FLOAT, 0, GL_BUFFER_OFFSET(vbufsize));
		} else {
			glBindBufferARB(GL_ARRAY_BUFFER_ARB, nbuf);
			glNormalPointer(GL_FLOAT, 0, 0);
		}

		// Bind the vertex buffer
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
		glVertexPointer(3, GL_FLOAT, 0, 0);
		// Bind the texture coordinates buffer
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, tbuf);
		glTexCoordPointer(2, GL_FLOAT, 0, 0);
		
	} else if (animated) {
		glVertexPointer(3, GL_FLOAT, 0, vertices);
		glNormalPointer(GL_FLOAT, 0, normals);
		glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
	}
	
	// Display in wireframe mode?
	if (showWireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	
	// Render the various parts of the model.
	for (size_t i=0; i<passes.size(); i++) {
		ModelRenderPass &p = passes[i];

		if (p.init(this)) {
			// we don't want to render completely transparent parts

			// render
			if (animated) {
				//glDrawElements(GL_TRIANGLES, p.indexCount, GL_UNSIGNED_SHORT, indices + p.indexStart);
				// a GDC OpenGL Performace Tuning paper recommended glDrawRangeElements over glDrawElements
				// I can't notice a difference but I guess it can't hurt
				if (video.supportVBO && video.supportDrawRangeElements) {
					glDrawRangeElements(GL_TRIANGLES, p.vertexStart, p.vertexEnd, p.indexCount, GL_UNSIGNED_SHORT, indices + p.indexStart);
				} else {
					glBegin(GL_TRIANGLES);
					for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
						uint16 a = indices[b];
						glNormal3fv(normals[a]);
						glTexCoord2fv(origVertices[a].texcoords);
						glVertex3fv(vertices[a]);
					}
					glEnd();
				}
			} else {
				glBegin(GL_TRIANGLES);
				for (size_t k = 0, b=p.indexStart; k<p.indexCount; k++,b++) {
					uint16 a = indices[b];
					glNormal3fv(normals[a]);
					glTexCoord2fv(origVertices[a].texcoords);
					glVertex3fv(vertices[a]);
				}
				glEnd();
			}

			p.deinit();
		}
	}
	
	if (showWireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// clean bind
	if (video.supportVBO && animated) {
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	}
	// done with all render ops
}

inline void WoWModel::draw()
{
	if (!ok)
		return;

	if (!animated) {
		if(showModel)
			glCallList(dlist);

	} else {
		if (ind) {
			animate(currentAnim);
		} else {
			if (!animcalc) {
				animate(currentAnim);
				//animcalc = true; // Not sure what this is really for but it breaks WMO animation
			}
		}

		if(showModel)
			drawModel();
	}
}

// These aren't really needed in the model viewer.. only wowmapviewer
void WoWModel::lightsOn(GLuint lbase)
{
	// setup lights
	for (size_t i=0, l=lbase; i<header.nLights; i++) 
		lights[i].setup(animtime, (GLuint)l++);
}

// These aren't really needed in the model viewer.. only wowmapviewer
void WoWModel::lightsOff(GLuint lbase)
{
	for (size_t i=0, l=lbase; i<header.nLights; i++) 
		glDisable((GLenum)l++);
}

// Updates our particles within models.
void WoWModel::updateEmitters(float dt)
{
	if (!ok || !showParticles || !GLOBALSETTINGS.bShowParticle)
		return;

	for (size_t i=0; i<header.nParticleEmitters; i++) {
		particleSystems[i].update(dt);
	}
}


// Draws the "bones" of models  (skeletal animation)
void WoWModel::drawBones()
{
	glDisable(GL_DEPTH_TEST);
	glBegin(GL_LINES);
	for (size_t i=0; i<header.nBones; i++) {
	//for (size_t i=30; i<40; i++) {
		if (bones[i].parent != -1) {
			glVertex3fv(bones[i].transPivot);
			glVertex3fv(bones[bones[i].parent].transPivot);
		}
	}
	glEnd();
	glEnable(GL_DEPTH_TEST);
}

// Sets up the models attachments
void WoWModel::setupAtt(int id)
{
	int l = attLookup[id];
	if (l>-1)
		atts[l].setup();
}

// Sets up the models attachments
void WoWModel::setupAtt2(int id)
{
	int l = attLookup[id];
	if (l>=0)
		atts[l].setupParticle();
}

// Draws the Bounding Volume, which is used for Collision detection.
void WoWModel::drawBoundingVolume()
{
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glBegin(GL_TRIANGLES);
	for (size_t i=0; i<header.nBoundingTriangles; i++) {
		size_t v = boundTris[i];
		if (v < header.nBoundingVertices)
			glVertex3fv(bounds[v]);
		else 
			glVertex3f(0,0,0);
	}
	glEnd();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Renders our particles into the pipeline.
void WoWModel::drawParticles()
{
	// draw particle systems
	for (size_t i=0; i<header.nParticleEmitters; i++) {
		if (particleSystems != NULL)
			particleSystems[i].draw();
	}

	// draw ribbons
	for (size_t i=0; i<header.nRibbonEmitters; i++) {
		if (ribbons != NULL)
			ribbons[i].draw();
	}
}

WoWItem * WoWModel::getItem(CharSlots slot)
{

  for(WoWModel::iterator it = this->begin();
      it != this->end() ;
      ++it)
    {
     if((*it)->slot() == slot)
       return *it;
    }

  return 0;
}

void WoWModel::UpdateTextureList(QString texName, int special)
{
  for (size_t i=0; i< header.nTextures; i++)
  {
    if (specialTextures[i] == special)
    {
      LOG_INFO << "Updating" << TextureList[i] << "to" << texName;
      TextureList[i] = texName;
      break;
    }
  }
}

std::map<int, std::string> WoWModel::getAnimsMap()
{
  std::map<int, std::string> result;
  if (animated && anims)
  {
    QString query = "SELECT ID,NAME FROM AnimationData WHERE ID IN(";
    for (unsigned int i=0; i<header.nAnimations; i++)
    {
      query += QString::number(anims[i].animID);
      if(i < header.nAnimations -1)
        query += ",";
      else
        query +=  ")";
    }

    sqlResult animsResult = GAMEDATABASE.sqlQuery(query);

    if(animsResult.valid && !animsResult.empty())
    {
      LOG_INFO << "Found" << animsResult.values.size() << "animations for model";

      // remap database results on model header indexes
      for(int i=0, imax=animsResult.values.size() ; i < imax ; i++)
      {
        result[animsResult.values[i][0].toInt()] = animsResult.values[i][1].toStdString();
      }
    }
  }
  return result;
}

void WoWModel::save(QXmlStreamWriter &stream)
{
  stream.writeStartElement("model");
  stream.writeStartElement("file");
  stream.writeAttribute("name", QString::fromStdString(modelname));
  stream.writeEndElement();
  cd.save(stream);
  stream.writeEndElement(); // model
}

void WoWModel::load(QXmlStreamReader &stream)
{
  cd.load(stream);
}

bool WoWModel::canSetTextureFromFile(int texnum)
{
  int textype;

  switch (texnum)
  {
    case 1:
      textype = TEXTURE_GAMEOBJECT1;
      break;
    case 2:
      textype = TEXTURE_GAMEOBJECT2;
      break;
    case 3:
      textype = TEXTURE_GAMEOBJECT3;
      break;
    default:
      return false;
  }

  for (size_t i=0; i<TEXTURE_MAX; i++)
  {
    if (specialTextures[i] == textype)
      return 1;
  }
  return 0;
}
