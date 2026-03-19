#pragma once

#include <map>
#include <ostream>
#include <utility>
#include <vector>

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include "GameFile.h"
#include "modelheaders.h"
#include "types.h"
#include "Logger.h"

#define _ANIMATED_API_

class modelAnimData
{
public:
	std::map<uint, int16> animIndexToAnimId;
	std::map<int16, std::pair<GameFile*, GameFile*>> animfiles;
	std::vector<uint32> globalSequences;
};

// interpolation functions
template <class T>
inline T interpolate(const float r, const T& v1, const T& v2)
{
	return static_cast<T>(v1 * (1.0f - r) + v2 * r);
}

template <class T>
inline T interpolateHermite(const float r, const T& v1, const T& v2, const T& in, const T& outVal)
{
	// basis functions
	float h1 = 2.0f * r * r * r - 3.0f * r * r + 1.0f;
	float h2 = -2.0f * r * r * r + 3.0f * r * r;
	float h3 = r * r * r - 2.0f * r * r + r;
	float h4 = r * r * r - r * r;

	// interpolation
	return static_cast<T>(v1 * h1 + v2 * h2 + in * h3 + outVal * h4);
}

template <class T>
inline T interpolateBezier(const float r, const T& v1, const T& v2, const T& in, const T& outVal)
{
	const float InverseFactor = (1.0f - r);
	const float FactorTimesTwo = r * r;
	const float InverseFactorTimesTwo = InverseFactor * InverseFactor;
	// basis functions
	float h1 = InverseFactorTimesTwo * InverseFactor;
	float h2 = 3.0f * r * InverseFactorTimesTwo;
	float h3 = 3.0f * FactorTimesTwo * InverseFactor;
	float h4 = FactorTimesTwo * r;

	// interpolation
	return static_cast<T>(v1 * h1 + v2 * h2 + in * h3 + outVal * h4);
}

// "linear" interpolation for quaternions should be slerp by default
template <>
inline glm::fquat interpolate<glm::fquat>(const float r, const glm::fquat& v1, const glm::fquat& v2)
{
	return glm::slerp(v1, v2, r);
}

typedef std::pair<size_t, size_t> AnimRange;

// global time for global sequences
_ANIMATED_API_ extern size_t globalTime;

enum Interpolations
{
	INTERPOLATION_NONE,
	INTERPOLATION_LINEAR,
	INTERPOLATION_HERMITE,
	INTERPOLATION_BEZIER
};

template <class T>
class Identity
{
public:
	static const T& conv(const T& t)
	{
		return t;
	}
};

// In WoW 2.0+ Blizzard are now storing rotation data in 16bit values instead of 32bit.
// I don't really understand why as its only a very minor saving in model sizes and adds extra overhead in
// processing the models.  Need this structure to read the data into.
struct PACK_QUATERNION
{
	int16 x, y, z, w;
};

class Quat16ToQuat32
{
public:
	static const glm::fquat conv(const PACK_QUATERNION t)
	{
		return glm::fquat(
			static_cast<float>(t.w < 0 ? t.w + 32768 : t.w - 32767) / 32767.0f,
			static_cast<float>(t.x < 0 ? t.x + 32768 : t.x - 32767) / 32767.0f,
			static_cast<float>(t.y < 0 ? t.y + 32768 : t.y - 32767) / 32767.0f,
			static_cast<float>(t.z < 0 ? t.z + 32768 : t.z - 32767) / 32767.0f);
	}
};

