#ifndef ANIMATED_H
#define ANIMATED_H

#include <cassert>
#include <utility>
#include <vector>

#include "modelheaders.h"
#include "mpq.h"
#include "vec3d.h"
#include "quaternion.h"

// interpolation functions
template<class T>
inline T interpolate(const float r, const T &v1, const T &v2)
{
	return static_cast<T>(v1*(1.0f - r) + v2*r);
}

template<class T>
inline T interpolateHermite(const float r, const T &v1, const T &v2, const T &in, const T &out)
{
	// basis functions
	float h1 = 2.0f*r*r*r - 3.0f*r*r + 1.0f;
	float h2 = -2.0f*r*r*r + 3.0f*r*r;
	float h3 = r*r*r - 2.0f*r*r + r;
	float h4 = r*r*r - r*r;

	// interpolation
	return static_cast<T>(v1*h1 + v2*h2 + in*h3 + out*h4);
}


template<class T>
inline T interpolateBezier(const float r, const T &v1, const T &v2, const T &in, const T &out)
{
	float InverseFactor = (1.0f - r);
	float FactorTimesTwo = r*r;
	float InverseFactorTimesTwo = InverseFactor*InverseFactor;
	// basis functions
	float h1 = InverseFactorTimesTwo * InverseFactor;
	float h2 = 3.0f * r * InverseFactorTimesTwo;
	float h3 = 3.0f * FactorTimesTwo * InverseFactor;
	float h4 = FactorTimesTwo * r;

	// interpolation
	return static_cast<T>(v1*h1 + v2*h2 + in*h3 + out*h4);
}

// "linear" interpolation for quaternions should be slerp by default
template<>
inline Quaternion interpolate<Quaternion>(const float r, const Quaternion &v1, const Quaternion &v2)
{
	return Quaternion::slerp(r, v1, v2);
}


typedef std::pair<size_t, size_t> AnimRange;

// global time for global sequences
extern size_t globalTime;
extern size_t globalFrame;

enum Interpolations {
	INTERPOLATION_NONE,
	INTERPOLATION_LINEAR,
	INTERPOLATION_HERMITE,
	INTERPOLATION_BEZIER
};

template <class T>
class Identity {
public:
	static const T& conv(const T& t)
	{
		return t;
	}
};

// In WoW 2.0+ Blizzard are now storing rotation data in 16bit values instead of 32bit.
// I don't really understand why as its only a very minor saving in model sizes and adds extra overhead in
// processing the models.  Need this structure to read the data into.
struct PACK_QUATERNION {  
	int16 x,y,z,w;  
}; 

class Quat16ToQuat32 {
public:
	static const Quaternion conv(const PACK_QUATERNION t)
	{
		return Quaternion(
			float(t.x < 0? t.x + 32768 : t.x - 32767)/ 32767.0f, 
			float(t.y < 0? t.y + 32768 : t.y - 32767)/ 32767.0f,
			float(t.z < 0? t.z + 32768 : t.z - 32767)/ 32767.0f,
			float(t.w < 0? t.w + 32768 : t.w - 32767)/ 32767.0f);
	}
};

// Convert opacity values stored as shorts to floating point
// I wonder why Blizzard decided to save 2 bytes by doing this
class ShortToFloat {
public:
	static float conv(const short t)
	{
		return t/32767.0f;
	}
};

/*
	Generic animated value class:

	T is the data type to animate
	D is the data type stored in the file (by default this is the same as T)
	Conv is a conversion object that defines T conv(D) to convert from D to T
		(by default this is an identity function)
	(there might be a nicer way to do this? meh meh)
*/

#define	MAX_ANIMATED	500
template <class T, class D=T, class Conv=Identity<T> >
class Animated {
public:

	ssize_t type, seq;
	uint32 *globals;
#ifndef WotLK
	bool used;
	std::vector<AnimRange> ranges;
	std::vector<size_t> times;
	std::vector<T> data;
	// for nonlinear interpolations:
	std::vector<T> in, out;
#else
	std::vector<size_t> times[MAX_ANIMATED];
	std::vector<T> data[MAX_ANIMATED];
	// for nonlinear interpolations:
	std::vector<T> in[MAX_ANIMATED], out[MAX_ANIMATED];
	size_t sizes; // for fix function
#endif
	bool uses(ssize_t anim)
	{
		if ((seq) && (seq>-1))
			anim = 0;
		return ((data[anim].size()) > 0);
	}

