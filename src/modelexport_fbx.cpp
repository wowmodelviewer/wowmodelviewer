#include "modelcanvas.h"
#include "modelexport.h"
#include "database.h"
#include "globalvars.h"

#include <wx/textdlg.h>

#define KFBX_DLLINFO
#ifdef _O_RDONLY
#undef _O_RDONLY
#endif
#ifdef _O_WRONLY
#undef _O_WRONLY
#endif
#include <fbxsdk.h>

#ifdef IOS_REF
#undef  IOS_REF
#define IOS_REF (*(pSdkManager->GetIOSettings()))
#endif

#define SCALE_FACTOR 50.0f

wxString g_fbx_meshname;
wxString g_fbx_basename;
wxString g_fbx_name;

void InitializeSdkObjects(KFbxSdkManager*& pSdkManager, KFbxScene*& pScene) {
    // The first thing to do is to create the FBX SDK manager which is the 
    // object allocator for almost all the classes in the SDK.
    pSdkManager = KFbxSdkManager::Create();

    if (!pSdkManager)
    {
        printf("Unable to create the FBX SDK manager\n");
        exit(0);
    }

	// create an IOSettings object
	KFbxIOSettings * ios = KFbxIOSettings::Create(pSdkManager, IOSROOT );
	pSdkManager->SetIOSettings(ios);

	// Load plugins from the executable directory
	KString lPath = KFbxGetApplicationDirectory();
	lPath += "FBXPlugins" + SLASH;
#if defined(KARCH_ENV_WIN)
	KString lExtension = "dll";
#elif defined(KARCH_ENV_MACOSX)
	KString lExtension = "dylib";
#elif defined(KARCH_ENV_LINUX)
	KString lExtension = "so";
#endif
	pSdkManager->LoadPluginsDirectory(lPath.Buffer(), lExtension.Buffer());

    // Create the entity that will hold the scene.
    pScene = KFbxScene::Create(pSdkManager,"");
}

void DestroySdkObjects(KFbxSdkManager* pSdkManager)
{
    // Delete the FBX SDK manager. All the objects that have been allocated 
    // using the FBX SDK manager and that haven't been explicitly destroyed 
    // are automatically destroyed at the same time.
    if (pSdkManager) pSdkManager->Destroy();
    pSdkManager = NULL;
}

bool SaveScene(KFbxSdkManager* pSdkManager, KFbxDocument* pScene, const char* pFilename, int pFileFormat=-1, bool pEmbedMedia=false)
{
    int lMajor, lMinor, lRevision;
    bool lStatus = true;

    // Create an exporter.
    KFbxExporter* lExporter = KFbxExporter::Create(pSdkManager, "");

    if( pFileFormat < 0 || pFileFormat >= pSdkManager->GetIOPluginRegistry()->GetWriterFormatCount() )
    {
        // Write in fall back format if pEmbedMedia is true
        pFileFormat = pSdkManager->GetIOPluginRegistry()->GetNativeWriterFormat();

        if (!pEmbedMedia)
        {
            //Try to export in ASCII if possible
            int lFormatIndex, lFormatCount = pSdkManager->GetIOPluginRegistry()->GetWriterFormatCount();

            for (lFormatIndex=0; lFormatIndex<lFormatCount; lFormatIndex++)
            {
                if (pSdkManager->GetIOPluginRegistry()->WriterIsFBX(lFormatIndex))
                {
                    KString lDesc =pSdkManager->GetIOPluginRegistry()->GetWriterFormatDescription(lFormatIndex);
                    //char* format = "ascii";
					char* format = "binary";
                    if (lDesc.Find(format)>=0)
                    {
                        pFileFormat = lFormatIndex;
                        break;
                    }
                }
            }
        }
    }

    // Set the export states. By default, the export states are always set to 
    // true except for the option eEXPORT_TEXTURE_AS_EMBEDDED. The code below 
    // shows how to change these states.

    IOS_REF.SetBoolProp(EXP_FBX_MATERIAL,        true);
    IOS_REF.SetBoolProp(EXP_FBX_TEXTURE,         true);
    IOS_REF.SetBoolProp(EXP_FBX_EMBEDDED,        pEmbedMedia);
    IOS_REF.SetBoolProp(EXP_FBX_SHAPE,           true);
    IOS_REF.SetBoolProp(EXP_FBX_GOBO,            true);
    IOS_REF.SetBoolProp(EXP_FBX_ANIMATION,       true);
    IOS_REF.SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

    // Initialize the exporter by providing a filename.
    if(lExporter->Initialize(pFilename, pFileFormat, pSdkManager->GetIOSettings()) == false)
    {
        printf("Call to KFbxExporter::Initialize() failed.\n");
        printf("Error returned: %s\n\n", lExporter->GetLastErrorString());
        return false;
    }

    KFbxSdkManager::GetFileFormatVersion(lMajor, lMinor, lRevision);
    printf("FBX version number for this version of the FBX SDK is %d.%d.%d\n\n", lMajor, lMinor, lRevision);

    // Export the scene.
    lStatus = lExporter->Export(pScene); 

    // Destroy the exporter.
    lExporter->Destroy();
    return lStatus;
}

