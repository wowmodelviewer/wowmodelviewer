#include "WoWModel.h"
#include <algorithm>
#include <cassert>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include "Attachment.h"
#include "CASCFile.h"
#include "Game.h"
#include "GlobalSettings.h"
#include "ModelColor.h"
#include "ModelEvent.h"
#include "ModelLight.h"
#include "ModelRenderPass.h"
#include "ModelTransparency.h"
#include "video.h"
#include "logger/Logger.h"
#include <QXmlStreamWriter>
#include "glm/gtc/epsilon.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/norm.hpp"

#define GL_BUFFER_OFFSET(i) ((char *)(0) + (i))

enum TextureFlags
{
	TEXTURE_WRAPX = 1,
	TEXTURE_WRAPY
};

void WoWModel::dumpTextureStatus()
{
	LOG_INFO << "-----------------------------------------";

	for (uint i = 0; i < textures.size(); i++)
		LOG_INFO << "textures[" << i << "] =" << textures[i];

	for (uint i = 0; i < specialTextures.size(); i++)
		LOG_INFO << "specialTextures[" << i << "] =" << specialTextures[i];

	for (uint i = 0; i < replaceTextures.size(); i++)
		LOG_INFO << "replaceTextures[" << i << "] =" << replaceTextures[i];

	LOG_INFO << " #### TEXTUREMANAGER ####";
	TEXTUREMANAGER.dump();
	LOG_INFO << " ########################";

	for (uint i = 0; i < passes.size(); i++)
		LOG_INFO << "passes[" << i << "] -> tex =" << passes[i]->tex << "specialTex" << passes[i]->specialTex <<
			"useTex2" << passes[i]->useTex2;

	LOG_INFO << "-----------------------------------------";
}

void glGetAll()
{
	GLint bled;
	LOG_INFO << "glGetAll Information";
	LOG_INFO << "GL_ALPHA_TEST:" << glIsEnabled(GL_ALPHA_TEST);
	LOG_INFO << "GL_BLEND:" << glIsEnabled(GL_BLEND);
	LOG_INFO << "GL_CULL_FACE:" << glIsEnabled(GL_CULL_FACE);
	glGetIntegerv(GL_FRONT_FACE, &bled);
	if (bled == GL_CW)
	{
		LOG_INFO << "glFrontFace: GL_CW";
	}
	else if (bled == GL_CCW)
	{
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
	glLightfv(GL_LIGHT0, GL_DIFFUSE, glm::value_ptr(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)));
	glLightfv(GL_LIGHT0, GL_AMBIENT, glm::value_ptr(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)));
	glLightfv(GL_LIGHT0, GL_SPECULAR, glm::value_ptr(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)));
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

WoWModel::WoWModel(GameFile* file, bool forceAnim):
	ManagedItem(""),
	forceAnim(forceAnim),
	gamefile(file)
{
	// Initiate our model variables.
	trans = 1.0f;
	rad = 1.0f;
	pos_ = glm::vec3(0.0f, 0.0f, 0.0f);
	rot_ = glm::vec3(0.0f, 0.0f, 0.0f);
	scale_ = 1.0f;

	specialTextures.resize(TEXTURE_MAX, -1);
	replaceTextures.resize(TEXTURE_MAX, ModelRenderPass::INVALID_TEX);

	for (short& i : attLookup)
		i = -1;

	for (short& i : keyBoneLookup)
		i = -1;

	dlist = 0;

	hasCamera = false;
	hasParticles = false;
	replaceParticleColors = false;
	replacableParticleColorIDs.clear();
	creatureGeosetData.clear();
	creatureGeosetDataID = 0;

	isWMO = false;
	isMount = false;

	showModel = false;
	showBones = false;
	showBounds = false;
	showWireframe = false;
	showParticles = false;
	showTexture = true;
	mirrored_ = false;

	charModelDetails.Reset();

	vbuf = nbuf = tbuf = 0;

	origVertices.clear();
	vertices = nullptr;
	normals = nullptr;
	texCoords = nullptr;
	indices.clear();

	animtime = 0;
	anim = 0;
	animManager = nullptr;
	currentAnim = 0;
	modelType = MT_NORMAL;
	attachment = nullptr;

	rawVertices.clear();
	rawIndices.clear();
	rawPasses.clear();
	rawGeosets.clear();

	mergedModelType = 0;

	initCommon();
}

WoWModel::~WoWModel()
{
	if (ok)
	{
		if (attachment)
			attachment->setModel(nullptr);

		// There is a small memory leak somewhere with the textures.
		// Especially if the texture was built into the model.
		// No matter what I try though I can't find the memory to unload.
		if (header.nTextures)
		{
			// For character models, the texture isn't loaded into the texture manager, manually remove it
			glDeleteTextures(1, &replaceTextures[1]);
			delete animManager;
			animManager = nullptr;

			if (animated)
			{
				// unload all sorts of crap
				// Need this if statement because VBO supported
				// cards have already deleted it.
				if (video.supportVBO)
				{
					glDeleteBuffersARB(1, &nbuf);
					glDeleteBuffersARB(1, &vbuf);
					glDeleteBuffersARB(1, &tbuf);

					vertices = nullptr;
				}

				delete[] normals;
				normals = nullptr;
				delete[] vertices;
				vertices = nullptr;
				delete[] texCoords;
				texCoords = nullptr;

				indices.clear();
				rawIndices.clear();
				origVertices.clear();
				rawVertices.clear();
				texAnims.clear();
				colors.clear();
				transparency.clear();
				lights.clear();
				particleSystems.clear();
				ribbons.clear();
				events.clear();

				for (const auto it : passes)
					delete it;

				for (const auto it : geosets)
					delete it;
			}
			else
			{
				glDeleteLists(dlist, 1);
			}
		}
	}
}

