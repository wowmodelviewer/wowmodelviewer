#include <wx/wfstream.h>
#include <math.h>

#include "Bone.h"
#include "globalvars.h"
#include "modelexport.h"
#include "modelcanvas.h"

#include "CxImage/ximage.h"

// 2 methods to go, just export the entire m2 model.
// or use our "drawing" routine to export only whats being drawn.

// SaveTexture
// Used to save composite textures, such as a character's face & body.
void SaveTexture(wxString fn)
{
	fn = fixMPQPath(fn);
	unsigned char *pixels = NULL;

	GLint width, height;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

	pixels = new unsigned char[width * height * 4];

	glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);

	CxImage *newImage = new CxImage(0);
	newImage->CreateFromArray(pixels, width, height, 32, (width*4), true);

	if (fn.Last() == 'g')
		newImage->Save(fn.mb_str(), CXIMAGE_FORMAT_PNG);
	else
		newImage->Save(fn.mb_str(), CXIMAGE_FORMAT_TGA);

	//newImage->Destroy();
	wxDELETE(newImage);
	wxDELETEA(pixels);

	// tga files, starcraft II needs 17th bytes as 8
	if (fn.Last() == 'a') {
		wxFFile f;
		f.Open(fn, wxT("r+b"));
		if (f.IsOpened()) {
			f.Seek(17, wxFromStart);
			char c=8;
			f.Write(&c, sizeof(char));
			f.Close();
		}
	}
}

// SaveTexture2 Function
// Used to export images that are filenames. For composited images, such as a character's face & body texture, use SaveTexture.
// ExportID identifies the exporting function. This is used in the path-generating section.
// Suffixes currently supported: "tga" & "png". Defaults to tga if omitted by exporter.
void SaveTexture2(wxString file, wxString outdir, wxString ExportID = wxEmptyString, wxString suffix = wxString(wxT("tga")))
{
	// Check to see if we have all our data...
	if (file == wxEmptyString)
		return;
	if (outdir == wxEmptyString)
		return;
	// Add a slash to our directory if it doesn't end with one.
	if (outdir.Last() != SLASH)
		outdir = outdir.Append(SLASH);

	//wxLogMessage(wxT("Starting Outdir: %s"),outdir);
	//wxLogMessage(wxT("File Input: %s"),file);

	wxFileName fn(file);
	if (fn.GetExt().Lower() != wxT("blp")){
		wxLogMessage(wxT("SaveTexture2 Error: Wrong Extension Found: %s"),fn.GetExt().Lower().c_str());
		return;
	}
	TextureID temptex = texturemanager.add(file);
	Texture &tex = *((Texture*)texturemanager.items[temptex]);
	if (tex.w == 0 || tex.h == 0){
		wxLogMessage(wxT("SaveTexture2 Error: Tex Width or Height == 0. W:%i, H:%i"),tex.w, tex.h);
		return;
	}

	wxString temp;

	unsigned char *tempbuf = (unsigned char*)malloc(tex.w*tex.h*4);
	tex.getPixels(tempbuf, GL_BGRA_EXT);

	CxImage *newImage = new CxImage(0);
	newImage->AlphaCreate();	// Create the alpha layer
	newImage->IncreaseBpp(32);	// set image to 32bit 
	newImage->CreateFromArray(tempbuf, tex.w, tex.h, 32, (tex.w*4), true);

	wxString ImgName = file.AfterLast(SLASH).BeforeLast('.');
	wxString ImgPath = file.BeforeLast(SLASH);
	//wxLogMessage(wxT("ImgName: %s, ImgPath: %s"),ImgName,ImgPath);
	//wxLogMessage(wxT("Outdir: %s"),outdir);

	// -= Pre-Path Directories =-
	// Add any directories inbetween the target directory and the preserved directory go here.

	outdir = fixMPQPath(outdir);

	// Lightwave
	if (ExportID.IsSameAs(wxT("LWO"))){
		if (modelExport_LW_PreserveDir == true){
			MakeDirs(outdir,wxT("Images"));
			wxString a = outdir << SLASH << wxT("Images") << SLASH;
			outdir.Empty();
			outdir = a;
		}
		//wxLogMessage(wxT("LWO Image Outdir: %s"),outdir);
	// Wavefront OBJ
	}else if (ExportID.IsSameAs(wxT("OBJ"))){
	}

	// Restore WoW's content directories for this image.
	if (modelExport_PreserveDir == true){
		MakeDirs(outdir,file.BeforeLast(SLASH));
		outdir << file.BeforeLast(SLASH) << SLASH;
	}
	//wxLogMessage(wxT("Preserve Outdir: %s"),outdir);

	// Final Filename
	temp = outdir+ImgName+wxT(".")+suffix;
	//wxLogMessage(wxT("Exporting Image: %s"),temp.c_str());

	//wxLogMessage(wxT("Info: Exporting texture to %s..."), temp.c_str());

	// Save image!
	if (suffix == wxT("tga"))
		newImage->Save(temp.mb_str(), CXIMAGE_FORMAT_TGA);
	else
		newImage->Save(temp.mb_str(), CXIMAGE_FORMAT_PNG);

	// Clear data we don't need anymore
	free(tempbuf);
	//newImage->Destroy();
	wxDELETE(newImage);
}

