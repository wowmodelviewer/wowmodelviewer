#ifndef MODELEXPORT_LWO_H
#define MODELEXPORT_LWO_H

#include <wx/wfstream.h>

#include "globalvars.h"
#include "modelexport.h"
#include "modelcanvas.h"

#define MAX_POINTS_PER_POLYGON 1023

// -----------------------------------------
// Lightwave 3D Header Data
// -----------------------------------------

int i32;
uint32 u32;
float f32;
uint16 u16;
unsigned char ub;
struct LWSurface;

// Counting system for Scene Items
struct CountSystem{
private:
	size_t Value;
public:
	// Functions
	void Reset(){		Value = 0; }
	size_t GetValue(){	return Value; }
	size_t GetPlus(){	return Value++; }
	size_t PlusGet(){	return ++Value; }

	// Constructors
	CountSystem(){
		Reset();
	}
	CountSystem(size_t StartingNumber){
		Value = StartingNumber;
	}

	// Operators
	size_t operator+ (const size_t &v) const{
		size_t a = Value+v;
		return a;
	}
	size_t operator- (const size_t &v) const{
		size_t a = Value-v;
		return a;
	}
	size_t& operator+= (const size_t &v)
	{
		Value += v;
		return Value;
	}
	size_t& operator-= (const size_t &v)
	{
		Value += v;
		return Value;
	}
};

CountSystem LW_ObjCount;
CountSystem LW_LightCount;
CountSystem LW_BoneCount;
CountSystem LW_CamCount;

// --== Lightwave Type Numbers ==--
// These numbers identify object, light and bone types in the Lightwave Program.
enum LWItemType {
	LW_ITEMTYPE_NOPARENT = -1,
	LW_ITEMTYPE_OBJECT = 1,
	LW_ITEMTYPE_LIGHT,
	LW_ITEMTYPE_CAMERA,
	LW_ITEMTYPE_BONE,
};
enum LWLightType {
	LW_LIGHTTYPE_DISTANT = 0,
	LW_LIGHTTYPE_POINT,
	LW_LIGHTTYPE_SPOTLIGHT,
	LW_LIGHTTYPE_LINEAR,
	LW_LIGHTTYPE_AREA,

	LW_LIGHTTYPE_PLUGIN = 100,
};
enum LWBoneType {
	LW_BONETYPE_BONE = 0,
	LW_BONETYPE_JOINT,
};
enum LWTextureAxisType{
	LW_TEXTUREAXIS_PLANAR = 0,
	LW_TEXTUREAXIS_UV,
};

// Polygon Chunk
struct PolyChunk {
	uint16 numVerts;		// The Number of Indices that make up the poly.
	size_t indice[3];		// The IDs of the 3 Indices that make up each poly. In reality, this should be indice[MAX_POINTS_PER_POLYGON], but WoW will never go above 3.
};

// Polygon Normal
struct PolyNormal {
	size_t indice[3];
	size_t polygon;
	Vec3D direction[3];		// This is the direction the polygon's normal should point towards.	
};

struct AnimVector{
	std::vector<float> Value;
	std::vector<float> Time;
	std::vector<uint16> Spline;

	// Push values into the arrays.
	void Push(float value, float time, uint16 spline = 0){
		Value.push_back(value);
		Time.push_back(time);
		Spline.push_back(spline);
	}

	// Get the number of keyframes for this channel.
	size_t Size(){
		return Time.size();
	}

	// Generate, and push the first key.
	AnimVector() {}
	AnimVector(float value, float time, uint16 spline = 0){
		Push(value,time,spline);
	}
};

struct AnimVec3D{
	AnimVector x, y, z;

	AnimVec3D() {}
	AnimVec3D(AnimVector cx, AnimVector cy, AnimVector cz){
		x = cx;
		y = cy;
		z = cz;
	}
};


// Animation Data
struct AnimationData {
	AnimVec3D Position;
	AnimVec3D Rotation;
	AnimVec3D Scale;