bool LoadScene(KFbxSdkManager* pSdkManager, KFbxDocument* pScene, const char* pFilename)
{
    int lFileMajor, lFileMinor, lFileRevision;
    int lSDKMajor,  lSDKMinor,  lSDKRevision;
    //int lFileFormat = -1;
    int i, lAnimStackCount;
    bool lStatus;
    char lPassword[1024];

    // Get the file version number generate by the FBX SDK.
    KFbxSdkManager::GetFileFormatVersion(lSDKMajor, lSDKMinor, lSDKRevision);

    // Create an importer.
    KFbxImporter* lImporter = KFbxImporter::Create(pSdkManager,"");

    // Initialize the importer by providing a filename.
    const bool lImportStatus = lImporter->Initialize(pFilename, -1, pSdkManager->GetIOSettings());
    lImporter->GetFileVersion(lFileMajor, lFileMinor, lFileRevision);

    if( !lImportStatus )
    {
        printf("Call to KFbxImporter::Initialize() failed.\n");
        printf("Error returned: %s\n\n", lImporter->GetLastErrorString());

        if (lImporter->GetLastErrorID() == KFbxIO::eFILE_VERSION_NOT_SUPPORTED_YET ||
            lImporter->GetLastErrorID() == KFbxIO::eFILE_VERSION_NOT_SUPPORTED_ANYMORE)
        {
            printf("FBX version number for this FBX SDK is %d.%d.%d\n", lSDKMajor, lSDKMinor, lSDKRevision);
            printf("FBX version number for file %s is %d.%d.%d\n\n", pFilename, lFileMajor, lFileMinor, lFileRevision);
        }

        return false;
    }

    printf("FBX version number for this FBX SDK is %d.%d.%d\n", lSDKMajor, lSDKMinor, lSDKRevision);

    if (lImporter->IsFBX())
    {
        printf("FBX version number for file %s is %d.%d.%d\n\n", pFilename, lFileMajor, lFileMinor, lFileRevision);

        // From this point, it is possible to access animation stack information without
        // the expense of loading the entire file.

        printf("Animation Stack Information\n");

        lAnimStackCount = lImporter->GetAnimStackCount();

        printf("    Number of Animation Stacks: %d\n", lAnimStackCount);
        printf("    Current Animation Stack: \"%s\"\n", lImporter->GetActiveAnimStackName().Buffer());
        printf("\n");

        for(i = 0; i < lAnimStackCount; i++)
        {
            KFbxTakeInfo* lTakeInfo = lImporter->GetTakeInfo(i);

            printf("    Animation Stack %d\n", i);
            printf("         Name: \"%s\"\n", lTakeInfo->mName.Buffer());
            printf("         Description: \"%s\"\n", lTakeInfo->mDescription.Buffer());

            // Change the value of the import name if the animation stack should be imported 
            // under a different name.
            printf("         Import Name: \"%s\"\n", lTakeInfo->mImportName.Buffer());

            // Set the value of the import state to false if the animation stack should be not
            // be imported. 
            printf("         Import State: %s\n", lTakeInfo->mSelect ? "true" : "false");
            printf("\n");
        }

        // Set the import states. By default, the import states are always set to 
        // true. The code below shows how to change these states.
        IOS_REF.SetBoolProp(IMP_FBX_MATERIAL,        true);
        IOS_REF.SetBoolProp(IMP_FBX_TEXTURE,         true);
        IOS_REF.SetBoolProp(IMP_FBX_LINK,            true);
        IOS_REF.SetBoolProp(IMP_FBX_SHAPE,           true);
        IOS_REF.SetBoolProp(IMP_FBX_GOBO,            true);
        IOS_REF.SetBoolProp(IMP_FBX_ANIMATION,       true);
        IOS_REF.SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);
    }

    // Import the scene.
    lStatus = lImporter->Import(pScene);

    if(lStatus == false && lImporter->GetLastErrorID() == KFbxIO::ePASSWORD_ERROR)
    {
        printf("Please enter password: ");

        lPassword[0] = '\0';

        scanf("%s", lPassword);
        KString lString(lPassword);

        IOS_REF.SetStringProp(IMP_FBX_PASSWORD,      lString);
        IOS_REF.SetBoolProp(IMP_FBX_PASSWORD_ENABLE, true);

        lStatus = lImporter->Import(pScene);

        if(lStatus == false && lImporter->GetLastErrorID() == KFbxIO::ePASSWORD_ERROR)
        {
            printf("\nPassword is wrong, import aborted.\n");
        }
    }

    // Destroy the importer.
    lImporter->Destroy();

    return lStatus;
}

