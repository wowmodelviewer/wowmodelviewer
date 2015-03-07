#include "next-gen/games/wow/wmo.h"
#include "GameFile.h"
#include "util.h"

#include "logger/Logger.h"

using namespace std;

WMO::WMO(wxString name): ManagedItem(name)
{
  /*
	GameFile f(name);
	ok = !f.isEof();
	if (!ok) {
		wxLogMessage(wxT("Error: Couldn't load WMO %s."), name.c_str());
		f.close();
		return;
	}

	wxLogMessage(wxT("Loading WMO %s"), name.c_str());

	char fourcc[5];
	uint32 size;
	float ff[3];

	char *ddnames = NULL;
	groupnames = 0;
	skybox = 0;
	groups = 0;
	mat = 0;
	doodadset = -1;
	includeDefaultDoodads = true;

	char *texbuf=0;

	while (!f.isEof()) {
		f.read(fourcc,4);
		f.read(&size, 4);

//		flipcc(fourcc); // in former mpq.h
		fourcc[4] = 0;

		size_t nextpos = f.getPos() + size;

		if (!strcmp(fourcc,"MOHD")) {
			// Header for the map object. 64 bytes.
			f.read(&nTextures, 4); // number of materials
			f.read(&nGroups, 4); // number of WMO groups
			f.read(&nP, 4); // number of portals
			f.read(&nLights, 4); // number of lights
			f.read(&nModels, 4); // number of M2 models imported
			f.read(&nDoodads, 4); // number of dedicated files
			f.read(&nDoodadSets, 4); // number of doodad sets
			f.read(&col, 4); // ambient color? RGB
			f.read(&nX, 4); // WMO ID (column 2 in WMOAreaTable.dbc)
			f.read(ff,12); // Bounding box corner 1
			v1 = Vec3D(ff[0],ff[1],ff[2]);
			f.read(ff,12); // Bounding box corner 2
			v2 = Vec3D(ff[0],ff[1],ff[2]);
			f.read(&LiquidType, 4);

			groups = new WMOGroup[nGroups];
			mat = new WMOMaterial[nTextures];
		} else if (!strcmp(fourcc,"MOTX")) {
			// textures
			// The beginning of a string is always aligned to a 4Byte Adress. (0, 4, 8, C). 
			// The end of the string is Zero terminated and filled with zeros until the next aligment. 
			// Sometimes there also empty aligtments for no (it seems like no) real reason.
			texbuf = new char[size];
			f.read(texbuf, size);
		} else if (!strcmp(fourcc,"MOMT")) {
			// materials
			// Materials used in this map object, 64 bytes per texture (BLP file), nMaterials entries.

			for (size_t i=0; i<nTextures; i++) {
				WMOMaterial *m = &mat[i];
				f.read(m, 0x40);

				wxString texpath(texbuf+m->nameStart, wxConvUTF8);
				fixname(texpath);

				m->tex = texturemanager.add(texpath);
				textures.push_back(texpath);
				
				// need repeat turned on
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);


				// material logging
//				gLog("Material %d:\t%d\t%d\t%d\t%X\t%d\t%X\t%d\t%f\t%f",
//					i, m->flags, m->d1, m->transparent, m->col1, m->d3, m->col2, m->d4, m->f1, m->f2);
//				for (size_t j=0; j<5; j++) gLog("\t%d", m->dx[j]);
//				gLog("\t - %s\n", texpath.c_str());

			}
		} else if (!strcmp(fourcc,"MOGN")) {
			// List of group names for the groups in this map object. There are nGroups entries in this chunk.
			// A contiguous block of zero-terminated strings. The names are purely informational, 
			// they aren't used elsewhere (to my knowledge)
			// i think that his realy is zero terminated and just a name list .. but so far im not sure 
			// _what_ else it could be - tharo
			groupnames = new char[size];
			memcpy(groupnames,f.getPointer(),size);
			
			//groupnames = (char*)f.getPointer();
		} else if (!strcmp(fourcc,"MOGI")) {
			// group info - important information! ^_^
			// Group information for WMO groups, 32 bytes per group, nGroups entries.
			for (size_t i=0; i<nGroups; i++) {
				groups[i].init(this, f, (int)i, groupnames);

			}
		} else if (!strcmp(fourcc,"MOLT")) {
			// Lighting information. 48 bytes per light, nLights entries
			for (size_t i=0; i<nLights; i++) {
				WMOLight l;
				l.init(f);
				lights.push_back(l);
			}
		} else if (!strcmp(fourcc,"MODN")) {
			// models ...
			// MMID would be relative offsets for MMDX filenames
			// List of filenames for M2 (mdx) models that appear in this WMO.
			// A block of zero-padded, zero-terminated strings. There are nModels file names in this list. They have to be .MDX!
			if (size) {

				ddnames = (char*)f.getPointer();
				fixnamen(ddnames, size);

				char *p=ddnames,*end=p+size;
				
				while (p<end) {
					wxString path(p, wxConvUTF8);
					p+=strlen(p)+1;
					while ((p<end) && (*p==0)) p++;

					//gWorld->modelmanager.add(path);
					models.push_back(path);
				}
				f.seekRelative((int)size);
			}
		} else if (!strcmp(fourcc,"MODS")) {
			// This chunk defines doodad sets.
			// Doodads in WoW are M2 model files. There are 32 bytes per doodad set, and nSets 
			// entries. Doodad sets specify several versions of "interior decoration" for a WMO. Like, 
			// a small house might have tables and a bed laid out neatly in one set called 
			// "Set_$DefaultGlobal", and have a horrible mess of abandoned broken things in another 
			// set called "Set_Abandoned01". The names are only informative.
			// The doodad set number for every WMO instance is specified in the ADT files.
			for (size_t i=0; i<nDoodadSets; i++) {
				WMODoodadSet dds;
				f.read(&dds, 32);
				doodadsets.push_back(dds);
			}
		} else if (!strcmp(fourcc,"MODD")) {
			// Information for doodad instances. 40 bytes per doodad instance, nDoodads entries.
			// While WMOs and models (M2s) in a map tile are rotated along the axes, doodads within 
			// a WMO are oriented using quaternions! Hooray for consistency!
			// I had to do some tinkering and mirroring to orient the doodads correctly using the 
			// quaternion, see model.cpp in the WoWmapview source code for the exact transform 
			// matrix. It's probably because I'm using another coordinate system, as a lot of other 
			// coordinates in WMOs and models also have to be read as (X,Z,-Y) to work in my system. 
			// But then again, the ADT files have the "correct" order of coordinates. Weird.
			
			nModels = (int)size / 0x28;
			for (size_t i=0; i<nModels; i++) {
				int ofs;
				f.read(&ofs,4); // Offset to the start of the model's filename in the MODN chunk. 
				//Model *m = (Model*)gWorld->modelmanager.items[gWorld->modelmanager.get(ddnames + ofs)];
				WMOModelInstance mi;
				mi.init(ddnames+ofs, f);
				modelis.push_back(mi);
			}

		}
		else if (!strcmp(fourcc,"MOSB")) {
			// Skybox. Always 00 00 00 00. Skyboxes are now defined in DBCs (Light.dbc etc.). 
			// Contained a M2 filename that was used as skybox.
			if (size>4) {
				wxString path = wxString((char*)f.getPointer(), wxConvUTF8);
				fixname(path);
				if (path.length()) {
					//gLog("SKYBOX:\n");

					//sbid = gWorld->modelmanager.add(path);
					//skybox = (Model*)gWorld->modelmanager.items[sbid];

//					if (!skybox->ok) {
//						gWorld->modelmanager.del(sbid);
//						skybox = 0;
					}

				}
			}
		}
		else if (!strcmp(fourcc,"MOPV")) {
			// Portal vertices, 4 * 3 * float per portal, nPortals entries.
			// Portals are (always?) rectangles that specify where doors or entrances are in a WMO. 
			// They could be used for visibility, but I currently have no idea what relations they have 
			// to each other or how they work.
			// Since when "playing" WoW, you're confined to the ground, checking for passing through 
			// these portals would be enough to toggle visibility for indoors or outdoors areas, however, 
			// when randomly flying around, this is not necessarily the case.
			// So.... What happens when you're flying around on a gryphon, and you fly into that 
			// arch-shaped portal into Ironforge? How is that portal calculated? It's all cool as long as 
			// you're inside "legal" areas, I suppose.
			// It's fun, you can actually map out the topology of the WMO using this and the MOPR chunk. 
			// This could be used to speed up the rendering once/if I figure out how.
			WMOPV p;
			for (size_t i=0; i<nP; i++) {
				f.read(ff,12);
				p.a = Vec3D(ff[0],ff[2],-ff[1]);
				f.read(ff,12);
				p.b = Vec3D(ff[0],ff[2],-ff[1]);
				f.read(ff,12);
				p.c = Vec3D(ff[0],ff[2],-ff[1]);
				f.read(ff,12);
				p.d = Vec3D(ff[0],ff[2],-ff[1]);
				pvs.push_back(p);
			}
		}
		else if (!strcmp(fourcc,"MOPR")) {
			// Portal <> group relationship? 2*nPortals entries of 8 bytes.
			// I think this might specify the two WMO groups that a portal connects.
			size_t nn = size / 8;
			WMOPR *pr = (WMOPR*)f.getPointer();
			for (size_t i=0; i<nn; i++) {
				prs.push_back(*pr++);
			}
		}
		else if (!strcmp(fourcc,"MOVV")) {
			// Visible block vertices
			// Just a list of vertices that corresponds to the visible block list.
		}
		else if (!strcmp(fourcc,"MOVB")) {
			// Visible block list
 			// WMOVB p;
		}
		else if (!strcmp(fourcc,"MFOG")) {
			// Fog information. Made up of blocks of 48 bytes.
			size_t nfogs = size / 0x30;
			for (size_t i=0; i<nfogs; i++) {
				WMOFog fog;
				fog.init(f);
				fogs.push_back(fog);
			}
		}
		else if (!strcmp(fourcc,"MFOG")) {
			// optional, Convex Volume Planes. Contains blocks of floating-point numbers.
		}

		f.seek(nextpos);
	}

	f.close();
	delete[] texbuf;

	//for (size_t i=0; i<nGroups; i++) groups[i].initDisplayList();
*/
}