	T getValue(ssize_t anim, size_t time)
	{
#ifdef WotLK
		// obtain a time value and a data range
		if (seq>-1) {
			// TODO
			if ((!globals)||(!globals[seq]))
				return T();
			if (globals[seq]==0) 
				time = 0;
			else 
				time = globalTime % globals[seq];
			anim = 0;
		}
		if (data[anim].size()>1 && times[anim].size()>1) {
			size_t t1, t2;
			size_t pos=0;
			float r;
			size_t max_time = times[anim][times[anim].size()-1];
			//if (max_time > 0)
			//	time %= max_time; // I think this might not be necessary?
			if (time > max_time) {
				pos=times[anim].size()-1;
				r = 1.0f;

				if (type == INTERPOLATION_NONE) 
					return data[anim][pos];
				else if (type == INTERPOLATION_LINEAR) 
					return interpolate<T>(r,data[anim][pos],data[anim][pos]);
				else if (type==INTERPOLATION_HERMITE){
					// INTERPOLATION_HERMITE is only used in cameras afaik?
					return interpolateHermite<T>(r,data[anim][pos],data[anim][pos],in[anim][pos],out[anim][pos]);
				}
				else if (type==INTERPOLATION_BEZIER){
					//Is this used ingame or only by custom models?
					return interpolateBezier<T>(r,data[anim][pos],data[anim][pos],in[anim][pos],out[anim][pos]);
				}
				else //this shouldn't appear!
					return data[anim][pos];
			} else {
				for (size_t i=0; i<times[anim].size()-1; i++) {
					if (time >= times[anim][i] && time < times[anim][i+1]) {
						pos = i;
						break;
					}
				}
				t1 = times[anim][pos];
				t2 = times[anim][pos+1];
				r = (time-t1)/(float)(t2-t1);

				if (type == INTERPOLATION_NONE) 
					return data[anim][pos];
				else if (type == INTERPOLATION_LINEAR) 
					return interpolate<T>(r,data[anim][pos],data[anim][pos+1]);
				else if (type==INTERPOLATION_HERMITE){
					// INTERPOLATION_HERMITE is only used in cameras afaik?
					return interpolateHermite<T>(r,data[anim][pos],data[anim][pos+1],in[anim][pos],out[anim][pos]);
				}
				else if (type==INTERPOLATION_BEZIER){
					//Is this used ingame or only by custom models?
					return interpolateBezier<T>(r,data[anim][pos],data[anim][pos+1],in[anim][pos],out[anim][pos]);
				}
				else //this shouldn't appear!
					return data[anim][pos];
			}
		} else {
			// default value
			if (data[anim].size() == 0)
				return T();
			else
				return data[anim][0];
		}
#else
		if (type != INTERPOLATION_NONE || data.size()>1) {
			AnimRange range;

			// obtain a time value and a data range
			if (seq>-1) {
				if (globals[seq]==0) 
					time = 0;
				else 
					time = globalTime % globals[seq];
				range.first = 0;
				range.second = data.size()-1;
			} else {
				range = ranges[anim];
				time %= times[times.size()-1]; // I think this might not be necessary?
			}

 			if (range.first != range.second) {
				size_t t1, t2;
				size_t pos=0;
				for (size_t i=range.first; i<range.second; i++) {
					if (time >= times[i] && time < times[i+1]) {
						pos = i;
						break;
					}
				}
				t1 = times[pos];
				t2 = times[pos+1];
				float r = (time-t1)/(float)(t2-t1);

				if (type == INTERPOLATION_LINEAR) 
					return interpolate<T>(r,data[pos],data[pos+1]);
				else if (type == INTERPOLATION_NONE) 
					return data[pos];
				else {
					// INTERPOLATION_HERMITE is only used in cameras afaik?
					return interpolateHermite<T>(r,data[pos],data[pos+1],in[pos],out[pos]);
				}
			} else {
				return data[range.first];
			}
		} else {
			// default value
			if (data.size() == 0)
				return 0;
			else
				return data[0];
		}

#endif

	}