// Convert opacity values stored as shorts to floating point
// I wonder why Blizzard decided to save 2 bytes by doing this
class ShortToFloat
{
public:
	static float conv(const short t)
	{
		return t / 32767.0f;
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

enum
{
	MAX_ANIMATED = 500
};

template <class T, class D=T, class Conv=Identity<T>>
class Animated
{
public:
	ssize_t type, seq;
	std::vector<uint32> globals;
	std::vector<size_t> times[MAX_ANIMATED];
	std::vector<T> data[MAX_ANIMATED];
	// for nonlinear interpolations:
	std::vector<T> in[MAX_ANIMATED], out[MAX_ANIMATED];
	size_t sizes; // for fix function

	bool uses(ssize_t anim) const
	{
		if (seq > -1)
			anim = 0;
		return ((data[anim].size()) > 0);
	}

	T getValue(ssize_t anim, size_t time)
	{
		// obtain a time value and a data range
		if (seq >= 0 && seq < static_cast<int>(globals.size()))
		{
			// TODO
			if (!globals[seq])
				return T();
			// if (globals[seq] == 0)
			//	time = 0;
			// else
				time = globalTime % globals[seq];
			anim = 0;
		}
		if (data[anim].size() > 1 && times[anim].size() > 1)
		{
			size_t pos = 0;
			float r;
			const size_t max_time = times[anim][times[anim].size() - 1];
			//if (max_time > 0)
			//  time %= max_time; // I think this might not be necessary?
			if (time > max_time)
			{
				pos = times[anim].size() - 1;
				r = 1.0f;

				if (type == INTERPOLATION_NONE)
					return data[anim][pos];
				else if (type == INTERPOLATION_LINEAR)
					return interpolate<T>(r, data[anim][pos], data[anim][pos]);
				else if (type == INTERPOLATION_HERMITE)
				{
					// INTERPOLATION_HERMITE is only used in cameras afaik?
					return interpolateHermite<T>(r, data[anim][pos], data[anim][pos], in[anim][pos], out[anim][pos]);
				}
				else if (type == INTERPOLATION_BEZIER)
				{
					//Is this used ingame or only by custom models?
					return interpolateBezier<T>(r, data[anim][pos], data[anim][pos], in[anim][pos], out[anim][pos]);
				}
				else //this shouldn't appear!
					return data[anim][pos];
			}
			else
			{
				for (size_t i = 0; i < times[anim].size() - 1; i++)
				{
					if (time >= times[anim][i] && time < times[anim][i + 1])
					{
						pos = i;
						break;
					}
				}
				const size_t t1 = times[anim][pos];
				const size_t t2 = times[anim][pos + 1];
				r = (time - t1) / static_cast<float>(t2 - t1);

				if (type == INTERPOLATION_NONE)
					return data[anim][pos];
				else if (type == INTERPOLATION_LINEAR)
					return interpolate<T>(r, data[anim][pos], data[anim][pos + 1]);
				else if (type == INTERPOLATION_HERMITE)
				{
					// INTERPOLATION_HERMITE is only used in cameras afaik?
					return interpolateHermite<T>(r, data[anim][pos], data[anim][pos + 1], in[anim][pos],
					                             out[anim][pos]);
				}
				else if (type == INTERPOLATION_BEZIER)
				{
					//Is this used ingame or only by custom models?
					return interpolateBezier<T>(r, data[anim][pos], data[anim][pos + 1], in[anim][pos], out[anim][pos]);
				}
				else //this shouldn't appear!
					return data[anim][pos];
			}
		}
		else
		{
			// default value
			if (data[anim].size() == 0)
				return T();
			else
				return data[anim][0];
		}
	}

	void init(AnimationBlock& b, GameFile* f, std::vector<uint32>& gs)
	{
		globals = gs;
		type = b.type;
		seq = b.seq;

		// times
		if (b.nTimes != b.nKeys)
			return;
		//assert(b.nTimes == b.nKeys);
		sizes = b.nTimes;
		if (b.nTimes == 0)
			return;

		for (size_t j = 0; j < b.nTimes; j++)
		{
			const AnimationBlockHeader* pHeadTimes = reinterpret_cast<AnimationBlockHeader*>(f->getBuffer() + b.ofsTimes + j * sizeof(
				AnimationBlockHeader));

			const unsigned int* ptimes = reinterpret_cast<unsigned int*>(f->getBuffer() + pHeadTimes->ofsEntrys);
			for (size_t i = 0; i < pHeadTimes->nEntrys; i++)
				times[j].push_back(ptimes[i]);
		}

		// keyframes
		for (size_t j = 0; j < b.nKeys; j++)
		{
			const AnimationBlockHeader* pHeadKeys = reinterpret_cast<AnimationBlockHeader*>(f->getBuffer() + b.ofsKeys + j * sizeof(
				AnimationBlockHeader));

			D* keys = reinterpret_cast<D*>(f->getBuffer() + pHeadKeys->ofsEntrys);
			switch (type)
			{
			case INTERPOLATION_NONE:
			case INTERPOLATION_LINEAR:
				for (size_t i = 0; i < pHeadKeys->nEntrys; i++)
					data[j].push_back(Conv::conv(keys[i]));
				break;
			case INTERPOLATION_HERMITE:
			case INTERPOLATION_BEZIER: //let's use same values like hermite?!?
				for (size_t i = 0; i < pHeadKeys->nEntrys; i++)
				{
					data[j].push_back(Conv::conv(keys[i * 3]));
					in[j].push_back(Conv::conv(keys[i * 3 + 1]));
					out[j].push_back(Conv::conv(keys[i * 3 + 2]));
				}
				break;
			default: ;
			}
		}
	}

	void init(AnimationBlock& b, GameFile& f, const modelAnimData& modelData)
	{
		globals = modelData.globalSequences;
		type = b.type;
		seq = b.seq;

		// times
		if (b.nTimes != b.nKeys)
			return;
		//assert(b.nTimes == b.nKeys);
		sizes = b.nTimes;
		if (b.nTimes == 0)
			return;

		for (size_t j = 0; j < b.nTimes; j++)
		{
			uint32* ptimes;
			AnimationBlockHeader* pHeadTimes;
			auto animIdIt = modelData.animIndexToAnimId.find(static_cast<uint>(j));
			if (animIdIt == modelData.animIndexToAnimId.end())
				continue;
			auto it = modelData.animfiles.find(animIdIt->second);
			if (it != modelData.animfiles.end())
			{
				GameFile* animfile = it->second.first;
				GameFile* skelfile = it->second.second;
				if (!animfile || !skelfile)
				{
					LOG_WARNING << "Animation data loading: null file pointer for animation index" << j;
					continue;
				}
				if (!skelfile->setChunk("SKB1"))
				{
					LOG_WARNING << "Animation data loading: setChunk(SKB1) failed for" << skelfile->fullname();
					continue;
				}
				unsigned char* skelBuf = skelfile->getBuffer();
				unsigned char* animBuf = animfile->getBuffer();
				// Only log if files claim to be open (not EOF) but have null buffers - this is the crash case
				if (!skelBuf || !animBuf)
				{
					if ((!skelBuf && !skelfile->isEof()) || (!animBuf && !animfile->isEof()))
					{
						LOG_WARNING << "Animation data loading: null buffer despite file not being EOF - skelBuf:" << (void*)skelBuf 
							<< "animBuf:" << (void*)animBuf 
							<< "skelEof:" << skelfile->isEof() 
							<< "animEof:" << animfile->isEof()
							<< "for files:" << skelfile->fullname() << "/" << animfile->fullname();
					}
					continue;
				}
				const size_t headerOffset = b.ofsTimes + j * sizeof(AnimationBlockHeader);
				if (skelfile->getSize() < headerOffset + sizeof(AnimationBlockHeader))
				{
					LOG_WARNING << "Animation data loading: header offset" << headerOffset << "out of bounds for" << skelfile->fullname();
					continue;
				}
				pHeadTimes = reinterpret_cast<AnimationBlockHeader*>(skelBuf + headerOffset);
				if (pHeadTimes->ofsEntrys >= animfile->getSize() || pHeadTimes->nEntrys > 10000)
				{
					LOG_WARNING << "Animation data loading: invalid header data - ofsEntrys:" << pHeadTimes->ofsEntrys 
						<< "nEntrys:" << pHeadTimes->nEntrys 
						<< "animSize:" << animfile->getSize()
						<< "for" << animfile->fullname();
					continue;
				}
				const size_t dataSize = static_cast<size_t>(pHeadTimes->nEntrys) * sizeof(uint32);
				if (dataSize > animfile->getSize() || pHeadTimes->ofsEntrys > animfile->getSize() - dataSize)
				{
					LOG_WARNING << "Animation data loading: data size" << dataSize << "at offset" << pHeadTimes->ofsEntrys 
						<< "exceeds file size" << animfile->getSize() << "for" << animfile->fullname();
					continue;
				}
				ptimes = reinterpret_cast<uint32*>(animBuf + pHeadTimes->ofsEntrys);
			}
			else
			{
				const size_t headerOffset = b.ofsTimes + j * sizeof(AnimationBlockHeader);
				if (f.getSize() < headerOffset + sizeof(AnimationBlockHeader))
					continue;
				pHeadTimes = reinterpret_cast<AnimationBlockHeader*>(f.getBuffer() + headerOffset);
				if (pHeadTimes->ofsEntrys >= f.getSize() || pHeadTimes->nEntrys > 10000)
					continue;
				const size_t dataSize = static_cast<size_t>(pHeadTimes->nEntrys) * sizeof(uint32);
				if (dataSize > f.getSize() || pHeadTimes->ofsEntrys > f.getSize() - dataSize)
					continue;
				ptimes = reinterpret_cast<uint32*>(f.getBuffer() + pHeadTimes->ofsEntrys);
			}

			for (size_t i = 0; i < pHeadTimes->nEntrys; i++)
				times[j].push_back(ptimes[i]);
		}

		// keyframes
		for (size_t j = 0; j < b.nKeys; j++)
		{
			D* keys;
			AnimationBlockHeader* pHeadKeys;
			auto animIdIt = modelData.animIndexToAnimId.find(static_cast<uint>(j));
			if (animIdIt == modelData.animIndexToAnimId.end())
				continue;
			auto it = modelData.animfiles.find(animIdIt->second);
			if (it != modelData.animfiles.end())
			{
				GameFile* animfile = it->second.first;
				GameFile* skelfile = it->second.second;
				if (!animfile || !skelfile)
				{
					LOG_WARNING << "Keyframe data loading: null file pointer for animation index" << j;
					continue;
				}
				if (!skelfile->setChunk("SKB1"))
				{
					LOG_WARNING << "Keyframe data loading: setChunk(SKB1) failed for" << skelfile->fullname();
					continue;
				}
				unsigned char* skelBuf = skelfile->getBuffer();
				unsigned char* animBuf = animfile->getBuffer();
				// Only log if files claim to be open (not EOF) but have null buffers - this is the crash case
				if (!skelBuf || !animBuf)
				{
					if ((!skelBuf && !skelfile->isEof()) || (!animBuf && !animfile->isEof()))
					{
						LOG_WARNING << "Keyframe data loading: null buffer despite file not being EOF - skelBuf:" << (void*)skelBuf 
							<< "animBuf:" << (void*)animBuf 
							<< "skelEof:" << skelfile->isEof() 
							<< "animEof:" << animfile->isEof()
							<< "for files:" << skelfile->fullname() << "/" << animfile->fullname();
					}
					continue;
				}
				const size_t headerOffset = b.ofsKeys + j * sizeof(AnimationBlockHeader);
				if (skelfile->getSize() < headerOffset + sizeof(AnimationBlockHeader))
				{
					LOG_WARNING << "Keyframe data loading: header offset" << headerOffset << "out of bounds for" << skelfile->fullname();
					continue;
				}
				pHeadKeys = reinterpret_cast<AnimationBlockHeader*>(skelBuf + headerOffset);
				if (pHeadKeys->ofsEntrys >= animfile->getSize() || pHeadKeys->nEntrys > 10000)
				{
					LOG_WARNING << "Keyframe data loading: invalid header data - ofsEntrys:" << pHeadKeys->ofsEntrys 
						<< "nEntrys:" << pHeadKeys->nEntrys 
						<< "animSize:" << animfile->getSize()
						<< "for" << animfile->fullname();
					continue;
				}
				const size_t multiplier = (type == INTERPOLATION_HERMITE || type == INTERPOLATION_BEZIER) ? 3 : 1;
				const size_t dataSize = static_cast<size_t>(pHeadKeys->nEntrys) * multiplier * sizeof(D);
				if (dataSize > animfile->getSize() || pHeadKeys->ofsEntrys > animfile->getSize() - dataSize)
				{
					LOG_WARNING << "Keyframe data loading: data size" << dataSize << "at offset" << pHeadKeys->ofsEntrys 
						<< "exceeds file size" << animfile->getSize() << "for" << animfile->fullname();
					continue;
				}
				keys = reinterpret_cast<D*>(animBuf + pHeadKeys->ofsEntrys);
			}
			else
			{
				const size_t headerOffset = b.ofsKeys + j * sizeof(AnimationBlockHeader);
				if (f.getSize() < headerOffset + sizeof(AnimationBlockHeader))
					continue;
				pHeadKeys = reinterpret_cast<AnimationBlockHeader*>(f.getBuffer() + headerOffset);
				if (pHeadKeys->ofsEntrys >= f.getSize() || pHeadKeys->nEntrys > 10000)
					continue;
				const size_t multiplier = (type == INTERPOLATION_HERMITE || type == INTERPOLATION_BEZIER) ? 3 : 1;
				const size_t dataSize = static_cast<size_t>(pHeadKeys->nEntrys) * multiplier * sizeof(D);
				if (dataSize > f.getSize() || pHeadKeys->ofsEntrys > f.getSize() - dataSize)
					continue;
				keys = reinterpret_cast<D*>(f.getBuffer() + pHeadKeys->ofsEntrys);
			}

			switch (type)
			{
			case INTERPOLATION_NONE:
			case INTERPOLATION_LINEAR:
				for (size_t i = 0; i < pHeadKeys->nEntrys; i++)
					data[j].push_back(Conv::conv(keys[i]));
				break;
			case INTERPOLATION_HERMITE:
			case INTERPOLATION_BEZIER: //let's use same values like hermite?!?
				for (size_t i = 0; i < pHeadKeys->nEntrys; i++)
				{
					data[j].push_back(Conv::conv(keys[i * 3]));
					in[j].push_back(Conv::conv(keys[i * 3 + 1]));
					out[j].push_back(Conv::conv(keys[i * 3 + 2]));
				}
				break;
			default: ;
			}
		}
	}

	friend std::ostream& operator<<(std::ostream& outVal, const Animated& v)
	{
		if (v.sizes == 0)
			return outVal;
		outVal << "      <type>" << v.type << "</type>" << std::endl;
		outVal << "      <seq>" << v.seq << "</seq>" << std::endl;
		outVal << "      <anims>" << std::endl;
		for (size_t j = 0; j < v.sizes; j++)
		{
			if (j != 0) 
			{
				// For global sequences, only process the first animation
				if (v.seq > -1)
					break;
				continue; // only output walk animation for non-global sequences
			}
			
			if (v.uses((unsigned int)j))
			{
				outVal << "    <anim id=\"" << j << "\" size=\"" << v.data[j].size() << "\">" << std::endl;
				for (size_t k = 0; k < v.data[j].size(); k++)
				{
					//        out << "      <data time=\"" << v.times[j][k]  << "\">" << v.data[j][k] << "</data>" << std::endl;
				}
				outVal << "    </anim>" << std::endl;
			}
		}
		outVal << "      </anims>" << std::endl;
		return outVal;
	}
};

typedef Animated<float, short, ShortToFloat> AnimatedShort;

float frand();

float randfloat(float lower, float upper);
_ANIMATED_API_ int randint(int lower, int upper);