WMO::~WMO()
{
	if (ok) {
		//gLog("Unloading WMO %s\n", name.c_str());
		delete[] groups;

		for (size_t i=0; i<textures.size(); i++) {
            texturemanager.delbyname(textures[i]);
		}

		/*
		for (vector<string>::iterator it = models.begin(); it != models.end(); ++it) {
			//gWorld->modelmanager.delbyname(*it);
		}
		*/

		delete[] mat;

		/*
		if (skybox) {
			delete skybox;
			//gWorld->modelmanager.del(sbid);
		}
		*/

		loadedModels.clear();
		delete groupnames;
	}
}

void WMO::loadGroup(int id)
{
	if (id==-1) {
		for (size_t i=0; i<nGroups; i++) {
			groups[i].initDisplayList();
			groups[i].visible = true;
		}
	}
	else if (id>=0 && (unsigned int)id<nGroups) {
		groups[id].initDisplayList();
		for (size_t i=0; i<nGroups; i++) {
			groups[i].visible = ((int)i==id);
			if ((int)i!=id) groups[i].cleanup();
		}
	}
	updateModels();
}

void WMO::showDoodadSet(int id)
{
	doodadset = id;
	updateModels();
}

void WMO::updateModels()
{
	// 1. look for visible models that aren't loaded
	for (size_t i=0; i<nGroups; i++) if (groups[i].visible) groups[i].updateModels(true);
	// 2. unload unused models
	for (size_t i=0; i<nGroups; i++) if (!groups[i].visible) groups[i].updateModels(false);
}

void WMO::update(int dt)
{
	loadedModels.resetAnim();
	loadedModels.updateEmitters(dt/1000.0f);
}

