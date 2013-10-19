
#include "Bone.h"
#include "modelcanvas.h"
#include "modelexport.h"
#include "database.h"

#include <fstream>
#include <cassert>
#include <map>

using namespace std;

namespace ogreexport {

class tabbed_ostream
{
private:
    size_t tabc_;
    std::string tab_;
    std::ostream& stream_;
    bool on_;

    void mtab() 
    {
        tab_ = "";
        for (size_t x = 0; x < tabc_; ++x)
            tab_ += "\t";
    };

public:
    tabbed_ostream(std::ostream& stream) : tabc_(0), tab_(""), stream_(stream), on_(true) 
    { 
        stream_.setf(std::ios::fixed);
        stream_.precision(6);
    }

    void toggle() { on_ = !on_; }
    void tab() { tabc_++; mtab(); }
    void rtab() { tabc_--; mtab(); }

    template<typename T>
    std::ostream& operator << (T c)
    {
        if (on_)
            stream_ << tab_.c_str();
        stream_ << c;
        return stream_;
    }

#ifdef _WINDOWS
	tabbed_ostream& operator<<(tabbed_ostream& (__cdecl *_Pfn)(tabbed_ostream&))
#else
	tabbed_ostream& operator<<(tabbed_ostream& (__attribute__((cdecl)) *_Pfn)(tabbed_ostream&))
#endif
	{   
		(*_Pfn)(*(tabbed_ostream *)this);
		return (*this);
	}
};


tabbed_ostream &rt(tabbed_ostream &s) {
	s.tab();
	return s;
}

tabbed_ostream &lt(tabbed_ostream &s) {
	s.rtab();
	return s;
}

} // ogreexport

using namespace ogreexport;

