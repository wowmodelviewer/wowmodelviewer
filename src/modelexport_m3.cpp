#include "Bone.h"
#include "globalvars.h"
#include "modelexport.h"
#include "modelexport_m3.h"
#include "modelcanvas.h"

#define	ROOT_BONE	(1)

enum M3_Class {
	AR_Default,
	AR_Bone,
	AR_MSEC,
	AR_Mat,
	AR_Layer,
	AR_Par
};

static wxString M3_Attach_Names[] = {
	wxT("Ref_Hardpoint"),	// 0
	wxT("Ref_Weapon Right"),	// Right Palm
	wxT("Ref_Weapon Left"),	// Left Palm
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),	// 5
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),	// 10
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Target"),		// 15, Front Hit Region
	wxT("Ref_Target"), 		// Rear Hit Region
	wxT("Ref_Hardpoint"), 
	wxT("Ref_Head"),			// Head Region
	wxT("Ref_Origin"),		// Base
	wxT("Ref_Overhead"),		// 20, Above
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),	// 25
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),	// 30
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Hardpoint"),
	wxT("Ref_Center"),		// Spell Impact
	wxT("Ref_Hardpoint"),	// 35
};

static std::vector<ReferenceEntry> reList;

typedef struct {
	uint16 texid;
	uint16 flags;
	uint16 blend;
	int16  animid;
	int16  color;
	int16  eye;
} MATmap;

typedef struct {
	uint16 regnIndex;
	uint16 matmIndex;
} MeshMap;

typedef	struct {
	int16 index;
	int16 type;
} AnimOffset;

typedef struct {
	int					timeline;
	EVNT				data;
	wxString			name;
} Anim_Event;

typedef struct {
	std::vector <int>	timeline;
	std::vector <float>	data;
} Anim_Float;

typedef struct {
	std::vector <int>	timeline;
	std::vector <Vec2D>	data;
} Anim_Vec2D;

typedef struct {
	std::vector <int>	timeline;
	std::vector <Vec3D>	data;
} Anim_Vec3D;

typedef struct {
	std::vector <int>	timeline;
	std::vector <Vec4D>	data;
} Anim_Vec4D;

typedef	struct {
	std::vector <uint32>		animid;
	std::vector <AnimOffset>	animoff;
	Anim_Event					animevent;
	std::vector <Anim_Float>	animfloat;
	std::vector <Anim_Vec2D>	animvec2d;
	std::vector <Anim_Vec3D>	animvec3d;
	std::vector <Anim_Vec4D>	animvec4d;
	SD							animsdevent;
	std::vector <SD>			animsdfloat;
	std::vector <SD>			animsdvec2d;
	std::vector <SD>			animsdvec3d;
	std::vector <SD>			animsdvec4d;
	wxString					animname;
} STCExtra;

typedef	struct {
	std::vector <LAYR>	layers;
	wxArrayString		names;
} MATExtra;

uint32 CreateAnimID(int m3class, int key, int subkey, int idx)
{
	return (m3class & 0xFF) << 24 | (key & 0xFF) << 16 | (subkey & 0xFF) << 8 | (idx & 0xFF);
}

void SetAnimed(AnimationReference &anim)
{
	anim.flags = 1;
	anim.animflag = 6;
}

Vec3D fixCoord(Vec3D v)
{
	return Vec3D(v.y, -v.x, v.z);
}

void padding(wxFFile &f, int pads=16)
{
	char pad=0xAA;
	if (f.Tell()%pads != 0) {
		int j=pads-(f.Tell()%pads);
		for(int i=0; i<j; i++) {
			f.Write(&pad, sizeof(pad));
		}
	}
}

void RefEntry(const char *id, uint32 offset, uint32 nEntries, uint32 vers)
{
	ReferenceEntry re;
	strncpy(re.id, id, 4);
	re.offset = offset;
	re.nEntries = nEntries;
	re.vers = vers;
	reList.push_back(re);
}

void NameRefEntry(Reference &name, wxString strName, wxFFile &f)
{
	strName.Append(wxT('\0'));
	name.nEntries = (uint32)strName.Len();
	name.ref = (uint32)reList.size();
	RefEntry("RAHC", f.Tell(), name.nEntries, 0);
	f.Write(strName.data(), strName.Len());
	padding(f);
}

void DataRefEntry(const char *id, Reference &data, int count, int size, void *buff, int ver, wxFFile &f)
{
	data.nEntries = count;
	if (count <= 0)
		return;
	data.ref = (uint32)reList.size();
	RefEntry(id, f.Tell(), data.nEntries, ver);
	f.Write(buff, size);
	padding(f);
}

uint32 nSkinnedBones(Model *m, MPQFile *mpqf)
{
	ModelVertex *verts = (ModelVertex*)(mpqf->getBuffer() + m->header.ofsVertices);
	ModelBoneDef *mb = (ModelBoneDef*)(mpqf->getBuffer() + m->header.ofsBones);
	uint8 *skinned = new uint8[m->header.nBones];
	memset(skinned, 0, sizeof(uint8)*m->header.nBones);
	uint32 nSkinnedBones = 0;

	for(uint32 i=0; i < m->header.nVertices; i++) {
		for(uint32 j=0; j<4; j++) {
			if (verts[i].weights[j] != 0)
				skinned[verts[i].bones[j]] = 1;
		}
	}

	for(uint32 i=0; i<m->header.nBones; i++) {
		if (skinned[i] == 1)
		{
			uint32 j = i;
			while(1)
			{
				if(mb[j].parent > -1)
				{
					j = mb[j].parent;
					skinned[j] = 1;
				}
				else
					break;
			}
		}
	}
	for(uint32 i=0; i<m->header.nBones; i++) {
		if (skinned[i] == 1)
			nSkinnedBones ++;
	}
	wxDELETEA(skinned);
	nSkinnedBones++;
	return nSkinnedBones;
}