void WMOGroup::updateModels(bool load)
{
	if (!ddr || !ok || nDoodads==0) 
		return;

	for (size_t i=0; i<nDoodads; i++) {
		short dd = ddr[i];

		bool inSet;

		if (wmo->doodadset==-1) {
			inSet = false;
		} else {
			inSet = ( ((dd >= wmo->doodadsets[wmo->doodadset].start) && (dd < (wmo->doodadsets[wmo->doodadset].start+(int)wmo->doodadsets[wmo->doodadset].size)))
			|| ( wmo->includeDefaultDoodads && (dd >= wmo->doodadsets[0].start) && ((dd < (wmo->doodadsets[0].start+(int)wmo->doodadsets[0].size) )) ) );
		}

		if (inSet) {
			WMOModelInstance &mi = wmo->modelis[dd];

			if (load && !mi.model) 
				mi.loadModel(wmo->loadedModels);
			else if (!load && mi.model) 
				mi.unloadModel(wmo->loadedModels);
		}
	}
}

void WMO::draw()
{
	if (!ok) return;

	glRotatef(viewrot.y, 1, 0, 0);
	glRotatef(viewrot.x, 0, 1, 0);
	glRotatef(viewrot.z, 0, 0, 1);
	glTranslatef(-100,0,0);
	glTranslatef(viewpos.x, viewpos.y, viewpos.z);

	for (size_t i=0; i<nGroups; i++) {
		groups[i].draw();
	}

	for (size_t i=0; i<nGroups; i++) {
		groups[i].drawDoodads(doodadset);
	}

	for (size_t i=0; i<nGroups; i++) {
		groups[i].drawLiquid();
	}

	/*
	// draw light placeholders
	glDisable(GL_LIGHTING);
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
	glBegin(GL_TRIANGLES);
	for (size_t i=0; i<nLights; i++) {
		glColor4fv(lights[i].fcolor);
		glVertex3fv(lights[i].pos);
		glVertex3fv(lights[i].pos + Vec3D(-0.5f,1,0));
		glVertex3fv(lights[i].pos + Vec3D(0.5f,1,0));
	}
	glEnd();
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glEnable(GL_LIGHTING);
	glColor4f(1,1,1,1);
	*/

	/*
	// draw fog positions..?
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	for (size_t i=0; i<fogs.size(); i++) {
		WMOFog &fog = fogs[i];
		glColor4f(1,1,1,1);
		glBegin(GL_LINE_LOOP);
		glVertex3fv(fog.pos);
		glVertex3fv(fog.pos + Vec3D(fog.rad1, 5, -fog.rad2));
		glVertex3fv(fog.pos + Vec3D(fog.rad1, 5, fog.rad2));
		glVertex3fv(fog.pos + Vec3D(-fog.rad1, 5, fog.rad2));
		glVertex3fv(fog.pos + Vec3D(-fog.rad1, 5, -fog.rad2));
		glEnd();
	}
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_LIGHTING);
	*/

	/*
	// draw group boundingboxes
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	for (size_t i=0; i<nGroups; i++) {
		WMOGroup &g = groups[i];
		float fc[2] = {1,0};
		glColor4f(fc[i%2],fc[(i/2)%2],fc[(i/3)%2],1);
		glBegin(GL_LINE_LOOP);

		glVertex3f(g.b1.x, g.b1.y, g.b1.z);
		glVertex3f(g.b1.x, g.b2.y, g.b1.z);
		glVertex3f(g.b2.x, g.b2.y, g.b1.z);
		glVertex3f(g.b2.x, g.b1.y, g.b1.z);

		glVertex3f(g.b2.x, g.b1.y, g.b2.z);
		glVertex3f(g.b2.x, g.b2.y, g.b2.z);
		glVertex3f(g.b1.x, g.b2.y, g.b2.z);
		glVertex3f(g.b1.x, g.b1.y, g.b2.z);

		glEnd();
	}
	// draw portal relations
	glBegin(GL_LINES);
	for (size_t i=0; i<prs.size(); i++) {
		WMOPR &pr = prs[i];
		WMOPV &pv = pvs[pr.portal];
		if (pr.dir>0) glColor4f(1,0,0,1);
		else glColor4f(0,0,1,1);
		Vec3D pc = (pv.a+pv.b+pv.c+pv.d)*0.25f;
		Vec3D gc = (groups[pr.group].b1 + groups[pr.group].b2)*0.5f;
		glVertex3fv(pc);
		glVertex3fv(gc);
	}
	glEnd();
	glColor4f(1,1,1,1);
	*/
	drawPortals();
}

void WMO::drawSkybox()
{
	if (skybox) {
		// TODO: only draw sky if we are "inside" the WMO... ?

		// We need to clear the depth buffer, because the skybox model can (will?)
		// require it *. This is inefficient - is there a better way to do this?
		// * planets in front of "space" in Caverns of Time
		//glClear(GL_DEPTH_BUFFER_BIT);

		// update: skybox models seem to have an explicit renderop ordering!
		// that saves us the depth buffer clear and the depth testing, too

		/*
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glPushMatrix();
		Vec3D o = gWorld->camera;
		glTranslatef(o.x, o.y, o.z);
		const float sc = 2.0f;
		glScalef(sc,sc,sc);
        skybox->draw();
		glPopMatrix();
		gWorld->hadSky = true;
		glEnable(GL_DEPTH_TEST);
		*/
	}
}

void WMO::drawPortals()
{
	/*
	// not used ;)
	glBegin(GL_QUADS);
	for (size_t i=0; i<nP; i++) {
		glVertex3fv(pvs[i].d);
		glVertex3fv(pvs[i].c);
		glVertex3fv(pvs[i].b);
		glVertex3fv(pvs[i].a);
	}
	glEnd();
	*/
}