// Alter a Vert by a Quaternion
Vec3D AlterVertByQuat(Vec3D vert, Quaternion q, Matrix m){
	return vert;
}

// Limit a value by a min & a max. The Inc controls by how much to reduce for every run.
double Clamp(double value, float min, float max, float inc = PI){
	if (value > 0){
		while (value >= max){
			value -= inc;
		}
	}else if (value < 0){
		while (value <= min){
			value += inc;
		}
	}
	
	return value;
}

// Converts WoW's Radians into a 3D friendly Heading, Pitch, Bank system.
// Tested only with Lightwave. Might have to make minor direction changes here to make it more univsersal.
Vec3D QuaternionToXYZ(Vec3D Dir, float W){
	//-dir.z,dir.x,dir.y	WoW Direction...
	Vec3D vdir(Dir.x,Dir.z,-Dir.y);
	float c,angle_x,angle_y,angle_z,tempx,tempy;

	Vec3D XYZ;
	Matrix m;
	Quaternion q(vdir, W);
	m.quaternionRotate(q);

	angle_y = asin(m.m[0][2]);
	c = cos(angle_y);
	if (fabs(c) > 0.005){		// If not Gimble-locked
		tempx = m.m[2][2];
		tempy = -m.m[1][2];

		angle_x = atan2(tempy,tempx);

		tempx = m.m[0][0] / c;
		tempy = -m.m[0][1] / c;

		angle_z = atan2(tempy,tempx);
	}else{						// If Gimble-lock occured...
		wxString ays, cs;
		ays << angle_y;
		cs << c;
		wxMessageBox(wxT("Gimbal Lock Occured!\nPlease send the logfile and the name of the\nobject you were outputting to the development\nteam so they can attempt to fix any errors\ncaused by this."),wxT("Gimble Lock Warning"));
		wxLogMessage(wxT("Gimbal Lock happened! angle_y=%s, c=%s"), ays.c_str(), cs.c_str());

		angle_x  = 0;
		tempx = m.m[1][1];
		tempy = m.m[1][0];

		angle_z  = atan2(tempy, tempx);
	}

	if (angle_y < 0)
		angle_y = -angle_y;

	// 1.0 = 1 Radian, or 57.295779513082320876798154814114 degrees (180/PI)
	float d180 = (float)PI;	// 180 degrees
	int accuracy = 6;	// to the 6th decimal point...
	XYZ.x = (float)(Clamp(round(angle_x+d180,accuracy),(float)-PI*2,(float)PI*2));			// Heading
	XYZ.y = (float)(Clamp(round(-angle_y,accuracy),(float)-PI*2,(float)PI*2));				// Pitch
	XYZ.z = (float)(-Clamp(round(angle_z+d180,accuracy),(float)-PI*2,(float)PI*2));			// Bank

	return XYZ;
}