	void init(AnimationBlock &b, MPQFile &f, uint32 *gs)
	{
		globals = gs;
		type = b.type;
		seq = b.seq;
		if (seq!=-1) {
			if (!gs)
				return;
			//assert(gs);
		}

#ifndef	WotLK
		// Old method
		//used = (type != INTERPOLATION_NONE) || (seq != -1);
		// New method suggested by Cryect
		used = (b.nKeys > 0);
#endif

		// ranges
		#ifndef WotLK
		if (b.nRanges > 0) {
			uint32 *pranges = (uint32*)(f.getBuffer() + b.ofsRanges);
			for (size_t i=0, k=0; i<b.nRanges; i++) {
				AnimRange r;
				r.first = pranges[k++];
				r.second = pranges[k++];
				ranges.push_back(r);
			}
		} else if (type!=0 && seq==-1) {
			AnimRange r;
			r.first = 0;
			r.second = b.nKeys - 1;
			ranges.push_back(r);
		}
		#endif

		// times
		if (b.nTimes != b.nKeys)
			return;
		//assert(b.nTimes == b.nKeys);
#ifdef WotLK // by Flow
		sizes = b.nTimes;
		if( b.nTimes == 0 )
			return;

		for(size_t j=0; j < b.nTimes; j++) {
			AnimationBlockHeader* pHeadTimes = (AnimationBlockHeader*)(f.getBuffer() + b.ofsTimes + j*sizeof(AnimationBlockHeader));
		
			unsigned int *ptimes = (unsigned int*)(f.getBuffer() + pHeadTimes->ofsEntrys);
			for (size_t i=0; i < pHeadTimes->nEntrys; i++)
				times[j].push_back(ptimes[i]);
		}

		// keyframes
		for(size_t j=0; j < b.nKeys; j++) {
			AnimationBlockHeader* pHeadKeys = (AnimationBlockHeader*)(f.getBuffer() + b.ofsKeys + j*sizeof(AnimationBlockHeader));

			D *keys = (D*)(f.getBuffer() + pHeadKeys->ofsEntrys);
			switch (type) {
				case INTERPOLATION_NONE:
				case INTERPOLATION_LINEAR:
					for (size_t i = 0; i < pHeadKeys->nEntrys; i++) 
						data[j].push_back(Conv::conv(keys[i]));
					break;
				case INTERPOLATION_HERMITE:
					for (size_t i = 0; i < pHeadKeys->nEntrys; i++) {
						data[j].push_back(Conv::conv(keys[i*3]));
						in[j].push_back(Conv::conv(keys[i*3+1]));
						out[j].push_back(Conv::conv(keys[i*3+2]));
					}
					break;
				//let's use same values like hermite?!?
				case INTERPOLATION_BEZIER:
					for (size_t i = 0; i < pHeadKeys->nEntrys; i++) {
						data[j].push_back(Conv::conv(keys[i*3]));
						in[j].push_back(Conv::conv(keys[i*3+1]));
						out[j].push_back(Conv::conv(keys[i*3+2]));
					}
					break;
			}
		}
#else
		uint32 *ptimes = (uint32*)(f.getBuffer() + b.ofsTimes);
		for (size_t i=0; i<b.nTimes; i++) 
			times.push_back(ptimes[i]);

		// keyframes
		assert((D*)(f.getBuffer() + b.ofsKeys));
		D *keys = (D*)(f.getBuffer() + b.ofsKeys);
		switch (type) {
			case INTERPOLATION_NONE:
			case INTERPOLATION_LINEAR:
				for (size_t i=0; i<b.nKeys; i++) 
					data.push_back(Conv::conv(keys[i]));
				break;
			case INTERPOLATION_HERMITE:
				for (size_t i=0; i<b.nKeys; i++) {
					data.push_back(Conv::conv(keys[i*3]));
					in.push_back(Conv::conv(keys[i*3+1]));
					out.push_back(Conv::conv(keys[i*3+2]));
				}
				break;
		}

		if (data.size()==0) 
			data.push_back(T());
#endif
	}

#ifdef WotLK
	void init(AnimationBlock &b, MPQFile &f, uint32 *gs, MPQFile *animfiles)
	{
		globals = gs;
		type = b.type;
		seq = b.seq;
		if (seq!=-1) {
			if (!gs)
				return;
			//assert(gs);
		}

		// times
		if (b.nTimes != b.nKeys)
			return;
		//assert(b.nTimes == b.nKeys);
		sizes = b.nTimes;
		if( b.nTimes == 0 )
			return;

		for(size_t j=0; j < b.nTimes; j++) {
			AnimationBlockHeader* pHeadTimes = (AnimationBlockHeader*)(f.getBuffer() + b.ofsTimes + j*sizeof(AnimationBlockHeader));
			uint32 *ptimes;
			if (animfiles[j].getSize() > pHeadTimes->ofsEntrys)
				ptimes = (uint32*)(animfiles[j].getBuffer() + pHeadTimes->ofsEntrys);
			else if (f.getSize() > pHeadTimes->ofsEntrys)
				ptimes = (uint32*)(f.getBuffer() + pHeadTimes->ofsEntrys);
			else
				continue;
			for (size_t i=0; i < pHeadTimes->nEntrys; i++)
				times[j].push_back(ptimes[i]);
		}

		// keyframes
		for(size_t j=0; j < b.nKeys; j++) {
			AnimationBlockHeader* pHeadKeys = (AnimationBlockHeader*)(f.getBuffer() + b.ofsKeys + j*sizeof(AnimationBlockHeader));
			assert((D*)(f.getBuffer() + pHeadKeys->ofsEntrys));
			D *keys;
			if (animfiles[j].getSize() > pHeadKeys->ofsEntrys)
				keys = (D*)(animfiles[j].getBuffer() + pHeadKeys->ofsEntrys);
			else if (f.getSize() > pHeadKeys->ofsEntrys)
				keys = (D*)(f.getBuffer() + pHeadKeys->ofsEntrys);
			else
				continue;
			switch (type) {
				case INTERPOLATION_NONE:
				case INTERPOLATION_LINEAR:
					for (size_t i = 0; i < pHeadKeys->nEntrys; i++) 
						data[j].push_back(Conv::conv(keys[i]));
					break;
				case INTERPOLATION_HERMITE:
					for (size_t i = 0; i < pHeadKeys->nEntrys; i++) {
						data[j].push_back(Conv::conv(keys[i*3]));
						in[j].push_back(Conv::conv(keys[i*3+1]));
						out[j].push_back(Conv::conv(keys[i*3+2]));
					}
					break;
				case INTERPOLATION_BEZIER:
					for (size_t i = 0; i < pHeadKeys->nEntrys; i++) {
						data[j].push_back(Conv::conv(keys[i*3]));
						in[j].push_back(Conv::conv(keys[i*3+1]));
						out[j].push_back(Conv::conv(keys[i*3+2]));
					}
					break;
			}
		}
	}
#endif