void WMOLight::init(GameFile &f)
{
/*
	I haven't quite figured out how WoW actually does lighting, as it seems much smoother than 
	the regular vertex lighting in my screenshots. The light paramters might be range or attenuation 
	information, or something else entirely. Some WMO groups reference a lot of lights at once.
	The WoW client (at least on my system) uses only one light, which is always directional. 
	Attenuation is always (0, 0.7, 0.03). So I suppose for models/doodads (both are M2 files anyway) 
	it selects an appropriate light to turn on. Global light is handled similarly. Some WMO textures 
	(BLP files) have specular maps in the alpha channel, the pixel shader renderpath uses these. 
	Still don't know how to determine direction/color for either the outdoor light or WMO local lights... :)
*/

	char pad;
	f.read(&lighttype,1); // LightType
	f.read(&type,1);
	f.read(&useatten,1);
	f.read(&pad,1);
	f.read(&color,4);
	f.read(pos, 12);
	f.read(&intensity, 4);
	f.read(&attenStart, 4);
	f.read(&attenEnd, 4);
	f.read(unk, 4*3);	// Seems to be -1, -0.5, X, where X changes from model to model. Guard Tower: 2.3611112, GoldshireInn: 5.8888888, Duskwood_TownHall: 5
	f.read(&r,4);

	pos = Vec3D(pos.x, pos.z, -pos.y);

	// rgb? bgr? hm
	float fa = ((color & 0xff000000) >> 24) / 255.0f;
	float fr = ((color & 0x00ff0000) >> 16) / 255.0f;
	float fg = ((color & 0x0000ff00) >>  8) / 255.0f;
	float fb = ((color & 0x000000ff)      ) / 255.0f;

	fcolor = Vec4D(fr,fg,fb,fa);
	fcolor *= intensity;
	fcolor.w = 1.0f;

	/*
	// light logging
	gLog("Light %08x @ (%4.2f,%4.2f,%4.2f)\t %4.2f, %4.2f, %4.2f, %4.2f, %4.2f, %4.2f, %4.2f\t(%d,%d,%d,%d)\n",
		color, pos.x, pos.y, pos.z, intensity,
		unk[0], unk[1], unk[2], unk[3], unk[4], r,
		type[0], type[1], type[2], type[3]);
	*/
}

void WMOLight::setup(GLint light)
{
	// not used right now -_-

	GLfloat LightAmbient[] = {0, 0, 0, 1.0f};
	GLfloat LightPosition[] = {pos.x, pos.y, pos.z, 0.0f};

	glLightfv(light, GL_AMBIENT, LightAmbient);
	glLightfv(light, GL_DIFFUSE, fcolor);
	glLightfv(light, GL_POSITION,LightPosition);

	glEnable(light);
}

void WMOLight::setupOnce(GLint light, Vec3D dir, Vec3D lcol)
{
	Vec4D position(dir, 0);
	//Vec4D position(0,1,0,0);

	Vec4D ambient = Vec4D(lcol * 0.3f, 1);
	//Vec4D ambient = Vec4D(0.101961f, 0.062776f, 0, 1);
	Vec4D diffuse = Vec4D(lcol, 1);
	//Vec4D diffuse = Vec4D(0.439216f, 0.266667f, 0, 1);

	glLightfv(light, GL_AMBIENT, ambient);
	glLightfv(light, GL_DIFFUSE, diffuse);
	glLightfv(light, GL_POSITION,position);

	glEnable(light);
}



void WMOGroup::init(WMO *wmo, GameFile &f, int num, char *names)
{
	/*
		Groups don't have placement or orientation information, because the coordinates for the 
		vertices in the additional .WMO files are already correctly transformed relative to (0,0,0) 
		which is the entire WMO's base position in model space.
		The name offsets seem to be incorrect (or something else entirely?). The correct name 
		offsets are in the WMO group file headers. (along with more descriptive names for some groups)
		It is just the index. You have to find the offsets by yourself. --Schlumpf 10:17, 31 July 2007 (CEST)
		The flags for the groups seem to specify whether it is indoors/outdoors, probably to 
		choose what kind of lighting to use. Not fully understood. "Indoors" and "Outdoors" are flags 
		used to tell the client whether certain spells can be cast and abilities used. (Example: Entangling 
		Roots cannot be used indoors).
	*/

	this->wmo = wmo;
	this->num = num;

	// extract group info from f
	f.read(&flags,4);
	float ff[3];
	f.read(ff,12); // Bounding box corner 1
	v1 = Vec3D(ff[0],ff[1],ff[2]);
	f.read(ff,12); // Bounding box corner 2
	v2 = Vec3D(ff[0],ff[1],ff[2]);
	int nameOfs;
	f.read(&nameOfs,4); // name in MOGN chunk (or -1 for no name?)

	// TODO: get proper name from group header and/or dbc?
	/*
	if (nameOfs > 0) {
        name = string(names + nameOfs);
	} else name = "(no name)";
	*/

	ddr = 0;
	nDoodads = 0;

	//lq = 0;

	ok = false;
	visible = false;
}

void setGLColor(unsigned int col)
{
	//glColor4ubv((GLubyte*)(&col));
	GLubyte r,g,b;
	r = (col & 0x00FF0000) >> 16;
	g = (col & 0x0000FF00) >> 8;
	b = (col & 0x000000FF);
    glColor4ub(r,g,b,1);
}

/*
The fields referenced from the MOPR chunk indicate portals leading out of the WMO group in question.
For the "Number of batches" fields, A + B + C == the total number of batches in the WMO group (in the MOBA chunk). This might be some kind of LOD thing, or just separating the batches into different types/groups...?
Flags: always contain more information than flags in MOGI. I suppose MOGI only deals with topology/culling, while flags here also include rendering info.
Flag		Meaning
0x1 		something with bounding
0x4 		Has vertex colors (MOCV chunk)
0x8 		Outdoor
0x40
0x200 		Has lights  (MOLR chunk)
0x400		
0x800 		Has doodads (MODR chunk)
0x1000 	        Has water   (MLIQ chunk)
0x2000		Indoor
0x8000
0x20000		Parses some additional chunk. No idea what it is. Oo
0x40000	        Show skybox
0x80000		isNotOcean, LiquidType related, see below in the MLIQ chunk.
*/
struct WMOGroupHeader {
    int nameStart; // Group name (offset into MOGN chunk)
	int nameStart2; // Descriptive group name (offset into MOGN chunk)
	int flags;
	float box1[3]; // Bounding box corner 1 (same as in MOGI)
	float box2[3]; // Bounding box corner 2
	short portalStart; // Index into the MOPR chunk
	short portalCount; // Number of items used from the MOPR chunk
	short batches[4];
	uint8 fogs[4]; // Up to four indices into the WMO fog list
	int32 unk1; // LiquidType related, see below in the MLIQ chunk.
	int32 id; // WMO group ID (column 4 in WMOAreaTable.dbc)
	int32 unk2; // Always 0?
	int32 unk3; // Always 0?
};