void ExportM3_M2(Attachment *att, Model *m, const char *fn, bool init)
{
	if (!m)
		return;

	wxFFile f(wxString(fn, wxConvUTF8), wxT("w+b"));
	reList.clear();

	if (!f.IsOpened()) {
		wxLogMessage(wxT("Error: Unable to open file '%s'. Could not export model."), fn);
		return;
	}
	LogExportData(wxT("M3"),m->modelname,wxString(fn, wxConvUTF8));

	MPQFile mpqf(m->modelname);
	MPQFile mpqfv(m->lodname);

	// 1. FileHead
	RefEntry("43DM", f.Tell(), 1, 0xB);

	struct MD34 fHead;
	memset(&fHead, 0, sizeof(fHead));
	strcpy(fHead.id, "43DM");
	fHead.mref.nEntries = 1;
	fHead.mref.ref = (uint32)reList.size();
	memset(&fHead.padding, 0xAA, sizeof(fHead.padding));
	f.Seek(sizeof(fHead), wxFromCurrent);

	// 2. ModelHead
	RefEntry("LDOM", f.Tell(), 1, 0x17);

	struct MODL mdata;
	memset(&mdata, 0, sizeof(mdata));
	mdata.init();
	f.Seek(sizeof(mdata), wxFromCurrent);

	// 3. Content
	// Prepare
	ModelView *view = (ModelView*)(mpqfv.getBuffer());
	ModelGeoset *ops = (ModelGeoset*)(mpqfv.getBuffer() + view->ofsSub);
	ModelTexUnit *tex = (ModelTexUnit*)(mpqfv.getBuffer() + view->ofsTex);
	uint16 *trianglelookup = (uint16*)(mpqfv.getBuffer() + view->ofsIndex);
	uint16 *triangles = (uint16*)(mpqfv.getBuffer() + view->ofsTris);

	ModelTextureDef *texdef = (ModelTextureDef*)(mpqf.getBuffer() + m->header.ofsTextures);
	uint16 *texlookup = (uint16*)(mpqf.getBuffer() + m->header.ofsTexLookup);
	uint16 *texunitlookup = (uint16*)(mpqf.getBuffer() + m->header.ofsTexUnitLookup);
	uint16 *texanimlookup = (uint16*)(mpqf.getBuffer() + m->header.ofsTexAnimLookup);
	ModelBoneDef *mb = (ModelBoneDef*)(mpqf.getBuffer() + m->header.ofsBones);
	ModelVertex *verts = (ModelVertex*)(mpqf.getBuffer() + m->header.ofsVertices);
	ModelRenderFlags *renderflags = (ModelRenderFlags *)(mpqf.getBuffer() + m->header.ofsTexFlags);
	//ModelTexAnimDef *texanim = (ModelTexAnimDef *)(mpqf.getBuffer() + m->header.ofsTexAnims);
	//uint16 *boneLookup = (uint16 *)(mpqf.getBuffer() + m->header.ofsBoneLookup);
	ModelParticleEmitterDef *particle = (ModelParticleEmitterDef *)(mpqf.getBuffer() + m->header.ofsParticleEmitters);
	ModelAttachmentDef *attachments = (ModelAttachmentDef*)(mpqf.getBuffer() + m->header.ofsAttachments);

	std::vector <SEQS>		Seqss;
	std::vector <STC>		Stcs;
	std::vector <STCExtra>	StcExtras;
	std::vector <STG>		Stgs;
	std::vector <STS>		Stss;
	std::vector <BONE>		Bones;
	wxArrayString			BoneNames;
	std::vector <Vertex32>	Verts;
	DIV						Div;
	std::vector <uint16>	Faces;
	std::vector <REGN>		Regns;
	std::vector <BAT>		Bats;
	MSEC					Msec;
	std::vector <uint16>	BoneLookup;
	std::vector <ATT>		Atts;
	wxArrayString			AttachNames;
	std::vector <int16>		AttachLoopkup;
	std::vector <MATM>		Matms;
	std::vector <MAT>		Mats;
	wxArrayString			MatNames;
	std::vector <MATExtra>	MatExtras;
	std::vector <PAR>		Pars;
	std::vector <IREF>		Irefs;

	std::vector <int>		MeshM2toM3;

	std::vector <uint32>	logAnimations;

	wxArrayString			vAnimations;
	wxArrayString			nameAnimations;
	wxArrayString			AttRefName;

/************************************************************************************/
/* Prepare Data                                                                     */
/************************************************************************************/

	wxString modelName = wxString(fn, wxConvUTF8).AfterLast(SLASH).BeforeLast('.');

	// init mesh, vertex, face, mat
	int boneidx = 0;
	int vertidx = 0;
	int faceidx = 0;

	for (size_t j=0; j<view->nSub; j++) 
	{
		if (m->showGeosets[j] != true)
		{
			MeshM2toM3.push_back(-1);
			continue;
		}

		std::vector<uint16> BoneLookup2; // local BoneLookup
		BoneLookup2.clear();

		REGN regn;
		memset(&regn, 0, sizeof(regn));
		regn.init();
		Vertex32 vert;

		for(int32 i=ops[j].vstart; i<(ops[j].vstart+ops[j].vcount); i++) {
			memset(&vert, 0, sizeof(vert));
			vert.pos = fixCoord(verts[trianglelookup[i]].pos); 
			memcpy(vert.weBone, verts[trianglelookup[i]].weights, 4);
			for (size_t k=0; k<4; k++)
			{
				if (verts[trianglelookup[i]].weights[k] != 0) {
					bool bFound = false;
					for(uint32 m=0; m<BoneLookup2.size(); m++) {
						if (BoneLookup2[m] == verts[trianglelookup[i]].bones[k])
						{
							bFound = true;
							vert.weIndice[k] = m;
							break;
						}
					}
					if (bFound == false)
					{
						vert.weIndice[k] = (unsigned char)BoneLookup2.size();
						BoneLookup2.push_back(verts[trianglelookup[i]].bones[k]);
					}
				}
			}
			// Vec3D normal -> char normal[4]
			vert.normal[0] = (verts[trianglelookup[i]].normal.x+1)*0xFF/2;
			vert.normal[1] = (verts[trianglelookup[i]].normal.y+1)*0xFF/2;
			vert.normal[2] = (verts[trianglelookup[i]].normal.z+1)*0xFF/2;
			// Vec2D texcoords -> uint16 uv[2]
			vert.uv[0] = verts[trianglelookup[i]].texcoords.x*0x800;
			vert.uv[1] = verts[trianglelookup[i]].texcoords.y*0x800;

			Verts.push_back(vert);
		}

		for(int32 i = ops[j].istart; i < ops[j].istart + ops[j].icount; i++) {
			uint16 face;
			face = triangles[i] - ops[j].vstart;
			Faces.push_back(face);
		}

		// push local BoneLookup to global, and lookup it in boneLookup
		for(uint32 i=0; i<BoneLookup2.size(); i++) {
			BoneLookup.push_back(BoneLookup2[i] + ROOT_BONE);
		}

		regn.boneCount = (uint16)BoneLookup2.size();
		regn.numBone = (uint16)BoneLookup2.size();
		regn.numFaces = ops[j].icount;
		regn.numVert = ops[j].vcount;
		regn.indBone = boneidx;
		regn.indFaces = faceidx;
		regn.indVert = vertidx;

		MeshM2toM3.push_back((const int)Regns.size());
		Regns.push_back(regn);

		boneidx += regn.numBone;
		faceidx += regn.numFaces;
		vertidx += regn.numVert;
	}

	//prepare BAT and MeshtoMat Table
	std::vector<MATmap> MATtable;
	for (size_t i=0; i<view->nTex; i++)
	{
		if ((gameVersion < VERSION_CATACLYSM && tex[i].texunit < m->header.nTexUnitLookup && texunitlookup[tex[i].texunit] == 0) || gameVersion >= VERSION_CATACLYSM) // cataclysm lost this table
		{	
			int idx = -1;

			for(uint32 j=0; j<MATtable.size(); j++)
			{

				if (MATtable[j].texid == texlookup[tex[i].textureid] && 
					MATtable[j].blend == renderflags[tex[i].flagsIndex].blend &&
					MATtable[j].flags == renderflags[tex[i].flagsIndex].flags &&
					MATtable[j].color == tex[i].colorIndex)
				{
					idx = j;
					break;
				}
			}
			if (idx < 0)
			{
				if (m->showGeosets[tex[i].op])
				{
					MATmap bm;
					bm.texid = texlookup[tex[i].textureid];
					bm.flags = renderflags[tex[i].flagsIndex].flags;
					bm.blend = renderflags[tex[i].flagsIndex].blend;
					bm.animid = texanimlookup[tex[i].texanimid];
					bm.color = tex[i].colorIndex;
					if (m->charModelDetails.isChar && ops[tex[i].op].id == 0 && tex[i].colorIndex >= 0)
						bm.eye = 1;
					else
						bm.eye = 0;
					idx = (int)MATtable.size();
					MATtable.push_back(bm);
					
					BAT bat;
					memset(&bat, 0, sizeof(bat));
					bat.init();
					bat.regnIndex = MeshM2toM3[tex[i].op];
					bat.matmIndex = idx;
					Bats.push_back(bat);
				}
			}
			else
			{
				int found = 0;
				for(uint32 k=0; k<Bats.size(); k++)
				{
					if (Bats[k].regnIndex == MeshM2toM3[tex[i].op] && Bats[k].matmIndex == idx)
					{
						found = 1;
						break;
					}
				}
				if (found == 0)
				{
					if (m->showGeosets[tex[i].op])
					{
						BAT bat;
						memset(&bat, 0, sizeof(bat));
						bat.init();
						bat.regnIndex = MeshM2toM3[tex[i].op];
						bat.matmIndex = idx;
						Bats.push_back(bat);
					}
				}
			}
		}
	}

	std::vector <uint32> M3TexAnimId;
	std::vector <uint32> M2TexAnimId;
	for(uint32 i=0; i<MATtable.size(); i++) {
		for(uint32 j=0; j<13; j++) {
			if (j == MAT_LAYER_DIFF && (MATtable[i].blend != BM_ADDITIVE_ALPHA && MATtable[i].blend != BM_ADDITIVE)) 
			{
				if (MATtable[i].animid != -1)
				{
					M3TexAnimId.push_back(CreateAnimID(AR_Layer, i, j, 7));
					M2TexAnimId.push_back(MATtable[i].animid);
				}
			}

			if (j == MAT_LAYER_EMISSIVE && (MATtable[i].blend == BM_ADDITIVE_ALPHA || MATtable[i].blend == BM_ADDITIVE)) 
			{
				if (MATtable[i].animid != -1)
				{
					M3TexAnimId.push_back(CreateAnimID(AR_Layer, i, j, 7));
					M2TexAnimId.push_back(MATtable[i].animid);
				}
			}

			if (j == MAT_LAYER_ALPHA && 
				(MATtable[i].blend == BM_ALPHA_BLEND || MATtable[i].blend == BM_ADDITIVE_ALPHA || MATtable[i].blend == BM_TRANSPARENT)) // LAYER_Alpha
			{
				if (MATtable[i].animid != -1)
				{
					M3TexAnimId.push_back(CreateAnimID(AR_Layer, i, j, 7));
					M2TexAnimId.push_back(MATtable[i].animid);
				}
			}
		}
	}
/*
	if (att->children.size() > 0)
	{
		for (size_t i = 0; i < att->children[0]->children.size(); i++)
		{
			Attachment *att2 = att->children[0]->children[i];
			Model *am = static_cast<Model*>(att2->model);
			if (am)
			{
				MPQFile ampqf(am->modelname);
				MPQFile ampqfv(am->lodname);

				ampqf.close();
				ampqfv.close();
			}
		}
	}
*/

	// init seqs
	if (modelExport_M3_Anims.size() > 0) {
		logAnimations = modelExport_M3_Anims;
		for(uint32 i=0; i<m->header.nAnimations; i++) {
			SEQS seqs;
			bool bFound = false;
			uint32 pos;
			for(pos=0; pos<logAnimations.size(); pos++) {
				if (logAnimations[pos] == i) {
					bFound = true;
					break;
				}
			}
			if (bFound == false)
				continue;
			wxString strName = modelExport_M3_AnimNames[pos];

			// make name unique
			uint32 counts = 0;
			for(uint32 j=0; j<vAnimations.size(); j++) {
				if (vAnimations[j] == strName) {
					counts ++;
				}
			}
			vAnimations.push_back(strName);
			if (counts > 0)
				strName += wxString::Format(wxT(" %02d"), counts);

			nameAnimations.push_back(strName);

			memset(&seqs, 0, sizeof(seqs));
			seqs.init();
			seqs.length = m->anims[i].timeEnd;
			seqs.moveSpeed = m->anims[i].moveSpeed;
			seqs.frequency = m->anims[i].playSpeed;
			seqs.boundSphere.min = fixCoord(m->anims[logAnimations[0]].boundSphere.min) * modelExport_M3_SphereScale;
			seqs.boundSphere.max = fixCoord(m->anims[logAnimations[0]].boundSphere.max) * modelExport_M3_SphereScale;
			seqs.boundSphere.radius = m->anims[logAnimations[0]].boundSphere.radius * modelExport_M3_SphereScale;
			Seqss.push_back(seqs);

		}
	} else {
		for(uint32 i=0; i<m->header.nAnimations; i++) {
			SEQS seqs;
			wxString strName;
			try {
				AnimDB::Record rec = animdb.getByAnimID(m->anims[i].animID);
				strName = rec.getString(AnimDB::Name);
			} catch (AnimDB::NotFound) {
				strName = wxT("???");
			}
			if (!strName.StartsWith(wxT("Run")) && !strName.StartsWith(wxT("Stand")) && 
					!strName.StartsWith(wxT("Attack")) && !strName.StartsWith(wxT("Death")))
				continue;
			if (strName.StartsWith(wxT("StandWound")))
				continue;

			if (strName.StartsWith(wxT("Run"))) {
				strName = wxT("Walk");
			}
			if (strName.StartsWith(wxT("Stand"))) {
				strName = wxT("Stand");
			}
			if (strName.StartsWith(wxT("Attack"))) {
				strName = wxT("Attack");
			}
			if (strName.StartsWith(wxT("Death"))) {
				strName = wxT("Death");
			}

			// make name unique
			uint32 counts = 0;
			for(uint32 j=0; j<vAnimations.size(); j++) {
				if (vAnimations[j] == strName) {
					counts ++;
				}
			}
			vAnimations.push_back(strName);
			if (counts > 0)
				strName += wxString::Format(wxT(" %02d"), counts);

			nameAnimations.push_back(strName);

			logAnimations.push_back(i);

			memset(&seqs, 0, sizeof(seqs));
			seqs.init();
			seqs.length = m->anims[i].timeEnd;
			seqs.moveSpeed = m->anims[i].moveSpeed;
			seqs.frequency = m->anims[i].playSpeed;
			seqs.boundSphere.min = fixCoord(m->anims[logAnimations[0]].boundSphere.min) * modelExport_M3_SphereScale;
			seqs.boundSphere.max = fixCoord(m->anims[logAnimations[0]].boundSphere.max) * modelExport_M3_SphereScale;
			seqs.boundSphere.radius = m->anims[logAnimations[0]].boundSphere.radius * modelExport_M3_SphereScale;
			Seqss.push_back(seqs);
		}
	}


	// init stc
	for(uint32 i=0; i < Seqss.size(); i++) {
		int anim_offset = logAnimations[i];
		STC stc;
		memset(&stc, 0, sizeof(stc));
		STCExtra extra;

		std::vector <uint32> M3OpacityAnimid;
		std::vector <uint32> M2OpacityIdx;

		M3OpacityAnimid.clear();
		M2OpacityIdx.clear();

		wxString strName = nameAnimations[i];
		strName.Append(wxT("_full"));
		extra.animname = strName;

		// animid
		for (size_t j=0; j<MATtable.size(); j++)
		{
			if (MATtable[j].color < 0)
				continue;
			if ((m->colors[MATtable[j].color].opacity.seq == -1 &&  m->colors[MATtable[j].color].opacity.data[anim_offset].size() > 0) ||
				(m->colors[MATtable[j].color].opacity.seq != -1 &&  m->colors[MATtable[j].color].opacity.data[0].size() > 0))
			{
				for(uint32 k=0; k<13; k++) 
				{

					if (k == MAT_LAYER_EMISSIVE && (MATtable[j].blend == BM_ADDITIVE_ALPHA || MATtable[j].blend == BM_ADDITIVE)) 
					{
						M3OpacityAnimid.push_back(CreateAnimID(AR_Layer, (int)j, (int)k, 2));
						M2OpacityIdx.push_back(MATtable[j].color);
					}

					if (k == MAT_LAYER_ALPHA && (MATtable[j].blend == BM_TRANSPARENT || MATtable[j].blend == BM_ALPHA_BLEND || MATtable[j].blend == BM_ADDITIVE_ALPHA))
					{
						M3OpacityAnimid.push_back(CreateAnimID(AR_Layer, (int)j, (int)k, 2));
						M2OpacityIdx.push_back(MATtable[j].color);
					}
				}
			}
		}

		int16 v2dcount = 0;
		int16 v3dcount = 0;
		int16 v4dcount = 0;
		int16 fcount = 0;
		AnimOffset animoff;
	
		// bone anim id
		for(uint32 j=0; j<m->header.nBones; j++) {
			if (m->bones[j].trans.data[anim_offset].size() > 0) {
				extra.animid.push_back(CreateAnimID(AR_Bone, j+ROOT_BONE, 0, 2));
				animoff.index = v3dcount++;
				animoff.type = STC_INDEX_VEC3D;
				extra.animoff.push_back(animoff);
			}
			if (m->bones[j].scale.data[anim_offset].size() > 0) {
				extra.animid.push_back(CreateAnimID(AR_Bone, j+ROOT_BONE, 0, 5));
				animoff.index = v3dcount++;
				animoff.type = STC_INDEX_VEC3D;
				extra.animoff.push_back(animoff);
			}
			if (m->bones[j].rot.data[anim_offset].size() > 0) {
				extra.animid.push_back(CreateAnimID(AR_Bone, j+ROOT_BONE, 0, 3));
				animoff.index = v4dcount++;
				animoff.type = STC_INDEX_QUAT;
				extra.animoff.push_back(animoff);
			}
		}

		// tex anim id
		for(uint32 j=0; j<M3TexAnimId.size(); j++) {
			extra.animid.push_back(M3TexAnimId[j]);
			animoff.index = v2dcount++;
			animoff.type = STC_INDEX_VEC2D;
			extra.animoff.push_back(animoff);
		}

		// mesh opacity id
		for (size_t j=0; j<M3OpacityAnimid.size(); j++) {
			extra.animid.push_back(M3OpacityAnimid[j]);
			animoff.index = fcount++;
			animoff.type = STC_INDEX_FLOAT;
			extra.animoff.push_back(animoff);
		}

		// particle rate id
		if (bShowParticle && gameVersion < VERSION_CATACLYSM)
		{
			for (size_t j=0; j<m->header.nParticleEmitters; j++)
			{
				if (particle[j].en.nTimes > 0 && 
					((m->particleSystems[j].enabled.seq == -1 &&  m->particleSystems[j].enabled.data[anim_offset].size() > 0) ||
					 (m->particleSystems[j].enabled.seq != -1 &&  m->particleSystems[j].enabled.data[0].size() > 0)))
				{
					extra.animid.push_back(CreateAnimID(AR_Par, (int)j, 0, 14));
					animoff.index = fcount++;
					animoff.type = STC_INDEX_FLOAT;
					extra.animoff.push_back(animoff);
				}
			}
		}

		// EVNT
		{
			SD sd;
			memset(&sd, 0, sizeof(sd));
			sd.init();

			for(uint32 j=0; j<m->header.nBones; j++) {
				if (m->bones[j].trans.data[anim_offset].size() > 0) {
					sd.length = Seqss[i].length;  
					break;
				}
			}

			EVNT evnt;
			memset(&evnt, 0, sizeof(evnt));
			evnt.init();

			extra.animevent.data = evnt;
			extra.animevent.timeline = sd.length;
			extra.animevent.name = wxT("Evt_SeqEnd");

			extra.animsdevent = sd;
		}


		// V2DS
		for(uint32 j=0; j<M3TexAnimId.size(); j++) {
			SD sd;
			memset(&sd, 0, sizeof(sd));
			sd.init();
			Anim_Vec2D av2d;

			int counts = (int)m->texAnims[M2TexAnimId[j]].trans.times[0].size();
			for (ssize_t k=0; k < counts; k++) {
				av2d.timeline.push_back(m->texAnims[M2TexAnimId[j]].trans.times[0][k]);
				Vec2D tran;
				tran.x = -m->texAnims[M2TexAnimId[j]].trans.data[0][k].x;
				tran.y = -m->texAnims[M2TexAnimId[j]].trans.data[0][k].y;
				av2d.data.push_back(tran);
			}

			sd.length = m->texAnims[M2TexAnimId[j]].trans.times[0][1];

			extra.animsdvec2d.push_back(sd);
			extra.animvec2d.push_back(av2d);
		}
		
		// Trans and Scale, V3DS
		for(uint32 j=0; j<m->header.nBones; j++) {
			// trans
			if (m->bones[j].trans.data[anim_offset].size() > 0) {
				SD sd;
				memset(&sd, 0, sizeof(sd));
				sd.init();
				Anim_Vec3D av3d;

				int counts = m->bones[j].trans.data[anim_offset].size();
				for (ssize_t k=0; k<counts; k++) {
					av3d.timeline.push_back(m->bones[j].trans.times[anim_offset][k]);
					Vec3D tran;
					if (m->bones[j].parent > -1)
						tran = m->bones[j].pivot - m->bones[m->bones[j].parent].pivot;
					else
						tran = m->bones[j].pivot;
					tran += m->bones[j].trans.data[anim_offset][k];
					tran.z *= -1.0f;
					av3d.data.push_back(Vec3D(tran.x, tran.z, tran.y));
				}

				sd.length = Seqss[i].length; 

				extra.animsdvec3d.push_back(sd);
				extra.animvec3d.push_back(av3d);
			}
			//scale
			if (m->bones[j].scale.data[anim_offset].size() > 0) {
				SD sd;
				memset(&sd, 0, sizeof(sd));
				sd.init();
				Anim_Vec3D av3d;

				int counts = m->bones[j].scale.data[anim_offset].size();
				for (ssize_t k=0; k<counts; k++) {
					av3d.timeline.push_back(m->bones[j].scale.times[anim_offset][k]);
					Vec3D scale;
					scale.x = m->bones[j].scale.data[anim_offset][k].x;
					scale.y = m->bones[j].scale.data[anim_offset][k].z;
					scale.z = m->bones[j].scale.data[anim_offset][k].y;
					av3d.data.push_back(scale);
				}

				sd.length = Seqss[i].length;

				extra.animsdvec3d.push_back(sd);
				extra.animvec3d.push_back(av3d);
			}
		}

		// Rot, Q4DS
		for(uint32 j=0; j<m->header.nBones; j++) {
			if (m->bones[j].rot.data[anim_offset].size() > 0) {
				SD sd;
				memset(&sd, 0, sizeof(sd));
				sd.init();
				Anim_Vec4D av4d;

				int counts = m->bones[j].rot.data[anim_offset].size();
				for (ssize_t k=0; k<counts; k++) {
					av4d.timeline.push_back(m->bones[j].rot.times[anim_offset][k]);
					Vec4D rot;
					rot.x = -m->bones[j].rot.data[anim_offset][k].x;
					rot.y = m->bones[j].rot.data[anim_offset][k].z;
					rot.z = -m->bones[j].rot.data[anim_offset][k].y;
					rot.w = m->bones[j].rot.data[anim_offset][k].w;
					av4d.data.push_back(rot);
				}

				sd.length = Seqss[i].length;

				extra.animsdvec4d.push_back(sd);
				extra.animvec4d.push_back(av4d);
			}
		}

		// Float, 3RDS
		for (size_t j=0; j<M3OpacityAnimid.size(); j++)
		{
			if ((m->colors[M2OpacityIdx[j]].opacity.seq == -1 &&  m->colors[M2OpacityIdx[j]].opacity.data[anim_offset].size() > 0) ||
			    (m->colors[M2OpacityIdx[j]].opacity.seq != -1 &&  m->colors[M2OpacityIdx[j]].opacity.data[0].size() > 0))
			{
				SD sd;
				memset(&sd, 0, sizeof(sd));
				sd.init();
				Anim_Float af;

				int animidx;
				if (m->colors[M2OpacityIdx[j]].opacity.seq == -1)
					animidx = anim_offset;
				else
					animidx = 0;
				int counts = m->colors[M2OpacityIdx[j]].opacity.data[animidx].size();
				for (ssize_t k=0; k<counts; k++) {
					af.timeline.push_back(m->colors[M2OpacityIdx[j]].opacity.times[animidx][k]);
					af.data.push_back(m->colors[M2OpacityIdx[j]].opacity.data[animidx][k]);
				}

				sd.length = Seqss[i].length;

				extra.animsdfloat.push_back(sd);
				extra.animfloat.push_back(af);
			}
		}

		// particle rate anim
		if (bShowParticle && gameVersion < VERSION_CATACLYSM)
		{
			for (size_t j=0; j<m->header.nParticleEmitters; j++)
			{
				if (particle[j].en.nTimes > 0)
				{
					if ((m->particleSystems[j].enabled.seq == -1 &&  m->particleSystems[j].enabled.data[anim_offset].size() > 0) ||
						(m->particleSystems[j].enabled.seq != -1 &&  m->particleSystems[j].enabled.data[0].size() > 0))
					{
						SD sd;
						memset(&sd, 0, sizeof(sd));
						sd.init();
						Anim_Float af;

						int animidx;
						if (m->particleSystems[j].enabled.seq == -1)
							animidx = anim_offset;
						else
							animidx = 0;
						int counts = m->particleSystems[j].enabled.data[animidx].size();
						for (ssize_t k=0; k<counts; k++) {
							af.timeline.push_back(m->particleSystems[j].enabled.times[animidx][k]);
							float rate;
							if (m->particleSystems[j].enabled.data[animidx][k] && m->particleSystems[j].rate.data[0].size() > 0)
								rate = m->particleSystems[j].rate.data[0][0];
							else
								rate = 0;	
							af.data.push_back(rate);
						}

						sd.length = Seqss[i].length;

						extra.animsdfloat.push_back(sd);
						extra.animfloat.push_back(af);
					}
				}
			}
		}

		Stcs.push_back(stc);
		StcExtras.push_back(extra);
	}

	// init stg
	for(uint32 i=0; i<Seqss.size(); i++) {
		STG stg;
		memset(&stg, 0, sizeof(stg));
		Stgs.push_back(stg);
	}

	// init sts
	for(uint32 i=0; i<Seqss.size(); i++) {
		STS sts;
		memset(&sts, 0, sizeof(sts));
		sts.init();
		Stss.push_back(sts);
	}

	// init bones
	if (ROOT_BONE == 1) {
		BONE bone;
		memset(&bone, 0, sizeof(BONE));
		bone.init();
		bone.parent = -1;
		bone.initTrans.AnimRef.animid = CreateAnimID(AR_Bone, 0, 0, 2);
		bone.initRot.AnimRef.animid = CreateAnimID(AR_Bone, 0, 0, 3);
		if (m->modelname.Lower().Mid(0, 4) == wxT("item"))
			bone.initRot.value = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
		else
			bone.initRot.value = Vec4D(0.0f, 0.0f, -sqrt(0.5f), sqrt(0.5f));
		bone.initScale.AnimRef.animid = CreateAnimID(AR_Bone, 0, 0, 5);
		bone.initScale.value = Vec3D(1.0f, 1.0f, 1.0f)*modelExport_M3_BoundScale;
		bone.ar1.AnimRef.animid = CreateAnimID(AR_Bone, 0, 0, 6);
		
		Bones.push_back(bone);

		BoneNames.push_back(modelName + wxT("_Bone_Root"));
	}

	for(uint32 i=0; i<m->header.nBones; i++) {
		BONE bone;
		int  idx = Bones.size();
		memset(&bone, 0, sizeof(BONE));

		// name
		wxString strName = modelName + wxString::Format(wxT("_Bone%d"), i);

		for(uint32 j=0; j < BONE_MAX; j++) {
			if (i >= ROOT_BONE && m->keyBoneLookup[j] == (int)(i-ROOT_BONE)) {
				strName += wxT("_")+Bone_Names[j];
				break;
			}
		}

		bone.init();
		bone.parent = mb[i].parent + ROOT_BONE;
		bone.initTrans.AnimRef.animid = CreateAnimID(AR_Bone, idx, 0, 2);
		for (size_t j=0; j<Seqss.size(); j++)
		{
			int anim_offset = logAnimations[j];
			if (m->bones[i].trans.data[anim_offset].size() > 0)
			{
				SetAnimed(bone.initTrans.AnimRef);
				break;
			}
		}

		bone.initTrans.value = mb[i].pivot;
		if (bone.parent > (ROOT_BONE - 1))
			bone.initTrans.value -= mb[bone.parent - ROOT_BONE].pivot ;

		bone.initRot.AnimRef.animid = CreateAnimID(AR_Bone, idx, 0, 3);
		for (size_t j=0; j<Seqss.size(); j++)
		{
			int anim_offset = logAnimations[j];
			if (m->bones[i].rot.data[anim_offset].size() > 0)
			{
				SetAnimed(bone.initRot.AnimRef);
				break;
			}
		}

		bone.initScale.AnimRef.animid = CreateAnimID(AR_Bone, idx, 0, 5);
		for (size_t j=0; j<Seqss.size(); j++)
		{
			int anim_offset = logAnimations[j];
			if (m->bones[i].scale.data[anim_offset].size() > 0)
			{
				SetAnimed(bone.initScale.AnimRef);
				break;
			}
		}
		bone.ar1.AnimRef.animid = CreateAnimID(AR_Bone, idx, 0, 6);
		Bones.push_back(bone);

		BoneNames.push_back(strName);
	}

	// nSkinnedBones
	mdata.nSkinnedBones = nSkinnedBones(m, &mpqf);

	// boundSphere, m->header.boundSphere is too big
	mdata.boundSphere.min = fixCoord(m->anims[logAnimations[0]].boundSphere.min) * modelExport_M3_SphereScale;
	mdata.boundSphere.max = fixCoord(m->anims[logAnimations[0]].boundSphere.max) * modelExport_M3_SphereScale;
	mdata.boundSphere.radius = m->anims[logAnimations[0]].boundSphere.radius * modelExport_M3_SphereScale;

	// init div
	memset(&Div, 0, sizeof(Div));
	Div.init();

	// init msec
	memset(&Msec, 0, sizeof(Msec));
	Msec.bndSphere.AnimRef.animid = CreateAnimID(AR_MSEC, 0, 0, 1);

	// init particle
	uint32 partexstart = MATtable.size();
	std::vector <int32> M3ParticleMap;
	if (bShowParticle && gameVersion < VERSION_CATACLYSM)
	{
		// prepare particle texture
		for (size_t i=0; i < m->header.nParticleEmitters; i++)
		{
			int textureidx = -1;
			if (particle[i].texture >= 0)
			{
				for (size_t j = partexstart; j < MATtable.size(); j++)
				{
					if (MATtable[j].texid == particle[i].texture)
					{
						textureidx = j;
						break;
					}
				}
				if (textureidx == -1)
				{
					textureidx = MATtable.size();
					MATmap bm;
					bm.texid = particle[i].texture;
					bm.flags = 0;
					bm.blend = BM_ADDITIVE_ALPHA; 
					bm.animid = -1;
					bm.color = -1;
					MATtable.push_back(bm);
				}
			}

			PAR par;
			memset(&par, 0, sizeof(PAR));

			par.emisSpeedStart.AnimRef.animid = CreateAnimID(AR_Par, i, 0, 1);
			par.speedVariation.AnimRef.animid = CreateAnimID(AR_Par, i, 0, 2);
			par.yAngle.AnimRef.animid =			CreateAnimID(AR_Par, i, 0, 3);
			par.xAngle.AnimRef.animid =			CreateAnimID(AR_Par, i, 0, 4);
			par.xSpread.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 5);
			par.ySpread.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 6);
			par.lifespan.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 7);
			par.decay.AnimRef.animid =			CreateAnimID(AR_Par, i, 0, 8);
			par.scale1.AnimRef.animid =			CreateAnimID(AR_Par, i, 0, 9);
			par.speedUnk1.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 10);
			par.col1Start.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 11);
			par.col1Mid.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 12);
			par.col1End.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 13);
			par.emissionRate.AnimRef.animid =	CreateAnimID(AR_Par, i, 0, 14);
			par.emissionArea.AnimRef.animid =	CreateAnimID(AR_Par, i, 0, 15);
			par.tailUnk1.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 16);
			par.pivotSpread.AnimRef.animid =	CreateAnimID(AR_Par, i, 0, 17);
			par.spreadUnk1.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 18);
			par.ar19.AnimRef.animid =			CreateAnimID(AR_Par, i, 0, 19);
			par.rotate.AnimRef.animid =			CreateAnimID(AR_Par, i, 0, 20);
			par.col2Start.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 21);
			par.col2Mid.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 22);
			par.col2End.AnimRef.animid =		CreateAnimID(AR_Par, i, 0, 23);
			par.ar24.AnimRef.animid =			CreateAnimID(AR_Par, i, 0, 24);
			par.ar25.AnimRef.animid =			CreateAnimID(AR_Par, i, 0, 25);
			par.ar26.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 26);
			par.ar27.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 27);
			par.ar28.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 28);
			par.ar29.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 29);
			par.ar30.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 30);
			par.ar31.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 31);
			par.ar32.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 32);
			par.ar33.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 33);
			par.ar34.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 34);
			par.ar35.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 35);
			par.ar36.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 36);
			par.ar37.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 37);
			par.ar38.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 38);
			par.ar39.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 39);
			par.ar40.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 40);
			par.ar41.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 41);
			par.ar42.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 42);
			par.ar43.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 43);
			par.ar44.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 44);
			par.ar45.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 45);
			par.ar46.AnimRef.animid = 			CreateAnimID(AR_Par, i, 0, 46);

			par.matmIndex = textureidx;
			par.bone = particle[i].bone + 1;
			par.maxParticles = 1000;
			if (m->particleSystems[i].speed.data[0].size() > 0)
				par.emisSpeedStart.value = m->particleSystems[i].speed.data[0][0];  //particle[i].EmissionSpeed;
			par.emisSpeedMid = par.emisSpeedStart.value;
			par.emisSpeedEnd = par.emisSpeedStart.value;
			if (m->particleSystems[i].variation.data[0].size() > 0)
				par.speedVariation.value = m->particleSystems[i].variation.data[0][0] * par.emisSpeedStart.value ;  //particle[i].SpeedVariation.
			if (m->particleSystems[i].spread.data[0].size() > 0)
			{
				par.xSpread.value = m->particleSystems[i].spread.data[0][0];
				par.ySpread.value = m->particleSystems[i].spread.data[0][0]; //m->particleSystems[i].lat.data[0][0];
			}
			if (m->particleSystems[i].lifespan.data[0].size() > 0)
				par.lifespan.value = m->particleSystems[i].lifespan.data[0][0];
			par.scaleRatio = 0.5f;
			par.scale1.value.x = m->particleSystems[i].sizes[0] * 2.0f;//particle[i].p.scales[0];
			par.scale1.value.y = m->particleSystems[i].sizes[1] * 2.0f;//particle[i].p.scales[1];
			par.scale1.value.z = m->particleSystems[i].sizes[2] * 2.0f;//particle[i].p.scales[2];
			Vec3D  colors2[3];
			uint16 opacity[3];
			memcpy(colors2, mpqf.getBuffer()+particle[i].p.colors.ofsKeys, sizeof(Vec3D)*3);
			memcpy(opacity, mpqf.getBuffer()+particle[i].p.opacity.ofsKeys, sizeof(uint16)*3);
			par.col1Start.value[0] = (int)colors2[0].z;
			par.col1Start.value[1] = (int)colors2[0].y;
			par.col1Start.value[2] = (int)colors2[0].x;
			par.col1Start.value[3] = (int)opacity[0] >> 7;
			par.col1Mid.value[0] = (int)colors2[1].z;
			par.col1Mid.value[1] = (int)colors2[1].y;
			par.col1Mid.value[2] = (int)colors2[1].x;
			par.col1Mid.value[3] = (int)opacity[1] >> 7;
			par.col1End.value[0] = (int)colors2[2].z;
			par.col1End.value[1] = (int)colors2[2].y;
			par.col1End.value[2] = (int)colors2[2].x;
			par.col1End.value[3] = (int)opacity[2] >> 7;

			if (m->particleSystems[i].areaw.data[0].size() > 0)
				par.emissionArea.value.x = m->particleSystems[i].areaw.data[0][0];
			if (m->particleSystems[i].areal.data[0].size() > 0)
				par.emissionArea.value.y = m->particleSystems[i].areal.data[0][0];

			par.f9[0] = 1.0f;
			par.f9[1] = 1.0f;

			par.d17[1] = 1;

			par.f5[0] = 1;
			par.f5[1] = 0.5;
			par.f5[2] = 0.5;

			if (particle[i].p.rotation > 0)
			{
				par.enableRotate = 1;
				par.rotate.value.x = 0;
				par.rotate.value.y = particle[i].p.rotation;
				par.rotate.value.z = particle[i].p.rotation;
			}

			if (m->particleSystems[i].rate.data[0].size() > 0)
				par.emissionRate.value = m->particleSystems[i].rate.data[0][0];
			par.columns = particle[i].cols;
			par.rows = particle[i].rows;

			if (par.columns > 1)
				par.f15[0] = 1.0f / par.columns;
			if (par.rows > 1)
				par.f15[1] = 1.0f / par.rows;

			if (par.columns > 1 || par.rows > 1)
			{
				par.parFlags |= PARTICLEFLAG_randFlipbookStart;
			}
			par.parFlags |= PARTICLEFLAG_useVertexAlpha;

			par.enableSpeedVariation = 1;

			if (particle[i].EmitterType == 1)
				par.ptenum = 1;
			else if (particle[i].EmitterType == 2)
				par.ptenum = 2;

			if (particle[i].en.nTimes > 0)
				SetAnimed(par.emissionRate.AnimRef);

			Pars.push_back(par);
		}
	}

	// init attach
	for(uint32 i=0; i < m->header.nAttachments; i++) {
		ATT att;
		memset(&att, 0, sizeof(att));
		att.init();
		att.bone = attachments[i].bone + ROOT_BONE;
		Atts.push_back(att);

		// name
		wxString strName = wxT("Ref_Hardpoint");

		if (attachments[i].id < WXSIZEOF(M3_Attach_Names))
			strName = wxString(M3_Attach_Names[attachments[i].id], wxConvUTF8);

		AttRefName.push_back(strName);

		int count = 0;
		for(uint32 j=0; j < AttRefName.size(); j++) {
			if (AttRefName[j] == strName)
				count++;
		}

		if (count > 1)
			strName += wxString::Format(wxT(" %02d"), count - 1);

		AttachNames.push_back(strName);
	}

	// init attachLu
	for (uint16 i=0; i < m->header.nAttachments; i++)
		AttachLoopkup.push_back(-1);

	// init matLu
	for(uint32 i=0; i<MATtable.size(); i++) {
		MATM matm;
		matm.init();
		matm.matind = i;
		Matms.push_back(matm);
	}

	// init mat
	for(uint32 i=0; i<MATtable.size(); i++) {
		MAT mat;
		MATExtra extra;
		memset(&mat, 0, sizeof(mat));
		mat.init();

		// name
		MatNames.push_back(modelName + wxString::Format(wxT("_Mat_%02d"), i+1));

		// layers
		for(uint32 j=0; j<13; j++) {
			int texid = MATtable[i].texid;
			wxString texName = m->TextureList[texid].BeforeLast('.').AfterLast(SLASH) + wxT(".tga");
			wxString fulltexName = wxT("");

			LAYR layer;
			memset(&layer, 0, sizeof(layer));
			layer.init();

			layer.Colour.AnimRef.animid =			CreateAnimID(AR_Layer, i, j, 1);
			layer.brightness_mult1.AnimRef.animid = CreateAnimID(AR_Layer, i, j, 2);
			layer.brightness_mult2.AnimRef.animid = CreateAnimID(AR_Layer, i, j, 3);
			layer.ar1.AnimRef.animid =				CreateAnimID(AR_Layer, i, j, 4);
			layer.ar2.AnimRef.animid =				CreateAnimID(AR_Layer, i, j, 5);
			layer.ar3.AnimRef.animid =				CreateAnimID(AR_Layer, i, j, 6);
			layer.ar4.AnimRef.animid =				CreateAnimID(AR_Layer, i, j, 7);
			layer.uvAngle.AnimRef.animid =			CreateAnimID(AR_Layer, i, j, 8);
			layer.uvTiling.AnimRef.animid =			CreateAnimID(AR_Layer, i, j, 9);
			layer.ar5.AnimRef.animid =				CreateAnimID(AR_Layer, i, j, 10);
			layer.ar6.AnimRef.animid =				CreateAnimID(AR_Layer, i, j, 11);
			layer.brightness.AnimRef.animid =		CreateAnimID(AR_Layer, i, j, 12);
		

			if (modelExport_M3_TexturePath.Len() > 0)
			{
				if (modelExport_M3_TexturePath.Last() != '/' && modelExport_M3_TexturePath.Last() != '\\')
					texName = modelExport_M3_TexturePath + SLASH + texName;
				else
					texName = modelExport_M3_TexturePath + texName;
			}

			if (j == MAT_LAYER_DIFF && (MATtable[i].blend != BM_ADDITIVE_ALPHA && MATtable[i].blend != BM_ADDITIVE)) 
			{
				fulltexName = texName;

				if (MATtable[i].animid != -1)
					SetAnimed(layer.ar4.AnimRef);
				if (bShowParticle && gameVersion < VERSION_CATACLYSM && i >= partexstart)
					layer.flags |= LAYR_FLAGS_SPLIT;
			}

			if (j == MAT_LAYER_EMISSIVE && (MATtable[i].blend == BM_ADDITIVE_ALPHA || MATtable[i].blend == BM_ADDITIVE)) 
			{
				fulltexName = texName;

				if (MATtable[i].animid != -1)
					SetAnimed(layer.ar4.AnimRef);
				if (MATtable[i].color != -1)
				{
					if (m->colors[MATtable[i].color].opacity.sizes != 0)
						SetAnimed(layer.brightness_mult1.AnimRef);
				}
				if (bShowParticle && gameVersion < VERSION_CATACLYSM && i >= partexstart)
					layer.flags |= LAYR_FLAGS_SPLIT;
			}

			if (j == MAT_LAYER_ALPHA && MATtable[i].blend == BM_OPAQUE && MATtable[i].color != -1)
			{
				fulltexName = wxT("NoTexture");
				layer.alphaFlags = LAYR_ALPHAFLAGS_ALPHAONLY;
				SetAnimed(layer.brightness_mult1.AnimRef);
				if (MATtable[i].eye == 1)
					layer.brightness_mult1.value = 0;
			}
			else if (j == MAT_LAYER_ALPHA && 
				( MATtable[i].blend == BM_TRANSPARENT || MATtable[i].blend == BM_ALPHA_BLEND || MATtable[i].blend == BM_ADDITIVE_ALPHA || MATtable[i].blend == BM_TRANSPARENT))
			{
				fulltexName = texName;
				layer.alphaFlags = LAYR_ALPHAFLAGS_ALPHAONLY;

				if (MATtable[i].animid != -1)
					SetAnimed(layer.ar4.AnimRef);
				if (MATtable[i].color != -1)
					SetAnimed(layer.brightness_mult1.AnimRef);
				if (bShowParticle && gameVersion < VERSION_CATACLYSM && i >= partexstart)
					layer.flags |= LAYR_FLAGS_SPLIT;
			}
			extra.names.push_back(fulltexName);
			extra.layers.push_back(layer);
		}

		mat.ar1.AnimRef.animid = CreateAnimID(AR_Mat, i, 0, 1);
		mat.ar2.AnimRef.animid = CreateAnimID(AR_Mat, i, 0, 2);

		switch(MATtable[i].blend)
		{
			case BM_OPAQUE: 
				mat.blendMode = MAT_BLEND_OPAQUE; 
				mat.cutoutThresh = 1;
				break;
			case BM_TRANSPARENT: 
				mat.blendMode = MAT_BLEND_OPAQUE; 
				mat.cutoutThresh = 180;
				break;
			case BM_ALPHA_BLEND: 
				mat.blendMode = MAT_BLEND_ALPHABLEND; 
				mat.cutoutThresh = 16;
				break;
			case BM_ADDITIVE: 
				mat.blendMode = MAT_BLEND_ADD; 
				mat.cutoutThresh = 0;
				break;
			case BM_ADDITIVE_ALPHA: 
				mat.blendMode = MAT_BLEND_ALPHAADD; 
				mat.cutoutThresh = 16;
				break;
			case BM_MODULATE: 
				mat.blendMode = MAT_BLEND_MOD; 
				mat.cutoutThresh = 0;
				break;
			default: 
				mat.blendMode = MAT_BLEND_OPAQUE;
				mat.cutoutThresh = 0;
		}

		if (MATtable[i].flags & RENDERFLAGS_UNLIT)
			mat.flags |= MAT_FLAG_UNSHADED;
		if (MATtable[i].flags & RENDERFLAGS_UNFOGGED)
			mat.flags |= MAT_FLAG_UNFOGGED;
		if (MATtable[i].flags & RENDERFLAGS_TWOSIDED)
			mat.flags |= MAT_FLAG_TWOSIDED;
		if (MATtable[i].flags & RENDERFLAGS_BILLBOARD)
			mat.flags |= MAT_FLAG_DEPTHPREPASS;

		Mats.push_back(mat);
		MatExtras.push_back(extra);
	}

	// init IREF
	for(uint32 i=0; i<Bones.size(); i++) {
		IREF iref;
		memset(&iref, 0, sizeof(iref));
		iref.init();
		if (i >= ROOT_BONE) {
			iref.matrix[3][0] = -m->bones[i-ROOT_BONE].pivot.x;
			iref.matrix[3][1] = m->bones[i-ROOT_BONE].pivot.z;
			iref.matrix[3][2] = -m->bones[i-ROOT_BONE].pivot.y;
		} 
		Irefs.push_back(iref);
	}

