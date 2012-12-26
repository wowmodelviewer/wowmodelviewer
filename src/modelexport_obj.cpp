#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <math.h>

#include "globalvars.h"
#include "modelexport.h"
#include "modelcanvas.h"

//#include "CxImage/ximage.h"

void ExportOBJ_M2(Attachment *att, Model *m, wxString fn, bool init)
{
	// Open file
	wxString filename(fn, wxConvUTF8);
	wxString rootpath = filename.BeforeLast(SLASH);
	int pathcount = 0;	// Counts the directories to the base/root directory, relative to the model.

	if (m->modelType != MT_CHAR){
		// Characters are always placed in the directory specified, and thus don't need to increase the pathcount.
		wxString filepath = m->name;
		while (filepath.Find(MPQ_SLASH)>0){
			wxString a = filepath.BeforeFirst(MPQ_SLASH) + MPQ_SLASH;
			filepath.Replace(a, wxEmptyString, true);
			pathcount++;
		}

		if (modelExport_PreserveDir == true){
			wxString Path1, Path2, Name;

			Path1 << filename.BeforeLast(SLASH);
			Name << filename.AfterLast(SLASH);
			Path2 << m->name.BeforeLast(SLASH);
			MakeDirs(Path1,Path2);
			filename.Empty();
			filename << Path1 << SLASH << Path2 << SLASH << Name;
		}
	}
	wxFFileOutputStream fs (filename);

	if (!fs.IsOk()) {
		wxLogMessage(wxT("Error: Unable to open file '%s'. Could not export model."), filename.c_str());
		return;
	}
	wxTextOutputStream f (fs);

	LogExportData(wxT("OBJ"), m->modelname, fn);

	//unsigned short numVerts = 0;
	//unsigned short numGroups = 0;
	//unsigned short numFaces = 0;
	//ModelData *verts = NULL;
	//GroupData *groups = NULL;

	ssize_t cAnim = 0;
	size_t cFrame = 0;
	if ((m->animated == true) && (init == false)){
		cAnim = m->currentAnim;
		cFrame = m->animManager->GetFrame();
	}

	//InitCommon(att, init, verts, groups, numVerts, numGroups, numFaces);
	//wxString out;

	// http://people.sc.fsu.edu/~burkardt/data/mtl/mtl.html
	wxString matName(filename, wxConvUTF8);
	matName = matName.BeforeLast('.');
	matName << wxT(".mtl");

	wxFFileOutputStream ms (matName);
	if (!ms.IsOk()) {
		wxLogMessage(wxT("Error: Unable to open file '%s'. Could not export materials."), matName.c_str());
		return;
	}
	wxTextOutputStream fm (ms);
	matName = matName.AfterLast(SLASH);

	fm << wxT("#") << endl;
	fm << wxT("# ") << matName << endl;
	fm << wxT("#") << endl;
	fm << endl;

	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];
			
		if (p.init(m)) {
			wxString Texture = m->TextureList[p.tex];
			wxString TexturePath = Texture.BeforeLast(MPQ_SLASH);
			wxString texName = Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));
			if (m->modelType == MT_CHAR){
				wxString charname = filename.AfterLast(SLASH).BeforeLast(wxT('.')) + wxT("_");
				if (texName.Find(MPQ_SLASH) != wxNOT_FOUND){
					texName = charname + Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));
					TexturePath = wxEmptyString;
				}
				if (texName.Find(wxT("Body")) != wxNOT_FOUND){
					texName = charname + wxT("Body");
				}
			}
			wxString ExportName = rootpath;
			ExportName << SLASH << texName;
			if (modelExport_PreserveDir == true){
				wxString Path1, Path2, Name;
				Path1 << ExportName.BeforeLast(SLASH);
				Name << texName.AfterLast(SLASH);
				Path2 << TexturePath;

				MakeDirs(Path1,Path2);

				ExportName.Empty();
				ExportName << Path1 << SLASH << Path2 << SLASH << Name;
			}
			ExportName << wxT(".tga");
			float amb = 0.50f;
			Vec4D diff = p.ocol;

			wxString material = texName;

			if (p.unlit == true) {
				// Add Lum, just in case there's a non-luminous surface with the same name.
				material = material + wxT("_Lum");
				amb = 1.0f;
				diff = Vec4D(0,0,0,0);
			}

			// If Doublesided
			if ((p.cull?true:false) == false) {
				material = material + wxT("_Dbl");
			}

			fm << wxT("newmtl ") << material << endl;
			fm << wxT("illum 1") << endl;
			fm << wxString::Format(wxT("Kd %.03f %.03f %.03f"), diff.x, diff.y, diff.z) << endl;
			fm << wxString::Format(wxT("Ka %.03f %.03f %.03f"), amb, amb, amb) << endl;
			fm << wxString::Format(wxT("Ks %.03f %.03f %.03f"), p.ecol.x, p.ecol.y, p.ecol.z) << endl;
			fm << wxT("Ns 1.0") << endl;

			wxString predir = wxEmptyString;
			for (size_t i=0;i<pathcount;i++){
				predir << wxT("..") << SLASH;
			}
			predir << TexturePath;
			wxString mapname = predir;
			if (TexturePath.Len() > 0)
				mapname << SLASH;
			if (m->modelType != MT_CHAR)
				mapname << ExportName;
			else
				mapname << texName;
			if (SLASH == wxT('\\')){
				mapname.Replace(wxT("\\"),wxT("\\\\"),true);
			}
			fm << wxT("map_Kd ") << mapname << wxT(".tga") << endl << endl;

			wxLogMessage(wxT("Exporting Image: %s"), ExportName.c_str());
			SaveTexture(ExportName);
		}
	}

	// Materials for Attached Objects
	if (att != NULL){
		if (att->model){
			wxLogMessage(wxT("Att Model found."));
		}
		for (size_t c=0; c<att->children.size(); c++) {
			Attachment *att2 = att->children[c];
			for (size_t j=0; j<att2->children.size(); j++) {
				Model *mAttChild = static_cast<Model*>(att2->children[j]->model);
				if (mAttChild){
					wxLogMessage(wxT("AttChild Model found."));
					for (size_t i=0; i<mAttChild->passes.size(); i++) {
						ModelRenderPass &p = mAttChild->passes[i];

						if (p.init(mAttChild)) {
							wxString Texture = mAttChild->TextureList[p.tex];
							wxString TexturePath = Texture.BeforeLast(MPQ_SLASH);
							wxString texName = Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));
							wxString ExportName = rootpath;
							ExportName << SLASH << texName;
							if (modelExport_PreserveDir == true){
								wxString Path1, Path2, Name;
								Path1 << ExportName.BeforeLast(SLASH);
								Name << texName.AfterLast(SLASH);
								Path2 << TexturePath;

								MakeDirs(Path1,Path2);

								ExportName.Empty();
								ExportName << Path1 << SLASH << Path2 << SLASH << Name;
							}
							ExportName << wxT(".tga");

							wxString material = texName;

							float amb = 0.50f;
							Vec4D diff = p.ocol;

							if (p.unlit == true) {
								// Add Lum, just in case there's a non-luminous surface with the same name.
								material = material + wxT("_Lum");
								amb = 1.0f;
								diff = Vec4D(0,0,0,0);
							}

							// If Doublesided
							if ((p.cull?true:false) == false) {
								material = material + wxT("_Dbl");
							}

							fm << wxT("newmtl ") << material << endl;
							fm << wxT("illum 1") << endl;
							fm << wxString::Format(wxT("Kd %.03f %.03f %.03f"), diff.x, diff.y, diff.z) << endl;
							fm << wxString::Format(wxT("Ka %.03f %.03f %.03f"), amb, amb, amb) << endl;
							fm << wxString::Format(wxT("Ks %.03f %.03f %.03f"), p.ecol.x, p.ecol.y, p.ecol.z) << endl;
							fm << wxT("Ns 1.0") << endl;

							wxString predir = wxEmptyString;
							for (size_t i=0;i<pathcount;i++){
								predir << wxT("..") << SLASH;
							}
							predir << TexturePath;
							wxString mapname = predir;
							if (TexturePath.Len() > 0)
								mapname << SLASH;
							mapname << texName;
							if (SLASH == wxT('\\')){
								mapname.Replace(wxT("\\"),wxT("\\\\"),true);
							}
							fm << wxT("map_Kd ") << mapname << wxT(".tga") << endl << endl;

							wxLogMessage(wxT("Exporting Image: %s"),ExportName.c_str());
							SaveTexture(ExportName);
						}
					}
				}
			}
		}
	}

	ms.Close();

	f << wxT("# Wavefront OBJ exported by WoW Model Viewer ") << APP_VERSION << endl << endl;
	f << wxT("mtllib ") << matName << endl << endl;

	wxLogMessage(wxT("Materials and Texture Images Exported."));

	bool vertMsg = false;
	// output all the vertice data
	int vertics = 0;
	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];

		if (p.init(m)) {
			//f << "# Chunk Indice Count: " << p.indexCount << endl;

			for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
				uint16 a = m->indices[b];
				Vec3D vert;
				if ((m->animated == true) && (init == false) && (m->vertices)) {
					if (vertMsg == false){
						wxLogMessage(wxT("Using Verticies"));
						vertMsg = true;
					}
					vert = m->vertices[a];
				} else {
					if (vertMsg == false){
						wxLogMessage(wxT("Using Original Verticies"));
						vertMsg = true;
					}
					vert = m->origVertices[a].pos;
				}
				MakeModelFaceForwards(vert,false);
				vert *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
				f << wxString::Format(wxT("v %.06f %.06f %.06f"), vert.x, vert.y, vert.z) << endl;

				vertics ++;
			}
		}
	}

	// Verticies for Attached Objects
	if (att != NULL){
		for (size_t c=0; c<att->children.size(); c++) {
			Attachment *att2 = att->children[c];
			for (size_t j=0; j<att2->children.size(); j++) {
				Model *mAttChild = static_cast<Model*>(att2->children[j]->model);

				if (mAttChild){
					int boneID = -1;
					Model *mParent = NULL;

					if (att2->parent) {
						mParent = static_cast<Model*>(att2->children[j]->parent->model);
						if (mParent)
							boneID = mParent->attLookup[att2->children[j]->id];
					}

					Vec3D mPos(0,0,0);
					Quaternion mRot(Vec4D(0,0,0,0));
					Vec3D mScale(1,1,1);

					Vec3D Seraph(1,1,1);
					Quaternion Niobe(Vec4D(0,0,0,0));

					if (boneID>-1) {
						Bone cbone = mParent->bones[mParent->atts[boneID].bone];
						Matrix mat = cbone.mat;
						Matrix rmat = cbone.mrot;

						// InitPose is a reference to the HandsClosed animation (#15), which is the closest to the Initial pose.
						// By using this animation, we'll get the proper scale for the items when in Init mode.
						//int InitPose = 15;

						if (init == true){
							mPos = mParent->atts[boneID].pos;
							mScale = cbone.scale.getValue((size_t)ANIMATION_HANDSCLOSED,0);
							mRot = cbone.rot.getValue((size_t)ANIMATION_HANDSCLOSED,0);
						}else{
							// Rotations aren't working correctly... Not sure why.
							rmat.quaternionRotate(cbone.rot.getValue(cAnim,cFrame));
							mat.scale(cbone.scale.getValue(cAnim,cFrame));
							mat.translation(cbone.transPivot);

							mPos.x = mat.m[0][3];
							mPos.y = mat.m[1][3];
							mPos.z = mat.m[2][3];

							mScale = cbone.scale.getValue(cAnim,(uint32)cFrame);
							mRot = rmat.GetQuaternion();
						}
						if (mScale.x == 0 && mScale.y == 0 && mScale.z == 0){
							mScale = Vec3D(1,1,1);
						}
					}

					for (size_t i=0; i<mAttChild->passes.size(); i++) {
						ModelRenderPass &p = mAttChild->passes[i];

						if (p.init(mAttChild)) {
							for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
								uint16 a = mAttChild->indices[b];
								// Points
								Vec3D vert;
								if ((init == false)&&(mAttChild->vertices)) {
									vert = mAttChild->vertices[a];
								} else {
									vert = mAttChild->origVertices[a].pos;
								}
								Matrix Neo;

								Neo.translation(vert);							// Set Original Position
								Neo.quaternionRotate(Niobe);					// Set Original Rotation
								Neo.scale(Seraph);								// Set Original Scale

								Neo *= Matrix::newTranslation(mPos);			// Apply New Position
								Neo *= Matrix::newQuatRotate(mRot);				// Apply New Rotation
								Neo *= Matrix::newScale(mScale);				// Apply New Scale

								Vec3D mVert = Neo * vert;
								MakeModelFaceForwards(mVert,false);

								mVert *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
								f << wxString::Format(wxT("v %.06f %.06f %.06f"), mVert.x, mVert.y, mVert.z) << endl;
								vertics++;
							}
						}
					}
				}
			}
		}
	}
	f << wxT("# ") << vertics << wxT(" vertices") << endl << endl;
	wxLogMessage(wxT("Geometry Verticies Exported."));

	// output all the texture coordinate data
	int textures = 0;
	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];
		// we don't want to render completely transparent parts
		if (p.init(m)) {
			for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
				uint16 a = m->indices[b];
				Vec2D tc =  m->origVertices[a].texcoords;
				f << wxString::Format(wxT("vt %.06f %.06f"), tc.x, 1-tc.y) << endl;
				//f << "vt " << m->origVertices[a].texcoords.x << " " << (1 - m->origVertices[a].texcoords.y) << endl;
				textures ++;
			}
		}
	}
	// UVs for Attached Objects
	if (att != NULL){
		for (size_t c=0; c<att->children.size(); c++) {
			Attachment *att2 = att->children[c];
			for (size_t j=0; j<att2->children.size(); j++) {
				Model *mAttChild = static_cast<Model*>(att2->children[j]->model);

				if (mAttChild){
					for (size_t i=0; i<mAttChild->passes.size(); i++) {
						ModelRenderPass &p = mAttChild->passes[i];

						if (p.init(mAttChild)) {
							for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
								uint16 a = mAttChild->indices[b];
								Vec2D tc =  mAttChild->origVertices[a].texcoords;
								f << wxString::Format(wxT("vt %.06f %.06f"), tc.x, 1-tc.y) << endl;
								textures ++;
							}
						}
					}
				}
			}
		}
	}
	f << wxT("# ") << textures << wxT(" texture coordinates") << endl << endl;
	wxLogMessage(wxT("Texture Verticies (UVs) Exported."));

	// output all the vertice normals data
	int normals = 0;
	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];
		if (p.init(m)) {
			for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
				uint16 a = m->indices[b];
				Vec3D n = m->origVertices[a].normal;
				f << wxString::Format(wxT("vn %.06f %.06f %.06f"), n.x, n.y, n.z) << endl;
				//f << "vn " << m->origVertices[a].normal.x << " " << m->origVertices[a].normal.y << " " << m->origVertices[a].normal.z << endl;
				normals ++;
			}
		}
	}
	// Normal data for Attached Objects
	if (att != NULL){
		for (size_t c=0; c<att->children.size(); c++) {
			Attachment *att2 = att->children[c];
			for (size_t j=0; j<att2->children.size(); j++) {
				Model *mAttChild = static_cast<Model*>(att2->children[j]->model);

				if (mAttChild){
					for (size_t i=0; i<mAttChild->passes.size(); i++) {
						ModelRenderPass &p = mAttChild->passes[i];

						if (p.init(mAttChild)) {
							for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
								uint16 a = mAttChild->indices[b];
								Vec3D n = mAttChild->origVertices[a].normal;
								f << wxString::Format(wxT("vn %.06f %.06f %.06f"), n.x, n.y, n.z) << endl;
								//f << "vn " << m->origVertices[a].normal.x << " " << m->origVertices[a].normal.y << " " << m->origVertices[a].normal.z << endl;
								normals ++;
							}
						}
					}
				}
			}
		}
	}
	f << wxT("# ") << normals << wxT(" normals") << endl << endl;
	wxLogMessage(wxT("Vertex Normals Exported."));

	int counter=1;
	uint32 pointnum = 0;
	// Polygon Data
	int triangles_total = 0;
	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];

		if (p.init(m)) {
			// Build Vert2Point DB
			size_t *Vert2Point = new size_t[p.vertexEnd];
			for (size_t v=p.vertexStart; v<p.vertexEnd; v++, pointnum++) {
				Vert2Point[v] = pointnum;
			}

			wxString Texture = m->TextureList[p.tex];
			wxString TexturePath = Texture.BeforeLast(MPQ_SLASH);
			wxString texName = Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));
			if (m->modelType == MT_CHAR){
				wxString charname = filename.AfterLast(SLASH).BeforeLast(wxT('.')) + wxT("_");
				if (texName.Find(MPQ_SLASH) != wxNOT_FOUND){
					texName = charname + Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));
					TexturePath = wxT("");
				}
				if (texName.Find(wxT("Body")) != wxNOT_FOUND){
					texName = charname + wxT("Body");
				}
			}

			if (p.unlit == true) {
				texName = texName + wxT("_Lum");
			}
			if ((p.cull?true:false) == false) {
				texName = texName + wxT("_Dbl");
			}

			int g = p.geoset;
			wxString partName;
			
			// Part Names
			int mesh = m->geosets[g].id / 100;
			if (m->modelType == MT_CHAR && mesh < 19 && meshes[mesh] != wxEmptyString){
				wxString msh = meshes[mesh];
				msh.Replace(wxT(" "),wxT("_"),true);
				partName = wxString::Format(wxT("Geoset_%03i-%s"), g, msh.c_str());
			}else{
				partName = wxString::Format(wxT("Geoset_%03i"), g);
			}

			f << wxT("g ") << partName << endl;
			f << wxT("usemtl ") << texName << endl;
			f << wxT("s 1") << endl;
			int triangles = 0;
			for (size_t k=0; k<p.indexCount; k+=3) {
				f << wxT("f ");
				f << counter << wxT("/") << counter << wxT("/") << counter << wxT(" ");
				counter ++;
				f << counter << wxT("/") << counter << wxT("/") << counter << wxT(" ");
				counter ++;
				f << counter << wxT("/") << counter << wxT("/") << counter << endl;
				counter ++;
				triangles ++;
			}
			f << wxT("# ") << triangles << wxT(" triangles in group") << endl << endl;
			triangles_total += triangles;
		}
	}
	// Polygon Data for Attached Objects
	if (att != NULL){
		for (size_t c=0; c<att->children.size(); c++) {
			Attachment *att2 = att->children[c];
			for (size_t j=0; j<att2->children.size(); j++) {
				Model *mAttChild = static_cast<Model*>(att2->children[j]->model);

				if (mAttChild){
					for (size_t i=0; i<mAttChild->passes.size(); i++) {
						ModelRenderPass &p = mAttChild->passes[i];

						if (p.init(mAttChild)) {
							wxString Texture = mAttChild->TextureList[p.tex];
							wxString TexturePath = Texture.BeforeLast(MPQ_SLASH);
							wxString texName = Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));

							wxString partName;

							int thisslot = att2->children[j]->slot;
							if (thisslot < 15 && slots[thisslot]!=wxEmptyString){
								wxString slt = slots[thisslot];
								slt.Replace(wxT(" "),wxT("_"),true);
								partName = wxString::Format(wxT("Child_%02i-%s"), j ,slt.c_str());
							}else{
								partName = wxString::Format(wxT("Child_%02i-Slot_%02i"), j, att2->children[j]->slot);
							}

							f << wxT("g ") << partName << endl;
							f << wxT("usemtl ") << texName << endl;
							f << wxT("s 1") << endl;
							int triangles = 0;
							for (size_t k=0; k<p.indexCount; k+=3) {
								f << wxT("f ");
								f << counter << wxT("/") << counter << wxT("/") << counter << wxT(" ");
								counter ++;
								f << counter << wxT("/") << counter << wxT("/") << counter << wxT(" ");
								counter ++;
								f << counter << wxT("/") << counter << wxT("/") << counter << endl;
								counter ++;
								triangles ++;
							}
							f << wxT("# ") << triangles << wxT(" triangles in group") << endl << endl;
							triangles_total += triangles;
						}
					}
				}
			}
		}
	}
	f << wxT("# ") << triangles_total << wxT(" triangles total") << endl << endl;
	wxLogMessage(wxT("Material Links Exported."));
	
	// Close file
	fs.Close();

	g_modelViewer->SetStatusText(wxT("Wavefront Object Export Successful!"));
	wxLogMessage(wxT("Wavefront OBJ successfully exported."));
}