void WMOGroup::initDisplayList()
{
  /*
	vertices = NULL;
	normals = NULL;
	texcoords = NULL;
	indices = NULL;
	materials = NULL;
	batches = 0;
	nBatches = 0;

	WMOGroupHeader gh;

	short *useLights = 0;
	int nLR = 0;

	// open group file
	wxString temp(wmo->name.c_str(), wxConvUTF8);
	temp = temp.BeforeLast(wxT('.'));
	
	wxString fname;
	fname.Printf(wxT("%s_%03d.wmo"), temp.c_str(), num);

	GameFile gf(fname);
    gf.seek(0x14);

	// read header
	gf.read(&gh, sizeof(WMOGroupHeader));
	WMOFog &wf = wmo->fogs[gh.fogs[0]];
	if (wf.r2 <= 0)
		fog = -1; // default outdoor fog..?
	else 
		fog = gh.fogs[0];

	name = wxString(wmo->groupnames + gh.nameStart, wxConvUTF8);
	desc = wxString(wmo->groupnames + gh.nameStart2, wxConvUTF8);

	b1 = Vec3D(gh.box1[0], gh.box1[2], -gh.box1[1]);
	b2 = Vec3D(gh.box2[0], gh.box2[2], -gh.box2[1]);

	gf.seek(0x58); // first chunk
	char fourcc[5];
	uint32 size;

	cv = 0;
	hascv = false;

	while (!gf.isEof()) {
		gf.read(fourcc,4);
		gf.read(&size, 4);

//		flipcc(fourcc); // in former mpq.h
		fourcc[4] = 0;

		size_t nextpos = gf.getPos() + size;

		// why copy stuff when I can just map it from memory ^_^

		if (!strcmp(fourcc,"MOPY")) {
			
//			Material info for triangles, two bytes per triangle. So size of this chunk in bytes is twice the number of triangles in the WMO group.
//			Offset	Type	Description
//			0x00	uint8	Flags?
//			0x01	uint8	Material ID
//			struct SMOPoly // 03-29-2005 By ObscuR ( Maybe not accurate :p )
//			{
//				enum
//				{
//					F_NOCAMCOLLIDE,
//					F_DETAIL,
//					F_COLLISION,
//					F_HINT,
//					F_RENDER,
//					F_COLLIDE_HIT,
//				};
//			000h  uint8 flags;
//			001h  uint8 mtlId;
//			002h
//			};
//
//			Frequently used flags are 0x20 and 0x40, but I have no idea what they do.
//			Flag	Description
//			0x00	?
//			0x01	?
//			0x04	no collision
//			0x08	?
//			0x20	?
//			0x40	?
//			Material ID specifies an index into the material table in the root WMO file's MOMT chunk. Some of the triangles have 0xFF for the material ID, I skip these. (but there might very well be a use for them?)
//			The triangles with 0xFF Material ID seem to be a simplified mesh. Like for collision detection or something like that. At least stairs are flattened to ramps if you only display these polys. --shlainn 7 Jun 2009
//			0xFF representing -1 is used for collision-only triangles. They aren't rendered but have collision. Problem with it: WoW seems to cast and reflect light on them. Its a bug in the engine. --schlumpf_ 20:40, 7 June 2009 (CEST)
//			Triangles stored here are more-or-less pre-sorted by texture, so it's ok to draw them sequentially.


			// materials per triangle
			nTriangles = (uint32)(size / 2);
			materials = new uint16[nTriangles];
			gf.read(materials, size);
			//memcpy(materials, gf.getPointer(), size);
		}
		else if (!strcmp(fourcc,"MOVI")) {

//			Vertex indices for triangles. Three 16-bit integers per triangle, that are indices into the vertex list. The numbers specify the 3 vertices for each triangle, their order makes it possible to do backface culling.

			nIndices = (size / 2);
			indices = new uint16[nIndices];
			gf.read(indices, size);
		}
		else if (!strcmp(fourcc,"MOVT")) {

//			Vertices chunk. 3 floats per vertex, the coordinates are in (X,Z,-Y) order. It's likely that WMOs and models (M2s) were created in a coordinate system with the Z axis pointing up and the Y axis into the screen, whereas in OpenGL, the coordinate system used in WoWmapview the Z axis points toward the viewer and the Y axis points up. Hence the juggling around with coordinates.

			nVertices = (size / 12);
			// let's hope it's padded to 12 bytes, not 16...
			vertices = new Vec3D[nVertices];
			gf.read(vertices, size);
			vmin = Vec3D( 9999999.0f, 9999999.0f, 9999999.0f);
			vmax = Vec3D(-9999999.0f,-9999999.0f,-9999999.0f);
			rad = 0;
			for (size_t i=0; i<nVertices; i++) {
				Vec3D v(vertices[i].x, vertices[i].z, -vertices[i].y);
				if (v.x < vmin.x) vmin.x = v.x;
				if (v.y < vmin.y) vmin.y = v.y;
				if (v.z < vmin.z) vmin.z = v.z;
				if (v.x > vmax.x) vmax.x = v.x;
				if (v.y > vmax.y) vmax.y = v.y;
				if (v.z > vmax.z) vmax.z = v.z;
			}
			center = (vmax + vmin) * 0.5f;
			rad = (vmax-center).length();
		}
		else if (!strcmp(fourcc,"MONR")) {
			// Normals. 3 floats per vertex normal, in (X,Z,-Y) order.
			uint32 tSize = (uint32)(size/12);
			normals = new Vec3D[tSize];
			gf.read(normals, size);
		}
		else if (!strcmp(fourcc,"MOTV")) {
			// Texture coordinates, 2 floats per vertex in (X,Y) order. The values range from 0.0 to 1.0. Vertices, normals and texture coordinates are in corresponding order, of course.
			uint32 tSize = (uint32)(size/8);
			texcoords = new Vec2D[tSize];
			gf.read(texcoords, size);
		}
		else if (!strcmp(fourcc,"MOLR")) {

//			Light references, one 16-bit integer per light reference.
//			This is basically a list of lights used in this WMO group, the numbers are indices into the WMO root file's MOLT table.
//			For some WMO groups there is a large number of lights specified here, more than what a typical video card will handle at once. I wonder how they do lighting properly. Currently, I just turn on the first GL_MAX_LIGHTS and hope for the best. :(

			nLR = (int)size / 2;
			useLights =  (short*)gf.getPointer();
		}
		else if (strcmp(fourcc,"MODR")==0) {

//			Doodad references, one 16-bit integer per doodad.
//			The numbers are indices into the doodad instance table (MODD chunk) of the WMO root file. These have to be filtered to the doodad set being used in any given WMO instance.

			nDoodads = (int)(size/2);
			ddr = new short[nDoodads];
			gf.read(ddr,size);
		}
		else if (strcmp(fourcc,"MOBN")==0) {

//			Array of t_BSP_NODE.
//			struct t_BSP_NODE
//			{
//				short planetype;		  // unsure
//				short children[2];		  // index of bsp child node(right in this array)
//				unsigned short numfaces;  // num of triangle faces in  MOBR
//				unsigned short firstface; // index of the first triangle index(in  MOBR)
//				short nUnk; 		  // 0
//				float fDist;
//			};
//			// The numfaces and firstface define a polygon plane.
//													2005-4-4 by linghuye
//			This+BoundingBox(in wmo_root.MOGI) is used for Collision --Tigurius

		}
		else if (strcmp(fourcc,"MOBR")==0) {
			// Triangle indices (in MOVI which define triangles) to describe polygon planes defined by MOBN BSP nodes.
		}
		else if (!strcmp(fourcc,"MODR")) {

//			Doodad references, one 16-bit integer per doodad.
//			The numbers are indices into the doodad instance table (MODD chunk) of the WMO root file. These have to be filtered to the doodad set being used in any given WMO instance.

			if (ddr) delete ddr;
			nDoodads = (int)(size / 2);
			ddr = new short[nDoodads];
			gf.read(ddr,size);
		}
		else if (!strcmp(fourcc,"MOBA")) {
//			Render batches. Records of 24 bytes.
//			struct SMOBatch // 03-29-2005 By ObscuR
//			{
//				enum
//				{
//					F_RENDERED
//				};
//				?? lightMap;
//				?? texture;
//				?? bx;
//				?? by;
//				?? bz;
//				?? tx;
//				?? ty;
//				?? tz;
//				?? startIndex;
//				?? count;
//				?? minIndex;
//				?? maxIndex;
//				?? flags;
//			};
//			For the enUS, enGB versions, it seems to be different from the preceding struct:
//			Offset	Type		Description
//			0x00	uint32		Some color?
//			0x04	uint32		Some color?
//			0x08	uint32		Some color?
//			0x0C	uint32		Start index
//			0x10	uint16		Number of indices
//			0x12	uint16		Start vertex
//			0x14	uint16		End vertex
//			0x16	uint8		0?
//			0x17	uint8		Texture
//
//			Flags
//			0x1		Unknown
//			0x4		Unknown

			nBatches = (uint32)(size/24);
			batches = new WMOBatch[nBatches];
			memcpy(batches, gf.getPointer(), size);


//			// batch logging
//			gLog("\nWMO group #%d - %s\nVertices: %d\nTriangles: %d\nIndices: %d\nBatches: %d\n",
//				this->num, this->name.c_str(), nVertices, nTriangles, nTriangles*3, nBatches);
//			WMOBatch *ba = batches;
//			for (size_t i=0; i<nBatches; i++) {
//				gLog("Batch %d:\t", i);
//
//				for (size_t j=0; j<12; j++) {
//					if ((j%4)==0 && j!=0) gLog("| ");
//					gLog("%d\t", ba[i].bytes[j]);
//				}
//
//				gLog("| %d\t%d\t| %d\t%d\t", ba[i].indexStart, ba[i].indexCount, ba[i].vertexStart, ba[i].vertexEnd);
//				gLog("%d\t%d\t%s\n", ba[i].flags, ba[i].texture, wmo->textures[ba[i].texture].c_str());
//
//			}
//			int l = nBatches-1;
//			gLog("Max index: %d\n", ba[l].indexStart + ba[l].indexCount);
		}
		else if (!strcmp(fourcc,"MOCV")) {
			size_t spos = gf.getPos();
//			Vertex colors, 4 bytes per vertex (BGRA), for WMO groups using indoor lighting.
//			I don't know if this is supposed to work together with, or replace, the lights referenced in MOLR. But it sure is the only way for the ground around the goblin smelting pot to turn red in the Deadmines. (but some corridors are, in turn, too dark - how the hell does lighting work anyway, are there lightmaps hidden somewhere?)
//			- I'm pretty sure WoW does not use lightmaps in it's WMOs...
//			After further inspection, this is it, actual pre-lit vertex colors for WMOs - vertex lighting is turned off. This is used if flag 0x2000 in the MOGI chunk is on for this group. This pretty much fixes indoor lighting in Ironforge and Undercity. The "light" lights are used only for M2 models (doodads and characters). (The "too dark" corridors seemed like that because I was looking at it in a window - in full screen it looks pretty much the same as in the game) Now THAT's progress!!!

			//gLog("CV: %d\n", size);
			hascv = true;
			cv = (unsigned int*)gf.getPointer();
			wxLogMessage(wxT("Original Vertex Colors Gathered."));

			// Temp, until we get this fully working.
			gf.seek(spos);
			wxLogMessage(wxT("Gathering New Vertex Colors..."));
			VertexColors = new WMOVertColor[nVertices];
			memcpy(VertexColors, gf.getPointer(), size);
//			for (size_t x=0;x<nVertices;x++){
//				WMOVertColor vc;
//				gf.read(&vc,4);
//				//wxLogMessage("Vertex Colors Gathered. R:%03i, G:%03i, B:%03i, A:%03i",vc.r,vc.g,vc.b,vc.a);
//				VertexColors.push_back(vc);
//			}

		}
		else if (!strcmp(fourcc,"MLIQ")) {
			// liquids
			WMOLiquidHeader hlq;
			gf.read(&hlq, 0x1E);

			//gLog("WMO Liquid: %dx%d, %dx%d, (%f,%f,%f) %d\n", hlq.X, hlq.Y, hlq.A, hlq.B, hlq.pos.x, hlq.pos.y, hlq.pos.z, hlq.type);

			//lq = new Liquid(hlq.A, hlq.B, Vec3D(hlq.pos.x, hlq.pos.z, -hlq.pos.y));
			//lq->initFromWMO(gf, wmo->mat[hlq.type], (flags&0x2000)!=0);
		}

		// TODO: figure out/use MFOG ?

 		gf.seek(nextpos);
	}

	// ok, make a display list

	indoor = (flags&8192)!=0;
	//gLog("Lighting: %s %X\n\n", indoor?"Indoor":"Outdoor", flags);

	initLighting(nLR,useLights);

	dl = glGenLists(1);
	glNewList(dl, GL_COMPILE);
	glDisable(GL_BLEND);

	glColor4f(1,1,1,1);

//	float xr=0,xg=0,xb=0;
//	if (flags & 0x0040) xr = 1;
//	if (flags & 0x2000) xg = 1;
//	if (flags & 0x8000) xb = 1;
//	glColor4f(xr,xg,xb,1);

	// assume that texturing is on, for unit 1

	IndiceToVerts = new uint32[nIndices]+2;

	for (size_t b=0; b<nBatches; b++) {
		WMOBatch *batch = &batches[b];
		WMOMaterial *mat = &wmo->mat[batch->texture];

		// build indice to vert array.
		//wxLogMessage("Indice to Vert Conversion Array for Batch %i:",b);
		for (size_t i=0;i<=batch->indexCount;i++){
			size_t a = indices[batch->indexStart + i];
			for (size_t j=batch->vertexStart;j<=batch->vertexEnd;j++){
				if (vertices[a] == vertices[j]){
					IndiceToVerts[batch->indexStart + i] = j;
					//wxLogMessage(wxT("Indice %i = Vert %i"),batch->indexStart + i,j);
					break;
				}
			}
		}

        // setup texture
		glBindTexture(GL_TEXTURE_2D, mat->tex);

		bool atest = (mat->transparent) != 0;

		if (atest) {
			glEnable(GL_ALPHA_TEST);
			float aval = 0;
            if (mat->flags & 0x80) aval = 0.3f;
			if (mat->flags & 0x01) aval = 0.0f;
			glAlphaFunc(GL_GREATER, aval);
		}

		if (mat->flags & WMO_MATERIAL_CULL) 
			glDisable(GL_CULL_FACE);
		else 
			glEnable(GL_CULL_FACE);

//		float fr,fg,fb;
//		fr = rand()/(float)RAND_MAX;
//		fg = rand()/(float)RAND_MAX;
//		fb = rand()/(float)RAND_MAX;
//		glColor4f(fr,fg,fb,1);

		bool overbright = ((mat->flags & 0x10) && !hascv);
		if (overbright) {
			// TODO: use emissive color from the WMO Material instead of 1,1,1,1
			GLfloat em[4] = {1,1,1,1};
			glMaterialfv(GL_FRONT, GL_EMISSION, em);
		}

		// render
		glBegin(GL_TRIANGLES);
		for (size_t t=0, i=batch->indexStart; t<batch->indexCount; t++,i++) {
			int a = indices[i];
			if (indoor && hascv) {
	            setGLColor(cv[a]);
			}
			glNormal3f(normals[a].x, normals[a].z, -normals[a].y);
			glTexCoord2fv(texcoords[a]);
			glVertex3f(vertices[a].x, vertices[a].z, -vertices[a].y);
		}
		glEnd();

		if (overbright) {
			GLfloat em[4] = {0,0,0,1};
			glMaterialfv(GL_FRONT, GL_EMISSION, em);
		}

		if (atest) {
			glDisable(GL_ALPHA_TEST);
		}
	}

	glColor4f(1,1,1,1);
	glEnable(GL_CULL_FACE);

	glEndList();

	gf.close();

	// hmm
	indoor = false;

	ok = true;
	*/
}