	// Returns the last frame number in this animation.
	size_t Size(){
		float time = 0.0f;

		// Check Position Timing
		for (size_t i=0;i<Position.x.Size();i++){
			if (Position.x.Time[i] > time)
				time = Position.x.Time[i];
			if (Position.y.Time[i] > time)
				time = Position.y.Time[i];
			if (Position.z.Time[i] > time)
				time = Position.z.Time[i];
		}
		// Check Rotation Timing
		for (size_t i=0;i<Rotation.x.Size();i++){
			if (Rotation.x.Time[i] > time)
				time = Rotation.x.Time[i];
			if (Rotation.y.Time[i] > time)
				time = Rotation.y.Time[i];
			if (Rotation.z.Time[i] > time)
				time = Rotation.z.Time[i];
		}
		// Check Scale Timing
		for (size_t i=0;i<Scale.x.Size();i++){
			if (Scale.x.Time[i] > time)
				time = Scale.x.Time[i];
			if (Scale.y.Time[i] > time)
				time = Scale.y.Time[i];
			if (Scale.z.Time[i] > time)
				time = Scale.z.Time[i];
		}

		return time;
	}

	AnimationData() {}
	AnimationData(AnimVec3D position, AnimVec3D rotation, AnimVec3D scale){
		Position = position;
		Rotation = rotation;
		Scale = scale;
	}
};

// --== Scene Formats ==--
struct LWBones{
	size_t BoneID;
	Vec3D RestPos;
	Vec3D RestRot;
	AnimationData AnimData;
	wxString Name;
	bool Active;
	uint16 BoneType;
	size_t ParentID;
	uint16 ParentType;
	float Length;
	wxString WeightMap_Name;
	bool WeightMap_Only;
	bool WeightMap_Normalize;

	LWBones(size_t BoneNum){
		BoneID = BoneNum;
		BoneType = LW_BONETYPE_JOINT;
		Active = true;
		WeightMap_Name = wxEmptyString;
		WeightMap_Only = true;
		WeightMap_Normalize = true;
		Length = 0.25f;
		RestPos = Vec3D(0,0,0);
		RestRot = Vec3D(0,0,0);
	}
};

// Scene Object
struct LWSceneObj{
	size_t ObjectID;
	ssize_t LayerID;
	AnimationData AnimData;
	wxString Name;
	wxString ObjectTags;
	std::vector<LWBones> Bones;
	bool isNull;
	size_t ParentID;
	int16 ParentType;	// -1 or LW_ITEMTYPE_NOPARENT for No Parent

	LWSceneObj(wxString name, size_t id, ssize_t parentID, int16 parentType = LW_ITEMTYPE_NOPARENT, bool IsNull = false, ssize_t layerID = 1){
		Name = name;
		ObjectID = id;
		ParentType = parentType;
		ParentID = parentID;
		isNull = IsNull;
		LayerID = layerID;
		ObjectTags = wxEmptyString;
	}
};

struct LWLight{
	size_t LightID;
	AnimationData AnimData;
	wxString Name;
	Vec3D Color;
	float Intensity;
	uint16 LightType;
	float FalloffRadius;
	size_t ParentID;
	int16 ParentType;	// -1 or LW_ITEMTYPE_NOPARENT for No Parent
	bool UseAttenuation;

	LWLight(){
		Intensity = 1.0f;
		LightType = LW_LIGHTTYPE_POINT;
		ParentType = LW_ITEMTYPE_NOPARENT;
		ParentID = 0;
		FalloffRadius = 2.5f;
	}
};

struct LWCamera{
	size_t CameraID;
	AnimationData AnimData;
	wxString Name;
	float FieldOfView;
	size_t TargetObjectID;
	size_t ParentID;
	int16 ParentType;	// -1 or LW_ITEMTYPE_NOPARENT for No Parent
	LWCamera(){
		CameraID = LW_CamCount.GetPlus();
		ParentType = LW_ITEMTYPE_NOPARENT;
		ParentID = 0;
		Name = wxT("Camera");
	}
};

// Master Scene
struct LWScene{
	std::vector<LWSceneObj> Objects;
	std::vector<LWLight> Lights;
	std::vector<LWCamera> Cameras;