void WoWModel::displayHeader(ModelHeader& a_header)
{
	LOG_INFO << "id:" << a_header.id[0] << a_header.id[1] << a_header.id[2] << a_header.id[3];
	LOG_INFO << "version:" << (int)a_header.version[0] << (int)a_header.version[1] << (int)a_header.version[2] << (int)
		a_header.version[3];
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

	//  LOG_INFO << "collisionSphere :";
	//  displaySphere(a_header.collisionSphere);
	//  LOG_INFO << "boundSphere :";
	//  displaySphere(a_header.boundSphere);

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

bool WoWModel::isAnimated()
{
	// see if we have any animated bones
	ModelBoneDef* bo = reinterpret_cast<ModelBoneDef*>(gamefile->getBuffer() + header.ofsBones);

	animGeometry = false;
	animBones = false;
	ind = false;

	for (auto ov_it = origVertices.begin(), ov_end = origVertices.end(); (ov_it != ov_end) && !animGeometry; ++ov_it)
	{
		for (size_t b = 0; b < 4; b++)
		{
			if (ov_it->weights[b] > 0)
			{
				const ModelBoneDef& bb = bo[ov_it->bones[b]];
				if (bb.translation.type || bb.rotation.type || bb.scaling.type || (bb.flags & MODELBONE_BILLBOARD))
				{
					if (bb.flags & MODELBONE_BILLBOARD)
					{
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
	{
		animBones = true;
	}
	else
	{
		for (uint i = 0; i < bones.size(); i++)
		{
			const ModelBoneDef& bb = bo[i];
			if (bb.translation.type || bb.rotation.type || bb.scaling.type)
			{
				animBones = true;
				animGeometry = true;
				break;
			}
		}
	}

	bool animMisc = header.nCameras > 0 || // why waste time, pretty much all models with cameras need animation
		header.nLights > 0 || // same here
		header.nParticleEmitters > 0 ||
		header.nRibbonEmitters > 0;

	if (animMisc)
		animBones = true;

	// animated colors
	if (header.nColors)
	{
		const ModelColorDef* cols = reinterpret_cast<ModelColorDef*>(gamefile->getBuffer() + header.ofsColors);
		for (size_t i = 0; i < header.nColors; i++)
		{
			if (cols[i].color.type != 0 || cols[i].opacity.type != 0)
			{
				animMisc = true;
				break;
			}
		}
	}

	// animated opacity
	if (header.nTransparency && !animMisc)
	{
		const ModelTransDef* trs = reinterpret_cast<ModelTransDef*>(gamefile->getBuffer() + header.ofsTransparency);
		for (size_t i = 0; i < header.nTransparency; i++)
		{
			if (trs[i].trans.type != 0)
			{
				animMisc = true;
				break;
			}
		}
	}

	// guess not...
	return animGeometry || (header.nTexAnims > 0) || animMisc;
}

void WoWModel::initCommon()
{
	// --
	ok = false;

	if (!gamefile)
		return;

	if (!gamefile->open() || gamefile->isEof() || (gamefile->getSize() < sizeof(ModelHeader)))
	{
		LOG_ERROR << "Unable to load model:" << gamefile->fullname();
		gamefile->close();
		return;
	}

	if (gamefile->isChunked() && !gamefile->setChunk("MD21")) // Legion chunked files
	{
		LOG_ERROR << "Unable to set chunk to MD21 for model:" << gamefile->fullname();
		gamefile->close();
		return;
	}

	setItemName(gamefile->fullname());

	initRaceInfos();
	cd.reset(this);

	// replace .MDX with .M2
	QString tempname = gamefile->fullname();
	tempname.replace(".mdx", ".m2");

	ok = true;

	memcpy(&header, gamefile->getBuffer(), sizeof(ModelHeader));

	LOG_INFO << "Loading model:" << tempname << "size:" << gamefile->getSize();

	// displayHeader(header);

	if (header.id[0] != 'M' && header.id[1] != 'D' && header.id[2] != '2' && header.id[3] != '0')
	{
		LOG_ERROR << "Invalid model!  May be corrupted. Header id:" << header.id[0] << header.id[1] << header.id[2] <<
			header.id[3];

		ok = false;
		gamefile->close();
		return;
	}

	if (header.GlobalModelFlags & 0x200000)
		model24500 = true;

	modelname = tempname.toStdString();
	QStringList list = tempname.split("\\");
	setName(list[list.size() - 1].replace(".m2", ""));

	// Error check
	// 10 1 0 0 = WoW 5.0 models (as of 15464)
	// 10 1 0 0 = WoW 4.0.0.12319 models
	// 9 1 0 0 = WoW 4.0 models
	// 8 1 0 0 = WoW 3.0 models
	// 4 1 0 0 = WoW 2.0 models
	// 0 1 0 0 = WoW 1.0 models

	if (gamefile->getSize() < header.ofsParticleEmitters)
	{
		LOG_ERROR << "Unable to load the Model \"" << tempname << "\", appears to be corrupted.";
		gamefile->close();
		return;
	}

	// init race info


	if (gamefile->isChunked() && gamefile->setChunk("SKID"))
	{
		uint32 skelFileID;
		gamefile->read(&skelFileID, sizeof(skelFileID));
		GameFile* skelFile = GAMEDIRECTORY.getFile(skelFileID);

		if (skelFile->open())
		{
			if (skelFile->setChunk("SKS1"))
			{
				SKS1 sks1;
				memcpy(&sks1, skelFile->getBuffer(), sizeof(SKS1));

				if (sks1.nGlobalSequences > 0)
				{
					// vector.assign() isn't working:
					// globalSequences.assign(skelFile->getBuffer() + sks1.ofsGlobalSequences, skelFile->getBuffer() + sks1.ofsGlobalSequences + sks1.nGlobalSequences);
					uint32* buffer = new uint32[sks1.nGlobalSequences];
					memcpy(buffer, skelFile->getBuffer() + sks1.ofsGlobalSequences,
					       sizeof(uint32) * sks1.nGlobalSequences);
					globalSequences.assign(buffer, buffer + sks1.nGlobalSequences);
					delete[] buffer;
				}

				// let's try to read parent skel file if needed
				if (skelFile->setChunk("SKPD"))
				{
					SKPD skpd;
					memcpy(&skpd, skelFile->getBuffer(), sizeof(SKPD));

					GameFile* parentFile = GAMEDIRECTORY.getFile(skpd.parentFileId);

					if (parentFile && parentFile->open() && parentFile->setChunk("SKS1"))
					{
						SKS1 Sks1;
						memcpy(&Sks1, parentFile->getBuffer(), sizeof(SKS1));

						if (Sks1.nGlobalSequences > 0)
						{
							uint32* buffer = new uint32[Sks1.nGlobalSequences];
							memcpy(buffer, parentFile->getBuffer() + Sks1.ofsGlobalSequences,
							       sizeof(uint32) * Sks1.nGlobalSequences);
							for (uint i = 0; i < Sks1.nGlobalSequences; i++)
								globalSequences.push_back(buffer[i]);
						}

						parentFile->close();
					}
				}
			}
			skelFile->close();
		}
		gamefile->setChunk("MD21");
	}
	else if (header.nGlobalSequences)
	{
		// vector.assign() isn't working:
		// globalSequences.assign(gamefile->getBuffer() + header.ofsGlobalSequences, gamefile->getBuffer() + header.ofsGlobalSequences + header.nGlobalSequences);
		uint32* buffer = new uint32[header.nGlobalSequences];
		memcpy(buffer, gamefile->getBuffer() + header.ofsGlobalSequences, sizeof(uint32) * header.nGlobalSequences);
		globalSequences.assign(buffer, buffer + header.nGlobalSequences);
		delete[] buffer;
	}

	if (gamefile->isChunked() && gamefile->setChunk("SFID"))
	{
		uint32 skinfile;

		if (header.nViews > 0)
		{
			for (uint i = 0; i < header.nViews; i++)
			{
				gamefile->read(&skinfile, sizeof(skinfile));
				skinFileIDs.push_back(skinfile);
				LOG_INFO << "Adding skin file" << i << ":" << skinfile;
				// If the first view is the best, and we don't need to switch to a lower one, then maybe we don't need to store all these file IDs, but we can for now.
			}
		}
		// LOD .skin file IDs are next in SFID, but we'll ignore them. They're probably unnecessary in a model viewer.

		gamefile->setChunk("MD21");
	}

	if (forceAnim)
		animBones = true;

	// Ready to render.
	showModel = true;
	alpha_ = 1.0f;

	ModelVertex* buffer = new ModelVertex[header.nVertices];
	memcpy(buffer, gamefile->getBuffer() + header.ofsVertices, sizeof(ModelVertex) * header.nVertices);
	rawVertices.assign(buffer, buffer + header.nVertices);
	delete[] buffer;

	origVertices = rawVertices;

	// This data is needed for both VBO and non-VBO cards.
	vertices = new glm::vec3[origVertices.size()];
	normals = new glm::vec3[origVertices.size()];

	uint i = 0;
	for (auto ov_it = origVertices.begin(), ov_end = origVertices.end(); ov_it != ov_end; i++, ov_it++)
	{
		// Set the data for our vertices, normals from the model data
		vertices[i] = ov_it->pos;
		normals[i] = glm::normalize(ov_it->normal);

		float len = glm::length2(ov_it->pos);
		if (len > rad)
		{
			rad = len;
		}
	}

	// model vertex radius
	rad = sqrtf(rad);

	// bounds
	if (header.nBoundingVertices > 0)
	{
		glm::vec3* Buffer = new glm::vec3[header.nBoundingVertices];
		memcpy(Buffer, gamefile->getBuffer() + header.ofsBoundingVertices,
		       sizeof(glm::vec3) * header.nBoundingVertices);
		bounds.assign(Buffer, Buffer + header.nBoundingVertices);
		delete[] Buffer;
	}

	if (header.nBoundingTriangles > 0)
	{
		uint16* Buffer = new uint16[header.nBoundingTriangles];
		memcpy(Buffer, gamefile->getBuffer() + header.ofsBoundingTriangles, sizeof(uint16) * header.nBoundingTriangles);
		boundTris.assign(Buffer, Buffer + header.nBoundingTriangles);
		delete[] Buffer;
	}

	// textures
	ModelTextureDef* texdef = reinterpret_cast<ModelTextureDef*>(gamefile->getBuffer() + header.ofsTextures);
	if (header.nTextures)
	{
		textures.resize(TEXTURE_MAX, ModelRenderPass::INVALID_TEX);

		std::vector<TXID> txids;

		if (gamefile->isChunked() && gamefile->setChunk("TXID"))
		{
			txids = readTXIDSFromFile(gamefile);
			gamefile->setChunk("MD21", false);
		}

		for (size_t I = 0; I < header.nTextures; I++)
		{
			/*
			Texture Types
			Texture type is 0 for textures whose file IDs or names are contained in the the model file.
			The older implementation has full file paths, but the newer uses file data IDs
			contained in a TXID chunk. We have to support both for now.
	  
			All other texture types (nonzero) are for textures that are obtained from other files.
			For instance, in the NightElfFemale model, her eye glow is a type 0 texture and has a
			file name. Her other 3 textures have types of 1, 2 and 6. The texture filenames for these
			come from client database files:
	  
			DBFilesClient\CharSections.dbc
			DBFilesClient\CreatureDisplayInfo.dbc
			DBFilesClient\ItemDisplayInfo.dbc
			(possibly more)
	  
			0   Texture given in filename
			1   Body + clothes
			2   Cape
			6   Hair, beard
			8   Tauren fur
			11 Skin for creatures #1
			12 Skin for creatures #2
			13 Skin for creatures #3
	  
			Texture Flags
			Value   Meaning
			1  Texture wrap X
			2  Texture wrap Y
			*/

			if (texdef[I].type == TEXTURE_FILENAME) // 0
			{
				GameFile* Tex;
				if (txids.size() > 0)
				{
					Tex = GAMEDIRECTORY.getFile(txids[I].fileDataId);
				}
				else
				{
					QString texname(reinterpret_cast<char*>(gamefile->getBuffer() + texdef[I].nameOfs));
					Tex = GAMEDIRECTORY.getFile(texname);
				}
				textures[I] = TEXTUREMANAGER.add(Tex);
			}
			else // non-zero
			{
				// special texture - only on characters and such...
				specialTextures[I] = texdef[I].type;

				if (texdef[I].type == TEXTURE_WEAPON_BLADE) // a fix for weapons with type-3 textures.
					replaceTextures[texdef[I].type] = TEXTUREMANAGER.add(
						GAMEDIRECTORY.getFile(R"(Item\ObjectComponents\Weapon\ArmorReflect4.BLP)"));
			}
		}
	}

	/*
	// replacable textures - it seems to be better to get this info from the texture types
	if (header.nTexReplace) {
	size_t m = header.nTexReplace;
	if (m>16) m = 16;
	int16 *texrep = (int16*)(gamefile->getBuffer() + header.ofsTexReplace);
	for (size_t i=0; i<m; i++) specialTextures[i] = texrep[i];
	}
	*/

	if (gamefile->isChunked() && gamefile->setChunk("SKID"))
	{
		uint32 skelFileID;
		gamefile->read(&skelFileID, sizeof(skelFileID));
		GameFile* skelFile = GAMEDIRECTORY.getFile(skelFileID);

		if (skelFile->open())
		{
			if (skelFile->setChunk("SKA1"))
			{
				SKA1 ska1;
				memcpy(&ska1, skelFile->getBuffer(), sizeof(SKA1));
				header.nAttachments = ska1.nAttachments;
				ModelAttachmentDef* attachments = reinterpret_cast<ModelAttachmentDef*>(skelFile->getBuffer() + ska1.ofsAttachments);
				for (size_t I = 0; I < ska1.nAttachments; I++)
				{
					ModelAttachment att;
					att.model = this;
					att.init(attachments[I]);
					atts.push_back(att);
				}

				header.nAttachLookup = ska1.nAttachLookup;
				if (ska1.nAttachLookup > 0)
				{
					int16* p = reinterpret_cast<int16*>(skelFile->getBuffer() + ska1.ofsAttachLookup);
					if (ska1.nAttachLookup > ATT_MAX)
						LOG_ERROR << "Model AttachLookup" << ska1.nAttachLookup << "over" << ATT_MAX;
					for (size_t I = 0; I < ska1.nAttachLookup; I++)
					{
						if (I > ATT_MAX - 1)
							break;
						attLookup[I] = p[I];
					}
				}
			}
			skelFile->close();
		}
		gamefile->setChunk("MD21");
	}
	else
	{
		// attachments
		if (header.nAttachments)
		{
			ModelAttachmentDef* attachments = reinterpret_cast<ModelAttachmentDef*>(gamefile->getBuffer() + header.ofsAttachments);
			for (size_t I = 0; I < header.nAttachments; I++)
			{
				ModelAttachment att;
				att.model = this;
				att.init(attachments[I]);
				atts.push_back(att);
			}
		}

		if (header.nAttachLookup)
		{
			int16* p = reinterpret_cast<int16*>(gamefile->getBuffer() + header.ofsAttachLookup);
			if (header.nAttachLookup > ATT_MAX)
				LOG_ERROR << "Model AttachLookup" << header.nAttachLookup << "over" << ATT_MAX;
			for (size_t I = 0; I < header.nAttachLookup; I++)
			{
				if (I > ATT_MAX - 1)
					break;
				attLookup[I] = p[I];
			}
		}
	}


	// init colors
	if (header.nColors)
	{
		colors.resize(header.nColors);
		ModelColorDef* colorDefs = reinterpret_cast<ModelColorDef*>(gamefile->getBuffer() + header.ofsColors);
		for (uint I = 0; I < colors.size(); I++)
			colors[I].init(gamefile, colorDefs[I], globalSequences);
	}

	// init transparency
	if (header.nTransparency)
	{
		transparency.resize(header.nTransparency);
		ModelTransDef* trDefs = reinterpret_cast<ModelTransDef*>(gamefile->getBuffer() + header.ofsTransparency);
		for (uint I = 0; I < header.nTransparency; I++)
			transparency[I].init(gamefile, trDefs[I], globalSequences);
	}

	if (header.nViews)
	{
		// just use the first LOD/view
		// First LOD/View being the worst?
		// TODO: Add support for selecting the LOD.
		// int viewLOD = 0; // sets LOD to worst
		// int viewLOD = header.nViews - 1; // sets LOD to best
		setLOD(0); // Set the default Level of Detail to the best possible.
	}

	// proceed with specialized init depending on model "type"

	animated = isAnimated() || forceAnim; // isAnimated will set animGeometry

	if (animated)
		initAnimated();
	else
		initStatic();

	QString query = QString("SELECT CreatureGeosetDataID "
			"FROM CreatureModelData "
			"WHERE FileDataID = %1")
		.arg(gamefile->fileDataId());
	sqlResult r = GAMEDATABASE.sqlQuery(query);
	if (r.valid && !r.values.empty())
	{
		creatureGeosetDataID = r.values[0][0].toInt();
	}

	gamefile->close();
}

void WoWModel::initStatic()
{
	dlist = glGenLists(1);
	glNewList(dlist, GL_COMPILE);

	drawModel();

	glEndList();

	// clean up vertices, indices etc
	delete[] vertices;
	vertices = nullptr;
	delete[] normals;
	normals = nullptr;
	indices.clear();
}

void WoWModel::initRaceInfos()
{
	// This mapping links *_sdr character models to their HD equivalents, so they can get race info for display.
	// *_sdr models are actually now obsolete and the database that used to provide this info is now empty,
	// but while the models are still appearing in the model tree we may as well keep them working, so they
	// don't look broken:
	std::map<int, int> SDReplacementModel = // {SDRFileID , HDFileID}
	{
		{1838568, 119369}, {1838570, 119376}, {1838201, 307453}, {1838592, 307454}, {1853956, 535052},
		{1853610, 589715}, {1838560, 878772}, {1838566, 900914}, {1838578, 917116}, {1838574, 921844},
		{1838564, 940356}, {1838580, 949470}, {1838562, 950080}, {1838584, 959310}, {1838586, 968705},
		{1838576, 974343}, {1839008, 986648}, {1838582, 997378}, {1838572, 1000764}, {1839253, 1005887},
		{1838385, 1011653}, {1838588, 1018060}, {1822372, 1022598}, {1838590, 1022938}, {1853408, 1100087},
		{1839709, 1100258}, {1825438, 1593999}, {1839042, 1620605}, {1858265, 1630218}, {1859379, 1630402},
		{1900779, 1630447}, {1894572, 1662187}, {1859345, 1733758}, {1858367, 1734034}, {1858099, 1810676},
		{1857801, 1814471}, {1892825, 1890763}, {1892543, 1890765}, {1968838, 1968587}, {1842700, 1000764}
	};

	auto fdid = gamefile->fileDataId();
	if (SDReplacementModel.count(fdid))
		// if it's an old *_sdr model, use the file ID of its HD counterpart for race info
		fdid = SDReplacementModel[fdid];

	if (!RaceInfos::getRaceInfosForFileID(fdid, infos))
		LOG_ERROR << "Unable to retrieve race infos for model" << gamefile->fullname() << gamefile->fileDataId();
}

std::vector<TXID> WoWModel::readTXIDSFromFile(GameFile* f)
{
	std::vector<TXID> txids;

	if (f->setChunk("TXID"))
	{
		TXID txid;
		while (!f->isEof())
		{
			f->read(&txid, sizeof(TXID));
			txids.push_back(txid);
		}
	}
	return txids;
}

std::vector<AFID> WoWModel::readAFIDSFromFile(GameFile* f)
{
	std::vector<AFID> afids;

	if (f->setChunk("AFID"))
	{
		AFID afid;
		while (!f->isEof())
		{
			f->read(&afid, sizeof(AFID));
			if (afid.fileId != 0)
				afids.push_back(afid);
		}
	}

	return afids;
}

void WoWModel::readAnimsFromFile(GameFile* f, std::vector<AFID>& afids, modelAnimData& data, uint32 nAnimations,
                                 uint32 ofsAnimation, uint32 nAnimationLookup, uint32 ofsAnimationLookup)
{
	for (uint i = 0; i < nAnimations; i++)
	{
		ModelAnimation a;
		memcpy(&a, f->getBuffer() + ofsAnimation + i * sizeof(ModelAnimation), sizeof(ModelAnimation));

		anims.push_back(a);

		GameFile* Anim = nullptr;

		// if we have animation file ids from AFID chunk, use them
		if (afids.size() > 0)
		{
			for (const auto it : afids)
			{
				if ((it.animId == anims[i].animID) && (it.subAnimId == anims[i].subAnimID))
				{
					Anim = GAMEDIRECTORY.getFile(it.fileId);
					break;
				}
			}
		}
		else // else use file naming to get them
		{
			QString tempname = QString::fromStdString(modelname).replace(".m2", "");
			tempname = QString("%1%2-%3.anim").arg(tempname).arg(anims[i].animID, 4, 10, QChar('0')).arg(
				anims[i].subAnimID, 2, 10, QChar('0'));
			Anim = GAMEDIRECTORY.getFile(tempname);
		}

		if (Anim && Anim->open())
		{
			Anim->setChunk("AFSB"); // try to set chunk if it exist, no effect if there is no AFSB chunk present
			{
				auto animIt = data.animfiles.find(anims[i].animID);
				if (animIt != data.animfiles.end())
					LOG_INFO << "WARNING - replacing" << data.animfiles[anims[i].animID].first->fullname() << "by" <<
						Anim->fullname();
			}

			data.animfiles[anims[i].animID] = std::make_pair(Anim, f);
		}
	}

	// Index at ofsAnimations which represents the animation in AnimationData.dbc. -1 if none.
	if (nAnimationLookup > 0)
	{
		// for unknown reason, using assign() on vector doesn't work
		// use intermediate buffer and push back instead...
		int16* buffer = new int16[nAnimationLookup];
		memcpy(buffer, f->getBuffer() + ofsAnimationLookup, sizeof(int16) * nAnimationLookup);
		for (uint i = 0; i < nAnimationLookup; i++)
			animLookups.push_back(buffer[i]);

		delete[] buffer;
	}
}

void WoWModel::initAnimated()
{
	modelAnimData data;
	data.globalSequences = globalSequences;

	if (gamefile->isChunked() && gamefile->setChunk("SKID"))
	{
		uint32 skelFileID;
		gamefile->read(&skelFileID, sizeof(skelFileID));
		GameFile* skelFile = GAMEDIRECTORY.getFile(skelFileID);

		if (skelFile->open())
		{
			// skelFile->dumpStructure();
			std::vector<AFID> afids = readAFIDSFromFile(skelFile);

			if (skelFile->setChunk("SKS1"))
			{
				SKS1 sks1;

				// let's try if there is a parent skel file to read
				GameFile* parentFile = nullptr;
				if (skelFile->setChunk("SKPD"))
				{
					SKPD skpd;
					skelFile->read(&skpd, sizeof(skpd));

					parentFile = GAMEDIRECTORY.getFile(skpd.parentFileId);

					if (parentFile && parentFile->open())
					{
						// parentFile->dumpStructure();
						afids = readAFIDSFromFile(parentFile);

						if (parentFile->setChunk("SKS1"))
						{
							SKS1 Sks1;
							parentFile->read(&Sks1, sizeof(Sks1));
							readAnimsFromFile(parentFile, afids, data, Sks1.nAnimations, Sks1.ofsAnimations,
							                  Sks1.nAnimationLookup, Sks1.ofsAnimationLookup);
						}

						parentFile->close();
					}
				}
				else
				{
					skelFile->read(&sks1, sizeof(sks1));
					memcpy(&sks1, skelFile->getBuffer(), sizeof(SKS1));
					readAnimsFromFile(skelFile, afids, data, sks1.nAnimations, sks1.ofsAnimations,
					                  sks1.nAnimationLookup, sks1.ofsAnimationLookup);
				}

				animManager = new AnimManager(*this);

				// init bones...
				if (skelFile->setChunk("SKB1"))
				{
					GameFile* fileToUse = skelFile;
					if (parentFile)
					{
						parentFile->open();
						parentFile->setChunk("SKB1");
						skelFile->close();
						fileToUse = parentFile;
					}

					SKB1 skb1;
					fileToUse->read(&skb1, sizeof(skb1));
					memcpy(&skb1, fileToUse->getBuffer(), sizeof(SKB1));
					bones.resize(skb1.nBones);
					ModelBoneDef* mb = reinterpret_cast<ModelBoneDef*>(fileToUse->getBuffer() + skb1.ofsBones);

					for (uint i = 0; i < anims.size(); i++)
						data.animIndexToAnimId[i] = anims[i].animID;

					for (size_t i = 0; i < skb1.nBones; i++)
						bones[i].initV3(*fileToUse, mb[i], data);

					// Block keyBoneLookup is a lookup table for Key Skeletal Bones, hands, arms, legs, etc.
					if (skb1.nKeyBoneLookup < BONE_MAX)
					{
						memcpy(keyBoneLookup, fileToUse->getBuffer() + skb1.ofsKeyBoneLookup,
						       sizeof(int16) * skb1.nKeyBoneLookup);
					}
					else
					{
						memcpy(keyBoneLookup, fileToUse->getBuffer() + skb1.ofsKeyBoneLookup, sizeof(int16) * BONE_MAX);
						LOG_ERROR << "KeyBone number" << skb1.nKeyBoneLookup << "over" << BONE_MAX;
					}
					fileToUse->close();
				}
			}
			skelFile->close();
		}
		gamefile->setChunk("MD21", false);
	}
	else if (header.nAnimations > 0)
	{
		std::vector<AFID> afids;

		if (gamefile->isChunked() && gamefile->setChunk("AFID"))
		{
			afids = readAFIDSFromFile(gamefile);
			gamefile->setChunk("MD21", false);
		}

		readAnimsFromFile(gamefile, afids, data, header.nAnimations, header.ofsAnimations, header.nAnimationLookup,
		                  header.ofsAnimationLookup);

		animManager = new AnimManager(*this);

		// init bones...
		bones.resize(header.nBones);
		ModelBoneDef* mb = reinterpret_cast<ModelBoneDef*>(gamefile->getBuffer() + header.ofsBones);

		for (uint i = 0; i < anims.size(); i++)
			data.animIndexToAnimId[i] = anims[i].animID;

		for (uint i = 0; i < bones.size(); i++)
			bones[i].initV3(*gamefile, mb[i], data);

		// Block keyBoneLookup is a lookup table for Key Skeletal Bones, hands, arms, legs, etc.
		if (header.nKeyBoneLookup < BONE_MAX)
		{
			memcpy(keyBoneLookup, gamefile->getBuffer() + header.ofsKeyBoneLookup,
			       sizeof(int16) * header.nKeyBoneLookup);
		}
		else
		{
			memcpy(keyBoneLookup, gamefile->getBuffer() + header.ofsKeyBoneLookup, sizeof(int16) * BONE_MAX);
			LOG_ERROR << "KeyBone number" << header.nKeyBoneLookup << "over" << BONE_MAX;
		}
	}

	// free MPQFile
	for (auto it : data.animfiles)
	{
		if (it.second.first != nullptr)
			it.second.first->close();
	}

	const size_t size = (origVertices.size() * sizeof(float));
	vbufsize = (3 * size); // we multiple by 3 for the x, y, z positions of the vertex

	texCoords = new glm::vec2[origVertices.size()];
	auto ov_it = origVertices.begin();
	for (size_t i = 0; i < origVertices.size(); i++, ++ov_it)
		texCoords[i] = ov_it->texcoords;

	if (video.supportVBO)
	{
		// Vert buffer
		glGenBuffersARB(1, &vbuf);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, vertices, GL_STATIC_DRAW_ARB);
		delete[] vertices;
		vertices = nullptr;

		// Texture buffer
		glGenBuffersARB(1, &tbuf);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, tbuf);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, 2 * size, texCoords, GL_STATIC_DRAW_ARB);
		delete[] texCoords;
		texCoords = nullptr;

		// normals buffer
		glGenBuffersARB(1, &nbuf);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, nbuf);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, normals, GL_STATIC_DRAW_ARB);
		delete[] normals;
		normals = nullptr;

		// clean bind
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	}

	if (header.nTexAnims > 0)
	{
		texAnims.resize(header.nTexAnims);
		ModelTexAnimDef* ta = reinterpret_cast<ModelTexAnimDef*>(gamefile->getBuffer() + header.ofsTexAnims);

		for (uint i = 0; i < texAnims.size(); i++)
			texAnims[i].init(gamefile, ta[i], globalSequences);
	}

	if (header.nEvents)
	{
		ModelEventDef* edefs = reinterpret_cast<ModelEventDef*>(gamefile->getBuffer() + header.ofsEvents);
		events.resize(header.nEvents);
		for (uint i = 0; i < events.size(); i++)
			events[i].init(edefs[i]);
	}

	// particle systems
	if (header.nParticleEmitters)
	{
		M2ParticleDef* pdefs = reinterpret_cast<M2ParticleDef*>(gamefile->getBuffer() + header.ofsParticleEmitters);
		M2ParticleDef* pdef;
		particleSystems.resize(header.nParticleEmitters);
		hasParticles = true;
		showParticles = true;
		for (uint i = 0; i < particleSystems.size(); i++)
		{
			pdef = (M2ParticleDef*)&pdefs[i];
			particleSystems[i].model = this;
			particleSystems[i].init(gamefile, *pdef, globalSequences);
			int pci = particleSystems[i].particleColID;
			if (pci && (std::find(replacableParticleColorIDs.begin(),
			                      replacableParticleColorIDs.end(), pci) == replacableParticleColorIDs.end()))
				replacableParticleColorIDs.push_back(pci);
		}
	}

	// ribbons
	if (header.nRibbonEmitters)
	{
		ModelRibbonEmitterDef* rdefs = reinterpret_cast<ModelRibbonEmitterDef*>(gamefile->getBuffer() + header.ofsRibbonEmitters);
		ribbons.resize(header.nRibbonEmitters);
		for (uint i = 0; i < ribbons.size(); i++)
		{
			ribbons[i].model = this;
			ribbons[i].init(gamefile, rdefs[i], globalSequences);
		}
	}

	// Cameras
	if (header.nCameras > 0)
	{
		if (header.version[0] <= 9)
		{
			ModelCameraDef* camDefs = reinterpret_cast<ModelCameraDef*>(gamefile->getBuffer() + header.ofsCameras);
			for (size_t i = 0; i < header.nCameras; i++)
			{
				ModelCamera a;
				a.init(gamefile, camDefs[i], globalSequences, modelname);
				cam.push_back(a);
			}
		}
		else if (header.version[0] <= 16)
		{
			ModelCameraDefV10* camDefs = reinterpret_cast<ModelCameraDefV10*>(gamefile->getBuffer() + header.ofsCameras);
			for (size_t i = 0; i < header.nCameras; i++)
			{
				ModelCamera a;
				a.initv10(gamefile, camDefs[i], globalSequences, modelname);
				cam.push_back(a);
			}
		}
		if (cam.size() > 0)
		{
			hasCamera = true;
		}
	}

	// init lights
	if (header.nLights)
	{
		lights.resize(header.nLights);
		ModelLightDef* lDefs = reinterpret_cast<ModelLightDef*>(gamefile->getBuffer() + header.ofsLights);
		for (uint i = 0; i < lights.size(); i++)
			lights[i].init(gamefile, lDefs[i], globalSequences);
	}

	animcalc = false;
}

void WoWModel::setLOD(int index)
{
	GameFile* g;

	if (gamefile->isChunked())
	{
		const int numSkinFiles = sizeof(skinFileIDs);
		if (!numSkinFiles)
		{
			LOG_ERROR << "Attempt to set view level when no .skin files exist.";
			return;
		}

		if (index < 0)
		{
			index = 0;
			LOG_ERROR << "Attempt to set view level to negative number (" << index << ").";
		}
		else if (index >= numSkinFiles)
		{
			index = numSkinFiles - 1;
			LOG_ERROR << "Attempt to set view level too high (" << index << "). Setting LOD to valid max (" << index <<
				").";
		}

		const uint32 skinfile = skinFileIDs[index];
		g = GAMEDIRECTORY.getFile(skinfile);
		if (!g || !g->open())
		{
			LOG_ERROR << "Unable to load .skin file with ID" << skinfile << ".";
			return;
		}
	}
	else
	{
		const QString tmpname = QString::fromStdString(modelname).replace(".m2", "", Qt::CaseInsensitive);
		lodname = QString("%1%2.skin").arg(tmpname).arg(index, 2, 10, QChar('0')).toStdString(); // Lods: 00, 01, 02, 03

		g = GAMEDIRECTORY.getFile(lodname.c_str());
		if (!g || !g->open())
		{
			LOG_ERROR << "Unable to load .skin file:" << lodname.c_str();
			return;
		}
	}

	// Texture definitions
	const ModelTextureDef* texdef = reinterpret_cast<ModelTextureDef*>(gamefile->getBuffer() + header.ofsTextures);

	// Transparency
	const int16* transLookup = reinterpret_cast<int16*>(gamefile->getBuffer() + header.ofsTransparencyLookup);

	if (g->isEof())
	{
		LOG_ERROR << "Unable to load .skin file:" << g->fullname() << ", ID:" << g->fileDataId();
		g->close();
		return;
	}

	const ModelView* view = reinterpret_cast<ModelView*>(g->getBuffer());

	if (view->id[0] != 'S' || view->id[1] != 'K' || view->id[2] != 'I' || view->id[3] != 'N')
	{
		LOG_ERROR << "Doesn't appear to be .skin file:" << g->fullname() << ", ID:" << g->fileDataId();
		g->close();
		return;
	}

	// Indices,  Triangles
	const uint16* indexLookup = reinterpret_cast<uint16*>(g->getBuffer() + view->ofsIndex);
	const uint16* triangles = reinterpret_cast<uint16*>(g->getBuffer() + view->ofsTris);
	rawIndices.clear();
	rawIndices.resize(view->nTris);

	for (size_t i = 0; i < view->nTris; i++)
	{
		rawIndices[i] = indexLookup[triangles[i]];
	}

	indices = rawIndices;

	// render ops
	ModelGeoset* ops = reinterpret_cast<ModelGeoset*>(g->getBuffer() + view->ofsSub);
	const ModelTexUnit* Tex = reinterpret_cast<ModelTexUnit*>(g->getBuffer() + view->ofsTex);
	ModelRenderFlags* renderFlags = reinterpret_cast<ModelRenderFlags*>(gamefile->getBuffer() + header.ofsTexFlags);
	const uint16* texlookup = reinterpret_cast<uint16*>(gamefile->getBuffer() + header.ofsTexLookup);
	const uint16* texanimlookup = reinterpret_cast<uint16*>(gamefile->getBuffer() + header.ofsTexAnimLookup);
	const int16* texunitlookup = reinterpret_cast<int16*>(gamefile->getBuffer() + header.ofsTexUnitLookup);

	uint32 istart = 0;
	for (size_t i = 0; i < view->nSub; i++)
	{
		ModelGeosetHD* hdgeo = new ModelGeosetHD(ops[i]);
		hdgeo->istart = istart;
		istart += hdgeo->icount;
		hdgeo->display = (hdgeo->id == 0);
		rawGeosets.push_back(hdgeo);
	}

	restoreRawGeosets();

	rawPasses.clear();

	for (size_t j = 0; j < view->nTex; j++)
	{
		ModelRenderPass* pass = new ModelRenderPass(this, Tex[j].op);

		uint texOffset = 0;
		const uint texCount = Tex[j].op_count;
		// THIS IS A QUICK AND DIRTY WORKAROUND. If op_count > 1 then the texture unit contains multiple textures.
		// Properly we should display them all, blended, but WMV doesn't support that yet, and it ends up
		// displaying one randomly. So for now we try to guess which one is the most important by checking
		// if any are special textures (11, 12 or 13). If so, we choose the first one that fits this criterion.
		pass->specialTex = specialTextures[texlookup[Tex[j].textureid]];
		for (size_t k = 0; k < texCount; k++)
		{
			const int special = specialTextures[texlookup[Tex[j].textureid + k]];
			if (special == 11 || special == 12 || special == 13)
			{
				texOffset = k;
				pass->specialTex = special;
				if (texCount > 1)
					LOG_INFO << "setLOD: texture unit" << j << "has" << texCount << "textures. Choosing texture" << k +
						1 << ", which has special type =" << special;
				break;
			}
		}
		pass->tex = texlookup[Tex[j].textureid + texOffset];

		// TODO: figure out these flags properly -_-
		const ModelRenderFlags& rf = renderFlags[Tex[j].flagsIndex];

		pass->blendmode = rf.blend;
		//if (rf.blend == 0) // Test to disable/hide different blend types
		//  continue;

		pass->color = Tex[j].colorIndex;

		pass->opacity = transLookup[Tex[j].transid + texOffset];

		pass->unlit = (rf.flags & RENDERFLAGS_UNLIT) != 0;

		pass->cull = (rf.flags & RENDERFLAGS_TWOSIDED) == 0;

		pass->billboard = (rf.flags & RENDERFLAGS_BILLBOARD) != 0;

		// Use environmental reflection effects?
		pass->useEnvMap = (texunitlookup[Tex[j].texunit] == -1) && pass->billboard && rf.blend > 2; //&& rf.blend<5;

		// Disable environmental mapping if its been unchecked.
		if (pass->useEnvMap && !video.useEnvMapping)
			pass->useEnvMap = false;

		pass->noZWrite = (rf.flags & RENDERFLAGS_ZBUFFERED) != 0;

		// ToDo: Work out the correct way to get the true/false of transparency
		pass->trans = (pass->blendmode > 0) && (pass->opacity > 0);
		// Transparency - not the correct way to get transparency

		// Texture flags
		pass->swrap = (texdef[pass->tex].flags & TEXTURE_WRAPX) != 0; // Texture wrap X
		pass->twrap = (texdef[pass->tex].flags & TEXTURE_WRAPY) != 0; // Texture wrap Y

		// tex[j].flags: Usually 16 for static textures, and 0 for animated textures.
		if ((Tex[j].flags & TEXTUREUNIT_STATIC) == 0)
		{
			pass->texanim = texanimlookup[Tex[j].texanimid + texOffset];
		}

		rawPasses.push_back(pass);
	}
	g->close();

	std::sort(rawPasses.begin(), rawPasses.end(), &WoWModel::sortPasses);
	passes = rawPasses;
}

bool WoWModel::sortPasses(ModelRenderPass* mrp1, ModelRenderPass* mrp2)
{
	if (mrp1->geoIndex == mrp2->geoIndex)
		return mrp1->specialTex < mrp2->specialTex;
	if (mrp1->blendmode == mrp2->blendmode)
		return (mrp1->geoIndex < mrp2->geoIndex);
	return mrp1->blendmode < mrp2->blendmode;
}

void WoWModel::calcBones(ssize_t Anim, size_t time)
{
	// Reset all bones to 'false' which means they haven't been animated yet.
	for (auto& it : bones)
	{
		it.calc = false;
	}

	// Character specific bone animation calculations.
	if (charModelDetails.isChar)
	{
		// Animate the "core" rotations and transformations for the rest of the model to adopt into their transformations
		if (keyBoneLookup[BONE_ROOT] > -1)
		{
			for (int i = 0; i <= keyBoneLookup[BONE_ROOT]; i++)
			{
				bones[i].calcMatrix(bones, Anim, time);
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
		if (animLookups.size() >= ANIMATION_HANDSCLOSED && animLookups[ANIMATION_HANDSCLOSED] > 0) // closed fist
			closeFistID = animLookups[ANIMATION_HANDSCLOSED];

		// Animate key skeletal bones except the fingers which we do later.
		// -----
		size_t a, t;

		// if we have a "secondary animation" selected,  animate upper body using that.
		if (animManager->GetSecondaryID() > -1)
		{
			a = animManager->GetSecondaryID();
			t = animManager->GetSecondaryFrame();
		}
		else
		{
			a = Anim;
			t = time;
		}

		for (size_t i = 0; i < animManager->GetSecondaryCount(); i++)
		{
			// only goto 5, otherwise it affects the hip/waist rotation for the lower-body.
			if (keyBoneLookup[i] > -1)
				bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
		}

		if (animManager->GetMouthID() > -1)
		{
			// Animate the head and jaw
			if (keyBoneLookup[BONE_HEAD] > -1)
				bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, animManager->GetMouthID(),
				                                           animManager->GetMouthFrame());
			if (keyBoneLookup[BONE_JAW] > -1)
				bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, animManager->GetMouthID(),
				                                          animManager->GetMouthFrame());
		}
		else
		{
			// Animate the head and jaw
			if (keyBoneLookup[BONE_HEAD] > -1)
				bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, a, t);
			if (keyBoneLookup[BONE_JAW] > -1)
				bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, a, t);
		}

		// still not sure what 18-26 bone lookups are but I think its more for things like wrist, etc which are not as visually obvious.
		for (size_t i = BONE_BTH; i < BONE_MAX; i++)
		{
			if (keyBoneLookup[i] > -1)
				bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
		}
		// =====

		if (charModelDetails.closeRHand)
		{
			a = closeFistID;
			t = 1;
		}
		else
		{
			a = Anim;
			t = time;
		}

		for (size_t i = 0; i < 5; i++)
		{
			if (keyBoneLookup[BONE_RFINGER1 + i] > -1)
				bones[keyBoneLookup[BONE_RFINGER1 + i]].calcMatrix(bones, a, t);
		}

		if (charModelDetails.closeLHand)
		{
			a = closeFistID;
			t = 1;
		}
		else
		{
			a = Anim;
			t = time;
		}

		for (size_t i = 0; i < 5; i++)
		{
			if (keyBoneLookup[BONE_LFINGER1 + i] > -1)
				bones[keyBoneLookup[BONE_LFINGER1 + i]].calcMatrix(bones, a, t);
		}
	}
	else
	{
		for (ssize_t i = 0; i < keyBoneLookup[BONE_ROOT]; i++)
		{
			bones[i].calcMatrix(bones, Anim, time);
		}

		// The following line fixes 'mounts' in that the character doesn't get rotated, but it also screws up the rotation for the entire model :(
		//bones[18].calcMatrix(bones, anim, time, false);

		// Animate key skeletal bones except the fingers which we do later.
		// -----
		size_t a, t;

		// if we have a "secondary animation" selected,  animate upper body using that.
		if (animManager->GetSecondaryID() > -1)
		{
			a = animManager->GetSecondaryID();
			t = animManager->GetSecondaryFrame();
		}
		else
		{
			a = Anim;
			t = time;
		}

		for (size_t i = 0; i < animManager->GetSecondaryCount(); i++)
		{
			// only goto 5, otherwise it affects the hip/waist rotation for the lower-body.
			if (keyBoneLookup[i] > -1)
				bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
		}

		if (animManager->GetMouthID() > -1)
		{
			// Animate the head and jaw
			if (keyBoneLookup[BONE_HEAD] > -1)
				bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, animManager->GetMouthID(),
				                                           animManager->GetMouthFrame());
			if (keyBoneLookup[BONE_JAW] > -1)
				bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, animManager->GetMouthID(),
				                                          animManager->GetMouthFrame());
		}
		else
		{
			// Animate the head and jaw
			if (keyBoneLookup[BONE_HEAD] > -1)
				bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, a, t);
			if (keyBoneLookup[BONE_JAW] > -1)
				bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, a, t);
		}

		// still not sure what 18-26 bone lookups are but I think its more for things like wrist, etc which are not as visually obvious.
		for (size_t i = BONE_ROOT; i < BONE_MAX; i++)
		{
			if (keyBoneLookup[i] > -1)
				bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
		}
	}

	// Animate everything thats left with the 'default' animation
	for (auto& it : bones)
	{
		it.calcMatrix(bones, Anim, time);
	}
}