void WMOGroup::initLighting(int nLR, short *useLights)
{
	dl_light = 0;
	// "real" lighting?
	if ((flags&0x2000) && hascv) {

		Vec3D dirmin(1,1,1);
		float lenmin;
		int lmin;

		for (size_t i=0; i<nDoodads; i++) {
			lenmin = 999999.0f*999999.0f;
			lmin = 0;
			WMOModelInstance &mi = wmo->modelis[ddr[i]];
			for (size_t j=0; j<wmo->nLights; j++) {
				WMOLight &l = wmo->lights[j];
				Vec3D dir = l.pos - mi.pos;
				float ll = dir.lengthSquared();
				if (ll < lenmin) {
					lenmin = ll;
					dirmin = dir;
					lmin = (int)j;
				}
			}
			mi.light = lmin;
			mi.ldir = dirmin;
		}

		outdoorLights = false;
	} else {
		outdoorLights = true;
	}
}

void WMOGroup::draw()
{
	if (!ok) {
		visible = false;
		return;
	}

	visible = true;

	if (hascv) {
		glDisable(GL_LIGHTING);
	}
	//setupFog();

	glCallList(dl);

	if (hascv) {
		glEnable(GL_LIGHTING);
	}
}

void WMOGroup::drawDoodads(int doodadset)
{
	if (!visible) return;
	if (nDoodads==0) return;
	if (doodadset<0) return;

	//setupFog();

	/*
	float xr=0,xg=0,xb=0;
	if (flags & 0x0040) xr = 1;
	//if (flags & 0x0008) xg = 1;
	if (flags & 0x8000) xb = 1;
	glColor4f(xr,xg,xb,1);
	*/

	// draw doodads
	glColor4f(1,1,1,1);
	for (size_t i=0; i<nDoodads; i++) {
		short dd = ddr[i];
		
		bool inSet;
		if (doodadset==-1) {
			inSet = false;
		} else {
			inSet = ( ((dd >= wmo->doodadsets[doodadset].start) && (dd < (wmo->doodadsets[doodadset].start+(int)wmo->doodadsets[doodadset].size))) 
			|| ( wmo->includeDefaultDoodads && (dd >= wmo->doodadsets[0].start) && ((dd < (wmo->doodadsets[0].start+(int)wmo->doodadsets[0].size) )) ) );
		}

		if (inSet) {

			WMOModelInstance &mi = wmo->modelis[dd];

			if (!outdoorLights) {
				glDisable(GL_LIGHT0);
				WMOLight::setupOnce(GL_LIGHT2, mi.ldir, mi.lcol);
			} else {
				glEnable(GL_LIGHT0);
			}

			wmo->modelis[dd].draw();
		}
	}

	glDisable(GL_LIGHT2);

	glColor4f(1,1,1,1);

}