void QuaternionToRotationMatrix(const Quaternion& quat, Matrix& rkRot) {
	float fTx  = ((float)2.0)*quat.y;
	float fTy  = ((float)2.0)*quat.z;
	float fTz  = ((float)2.0)*quat.w;
	float fTwx = fTx*quat.x;
	float fTwy = fTy*quat.x;
	float fTwz = fTz*quat.x;
	float fTxx = fTx*quat.y;
	float fTxy = fTy*quat.y;
	float fTxz = fTz*quat.y;
	float fTyy = fTy*quat.z;
	float fTyz = fTz*quat.z;
	float fTzz = fTz*quat.w;

	rkRot.m[0][0] = (float)1.0-(fTyy+fTzz);
	rkRot.m[0][1] = fTxy-fTwz;
	rkRot.m[0][2] = fTxz+fTwy;
	rkRot.m[1][0] = fTxy+fTwz;
	rkRot.m[1][1] = (float)1.0-(fTxx+fTzz);
	rkRot.m[1][2] = fTyz-fTwx;
	rkRot.m[2][0] = fTxz-fTwy;
	rkRot.m[2][1] = fTyz+fTwx;
	rkRot.m[2][2] = (float)1.0-(fTxx+fTyy);
}

void RotationMatrixToEulerAnglesXYZ(const Matrix& rkRot, float& rfXAngle, float& rfYAngle, float& rfZAngle)
{
	if (rkRot.m[0][2] < (float)1.0){
		if (rkRot.m[0][2] > -(float)1.0){
			// y_angle = asin(r02)
			// x_angle = atan2(-r12,r22)
			// z_angle = atan2(-r01,r00)
			rfYAngle = (float)asin((double)rkRot.m[0][2]);
			rfXAngle = atan2(-rkRot.m[1][2],rkRot.m[2][2]);
			rfZAngle = atan2(-rkRot.m[0][1],rkRot.m[0][0]);
			return ;
		}else{
			// y_angle = -pi/2
			// z_angle - x_angle = atan2(r10,r11)
			// WARNING.  The solution is not unique.  Choosing z_angle = 0.
			rfYAngle = -HALFPIf;
			rfXAngle = -atan2(rkRot.m[1][0],rkRot.m[1][1]);
			rfZAngle = (float)0.0f;
			return ;
		}
	}else{
		// y_angle = +pi/2
		// z_angle + x_angle = atan2(r10,r11)
		// WARNING.  The solutions is not unique.  Choosing z_angle = 0.
		rfYAngle = HALFPIf;
		rfXAngle = atan2(rkRot.m[1][0],rkRot.m[1][1]);
		rfZAngle = (float)0.0f;
		return ;
	}
}

void AddCount(Model *m, unsigned short &numGroups, unsigned short &numVerts)
{
	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];

		if (p.init(m)) {
			numGroups++;
			
			for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
				numVerts++;
			}
		}
	}
}