void WoWModel::animate(ssize_t Anim)
{
	size_t t = 0;

	const ModelAnimation& a = anims[Anim];
	int tmax = a.length;
	if (tmax == 0)
		tmax = 1;

	if (isWMO == true)
	{
		t = globalTime;
		t %= tmax;
	}
	else
		t = animManager->GetFrame();

	this->animtime = t;
	this->anim = Anim;

	if (animBones) // && (!animManager->IsPaused() || !animManager->IsParticlePaused()))
	{
		calcBones(Anim, t);
	}

	if (animGeometry)
	{
		if (video.supportVBO)
		{
			glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
			glBufferDataARB(GL_ARRAY_BUFFER_ARB, 2 * vbufsize, nullptr, GL_STREAM_DRAW_ARB);

			vertices = static_cast<glm::vec3*>(glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY));
		}

		// transform vertices
		auto ov_it = origVertices.begin();
		for (size_t i = 0; ov_it != origVertices.end(); ++i, ++ov_it)
		{
			//,k=0
			glm::vec3 v(0, 0, 0), n(0, 0, 0);

			for (size_t b = 0; b < 4; b++)
			{
				if (ov_it->weights[b] > 0)
				{
					glm::vec3 tv = glm::vec3(bones[ov_it->bones[b]].mat * glm::vec4(ov_it->pos, 1.0f));
					glm::vec3 tn = glm::vec3(bones[ov_it->bones[b]].mrot * glm::vec4(ov_it->normal, 1.0f));
					v += tv * (static_cast<float>(ov_it->weights[b]) / 255.0f);
					n += tn * (static_cast<float>(ov_it->weights[b]) / 255.0f);
				}
			}

			vertices[i] = v;
			if (video.supportVBO)
				vertices[origVertices.size() + i] = glm::normalize(n); // shouldn't these be normal by default?
			else
				normals[i] = n;
		}

		// clear bind
		if (video.supportVBO)
		{
			glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
		}
	}

	for (auto& light : lights)
	{
		if (light.parent >= 0)
		{
			light.tpos = glm::vec3(bones[light.parent].mat * glm::vec4(light.pos, 1.0f));
			light.tdir = glm::vec3(bones[light.parent].mrot * glm::vec4(light.dir, 1.0f));
		}
	}

	for (auto& it : particleSystems)
	{
		// random time distribution for teh win ..?
		//int pt = a.timeStart + (t + (int)(tmax*particleSystems[i].tofs)) % tmax;
		it.setup(Anim, t);
	}

	for (auto& it : ribbons)
		it.setup(Anim, t);

	for (auto& it : texAnims)
		it.calc(Anim, t);
}

inline void WoWModel::drawModel()
{
	glPushMatrix();

	glm::vec3 scaling = glm::vec3(scale_, scale_, scale_);
	if (mirrored_)
	{
		glFrontFace(GL_CW); // necessary when model is being mirrored or it appears inside-out
		scaling.y *= -1.0f;
	}
	else
	{
		glFrontFace(GL_CCW);
	}

	// no need to scale if its already 100%
	// scaling manually set from model control panel
	if (scaling != glm::vec3(1.0f, 1.0f, 1.0f))
		glScalef(scaling.x, scaling.y, scaling.z);

	if (pos_ != glm::vec3(0.0f, 0.0f, 0.0f))
		glTranslatef(pos_.x, pos_.y, pos_.z);


	if (rot_ != glm::vec3(0.0f, 0.0f, 0.0f))
	{
		glRotatef(rot_.x, 1.0f, 0.0f, 0.0f);
		glRotatef(rot_.y, 0.0f, 1.0f, 0.0f);
		glRotatef(rot_.z, 0.0f, 0.0f, 1.0f);
	}


	if (showModel && (alpha_ != 1.0f))
	{
		glDisable(GL_COLOR_MATERIAL);

		const float a[] = {1.0f, 1.0f, 1.0f, alpha_};
		glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, a);

		glEnable(GL_BLEND);
		//glDisable(GL_DEPTH_TEST);
		//glDepthMask(GL_FALSE);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	if (!showTexture || video.useMasking)
		glDisable(GL_TEXTURE_2D);
	else
		glEnable(GL_TEXTURE_2D);

	// assume these client states are enabled: GL_VERTEX_ARRAY, GL_NORMAL_ARRAY, GL_TEXTURE_COORD_ARRAY
	if (video.supportVBO && animated)
	{
		// bind / point to the vertex normals buffer
		if (animGeometry)
		{
			glNormalPointer(GL_FLOAT, 0, GL_BUFFER_OFFSET(vbufsize));
		}
		else
		{
			glBindBufferARB(GL_ARRAY_BUFFER_ARB, nbuf);
			glNormalPointer(GL_FLOAT, 0, nullptr);
		}

		// Bind the vertex buffer
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
		glVertexPointer(3, GL_FLOAT, 0, nullptr);
		// Bind the texture coordinates buffer
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, tbuf);
		glTexCoordPointer(2, GL_FLOAT, 0, nullptr);
	}
	else if (animated)
	{
		glVertexPointer(3, GL_FLOAT, 0, vertices);
		glNormalPointer(GL_FLOAT, 0, normals);
		glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
	}

	// Display in wireframe mode?
	if (showWireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	// Render the various parts of the model.
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for (const auto it : passes)
	{
		if (it->init())
		{
			it->render(animated);
			it->deinit();
		}
	}

	if (showWireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// clean bind
	if (video.supportVBO && animated)
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);

	if (showModel && (alpha_ != 1.0f))
	{
		const float a[] = {1.0f, 1.0f, 1.0f, 1.0f};
		glMaterialfv(GL_FRONT, GL_DIFFUSE, a);

		glDisable(GL_BLEND);
		//glEnable(GL_DEPTH_TEST);
		//glDepthMask(GL_TRUE);
		glEnable(GL_COLOR_MATERIAL);
	}

	glPopMatrix();
	// done with all render ops
}

inline void WoWModel::draw()
{
	if (!ok)
		return;

	if (!animated)
	{
		if (showModel)
			glCallList(dlist);
	}
	else
	{
		if (ind)
		{
			animate(currentAnim);
		}
		else
		{
			if (!animcalc)
			{
				animate(currentAnim);
				//animcalc = true; // Not sure what this is really for but it breaks WMO animation
			}
		}

		if (showModel)
			drawModel();
	}

	if (!video.useMasking && (showBounds || showBones))
	{
		glDisable(GL_LIGHTING);
		glDisable(GL_TEXTURE_2D);

		if (showBounds)
			drawBoundingVolume();

		if (showBones)
			drawBones();

		glEnable(GL_TEXTURE_2D);
		glEnable(GL_LIGHTING);
	}
}

// These aren't really needed in the model viewer.. only wowmapviewer
void WoWModel::lightsOn(GLuint lbase)
{
	// setup lights
	for (uint i = 0, l = lbase; i < lights.size(); i++)
		lights[i].setup(animtime, (GLuint)l++);
}

// These aren't really needed in the model viewer.. only wowmapviewer
void WoWModel::lightsOff(GLuint lbase)
{
	for (uint i = 0, l = lbase; i < lights.size(); i++)
		glDisable((GLenum)l++);
}

// Updates our particles within models.
void WoWModel::updateEmitters(float dt)
{
	if (!ok || !showParticles || !GLOBALSETTINGS.bShowParticle)
		return;

	for (auto& it : particleSystems)
	{
		it.update(dt);
		it.replaceParticleColors = replaceParticleColors;
		it.particleColorReplacements = particleColorReplacements;
	}
}

// Draws the "bones" of models  (skeletal animation)
void WoWModel::drawBones()
{
	glDisable(GL_DEPTH_TEST);
	glBegin(GL_LINES);
	for (auto it : bones)
	{
		//for (size_t i=30; i<40; i++) {
		if (it.parent != -1)
		{
			glVertex3fv(glm::value_ptr(it.transPivot));
			glVertex3fv(glm::value_ptr(bones[it.parent].transPivot));
		}
	}
	glEnd();
	glEnable(GL_DEPTH_TEST);
}

// Sets up the models attachments
void WoWModel::setupAtt(int id)
{
	const int l = attLookup[id];
	if (l > -1)
		atts[l].setup();
}

// Sets up the models attachments
void WoWModel::setupAtt2(int id)
{
	const int l = attLookup[id];
	if (l >= 0)
		atts[l].setupParticle();
}