void WMOGroup::drawLiquid()
{
	if (!visible) 
		return;

	/*
	// draw liquid
	// TODO: culling for liquid boundingbox or something
	if (lq) {
		setupFog();
		if (outdoorLights) {
			//gWorld->outdoorLights(true);
		} else {
			// TODO: setup some kind of indoor lighting... ?
			//gWorld->outdoorLights(false);
			glEnable(GL_LIGHT2);
			glLightfv(GL_LIGHT2, GL_AMBIENT, Vec4D(0.1f,0.1f,0.1f,1));
			glLightfv(GL_LIGHT2, GL_DIFFUSE, Vec4D(0.8f,0.8f,0.8f,1));
			glLightfv(GL_LIGHT2, GL_POSITION, Vec4D(0,1,0,0));
		}
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
		glDepthMask(GL_TRUE);
		glColor4f(1,1,1,1);
		lq->draw();
		glDisable(GL_LIGHT2);
	}
	*/
}

void WMOGroup::setupFog()
{
	/*
	if (outdoorLights || fog==-1) {
		gWorld->setupFog();
	} else {
		wmo->fogs[fog].setup();
	}
	*/
}

WMOGroup::WMOGroup() :
dl(0), ddr(0), vertices(NULL), normals(NULL), texcoords(NULL),
indices(NULL), materials(NULL), nTriangles(0), nVertices(0),
nIndices(0), nBatches(0)
{
}