// Create materials.
void CreateMaterials(KFbxSdkManager* sdk_mgr, KFbxScene* scene, Model* m, const char* fn) {
	size_t num_of_passes = m->passes.size();
	for (size_t i = 0; i < num_of_passes; ++i) {
		ModelRenderPass& pass = m->passes[i];
		if (pass.init(m)) {
			// Build material name.
			KString mtrl_name = g_fbx_name.c_str();
			mtrl_name.Append("_", 1);
			char tmp[32];
			itoa((int)i, tmp, 10);
			mtrl_name.Append(tmp, strlen(tmp));

			// Create material.
			KString shading_name = "Phong";
			KFbxSurfacePhong* material = KFbxSurfacePhong::Create(sdk_mgr, mtrl_name.Buffer());
			material->GetAmbientColor().Set(fbxDouble3(0.7, 0.7, 0.7));

			wxString tex_name = GetM2TextureName(m, pass, i) + wxT(".tga");
			wxString tex_fullpath_filename = g_fbx_basename.BeforeLast(SLASH) + wxT(SLASH) + tex_name;
			SaveTexture(tex_fullpath_filename);
			KFbxTexture* texture = KFbxTexture::Create(sdk_mgr, tex_name.c_str());
			texture->SetFileName(tex_fullpath_filename.c_str());
			texture->SetTextureUse(KFbxTexture::eSTANDARD);
			texture->SetMappingType(KFbxTexture::eUV);
			texture->SetMaterialUse(KFbxTexture::eMODEL_MATERIAL);
			texture->SetSwapUV(false);
			texture->SetTranslation(0.0, 0.0);
			texture->SetScale(1.0, 1.0);
			texture->SetRotation(0.0, 0.0);
			material->GetDiffuseColor().ConnectSrcObject(texture);

			// Add material to the scene.
			scene->AddMaterial(material);
		}
	}
}