void AddVertices(Model *m, Attachment *att, bool init, ModelData *verts, unsigned short &vertIndex, GroupData *groups, unsigned short &grpIndex)
{
	wxLogMessage(wxT("Adding Verticies from %s..."), m->name.c_str());
	int boneID = -1;
	Model *mParent = NULL;

	if (att->parent) {
		mParent = static_cast<Model*>(att->parent->model);
		if (mParent)
			boneID = mParent->attLookup[att->id];
	}

	Vec3D pos(0,0,0);
	Vec3D scale(1,1,1);
	if (boneID>-1) {
		// Note: We still need to rotate the item's points!! Note 2: Rotations should happen BEFORE scale!
		pos = mParent->atts[boneID].pos;
		Bone cbone = mParent->bones[mParent->atts[boneID].bone];
		Matrix mat = cbone.mat;
		if (init == true){
			// InitPose is a reference to the HandsClosed animation (#15), which is the closest to the Initial pose.
			// By using this animation, we'll get the proper scale for the items when in Init mode.
			int InitPose = 15;
			scale = cbone.scale.getValue(InitPose,0);
			if (scale.x == 0 && scale.y == 0 && scale.z == 0){
				scale.x = 1;
				scale.y = 1;
				scale.z = 1;
			}
		}else{
			// Scale takes into consideration only the final size of an object. This means that if a staff it rotated 90 degrees,
			// the final scale will be as if the staff is REALLY short. This should solve itself after we get rotations working.
			scale.x = mat.m[0][0];
			scale.y = mat.m[1][1];
			scale.z = mat.m[2][2];

			// Moves the item to the proper position.
			mat.translation(cbone.transPivot);
			pos.x = mat.m[0][3];
			pos.y = mat.m[1][3];
			pos.z = mat.m[2][3];
		}
	}

	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];

		if (p.init(m)) {
			for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++) {
				//wxLogMessage(wxT("Processing vertIndex %i, grpIndex %i"),vertIndex,grpIndex);
				uint16 a = m->indices[b];
				
				if ((init == false)&&(m->vertices)) {
					verts[vertIndex].vertex.x = ((m->vertices[a].x * scale.x) + pos.x);
					verts[vertIndex].vertex.y = ((m->vertices[a].y * scale.y) + pos.y);
					verts[vertIndex].vertex.z = ((m->vertices[a].z * scale.z) + pos.z);

					if (video.supportVBO) {
						verts[vertIndex].normal.x = (m->vertices[m->header.nVertices + a].x + pos.x);
						verts[vertIndex].normal.y = (m->vertices[m->header.nVertices + a].y + pos.y);
						verts[vertIndex].normal.z = (m->vertices[m->header.nVertices + a].z + pos.z);
					} else {
						verts[vertIndex].normal.x = m->normals[a].x;
						verts[vertIndex].normal.y = m->normals[a].y;
						verts[vertIndex].normal.z = m->normals[a].z;
					}
				} else {
					verts[vertIndex].vertex.x = ((m->origVertices[a].pos.x * scale.x) + pos.x);
					verts[vertIndex].vertex.y = ((m->origVertices[a].pos.y * scale.y) + pos.y);
					verts[vertIndex].vertex.z = ((m->origVertices[a].pos.z * scale.z) + pos.z);

					verts[vertIndex].normal.x = m->origVertices[a].normal.x;
					verts[vertIndex].normal.y = m->origVertices[a].normal.y;
					verts[vertIndex].normal.z = m->origVertices[a].normal.z;
				}

				verts[vertIndex].tu = m->origVertices[a].texcoords.x;
				verts[vertIndex].tv = m->origVertices[a].texcoords.y;

				verts[vertIndex].groupIndex = grpIndex;
				verts[vertIndex].boneid = m->origVertices[a].bones[0];

				vertIndex++;
			}
			groups[grpIndex].p = p;
			groups[grpIndex].m = m;
			grpIndex++;
		}
	}
}

void InitCommon(Attachment *att, bool init, ModelData *&verts, GroupData *&groups, unsigned short &numVerts, unsigned short &numGroups, unsigned short &numFaces)
{
	unsigned short vertIndex = 0;
	unsigned short grpIndex = 0;

	if (!att)
		return;

	wxLogMessage(wxT("Counting Verticies via InitCommon..."));

	Model *m = NULL;
	if (att->model) {
		m = static_cast<Model*>(att->model);
		if (!m)
			return;

		AddCount(m, numGroups, numVerts);
	}

	// children:
	for (size_t i=0; i<att->children.size(); i++) {
		Model *mAtt = static_cast<Model*>(att->children[i]->model);
		if (mAtt)
			AddCount(mAtt, numGroups, numVerts);

		Attachment *att2 = att->children[i];
		for (size_t j=0; j<att2->children.size(); j++) {
			Model *mAttChild = static_cast<Model*>(att2->children[j]->model);
			if (mAttChild)
				AddCount(mAttChild, numGroups, numVerts);
		}
	}

	numFaces = (numVerts / 3);

	verts = new ModelData[numVerts];
	//indices = new float[numVerts];
	groups = new GroupData[numGroups];

	wxLogMessage(wxT("Num Verts: %i, Num Faces: %i, Num Groups: %i"), numVerts, numFaces, numGroups);
	wxLogMessage(wxT("Adding Verticies via InitCommon..."));

	if (m)
		AddVertices(m, att, init, verts, vertIndex, groups, grpIndex);

	// children:
	for (size_t i=0; i<att->children.size(); i++) {
		Model *mAtt = static_cast<Model*>(att->children[i]->model);
		if (mAtt)
			AddVertices(mAtt, att->children[i], init, verts, vertIndex, groups, grpIndex);

		Attachment *att2 = att->children[i];
		for (size_t j=0; j<att2->children.size(); j++) {
			Model *mAttChild = static_cast<Model*>(att2->children[j]->model);
			if (mAttChild)
				AddVertices(mAttChild, att2->children[j], init, verts, vertIndex, groups, grpIndex);
		}
	}
	#ifdef _DEBUG
		wxLogMessage(wxT("Vert[0] BoneID: %i"),verts[0].boneid);
	#endif

	wxLogMessage(wxT("Finished InitCommon Function."));
}