WMOGroup::~WMOGroup()
{
	cleanup();
	if (ddr) delete ddr;
}

void WMOGroup::cleanup()
{
	if (dl) glDeleteLists(dl, 1);
	dl = 0;
	if (dl_light) glDeleteLists(dl_light, 1);
	dl_light = 0;
	//if (lq) delete lq; lq = 0;
	ok = false;

	delete vertices;
	delete normals;
	delete texcoords;
	delete indices;
	delete materials;
	delete batches;
}

void WMOFog::init(GameFile &f)
{
	f.read(this, 0x30);
	color = Vec4D( ((color1 & 0x00FF0000) >> 16)/255.0f, ((color1 & 0x0000FF00) >> 8)/255.0f,
					(color1 & 0x000000FF)/255.0f, ((color1 & 0xFF000000) >> 24)/255.0f);
	float temp;
	temp = pos.y;
	pos.y = pos.z;
	pos.z = -temp;
	fogstart = fogstart * fogend;
}

void WMOFog::setup()
{
	/*
	if (gWorld->drawfog) {
		glFogfv(GL_FOG_COLOR, color);
		glFogf(GL_FOG_START, fogstart);
		glFogf(GL_FOG_END, fogend);

		glEnable(GL_FOG);
	} else {
		glDisable(GL_FOG);
	}
	*/
}

/*

int WMOManager::add(wxString name)
{
	int id;
	if (names.find(name) != names.end()) {
		id = names[name];
		items[id]->addref();
		//gLog("Loading WMO %s [already loaded]\n",name.c_str());
		return id;
	}

	// load new
	WMO *wmo = new WMO(name);
	id = nextID();
    do_add(name, id, wmo);
    return id;
}



WMOInstance::WMOInstance(WMO *wmo, GameFile &f) : wmo (wmo)
{
	float ff[3];
    f.read(&id, 4);
	f.read(ff,12);
	pos = Vec3D(ff[0],ff[1],ff[2]);
	f.read(ff,12);
	dir = Vec3D(ff[0],ff[1],ff[2]);
	f.read(ff,12);
	pos2 = Vec3D(ff[0],ff[1],ff[2]);
	f.read(ff,12);
	pos3 = Vec3D(ff[0],ff[1],ff[2]);
	f.read(&d2,4);
	f.read(&d3,4);

	doodadset = (d2 & 0xFFFF0000) >> 16;

	//gLog("WMO instance: %s (%d, %d)\n", wmo->name.c_str(), d2, d3);
}

void WMOInstance::draw()
{
	if (ids.find(id) != ids.end()) return;
	ids.insert(id);

	glPushMatrix();
	glTranslatef(pos.x, pos.y, pos.z);

	float rot = -90.0f + dir.y;

	// TODO: replace this with a single transform matrix calculated at load time

	glRotatef(dir.y - 90.0f, 0, 1, 0);
	glRotatef(-dir.x, 0, 0, 1);
	glRotatef(dir.z, 1, 0, 0);

	wmo->draw(doodadset,pos,-rot);

	glPopMatrix();
}

void WMOInstance::reset()
{
    ids.clear();
}

std::set<int> WMOInstance::ids;

*/


void WMOModelInstance::init(char *fname, GameFile &f)
{
	filename = wxString(fname, wxConvUTF8);
	model = 0;

	float ff[3],temp;
	f.read(ff,12); // Position (X,Z,-Y)
	pos = Vec3D(ff[0],ff[1],ff[2]);
	temp = pos.z;
	pos.z = -pos.y;
	pos.y = temp;
	f.read(&w,4); // W component of the orientation quaternion
	f.read(ff,12); // X, Y, Z components of the orientaton quaternion
	dir = Vec3D(ff[0],ff[1],ff[2]);
	f.read(&sc,4); // Scale factor
	f.read(&d1,4); // (B,G,R,A) Lightning-color. 
	lcol = Vec3D(((d1&0xff0000)>>16) / 255.0f, ((d1&0x00ff00)>>8) / 255.0f, (d1&0x0000ff) / 255.0f);
}

void glQuaternionRotate(const Vec3D& vdir, float w)
{
	Matrix m;
	Quaternion q(vdir, w);
	m.quaternionRotate(q);
	glMultMatrixf(m);
}

void WMOModelInstance::draw()
{
	if (!model) return;

	glPushMatrix();

	glTranslatef(pos.x, pos.y, pos.z);
	Vec3D vdir(-dir.z,dir.x,dir.y);
	glQuaternionRotate(vdir,w);
	glScalef(sc,-sc,-sc);

	model->draw();
	glPopMatrix();
}

void WMOModelInstance::loadModel(ModelManager &mm)
{
    model = (WoWModel*)mm.items[mm.add(filename)];
	model->isWMO = true;
}

void WMOModelInstance::unloadModel(ModelManager &mm)
{
	mm.delbyname(filename);
	model = 0;
}