	wxString FileName;
	wxString FilePath;
	float AmbientIntensity;
	float FirstFrame;
	float LastFrame;

	LWScene(wxString file, wxString path, float ambint = 0.5, float FrameFirst = 0.0f, float FrameLast = 1.0f){
		FileName = file;
		FilePath = path + SLASH;
		FirstFrame = FrameFirst;
		LastFrame = FrameLast;

		AmbientIntensity = ambint;
	}
	LWScene(){
		FileName = wxEmptyString;
		FilePath = wxEmptyString;
		FirstFrame = 0.0f;
		LastFrame = 1.0f;
	}

	~LWScene(){
		Objects.~vector();
		Lights.~vector();
		Cameras.~vector();
		FileName = wxT("");
		FilePath = wxT("");
		//free(&AmbientIntensity);
	}

	LWScene& operator= (const LWScene &v) {
        Objects = v.Objects;
		Lights = v.Lights;
		Cameras = v.Cameras;
		FileName = v.FileName;
		FilePath = v.FilePath;
		AmbientIntensity = v.AmbientIntensity;
		FirstFrame = v.FirstFrame;
		LastFrame = v.LastFrame;

		return *this;
	}

};

// --== Object Formats ==--
// Weight Data
struct LWWeightInfo{
	size_t PointID;
	float Value;
};

struct LWWeightMap{
	wxString WeightMapName;
	std::vector<LWWeightInfo> PData;
	size_t BoneID;

	LWWeightMap(wxString MapName=wxT("Weight"), size_t Bone_ID = 0){
		WeightMapName = MapName;
		BoneID = Bone_ID;
	}
};

// Vertex Color Data
struct LWVertexColor{
	uint8 r, g, b, a;

	LWVertexColor(uint8 R=255, uint8 G=255, uint8 B=255, uint8 A=255){
		r = R;
		g = G;
		b = B;
		a = A;
	}
};

// Point Chunk Data
struct LWPoint {
	Vec3D PointData;
	Vec2D UVData;
	LWVertexColor VertexColors;		// In reality, this should be std::vector<LWVertexColor>, but WoW doesn't use more than 1.
};

// Poly Chunk Data
struct LWPoly {
	PolyChunk PolyData;
	PolyNormal Normals;
	wxString NormalMapName;
	size_t PartTagID;
	size_t SurfTagID;
	LWPoly(){
		NormalMapName = wxEmptyString;
	}
};

// -= Lightwave Chunk Structures =-

// Layer Data
struct LWLayer {
	// Layer Data
	wxString Name;						// Name of the Layer, Optional
	int ParentLayer;					// 0-based number of parent layer. -1 or omitted for no parent.

	// Points Block
	std::vector<LWPoint>Points;			// Various Point Blocks used by this layer.
	std::vector<LWWeightMap>Weights;	// Weight Map Data
	bool HasVectorColors;				// Is True if the layer has a Vector Color map
	Vec3D BoundingBox1;					// First Corner of the Layer's Bounding Box
	Vec3D BoundingBox2;					// Second Corner of the Layer's Bounding Box

	// Poly Block
	std::vector<LWPoly> Polys;

	LWLayer(){
		Name = wxT("(unnamed)");
		HasVectorColors = false;
		ParentLayer = -1;
		BoundingBox1 = Vec3D();
		BoundingBox2 = Vec3D();
	};
};

// Clip Data
struct LWClip {
	wxString Filename;		// The Path & Filename of the image to be used in Lightwave
	wxString Source;		// The Source Path & Filename, as used in WoW.
	size_t TagID;			// = Number of Parts + Texture number
};

struct LWSurf_Image {
	ssize_t ID;				// Tag ID for the Image
	uint16 Axis;			// LWTextureAxisType value
	float UVRate_U;			// Rate to move the U with UV Animation
	float UVRate_V;			// Rate to move the V with UV Animation

	// Contructor
	LWSurf_Image(ssize_t idtag=-1, uint16 axis=LW_TEXTUREAXIS_UV, float UVAnimRate_U=0.0f, float UVAnimRate_V=0.0f){
		ID = idtag;
		Axis = axis;
		UVRate_U = UVAnimRate_U;
		UVRate_V = UVAnimRate_V;
	}
};