// Change a Vec3D so it now faces forwards
void MakeModelFaceForwards(Vec3D &vect, bool flipZ = false){
	Vec3D Temp;

	Temp.x = 0-vect.z;
	Temp.y = vect.y;
	Temp.z = vect.x;
	if (flipZ==true){
		Temp.z = -Temp.z;
		Temp.x = -Temp.x;
	}

	vect = Temp;
}

// Get Proper Texture Names for an M2 File
wxString GetM2TextureName(Model *m, ModelRenderPass p, size_t PassNumber){
	wxString texName;
	if ((int)m->TextureList.size() > p.tex)
		texName = m->TextureList[p.tex].BeforeLast(wxT('.')).AfterLast(SLASH);

	if (texName.Len() == 0)
		texName = m->modelname.BeforeLast(wxT('.')).AfterLast(SLASH) + wxString::Format(wxT("_Image_%03i"),PassNumber);

	return texName;
}

// Write out some debug info
void LogExportData(wxString ExporterExtention, wxString ModelName, wxString Destination){
	wxLogMessage(wxT("\n\n========================================================================\n   Exporting Model...\n========================================================================\n"));
	wxLogMessage(wxT("Exporting Model: %s"),ModelName.c_str());
	wxLogMessage(wxT("Exporting to File: %s"),Destination.c_str());
	wxLogMessage(wxT("Exporting File Type: %s"),ExporterExtention.c_str());
	wxLogMessage(wxT("Export Init Mode: %s"),(modelExportInitOnly==true?"True":"False"));
	wxLogMessage(wxT("Preserve Directories: %s"),(modelExport_PreserveDir==true?"True":"False"));
	wxLogMessage(wxT("Scale to Real World: %s"),(modelExport_ScaleToRealWorld==true?"True":"False"));
	wxLogMessage(wxT("Use WMV Position & Rotation: %s"),(modelExport_UseWMVPosRot==true?"True":"False"));

	// Animation Information
	if (g_canvas->model){
		size_t cAnim = 0;
		size_t cFrame = 0;
		wxString AnimName;

		if (g_canvas->model->animated){
			cAnim = g_selModel->currentAnim;
			cFrame = g_selModel->animManager->GetFrame();
		}

		try {
			AnimDB::Record rec = animdb.getByAnimID(g_selModel->anims[cAnim].animID);
			AnimName = rec.getString(AnimDB::Name);
		} catch (AnimDB::NotFound) {
			AnimName = wxT("???");
		}

		wxLogMessage(wxT("isAnimated: %s, Current Anim: %i (%s), Current Frame: %i"),(g_selModel->animated?"true":"false"), cAnim, AnimName.c_str(), cFrame);
	}

	// Lightwave Options
	if (ExporterExtention == wxT("LWO")){
		wxLogMessage(wxT("Preserve Lightwave Directories: %s"),(modelExport_LW_PreserveDir==true?"True":"False"));
		wxLogMessage(wxT("Always Write Scene File: %s"),(modelExport_LW_AlwaysWriteSceneFile==true?"True":"False"));
		wxLogMessage(wxT("Export Doodads: %s"),(modelExport_LW_ExportDoodads==true?"True":"False"));
		wxLogMessage(wxT("Export Lights: %s"),(modelExport_LW_ExportLights==true?"True":"False"));
		wxLogMessage(wxT("Export Cameras: %s"),(modelExport_LW_ExportCameras==true?"True":"False"));
		wxLogMessage(wxT("Export Bones: %s"),(modelExport_LW_ExportBones==true?"True":"False"));
		wxString XDDas;
		switch (modelExport_LW_DoodadsAs) {
			case 0:
				XDDas = wxT("All Doodads as Nulls");
				break;
			case 1:
				XDDas = wxT("All Doodads as Scene Objects");
				break;
			case 2:
				XDDas = wxT("Each Doodad Set as a Seperate Layer");
				break;
			case 3:
				XDDas = wxT("All Doodads as a Single Layer");
				break;
			case 4:
				XDDas = wxT("Doodads as a Single Layer, Per Group");
				break;
		}
		wxLogMessage(wxT("Export Doodads as: %s"),XDDas.c_str());
	// X3D Options
	}else if (ExporterExtention == wxT("X3D")){
		wxLogMessage(wxT("Export Animation: %s"),(modelExport_X3D_ExportAnimation==true?"True":"False"));
		wxLogMessage(wxT("Center Model: %s"),(modelExport_X3D_CenterModel==true?"True":"False"));
	}else if (ExporterExtention == wxT("M3")){
		wxLogMessage(wxT("Bound Scale: %f"), modelExport_M3_BoundScale);
		wxLogMessage(wxT("Sphere Scale: %f"), modelExport_M3_SphereScale);
		wxLogMessage(wxT("Texture Path: \"%s\""), modelExport_M3_TexturePath.c_str());
		wxLogMessage(wxT("Number Animations to Export: %i"), modelExport_M3_Anims.size());
		if ((modelExport_M3_Anims.size() > 0)&&(g_canvas->model)){
			wxLogMessage(wxT("Animation List:\n	Original Name, Exported Name"));
			for (size_t i=0;i<modelExport_M3_AnimNames.size();i++){
				wxString strName;
				try {
					AnimDB::Record rec = animdb.getByAnimID(g_selModel->anims[modelExport_M3_Anims[i]].animID);
					strName = rec.getString(AnimDB::Name);
				} catch (AnimDB::NotFound) {
					strName = wxT("???");
				}
				wxLogMessage(wxT("	%s, %s"), strName.c_str(), modelExport_M3_AnimNames[i].c_str());
			}
		}
	}
}