static bool usesAnimation(Bone &b, size_t animIdx) {
	return (b.trans.uses((unsigned int)animIdx) || b.rot.uses((unsigned int)animIdx) || b.scale.uses((unsigned int)animIdx));
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

struct ExportData {
	typedef uint16	Index;

	Model		*model;
	const char	*fn;
	bool		bInitialPose;
	wxString	&baseName;
	wxString	&name;

	size_t numVertices() const { return model->header.nVertices; }
	size_t numPasses() const { return model->passes.size(); }
	size_t numBones() const { return model->header.nBones; }
	size_t numAnimations() const { return model->header.nAnimations; }

	Index getIndex(size_t n) const { return model->indices[n]; }
	ModelVertex &getVertex(size_t n) const { return model->origVertices[n]; }
	ModelRenderPass &getPass(size_t n) const { return model->passes[n]; }
	ModelGeoset &getGeoset(size_t n) const { return model->geosets[n]; }
	Bone &getBone(size_t n) const { return model->bones[n]; }
	ModelAnimation &getAnimation(size_t n) const { return model->anims[n]; }

	bool hasAnimation(size_t animIdx) const {
		for (size_t n = 0; n < numBones(); n++) {
			Bone &b = getBone(n);
			if (usesAnimation(b, animIdx))
				return true;
		}
		return false;
	}

	Vec3D getBoneParentTrans(int n) const {
		Bone &b = getBone(n);
		Vec3D tr = b.pivot;
		int pid = b.parent;
		if (pid > -1)
			tr -= getBone(pid).pivot;
		return tr;
	}

};

typedef size_t			TimeT;
typedef map<TimeT, int>	Timeline;

static const int KEY_TRANSLATE	= 1;
static const int KEY_ROTATE		= 2;
static const int KEY_SCALE		= 4;

static void WriteMesh(const ExportData &data, wxString filename);
static void WriteMaterial(const ExportData &data, wxString filename);
static void WriteSkeleton(const ExportData &data, wxString filename);

void ExportOgreXML_M2(Model *m, const char *fn, bool init) {
	assert( m && fn );

	LogExportData(wxT("OgreXML"),m->modelname,wxString(fn, wxConvUTF8));

	wxString meshName(fn, wxConvUTF8);
	wxString baseName = (meshName.Right(9).CmpNoCase(wxT(".mesh.xml")) == 0) ? (meshName.Left(meshName.Length() - 9)) : meshName;
	wxString matName = baseName + wxT(".material");
	wxString sktName = baseName + wxT(".skeleton.xml");
	wxString name = baseName.AfterLast(SLASH);

	ExportData data = { m, fn, init, baseName, name };

	WriteMesh(data, meshName);
	WriteMaterial(data, matName);
	WriteSkeleton(data, sktName);
}

void ExportWMOtoOgreXml(WMO *wmo, const char *fn) {
	assert( wmo && fn);
	//LogExportData(wxT("OgreXML"),wxString(fn, wxConvUTF8).BeforeLast(SLASH),wxT("WMO"));
}

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

template<typename T>
void write(ofstream &f, const T &t) {
	f.write((char *)(&t), sizeof(T));
}

static void WriteMesh(const ExportData &data, wxString filename) 
{
	// FIXME: ofstream is not compitable with multibyte path name
#ifndef _MINGW
	ofstream f(filename.fn_str(), ios::trunc);
#else
	ofstream f(filename.char_str(), ios::trunc);
#endif
	if (!f.good())
		return;

	tabbed_ostream s(f);

	s << "<mesh>" << endl;
	
	size_t numVertices = data.numVertices();
	s << rt << "<sharedgeometry vertexcount=\"" << numVertices << "\">" << endl;
	s << rt << "<vertexbuffer positions=\"true\" normals=\"true\" texture_coords=\"1\" texture_coord_dimensions_0=\"2\">" << endl;
	s << rt;
	for (size_t n = 0; n < numVertices; n++) {
		ModelVertex &v = data.getVertex(n);
		s << "<vertex>" << endl;
		s << rt;
		s << "<position x=\"" << v.pos.x << "\" y=\"" << v.pos.y << "\" z=\"" << v.pos.z << "\" />" << endl;
		s << "<normal x=\"" << v.normal.x << "\" y=\"" << v.normal.y << "\" z=\"" << v.normal.z << "\" />" << endl;
		s << "<texcoord u=\"" << v.texcoords.x << "\" v=\"" << v.texcoords.y << "\" />" << endl;
		s << lt;
		s << "</vertex>" << endl;
	}
	s << lt << "</vertexbuffer>" << endl;
	s << lt << "</sharedgeometry>" << endl;
	
	s << "<submeshes>" << endl;
	s << rt;
	size_t numPasses = data.numPasses();
	for (size_t n = 0; n < numPasses; n++) {
		ModelRenderPass &p = data.getPass(n);
		if (p.init(data.model)) {
			ModelGeoset g = data.getGeoset(p.geoset);
			s << "<submesh material=\"" << data.name << "_" << n << "\" usesharedvertices=\"true\">" << endl;
			size_t numFaces = g.icount / 3;
			s << rt << "<faces count=\"" << numFaces << "\">" << endl;
			s << rt;
			for (size_t k = 0; k < numFaces; k++) {
				s << "<face v1=\"" << data.getIndex(g.istart + k * 3) << "\" v2=\"" << data.getIndex(g.istart + k * 3 + 1) << "\" v3=\"" << data.getIndex(g.istart + k * 3 + 2) << "\"/>" << endl;
			}
			s << lt << "</faces>" << endl;
			s << lt << "</submesh>" << endl;
		}
	}
	s << lt << "</submeshes>" << endl;

	wxString sktName = data.baseName.AfterLast(SLASH) + wxT(".skeleton");
	s << "<skeletonlink name=\"" << sktName << "\" />" << endl;

	s << "<boneassignments>" << endl;
	s << rt;
	for (size_t n = 0; n < numVertices; n++) {
		ModelVertex &v = data.getVertex(n);
		for (size_t k = 0; k < 4; k++) {
			if ((v.bones[k] == 0) && (v.weights[k] == 0))
				continue;
			s << "<vertexboneassignment vertexindex=\"" << n << "\" boneindex=\"" << static_cast<int>(v.bones[k]) << "\" weight=\"" << (static_cast<float>(v.weights[k]) / 255.0f) << "\" />" << endl;
		}
	}
	s << lt << "</boneassignments>" << endl;

	s << lt << "</mesh>" << endl;

	f.close();
}

static void WriteMaterial(const ExportData &data, wxString filename) 
{
	// FIXME: ofstream is not compitable with multibyte path name
#ifndef _MINGW
	ofstream f(filename.fn_str(), ios::trunc);
#else
	ofstream f(filename.char_str(), ios::trunc);
#endif
	if (!f.good())
		return;

	tabbed_ostream s(f);

	size_t numPasses = data.numPasses();
	for (size_t n = 0; n < numPasses; n++) {
		ModelRenderPass &p = data.getPass(n);
		if (p.init(data.model)) {
			s << "material " << data.name << "_" << n << endl;
			s << "{" << endl;
			s << rt << "technique" << endl;
			s << "{" << endl;
			s << rt << "pass" << endl;
			s << "{" << endl;
			s << rt;
			s << "ambient 0.7 0.7 0.7" << endl;
			s << "texture_unit" << endl;
			s << "{" << endl;
			s << rt;
			wxString texName = GetM2TextureName(data.model, p, (int)n) + wxT(".tga");
			s << "texture " << texName << " -1" << endl;
#ifndef _MINGW
			// @TODO : fixme, broken on mingw
			SaveTexture(data.baseName.BeforeLast(SLASH) + SLASH + wxString(texName));
#endif
			s << lt << "}" << endl;
			s << lt << "}" << endl;
			s << lt << "}" << endl;
			s << lt << "}" << endl;
		}
	}
	
	f.close();
}

static void WriteSkeleton(const ExportData &data, wxString filename) 
{
	// FIXME: ofstream is not compitable with multibyte path name
#ifndef _MINGW
	ofstream f(filename.fn_str(), ios::trunc);
#else
	ofstream f(filename.char_str(), ios::trunc);
#endif
	if (!f.good())
		return;

	tabbed_ostream s(f);

	s << "<skeleton>" << endl;

	s << rt << "<bones>" << endl;
	s << rt;
	size_t numBones = data.numBones();
	for (size_t n = 0; n < numBones; n++) {
		//Bone &b = data.getBone(n);
		Vec3D v = data.getBoneParentTrans((int)n);
		s << "<bone id=\"" << n << "\" name=\"" << n << "\">" << endl;
		s << rt;
		s << "<position x=\"" << v.x << "\" y=\"" << v.y << "\" z=\"" << v.z << "\" />" << endl;
		s << "<rotation angle=\"0\">" << endl;
		s << rt << "<axis x=\"1\" y=\"0\" z=\"0\" />" << endl;
		s << lt << "</rotation>" << endl;
		s << lt << "</bone>" << endl;
	}
	s << lt << "</bones>" << endl;

	s << "<bonehierarchy>" << endl;
	s << rt;
	for (size_t n = 0; n < numBones; n++) {
		Bone &b = data.getBone(n);
		if (b.parent != -1) {
			s << "<boneparent bone=\"" << n << "\" parent=\"" << static_cast<int>(b.parent) << "\" />" << endl;
		}
	}
	s << lt << "</bonehierarchy>" << endl;

	s << "<animations>" << endl;
	s << rt;
	size_t numAnimations = data.numAnimations();
//	if (numAnimations > 5) // DEBUG!!!
//		numAnimations = 5;
	map<wxString, int> animNames;
	for (size_t animIdx = 0; animIdx < numAnimations; animIdx++) {
		if (data.hasAnimation(animIdx)) {
			ModelAnimation &anim = data.getAnimation(animIdx);
			wxString name;
			try {
				AnimDB::Record r = animdb.getByAnimID(anim.animID);
				name = r.getString(AnimDB::Name);
			}
			catch (DBCFile::NotFound &) {
				name = wxString::Format(wxT("Unknown_%i"), anim.animID);
			}
			map<wxString, int>::iterator it = animNames.find(name);
			if (it == animNames.end()) {
				animNames[name] = 0;
			}
			else {
				it->second++;
				name += wxString::Format(wxT("%i"), it->second);
			}
			// TODO: if GlobalSequence used, animation.length must be GlobalSequence[bone.seq] time, not animation.length time
			s << "<animation name=\"" << name << "\" length=\"" << ((anim.timeEnd - anim.timeStart) / 1000.0f) << "\">" << endl;
			s << rt << "<tracks>" << endl;
			s << rt;
			for (size_t boneIdx = 0; boneIdx < numBones; boneIdx++) {
				Bone &b = data.getBone(boneIdx);
				if (usesAnimation(b, animIdx)) {
					s << "<track bone=\"" << boneIdx << "\">" << endl;
					s << rt;
					Timeline timeline;
					updateTimeline(timeline, b.trans.times[animIdx], KEY_TRANSLATE);
					updateTimeline(timeline, b.rot.times[animIdx], KEY_ROTATE);
					updateTimeline(timeline, b.scale.times[animIdx], KEY_SCALE);
					s << "<keyframes>" << endl;
					s << rt;
					size_t ntrans = 0;
					size_t nrot = 0;
					//size_t nscale = 0;
					for (Timeline::iterator it = timeline.begin(); it != timeline.end(); it++) {
						s << "<keyframe time=\"" << (it->first / 1000.0f) << "\">" << endl;
						s << rt;
						if (it->second & KEY_TRANSLATE) {
							Vec3D v = b.trans.data[animIdx][ntrans];
							s << "<translate x=\"" << v.x << "\" y=\"" << v.y << "\" z=\"" << v.z << "\" />" << endl;
							ntrans++;
						}
						if (it->second & KEY_ROTATE) {
							Quaternion q = b.rot.data[animIdx][nrot];
							Vec4D v;
							QuaternionToAxisAngle(q, v);
							s << "<rotate angle=\"" << -v.w << "\">" << endl;
							s << rt << "<axis x=\"" << v.x << "\" y=\"" << v.y << "\" z=\"" << v.z << "\" />" << endl;
							s << lt << "</rotate>" << endl;
							nrot++;
						}
						if (it->second & KEY_SCALE) {
//							Vec3D &v = b.origScale.data[animIdx][nscale];
//							s << "<scale x=\"" << v.x << "\" y=\"" << v.y << "\" z=\"" << v.z << "\" />" << endl;
//							nscale++;
						}
						s << lt << "</keyframe>" << endl;
					}
					s << lt << "</keyframes>" << endl;
					s << lt << "</track>" << endl;
				}
			}
			s << lt << "</tracks>" << endl;
			s << lt << "</animation>" << endl;
		}
	}
	s << lt << "</animations>" << endl;

	s << lt << "</skeleton>" << endl;

	f.close();
}