	void fix(T fixfunc(const T))
	{
		switch (type) {
			case INTERPOLATION_NONE:
			case INTERPOLATION_LINEAR:
#ifdef WotLK
				for (size_t i=0; i<sizes; i++) {
					for (size_t j=0; j<data[i].size(); j++) {
						data[i][j] = fixfunc(data[i][j]);
					}
				}
#else
				for (size_t i=0; i<data.size(); i++) {
					data[i] = fixfunc(data[i]);
				}
#endif
				break;
			case INTERPOLATION_HERMITE:
#ifdef WotLK
				for (size_t i=0; i<sizes; i++) {
					for (size_t j=0; j<data[i].size(); j++) {
						data[i][j] = fixfunc(data[i][j]);
						in[i][j] = fixfunc(in[i][j]);
						out[i][j] = fixfunc(out[i][j]);
					}
				}
#else
				for (size_t i=0; i<data.size(); i++) {
					data[i] = fixfunc(data[i]);
					in[i] = fixfunc(in[i]);
					out[i] = fixfunc(out[i]);
				}
#endif
				break;
			case INTERPOLATION_BEZIER:
#ifdef WotLK
				for (size_t i=0; i<sizes; i++) {
					for (size_t j=0; j<data[i].size(); j++) {
						data[i][j] = fixfunc(data[i][j]);
						in[i][j] = fixfunc(in[i][j]);
						out[i][j] = fixfunc(out[i][j]);
					}
				}
#endif
				break;
		}
	}
	friend std::ostream& operator<<(std::ostream& out, Animated& v)
	{
		if (v.sizes == 0)
			return out;
		out << "      <type>"<< v.type << "</type>" << endl;
		out << "      <seq>"<< v.seq << "</seq>" << endl;
		out << "      <anims>"<< endl;
		for(size_t j=0; j<v.sizes; j++) {
			if (j != 0) continue; // only output walk animation
			if (v.uses((unsigned int)j)) {
				out << "    <anim id=\"" << j << "\" size=\""<< v.data[j].size() <<"\">" << endl;
				for(size_t k=0; k<v.data[j].size(); k++) {
					out << "      <data time=\"" << v.times[j][k]  << "\">" << v.data[j][k] << "</data>" << endl;
				}
				out << "    </anim>" << endl;
			}
			if (v.seq > -1 && j > 0)
				break;
		}
		out << "      </anims>"<< endl;
		return out;
	}
};

typedef Animated<float,short,ShortToFloat> AnimatedShort;

#endif
