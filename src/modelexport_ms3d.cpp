#include <wx/wfstream.h>

#include "globalvars.h"
#include "modelexport.h"
#include "modelexport_ms3d.h"
#include "modelcanvas.h"

//#include "CxImage/ximage.h"

Vec3D FixPivot(Model *m, int index, Vec3D v)
{
    int parent = m->bones[index].parent;

    if (parent == -1)
    {
		return v;
	}
    else
    {
        return FixPivot(m, parent, v - FixPivot(m, parent, m->bones[parent].pivot));
    }
}

//yet another Quaternion to Euler angles function...
Vec3D QuatToEuler(const Quaternion rot)
{
	float sqw = pow(rot.w, 2.0f);
    float sqx = pow(rot.x, 2.0f);
    float sqy = pow(rot.y, 2.0f);
    float sqz = pow(rot.z, 2.0f);

	float rotxrad = atan2f(2.0f * (rot.y * rot.z + rot.x * -rot.w), (-sqx - sqy + sqz + sqw));
    float rotyrad = asinf(-2.0f * (rot.x * rot.z - rot.y * -rot.w));
    float rotzrad = atan2f(2.0f * (rot.x * rot.y + rot.z * -rot.w), (sqx - sqy - sqz + sqw));

    return Vec3D(rotxrad, rotyrad, rotzrad);
}