// Draws the Bounding Volume, which is used for Collision detection.
void WoWModel::drawBoundingVolume()
{
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glBegin(GL_TRIANGLES);
	for (const unsigned int v : boundTris)
	{
		if (v < bounds.size())
			glVertex3fv(glm::value_ptr(bounds[v]));
		else
			glVertex3f(0, 0, 0);
	}
	glEnd();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Renders our particles into the pipeline.
void WoWModel::drawParticles()
{
	if (hasParticles && showParticles)
	{
		glPushMatrix();

		glm::vec3 scaling = glm::vec3(scale_, scale_, scale_);
		if (mirrored_)
		{
			glFrontFace(GL_CW); // necessary when model is being mirrored or it appears inside-out
			scaling.y *= -1.0f;
		}
		else
		{
			glFrontFace(GL_CCW);
		}

		// no need to scale if its already 100%
		// scaling manually set from model control panel
		if (scaling != glm::vec3(1.0f, 1.0f, 1.0f))
			glScalef(scaling.x, scaling.y, scaling.z);

		if (rot_ != glm::vec3(0.0f, 0.0f, 0.0f))
			glRotatef(rot_.y, 0.0f, 1.0f, 0.0f);

		//glRotatef(45.0f, 1,0,0);

		if (pos_ != glm::vec3(0.0f, 0.0f, 0.0f))
			glTranslatef(pos_.x, pos_.y, pos_.z);

		// draw particle systems
		for (auto& it : particleSystems)
			it.draw();

		// draw ribbons
		for (auto& it : ribbons)
			it.draw();

		glPopMatrix();
	}
}

WoWItem* WoWModel::getItem(CharSlots slot)
{
	for (const auto it : *this)
	{
		if (it->slot() == slot)
			return it;
	}

	return nullptr;
}

int WoWModel::getItemId(CharSlots slot)
{
	const auto* item = getItem(slot);

	if (item == nullptr)
		return 0;

	return item->id();
}

bool WoWModel::isWearingARobe()
{
	const auto* chest = getItem(CS_CHEST);
	if (chest == nullptr)
		return false;

	const auto& item = items.getById(chest->id());

	return item.type == IT_ROBE;
}

void WoWModel::update(int dt) // (float dt)
{
	if (animated && animManager != nullptr)
		animManager->Tick(dt);
	updateEmitters((dt / 1000.0f));
}

void WoWModel::updateTextureList(GameFile* Tex, int special)
{
	for (const int specialTexture : specialTextures)
	{
		if (specialTexture == special)
		{
			if (replaceTextures[special] != ModelRenderPass::INVALID_TEX)
				TEXTUREMANAGER.del(replaceTextures[special]);

			replaceTextures[special] = TEXTUREMANAGER.add(Tex);
			break;
		}
	}
}

std::map<int, std::wstring> WoWModel::getAnimsMap()
{
	std::map<int, std::wstring> result;

	static const std::map<int, std::wstring> AnimationNames =
	{
		{0, L"Stand"},
		{1, L"Death"},
		{2, L"Spell"},
		{3, L"Stop"},
		{4, L"Walk"},
		{5, L"Run"},
		{6, L"Dead"},
		{7, L"Rise"},
		{8, L"StandWound"},
		{9, L"CombatWound"},
		{10, L"CombatCritical"},
		{11, L"ShuffleLeft"},
		{12, L"ShuffleRight"},
		{13, L"Walkbackwards"},
		{14, L"Stun"},
		{15, L"HandsClosed"},
		{16, L"AttackUnarmed"},
		{17, L"Attack1H"},
		{18, L"Attack2H"},
		{19, L"Attack2HL"},
		{20, L"ParryUnarmed"},
		{21, L"Parry1H"},
		{22, L"Parry2H"},
		{23, L"Parry2HL"},
		{24, L"ShieldBlock"},
		{25, L"ReadyUnarmed"},
		{26, L"Ready1H"},
		{27, L"Ready2H"},
		{28, L"Ready2HL"},
		{29, L"ReadyBow"},
		{30, L"Dodge"},
		{31, L"SpellPrecast"},
		{32, L"SpellCast"},
		{33, L"SpellCastArea"},
		{34, L"NPCWelcome"},
		{35, L"NPCGoodbye"},
		{36, L"Block"},
		{37, L"JumpStart"},
		{38, L"Jump"},
		{39, L"JumpEnd"},
		{40, L"Fall"},
		{41, L"SwimIdle"},
		{42, L"Swim"},
		{43, L"SwimLeft"},
		{44, L"SwimRight"},
		{45, L"SwimBackwards"},
		{46, L"AttackBow"},
		{47, L"FireBow"},
		{48, L"ReadyRifle"},
		{49, L"AttackRifle"},
		{50, L"Loot"},
		{51, L"ReadySpellDirected"},
		{52, L"ReadySpellOmni"},
		{53, L"SpellCastDirected"},
		{54, L"SpellCastOmni"},
		{55, L"BattleRoar"},
		{56, L"ReadyAbility"},
		{57, L"Special1H"},
		{58, L"Special2H"},
		{59, L"ShieldBash"},
		{60, L"EmoteTalk"},
		{61, L"EmoteEat"},
		{62, L"EmoteWork"},
		{63, L"EmoteUseStanding"},
		{64, L"EmoteTalkExclamation"},
		{65, L"EmoteTalkQuestion"},
		{66, L"EmoteBow"},
		{67, L"EmoteWave"},
		{68, L"EmoteCheer"},
		{69, L"EmoteDance"},
		{70, L"EmoteLaugh"},
		{71, L"EmoteSleep"},
		{72, L"EmoteSitGround"},
		{73, L"EmoteRude"},
		{74, L"EmoteRoar"},
		{75, L"EmoteKneel"},
		{76, L"EmoteKiss"},
		{77, L"EmoteCry"},
		{78, L"EmoteChicken"},
		{79, L"EmoteBeg"},
		{80, L"EmoteApplaud"},
		{81, L"EmoteShout"},
		{82, L"EmoteFlex"},
		{83, L"EmoteShy"},
		{84, L"EmotePoint"},
		{85, L"Attack1HPierce"},
		{86, L"Attack2HLoosePierce"},
		{87, L"AttackOff"},
		{88, L"AttackOffPierce"},
		{89, L"Sheath"},
		{90, L"HipSheath"},
		{91, L"Mount"},
		{92, L"RunRight"},
		{93, L"RunLeft"},
		{94, L"MountSpecial"},
		{95, L"Kick"},
		{96, L"SitGroundDown"},
		{97, L"SitGround"},
		{98, L"SitGroundUp"},
		{99, L"SleepDown"},
		{100, L"Sleep"},
		{101, L"SleepUp"},
		{102, L"SitChairLow"},
		{103, L"SitChairMed"},
		{104, L"SitChairHigh"},
		{105, L"LoadBow"},
		{106, L"LoadRifle"},
		{107, L"AttackThrown"},
		{108, L"ReadyThrown"},
		{109, L"HoldBow"},
		{110, L"HoldRifle"},
		{111, L"HoldThrown"},
		{112, L"LoadThrown"},
		{113, L"EmoteSalute"},
		{114, L"KneelStart"},
		{115, L"KneelLoop"},
		{116, L"KneelEnd"},
		{117, L"AttackUnarmedOff"},
		{118, L"SpecialUnarmed"},
		{119, L"StealthWalk"},
		{120, L"StealthStand"},
		{121, L"Knockdown"},
		{122, L"EatingLoop"},
		{123, L"UseStandingLoop"},
		{124, L"ChannelCastDirected"},
		{125, L"ChannelCastOmni"},
		{126, L"Whirlwind"},
		{127, L"Birth"},
		{128, L"UseStandingStart"},
		{129, L"UseStandingEnd"},
		{130, L"CreatureSpecial"},
		{131, L"Drown"},
		{132, L"Drowned"},
		{133, L"FishingCast"},
		{134, L"FishingLoop"},
		{135, L"Fly"},
		{136, L"EmoteWorkNoSheathe"},
		{137, L"EmoteStunNoSheathe"},
		{138, L"EmoteUseStandingNoSheathe"},
		{139, L"SpellSleepDown"},
		{140, L"SpellKneelStart"},
		{141, L"SpellKneelLoop"},
		{142, L"SpellKneelEnd"},
		{143, L"Sprint"},
		{144, L"InFlight"},
		{145, L"Spawn"},
		{146, L"Close"},
		{147, L"Closed"},
		{148, L"Open"},
		{149, L"Opened"},
		{150, L"Destroy"},
		{151, L"Destroyed"},
		{152, L"Rebuild"},
		{153, L"Custom0"},
		{154, L"Custom1"},
		{155, L"Custom2"},
		{156, L"Custom3"},
		{157, L"Despawn"},
		{158, L"Hold"},
		{159, L"Decay"},
		{160, L"BowPull"},
		{161, L"BowRelease"},
		{162, L"ShipStart"},
		{163, L"ShipMoving"},
		{164, L"ShipStop"},
		{165, L"GroupArrow"},
		{166, L"Arrow"},
		{167, L"CorpseArrow"},
		{168, L"GuideArrow"},
		{169, L"Sway"},
		{170, L"DruidCatPounce"},
		{171, L"DruidCatRip"},
		{172, L"DruidCatRake"},
		{173, L"DruidCatRavage"},
		{174, L"DruidCatClaw"},
		{175, L"DruidCatCower"},
		{176, L"DruidBearSwipe"},
		{177, L"DruidBearBite"},
		{178, L"DruidBearMaul"},
		{179, L"DruidBearBash"},
		{180, L"DragonTail"},
		{181, L"DragonStomp"},
		{182, L"DragonSpit"},
		{183, L"DragonSpitHover"},
		{184, L"DragonSpitFly"},
		{185, L"EmoteYes"},
		{186, L"EmoteNo"},
		{187, L"JumpLandRun"},
		{188, L"LootHold"},
		{189, L"LootUp"},
		{190, L"StandHigh"},
		{191, L"Impact"},
		{192, L"LiftOff"},
		{193, L"Hover"},
		{194, L"SuccubusEntice"},
		{195, L"EmoteTrain"},
		{196, L"EmoteDead"},
		{197, L"EmoteDanceOnce"},
		{198, L"Deflect"},
		{199, L"EmoteEatNoSheathe"},
		{200, L"Land"},
		{201, L"Submerge"},
		{202, L"Submerged"},
		{203, L"Cannibalize"},
		{204, L"ArrowBirth"},
		{205, L"GroupArrowBirth"},
		{206, L"CorpseArrowBirth"},
		{207, L"GuideArrowBirth"},
		{208, L"EmoteTalkNoSheathe"},
		{209, L"EmotePointNoSheathe"},
		{210, L"EmoteSaluteNoSheathe"},
		{211, L"EmoteDanceSpecial"},
		{212, L"Mutilate"},
		{213, L"CustomSpell01"},
		{214, L"CustomSpell02"},
		{215, L"CustomSpell03"},
		{216, L"CustomSpell04"},
		{217, L"CustomSpell05"},
		{218, L"CustomSpell06"},
		{219, L"CustomSpell07"},
		{220, L"CustomSpell08"},
		{221, L"CustomSpell09"},
		{222, L"CustomSpell10"},
		{223, L"StealthRun"},
		{224, L"Emerge"},
		{225, L"Cower"},
		{226, L"Grab"},
		{227, L"GrabClosed"},
		{228, L"GrabThrown"},
		{229, L"FlyStand"},
		{230, L"FlyDeath"},
		{231, L"FlySpell"},
		{232, L"FlyStop"},
		{233, L"FlyWalk"},
		{234, L"FlyRun"},
		{235, L"FlyDead"},
		{236, L"FlyRise"},
		{237, L"FlyStandWound"},
		{238, L"FlyCombatWound"},
		{239, L"FlyCombatCritical"},
		{240, L"FlyShuffleLeft"},
		{241, L"FlyShuffleRight"},
		{242, L"FlyWalkbackwards"},
		{243, L"FlyStun"},
		{244, L"FlyHandsClosed"},
		{245, L"FlyAttackUnarmed"},
		{246, L"FlyAttack1H"},
		{247, L"FlyAttack2H"},
		{248, L"FlyAttack2HL"},
		{249, L"FlyParryUnarmed"},
		{250, L"FlyParry1H"},
		{251, L"FlyParry2H"},
		{252, L"FlyParry2HL"},
		{253, L"FlyShieldBlock"},
		{254, L"FlyReadyUnarmed"},
		{255, L"FlyReady1H"},
		{256, L"FlyReady2H"},
		{257, L"FlyReady2HL"},
		{258, L"FlyReadyBow"},
		{259, L"FlyDodge"},
		{260, L"FlySpellPrecast"},
		{261, L"FlySpellCast"},
		{262, L"FlySpellCastArea"},
		{263, L"FlyNPCWelcome"},
		{264, L"FlyNPCGoodbye"},
		{265, L"FlyBlock"},
		{266, L"FlyJumpStart"},
		{267, L"FlyJump"},
		{268, L"FlyJumpEnd"},
		{269, L"FlyFall"},
		{270, L"FlySwimIdle"},
		{271, L"FlySwim"},
		{272, L"FlySwimLeft"},
		{273, L"FlySwimRight"},
		{274, L"FlySwimBackwards"},
		{275, L"FlyAttackBow"},
		{276, L"FlyFireBow"},
		{277, L"FlyReadyRifle"},
		{278, L"FlyAttackRifle"},
		{279, L"FlyLoot"},
		{280, L"FlyReadySpellDirected"},
		{281, L"FlyReadySpellOmni"},
		{282, L"FlySpellCastDirected"},
		{283, L"FlySpellCastOmni"},
		{284, L"FlyBattleRoar"},
		{285, L"FlyReadyAbility"},
		{286, L"FlySpecial1H"},
		{287, L"FlySpecial2H"},
		{288, L"FlyShieldBash"},
		{289, L"FlyEmoteTalk"},
		{290, L"FlyEmoteEat"},
		{291, L"FlyEmoteWork"},
		{292, L"FlyEmoteUseStanding"},
		{293, L"FlyEmoteTalkExclamation"},
		{294, L"FlyEmoteTalkQuestion"},
		{295, L"FlyEmoteBow"},
		{296, L"FlyEmoteWave"},
		{297, L"FlyEmoteCheer"},
		{298, L"FlyEmoteDance"},
		{299, L"FlyEmoteLaugh"},
		{300, L"FlyEmoteSleep"},
		{301, L"FlyEmoteSitGround"},
		{302, L"FlyEmoteRude"},
		{303, L"FlyEmoteRoar"},
		{304, L"FlyEmoteKneel"},
		{305, L"FlyEmoteKiss"},
		{306, L"FlyEmoteCry"},
		{307, L"FlyEmoteChicken"},
		{308, L"FlyEmoteBeg"},
		{309, L"FlyEmoteApplaud"},
		{310, L"FlyEmoteShout"},
		{311, L"FlyEmoteFlex"},
		{312, L"FlyEmoteShy"},
		{313, L"FlyEmotePoint"},
		{314, L"FlyAttack1HPierce"},
		{315, L"FlyAttack2HLoosePierce"},
		{316, L"FlyAttackOff"},
		{317, L"FlyAttackOffPierce"},
		{318, L"FlySheath"},
		{319, L"FlyHipSheath"},
		{320, L"FlyMount"},
		{321, L"FlyRunRight"},
		{322, L"FlyRunLeft"},
		{323, L"FlyMountSpecial"},
		{324, L"FlyKick"},
		{325, L"FlySitGroundDown"},
		{326, L"FlySitGround"},
		{327, L"FlySitGroundUp"},
		{328, L"FlySleepDown"},
		{329, L"FlySleep"},
		{330, L"FlySleepUp"},
		{331, L"FlySitChairLow"},
		{332, L"FlySitChairMed"},
		{333, L"FlySitChairHigh"},
		{334, L"FlyLoadBow"},
		{335, L"FlyLoadRifle"},
		{336, L"FlyAttackThrown"},
		{337, L"FlyReadyThrown"},
		{338, L"FlyHoldBow"},
		{339, L"FlyHoldRifle"},
		{340, L"FlyHoldThrown"},
		{341, L"FlyLoadThrown"},
		{342, L"FlyEmoteSalute"},
		{343, L"FlyKneelStart"},
		{344, L"FlyKneelLoop"},
		{345, L"FlyKneelEnd"},
		{346, L"FlyAttackUnarmedOff"},
		{347, L"FlySpecialUnarmed"},
		{348, L"FlyStealthWalk"},
		{349, L"FlyStealthStand"},
		{350, L"FlyKnockdown"},
		{351, L"FlyEatingLoop"},
		{352, L"FlyUseStandingLoop"},
		{353, L"FlyChannelCastDirected"},
		{354, L"FlyChannelCastOmni"},
		{355, L"FlyWhirlwind"},
		{356, L"FlyBirth"},
		{357, L"FlyUseStandingStart"},
		{358, L"FlyUseStandingEnd"},
		{359, L"FlyCreatureSpecial"},
		{360, L"FlyDrown"},
		{361, L"FlyDrowned"},
		{362, L"FlyFishingCast"},
		{363, L"FlyFishingLoop"},
		{364, L"FlyFly"},
		{365, L"FlyEmoteWorkNoSheathe"},
		{366, L"FlyEmoteStunNoSheathe"},
		{367, L"FlyEmoteUseStandingNoSheathe"},
		{368, L"FlySpellSleepDown"},
		{369, L"FlySpellKneelStart"},
		{370, L"FlySpellKneelLoop"},
		{371, L"FlySpellKneelEnd"},
		{372, L"FlySprint"},
		{373, L"FlyInFlight"},
		{374, L"FlySpawn"},
		{375, L"FlyClose"},
		{376, L"FlyClosed"},
		{377, L"FlyOpen"},
		{378, L"FlyOpened"},
		{379, L"FlyDestroy"},
		{380, L"FlyDestroyed"},
		{381, L"FlyRebuild"},
		{382, L"FlyCustom0"},
		{383, L"FlyCustom1"},
		{384, L"FlyCustom2"},
		{385, L"FlyCustom3"},
		{386, L"FlyDespawn"},
		{387, L"FlyHold"},
		{388, L"FlyDecay"},
		{389, L"FlyBowPull"},
		{390, L"FlyBowRelease"},
		{391, L"FlyShipStart"},
		{392, L"FlyShipMoving"},
		{393, L"FlyShipStop"},
		{394, L"FlyGroupArrow"},
		{395, L"FlyArrow"},
		{396, L"FlyCorpseArrow"},
		{397, L"FlyGuideArrow"},
		{398, L"FlySway"},
		{399, L"FlyDruidCatPounce"},
		{400, L"FlyDruidCatRip"},
		{401, L"FlyDruidCatRake"},
		{402, L"FlyDruidCatRavage"},
		{403, L"FlyDruidCatClaw"},
		{404, L"FlyDruidCatCower"},
		{405, L"FlyDruidBearSwipe"},
		{406, L"FlyDruidBearBite"},
		{407, L"FlyDruidBearMaul"},
		{408, L"FlyDruidBearBash"},
		{409, L"FlyDragonTail"},
		{410, L"FlyDragonStomp"},
		{411, L"FlyDragonSpit"},
		{412, L"FlyDragonSpitHover"},
		{413, L"FlyDragonSpitFly"},
		{414, L"FlyEmoteYes"},
		{415, L"FlyEmoteNo"},
		{416, L"FlyJumpLandRun"},
		{417, L"FlyLootHold"},
		{418, L"FlyLootUp"},
		{419, L"FlyStandHigh"},
		{420, L"FlyImpact"},
		{421, L"FlyLiftOff"},
		{422, L"FlyHover"},
		{423, L"FlySuccubusEntice"},
		{424, L"FlyEmoteTrain"},
		{425, L"FlyEmoteDead"},
		{426, L"FlyEmoteDanceOnce"},
		{427, L"FlyDeflect"},
		{428, L"FlyEmoteEatNoSheathe"},
		{429, L"FlyLand"},
		{430, L"FlySubmerge"},
		{431, L"FlySubmerged"},
		{432, L"FlyCannibalize"},
		{433, L"FlyArrowBirth"},
		{434, L"FlyGroupArrowBirth"},
		{435, L"FlyCorpseArrowBirth"},
		{436, L"FlyGuideArrowBirth"},
		{437, L"FlyEmoteTalkNoSheathe"},
		{438, L"FlyEmotePointNoSheathe"},
		{439, L"FlyEmoteSaluteNoSheathe"},
		{440, L"FlyEmoteDanceSpecial"},
		{441, L"FlyMutilate"},
		{442, L"FlyCustomSpell01"},
		{443, L"FlyCustomSpell02"},
		{444, L"FlyCustomSpell03"},
		{445, L"FlyCustomSpell04"},
		{446, L"FlyCustomSpell05"},
		{447, L"FlyCustomSpell06"},
		{448, L"FlyCustomSpell07"},
		{449, L"FlyCustomSpell08"},
		{450, L"FlyCustomSpell09"},
		{451, L"FlyCustomSpell10"},
		{452, L"FlyStealthRun"},
		{453, L"FlyEmerge"},
		{454, L"FlyCower"},
		{455, L"FlyGrab"},
		{456, L"FlyGrabClosed"},
		{457, L"FlyGrabThrown"},
		{458, L"ToFly"},
		{459, L"ToHover"},
		{460, L"ToGround"},
		{461, L"FlyToFly"},
		{462, L"FlyToHover"},
		{463, L"FlyToGround"},
		{464, L"Settle"},
		{465, L"FlySettle"},
		{466, L"DeathStart"},
		{467, L"DeathLoop"},
		{468, L"DeathEnd"},
		{469, L"FlyDeathStart"},
		{470, L"FlyDeathLoop"},
		{471, L"FlyDeathEnd"},
		{472, L"DeathEndHold"},
		{473, L"FlyDeathEndHold"},
		{474, L"Strangulate"},
		{475, L"FlyStrangulate"},
		{476, L"ReadyJoust"},
		{477, L"LoadJoust"},
		{478, L"HoldJoust"},
		{479, L"FlyReadyJoust"},
		{480, L"FlyLoadJoust"},
		{481, L"FlyHoldJoust"},
		{482, L"AttackJoust"},
		{483, L"FlyAttackJoust"},
		{484, L"ReclinedMount"},
		{485, L"FlyReclinedMount"},
		{486, L"ToAltered"},
		{487, L"FromAltered"},
		{488, L"FlyToAltered"},
		{489, L"FlyFromAltered"},
		{490, L"InStocks"},
		{491, L"FlyInStocks"},
		{492, L"VehicleGrab"},
		{493, L"VehicleThrow"},
		{494, L"FlyVehicleGrab"},
		{495, L"FlyVehicleThrow"},
		{496, L"ToAlteredPostSwap"},
		{497, L"FromAlteredPostSwap"},
		{498, L"FlyToAlteredPostSwap"},
		{499, L"FlyFromAlteredPostSwap"},
		{500, L"ReclinedMountPassenger"},
		{501, L"FlyReclinedMountPassenger"},
		{502, L"Carry2H"},
		{503, L"Carried2H"},
		{504, L"FlyCarry2H"},
		{505, L"FlyCarried2H"},
		{506, L"EmoteSniff"},
		{507, L"EmoteFlySniff"},
		{508, L"AttackFist1H"},
		{509, L"FlyAttackFist1H"},
		{510, L"AttackFist1HOff"},
		{511, L"FlyAttackFist1HOff"},
		{512, L"ParryFist1H"},
		{513, L"FlyParryFist1H"},
		{514, L"ReadyFist1H"},
		{515, L"FlyReadyFist1H"},
		{516, L"SpecialFist1H"},
		{517, L"FlySpecialFist1H"},
		{518, L"EmoteReadStart"},
		{519, L"FlyEmoteReadStart"},
		{520, L"EmoteReadLoop"},
		{521, L"FlyEmoteReadLoop"},
		{522, L"EmoteReadEnd"},
		{523, L"FlyEmoteReadEnd"},
		{524, L"SwimRun"},
		{525, L"FlySwimRun"},
		{526, L"SwimWalk"},
		{527, L"FlySwimWalk"},
		{528, L"SwimWalkBackwards"},
		{529, L"FlySwimWalkBackwards"},
		{530, L"SwimSprint"},
		{531, L"FlySwimSprint"},
		{532, L"MountSwimIdle"},
		{533, L"FlyMountSwimIdle"},
		{534, L"MountSwimBackwards"},
		{535, L"FlyMountSwimBackwards"},
		{536, L"MountSwimLeft"},
		{537, L"FlyMountSwimLeft"},
		{538, L"MountSwimRight"},
		{539, L"FlyMountSwimRight"},
		{540, L"MountSwimRun"},
		{541, L"FlyMountSwimRun"},
		{542, L"MountSwimSprint"},
		{543, L"FlyMountSwimSprint"},
		{544, L"MountSwimWalk"},
		{545, L"FlyMountSwimWalk"},
		{546, L"MountSwimWalkBackwards"},
		{547, L"FlyMountSwimWalkBackwards"},
		{548, L"MountFlightIdle"},
		{549, L"FlyMountFlightIdle"},
		{550, L"MountFlightBackwards"},
		{551, L"FlyMountFlightBackwards"},
		{552, L"MountFlightLeft"},
		{553, L"FlyMountFlightLeft"},
		{554, L"MountFlightRight"},
		{555, L"FlyMountFlightRight"},
		{556, L"MountFlightRun"},
		{557, L"FlyMountFlightRun"},
		{558, L"MountFlightSprint"},
		{559, L"FlyMountFlightSprint"},
		{560, L"MountFlightWalk"},
		{561, L"FlyMountFlightWalk"},
		{562, L"MountFlightWalkBackwards"},
		{563, L"FlyMountFlightWalkBackwards"},
		{564, L"MountFlightStart"},
		{565, L"FlyMountFlightStart"},
		{566, L"MountSwimStart"},
		{567, L"FlyMountSwimStart"},
		{568, L"MountSwimLand"},
		{569, L"FlyMountSwimLand"},
		{570, L"MountSwimLandRun"},
		{571, L"FlyMountSwimLandRun"},
		{572, L"MountFlightLand"},
		{573, L"FlyMountFlightLand"},
		{574, L"MountFlightLandRun"},
		{575, L"FlyMountFlightLandRun"},
		{576, L"ReadyBlowDart"},
		{577, L"FlyReadyBlowDart"},
		{578, L"LoadBlowDart"},
		{579, L"FlyLoadBlowDart"},
		{580, L"HoldBlowDart"},
		{581, L"FlyHoldBlowDart"},
		{582, L"AttackBlowDart"},
		{583, L"FlyAttackBlowDart"},
		{584, L"CarriageMount"},
		{585, L"FlyCarriageMount"},
		{586, L"CarriagePassengerMount"},
		{587, L"FlyCarriagePassengerMount"},
		{588, L"CarriageMountAttack"},
		{589, L"FlyCarriageMountAttack"},
		{590, L"BarTendStand"},
		{591, L"FlyBarTendStand"},
		{592, L"BarServerWalk"},
		{593, L"FlyBarServerWalk"},
		{594, L"BarServerRun"},
		{595, L"FlyBarServerRun"},
		{596, L"BarServerShuffleLeft"},
		{597, L"FlyBarServerShuffleLeft"},
		{598, L"BarServerShuffleRight"},
		{599, L"FlyBarServerShuffleRight"},
		{600, L"BarTendEmoteTalk"},
		{601, L"FlyBarTendEmoteTalk"},
		{602, L"BarTendEmotePoint"},
		{603, L"FlyBarTendEmotePoint"},
		{604, L"BarServerStand"},
		{605, L"FlyBarServerStand"},
		{606, L"BarSweepWalk"},
		{607, L"FlyBarSweepWalk"},
		{608, L"BarSweepRun"},
		{609, L"FlyBarSweepRun"},
		{610, L"BarSweepShuffleLeft"},
		{611, L"FlyBarSweepShuffleLeft"},
		{612, L"BarSweepShuffleRight"},
		{613, L"FlyBarSweepShuffleRight"},
		{614, L"BarSweepEmoteTalk"},
		{615, L"FlyBarSweepEmoteTalk"},
		{616, L"BarPatronSitEmotePoint"},
		{617, L"FlyBarPatronSitEmotePoint"},
		{618, L"MountSelfIdle"},
		{619, L"FlyMountSelfIdle"},
		{620, L"MountSelfWalk"},
		{621, L"FlyMountSelfWalk"},
		{622, L"MountSelfRun"},
		{623, L"FlyMountSelfRun"},
		{624, L"MountSelfSprint"},
		{625, L"FlyMountSelfSprint"},
		{626, L"MountSelfRunLeft"},
		{627, L"FlyMountSelfRunLeft"},
		{628, L"MountSelfRunRight"},
		{629, L"FlyMountSelfRunRight"},
		{630, L"MountSelfShuffleLeft"},
		{631, L"FlyMountSelfShuffleLeft"},
		{632, L"MountSelfShuffleRight"},
		{633, L"FlyMountSelfShuffleRight"},
		{634, L"MountSelfWalkBackwards"},
		{635, L"FlyMountSelfWalkBackwards"},
		{636, L"MountSelfSpecial"},
		{637, L"FlyMountSelfSpecial"},
		{638, L"MountSelfJump"},
		{639, L"FlyMountSelfJump"},
		{640, L"MountSelfJumpStart"},
		{641, L"FlyMountSelfJumpStart"},
		{642, L"MountSelfJumpEnd"},
		{643, L"FlyMountSelfJumpEnd"},
		{644, L"MountSelfJumpLandRun"},
		{645, L"FlyMountSelfJumpLandRun"},
		{646, L"MountSelfStart"},
		{647, L"FlyMountSelfStart"},
		{648, L"MountSelfFall"},
		{649, L"FlyMountSelfFall"},
		{650, L"Stormstrike"},
		{651, L"FlyStormstrike"},
		{652, L"ReadyJoustNoSheathe"},
		{653, L"FlyReadyJoustNoSheathe"},
		{654, L"Slam"},
		{655, L"FlySlam"},
		{656, L"DeathStrike"},
		{657, L"FlyDeathStrike"},
		{658, L"SwimAttackUnarmed"},
		{659, L"FlySwimAttackUnarmed"},
		{660, L"SpinningKick"},
		{661, L"FlySpinningKick"},
		{662, L"RoundHouseKick"},
		{663, L"FlyRoundHouseKick"},
		{664, L"RollStart"},
		{665, L"FlyRollStart"},
		{666, L"Roll"},
		{667, L"FlyRoll"},
		{668, L"RollEnd"},
		{669, L"FlyRollEnd"},
		{670, L"PalmStrike"},
		{671, L"FlyPalmStrike"},
		{672, L"MonkOffenseAttackUnarmed"},
		{673, L"FlyMonkOffenseAttackUnarmed"},
		{674, L"MonkOffenseAttackUnarmedOff"},
		{675, L"FlyMonkOffenseAttackUnarmedOff"},
		{676, L"MonkOffenseParryUnarmed"},
		{677, L"FlyMonkOffenseParryUnarmed"},
		{678, L"MonkOffenseReadyUnarmed"},
		{679, L"FlyMonkOffenseReadyUnarmed"},
		{680, L"MonkOffenseSpecialUnarmed"},
		{681, L"FlyMonkOffenseSpecialUnarmed"},
		{682, L"MonkDefenseAttackUnarmed"},
		{683, L"FlyMonkDefenseAttackUnarmed"},
		{684, L"MonkDefenseAttackUnarmedOff"},
		{685, L"FlyMonkDefenseAttackUnarmedOff"},
		{686, L"MonkDefenseParryUnarmed"},
		{687, L"FlyMonkDefenseParryUnarmed"},
		{688, L"MonkDefenseReadyUnarmed"},
		{689, L"FlyMonkDefenseReadyUnarmed"},
		{690, L"MonkDefenseSpecialUnarmed"},
		{691, L"FlyMonkDefenseSpecialUnarmed"},
		{692, L"MonkHealAttackUnarmed"},
		{693, L"FlyMonkHealAttackUnarmed"},
		{694, L"MonkHealAttackUnarmedOff"},
		{695, L"FlyMonkHealAttackUnarmedOff"},
		{696, L"MonkHealParryUnarmed"},
		{697, L"FlyMonkHealParryUnarmed"},
		{698, L"MonkHealReadyUnarmed"},
		{699, L"FlyMonkHealReadyUnarmed"},
		{700, L"MonkHealSpecialUnarmed"},
		{701, L"FlyMonkHealSpecialUnarmed"},
		{702, L"FlyingKick"},
		{703, L"FlyFlyingKick"},
		{704, L"FlyingKickStart"},
		{705, L"FlyFlyingKickStart"},
		{706, L"FlyingKickEnd"},
		{707, L"FlyFlyingKickEnd"},
		{708, L"CraneStart"},
		{709, L"FlyCraneStart"},
		{710, L"CraneLoop"},
		{711, L"FlyCraneLoop"},
		{712, L"CraneEnd"},
		{713, L"FlyCraneEnd"},
		{714, L"Despawned"},
		{715, L"FlyDespawned"},
		{716, L"ThousandFists"},
		{717, L"FlyThousandFists"},
		{718, L"MonkHealReadySpellDirected"},
		{719, L"FlyMonkHealReadySpellDirected"},
		{720, L"MonkHealReadySpellOmni"},
		{721, L"FlyMonkHealReadySpellOmni"},
		{722, L"MonkHealSpellCastDirected"},
		{723, L"FlyMonkHealSpellCastDirected"},
		{724, L"MonkHealSpellCastOmni"},
		{725, L"FlyMonkHealSpellCastOmni"},
		{726, L"MonkHealChannelCastDirected"},
		{727, L"FlyMonkHealChannelCastDirected"},
		{728, L"MonkHealChannelCastOmni"},
		{729, L"FlyMonkHealChannelCastOmni"},
		{730, L"Torpedo"},
		{731, L"FlyTorpedo"},
		{732, L"Meditate"},
		{733, L"FlyMeditate"},
		{734, L"BreathOfFire"},
		{735, L"FlyBreathOfFire"},
		{736, L"RisingSunKick"},
		{737, L"FlyRisingSunKick"},
		{738, L"GroundKick"},
		{739, L"FlyGroundKick"},
		{740, L"KickBack"},
		{741, L"FlyKickBack"},
		{742, L"PetBattleStand"},
		{743, L"FlyPetBattleStand"},
		{744, L"PetBattleDeath"},
		{745, L"FlyPetBattleDeath"},
		{746, L"PetBattleRun"},
		{747, L"FlyPetBattleRun"},
		{748, L"PetBattleWound"},
		{749, L"FlyPetBattleWound"},
		{750, L"PetBattleAttack"},
		{751, L"FlyPetBattleAttack"},
		{752, L"PetBattleReadySpell"},
		{753, L"FlyPetBattleReadySpell"},
		{754, L"PetBattleSpellCast"},
		{755, L"FlyPetBattleSpellCast"},
		{756, L"PetBattleCustom0"},
		{757, L"FlyPetBattleCustom0"},
		{758, L"PetBattleCustom1"},
		{759, L"FlyPetBattleCustom1"},
		{760, L"PetBattleCustom2"},
		{761, L"FlyPetBattleCustom2"},
		{762, L"PetBattleCustom3"},
		{763, L"FlyPetBattleCustom3"},
		{764, L"PetBattleVictory"},
		{765, L"FlyPetBattleVictory"},
		{766, L"PetBattleLoss"},
		{767, L"FlyPetBattleLoss"},
		{768, L"PetBattleStun"},
		{769, L"FlyPetBattleStun"},
		{770, L"PetBattleDead"},
		{771, L"FlyPetBattleDead"},
		{772, L"PetBattleFreeze"},
		{773, L"FlyPetBattleFreeze"},
		{774, L"MonkOffenseAttackWeapon"},
		{775, L"FlyMonkOffenseAttackWeapon"},
		{776, L"BarTendEmoteWave"},
		{777, L"FlyBarTendEmoteWave"},
		{778, L"BarServerEmoteTalk"},
		{779, L"FlyBarServerEmoteTalk"},
		{780, L"BarServerEmoteWave"},
		{781, L"FlyBarServerEmoteWave"},
		{782, L"BarServerPourDrinks"},
		{783, L"FlyBarServerPourDrinks"},
		{784, L"BarServerPickup"},
		{785, L"FlyBarServerPickup"},
		{786, L"BarServerPutDown"},
		{787, L"FlyBarServerPutDown"},
		{788, L"BarSweepStand"},
		{789, L"FlyBarSweepStand"},
		{790, L"BarPatronSit"},
		{791, L"FlyBarPatronSit"},
		{792, L"BarPatronSitEmoteTalk"},
		{793, L"FlyBarPatronSitEmoteTalk"},
		{794, L"BarPatronStand"},
		{795, L"FlyBarPatronStand"},
		{796, L"BarPatronStandEmoteTalk"},
		{797, L"FlyBarPatronStandEmoteTalk"},
		{798, L"BarPatronStandEmotePoint"},
		{799, L"FlyBarPatronStandEmotePoint"},
		{800, L"CarrionSwarm"},
		{801, L"FlyCarrionSwarm"},
		{802, L"WheelLoop"},
		{803, L"FlyWheelLoop"},
		{804, L"StandCharacterCreate"},
		{805, L"FlyStandCharacterCreate"},
		{806, L"MountChopper"},
		{807, L"FlyMountChopper"},
		{808, L"FacePose"},
		{809, L"FlyFacePose"},
		{810, L"CombatAbility2HBig01"},
		{811, L"FlyCombatAbility2HBig01"},
		{812, L"CombatAbility2H01"},
		{813, L"FlyCombatAbility2H01"},
		{814, L"CombatWhirlwind"},
		{815, L"FlyCombatWhirlwind"},
		{816, L"CombatChargeLoop"},
		{817, L"FlyCombatChargeLoop"},
		{818, L"CombatAbility1H01"},
		{819, L"FlyCombatAbility1H01"},
		{820, L"CombatChargeEnd"},
		{821, L"FlyCombatChargeEnd"},
		{822, L"CombatAbility1H02"},
		{823, L"FlyCombatAbility1H02"},
		{824, L"CombatAbility1HBig01"},
		{825, L"FlyCombatAbility1HBig01"},
		{826, L"CombatAbility2H02"},
		{827, L"FlyCombatAbility2H02"},
		{828, L"ShaSpellPrecastBoth"},
		{829, L"FlyShaSpellPrecastBoth"},
		{830, L"ShaSpellCastBothFront"},
		{831, L"FlyShaSpellCastBothFront"},
		{832, L"ShaSpellCastLeftFront"},
		{833, L"FlyShaSpellCastLeftFront"},
		{834, L"ShaSpellCastRightFront"},
		{835, L"FlyShaSpellCastRightFront"},
		{836, L"ReadyCrossbow"},
		{837, L"FlyReadyCrossbow"},
		{838, L"LoadCrossbow"},
		{839, L"FlyLoadCrossbow"},
		{840, L"AttackCrossbow"},
		{841, L"FlyAttackCrossbow"},
		{842, L"HoldCrossbow"},
		{843, L"FlyHoldCrossbow"},
		{844, L"CombatAbility2HL01"},
		{845, L"FlyCombatAbility2HL01"},
		{846, L"CombatAbility2HL02"},
		{847, L"FlyCombatAbility2HL02"},
		{848, L"CombatAbility2HLBig01"},
		{849, L"FlyCombatAbility2HLBig01"},
		{850, L"CombatUnarmed01"},
		{851, L"FlyCombatUnarmed01"},
		{852, L"CombatStompLeft"},
		{853, L"FlyCombatStompLeft"},
		{854, L"CombatStompRight"},
		{855, L"FlyCombatStompRight"},
		{856, L"CombatLeapLoop"},
		{857, L"FlyCombatLeapLoop"},
		{858, L"CombatLeapEnd"},
		{859, L"FlyCombatLeapEnd"},
		{860, L"ShaReadySpellCast"},
		{861, L"FlyShaReadySpellCast"},
		{862, L"ShaSpellPrecastBothChannel"},
		{863, L"FlyShaSpellPrecastBothChannel"},
		{864, L"ShaSpellCastBothUp"},
		{865, L"FlyShaSpellCastBothUp"},
		{866, L"ShaSpellCastBothUpChannel"},
		{867, L"FlyShaSpellCastBothUpChannel"},
		{868, L"ShaSpellCastBothFrontChannel"},
		{869, L"FlyShaSpellCastBothFrontChannel"},
		{870, L"ShaSpellCastLeftFrontChannel"},
		{871, L"FlyShaSpellCastLeftFrontChannel"},
		{872, L"ShaSpellCastRightFrontChannel"},
		{873, L"FlyShaSpellCastRightFrontChannel"},
		{874, L"PriReadySpellCast"},
		{875, L"FlyPriReadySpellCast"},
		{876, L"PriSpellPrecastBoth"},
		{877, L"FlyPriSpellPrecastBoth"},
		{878, L"PriSpellPrecastBothChannel"},
		{879, L"FlyPriSpellPrecastBothChannel"},
		{880, L"PriSpellCastBothUp"},
		{881, L"FlyPriSpellCastBothUp"},
		{882, L"PriSpellCastBothFront"},
		{883, L"FlyPriSpellCastBothFront"},
		{884, L"PriSpellCastLeftFront"},
		{885, L"FlyPriSpellCastLeftFront"},
		{886, L"PriSpellCastRightFront"},
		{887, L"FlyPriSpellCastRightFront"},
		{888, L"PriSpellCastBothUpChannel"},
		{889, L"FlyPriSpellCastBothUpChannel"},
		{890, L"PriSpellCastBothFrontChannel"},
		{891, L"FlyPriSpellCastBothFrontChannel"},
		{892, L"PriSpellCastLeftFrontChannel"},
		{893, L"FlyPriSpellCastLeftFrontChannel"},
		{894, L"PriSpellCastRightFrontChannel"},
		{895, L"FlyPriSpellCastRightFrontChannel"},
		{896, L"MagReadySpellCast"},
		{897, L"FlyMagReadySpellCast"},
		{898, L"MagSpellPrecastBoth"},
		{899, L"FlyMagSpellPrecastBoth"},
		{900, L"MagSpellPrecastBothChannel"},
		{901, L"FlyMagSpellPrecastBothChannel"},
		{902, L"MagSpellCastBothUp"},
		{903, L"FlyMagSpellCastBothUp"},
		{904, L"MagSpellCastBothFront"},
		{905, L"FlyMagSpellCastBothFront"},
		{906, L"MagSpellCastLeftFront"},
		{907, L"FlyMagSpellCastLeftFront"},
		{908, L"MagSpellCastRightFront"},
		{909, L"FlyMagSpellCastRightFront"},
		{910, L"MagSpellCastBothUpChannel"},
		{911, L"FlyMagSpellCastBothUpChannel"},
		{912, L"MagSpellCastBothFrontChannel"},
		{913, L"FlyMagSpellCastBothFrontChannel"},
		{914, L"MagSpellCastLeftFrontChannel"},
		{915, L"FlyMagSpellCastLeftFrontChannel"},
		{916, L"MagSpellCastRightFrontChannel"},
		{917, L"FlyMagSpellCastRightFrontChannel"},
		{918, L"LocReadySpellCast"},
		{919, L"FlyLocReadySpellCast"},
		{920, L"LocSpellPrecastBoth"},
		{921, L"FlyLocSpellPrecastBoth"},
		{922, L"LocSpellPrecastBothChannel"},
		{923, L"FlyLocSpellPrecastBothChannel"},
		{924, L"LocSpellCastBothUp"},
		{925, L"FlyLocSpellCastBothUp"},
		{926, L"LocSpellCastBothFront"},
		{927, L"FlyLocSpellCastBothFront"},
		{928, L"LocSpellCastLeftFront"},
		{929, L"FlyLocSpellCastLeftFront"},
		{930, L"LocSpellCastRightFront"},
		{931, L"FlyLocSpellCastRightFront"},
		{932, L"LocSpellCastBothUpChannel"},
		{933, L"FlyLocSpellCastBothUpChannel"},
		{934, L"LocSpellCastBothFrontChannel"},
		{935, L"FlyLocSpellCastBothFrontChannel"},
		{936, L"LocSpellCastLeftFrontChannel"},
		{937, L"FlyLocSpellCastLeftFrontChannel"},
		{938, L"LocSpellCastRightFrontChannel"},
		{939, L"FlyLocSpellCastRightFrontChannel"},
		{940, L"DruReadySpellCast"},
		{941, L"FlyDruReadySpellCast"},
		{942, L"DruSpellPrecastBoth"},
		{943, L"FlyDruSpellPrecastBoth"},
		{944, L"DruSpellPrecastBothChannel"},
		{945, L"FlyDruSpellPrecastBothChannel"},
		{946, L"DruSpellCastBothUp"},
		{947, L"FlyDruSpellCastBothUp"},
		{948, L"DruSpellCastBothFront"},
		{949, L"FlyDruSpellCastBothFront"},
		{950, L"DruSpellCastLeftFront"},
		{951, L"FlyDruSpellCastLeftFront"},
		{952, L"DruSpellCastRightFront"},
		{953, L"FlyDruSpellCastRightFront"},
		{954, L"DruSpellCastBothUpChannel"},
		{955, L"FlyDruSpellCastBothUpChannel"},
		{956, L"DruSpellCastBothFrontChannel"},
		{957, L"FlyDruSpellCastBothFrontChannel"},
		{958, L"DruSpellCastLeftFrontChannel"},
		{959, L"FlyDruSpellCastLeftFrontChannel"},
		{960, L"DruSpellCastRightFrontChannel"},
		{961, L"FlyDruSpellCastRightFrontChannel"},
		{962, L"ArtMainLoop"},
		{963, L"FlyArtMainLoop"},
		{964, L"ArtDualLoop"},
		{965, L"FlyArtDualLoop"},
		{966, L"ArtFistsLoop"},
		{967, L"FlyArtFistsLoop"},
		{968, L"ArtBowLoop"},
		{969, L"FlyArtBowLoop"},
		{970, L"CombatAbility1H01Off"},
		{971, L"FlyCombatAbility1H01Off"},
		{972, L"CombatAbility1H02Off"},
		{973, L"FlyCombatAbility1H02Off"},
		{974, L"CombatFuriousStrike01"},
		{975, L"FlyCombatFuriousStrike01"},
		{976, L"CombatFuriousStrike02"},
		{977, L"FlyCombatFuriousStrike02"},
		{978, L"CombatFuriousStrikes"},
		{979, L"FlyCombatFuriousStrikes"},
		{980, L"CombatReadySpellCast"},
		{981, L"FlyCombatReadySpellCast"},
		{982, L"CombatShieldThrow"},
		{983, L"FlyCombatShieldThrow"},
		{984, L"PalSpellCast1HUp"},
		{985, L"FlyPalSpellCast1HUp"},
		{986, L"CombatReadyPostSpellCast"},
		{987, L"FlyCombatReadyPostSpellCast"},
		{988, L"PriReadyPostSpellCast"},
		{989, L"FlyPriReadyPostSpellCast"},
		{990, L"DHCombatRun"},
		{991, L"FlyDHCombatRun"},
		{992, L"CombatShieldBash"},
		{993, L"FlyCombatShieldBash"},
		{994, L"CombatThrow"},
		{995, L"FlyCombatThrow"},
		{996, L"CombatAbility1HPierce"},
		{997, L"FlyCombatAbility1HPierce"},
		{998, L"CombatAbility1HOffPierce"},
		{999, L"FlyCombatAbility1HOffPierce"},
		{1000, L"CombatMutilate"},
		{1001, L"FlyCombatMutilate"},
		{1002, L"CombatBladeStorm"},
		{1003, L"FlyCombatBladeStorm"},
		{1004, L"CombatFinishingMove"},
		{1005, L"FlyCombatFinishingMove"},
		{1006, L"CombatLeapStart"},
		{1007, L"FlyCombatLeapStart"},
		{1008, L"GlvThrowMain"},
		{1009, L"FlyGlvThrowMain"},
		{1010, L"GlvThrownOff"},
		{1011, L"FlyGlvThrownOff"},
		{1012, L"DHCombatSprint"},
		{1013, L"FlyDHCombatSprint"},
		{1014, L"CombatAbilityGlv01"},
		{1015, L"FlyCombatAbilityGlv01"},
		{1016, L"CombatAbilityGlv02"},
		{1017, L"FlyCombatAbilityGlv02"},
		{1018, L"CombatAbilityGlvOff01"},
		{1019, L"FlyCombatAbilityGlvOff01"},
		{1020, L"CombatAbilityGlvOff02"},
		{1021, L"FlyCombatAbilityGlvOff02"},
		{1022, L"CombatAbilityGlvBig01"},
		{1023, L"FlyCombatAbilityGlvBig01"},
		{1024, L"CombatAbilityGlvBig02"},
		{1025, L"FlyCombatAbilityGlvBig02"},
		{1026, L"ReadyGlv"},
		{1027, L"FlyReadyGlv"},
		{1028, L"CombatAbilityGlvBig03"},
		{1029, L"FlyCombatAbilityGlvBig03"},
		{1030, L"DoubleJumpStart"},
		{1031, L"FlyDoubleJumpStart"},
		{1032, L"DoubleJump"},
		{1033, L"FlyDoubleJump"},
		{1034, L"CombatEviscerate"},
		{1035, L"FlyCombatEviscerate"},
		{1036, L"DoubleJumpLandRun"},
		{1037, L"FlyDoubleJumpLandRun"},
		{1038, L"BackFlipStart"},
		{1039, L"FlyBackFlipStart"},
		{1040, L"BackFlipLoop"},
		{1041, L"FlyBackFlipLoop"},
		{1042, L"FelRushLoop"},
		{1043, L"FlyFelRushLoop"},
		{1044, L"FelRushEnd"},
		{1045, L"FlyFelRushEnd"},
		{1046, L"DHToAlteredStart"},
		{1047, L"FlyDHToAlteredStart"},
		{1048, L"DHToAlteredEnd"},
		{1049, L"FlyDHToAlteredEnd"},
		{1050, L"DHGlide"},
		{1051, L"FlyDHGlide"},
		{1052, L"FanOfKnives"},
		{1053, L"FlyFanOfKnives"},
		{1054, L"SingleJumpStart"},
		{1055, L"FlySingleJumpStart"},
		{1056, L"DHBladeDance1"},
		{1057, L"FlyDHBladeDance1"},
		{1058, L"DHBladeDance2"},
		{1059, L"FlyDHBladeDance2"},
		{1060, L"DHBladeDance3"},
		{1061, L"FlyDHBladeDance3"},
		{1062, L"DHMeteorStrike"},
		{1063, L"FlyDHMeteorStrike"},
		{1064, L"CombatExecute"},
		{1065, L"FlyCombatExecute"},
		{1066, L"ArtLoop"},
		{1067, L"FlyArtLoop"},
		{1068, L"ParryGlv"},
		{1069, L"FlyParryGlv"},
		{1070, L"CombatUnarmed02"},
		{1071, L"FlyCombatUnarmed02"},
		{1072, L"CombatPistolShot"},
		{1073, L"FlyCombatPistolShot"},
		{1074, L"CombatPistolShotOff"},
		{1075, L"FlyCombatPistolShotOff"},
		{1076, L"Monk2HLIdle"},
		{1077, L"FlyMonk2HLIdle"},
		{1078, L"ArtShieldLoop"},
		{1079, L"FlyArtShieldLoop"},
		{1080, L"CombatAbility2H03"},
		{1081, L"FlyCombatAbility2H03"},
		{1082, L"CombatStomp"},
		{1083, L"FlyCombatStomp"},
		{1084, L"CombatRoar"},
		{1085, L"FlyCombatRoar"},
		{1086, L"PalReadySpellCast"},
		{1087, L"FlyPalReadySpellCast"},
		{1088, L"PalSpellPrecastRight"},
		{1089, L"FlyPalSpellPrecastRight"},
		{1090, L"PalSpellPrecastRightChannel"},
		{1091, L"FlyPalSpellPrecastRightChannel"},
		{1092, L"PalSpellCastRightFront"},
		{1093, L"FlyPalSpellCastRightFront"},
		{1094, L"ShaSpellCastBothOut"},
		{1095, L"FlyShaSpellCastBothOut"},
		{1096, L"AttackWeapon"},
		{1097, L"FlyAttackWeapon"},
		{1098, L"ReadyWeapon"},
		{1099, L"FlyReadyWeapon"},
		{1100, L"AttackWeaponOff"},
		{1101, L"FlyAttackWeaponOff"},
		{1102, L"SpecialDual"},
		{1103, L"FlySpecialDual"},
		{1104, L"DkCast1HFront"},
		{1105, L"FlyDkCast1HFront"},
		{1106, L"CastStrongRight"},
		{1107, L"FlyCastStrongRight"},
		{1108, L"CastStrongLeft"},
		{1109, L"FlyCastStrongLeft"},
		{1110, L"CastCurseRight"},
		{1111, L"FlyCastCurseRight"},
		{1112, L"CastCurseLeft"},
		{1113, L"FlyCastCurseLeft"},
		{1114, L"CastSweepRight"},
		{1115, L"FlyCastSweepRight"},
		{1116, L"CastSweepLeft"},
		{1117, L"FlyCastSweepLeft"},
		{1118, L"CastStrongUpLeft"},
		{1119, L"FlyCastStrongUpLeft"},
		{1120, L"CastTwistUpBoth"},
		{1121, L"FlyCastTwistUpBoth"},
		{1122, L"CastOutStrong"},
		{1123, L"FlyCastOutStrong"},
		{1124, L"DrumLoop"},
		{1125, L"FlyDrumLoop"},
		{1126, L"ParryWeapon"},
		{1127, L"FlyParryWeapon"},
		{1128, L"ReadyFL"},
		{1129, L"FlyReadyFL"},
		{1130, L"AttackFL"},
		{1131, L"FlyAttackFL"},
		{1132, L"AttackFLOff"},
		{1133, L"FlyAttackFLOff"},
		{1134, L"ParryFL"},
		{1135, L"FlyParryFL"},
		{1136, L"SpecialFL"},
		{1137, L"FlySpecialFL"},
		{1138, L"PriHoverForward"},
		{1139, L"FlyPriHoverForward"},
		{1140, L"PriHoverBackward"},
		{1141, L"FlyPriHoverBackward"},
		{1142, L"PriHoverRight"},
		{1143, L"FlyPriHoverRight"},
		{1144, L"PriHoverLeft"},
		{1145, L"FlyPriHoverLeft"},
		{1146, L"RunBackwards"},
		{1147, L"FlyRunBackwards"},
		{1148, L"CastStrongUpRight"},
		{1149, L"FlyCastStrongUpRight"},
		{1150, L"WAWalk"},
		{1151, L"FlyWAWalk"},
		{1152, L"WARun"},
		{1153, L"FlyWARun"},
		{1154, L"WADrunkStand"},
		{1155, L"FlyWADrunkStand"},
		{1156, L"WADrunkShuffleLeft"},
		{1157, L"FlyWADrunkShuffleLeft"},
		{1158, L"WADrunkShuffleRight"},
		{1159, L"FlyWADrunkShuffleRight"},
		{1160, L"WADrunkWalk"},
		{1161, L"FlyWADrunkWalk"},
		{1162, L"WADrunkWalkBackwards"},
		{1163, L"FlyWADrunkWalkBackwards"},
		{1164, L"WADrunkWound"},
		{1165, L"FlyWADrunkWound"},
		{1166, L"WADrunkTalk"},
		{1167, L"FlyWADrunkTalk"},
		{1168, L"WATrance01"},
		{1169, L"FlyWATrance01"},
		{1170, L"WATrance02"},
		{1171, L"FlyWATrance02"},
		{1172, L"WAChant01"},
		{1173, L"FlyWAChant01"},
		{1174, L"WAChant02"},
		{1175, L"FlyWAChant02"},
		{1176, L"WAChant03"},
		{1177, L"FlyWAChant03"},
		{1178, L"WAHang01"},
		{1179, L"FlyWAHang01"},
		{1180, L"WAHang02"},
		{1181, L"FlyWAHang02"},
		{1182, L"WASummon01"},
		{1183, L"FlyWASummon01"},
		{1184, L"WASummon02"},
		{1185, L"FlyWASummon02"},
		{1186, L"WABeggarTalk"},
		{1187, L"FlyWABeggarTalk"},
		{1188, L"WABeggarStand"},
		{1189, L"FlyWABeggarStand"},
		{1190, L"WABeggarPoint"},
		{1191, L"FlyWABeggarPoint"},
		{1192, L"WABeggarBeg"},
		{1193, L"FlyWABeggarBeg"},
		{1194, L"WASit01"},
		{1195, L"FlyWASit01"},
		{1196, L"WASit02"},
		{1197, L"FlyWASit02"},
		{1198, L"WASit03"},
		{1199, L"FlyWASit03"},
		{1200, L"WACrierStand01"},
		{1201, L"FlyWACrierStand01"},
		{1202, L"WACrierStand02"},
		{1203, L"FlyWACrierStand02"},
		{1204, L"WACrierStand03"},
		{1205, L"FlyWACrierStand03"},
		{1206, L"WACrierTalk"},
		{1207, L"FlyWACrierTalk"},
		{1208, L"WACrateHold"},
		{1209, L"FlyWACrateHold"},
		{1210, L"WABarrelHold"},
		{1211, L"FlyWABarrelHold"},
		{1212, L"WASackHold"},
		{1213, L"FlyWASackHold"},
		{1214, L"WAWheelBarrowStand"},
		{1215, L"FlyWAWheelBarrowStand"},
		{1216, L"WAWheelBarrowWalk"},
		{1217, L"FlyWAWheelBarrowWalk"},
		{1218, L"WAWheelBarrowRun"},
		{1219, L"FlyWAWheelBarrowRun"},
		{1220, L"WAHammerLoop"},
		{1221, L"FlyWAHammerLoop"},
		{1222, L"WACrankLoop"},
		{1223, L"FlyWACrankLoop"},
		{1224, L"WAPourStart"},
		{1225, L"FlyWAPourStart"},
		{1226, L"WAPourLoop"},
		{1227, L"FlyWAPourLoop"},
		{1228, L"WAPourEnd"},
		{1229, L"FlyWAPourEnd"},
		{1230, L"WAEmotePour"},
		{1231, L"FlyWAEmotePour"},
		{1232, L"WARowingStandRight"},
		{1233, L"FlyWARowingStandRight"},
		{1234, L"WARowingStandLeft"},
		{1235, L"FlyWARowingStandLeft"},
		{1236, L"WARowingRight"},
		{1237, L"FlyWARowingRight"},
		{1238, L"WARowingLeft"},
		{1239, L"FlyWARowingLeft"},
		{1240, L"WAGuardStand01"},
		{1241, L"FlyWAGuardStand01"},
		{1242, L"WAGuardStand02"},
		{1243, L"FlyWAGuardStand02"},
		{1244, L"WAGuardStand03"},
		{1245, L"FlyWAGuardStand03"},
		{1246, L"WAGuardStand04"},
		{1247, L"FlyWAGuardStand04"},
		{1248, L"WAFreezing01"},
		{1249, L"FlyWAFreezing01"},
		{1250, L"WAFreezing02"},
		{1251, L"FlyWAFreezing02"},
		{1252, L"WAVendorStand01"},
		{1253, L"FlyWAVendorStand01"},
		{1254, L"WAVendorStand02"},
		{1255, L"FlyWAVendorStand02"},
		{1256, L"WAVendorStand03"},
		{1257, L"FlyWAVendorStand03"},
		{1258, L"WAVendorTalk"},
		{1259, L"FlyWAVendorTalk"},
		{1260, L"WALean01"},
		{1261, L"FlyWALean01"},
		{1262, L"WALean02"},
		{1263, L"FlyWALean02"},
		{1264, L"WALean03"},
		{1265, L"FlyWALean03"},
		{1266, L"WALeanTalk"},
		{1267, L"FlyWALeanTalk"},
		{1268, L"WABoatWheel"},
		{1269, L"FlyWABoatWheel"},
		{1270, L"WASmithLoop"},
		{1271, L"FlyWASmithLoop"},
		{1272, L"WAScrubbing"},
		{1273, L"FlyWAScrubbing"},
		{1274, L"WAWeaponSharpen"},
		{1275, L"FlyWAWeaponSharpen"},
		{1276, L"WAStirring"},
		{1277, L"FlyWAStirring"},
		{1278, L"WAPerch01"},
		{1279, L"FlyWAPerch01"},
		{1280, L"WAPerch02"},
		{1281, L"FlyWAPerch02"},
		{1282, L"HoldWeapon"},
		{1283, L"FlyHoldWeapon"},
		{1284, L"WABarrelWalk"},
		{1285, L"FlyWABarrelWalk"},
		{1286, L"WAPourHold"},
		{1287, L"FlyWAPourHold"},
		{1288, L"CastStrong"},
		{1289, L"FlyCastStrong"},
		{1290, L"CastCurse"},
		{1291, L"FlyCastCurse"},
		{1292, L"CastSweep"},
		{1293, L"FlyCastSweep"},
		{1294, L"CastStrongUp"},
		{1295, L"FlyCastStrongUp"},
		{1296, L"WABoatWheelStand"},
		{1297, L"FlyWABoatWheelStand"},
		{1298, L"WASmithStand"},
		{1299, L"FlyWASmithStand"},
		{1300, L"WACrankStand"},
		{1301, L"FlyWACrankStand"},
		{1302, L"WAPourWalk"},
		{1303, L"FlyWAPourWalk"},
		{1304, L"FalconeerStart"},
		{1305, L"FlyFalconeerStart"},
		{1306, L"FalconeerLoop"},
		{1307, L"FlyFalconeerLoop"},
		{1308, L"FalconeerEnd"},
		{1309, L"FlyFalconeerEnd"},
		{1310, L"WADrunkDrink"},
		{1311, L"FlyWADrunkDrink"},
		{1312, L"WAStandEat"},
		{1313, L"FlyWAStandEat"},
		{1314, L"WAStandDrink"},
		{1315, L"FlyWAStandDrink"},
		{1316, L"WABound01"},
		{1317, L"FlyWABound01"},
		{1318, L"WABound02"},
		{1319, L"FlyWABound02"},
		{1320, L"CombatAbility1H03Off"},
		{1321, L"FlyCombatAbility1H03Off"},
		{1322, L"CombatAbilityDualWield01"},
		{1323, L"FlyCombatAbilityDualWield01"},
		{1324, L"WACradle01"},
		{1325, L"FlyWACradle01"},
		{1326, L"LocSummon"},
		{1327, L"FlyLocSummon"},
		{1328, L"LoadWeapon"},
		{1329, L"FlyLoadWeapon"},
		{1330, L"ArtOffLoop"},
		{1331, L"FlyArtOffLoop"},
		{1332, L"WADead01"},
		{1333, L"FlyWADead01"},
		{1334, L"WADead02"},
		{1335, L"FlyWADead02"},
		{1336, L"WADead03"},
		{1337, L"FlyWADead03"},
		{1338, L"WADead04"},
		{1339, L"FlyWADead04"},
		{1340, L"WADead05"},
		{1341, L"FlyWADead05"},
		{1342, L"WADead06"},
		{1343, L"FlyWADead06"},
		{1344, L"WADead07"},
		{1345, L"FlyWADead07"},
		{1346, L"GiantRun"},
		{1347, L"FlyGiantRun"},
		{1348, L"BarTendEmoteCheer"},
		{1349, L"FlyBarTendEmoteCheer"},
		{1350, L"BarTendEmoteTalkQuestion"},
		{1351, L"FlyBarTendEmoteTalkQuestion"},
		{1352, L"BarTendEmoteTalkExclamation"},
		{1353, L"FlyBarTendEmoteTalkExclamation"},
		{1354, L"BarTendWalk"},
		{1355, L"FlyBarTendWalk"},
		{1356, L"BartendShuffleLeft"},
		{1357, L"FlyBartendShuffleLeft"},
		{1358, L"BarTendShuffleRight"},
		{1359, L"FlyBarTendShuffleRight"},
		{1360, L"BarTendCustomSpell01"},
		{1361, L"FlyBarTendCustomSpell01"},
		{1362, L"BarTendCustomSpell02"},
		{1363, L"FlyBarTendCustomSpell02"},
		{1364, L"BarTendCustomSpell03"},
		{1365, L"FlyBarTendCustomSpell03"},
		{1366, L"BarServerEmoteCheer"},
		{1367, L"FlyBarServerEmoteCheer"},
		{1368, L"BarServerEmoteTalkQuestion"},
		{1369, L"FlyBarServerEmoteTalkQuestion"},
		{1370, L"BarServerEmoteTalkExclamation"},
		{1371, L"FlyBarServerEmoteTalkExclamation"},
		{1372, L"BarServerCustomSpell01"},
		{1373, L"FlyBarServerCustomSpell01"},
		{1374, L"BarServerCustomSpell02"},
		{1375, L"FlyBarServerCustomSpell02"},
		{1376, L"BarServerCustomSpell03"},
		{1377, L"FlyBarServerCustomSpell03"},
		{1378, L"BarPatronEmoteDrink"},
		{1379, L"FlyBarPatronEmoteDrink"},
		{1380, L"BarPatronEmoteCheer"},
		{1381, L"FlyBarPatronEmoteCheer"},
		{1382, L"BarPatronCustomSpell01"},
		{1383, L"FlyBarPatronCustomSpell01"},
		{1384, L"BarPatronCustomSpell02"},
		{1385, L"FlyBarPatronCustomSpell02"},
		{1386, L"BarPatronCustomSpell03"},
		{1387, L"FlyBarPatronCustomSpell03"},
		{1388, L"HoldDart"},
		{1389, L"FlyHoldDart"},
		{1390, L"ReadyDart"},
		{1391, L"FlyReadyDart"},
		{1392, L"AttackDart"},
		{1393, L"FlyAttackDart"},
		{1394, L"LoadDart"},
		{1395, L"FlyLoadDart"},
		{1396, L"WADartTargetStand"},
		{1397, L"FlyWADartTargetStand"},
		{1398, L"WADartTargetEmoteTalk"},
		{1399, L"FlyWADartTargetEmoteTalk"},
		{1400, L"BarPatronSitEmoteCheer"},
		{1401, L"FlyBarPatronSitEmoteCheer"},
		{1402, L"BarPatronSitCustomSpell01"},
		{1403, L"FlyBarPatronSitCustomSpell01"},
		{1404, L"BarPatronSitCustomSpell02"},
		{1405, L"FlyBarPatronSitCustomSpell02"},
		{1406, L"BarPatronSitCustomSpell03"},
		{1407, L"FlyBarPatronSitCustomSpell03"},
		{1408, L"BarPianoStand"},
		{1409, L"FlyBarPianoStand"},
		{1410, L"BarPianoEmoteTalk"},
		{1411, L"FlyBarPianoEmoteTalk"},
		{1412, L"WAHearthSit"},
		{1413, L"FlyWAHearthSit"},
		{1414, L"WAHearthSitEmoteCry"},
		{1415, L"FlyWAHearthSitEmoteCry"},
		{1416, L"WAHearthSitEmoteCheer"},
		{1417, L"FlyWAHearthSitEmoteCheer"},
		{1418, L"WAHearthSitCustomSpell01"},
		{1419, L"FlyWAHearthSitCustomSpell01"},
		{1420, L"WAHearthSitCustomSpell02"},
		{1421, L"FlyWAHearthSitCustomSpell02"},
		{1422, L"WAHearthSitCustomSpell03"},
		{1423, L"FlyWAHearthSitCustomSpell03"},
		{1424, L"WAHearthStand"},
		{1425, L"FlyWAHearthStand"},
		{1426, L"WAHearthStandEmoteCheer"},
		{1427, L"FlyWAHearthStandEmoteCheer"},
		{1428, L"WAHearthStandEmoteTalk"},
		{1429, L"FlyWAHearthStandEmoteTalk"},
		{1430, L"WAHearthStandCustomSpell01"},
		{1431, L"FlyWAHearthStandCustomSpell01"},
		{1432, L"WAHearthStandCustomSpell02"},
		{1433, L"FlyWAHearthStandCustomSpell02"},
		{1434, L"WAHearthStandCustomSpell03"},
		{1435, L"FlyWAHearthStandCustomSpell03"},
		{1436, L"WAScribeStart"},
		{1437, L"FlyWAScribeStart"},
		{1438, L"WAScribeLoop"},
		{1439, L"FlyWAScribeLoop"},
		{1440, L"WAScribeEnd"},
		{1441, L"FlyWAScribeEnd"},
		{1442, L"WAEmoteScribe"},
		{1443, L"FlyWAEmoteScribe"},
		{1444, L"Haymaker"},
		{1445, L"FlyHaymaker"},
		{1446, L"HaymakerPrecast"},
		{1447, L"FlyHaymakerPrecast"},
		{1448, L"ChannelCastOmniUp"},
		{1449, L"FlyChannelCastOmniUp	"},
		{1450, L"DHJumpLandRun	"},
		{1451, L"FlyDHJumpLandRun	"},
		{1452, L"Cinematic01	"},
		{1453, L"FlyCinematic01	"},
		{1454, L"Cinematic02	"},
		{1455, L"FlyCinematic02	"},
		{1456, L"Cinematic03	"},
		{1457, L"FlyCinematic03	"},
		{1458, L"Cinematic04	"},
		{1459, L"FlyCinematic04	"},
		{1460, L"Cinematic05	"},
		{1461, L"FlyCinematic05	"},
		{1462, L"Cinematic06	"},
		{1463, L"FlyCinematic06	"},
		{1464, L"Cinematic07	"},
		{1465, L"FlyCinematic07	"},
		{1466, L"Cinematic08	"},
		{1467, L"FlyCinematic08	"},
		{1468, L"Cinematic09	"},
		{1469, L"FlyCinematic09	"},
		{1470, L"Cinematic10	"},
		{1471, L"FlyCinematic10"},
		{1472, L"TakeOffStart	"},
		{1473, L"FlyTakeOffStart	"},
		{1474, L"TakeOffFinish	"},
		{1475, L"FlyTakeOffFinish	"},
		{1476, L"LandStart	"},
		{1477, L"FlyLandStart	"},
		{1478, L"LandFinish	"},
		{1479, L"FlyLandFinish"},
		{1480, L"WAWalkTalk	"},
		{1481, L"FlyWAWalkTalk	"},
		{1482, L"WAPerch03	"},
		{1483, L"FlyWAPerch03"},
		{1484, L"CarriageMountMoving	"},
		{1485, L"FlyCarriageMountMoving"},
		{1486, L"TakeOffFinishFly	"},
		{1487, L"FlyTakeOffFinishFly	"},
		{1488, L"CombatAbility2HBig02	"},
		{1489, L"FlyCombatAbility2HBig02	"},
		{1490, L"MountWide	"},
		{1491, L"FlyMountWide"},
		{1492, L"EmoteTalkSubdued	"},
		{1493, L"FlyEmoteTalkSubdued"},
		{1494, L"WASit04	"},
		{1495, L"FlyWASit04"},
		{1496, L"MountSummon	"},
		{1497, L"FlyMountSummon"},
		{1498, L"EmoteSelfie"},
		{1499, L"FlyEmoteSelfie"},
		{1500, L"CustomSpell11"},
		{1501, L"FlyCustomSpell11"},
		{1502, L"CustomSpell12"},
		{1503, L"FlyCustomSpell12"},
		{1504, L"CustomSpell13"},
		{1505, L"FlyCustomSpell13"},
		{1506, L"CustomSpell14"},
		{1507, L"FlyCustomSpell14"},
		{1508, L"CustomSpell15"},
		{1509, L"FlyCustomSpell15"},
		{1510, L"CustomSpell16"},
		{1511, L"FlyCustomSpell16"},
		{1512, L"CustomSpell17"},
		{1513, L"FlyCustomSpell17"},
		{1514, L"CustomSpell18"},
		{1515, L"FlyCustomSpell18"},
		{1516, L"CustomSpell19"},
		{1517, L"FlyCustomSpell19"},
		{1518, L"CustomSpell20"},
		{1519, L"FlyCustomSpell20"},
		{1520, L"AdvFlyLeft"},
		{1521, L"FlyAdvFlyLeft"},
		{1522, L"AdvFlyRight"},
		{1523, L"FlyAdvFlyRight"},
		{1524, L"AdvFlyForward"},
		{1525, L"FlyAdvFlyForward"},
		{1526, L"AdvFlyBackward"},
		{1527, L"FlyAdvFlyBackward"},
		{1528, L"AdvFlyUp"},
		{1529, L"FlyAdvFlyUp"},
		{1530, L"AdvFlyDown"},
		{1531, L"FlyAdvFlyDown"},
		{1532, L"AdvFlyForwardGlide"},
		{1533, L"FlyAdvFlyForwardGlide"},
		{1534, L"AdvFlyRoll"},
		{1535, L"FlyAdvFlyRoll"},
		{1536, L"ProfCookingLoop"},
		{1537, L"FlyProfCookingLoop"},
		{1538, L"ProfCookingStart"},
		{1539, L"FlyProfCookingStart"},
		{1540, L"ProfCookingEnd"},
		{1541, L"FlyProfCookingEnd"},
		{1542, L"WACurious"},
		{1543, L"FlyWACurious"},
		{1544, L"WAAlert"},
		{1545, L"FlyWAAlert"},
		{1546, L"WAInvestigate"},
		{1547, L"FlyWAInvestigate"},
		{1548, L"WAInteraction"},
		{1549, L"FlyWAInteraction"},
		{1550, L"WAThreaten"},
		{1551, L"FlyWAThreaten"},
		{1552, L"WAReact01"},
		{1553, L"FlyWAReact01"},
		{1554, L"WAReact02"},
		{1555, L"FlyWAReact02"},
		{1556, L"AdvFlyRollStart"},
		{1557, L"FlyAdvFlyRollStart"},
		{1558, L"AdvFlyRollEnd"},
		{1559, L"FlyAdvFlyRollEnd"},
		{1560, L"EmpBreathPrecast"},
		{1561, L"FlyEmpBreathPrecast"},
		{1562, L"EmpBreathPrecastChannel"},
		{1563, L"FlyEmpBreathPrecastChannel"},
		{1564, L"EmpBreathSpellCast"},
		{1565, L"FlyEmpBreathSpellCast"},
		{1566, L"EmpBreathSpellCastChannel"},
		{1567, L"FlyEmpBreathSpellCastChannel"},
		{1568, L"DracFlyBreathTakeoffStart"},
		{1569, L"FlyDracFlyBreathTakeoffStart"},
		{1570, L"DracFlyBreathTakeoffFinish"},
		{1571, L"FlyDracFlyBreathTakeoffFinish"},
		{1572, L"DracFlyBreath"},
		{1573, L"FlyDracFlyBreath"},
		{1574, L"DracFlyBreathLandStart"},
		{1575, L"FlyDracFlyBreathLandStart"},
		{1576, L"DracFlyBreathLandFinish"},
		{1577, L"FlyDracFlyBreathLandFinish"},
		{1578, L"DracAirDashLeft"},
		{1579, L"FlyDracAirDashLeft"},
		{1580, L"DracAirDashForward"},
		{1581, L"FlyDracAirDashForward"},
		{1582, L"DracAirDashBackward"},
		{1583, L"FlyDracAirDashBackward"},
		{1584, L"DracAirDashRight"},
		{1585, L"FlyDracAirDashRight"},
		{1586, L"LivingWorldProximityEnter"},
		{1587, L"FlyLivingWorldProximityEnter"},
		{1588, L"AdvFlyDownEnd"},
		{1589, L"FlyAdvFlyDownEnd"},
		{1590, L"LivingWorldProximityLoop"},
		{1591, L"FlyLivingWorldProximityLoop"},
		{1592, L"LivingWorldProximityLeave"},
		{1593, L"FlyLivingWorldProximityLeave"},
		{1594, L"EmpAirBarragePrecast"},
		{1595, L"FlyEmpAirBarragePrecast"},
		{1596, L"EmpAirBarragePrecastChannel"},
		{1597, L"FlyEmpAirBarragePrecastChannel"},
		{1598, L"EmpAirBarrageSpellCast"},
		{1599, L"FlyEmpAirBarrageSpellCast"},
		{1600, L"DracClawSwipeLeft"},
		{1601, L"FlyDracClawSwipeLeft"},
		{1602, L"DracClawSwipeRight"},
		{1603, L"FlyDracClawSwipeRight"},
		{1604, L"DracHoverIdle"},
		{1605, L"FlyDracHoverIdle"},
		{1606, L"DracHoverLeft"},
		{1607, L"FlyDracHoverLeft"},
		{1608, L"DracHoverRight"},
		{1609, L"FlyDracHoverRight"},
		{1610, L"DracHoverBackward"},
		{1611, L"FlyDracHoverBackward"},
		{1612, L"DracHoverForward"},
		{1613, L"FlyDracHoverForward"},
		{1614, L"DracAttackWings"},
		{1615, L"FlyDracAttackWings"},
		{1616, L"DracAttackTail"},
		{1617, L"FlyDracAttackTail"},
		{1618, L"AdvFlyStart"},
		{1619, L"FlyAdvFlyStart"},
		{1620, L"AdvFlyLand"},
		{1621, L"FlyAdvFlyLand"},
		{1622, L"AdvFlyLandRun"},
		{1623, L"FlyAdvFlyLandRun"},
		{1624, L"AdvFlyStrafeLeft"},
		{1625, L"FlyAdvFlyStrafeLeft"},
		{1626, L"AdvFlyStrafeRight"},
		{1627, L"FlyAdvFlyStrafeRight"},
		{1628, L"AdvFlyIdle"},
		{1629, L"FlyAdvFlyIdle"},
		{1630, L"AdvFlyRollRight"},
		{1631, L"FlyAdvFlyRollRight"},
		{1632, L"AdvFlyRollRightEnd"},
		{1633, L"FlyAdvFlyRollRightEnd"},
		{1634, L"AdvFlyRollLeft"},
		{1635, L"FlyAdvFlyRollLeft"},
		{1636, L"AdvFlyRollLeftEnd"},
		{1637, L"FlyAdvFlyRollLeftEnd"},
		{1638, L"AdvFlyFlap"},
		{1639, L"FlyAdvFlyFlap"},
		{1640, L"DracHoverDracClawSwipeLeft"},
		{1641, L"FlyDracHoverDracClawSwipeLeft"},
		{1642, L"DracHoverDracClawSwipeRight"},
		{1643, L"FlyDracHoverDracClawSwipeRight"},
		{1644, L"DracHoverDracAttackWings"},
		{1645, L"FlyDracHoverDracAttackWings"},
		{1646, L"DracHoverReadySpellOmni"},
		{1647, L"FlyDracHoverReadySpellOmni"},
		{1648, L"DracHoverSpellCastOmni"},
		{1649, L"FlyDracHoverSpellCastOmni"},
		{1650, L"DracHoverChannelSpellOmni"},
		{1651, L"FlyDracHoverChannelSpellOmni"},
		{1652, L"DracHoverReadySpellDirected"},
		{1653, L"FlyDracHoverReadySpellDirected"},
		{1654, L"DracHoverChannelSpellDirected"},
		{1655, L"FlyDracHoverChannelSpellDirected"},
		{1656, L"DracHoverSpellCastDirected"},
		{1657, L"FlyDracHoverSpellCastDirected"},
		{1658, L"DracHoverCastOutStrong"},
		{1659, L"FlyDracHoverCastOutStrong"},
		{1660, L"DracHoverBattleRoar"},
		{1661, L"FlyDracHoverBattleRoar"},
		{1662, L"DracHoverEmpBreathSpellCast"},
		{1663, L"FlyDracHoverEmpBreathSpellCast"},
		{1664, L"DracHoverEmpBreathSpellCastChannel"},
		{1665, L"FlyDracHoverEmpBreathSpellCastChannel"},
		{1666, L"LivingWorldTimeOfDayEnter"},
		{1667, L"FlyLivingWorldTimeOfDayEnter"},
		{1668, L"LivingWorldTimeOfDayLoop"},
		{1669, L"FlyLivingWorldTimeOfDayLoop"},
		{1670, L"LivingWorldTimeOfDayLeave"},
		{1671, L"FlyLivingWorldTimeOfDayLeave"},
		{1672, L"LivingWorldWeatherEnter"},
		{1673, L"FlyLivingWorldWeatherEnter"},
		{1674, L"LivingWorldWeatherLoop"},
		{1675, L"FlyLivingWorldWeatherLoop"},
		{1676, L"LivingWorldWeatherLeave"},
		{1677, L"FlyLivingWorldWeatherLeave"},
		{1678, L"AdvFlyDownStart"},
		{1679, L"FlyAdvFlyDownStart"},
		{1680, L"AdvFlyFlapBig"},
		{1681, L"FlyAdvFlyFlapBig"},
		{1682, L"DracHoverReadyUnarmed"},
		{1683, L"FlyDracHoverReadyUnarmed"},
		{1684, L"DracHoverAttackUnarmed"},
		{1685, L"FlyDracHoverAttackUnarmed"},
		{1686, L"DracHoverParryUnarmed"},
		{1687, L"FlyDracHoverParryUnarmed"},
		{1688, L"DracHoverCombatWound"},
		{1689, L"FlyDracHoverCombatWound"},
		{1690, L"DracHoverCombatCritical"},
		{1691, L"FlyDracHoverCombatCritical"},
		{1692, L"DracHoverAttackTail"},
		{1693, L"FlyDracHoverAttackTail"},
		{1694, L"Glide"},
		{1695, L"FlyGlide"},
		{1696, L"GlideEnd"},
		{1697, L"FlyGlideEnd"},
		{1698, L"DracClawSwipe"},
		{1699, L"FlyDracClawSwipe"},
		{1700, L"DracHoverDracClawSwipe"},
		{1701, L"FlyDracHoverDracClawSwipe"},
		{1702, L"AdvFlyFlapUp"},
		{1703, L"FlyAdvFlyFlapUp"},
		{1704, L"AdvFlySlowFall"},
		{1705, L"FlyAdvFlySlowFall"},
		{1706, L"AdvFlyFlapFoward"},
		{1707, L"FlyAdvFlyFlapFoward"},
		{1708, L"DracSpellCastWings"},
		{1709, L"FlyDracSpellCastWings"},
		{1710, L"DracHoverDracSpellCastWings"},
		{1711, L"FlyDracHoverDracSpellCastWings"},
		{1712, L"DracAirDashVertical"},
		{1713, L"FlyDracAirDashVertical"},
		{1714, L"DracAirDashRefresh"},
		{1715, L"FlyDracAirDashRefresh"},
		{1716, L"SkinningLoop"},
		{1717, L"FlySkinningLoop"},
		{1718, L"SkinningStart"},
		{1719, L"FlySkinningStart"},
		{1720, L"SkinningEnd"},
		{1721, L"FlySkinningEnd"},
		{1722, L"AdvFlyForwardGlideSlow"},
		{1723, L"FlyAdvFlyForwardGlideSlow"},
		{1724, L"AdvFlyForwardGlideFast"},
		{1725, L"FlyAdvFlyForwardGlideFast"},
		{1726, L"AdvFlySecondFlapUp"},
		{1727, L"FlyAdvFlySecondFlapUp"},
		{1728, L"FloatIdle"},
		{1729, L"FlyFloatIdle"},
		{1730, L"FloatWalk"},
		{1731, L"FlyFloatWalk"},
		{1732, L"CinematicTalk"},
		{1733, L"FlyCinematicTalk"},
		{1734, L"CinematicWAGuardEmoteSlam01"},
		{1735, L"FlyCinematicWAGuardEmoteSlam01"},
		{1736, L"WABlowHorn"},
		{1737, L"FlyWABlowHorn"},
		{1738, L"MountExtraWide"},
		{1739, L"FlyMountExtraWide"},
		{1740, L"WA2HIdle"},
		{1741, L"FlyWA2HIdle"},
		{1742, L"HerbalismLoop"},
		{1743, L"FlyHerbalismLoop"},
		{1744, L"CookingLoop"},
		{1745, L"FlyCookingLoop"},
		{1746, L"WAWeaponSharpenNoSheathe"},
		{1747, L"FlyWAWeaponSharpenNoSheathe"},
		{1748, L"CinematicDeath"},
		{1749, L"FlyCinematicDeath"},
		{1750, L"CinematicDeathPose"},
		{1751, L"FlyCinematicDeathPose"},
		{1752, L"EmpSlamPrecast"},
		{1753, L"FlyEmpSlamPrecast"},
		{1754, L"EmpSlamPrecastChannel"},
		{1755, L"FlyEmpSlamPrecastChannel"},
		{1756, L"EmpSlamSpellCast"},
		{1757, L"FlyEmpSlamSpellCast"},
		{1758, L"Climb"},
		{1759, L"FlyClimb"},
		{1760, L"ClimbStart"},
		{1761, L"FlyClimbStart"},
		{1762, L"ClimbEnd"},
		{1763, L"FlyClimbEnd"},
		{1764, L"MountLeanLeft"},
		{1765, L"FlyMountLeanLeft"},
		{1766, L"MountLeanRight"},
		{1767, L"FlyMountLeanRight"},
		{1768, L"MountDive"},
		{1769, L"FlyMountDive"},
		{1770, L"MountCrouch"},
		{1771, L"FlyMountCrouch"}
	};

	if (animated && anims.size() > 0)
	{
		for (const auto& entry : AnimationNames)
		{
			result[entry.first] = entry.second;
		}

		if (!result.empty())
		{
			LOG_INFO << "Found " << result.size() << " animations for model";
		}
	}
	return result;
}

void WoWModel::save(QXmlStreamWriter& stream)
{
	stream.writeStartElement("model");
	stream.writeStartElement("file");
	stream.writeAttribute("name", QString::fromStdString(modelname));
	stream.writeEndElement();
	cd.save(stream);
	stream.writeEndElement(); // model
}

void WoWModel::load(QString& file)
{
	cd.load(file);
}

bool WoWModel::canSetTextureFromFile(int texnum)
{
	for (size_t i = 0; i < TEXTURE_MAX; i++)
	{
		if (specialTextures[i] == texnum)
			return true;
	}
	return false;
}

QString WoWModel::getCGGroupName(CharGeosets cg)
{
	QString result = "";

	static std::map<CharGeosets, QString> groups =
	{
		{CG_SKIN_OR_HAIR, "Skin or Hair"},
		{CG_FACE_1, "Face 1"},
		{CG_FACE_2, "Face 2"},
		{CG_FACE_3, "Face 3"},
		{CG_GLOVES, "Bracers"},
		{CG_BOOTS, "Boots"},
		{CG_EARS, "Ears"},
		{CG_SLEEVES, "Sleeves"},
		{CG_KNEEPADS, "Kneepads"},
		{CG_CHEST, "Chest"},
		{CG_PANTS, "Pants"},
		{CG_TABARD, "Tabard"},
		{CG_TROUSERS, "Trousers"},
		{CG_DH_LOINCLOTH, "Demon Hunter Loincloth"},
		{CG_CLOAK, "Cloak"},
		{CG_EYEGLOW, "Eye Glow"},
		{CG_BELT, "Belt"},
		{CG_BONE, "Bone"},
		{CG_FEET, "Feet"},
		{CG_GEOSET2100, "Geoset2100"},
		{CG_TORSO, "Torso"},
		{CG_HAND_ATTACHMENT, "Hand Attachment"},
		{CG_HEAD_ATTACHMENT, "Head Attachment"},
		{CG_DH_BLINDFOLDS, "Demon Hunter Blindfolds"},
		{CG_GEOSET2600, "Geoset2600"},
		{CG_GEOSET2700, "Geoset2700"},
		{CG_GEOSET2800, "Geoset2800"},
		{CG_MECHAGNOME_ARMS_OR_HANDS, "Mechagnome Arms or Hands"},
		{CG_MECHAGNOME_LEGS, "Mechagnome Legs"},
		{CG_MECHAGNOME_FEET, "Mechagnome Feet"},
		{CG_FACE, "Face"},
		{CG_EYES, "Eyes"},
		{CG_EYEBROWS, "Eyebrows"},
		{CG_EARRINGS, "Earrings"},
		{CG_NECKLACE, "Necklace"},
		{CG_HEADDRESS, "Headdress"},
		{CG_TAILS, "Tails"},
		{CG_VINES, "Vines"},
		{CG_TUSKS, "Tusks"},
		{CG_NOSES, "Noses"},
		{CG_HAIR_DECORATION, "Hair Decoration"},
		{CG_HORN_DECORATION, "Horn Decoration"}
	};

	const auto it = groups.find(cg);
	if (it != groups.end())
		result = it->second;

	return result;
}

void WoWModel::showGeoset(uint geosetindex, bool value)
{
	if (geosetindex < geosets.size())
		geosets[geosetindex]->display = value;
}

bool WoWModel::isGeosetDisplayed(uint geosetindex)
{
	bool result = false;

	if (geosetindex < geosets.size())
		result = geosets[geosetindex]->display;

	return result;
}

void WoWModel::setGeosetGroupDisplay(CharGeosets group, int val)
{
	const int a = static_cast<int>(group) * 100;
	const int b = (static_cast<int>(group) + 1) * 100;
	const int geosetID = a + val;

	// This loop must be done only on first geosets (original ones) in case of merged models
	// This is why rawGeosets.size() is used as a stop criteria even if we are looping over
	// geosets member
	for (uint i = 0; i < rawGeosets.size(); i++)
	{
		const int id = geosets[i]->id;
		if (id > a && id < b)
			showGeoset(i, (id == geosetID));
	}
	/*
	for (auto it : mergedModels)
	  it->setGeosetGroupDisplay(group, val);
	*/
}

void WoWModel::setCreatureGeosetData(std::set<GeosetNum> cgd)
{
	// Hide geosets that were set by old creatureGeosetData:
	if (creatureGeosetData.size() > 0)
		restoreRawGeosets();

	creatureGeosetData = cgd;

	if (cgd.size() == 0 && creatureGeosetDataID == 0)
		return;

	int geomax = 900;

	if (cgd.size() > 0)
	{
		geomax = *cgd.rbegin() + 1; // highest value in set
		// We should only be dealing with geosets below 900, but just in case
		// Blizzard changes this, we'll set the max higher and log that it's happened:
		if (geomax > 900)
		{
			LOG_ERROR << "setCreatureGeosetData value of " << geomax <<
				" detected. We were assuming the maximum was 899.";
			geomax = ((geomax / 100) + 1) * 100; // round the max up to the next 100 (next geoset group)
		}
		else
			geomax = 900;
	}

	// If creatureGeosetData is used , or creatureGeosetDataID > 0, then
	// we switch off ALL geosets from 1 to 899 that aren't specified by it:
	for (uint i = 0; i < rawGeosets.size(); i++)
	{
		int id = geosets[i]->id;
		if (id > 0 && id < geomax)
			showGeoset(i, cgd.count(id) > 0);
	}
}

WoWModel* WoWModel::mergeModel(QString& name, int type, bool noRefresh)
{
	name = name.replace("\\", "/");

	LOG_INFO << __FUNCTION__ << name;
	const auto it = std::find_if(std::begin(mergedModels), std::end(mergedModels),
	                             [&](const WoWModel* m) { return m->gamefile->fullname() == name; });

	if (it != mergedModels.end())
		return *it;

	WoWModel* m = new WoWModel(GAMEDIRECTORY.getFile(name), true);

	if (!m->ok)
		return nullptr;

	m->mergedModelType = type;
	return mergeModel(m, type, noRefresh);
}

WoWModel* WoWModel::mergeModel(uint fileID, int type, bool noRefresh)
{
	LOG_INFO << __FUNCTION__ << fileID;
	const auto it = std::find_if(std::begin(mergedModels), std::end(mergedModels),
	                             [&](const WoWModel* m) { return m->gamefile->fileDataId() == fileID; });
	if (it != mergedModels.end())
		return *it;

	WoWModel* m = new WoWModel(GAMEDIRECTORY.getFile(fileID), true);

	if (!m->ok)
		return nullptr;

	m->mergedModelType = type;
	return mergeModel(m, type, noRefresh);
}

WoWModel* WoWModel::mergeModel(WoWModel* m, int type, bool noRefresh)
{
	LOG_INFO << __FUNCTION__ << m;
	m->mergedModelType = type;
	const auto it = mergedModels.insert(m);
	if (it.second == true && !noRefresh) // new element inserted
		refreshMerging();
	return m;
}

WoWModel* WoWModel::getMergedModel(uint fileID)
{
	for (const auto it : mergedModels)
	{
		if (it->gamefile->fileDataId() == fileID)
			return it;
	}
	return nullptr;
}

void WoWModel::refreshMerging()
{
	/*
	LOG_INFO << __FUNCTION__;
	for (auto it : mergedModels)
	  LOG_INFO << it->name() << it->gamefile->fullname();
	  */

	// first reinit this model with original data
	origVertices = rawVertices;
	indices = rawIndices;
	passes = rawPasses;
	geosets = rawGeosets;
	textures.resize(TEXTURE_MAX);
	replaceTextures.resize(TEXTURE_MAX);
	specialTextures.resize(TEXTURE_MAX);

	uint mergeIndex = 0;
	for (const auto modelsIt : mergedModels)
	{
		const uint nbVertices = origVertices.size();
		const uint nbIndices = indices.size();
		const uint nbGeosets = geosets.size();

		// reinit merged model as well, just in case
		modelsIt->origVertices = modelsIt->rawVertices;
		modelsIt->indices = modelsIt->rawIndices;
		modelsIt->passes = modelsIt->rawPasses;
		modelsIt->restoreRawGeosets();

		mergeIndex++;

		for (auto it : modelsIt->geosets)
		{
			it->istart += nbIndices;
			it->vstart += nbVertices;
			geosets.push_back(it);
		}

		// build bone correspondence table
		uint32 nbBonesInNewModel = modelsIt->header.nBones;
		int16* boneConvertTable = new int16[nbBonesInNewModel];

		for (uint i = 0; i < nbBonesInNewModel; ++i)
			boneConvertTable[i] = i;

		for (uint i = 0; i < nbBonesInNewModel; ++i)
		{
			glm::vec3 pivot = modelsIt->bones[i].pivot;
			for (uint b = 0; b < bones.size(); ++b)
			{
				if (glm::all(glm::epsilonEqual(bones[b].pivot, pivot, glm::vec3(0.0001f))) &&
					(bones[b].boneDef.unknown == modelsIt->bones[i].boneDef.unknown))
				{
					boneConvertTable[i] = b;
					break;
				}
			}
		}

#ifdef DEBUG_DH_SUPPORT
    for (uint i = 0; i < nbBonesInNewModel; ++i)
      LOG_INFO << i << "=>" << boneConvertTable[i];
#endif

		// change bone from new model to character one
		for (auto& it : modelsIt->origVertices)
		{
			for (uint i = 0; i < 4; ++i)
			{
				if (it.weights[i] > 0)
					it.bones[i] = boneConvertTable[it.bones[i]];
			}
		}

		delete[] boneConvertTable;

		origVertices.reserve(origVertices.size() + modelsIt->origVertices.size());
		origVertices.insert(origVertices.end(), modelsIt->origVertices.begin(), modelsIt->origVertices.end());

		indices.reserve(indices.size() + modelsIt->indices.size());

		for (const auto& it : modelsIt->indices)
			indices.push_back(it + nbVertices);

		// retrieve tex id associated to model hands (needed for DH)
		uint16 handTex = ModelRenderPass::INVALID_TEX;
		for (const auto it : passes)
		{
			if (geosets[it->geoIndex]->id / 100 == 23)
				handTex = it->tex;
		}

		for (const auto it : modelsIt->passes)
		{
			ModelRenderPass* p = new ModelRenderPass(*it);
			p->model = this;
			p->geoIndex += nbGeosets;
			if (geosets[it->geoIndex]->id / 100 != 23) // don't copy texture for hands
				p->tex += (mergeIndex * TEXTURE_MAX);
			else
				p->tex = handTex; // use regular model texture instead

			passes.push_back(p);
		}

#ifdef DEBUG_DH_SUPPORT
    LOG_INFO << "---- FINAL ----";
    LOG_INFO << "nbGeosets =" << geosets.size();
    LOG_INFO << "nbVertices =" << origVertices.size();
    LOG_INFO << "nbIndices =" << indices.size();
    LOG_INFO << "nbPasses =" << passes.size();
#endif

		// add model textures
		for (const auto it : modelsIt->textures)
		{
			if (it != ModelRenderPass::INVALID_TEX)
				textures.push_back(TEXTUREMANAGER.add(GAMEDIRECTORY.getFile(TEXTUREMANAGER.get(it))));
			else
				textures.push_back(ModelRenderPass::INVALID_TEX);
		}

		for (auto it : modelsIt->specialTextures)
		{
			if (it == -1 || it == TEXTURE_SKIN_EXTRA) // if texture type is TEXTURE_SKIN_EXTRA use parent texture
				specialTextures.push_back(it);
			else
				specialTextures.push_back(it + (mergeIndex * TEXTURE_MAX));
		}

		for (auto it : modelsIt->replaceTextures)
			replaceTextures.push_back(it);
	}

	delete[] vertices;
	delete[] normals;

	vertices = new glm::vec3[origVertices.size()];
	normals = new glm::vec3[origVertices.size()];

	uint i = 0;
	for (auto& ov_it : origVertices)
	{
		// Set the data for our vertices, normals from the model data
		vertices[i] = ov_it.pos;
		normals[i] = glm::normalize(ov_it.normal);
		++i;
	}

	const size_t size = (origVertices.size() * sizeof(float));
	vbufsize = (3 * size); // we multiple by 3 for the x, y, z positions of the vertex

	if (video.supportVBO)
	{
		glDeleteBuffersARB(1, &nbuf);
		glDeleteBuffersARB(1, &vbuf);

		// Vert buffer
		glGenBuffersARB(1, &vbuf);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, vertices, GL_STATIC_DRAW_ARB);
		delete[] vertices;
		vertices = nullptr;

		// normals buffer
		glGenBuffersARB(1, &nbuf);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, nbuf);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, normals, GL_STATIC_DRAW_ARB);
		delete[] normals;
		normals = nullptr;

		// clean bind
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	}
}