// Disabled Collada Exporters
void ExportCOLLADA_M2(Attachment *att, Model *m, const char *fn, bool init){}
void ExportCOLLADA_WMO(WMO *m, const char *fn){}

void SaveBaseFile(){
	if (g_fileControl->fileTree->HasChildren(g_fileControl->CurrentItem)){
		wxLogMessage(wxT("File has children. Cancelling Save Fuction..."));
		return;
	}
	FileTreeData *data = (FileTreeData*)g_fileControl->fileTree->GetItemData(g_fileControl->CurrentItem);
	wxString modelfile(data->fn);

	//modelfile = g_fileControl->fileTree->GetParent()+wxT("\\")+g_fileControl->fileTree->GetItemText(g_fileControl->CurrentItem);
	wxLogMessage(wxT("Original Model File Name: %s"), modelfile.c_str());

	if (modelfile.IsEmpty())
		return;
	MPQFile f(modelfile);
	if (f.isEof()) {
		wxLogMessage(wxT("Error: Could not extract %s\n"), modelfile.c_str());
		f.close();
		return;
	}
	wxFileName fn = fixMPQPath(modelfile);

	FILE *hFile = NULL;
	wxString filename;
	filename = wxFileSelector(wxT("Please select your file to export"), wxGetCwd(), fn.GetName(), fn.GetExt(), fn.GetExt()+wxT(" files (.")+fn.GetExt()+wxT(")|*.")+fn.GetExt());

	if ( !filename.empty() )
	{
		hFile = fopen(filename.mb_str(), "wb");
	}
	if (hFile) {
		fwrite(f.getBuffer(), 1, f.getSize(), hFile);
		fclose(hFile);
	}
	f.close();
}