// Surface Chunk Data
struct LWSurface {
	wxString Name;			// The Surface's Name
	wxString Comment;		// Comment for the surface.
	wxString NormalMapName;	// Name of the Normal Map this surface uses.

	bool isDoubleSided;		// Should it show the same surface on both sides of a poly.
	bool hasVertColors;

	// Base Attributes
	Vec3D Surf_Color;			// Surface Color as floats
	float Surf_Diff;			// Diffusion Amount
	float Surf_Lum;				// Luminosity Amount
	float Surf_Spec;			// Specularity Amount
	float Surf_Reflect;			// Reflection Amount
	float Surf_Trans;			// Transparancy Amount

	// Images
	LWSurf_Image Image_Color;	// Color Image data
	LWSurf_Image Image_Spec;	// Specular Image data
	LWSurf_Image Image_Trans;	// Transparancy Image data

	// Constructors
	LWSurface(wxString name, wxString comment, LWSurf_Image ColorID, LWSurf_Image SpecID, LWSurf_Image TransID, Vec3D SurfColor=Vec3D(1,1,1), float Diffusion=1.0f, float Luminosity = 0.0f, bool doublesided = false, bool hasVC = false){
		Name = name;
		Comment = comment;
		NormalMapName = wxEmptyString;

		Surf_Color = SurfColor;
		Surf_Diff = Diffusion;
		Surf_Lum = Luminosity;
		Surf_Spec = 0.0f;
		Surf_Reflect = 0.0f;
		Surf_Trans = 0.0f;

		Image_Color = ColorID;
		Image_Spec = SpecID;
		Image_Trans = TransID;

		isDoubleSided = doublesided;
		hasVertColors = hasVC;
	}
};

// The Master Structure for each individual LWO file.
struct LWObject {
	wxArrayString PartNames;			// List of names for all the Parts;
	std::vector<LWLayer> Layers;		// List of Layers (usually 1) that make up the Geometery.
	std::vector<LWClip> Images;			// List of all the Unique Images used in the model.
	std::vector<LWSurface> Surfaces;	// List of the Surfaces used in the model.

	wxString SourceType;				// M2, WMO or ADT

	LWObject(){
		SourceType = wxEmptyString;
	}