void WoWModel::unmergeModel(QString& name)
{
	LOG_INFO << __FUNCTION__ << name;
	const auto it = std::find_if(std::begin(mergedModels),
	                             std::end(mergedModels),
	                             [&](const WoWModel* m) { return m->gamefile->fullname() == name.replace("\\", "/"); });

	if (it != mergedModels.end())
	{
		WoWModel* m = *it;
		unmergeModel(m);
		delete m;
	}
}

void WoWModel::unmergeModel(uint fileID)
{
	LOG_INFO << __FUNCTION__ << fileID;
	const auto it = std::find_if(std::begin(mergedModels),
	                             std::end(mergedModels),
	                             [&](const WoWModel* m) { return m->gamefile->fileDataId() == fileID; });

	if (it != mergedModels.end())
	{
		WoWModel* m = *it;
		unmergeModel(m);
		delete m;
	}
}

void WoWModel::unmergeModel(WoWModel* m)
{
	LOG_INFO << __FUNCTION__ << m->name();
	mergedModels.erase(m);
	refreshMerging();
}

void WoWModel::refresh()
{
	// apply chardetails customization
	cd.refresh();

	// if no race info found, simply update geosets
	if (infos.raceID == -1)
		return;

	const auto headItemId = getItemId(CS_HEAD);

	if (headItemId != -1 && cd.autoHideGeosetsForHeadItems)
	{
		const auto query = QString("SELECT HideGeosetGroup FROM HelmetGeosetData WHERE HelmetGeosetData.RaceID = %1 "
			                   "AND HelmetGeosetData.HelmetGeosetVisDataID = (SELECT %2 FROM ItemDisplayInfo WHERE ItemDisplayInfo.ID = "
			                   "(SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = (SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %3)))")
		                   .arg(infos.raceID)
		                   .arg((infos.sexID == 0) ? "HelmetGeosetVis1" : "HelmetGeosetVis2")
		                   .arg(headItemId);

		const auto helmetInfos = GAMEDATABASE.sqlQuery(query);

		if (helmetInfos.valid && !helmetInfos.values.empty())
		{
			for (auto it : helmetInfos.values)
			{
				setGeosetGroupDisplay(static_cast<CharGeosets>(it[0].toInt()), 0);
			}
		}
	}

	// reset char texture
	tex.reset(infos.textureLayoutID);
	for (const auto t : cd.textures)
	{
		if (t.type != 1)
		{
			updateTextureList(GAMEDIRECTORY.getFile(t.fileId), t.type);
		}
		else
		{
			tex.addLayer(GAMEDIRECTORY.getFile(t.fileId), t.region, t.layer, t.blendMode);
		}
	}

	//refresh equipment
	for (auto* it : *this)
		it->refresh();

	LOG_INFO << "Current Equipment :"
		<< "Head" << getItemId(CS_HEAD)
		<< "Shoulder" << getItemId(CS_SHOULDER)
		<< "Shirt" << getItemId(CS_SHIRT)
		<< "Chest" << getItemId(CS_CHEST)
		<< "Belt" << getItemId(CS_BELT)
		<< "Legs" << getItemId(CS_PANTS)
		<< "Boots" << getItemId(CS_BOOTS)
		<< "Bracers" << getItemId(CS_BRACERS)
		<< "Gloves" << getItemId(CS_GLOVES)
		<< "Cape" << getItemId(CS_CAPE)
		<< "Right Hand" << getItemId(CS_HAND_RIGHT)
		<< "Left Hand" << getItemId(CS_HAND_LEFT)
		<< "Quiver" << getItemId(CS_QUIVER)
		<< "Tabard" << getItemId(CS_TABARD);

	// gloves - this is so gloves have preference over shirt sleeves.
	if (cd.geosets[CG_GLOVES] > 1)
		cd.geosets[CG_SLEEVES] = 0;

	// If model is one of these races, show the feet (don't wear boots)
	cd.showFeet = infos.barefeet;

	// Reset geosets
	for (const auto geo : cd.geosets)
		setGeosetGroupDisplay(static_cast<CharGeosets>(geo.first), geo.second);

	// finalize character texture
	const GLuint charTex = 0;
	tex.compose(charTex);

	// set replacable textures
	replaceTextures[TEXTURE_SKIN] = charTex;

	// Eye Glow Geosets are ID 1701, 1702, etc.
	const size_t egt = cd.eyeGlowType;
	const int egtId = CG_EYEGLOW * 100 + egt + 1; // CG_EYEGLOW = 17
	for (size_t i = 0; i < rawGeosets.size(); i++)
	{
		const int id = geosets[i]->id;
		if ((int)(id / 100) == CG_EYEGLOW) // geosets 1700..1799
			showGeoset(i, (id == egtId));
	}

	// refresh merged models
	refreshMerging();
}

