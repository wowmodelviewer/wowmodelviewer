#include "wmo.h"

#include "CASCFile.h"
#include "Game.h"
#include "WMOGroup.h"

#include "logger/Logger.h"

#include <algorithm>


using namespace std;

void WMO::flipcc(QString & cc)
{
  QByteArray ba = cc.toLatin1();
  char *d = ba.data();
  std::reverse(d, d + cc.length());
  cc = QString(d);
}

WMO::WMO(QString name) : 
  ManagedItem(name),
  maxCoord(), 
  minCoord()
{
  CASCFile f(name);
  f.open();
	ok = !f.isEof();
	if (!ok) {
		LOG_ERROR << "Couldn't load WMO" << name;
		f.close();
		return;
	}

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
    char buf[5];
    f.read(buf, 4);
    buf[4] = 0;

    QString fourcc = QString::fromLatin1(buf);
    flipcc(fourcc);

		f.read(&size, 4);

		size_t nextpos = f.getPos() + size;

		if (fourcc == "MOHD") {
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
      v1 = fixCoordSystem(Vec3D(ff[0], ff[1], ff[2]));
			f.read(ff,12); // Bounding box corner 2
      v2 = fixCoordSystem(Vec3D(ff[0], ff[1], ff[2]));
			f.read(&LiquidType, 4);

			groups = new WMOGroup[nGroups];
			mat = new WMOMaterial[nTextures];
		} else if (fourcc == "MOTX") {
			// textures
			// The beginning of a string is always aligned to a 4Byte Adress. (0, 4, 8, C). 
			// The end of the string is Zero terminated and filled with zeros until the next aligment. 
			// Sometimes there also empty aligtments for no (it seems like no) real reason.
			texbuf = new char[size];
			f.read(texbuf, size);
		} else if (fourcc == "MOMT") {
			// materials
			// Materials used in this map object, 64 bytes per texture (BLP file), nMaterials entries.

			for (size_t i=0; i<nTextures; i++) {
				WMOMaterial *m = &mat[i];
				f.read(m, 0x40);

				QString texpath = QString::fromLatin1(texbuf+m->nameStart);

        m->tex = texturemanager.add(GAMEDIRECTORY.getFile(texpath));
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
		} else if (fourcc == "MOGN") {
			// List of group names for the groups in this map object. There are nGroups entries in this chunk.
			// A contiguous block of zero-terminated strings. The names are purely informational, 
			// they aren't used elsewhere (to my knowledge)
			// i think that his realy is zero terminated and just a name list .. but so far im not sure 
			// _what_ else it could be - tharo
      groupnames = new char[size];
      f.read(groupnames, size);
		} else if (fourcc == "MOGI") {
			// group info - important information! ^_^
			// Group information for WMO groups, 32 bytes per group, nGroups entries.
			for (size_t i=0; i<nGroups; i++) {
				groups[i].init(this, f, (int)i, groupnames);

			}
		} else if (fourcc == "MOLT") {
			// Lighting information. 48 bytes per light, nLights entries
			for (size_t i=0; i<nLights; i++) {
				WMOLight l;
				l.init(f);
				lights.push_back(l);
			}
		} else if (fourcc == "MODN") {
			// models ...
			// MMID would be relative offsets for MMDX filenames
			// List of filenames for M2 (mdx) models that appear in this WMO.
			// A block of zero-padded, zero-terminated strings. There are nModels file names in this list. They have to be .MDX!
			if (size) {

				ddnames = (char*)f.getPointer();
				//fixnamen(ddnames, size);

				char *p=ddnames,*end=p+size;
				
				while (p<end) {
					QString path = QString::fromLatin1(p);
					p+=strlen(p)+1;
					while ((p<end) && (*p==0)) p++;
					models.push_back(path.toStdString());
				}
				f.seekRelative((int)size);
			}
		} else if (fourcc == "MODS") {
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
		} else if (fourcc == "MODD") {
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
		else if (fourcc == "MOSB") {
			// Skybox. Always 00 00 00 00. Skyboxes are now defined in DBCs (Light.dbc etc.). 
			// Contained a M2 filename that was used as skybox.
			if (size>4) {
				QString path = QString::fromLatin1((char*)f.getPointer());
				//fixname(path);
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
		else if (fourcc == "MOPV") {
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
        p.a = fixCoordSystem(Vec3D(ff[0], ff[1], ff[2]));
				f.read(ff,12);
        p.b = fixCoordSystem(Vec3D(ff[0], ff[1], ff[2]));
				f.read(ff,12);
        p.c = fixCoordSystem(Vec3D(ff[0], ff[1], ff[2]));
				f.read(ff,12);
        p.d = fixCoordSystem(Vec3D(ff[0], ff[1], ff[2]));
				pvs.push_back(p);
			}
		}
		else if (fourcc == "MOPR") {
			// Portal <> group relationship? 2*nPortals entries of 8 bytes.
			// I think this might specify the two WMO groups that a portal connects.
			size_t nn = size / 8;
			WMOPR *pr = (WMOPR*)f.getPointer();
			for (size_t i=0; i<nn; i++) {
				prs.push_back(*pr++);
			}
		}
		else if (fourcc == "MOVV") {
			// Visible block vertices
			// Just a list of vertices that corresponds to the visible block list.
		}
		else if (fourcc == "MOVB") {
			// Visible block list
 			// WMOVB p;
		}
		else if (fourcc == "MFOG") {
			// Fog information. Made up of blocks of 48 bytes.
			size_t nfogs = size / 0x30;
			for (size_t i=0; i<nfogs; i++) {
				WMOFog fog;
				fog.init(f);
				fogs.push_back(fog);
			}
		}
		else if (fourcc == "MFOG") {
			// optional, Convex Volume Planes. Contains blocks of floating-point numbers.
		}

		f.seek(nextpos);
	}

	f.close();
  delete[] texbuf;

	for (size_t i=0; i<nGroups; i++) groups[i].initDisplayList();

  // compute min/max bounds based on groups
  for (size_t i = 0; i < nGroups; i++)
  {
    if (groups[i].vmin.x < minCoord.x)
      minCoord.x = groups[i].vmin.x;
    else if (groups[i].vmax.x > maxCoord.x)
      maxCoord.x = groups[i].vmax.x;

    if (groups[i].vmin.y < minCoord.y)
      minCoord.y = groups[i].vmin.y;
    else if (groups[i].vmax.y > maxCoord.y)
      maxCoord.y = groups[i].vmax.y;

    if (groups[i].vmin.z < minCoord.z)
      minCoord.z = groups[i].vmin.z;
    else if (groups[i].vmax.z > maxCoord.z)
      maxCoord.z = groups[i].vmax.z;
  }
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

		
		if (skybox) {
			delete skybox;
			//gWorld->modelmanager.del(sbid);
		}

		loadedModels.clear();
		delete groupnames;
	}
}

void WMO::loadGroup(int id)
{
	if (id==-1) {
    for (size_t i = 0; i < nGroups; i++) {
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

void WMO::draw()
{
	if (!ok) return;
  
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