void ExportOBJ_WMO(WMO *m, wxString file)
{
	wxString rootpath = file.BeforeLast(SLASH);
	int pathcount = 0;
	wxString filepath = m->name;
	while (filepath.Find(MPQ_SLASH)>0){
		wxString a = filepath.BeforeFirst(MPQ_SLASH) + MPQ_SLASH;
		filepath.Replace(a, wxEmptyString, true);
		pathcount++;
	}

	// Open file
	if (modelExport_PreserveDir == true){
		wxString Path1, Path2, Name;
		Path1 << rootpath;
		Name << file.AfterLast(SLASH);
		Path2 << m->name.BeforeLast(SLASH);

		MakeDirs(Path1,Path2);

		file.Empty();
		file << Path1 << SLASH << Path2 << SLASH << Name;
	}
	wxLogMessage(wxT("Final Output File: \"%s\""),file.c_str());

	// FIXME: ofstream is not compitable with multibyte path name
#ifndef _MINGW
	ofstream f(file.fn_str(), ios_base::out | ios_base::trunc);
#else
	ofstream f(file.char_str(), ios_base::out | ios_base::trunc);
#endif
	if (!f.is_open()) {
		wxLogMessage(wxT("Error: Unable to open file '%s'. Could not export model."), file.c_str());
		return;
	}
	LogExportData(wxT("OBJ"),m->name,file);

	wxString mtlName = file;
	mtlName = mtlName.BeforeLast('.');
	mtlName << wxT(".mtl");

	// FIXME: ofstream is not compitable with multibyte path name
#ifndef _MINGW
	ofstream fm(mtlName.fn_str(), ios_base::out | ios_base::trunc);
#else
	ofstream fm(mtlName.char_str(), ios_base::out | ios_base::trunc);
#endif
	mtlName = mtlName.AfterLast(SLASH);

	fm << wxT("#") << endl;
	fm << wxT("# ") << mtlName.mb_str() << endl;
	fm << wxT("#") << endl;
	fm <<  endl;

	wxString *texarray = new wxString[m->nTextures+1];

	// Find a Match for mat->tex and place it into the Texture Name Array.
	for (size_t i=0; i<m->nGroups; i++) {
		for (size_t j=0; j<m->groups[i].nBatches; j++)
		{
			WMOBatch *batch = &m->groups[i].batches[j];
			WMOMaterial *mat = &m->mat[batch->texture];

			wxString outname = file.AfterLast(SLASH).BeforeLast('.');

			bool nomatch = true;
			for (size_t t=0;t<=m->nTextures; t++) {
				if (t == mat->tex) {
					texarray[mat->tex] = wxString(m->textures[t-1].c_str(), wxConvUTF8);
					texarray[mat->tex] = texarray[mat->tex].BeforeLast('.');
					nomatch = false;
					break;
				}
			}
			if (nomatch == true){
				texarray[mat->tex] = outname << wxString::Format(wxT("_Material_%03i"), mat->tex);
			}
		}
	}

	for (size_t i=0; i<m->nGroups; i++) {
		for (size_t j=0; j<m->groups[i].nBatches; j++)
		{
			WMOBatch *batch = &m->groups[i].batches[j];
			WMOMaterial *mat = &m->mat[batch->texture];

			//wxString matName(wxString::Format(wxT("Material_%03i"), mat->tex));
			wxString matName = texarray[mat->tex];

			//wxString texName(fn, wxConvUTF8);
			wxString texNameFull = texarray[mat->tex];
			texNameFull << wxT(".tga");
			wxString texPath = texNameFull.BeforeLast(MPQ_SLASH);
			wxString texName = texNameFull.AfterLast(MPQ_SLASH);
			//wxLogMessage(wxT("Processing %s..."),texNameFull);

			//fm << "newmtl " << "Material_" << mat->tex+1 << endl;
			fm << wxT("newmtl ") << texName.BeforeLast('.') << endl;
			fm << wxT("illum 1") << endl;
			fm << wxT("Kd 0.500 0.500 0.500") << endl; // diffuse
			fm << wxT("Ka 0.500 0.500 0.500") << endl; // ambient/base color
			fm << wxT("Ks 0.000 0.000 0.000") << endl; // specular amount
			fm << wxT("Ns 1") << endl;	// glossiness, range: 0-1000

			wxString predir = wxEmptyString;
			for (size_t i=0;i<pathcount;i++){
				predir << wxT("..") << SLASH;
			}
			wxString mapname = predir;
			if (modelExport_PreserveDir == true)
				mapname << texNameFull;
			else
				mapname << texName;
			if (SLASH == wxT('\\')){
				mapname.Replace(wxT("\\"),wxT("\\\\"),true);
			}

			fm << wxT("map_Kd ") << mapname << endl << endl;

			wxString texFilename = rootpath;
			texFilename << SLASH << texName;
			
			if (modelExport_PreserveDir == true){
				wxString Path1, Path2, Name;
				Path1 << texFilename.BeforeLast(SLASH);
				Name << texName;
				Path2 << texPath;

				MakeDirs(Path1,Path2);

				texFilename.Empty();
				texFilename << Path1 << SLASH << Path2 << SLASH << Name;
			}

			// setup texture
			glBindTexture(GL_TEXTURE_2D, mat->tex);
			wxLogMessage(wxT("Exporting Image: %s"),texFilename.c_str());
			SaveTexture(texFilename);
		}
	}
	fm.close();
	wxLogMessage(wxT("Materials and Texture Images Exported."));

	f << wxT("# Wavefront OBJ exported by WoW Model Viewer ") << APP_VERSION << endl << endl;
	f << wxT("mtllib ") << mtlName.mb_str() << endl << endl;

	// geometric vertices (v)
	// v x y z weight
	for (size_t i=0; i<m->nGroups; i++) {
		for (size_t j=0; j<m->groups[i].nBatches; j++)
		{
			WMOBatch *batch = &m->groups[i].batches[j];
			for(int ii=0;ii<batch->indexCount;ii++)
			{
				int a = m->groups[i].indices[batch->indexStart + ii];
				Vec3D p = m->groups[i].vertices[a];
				p *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
				f << "v " << p.x << " " << p.z << " " << -p.y << endl;
			}
		}
	}
	f << endl;
	wxLogMessage(wxT("Geometry Verticies Exported."));

	// texture vertices (vt)
	// vt horizontal vertical depth
	for (size_t i=0; i<m->nGroups; i++) {
		for (size_t j=0; j<m->groups[i].nBatches; j++)
		{
			WMOBatch *batch = &m->groups[i].batches[j];
			for(int ii=0;ii<batch->indexCount;ii++)
			{
				int a = m->groups[i].indices[batch->indexStart + ii];
				f << "vt " << m->groups[i].texcoords[a].x << " " << (1 - m->groups[i].texcoords[a].y) << endl;
			}
		}
	}
	f << endl;
	wxLogMessage(wxT("Texture Verticies (UVs) Exported."));

	// vertex normals (vn)
	// vn x y z
	for (size_t i=0; i<m->nGroups; i++) {
		for (size_t j=0; j<m->groups[i].nBatches; j++)
		{
			WMOBatch *batch = &m->groups[i].batches[j];
			for(int ii=0;ii<batch->indexCount;ii++)
			{
				int a = m->groups[i].indices[batch->indexStart + ii];
				f << "vn " << m->groups[i].normals[a].x << " " << m->groups[i].normals[a].z << " " << -m->groups[i].normals[a].y << endl;
			}
		}
	}
	f << endl;
	wxLogMessage(wxT("Vertex Normals Exported."));

	// Referencing groups of vertices
	// f v/vt/vn v/vt/vn v/vt/vn v/vt/vn
	int counter = 1;
	for (size_t i=0; i<m->nGroups; i++) {
		for (size_t j=0; j<m->groups[i].nBatches; j++)
		{
			WMOBatch *batch = &m->groups[i].batches[j];
			WMOMaterial *mat = &m->mat[batch->texture];

			// batch->texture or mat->tex ?
			f << wxT("g ") << m->groups[i].name << endl;
			f << wxT("s 1") << endl;
			//f << "usemtl Material_" << mat->tex+1 << endl;
			// MilkShape3D cann't read long texname
			f << wxT("usemtl ") << texarray[mat->tex].AfterLast(MPQ_SLASH) << endl;
			for (size_t k=0; k<batch->indexCount; k+=3) {
				f << wxT("f ");
				f << counter << wxT("/") << counter << wxT("/") << counter << wxT(" ");
				f << (counter+1) << wxT("/") << (counter+1) << wxT("/") << (counter+1) << wxT(" ");
				f << (counter+2) << wxT("/") << (counter+2) << wxT("/") << (counter+2) << endl;
				counter += 3;
			}
			f << endl;
		}
	}
	wxLogMessage(wxT("Material Links Exported."));

	// Close file
	f.close();
	wxDELETEA(texarray);

	g_modelViewer->SetStatusText(wxT("Wavefront Object Export Successful!"));
	wxLogMessage(wxT("Wavefront OBJ successfully exported."));
}