	void Plus(LWObject o, ssize_t LayerNum=0,wxString PartNamePrefix = wxT("")){
		//wxLogMessage(wxT("Running LW Plus Function, Num Layers: %i, into Layer %i."),o.Layers.size(),LayerNum);
		// Add layers if nessicary...
		while (Layers.size() < (size_t)LayerNum+1){
			LWLayer a;
			Layers.push_back(a);
		}

		size_t OldPartNum = PartNames.size();
		size_t OldSurfNum = Surfaces.size();
		size_t OldTagNum =  OldPartNum + OldSurfNum;
		std::vector<size_t> OldPointNum;

		// Parts
		for (size_t i=0;i<o.PartNames.size();i++){
			wxString a = PartNamePrefix + o.PartNames[i];
			PartNames.push_back(a);
		}
		// Surfaces
		for (size_t i=0;i<o.Surfaces.size();i++){
			LWSurface s = o.Surfaces[i];
			s.Image_Color.ID += OldTagNum;
			Surfaces.push_back(s);
		}
		//Images
		for (size_t i=0;i<o.Images.size();i++){
			LWClip a = o.Images[i];
			a.TagID += OldTagNum;
			Images.push_back(a);
		}

		// Parts Difference
		size_t PartDiff = PartNames.size() - OldPartNum;

		// --== Layers ==--
		// Process Old Layers
		for (size_t i=0;i<Layers.size();i++){
			OldPointNum.push_back((uint32)Layers[i].Points.size());
			for (size_t x=0;x<Layers[i].Polys.size();x++){
				Layers[i].Polys[x].SurfTagID += PartDiff;
			}
		}
		// Proccess New Layers
		for (size_t i=0;i<o.Layers.size();i++){
			LWLayer a = o.Layers[i];

			// Vector Colors
			if (a.HasVectorColors == true)
				Layers[LayerNum].HasVectorColors = true;

			// Bounding Box
			if (a.BoundingBox1.x > Layers[LayerNum].BoundingBox1.x)
				Layers[LayerNum].BoundingBox1.x = a.BoundingBox1.x;
			if (a.BoundingBox1.y > Layers[LayerNum].BoundingBox1.y)
				Layers[LayerNum].BoundingBox1.y = a.BoundingBox1.y;
			if (a.BoundingBox1.z > Layers[LayerNum].BoundingBox1.z)
				Layers[LayerNum].BoundingBox1.z = a.BoundingBox1.z;
			if (a.BoundingBox2.x < Layers[LayerNum].BoundingBox2.x)
				Layers[LayerNum].BoundingBox2.x = a.BoundingBox2.x;
			if (a.BoundingBox2.y < Layers[LayerNum].BoundingBox2.y)
				Layers[LayerNum].BoundingBox2.y = a.BoundingBox2.y;
			if (a.BoundingBox2.z < Layers[LayerNum].BoundingBox2.z)
				Layers[LayerNum].BoundingBox2.z = a.BoundingBox2.z;

			// Points
			for (size_t x=0;x<a.Points.size();x++){
				Layers[LayerNum].Points.push_back(a.Points[x]);
			}
			// Polys
			for (size_t x=0;x<a.Polys.size();x++){
				for (uint16 j=0;j<a.Polys[x].PolyData.numVerts;j++){
					a.Polys[x].PolyData.indice[j] += OldPointNum[LayerNum];
				}
				a.Polys[x].PartTagID += OldPartNum;
				a.Polys[x].SurfTagID += OldTagNum;
				Layers[LayerNum].Polys.push_back(a.Polys[x]);
			}
		}
	}
	LWObject operator= (const LWObject o){
		PartNames = o.PartNames;
		Layers = o.Layers;
		Images = o.Images;
		Surfaces = o.Surfaces;

		SourceType = o.SourceType;

		// return new LWObject
		return *this;
	}
	~LWObject(){
		if (PartNames.size() > 0)
			PartNames.Clear();
		if (Layers.size() > 0)
			Layers.clear();
		if (Images.size() > 0)
			Images.clear();
		if (Surfaces.size() > 0)
			Surfaces.clear();
		if (SourceType.size() > 0)
			SourceType.Clear();
	}
};

// --== Writing Functions ==--
// VX is Lightwave Shorthand for any Point Number, because Lightwave stores points differently if they're over a certain threshold.
void LW_WriteVX(wxFFileOutputStream &f, size_t p, uint32 &Size){
	// Mask the data if we're running 64-bit
	uint32 q;
	if (sizeof(p)>sizeof(uint32)){
		q = (uint32)(p & 0x00000000FFFFFFFF);
	}else{
		q = (uint32)p;
	}
	// Write the Point Data
	if (q <= 0xFF00){
		uint16 indice = MSB2(q & 0x0000FFFF);
		f.Write(reinterpret_cast<char *>(&indice),2);
		Size += 2;
	}else{
		uint32 indice = MSB4<uint32>(q + 0xFF000000);
		f.Write(reinterpret_cast<char *>(&indice), 4);
		Size += 4;
	}
}

// Keyframe Writing Functions
// Writes a single Key for an envelope.
void WriteLWSceneEnvKey(wxTextOutputStream &fs, uint32 Chan, float value, float time, uint16 spline = 0)
{
	fs << wxT("  Key ");			// Announces the start of a Key
	fs << value;					// The Key's Value;
	fs << wxT(" ") << time;			// Time, in seconds, a float. This can be negative, zero or positive. Keys are listed in the envelope in increasing time order.
	fs << wxT(" ") << spline;		// The curve type, an integer: 0 - TCB, 1 - Hermite, 2 - 1D Bezier (obsolete, equivalent to Hermite), 3 - Linear, 4 - Stepped, 5 - 2D Bezier
	fs << wxT(" 0 0 0 0 0 0\n");	// Curve Data 1-6, all 0s for now.
}

