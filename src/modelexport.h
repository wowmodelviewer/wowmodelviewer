#ifndef MODELEXPORT_H
#define MODELEXPORT_H

#include "model.h"
#include "modelcanvas.h"

// Various definitions
#define RADIAN 57.295779513082320876798154814114
#define FRAMES_PER_SECOND 30

// This number is used to scale items to their "real world" height. 0.9 seems to be perfect for player characters.
#define REALWORLD_SCALE 0.9f

// Exporter Error Codes
enum ExportErrorCodes {
	EXPORT_OKAY = 0,
	EXPORT_ERROR_NO_DATA,
	EXPORT_ERROR_NO_OVERWRITE,
	EXPORT_ERROR_FILE_ACCESS,
	EXPORT_ERROR_SETTINGS_WRONG,
	EXPORT_ERROR_BAD_FILENAME,
	EXPORT_ERROR_UNKNOWN,
};

// Structures
struct Vertex3f {
	float x, y, z;
};

struct ModelData {
	Vertex3f vertex;
	float tu;
	float tv;
	Vertex3f normal;
	unsigned short groupIndex;
	char boneid;
	char boneidEx[3];
	char weight[4];
};

struct GroupData {
	ModelRenderPass p;
	Model *m;
};

// Mesh & Slot names
//static size_t meshnum = 19;
//static size_t slotnum = 15;
static wxString meshes[19] = {wxT("Hairstyles"), wxT("Facial1"), wxT("Facial2"), wxT("Facial3"), wxT("Braces"), wxT("Boots"), wxEmptyString, wxT("Ears"), wxT("Wristbands"),  wxT("Kneepads"), wxT("Pants"), wxT("Pants"), wxT("Tarbard"), wxT("Trousers"), wxEmptyString, wxT("Cape"), wxEmptyString, wxT("Eyeglows"), wxT("Belt") };
static wxString slots[15] = {wxT("Helm"), wxEmptyString, wxT("Shoulder"), wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, wxT("Right Hand Item"), wxT("Left Hand Item"), wxEmptyString, wxEmptyString, wxT("Quiver") };

static wxString Attach_Names[] = { // enum POSITION_SLOTS
	wxT("Left Wrist (Shield) / Mount"), // 0
	wxT("Right Palm"), 
	wxT("Left Palm"), 
	wxT("Right Elbow"), 
	wxT("Left Elbow"), 
	wxT("Right Shoulder"), // 5
	wxT("Left Shoulder"), 
	wxT("Right Knee"), 
	wxT("Left Knee"), 
	wxT("Right Hip"),
	wxT("Left Hip"), // 10
	wxT("Helmet"), 
	wxT("Back"), 
	wxT("Right Shoulder Horizontal"), 
	wxT("Left Shoulder Horizontal"), 
	wxT("Front Hit Region"), // 15
	wxT("Rear Hit Region"), 
	wxT("Mouth"), 
	wxT("Head Region"), 
	wxT("Base"),
	wxT("Above"), // 20
	wxT("Pre-Cast 1 L"), 
	wxT("Pre-Cast 1 R"), 
	wxT("Pre-Cast 2 L"), 
	wxT("Pre-Cast 2 R"),
	wxT("Pre-Cast 3"), //25 
	wxT("Upper Back Sheath R"), 
	wxT("Upper Back Sheath L"), 
	wxT("Middle Back Sheath"), 
	wxT("Belly"), 
	wxT("Reverse Back, Up Back L"), //30
	wxT("Right Back"), 
	wxT("Left Hip Sheath"), 
	wxT("Right Hip Sheath"), 
	wxT("Spell Impact"),
	wxT("Right Palm (Unk1)"), //35
	wxT("Right Palm (Unk2)"),
	wxT("Demolishervehicle"),
	wxT("Demolishervehicle2"),
	wxT("Vehicle Seat1"),
	wxT("Vehicle Seat2"), // 40
	wxT("Vehicle Seat3"),
	wxT("Vehicle Seat4"),
};

static wxString Bone_Names[] = { // enum KeyBoneTable
	wxT("ArmL"), // 0
	wxT("ArmR"),
	wxT("ShoulderL"),
	wxT("ShoulderR"),
	wxT("SpineLow"),
	wxT("Waist"), // 5
	wxT("Head"),
	wxT("Jaw"),
	wxT("RFingerIndex"),
	wxT("RFingerMiddle"),
	wxT("RFingerPinky"), // 10
	wxT("RFingerRing"),
	wxT("RThumb"),
	wxT("LFingerIndex"),
	wxT("LFingerMiddle"),
	wxT("LFingerPinky"), // 15
	wxT("LFingerRing"),
	wxT("LThumb"),
	wxT("BTH"),
	wxT("CSR"),
	wxT("CSL"), // 20
	wxT("Breath"),
	wxT("Name"),
	wxT("NameMount"),
	wxT("CHD"),
	wxT("CCH"), // 25
	wxT("Root"),
	wxT("Wheel1"),
	wxT("Wheel2"),
	wxT("Wheel3"),
	wxT("Wheel4"), // 30
	wxT("Wheel5"),
	wxT("Wheel6"),
	wxT("Wheel7"),
	wxT("Wheel8"),
	wxT("")
};


// Common functions
void LogExportData(wxString ExporterExtention, wxString ModelName, wxString Destination);
void SaveTexture(wxString fn);
void SaveTexture2(wxString file, wxString outdir, wxString ExportID, wxString suffix);
Vec3D QuaternionToXYZ(Vec3D Dir, float W);
void InitCommon(Attachment *att, bool init, ModelData *&verts, GroupData *&groups, unsigned short &numVerts, unsigned short &numGroups, unsigned short &numFaces);
wxString GetM2TextureName(Model *m, ModelRenderPass p, size_t PassNumber);
void MakeModelFaceForwards(Vec3D &vect, bool flipZ);

void QuaternionToRotationMatrix(const Quaternion& quat, Matrix& rkRot);
void RotationMatrixToEulerAnglesXYZ(const Matrix& rkRot, float& rfXAngle, float& rfYAngle, float& rfZAngle);

// --== Exporter Functions ==--
// List your exporter functions here. There is no need to include functions for formats you're not exporting from.
// IE: Don't include a WMO exporter function if you've only written a M2 exporter.
// Make sure to properly enable/disable your exporter's functions in exporters.h!

// Raw Model File
void SaveBaseFile();

// Lightwave
size_t ExportLWO_M2(Attachment *att, Model *m, const char *fn, bool init);
size_t ExportLWO_WMO(WMO *m, const char *fn);
size_t ExportLWO_ADT(MapTile *m, const char *fn);

// Wavefront Object
void ExportOBJ_M2(Attachment *att, Model *m, wxString fn, bool init);
void ExportOBJ_WMO(WMO *m, wxString fn);

// Milkshape
void ExportMS3D_M2(Attachment *att, Model *m, const char *fn, bool init);

// Collada
void ExportCOLLADA_M2(Attachment *att, Model *m, const char *fn, bool init);
void ExportCOLLADA_WMO(WMO *m, const char *fn);

// X3D
void ExportX3D_M2(Model *m, const char *fn, bool init);

//XHTML
void ExportXHTML_M2(Model *m, const char *fn, bool init);

// Ogre XML
void ExportOgreXML_M2(Model *m, const char *fn, bool init);

// FBX
void ExportFBX_M2(Model* m, const char* fn, bool init);
void ExportFBX_WMO(WMO* m, const char* fn);

// M3
void ExportM3_M2(Attachment *att, Model* m, const char* fn, bool init);

#endif