// Create mesh.
void CreateMesh(KFbxSdkManager* sdk_mgr, KFbxScene* scene, Model* m, const char* fn) {
	// Get the scene¡¯s root node.
	KFbxNode* root_node = scene->GetRootNode();
	// Create a node for the mesh.
	KFbxNode* node = KFbxNode::Create(sdk_mgr, g_fbx_name.c_str());

	// Set the node as a child of the scene¡¯s root node.
	root_node->AddChild(node);

	// Create mesh.
	size_t num_of_vertices = m->header.nVertices;
	KFbxMesh* mesh = KFbxMesh::Create(sdk_mgr, "");
	mesh->InitControlPoints((int)num_of_vertices);
	KFbxVector4* vertices = mesh->GetControlPoints();

	// Set the normals on Layer 0.
	KFbxLayer* layer = mesh->GetLayer(0);
	if (layer == 0) {
		mesh->CreateLayer();
		layer = mesh->GetLayer(0);
	}

	// We want to have one normal for each vertex (or control point),
	// so we set the mapping mode to eBY_CONTROL_POINT.
	KFbxLayerElementNormal* layer_normal= KFbxLayerElementNormal::Create(mesh, "");
	layer_normal->SetMappingMode(KFbxLayerElement::eBY_CONTROL_POINT);
	layer_normal->SetReferenceMode(KFbxLayerElement::eDIRECT);
	layer->SetNormals(layer_normal);

	// Create UV for Diffuse channel.
	KFbxLayerElementUV* layer_texcoord = KFbxLayerElementUV::Create(mesh, "DiffuseUV");
	layer_texcoord->SetMappingMode(KFbxLayerElement::eBY_CONTROL_POINT);
	layer_texcoord->SetReferenceMode(KFbxLayerElement::eDIRECT);
	layer->SetUVs(layer_texcoord, KFbxLayerElement::eDIFFUSE_TEXTURES);

	// Fill data.
	for (size_t i = 0; i < num_of_vertices; i++) {
		ModelVertex &v = m->origVertices[i];
		vertices[i].Set(v.pos.x * SCALE_FACTOR, v.pos.y * SCALE_FACTOR, v.pos.z * SCALE_FACTOR);
		layer_normal->GetDirectArray().Add(KFbxVector4(v.normal.x, v.normal.y, v.normal.z));
		layer_texcoord->GetDirectArray().Add(KFbxVector2(v.texcoords.x, 1.0 - v.texcoords.y));
	}

	// Create polygons.
	size_t num_of_passes = m->passes.size();
	KFbxLayerElementMaterial* layer_material=KFbxLayerElementMaterial::Create(mesh, "");
	layer_material->SetMappingMode(KFbxLayerElement::eBY_POLYGON);
	layer_material->SetReferenceMode(KFbxLayerElement::eINDEX_TO_DIRECT);
	layer->SetMaterials(layer_material);

	int mtrl_index = 0;
	for (size_t i = 0; i < num_of_passes; i++) {
		ModelRenderPass& p = m->passes[i];
		if (p.init(m)) {
			// Build material name.
			KString mtrl_name = g_fbx_name.c_str();
			mtrl_name.Append("_", 1);
			char tmp[32];
			itoa((int)i, tmp, 10);
			mtrl_name.Append(tmp, strlen(tmp));
			KFbxSurfaceMaterial* material = scene->GetMaterial(mtrl_name.Buffer());
			node->AddMaterial(material);

			ModelGeoset g = m->geosets[p.geoset];
			size_t num_of_faces = g.icount / 3;
			for (size_t j = 0; j < num_of_faces; j++) {
				mesh->BeginPolygon(mtrl_index);
				mesh->AddPolygon(m->indices[g.istart + j * 3]);
				mesh->AddPolygon(m->indices[g.istart + j * 3 + 1]);
				mesh->AddPolygon(m->indices[g.istart + j * 3 + 2]);
				mesh->EndPolygon();
			}

			mtrl_index++;
		}
	}

	// Set mesh smoothness.
	mesh->SetMeshSmoothness(KFbxMesh::eFINE);

	// Set the mesh as the node attribute of the node.
	node->SetNodeAttribute(mesh);
	// Set the shading mode to view texture.
	node->SetShadingMode(KFbxNode::eTEXTURE_SHADING);
}

static bool uses_anim(Bone &b, size_t anim_index) {
	return (b.trans.uses(anim_index) || b.rot.uses(anim_index) || b.scale.uses(anim_index));
}

bool has_anim(Model* m, size_t anim_index) {
	for (size_t n = 0; n < m->header.nBones; n++) {
		Bone& b = m->bones[n];
		if (uses_anim(b, anim_index))
			return true;
	}
	return false;
}
typedef size_t			TimeT;
typedef map<TimeT, int>	Timeline;