// Used for writing the keyframes of an animation.
// Single Keyframe use. Use WriteLWSceneEnvArray for writing animations.
void WriteLWSceneEnvChannel(wxTextOutputStream &fs, uint32 ChanNum, float value, float time, uint16 spline = 0)
{
	fs << wxT("Channel ") << ChanNum << wxT("\n");	// Channel Number
	fs << wxT("{ Envelope\n");
	fs << wxT("  1\n");							// Number of Keys in this envelope.
	WriteLWSceneEnvKey(fs,ChanNum,value,time,spline);
	fs << wxT("  Behaviors 1 1\n");				// Pre/Post Behaviors. Defaults to 1 - Constant.
	fs << wxT("}\n");
}

// Multiple-frame use.
void WriteLWSceneEnvArray(wxTextOutputStream &fs, uint32 ChanNum, AnimVector value)
{
	fs << wxT("Channel ") << ChanNum << wxT("\n");
	fs << wxT("{ Envelope\n");
	fs << wxT("  ") << (int)value.Time.size() << wxT("\n");
	for (size_t n=0;n<value.Time.size();n++){
		float time = (value.Time[n]/FRAMES_PER_SECOND)/FRAMES_PER_SECOND;	// Convert from WoW Frame Number into a Per-Second Float

		WriteLWSceneEnvKey(fs,ChanNum,value.Value[n],time,value.Spline[n]);
	}

	fs << wxT("  Behaviors 1 1\n");
	fs << wxT("}\n");
}

// Writes the "Plugin" information for a scene object, light, camera &/or bones.
void WriteLWScenePlugin(wxTextOutputStream &fs, wxString type, uint32 PluginCount, wxString PluginName, wxString Data = wxEmptyString)
{
	fs << wxT("Plugin ") << type << wxT(" ") << PluginCount << wxT(" ") << PluginName << wxT("\n") << Data << wxT("EndPlugin\n");
}

// Write Keyframe Motion Array
void LW_WriteMotionArray(wxTextOutputStream &fs,AnimationData AnimData, size_t NumChannels){
	fs << wxT("NumChannels ") << (int)NumChannels << wxT("\n");

	// Position
	if (NumChannels > 0)
		WriteLWSceneEnvArray(fs,0,AnimData.Position.x);
	if (NumChannels > 1)
		WriteLWSceneEnvArray(fs,1,AnimData.Position.y);
	if (NumChannels > 2)
		WriteLWSceneEnvArray(fs,2,AnimData.Position.z);
	// Rotation
	if (NumChannels > 3)
		WriteLWSceneEnvArray(fs,3,AnimData.Rotation.x);
	if (NumChannels > 4)
		WriteLWSceneEnvArray(fs,4,AnimData.Rotation.y);
	if (NumChannels > 5)
		WriteLWSceneEnvArray(fs,5,AnimData.Rotation.z);
	// Scale
	if (NumChannels > 6)
		WriteLWSceneEnvArray(fs,6,AnimData.Scale.x);
	if (NumChannels > 7)
		WriteLWSceneEnvArray(fs,7,AnimData.Scale.y);
	if (NumChannels > 8)
		WriteLWSceneEnvArray(fs,8,AnimData.Scale.z);
}

// Gather Functions
LWObject GatherM2forLWO(Attachment *att, Model *m, bool init, wxString fn, LWScene &scene, bool announce = true);
LWObject GatherWMOforLWO(WMO *m, const char *fn, LWScene &scene);
LWObject GatherADTforLWO(MapTile *m, const char *fn, LWScene &scene);

AnimVec3D animValue0 = AnimVec3D(AnimVector(0,0),AnimVector(0,0),AnimVector(0,0));
AnimVec3D animValue1 = AnimVec3D(AnimVector(1,0),AnimVector(1,0),AnimVector(1,0));
AnimVec3D animValue1_Scaled = AnimVec3D(AnimVector(REALWORLD_SCALE,0),AnimVector(REALWORLD_SCALE,0),AnimVector(REALWORLD_SCALE,0));

#endif