QString WoWModel::getNameForTex(uint16 Tex)
{
	if (specialTextures[Tex] == TEXTURE_SKIN)
		return "Body.blp";
	else
		return TEXTUREMANAGER.get(getGLTexture(Tex));
}

GLuint WoWModel::getGLTexture(uint16 Tex) const
{
	if (Tex >= specialTextures.size())
		return ModelRenderPass::INVALID_TEX;

	if (specialTextures[Tex] == -1)
		return textures[Tex];
	else
		return replaceTextures[specialTextures[Tex]];
}

void WoWModel::restoreRawGeosets()
{
	std::vector<bool> geosetDisplayStatus;

	for (const auto it : geosets)
	{
		geosetDisplayStatus.push_back(it->display);
		delete it;
	}

	geosets.clear();

	for (const auto it : rawGeosets)
	{
		ModelGeosetHD* geo = new ModelGeosetHD(*it);
		geosets.push_back(geo);
	}

	uint i = 0;
	for (const auto it : geosetDisplayStatus)
	{
		geosets[i]->display = it;
		i++;
	}
}

void WoWModel::hideAllGeosets()
{
	for (uint i = 0; i < geosets.size(); i++)
		showGeoset(i, false);
}

std::ostream& operator<<(std::ostream& out, const WoWModel& m)
{
	out << "<m2>" << endl;
	out << "  <info>" << endl;
	out << "    <modelname>" << m.modelname.c_str() << "</modelname>" << endl;
	out << "  </info>" << endl;
	out << "  <header>" << endl;
	//  out << "    <id>" << m.header.id << "</id>" << endl;
	out << "    <nameLength>" << m.header.nameLength << "</nameLength>" << endl;
	out << "    <nameOfs>" << m.header.nameOfs << "</nameOfs>" << endl;
	//  out << "    <name>" << f.getBuffer()+m.header.nameOfs << "</name>" << endl; // @TODO
	out << "    <GlobalModelFlags>" << m.header.GlobalModelFlags << "</GlobalModelFlags>" << endl;
	out << "    <nGlobalSequences>" << m.header.nGlobalSequences << "</nGlobalSequences>" << endl;
	out << "    <ofsGlobalSequences>" << m.header.ofsGlobalSequences << "</ofsGlobalSequences>" << endl;
	out << "    <nAnimations>" << m.header.nAnimations << "</nAnimations>" << endl;
	out << "    <ofsAnimations>" << m.header.ofsAnimations << "</ofsAnimations>" << endl;
	out << "    <nAnimationLookup>" << m.header.nAnimationLookup << "</nAnimationLookup>" << endl;
	out << "    <ofsAnimationLookup>" << m.header.ofsAnimationLookup << "</ofsAnimationLookup>" << endl;
	out << "    <nBones>" << m.header.nBones << "</nBones>" << endl;
	out << "    <ofsBones>" << m.header.ofsBones << "</ofsBones>" << endl;
	out << "    <nKeyBoneLookup>" << m.header.nKeyBoneLookup << "</nKeyBoneLookup>" << endl;
	out << "    <ofsKeyBoneLookup>" << m.header.ofsKeyBoneLookup << "</ofsKeyBoneLookup>" << endl;
	out << "    <nVertices>" << m.header.nVertices << "</nVertices>" << endl;
	out << "    <ofsVertices>" << m.header.ofsVertices << "</ofsVertices>" << endl;
	out << "    <nViews>" << m.header.nViews << "</nViews>" << endl;
	out << "    <lodname>" << m.lodname.c_str() << "</lodname>" << endl;
	out << "    <nColors>" << m.header.nColors << "</nColors>" << endl;
	out << "    <ofsColors>" << m.header.ofsColors << "</ofsColors>" << endl;
	out << "    <nTextures>" << m.header.nTextures << "</nTextures>" << endl;
	out << "    <ofsTextures>" << m.header.ofsTextures << "</ofsTextures>" << endl;
	out << "    <nTransparency>" << m.header.nTransparency << "</nTransparency>" << endl;
	out << "    <ofsTransparency>" << m.header.ofsTransparency << "</ofsTransparency>" << endl;
	out << "    <nTexAnims>" << m.header.nTexAnims << "</nTexAnims>" << endl;
	out << "    <ofsTexAnims>" << m.header.ofsTexAnims << "</ofsTexAnims>" << endl;
	out << "    <nTexReplace>" << m.header.nTexReplace << "</nTexReplace>" << endl;
	out << "    <ofsTexReplace>" << m.header.ofsTexReplace << "</ofsTexReplace>" << endl;
	out << "    <nTexFlags>" << m.header.nTexFlags << "</nTexFlags>" << endl;
	out << "    <ofsTexFlags>" << m.header.ofsTexFlags << "</ofsTexFlags>" << endl;
	out << "    <nBoneLookup>" << m.header.nBoneLookup << "</nBoneLookup>" << endl;
	out << "    <ofsBoneLookup>" << m.header.ofsBoneLookup << "</ofsBoneLookup>" << endl;
	out << "    <nTexLookup>" << m.header.nTexLookup << "</nTexLookup>" << endl;
	out << "    <ofsTexLookup>" << m.header.ofsTexLookup << "</ofsTexLookup>" << endl;
	out << "    <nTexUnitLookup>" << m.header.nTexUnitLookup << "</nTexUnitLookup>" << endl;
	out << "    <ofsTexUnitLookup>" << m.header.ofsTexUnitLookup << "</ofsTexUnitLookup>" << endl;
	out << "    <nTransparencyLookup>" << m.header.nTransparencyLookup << "</nTransparencyLookup>" << endl;
	out << "    <ofsTransparencyLookup>" << m.header.ofsTransparencyLookup << "</ofsTransparencyLookup>" << endl;
	out << "    <nTexAnimLookup>" << m.header.nTexAnimLookup << "</nTexAnimLookup>" << endl;
	out << "    <ofsTexAnimLookup>" << m.header.ofsTexAnimLookup << "</ofsTexAnimLookup>" << endl;
	out << "    <collisionSphere>" << endl;
	out << "      <min>" << m.header.collisionSphere.min.x << " " << m.header.collisionSphere.min.y << " " << m.header.
		collisionSphere.min.z << "</min>" << endl;
	out << "      <max>" << m.header.collisionSphere.max.x << " " << m.header.collisionSphere.max.y << " " << m.header.
		collisionSphere.max.z << "</max>" << endl;
	out << "      <radius>" << m.header.collisionSphere.radius << "</radius>" << endl;
	out << "    </collisionSphere>" << endl;
	out << "    <boundSphere>" << endl;
	out << "      <min>" << m.header.boundSphere.min.x << " " << m.header.boundSphere.min.y << " " << m.header.
		boundSphere.min.z << "</min>" << endl;
	out << "      <min>" << m.header.boundSphere.max.x << " " << m.header.boundSphere.max.y << " " << m.header.
		boundSphere.max.z << "</min>" << endl;
	out << "      <radius>" << m.header.boundSphere.radius << "</radius>" << endl;
	out << "    </boundSphere>" << endl;
	out << "    <nBoundingTriangles>" << m.header.nBoundingTriangles << "</nBoundingTriangles>" << endl;
	out << "    <ofsBoundingTriangles>" << m.header.ofsBoundingTriangles << "</ofsBoundingTriangles>" << endl;
	out << "    <nBoundingVertices>" << m.header.nBoundingVertices << "</nBoundingVertices>" << endl;
	out << "    <ofsBoundingVertices>" << m.header.ofsBoundingVertices << "</ofsBoundingVertices>" << endl;
	out << "    <nBoundingNormals>" << m.header.nBoundingNormals << "</nBoundingNormals>" << endl;
	out << "    <ofsBoundingNormals>" << m.header.ofsBoundingNormals << "</ofsBoundingNormals>" << endl;
	out << "    <nAttachments>" << m.header.nAttachments << "</nAttachments>" << endl;
	out << "    <ofsAttachments>" << m.header.ofsAttachments << "</ofsAttachments>" << endl;
	out << "    <nAttachLookup>" << m.header.nAttachLookup << "</nAttachLookup>" << endl;
	out << "    <ofsAttachLookup>" << m.header.ofsAttachLookup << "</ofsAttachLookup>" << endl;
	out << "    <nEvents>" << m.header.nEvents << "</nEvents>" << endl;
	out << "    <ofsEvents>" << m.header.ofsEvents << "</ofsEvents>" << endl;
	out << "    <nLights>" << m.header.nLights << "</nLights>" << endl;
	out << "    <ofsLights>" << m.header.ofsLights << "</ofsLights>" << endl;
	out << "    <nCameras>" << m.header.nCameras << "</nCameras>" << endl;
	out << "    <ofsCameras>" << m.header.ofsCameras << "</ofsCameras>" << endl;
	out << "    <nCameraLookup>" << m.header.nCameraLookup << "</nCameraLookup>" << endl;
	out << "    <ofsCameraLookup>" << m.header.ofsCameraLookup << "</ofsCameraLookup>" << endl;
	out << "    <nRibbonEmitters>" << m.header.nRibbonEmitters << "</nRibbonEmitters>" << endl;
	out << "    <ofsRibbonEmitters>" << m.header.ofsRibbonEmitters << "</ofsRibbonEmitters>" << endl;
	out << "    <nParticleEmitters>" << m.header.nParticleEmitters << "</nParticleEmitters>" << endl;
	out << "    <ofsParticleEmitters>" << m.header.ofsParticleEmitters << "</ofsParticleEmitters>" << endl;
	out << "  </header>" << endl;

	out << "  <SkeletonAndAnimation>" << endl;

	out << "  <GlobalSequences size=\"" << m.globalSequences.size() << "\">" << endl;
	for (const unsigned int globalSequence : m.globalSequences)
		out << "<Sequence>" << globalSequence << "</Sequence>" << endl;
	out << "  </GlobalSequences>" << endl;

	out << "  <Animations size=\"" << m.anims.size() << "\">" << endl;
	for (size_t i = 0; i < m.anims.size(); i++)
	{
		out << "    <Animation id=\"" << i << "\">" << endl;
		out << "      <animID>" << m.anims[i].animID << "</animID>" << endl;
		std::string strName;
		QString query = QString("SELECT Name FROM AnimationData WHERE ID = %1").arg(m.anims[i].animID);
		sqlResult anim = GAMEDATABASE.sqlQuery(query);
		if (anim.valid && !anim.empty())
			strName = anim.values[0][0].toStdString();
		else
			strName = "???";
		out << "      <animName>" << strName << "</animName>" << endl;
		out << "      <length>" << m.anims[i].length << "</length>" << endl;
		out << "      <moveSpeed>" << m.anims[i].moveSpeed << "</moveSpeed>" << endl;
		out << "      <flags>" << m.anims[i].flags << "</flags>" << endl;
		out << "      <probability>" << m.anims[i].probability << "</probability>" << endl;
		out << "      <d1>" << m.anims[i].d1 << "</d1>" << endl;
		out << "      <d2>" << m.anims[i].d2 << "</d2>" << endl;
		out << "      <playSpeed>" << m.anims[i].playSpeed << "</playSpeed>" << endl;
		out << "      <boxA>" << m.anims[i].boundSphere.min.x << " " << m.anims[i].boundSphere.min.y << " " << m.anims[
			i].boundSphere.min.z << "</boxA>" << endl;
		out << "      <boxA>" << m.anims[i].boundSphere.max.x << " " << m.anims[i].boundSphere.max.y << " " << m.anims[
			i].boundSphere.max.z << "</boxA>" << endl;
		out << "      <rad>" << m.anims[i].boundSphere.radius << "</rad>" << endl;
		out << "      <NextAnimation>" << m.anims[i].NextAnimation << "</NextAnimation>" << endl;
		out << "      <Index>" << m.anims[i].Index << "</Index>" << endl;
		out << "    </Animation>" << endl;
	}
	out << "  </Animations>" << endl;

	out << "  <AnimationLookups size=\"" << m.animLookups.size() << "\">" << endl;
	for (size_t i = 0; i < m.animLookups.size(); i++)
		out << "    <AnimationLookup id=\"" << i << "\">" << m.animLookups[i] << "</AnimationLookup>" << endl;
	out << "  </AnimationLookups>" << endl;

	out << "  <Bones size=\"" << m.bones.size() << "\">" << endl;
	for (size_t i = 0; i < m.bones.size(); i++)
	{
		out << "    <Bone id=\"" << i << "\">" << endl;
		out << "      <keyboneid>" << m.bones[i].boneDef.keyboneid << "</keyboneid>" << endl;
		out << "      <billboard>" << m.bones[i].billboard << "</billboard>" << endl;
		out << "      <parent>" << m.bones[i].boneDef.parent << "</parent>" << endl;
		out << "      <geoid>" << m.bones[i].boneDef.geoid << "</geoid>" << endl;
		out << "      <unknown>" << m.bones[i].boneDef.unknown << "</unknown>" << endl;
#if 1 // too huge
		// AB translation
		out << "      <trans>" << endl;
		out << m.bones[i].trans;
		out << "      </trans>" << endl;
		// AB rotation
		out << "      <rot>" << endl;
		out << m.bones[i].rot;
		out << "      </rot>" << endl;
		// AB scaling
		out << "      <scale>" << endl;
		out << m.bones[i].scale;
		out << "      </scale>" << endl;
#endif
		out << "      <pivot>" << m.bones[i].boneDef.pivot.x << " " << m.bones[i].boneDef.pivot.y << " " << m.bones[i].
			boneDef.pivot.z << "</pivot>" << endl;
		out << "    </Bone>" << endl;
	}
	out << "  </Bones>" << endl;

	//  out << "  <BoneLookups size=\"" << m.header.nBoneLookup << "\">" << endl;
	//  uint16 *boneLookup = (uint16 *)(f.getBuffer() + m.header.ofsBoneLookup);
	//  for(size_t i=0; i<m.header.nBoneLookup; i++) {
	//    out << "    <BoneLookup id=\"" << i << "\">" << boneLookup[i] << "</BoneLookup>" << endl;
	//  }
	//  out << "  </BoneLookups>" << endl;

	out << "  <KeyBoneLookups size=\"" << m.header.nKeyBoneLookup << "\">" << endl;
	for (size_t i = 0; i < m.header.nKeyBoneLookup; i++)
		out << "    <KeyBoneLookup id=\"" << i << "\">" << m.keyBoneLookup[i] << "</KeyBoneLookup>" << endl;
	out << "  </KeyBoneLookups>" << endl;

	out << "  </SkeletonAndAnimation>" << endl;

	out << "  <GeometryAndRendering>" << endl;

	//  out << "  <Vertices size=\"" << m.header.nVertices << "\">" << endl;
	//  ModelVertex *verts = (ModelVertex*)(f.getBuffer() + m.header.ofsVertices);
	//  for(uint32 i=0; i<m.header.nVertices; i++) {
	//    out << "    <Vertice id=\"" << i << "\">" << endl;
	//    out << "      <pos>" << verts[i].pos << "</pos>" << endl; // TODO
	//    out << "    </Vertice>" << endl;
	//  }
	out << "  </Vertices>" << endl; // TODO
	out << "  <Views>" << endl;

	//  out << "  <Indices size=\"" << view->nIndex << "\">" << endl;
	//  out << "  </Indices>" << endl; // TODO
	//  out << "  <Triangles size=\""<< view->nTris << "\">" << endl;
	//  out << "  </Triangles>" << endl; // TODO
	//  out << "  <Properties size=\"" << view->nProps << "\">" << endl;
	//  out << "  </Properties>" << endl; // TODO
	//  out << "  <Subs size=\"" << view->nSub << "\">" << endl;
	//  out << "  </Subs>" << endl; // TODO

	out << "  <RenderPasses size=\"" << m.passes.size() << "\">" << endl;
	for (size_t i = 0; i < m.passes.size(); i++)
	{
		out << "    <RenderPass id=\"" << i << "\">" << endl;
		const ModelRenderPass* p = m.passes[i];
		const ModelGeosetHD* geoset = m.geosets[p->geoIndex];
		out << "      <indexStart>" << geoset->istart << "</indexStart>" << endl;
		out << "      <indexCount>" << geoset->icount << "</indexCount>" << endl;
		out << "      <vertexStart>" << geoset->vstart << "</vertexStart>" << endl;
		out << "      <vertexEnd>" << geoset->vstart + geoset->vcount << "</vertexEnd>" << endl;
		out << "      <tex>" << p->tex << "</tex>" << endl;
		if (p->tex >= 0)
			out << "      <texName>" << TEXTUREMANAGER.get(p->tex).toStdString() << "</texName>" << endl;
		out << "      <useTex2>" << p->useTex2 << "</useTex2>" << endl;
		out << "      <useEnvMap>" << p->useEnvMap << "</useEnvMap>" << endl;
		out << "      <cull>" << p->cull << "</cull>" << endl;
		out << "      <trans>" << p->trans << "</trans>" << endl;
		out << "      <unlit>" << p->unlit << "</unlit>" << endl;
		out << "      <noZWrite>" << p->noZWrite << "</noZWrite>" << endl;
		out << "      <billboard>" << p->billboard << "</billboard>" << endl;
		out << "      <texanim>" << p->texanim << "</texanim>" << endl;
		out << "      <color>" << p->color << "</color>" << endl;
		out << "      <opacity>" << p->opacity << "</opacity>" << endl;
		out << "      <blendmode>" << p->blendmode << "</blendmode>" << endl;
		out << "      <geoset>" << geoset->id << "</geoset>" << endl;
		out << "      <swrap>" << p->swrap << "</swrap>" << endl;
		out << "      <twrap>" << p->twrap << "</twrap>" << endl;
		out << "      <ocol>" << p->ocol.x << " " << p->ocol.y << " " << p->ocol.z << " " << p->ocol.w << "</ocol>" <<
			endl;
		out << "      <ecol>" << p->ecol.x << " " << p->ecol.y << " " << p->ecol.z << " " << p->ecol.w << "</ecol>" <<
			endl;
		out << "    </RenderPass>" << endl;
	}
	out << "  </RenderPasses>" << endl;

	out << "  <Geosets size=\"" << m.geosets.size() << "\">" << endl;
	for (size_t i = 0; i < m.geosets.size(); i++)
	{
		out << "    <Geoset id=\"" << i << "\">" << endl;
		out << "      <id>" << m.geosets[i]->id << "</id>" << endl;
		out << "      <vstart>" << m.geosets[i]->vstart << "</vstart>" << endl;
		out << "      <vcount>" << m.geosets[i]->vcount << "</vcount>" << endl;
		out << "      <istart>" << m.geosets[i]->istart << "</istart>" << endl;
		out << "      <icount>" << m.geosets[i]->icount << "</icount>" << endl;
		out << "      <nSkinnedBones>" << m.geosets[i]->nSkinnedBones << "</nSkinnedBones>" << endl;
		out << "      <StartBones>" << m.geosets[i]->StartBones << "</StartBones>" << endl;
		out << "      <rootBone>" << m.geosets[i]->rootBone << "</rootBone>" << endl;
		out << "      <nBones>" << m.geosets[i]->nBones << "</nBones>" << endl;
		out << "      <BoundingBox>" << m.geosets[i]->BoundingBox[0].x << " " << m.geosets[i]->BoundingBox[0].y << " "
			<< m.geosets[i]->BoundingBox[0].z << "</BoundingBox>" << endl;
		out << "      <BoundingBox>" << m.geosets[i]->BoundingBox[1].x << " " << m.geosets[i]->BoundingBox[1].y << " "
			<< m.geosets[i]->BoundingBox[1].z << "</BoundingBox>" << endl;
		out << "      <radius>" << m.geosets[i]->radius << "</radius>" << endl;
		out << "    </Geoset>" << endl;
	}
	out << "  </Geosets>" << endl;

	//  ModelTexUnit *tex = (ModelTexUnit*)(g.getBuffer() + view->ofsTex);
	//  out << "  <TexUnits size=\"" << view->nTex << "\">" << endl;
	//  for (size_t i=0; i<view->nTex; i++) {
	//    out << "    <TexUnit id=\"" << i << "\">" << endl;
	//    out << "      <flags>" << tex[i].flags << "</flags>" << endl;
	//    out << "      <shading>" << tex[i].shading << "</shading>" << endl;
	//    out << "      <op>" << tex[i].op << "</op>" << endl;
	//    out << "      <op2>" << tex[i].op2 << "</op2>" << endl;
	//    out << "      <colorIndex>" << tex[i].colorIndex << "</colorIndex>" << endl;
	//    out << "      <flagsIndex>" << tex[i].flagsIndex << "</flagsIndex>" << endl;
	//    out << "      <texunit>" << tex[i].texunit << "</texunit>" << endl;
	//    out << "      <mode>" << tex[i].mode << "</mode>" << endl;
	//    out << "      <textureid>" << tex[i].textureid << "</textureid>" << endl;
	//    out << "      <texunit2>" << tex[i].texunit2 << "</texunit2>" << endl;
	//    out << "      <transid>" << tex[i].transid << "</transid>" << endl;
	//    out << "      <texanimid>" << tex[i].texanimid << "</texanimid>" << endl;
	//    out << "    </TexUnit>" << endl;
	//  }
	//  out << "  </TexUnits>" << endl;

	out << "  </Views>" << endl;

	out << "  <RenderFlags></RenderFlags>" << endl;

	out << "  <Colors size=\"" << m.colors.size() << "\">" << endl;
	for (uint i = 0; i < m.colors.size(); i++)
	{
		out << "    <Color id=\"" << i << "\">" << endl;
		// AB color
		out << "    <color>" << endl;
		out << m.colors[i].color;
		out << "    </color>" << endl;
		// AB opacity
		out << "    <opacity>" << endl;
		out << m.colors[i].opacity;
		out << "    </opacity>" << endl;
		out << "    </Color>" << endl;
	}
	out << "  </Colors>" << endl;

	out << "  <Transparency size=\"" << m.transparency.size() << "\">" << endl;
	for (uint i = 0; i < m.transparency.size(); i++)
	{
		out << "    <Tran id=\"" << i << "\">" << endl;
		// AB trans
		out << "    <trans>" << endl;
		out << m.transparency[i].trans;
		out << "    </trans>" << endl;
		out << "    </Tran>" << endl;
	}
	out << "  </Transparency>" << endl;

	out << "  <TransparencyLookup></TransparencyLookup>" << endl;

	//  ModelTextureDef *texdef = (ModelTextureDef*)(f.getBuffer() + m.header.ofsTextures);
	//  out << "  <Textures size=\"" << m.header.nTextures << "\">" << endl;
	//  for(size_t i=0; i<m.header.nTextures; i++) {
	//    out << "    <Texture id=\"" << i << "\">" << endl;
	//    out << "      <type>" << texdef[i].type << "</type>" << endl;
	//    out << "      <flags>" << texdef[i].flags << "</flags>" << endl;
	//    //out << "      <nameLen>" << texdef[i].nameLen << "</nameLen>" << endl;
	//    //out << "      <nameOfs>" << texdef[i].nameOfs << "</nameOfs>" << endl;
	//    if (texdef[i].type == TEXTURE_FILENAME)
	//      out << "    <name>" << f.getBuffer()+texdef[i].nameOfs  << "</name>" << endl;
	//    out << "    </Texture>" << endl;
	//  }
	//  out << "  </Textures>" << endl;

	//  out << "  <TexLookups size=\"" << m.header.nTexLookup << "\">" << endl;
	//  uint16 *texLookup = (uint16 *)(f.getBuffer() + m.header.ofsTexLookup);
	//  for(size_t i=0; i<m.header.nTexLookup; i++) {
	//    out << "    <TexLookup id=\"" << i << "\">" << texLookup[i] << "</TexLookup>" << endl;
	//  }
	//  out << "  </TexLookups>" << endl;

	out << "  <ReplacableTextureLookup></ReplacableTextureLookup>" << endl;

	out << "  </GeometryAndRendering>" << endl;

	out << "  <Effects>" << endl;

	out << "  <TexAnims size=\"" << m.texAnims.size() << "\">" << endl;
	for (uint i = 0; i < m.texAnims.size(); i++)
	{
		out << "    <TexAnim id=\"" << i << "\">" << endl;
		// AB trans
		out << "    <trans>" << endl;
		out << m.texAnims[i].trans;
		out << "    </trans>" << endl;
		// AB rot
		out << "    <rot>" << endl;
		out << m.texAnims[i].rot;
		out << "    </rot>" << endl;
		// AB scale
		out << "    <scale>" << endl;
		out << m.texAnims[i].scale;
		out << "    </scale>" << endl;
		out << "    </TexAnim>" << endl;
	}
	out << "  </TexAnims>" << endl;

	out << "  <RibbonEmitters></RibbonEmitters>" << endl; // TODO

	out << "  <Particles size=\"" << m.header.nParticleEmitters << "\">" << endl;
	for (size_t i = 0; i < m.particleSystems.size(); i++)
	{
		out << "    <Particle id=\"" << i << "\">" << endl;
		out << m.particleSystems[i];
		out << "    </Particle>" << endl;
	}
	out << "  </Particles>" << endl;

	out << "  </Effects>" << endl;

	out << "  <Miscellaneous>" << endl;

	out << "  <BoundingVolumes></BoundingVolumes>" << endl;
	out << "  <Lights></Lights>" << endl;
	out << "  <Cameras></Cameras>" << endl;

	out << "  <Attachments size=\"" << m.header.nAttachments << "\">" << endl;
	for (size_t i = 0; i < m.header.nAttachments; i++)
	{
		out << "    <Attachment id=\"" << i << "\">" << endl;
		out << "      <id>" << m.atts[i].id << "</id>" << endl;
		out << "      <bone>" << m.atts[i].bone << "</bone>" << endl;
		out << "      <pos>" << m.atts[i].pos.x << " " << m.atts[i].pos.y << " " << m.atts[i].pos.z << "</pos>" << endl;
		out << "    </Attachment>" << endl;
	}
	out << "  </Attachments>" << endl;

	out << "  <AttachLookups size=\"" << m.header.nAttachLookup << "\">" << endl;
	//  int16 *attachLookup = (int16 *)(f.getBuffer() + m.header.ofsAttachLookup);
	//  for(size_t i=0; i<m.header.nAttachLookup; i++) {
	//    out << "    <AttachLookup id=\"" << i << "\">" << attachLookup[i] << "</AttachLookup>" << endl;
	//  }
	//  out << "  </AttachLookups>" << endl;

	out << "  <Events size=\"" << m.events.size() << "\">" << endl;
	for (size_t i = 0; i < m.events.size(); i++)
	{
		out << "    <Event id=\"" << i << "\">" << endl;
		out << m.events[i];
		out << "    </Event>" << endl;
	}
	out << "  </Events>" << endl;

	out << "  </Miscellaneous>" << endl;

	//  out << "    <>" << m.header. << "</>" << endl;
	out << "  <TextureLists>" << endl;
	for (const auto it : m.passes)
	{
		const GLuint tex = m.getGLTexture(it->tex);
		if (tex != ModelRenderPass::INVALID_TEX)
			out << "    <TextureList id=\"" << tex << "\">" << TEXTUREMANAGER.get(tex).toStdString() << "</TextureList>"
				<< endl;
	}
	out << "  </TextureLists>" << endl;

	out << "</m2>" << endl;

	return out;
}