static const int KEY_TRANSLATE	= 1;
static const int KEY_ROTATE		= 2;
static const int KEY_SCALE		= 4;

static void updateTimeline(Timeline &timeline, vector<TimeT> &times, int keyMask) {
	size_t numTimes = times.size();
	for (size_t n = 0; n < numTimes; n++) {
		TimeT time = times[n];
		Timeline::iterator it = timeline.find(time);
		if (it != timeline.end()) {
			it->second |= keyMask;
		}
		else {
			timeline[time] = keyMask;
		}
	}
}

static void QuaternionToAxisAngle(const Quaternion q, Vec4D &v) {
	float sqrLength = q.x * q.x + q.y * q.y + q.z * q.z;
	if (sqrLength > 0) {
		float invLength = 1 / sqrtf(sqrLength);
		v.x = q.x * invLength;
		v.y = q.y * invLength;
		v.z = q.z * invLength;
		v.w = 2.0f * acosf(q.w);
	}
	else {
		v.w = 0;
		v.x = 1;
		v.y = 0;
		v.z = 0;
	}
}

#define FBX_PI		(3.14159265358979323846)

// Create skeleton.
void CreateSkeleton(KFbxSdkManager* sdk_mgr, KFbxScene* scene, Model* m, const char* fn) {
	// Get the scene¡¯s root node.
	KFbxNode* root_node = scene->GetRootNode();
	// Get the mesh's node.
	KFbxNode* node = root_node->FindChild(g_fbx_name.c_str());
	KFbxNode* bone_group_node = KFbxNode::Create(scene, g_fbx_name.c_str());
	KFbxSkeleton* bone_group_skeleton_attribute = KFbxSkeleton::Create(scene, g_fbx_name.c_str());
	bone_group_skeleton_attribute->SetSkeletonType(KFbxSkeleton::eROOT);
	bone_group_skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
	bone_group_node->SetNodeAttribute(bone_group_skeleton_attribute);
	root_node->AddChild(bone_group_node);
	KFbxXMatrix matrix;

	KFbxSkin* skin = KFbxSkin::Create(scene, "");
	std::vector<KFbxNode*> bone_nodes;
	std::vector<KFbxCluster*> bone_clusters;
	std::vector<KFbxSkeleton::ESkeletonType> bone_types;
	size_t num_of_bones = m->header.nBones;
	// Set bone type.
	for (size_t i = 0; i < num_of_bones; ++i) {
		Bone &bone = m->bones[i];
		if (bone.parent == -1) {
			bone_types.push_back(KFbxSkeleton::eROOT);
		} else {
			if (bone_types[bone.parent] != KFbxSkeleton::eROOT)
				bone_types[bone.parent] = KFbxSkeleton::eLIMB;
			bone_types.push_back(KFbxSkeleton::eLIMB_NODE);
		}
	}
	// Create bone.
	for (size_t i = 0; i < num_of_bones; ++i) {
		Bone &bone = m->bones[i];
		Vec3D trans = bone.pivot;
		int pid = bone.parent;
		if (pid > -1)
			trans -= m->bones[pid].pivot;

		KString bone_name(g_fbx_name.c_str());
		bone_name += "_bone_";
		int j = 0;
		for (; j < BONE_MAX; ++j) {
			if (m->keyBoneLookup[j] == static_cast<int>(i)) {
				bone_name += Bone_Names[j].c_str();
				break;
			}
		}
		if (j == BONE_MAX) bone_name += static_cast<int>(i);

		KFbxNode* skeleton_node = KFbxNode::Create(scene, bone_name);
		bone_nodes.push_back(skeleton_node);
		skeleton_node->LclTranslation.Set(KFbxVector4(trans.x * SCALE_FACTOR, trans.y * SCALE_FACTOR, trans.z * SCALE_FACTOR));

		KFbxSkeleton* skeleton_attribute = KFbxSkeleton::Create(scene, bone_name);
		if (bone_types[i] == KFbxSkeleton::eROOT) {
			skeleton_attribute->SetSkeletonType(KFbxSkeleton::eROOT);
			skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
			bone_group_node->AddChild(skeleton_node);
		} else if (bone_types[i] == KFbxSkeleton::eLIMB) {
			skeleton_attribute->SetSkeletonType(KFbxSkeleton::eLIMB);
			skeleton_attribute->LimbLength.Set(100.0 * SCALE_FACTOR * (sqrtf(trans.x * trans.x + trans.y * trans.y + trans.z * trans.z)));
			bone_nodes[pid]->AddChild(skeleton_node);
		} else {
			skeleton_attribute->SetSkeletonType(KFbxSkeleton::eLIMB_NODE);
			skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
			bone_nodes[pid]->AddChild(skeleton_node);
		}
		skeleton_node->SetNodeAttribute(skeleton_attribute);

		KFbxCluster* cluster = KFbxCluster::Create(scene, "");
		bone_clusters.push_back(cluster);
		cluster->SetLink(skeleton_node);
		cluster->SetLinkMode(KFbxCluster::eTOTAL1);

		matrix = scene->GetEvaluator()->GetNodeGlobalTransform(node);
		cluster->SetTransformMatrix(matrix);
		matrix = scene->GetEvaluator()->GetNodeGlobalTransform(skeleton_node);
		cluster->SetTransformLinkMatrix(matrix);
		skin->AddCluster(bone_clusters[i]);
	}

	size_t num_of_vertices = m->header.nVertices;
	for (size_t i = 0; i < num_of_vertices; i++) {
		ModelVertex& vertex = m->origVertices[i];
		for (size_t j = 0; j < 4; j++) {
			if ((vertex.bones[j] == 0) && (vertex.weights[j] == 0))
				continue;
			bone_clusters[vertex.bones[j]]->AddControlPointIndex((int)i, static_cast<double>(vertex.weights[j]) / 255.0);
		}
	}

	skin->SetGeometry(node->GetMesh());

	// Create animation.
  wxString sel_anim_name;
#ifdef _DEBUG
  sel_anim_name = wxT("Stand");
#endif
  wxTextEntryDialog sel_anim_dlg(
    g_modelViewer,
    wxT("Please input the animation name:\r\n(Keep empty means all animations)"),
    wxT("Select animation to export"),
    sel_anim_name,
    wxOK);
  sel_anim_dlg.ShowModal();
  sel_anim_name = sel_anim_dlg.GetValue();
	map<wxString, int> anim_names;
	size_t num_of_anims = m->header.nAnimations;
	for (size_t i = 0; i < num_of_anims; ++i) {
		if (has_anim(m, i)) {
			ModelAnimation& anim = m->anims[i];
			wxString anim_name;
			try {
				AnimDB::Record r = animdb.getByAnimID(anim.animID);
				anim_name = r.getString(AnimDB::Name);
			} catch (DBCFile::NotFound &) {
				anim_name = wxString::Format(wxT("Unknown_%i"), anim.animID);
			}
			map<wxString, int>::iterator it = anim_names.find(anim_name);
			if (it == anim_names.end()) {
				anim_names[anim_name] = 0;
			} else {
				it->second++;
				anim_name += wxString::Format(wxT("%i"), it->second);
			}
      if (!sel_anim_name.IsEmpty() && anim_name != sel_anim_name)
        continue;

			// Animation stack and layer.
			KFbxAnimStack* anim_stack = KFbxAnimStack::Create(scene, anim_name.c_str());
			KFbxAnimLayer* anim_layer = KFbxAnimLayer::Create(scene, anim_name.c_str());
			anim_stack->AddMember(anim_layer);

			size_t num_of_bones = m->header.nBones;
			for (size_t j = 0; j < num_of_bones; ++j) {
				Bone& bone = m->bones[j];
				if (uses_anim(bone, i)) {
					Timeline timeline;
					updateTimeline(timeline, bone.trans.times[i], KEY_TRANSLATE);
					updateTimeline(timeline, bone.rot.times[i], KEY_ROTATE);
					updateTimeline(timeline, bone.scale.times[i], KEY_SCALE);
					size_t ntrans = 0;
					size_t nrot = 0;
					size_t nscale = 0;

					KFbxAnimCurve* t_curve_x = bone_nodes[j]->LclTranslation.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_T_X, true);
					KFbxAnimCurve* t_curve_y = bone_nodes[j]->LclTranslation.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_T_Y, true);
					KFbxAnimCurve* t_curve_z = bone_nodes[j]->LclTranslation.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_T_Z, true);
					KFbxAnimCurve* r_curve_x = bone_nodes[j]->LclRotation.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_R_X, true);
					KFbxAnimCurve* r_curve_y = bone_nodes[j]->LclRotation.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_R_Y, true);
					KFbxAnimCurve* r_curve_z = bone_nodes[j]->LclRotation.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_R_Z, true);
					KFbxAnimCurve* s_curve_x = bone_nodes[j]->LclScaling.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_S_X, true);
					KFbxAnimCurve* s_curve_y = bone_nodes[j]->LclScaling.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_S_Y, true);
					KFbxAnimCurve* s_curve_z = bone_nodes[j]->LclScaling.GetCurve<KFbxAnimCurve>(anim_layer, KFCURVENODE_S_Z, true);

					KTime time;
					for (Timeline::iterator it = timeline.begin(); it != timeline.end(); it++) {
						time.SetSecondDouble(static_cast<double>(it->first) / 1000.0);
						if (it->second & KEY_TRANSLATE && ntrans < bone.trans.data[i].size()) {
							Vec3D v = bone.trans.getValue(i, it->first);
							if (bone.parent >= 0) {
								Bone& parent_bone = m->bones[bone.parent];
								v += (bone.pivot - parent_bone.pivot);
							}
							ntrans++;

							t_curve_x->KeyModifyBegin();
							int key_index = t_curve_x->KeyAdd(time);
							t_curve_x->KeySetValue(key_index, v.x * SCALE_FACTOR);
							t_curve_x->KeySetInterpolation(key_index, bone.trans.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							t_curve_x->KeyModifyEnd();

							t_curve_y->KeyModifyBegin();
							key_index = t_curve_y->KeyAdd(time);
							t_curve_y->KeySetValue(key_index, v.y * SCALE_FACTOR);
							t_curve_y->KeySetInterpolation(key_index, bone.trans.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							t_curve_y->KeyModifyEnd();

							t_curve_z->KeyModifyBegin();
							key_index = t_curve_z->KeyAdd(time);
							t_curve_z->KeySetValue(key_index, v.z * SCALE_FACTOR);
							t_curve_z->KeySetInterpolation(key_index, bone.trans.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							t_curve_z->KeyModifyEnd();
						}
						if (it->second & KEY_ROTATE) {
							float x, y, z;
							Quaternion q = bone.rot.getValue(i, it->first);
							Quaternion tq;
							tq.x = q.w; tq.y = q.x; tq.z = q.y; tq.w = q.z;
							Matrix mtx;
							QuaternionToRotationMatrix(tq, mtx);
							RotationMatrixToEulerAnglesXYZ(mtx, x, y, z);
							x = x * (180.0f / static_cast<float>(FBX_PI));
							y = y * (180.0f / static_cast<float>(FBX_PI));
							z = z * (180.0f / static_cast<float>(FBX_PI));
							nrot++;

							r_curve_x->KeyModifyBegin();
							int key_index = r_curve_x->KeyAdd(time);
							r_curve_x->KeySetValue(key_index,-x);
							r_curve_x->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							r_curve_x->KeyModifyEnd();

							r_curve_y->KeyModifyBegin();
							key_index = r_curve_y->KeyAdd(time);
							r_curve_y->KeySetValue(key_index,-y);
							r_curve_y->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							r_curve_y->KeyModifyEnd();

							r_curve_z->KeyModifyBegin();
							key_index = r_curve_z->KeyAdd(time);
							r_curve_z->KeySetValue(key_index,-z);
							r_curve_z->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							r_curve_z->KeyModifyEnd();
						}
						if (it->second & KEY_SCALE) {
							Vec3D& v = bone.scale.getValue(i, it->first);
							nscale++;

							s_curve_x->KeyModifyBegin();
							int key_index = s_curve_x->KeyAdd(time);
							s_curve_x->KeySetValue(key_index, v.x);
							s_curve_x->KeySetInterpolation(key_index, bone.scale.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							s_curve_x->KeyModifyEnd();

							s_curve_y->KeyModifyBegin();
							key_index = s_curve_y->KeyAdd(time);
							s_curve_y->KeySetValue(key_index, v.y);
							s_curve_y->KeySetInterpolation(key_index, bone.scale.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							s_curve_y->KeyModifyEnd();

							s_curve_z->KeyModifyBegin();
							key_index = s_curve_z->KeyAdd(time);
							s_curve_z->KeySetValue(key_index, v.z);
							s_curve_z->KeySetInterpolation(key_index, bone.scale.type == INTERPOLATION_LINEAR ? KFbxAnimCurveDef::eINTERPOLATION_LINEAR : KFbxAnimCurveDef::eINTERPOLATION_CUBIC);
							s_curve_z->KeyModifyEnd();
						}
					}
				}
			}
		}
	}
}