/************************************************************************************/
/* Write Data                                                                       */
/************************************************************************************/

	// Modelname
	NameRefEntry(mdata.name, modelName, f);

	// mSEQ
	for(uint32 i=0; i < Seqss.size(); i++) 
		NameRefEntry(Seqss[i].name, nameAnimations[i], f);

	DataRefEntry("SQES", mdata.mSEQS, Seqss.size(), sizeof(SEQS) * Seqss.size(), &Seqss.front(), 1, f);

	// mSTC
	for(uint32 i=0; i < Stcs.size(); i++) {
		// name
		NameRefEntry(Stcs[i].name, StcExtras[i].animname, f);

		// animid
		if (StcExtras[i].animid.size() > 0)
			DataRefEntry("_23U", Stcs[i].animid, StcExtras[i].animid.size(), sizeof(uint32) * StcExtras[i].animid.size(), &StcExtras[i].animid.front(), 0, f);

		// animindex
		if (StcExtras[i].animoff.size() > 0)
			DataRefEntry("_23U", Stcs[i].animindex, StcExtras[i].animoff.size(), sizeof(AnimOffset) * StcExtras[i].animoff.size(), &StcExtras[i].animoff.front(), 0, f);

		// Events, VEDS
		{

			DataRefEntry("_23I", StcExtras[i].animsdevent.timeline, 1, sizeof(int32), &StcExtras[i].animevent.timeline, 0, f);

			NameRefEntry(StcExtras[i].animevent.data.name, StcExtras[i].animevent.name, f);

			DataRefEntry("TNVE", StcExtras[i].animsdevent.data, 1, sizeof(EVNT), &StcExtras[i].animevent.data, 0, f);

			DataRefEntry("VEDS", Stcs[i].Events, 1, sizeof(SD), &StcExtras[i].animsdevent, 0, f);
		}

		// V2DS
		for(uint32 j=0; j<StcExtras[i].animvec2d.size(); j++) {
			int counts = StcExtras[i].animvec2d[j].timeline.size();

			DataRefEntry("_23I", StcExtras[i].animsdvec2d[j].timeline, counts, sizeof(int32) * counts, &StcExtras[i].animvec2d[j].timeline.front(), 0, f);
			DataRefEntry("2CEV", StcExtras[i].animsdvec2d[j].data, counts, sizeof(Vec2D) * counts, &StcExtras[i].animvec2d[j].data.front(), 0, f);
		}
		
		if (StcExtras[i].animvec2d.size() > 0)
			DataRefEntry("V2DS", Stcs[i].arVec2D, StcExtras[i].animvec2d.size(), sizeof(SD) * StcExtras[i].animvec2d.size(), &StcExtras[i].animsdvec2d.front(), 0, f);

		// Trans and Scale, V3DS
		for(uint32 j=0; j<StcExtras[i].animvec3d.size(); j++) {
			int counts = StcExtras[i].animvec3d[j].timeline.size();

			DataRefEntry("_23I", StcExtras[i].animsdvec3d[j].timeline, counts, sizeof(int32) * counts, &StcExtras[i].animvec3d[j].timeline.front(), 0, f);
			DataRefEntry("3CEV", StcExtras[i].animsdvec3d[j].data, counts, sizeof(Vec3D) * counts, &StcExtras[i].animvec3d[j].data.front(), 0, f);
		}
		
		if (StcExtras[i].animvec3d.size() > 0)
			DataRefEntry("V3DS", Stcs[i].arVec3D, StcExtras[i].animvec3d.size(), sizeof(SD) * StcExtras[i].animvec3d.size(), &StcExtras[i].animsdvec3d.front(), 0, f);

		// Rot, Q4DS
		for(uint32 j=0; j<StcExtras[i].animvec4d.size(); j++) {
			int counts = StcExtras[i].animvec4d[j].timeline.size();

			DataRefEntry("_23I", StcExtras[i].animsdvec4d[j].timeline, counts, sizeof(int32) * counts, &StcExtras[i].animvec4d[j].timeline.front(), 0, f);
			DataRefEntry("TAUQ", StcExtras[i].animsdvec4d[j].data, counts, sizeof(Vec4D) * counts, &StcExtras[i].animvec4d[j].data.front(), 0, f);
		}
		
		if (StcExtras[i].animvec4d.size() > 0)
			DataRefEntry("Q4DS", Stcs[i].arQuat, StcExtras[i].animvec4d.size(), sizeof(SD) * StcExtras[i].animvec4d.size(), &StcExtras[i].animsdvec4d.front(), 0, f);

		// Float, 3RDS
		for(uint32 j=0; j<StcExtras[i].animfloat.size(); j++) {
			int counts = StcExtras[i].animfloat[j].timeline.size();

			DataRefEntry("_23I", StcExtras[i].animsdfloat[j].timeline, counts, sizeof(int32) * counts, &StcExtras[i].animfloat[j].timeline.front(), 0, f);
			DataRefEntry("LAER", StcExtras[i].animsdfloat[j].data, counts, sizeof(float) * counts, &StcExtras[i].animfloat[j].data.front(), 0, f);
		}
		
		if (StcExtras[i].animfloat.size() > 0)
			DataRefEntry("3RDS", Stcs[i].arFloat, StcExtras[i].animfloat.size(), sizeof(SD) * StcExtras[i].animfloat.size(), &StcExtras[i].animsdfloat.front(), 0, f);
	}

	DataRefEntry("_CTS", mdata.mSTC, Stcs.size(), sizeof(STC) * Stcs.size(), &Stcs.front(), 4, f);

	// mSTG
	for(uint32 i=0; i<Stgs.size(); i++) {
		// name
		NameRefEntry(Stgs[i].name, nameAnimations[i], f);
		
		// stcID
		DataRefEntry("_23U", Stgs[i].stcID, 1, sizeof(uint32), &i, 0, f);
	}

	DataRefEntry("_GTS", mdata.mSTG, Stgs.size(), sizeof(STG) * Stgs.size(), &Stgs.front(), 0, f);

	// mSTS
	for(uint32 i=0; i<Stss.size(); i++) {
		if (StcExtras[i].animid.size() > 0)
			DataRefEntry("_23U", Stss[i].animid, StcExtras[i].animid.size(), sizeof(uint32) * StcExtras[i].animid.size(), &StcExtras[i].animid.front(), 0, f);
	}
	DataRefEntry("_STS", mdata.mSTS, Stss.size(), sizeof(STS) * Stss.size(), &Stss.front(), 0, f);

	// mBone
	for(uint32 i = 0; i < Bones.size(); i++) 
		NameRefEntry(Bones[i].name, BoneNames[i], f);

	if (Bones.size() > 0)
		DataRefEntry("ENOB", mdata.mBone, Bones.size(), sizeof(BONE) * Bones.size(), &Bones.front(), 1, f);

	// mVert
	DataRefEntry("__8U", mdata.mVert, Verts.size() * sizeof(Vertex32), Verts.size() * sizeof(Vertex32), &Verts.front(), 0, f);

	// mDIV
	// mDiv.Faces
	DataRefEntry("_61U", Div.faces, Faces.size(), sizeof(uint16) * Faces.size(), &Faces.front(), 0, f);

	// mDiv.meash
	DataRefEntry("NGER", Div.REGN, Regns.size(), sizeof(REGN) * Regns.size(), &Regns.front(), 3, f);

	// mDiv.BAT
	if (Bats.size() > 0)
		DataRefEntry("_TAB", Div.BAT, Bats.size(), sizeof(BAT) * Bats.size(), &Bats.front(), 1, f);

	// mDiv.MSEC
	DataRefEntry("CESM", Div.MSEC, 1, sizeof(MSEC), &Msec, 1, f);

	DataRefEntry("_VID", mdata.mDIV, 1, sizeof(DIV), &Div, 2, f);

	// mBoneLU
	if (BoneLookup.size() > 0)
		DataRefEntry("_61U", mdata.mBoneLU, BoneLookup.size(), sizeof(uint16) * BoneLookup.size(), &BoneLookup.front(), 0, f);

	// mAttach
	for(uint32 i=0; i<Atts.size(); i++)
		NameRefEntry(Atts[i].name, AttachNames[i], f);

	if (Atts.size() > 0)
		DataRefEntry("_TTA", mdata.mAttach, Atts.size(), sizeof(ATT) * Atts.size(), &Atts.front(), 1, f);

	// mAttachLU
	if (AttachLoopkup.size() > 0)
		DataRefEntry("_61U", mdata.mAttachLU, AttachLoopkup.size(), sizeof(int16) * AttachLoopkup.size(), &AttachLoopkup.front(), 0, f);

	// mMatLU
	if (Matms.size() > 0)
		DataRefEntry("MTAM", mdata.mMatLU, Matms.size(), sizeof(MATM) * Matms.size(), &Matms.front(), 0, f);

	// mMat
	if (Mats.size() > 0) {
		for(uint32 i=0; i<Mats.size(); i++) {
			// name
			NameRefEntry(Mats[i].name, MatNames[i], f);

			// layers
			for(uint32 j=0; j<13; j++) {
				if (MatExtras[i].names[j].Len() > 0)
					NameRefEntry(MatExtras[i].layers[j].name, MatExtras[i].names[j], f);
				
				DataRefEntry("RYAL", Mats[i].layers[j], 1, sizeof(LAYR), &MatExtras[i].layers[j], 0x16, f);
			}
		}
		DataRefEntry("_TAM", mdata.mMat, Mats.size(), sizeof(MAT) * Mats.size(), &Mats.front(), 0xF, f);
	}

	if (bShowParticle && gameVersion < VERSION_CATACLYSM && Pars.size() > 0)
		DataRefEntry("_RAP", mdata.mPar, Pars.size(), sizeof(PAR) * Pars.size(), &Pars.front(), 12, f);

	// mIREF
	if (Irefs.size() > 0)
		DataRefEntry("FERI", mdata.mIREF, Irefs.size(), sizeof(IREF) * Irefs.size(), &Irefs.front(), 0, f);

	// 4. ReferenceEntry
	fHead.nRefs = (uint32)reList.size();
	fHead.ofsRefs = f.Tell();
	f.Write(&reList.front(), sizeof(ReferenceEntry) * fHead.nRefs);

	// 5. rewrite head
	f.Seek(0, wxFromStart);
	f.Write(&fHead, sizeof(fHead));
	f.Write(&mdata, sizeof(mdata));
	
	// save textures
	wxString texFilename(fn, wxConvUTF8);
	texFilename = texFilename.BeforeLast(SLASH);
	if (modelExport_M3_TexturePath.Len() > 0)
		MakeDirs(texFilename, modelExport_M3_TexturePath);

	for (size_t i=0; i < MATtable.size(); i++)
	{
		int texid = MATtable[i].texid;
		wxString texName;
		texName = m->TextureList[texid];

		if (texdef[texid].type == TEXTURE_BODY)
		{
			glBindTexture(GL_TEXTURE_2D, m->replaceTextures[m->specialTextures[texid]]);
		}
		else
		{
			GLuint bindtex = texturemanager.add(texName);
			glBindTexture(GL_TEXTURE_2D, bindtex);
		}

		texName = texName.BeforeLast('.').AfterLast(SLASH);
		texName.Append(wxT(".tga"));
		
		texName = texFilename + SLASH + modelExport_M3_TexturePath + SLASH + texName;
		//wxLogMessage(wxT("Exporting Image: %s"),texName.c_str());
		SaveTexture(texName);
	}

	mpqf.close();
	mpqfv.close();
	f.Close();
	reList.clear();
}