// MilkShape 3D
void ExportMS3D_M2(Attachment *att, Model *m, const char *fn, bool init)
{
	wxFFileOutputStream f(wxString(fn, wxConvUTF8), wxT("w+b"));

	if (!f.IsOk()) {
		wxLogMessage(wxT("Error: Unable to open file '%s'. Could not export model."), fn);
		return;
	}
	LogExportData(wxT("MS3D"),m->modelname,wxString(fn, wxConvUTF8));
	unsigned short numVerts = 0;
	unsigned short numFaces = 0;
	unsigned short numGroups = 0;
	ModelData *verts = NULL;
	GroupData *groups = NULL;

	//we need the initial position anyway
	InitCommon(att, true, verts, groups, numVerts, numGroups, numFaces);
	//wxLogMessage(wxT("Num Verts: %i, Num Faces: %i, Num Groups: %i"), numVerts, numFaces, numGroups);
	//wxLogMessage(wxT("Vert[0] BoneID: %i, Group[0].m.name = %s"),verts[0].boneid, groups[0].m->name);
	wxLogMessage(wxT("Init Common Complete."));

	// Write the header
	ms3d_header_t header;
	strncpy(header.id, "MS3D000000", sizeof(header.id));
	header.version = 4;

	// Header
	f.Write(reinterpret_cast<char *>(&header), sizeof(ms3d_header_t));
	wxLogMessage(wxT("Header Data Written."));
	// Vertex Count
	f.Write(reinterpret_cast<char *>(&numVerts), sizeof(numVerts));
	//wxLogMessage(wxT("NumVerts: %i"),numVerts);
	
	// Write Vertex data?
	for (size_t i=0; i<numVerts; i++) {
		ms3d_vertex_t vert;
		vert.boneId = verts[i].boneid;
		vert.flags = 0; //SELECTED;
		vert.referenceCount = 0; // what the?
		vert.vertex[0] = verts[i].vertex.x;
		vert.vertex[1] = verts[i].vertex.y;
		vert.vertex[2] = verts[i].vertex.z;
		f.Write(reinterpret_cast<char *>(&vert), sizeof(ms3d_vertex_t));
	}
	wxLogMessage(wxT("Vertex Data Written."));
	// ---------------------------

	// Triangle Count
	f.Write(reinterpret_cast<char *>(&numFaces), sizeof(numFaces));
	//wxLogMessage(wxT("NumFaces: %i"),numFaces);

	// Write Triangle Data?
	for (size_t i=0; i<(unsigned int)numVerts; i+=3) {
		ms3d_triangle_t tri;
		tri.flags = 0; //SELECTED;
		tri.groupIndex = (unsigned char)verts[i].groupIndex;
		tri.smoothingGroup = 1; // 1 - 32

		for (ssize_t j=0; j<3; j++) {
			tri.vertexIndices[j] = (word)i+j;
			tri.s[j] = verts[i+j].tu;
			tri.t[j] = verts[i+j].tv;
			
			tri.vertexNormals[j][0] = verts[i+j].normal.x;
			tri.vertexNormals[j][1] = verts[i+j].normal.y;
			tri.vertexNormals[j][2] = verts[i+j].normal.z;
		}

		f.Write(reinterpret_cast<char *>(&tri), sizeof(ms3d_triangle_t));
	}
	wxLogMessage(wxT("Triangle Data Written."));
	// ---------------------------

	// Number of groups
	f.Write(reinterpret_cast<char *>(&numGroups), sizeof(numGroups));
	//wxLogMessage(wxT("NumGroups: %i"),numGroups);

	unsigned short indiceCount = 0;
	for (unsigned short i=0; i<(unsigned int)numGroups; i++) {
		wxString groupName(wxString::Format(wxT("Geoset_%i"), i));

		const char flags = 0; // SELECTED
		f.Write(&flags, sizeof(flags));

		char name[32];
		strncpy(name, groupName.mb_str(), sizeof(name));
		f.Write(name, sizeof(name));

		unsigned short faceCount = groups[i].p.indexCount / 3;
		f.Write(reinterpret_cast<char *>(&faceCount), sizeof(faceCount));
		
		for (ssize_t k=0; k<faceCount; k++) {
			//triIndices[k] = indiceCount;
			f.Write(reinterpret_cast<char *>(&indiceCount), sizeof(indiceCount));
			indiceCount++;
		}

		unsigned char gIndex = (char)i;
		f.Write(reinterpret_cast<char *>(&gIndex), sizeof(gIndex));
	}
	wxLogMessage(wxT("Group Data Written."));

	// Number of materials (pretty much identical to groups, each group has its own material)
	f.Write(reinterpret_cast<char *>(&numGroups), sizeof(numGroups));
	
	for (unsigned short i=0; i<(unsigned int)numGroups; i++) {
		wxString matName(wxString::Format(wxT("Material_%i"), i));

		ModelRenderPass p = groups[i].p;
		if (p.init(groups[i].m)) {
			ms3d_material_t mat;
			memset(mat.alphamap, '\0', sizeof(mat.alphamap));

			strncpy(mat.name, matName.mb_str(), sizeof(mat.name));
			mat.ambient[0] = 0.7f;
			mat.ambient[1] = 0.7f;
			mat.ambient[2] = 0.7f;
			mat.ambient[3] = 1.0f;
			mat.diffuse[0] = p.ocol.x;
			mat.diffuse[1] = p.ocol.y;
			mat.diffuse[2] = p.ocol.z;
			mat.diffuse[3] = p.ocol.w;
			mat.specular[0] = 0.0f;
			mat.specular[1] = 0.0f;
			mat.specular[2] = 0.0f;
			mat.specular[3] = 1.0f;
			mat.emissive[0] = p.ecol.x;
			mat.emissive[1] = p.ecol.y;
			mat.emissive[2] = p.ecol.z;
			mat.emissive[3] = p.ecol.w;
			mat.transparency = p.ocol.w;

			if (p.useEnvMap) {
				mat.shininess = 30.0f;
				mat.mode = 1;
			} else {
				mat.shininess = 0.0f;
				mat.mode = 0;
			}
/*
			unsigned int bindtex = 0;
			if (groups[i].m->specialTextures[p.tex]==-1) 
				bindtex = groups[i].m->textures[p.tex];
			else 
				bindtex = groups[i].m->replaceTextures[groups[i].m->specialTextures[p.tex]];
*/
			wxString texName = GetM2TextureName(m,p,i);
			texName << wxT(".tga");
			strncpy(mat.texture, texName.mb_str(), sizeof(mat.texture));

			f.Write(reinterpret_cast<char *>(&mat), sizeof(ms3d_material_t));

			wxString texFilename(fn, wxConvUTF8);
			texFilename = texFilename.BeforeLast(SLASH);
			texFilename += SLASH;
			texFilename += texName;
			wxLogMessage(wxT("Exporting Image: %s"),texFilename.c_str());
			SaveTexture(texFilename);
		}
	}
	wxLogMessage(wxT("Material Data Written."));

	if (init)
	{
		float fps = 1.0f;
		float fCurTime = 0.0f;
		int totalFrames = 0;

		f.Write(reinterpret_cast<char *>(&fps), sizeof(fps));
		f.Write(reinterpret_cast<char *>(&fCurTime), sizeof(fCurTime));
		f.Write(reinterpret_cast<char *>(&totalFrames), sizeof(totalFrames));
		
		// number of joints
		unsigned short numJoints = 0;

		f.Write(reinterpret_cast<char *>(&numJoints), sizeof(numJoints));
	}
	else
	{
		float fps = 25.0f;
		float fCurTime = 0.0f;
		int totalFrames = ceil((m->anims[m->anim].timeEnd - m->anims[m->anim].timeStart) / 1000.0f * fps);

		f.Write(reinterpret_cast<char *>(&fps), sizeof(fps));
		f.Write(reinterpret_cast<char *>(&fCurTime), sizeof(fCurTime));
		f.Write(reinterpret_cast<char *>(&totalFrames), sizeof(totalFrames));
		
		// number of joints
		unsigned short numJoints = (unsigned short)m->header.nBones;

		f.Write(reinterpret_cast<char *>(&numJoints), sizeof(numJoints));

		for (size_t i=0; i<numJoints; i++)
		{
			ms3d_joint_t joint;

			int parent = m->bones[i].parent;

			joint.flags = 0; // SELECTED
			memset(joint.name, '\0', sizeof(joint.name));
			snprintf(joint.name, sizeof(joint.name), "Bone_%i", i);
			memset(joint.parentName, '\0', sizeof(joint.parentName));
			if (parent != -1) snprintf(joint.parentName, sizeof(joint.parentName), "Bone_%i", parent);

			joint.rotation[0] = 0;
			joint.rotation[1] = 0;
			joint.rotation[2] = 0;

			Vec3D p = FixPivot(m, (int)i, m->bones[i].pivot);
			joint.position[0] = p.x;
			joint.position[1] = p.y;
			joint.position[2] = p.z;

			joint.numKeyFramesRot = (unsigned short)m->bones[i].rot.data[m->anim].size();
			joint.numKeyFramesTrans = (unsigned short)m->bones[i].trans.data[m->anim].size();

			f.Write(reinterpret_cast<char *>(&joint), sizeof(ms3d_joint_t));

			if (joint.numKeyFramesRot > 0)
			{
				ms3d_keyframe_rot_t *keyFramesRot = new ms3d_keyframe_rot_t[joint.numKeyFramesRot];
				for (size_t j=0; j<joint.numKeyFramesRot; j++)
				{
					keyFramesRot[j].time = m->bones[i].rot.times[m->anim][j] / 1000.0f;
					Vec3D euler = QuatToEuler(m->bones[i].rot.data[m->anim][j]);
					keyFramesRot[j].rotation[0] = euler.x;
					keyFramesRot[j].rotation[1] = euler.y;
					keyFramesRot[j].rotation[2] = euler.z;
				}

				f.Write(reinterpret_cast<char *>(keyFramesRot), sizeof(ms3d_keyframe_rot_t) * joint.numKeyFramesRot);
				wxDELETEA(keyFramesRot);
			}

			if (joint.numKeyFramesTrans > 0)
			{
				ms3d_keyframe_pos_t *keyFramesTrans = new ms3d_keyframe_pos_t[joint.numKeyFramesTrans];
				for (size_t j=0; j<joint.numKeyFramesTrans; j++)
				{
					keyFramesTrans[j].time = m->bones[i].trans.times[m->anim][j] / 1000.0f;
					keyFramesTrans[j].position[0] = m->bones[i].trans.data[m->anim][j].x;
					keyFramesTrans[j].position[1] = m->bones[i].trans.data[m->anim][j].y;
					keyFramesTrans[j].position[2] = m->bones[i].trans.data[m->anim][j].z;
				}

				f.Write(reinterpret_cast<char *>(keyFramesTrans), sizeof(ms3d_keyframe_pos_t) * joint.numKeyFramesTrans);
				wxDELETEA(keyFramesTrans);
			}
		}
	}
	f.Close();
	wxLogMessage(wxT("Finished Milkshape Export."));

	if (verts){
		//wxLogMessage("verts found. Deleting...");
		wxDELETEA(verts);
	}
	if (groups){
		//wxLogMessage("groups found. Deleting...");
		wxDELETEA(groups);
	}

	//wxLogMessage(wxT("Finished Milkshape Cleanup.\n"));
}