// FBX
void ExportFBX_M2(Model* m, const char* fn, bool init) {
	g_fbx_meshname = wxString(fn, wxConvUTF8);
	g_fbx_basename = (g_fbx_meshname.Right(4).CmpNoCase(wxT(".fbx")) == 0) ? (g_fbx_meshname.Left(g_fbx_meshname.Length() - 4)) : g_fbx_meshname;
	g_fbx_name = g_fbx_basename.AfterLast(SLASH);

	LogExportData(wxT("FBX"),m->modelname,wxString(fn, wxConvUTF8));

	KFbxSdkManager* sdk_mgr = 0;
	KFbxScene* scene = NULL;

	// Prepare the FBX SDK.
	InitializeSdkObjects(sdk_mgr, scene);

    // Create the scene.
	KFbxDocumentInfo* scene_info = KFbxDocumentInfo::Create(sdk_mgr,"scene_info");
	scene_info->mTitle		= "WMOtoFBX scene";
	scene_info->mSubject	= "wowmodelview export file.";
	scene_info->mAuthor		= "wowmodelview";
	scene_info->mRevision	= "rev. 1.0";
	scene_info->mKeywords	= "wowmodelview, wow, WMO";
	scene_info->mComment	= "";

	// Create materials.
	CreateMaterials(sdk_mgr, scene, m, fn);
	// Create mesh.
	CreateMesh(sdk_mgr, scene, m, fn);
	// Create skeleton.
	CreateSkeleton(sdk_mgr, scene, m, fn);

	// Save the scene.
	if (SaveScene(sdk_mgr, scene, fn) == false)
        wxMessageBox(wxT("An error occurred while saving the scene..."), wxT("Error"));

	// Destroy all objects created by the FBX SDK.
	DestroySdkObjects(sdk_mgr);
}

void ExportFBX_WMO(WMO* m, const char* fn) {
 //   LogExportData(wxT("FBX"),wxString(fn, wxConvUTF8).BeforeLast(SLASH),wxT("WMO"));
 //   KFbxSdkManager* sdk_mgr = 0;
 //   KFbxScene* scene = NULL;

 //   // Prepare the FBX SDK.
 //   InitializeSdkObjects(sdk_mgr, scene);

 //   // Create the scene.
	//KFbxDocumentInfo* scene_info = KFbxDocumentInfo::Create(sdk_mgr,"scene_info");
	//scene_info->mTitle		= "WMOtoFBX scene";
	//scene_info->mSubject	= "wowmodelview export file.";
	//scene_info->mAuthor		= "wowmodelview";
	//scene_info->mRevision	= "rev. 1.0";
	//scene_info->mKeywords	= "wowmodelview, wow, WMO";
	//scene_info->mComment	= "";

 //   // Destroy all objects created by the FBX SDK.
 //   DestroySdkObjects(sdk_mgr);
}
