#ifndef ANIMATED_H
#define ANIMATED_H

#include <cassert>
#include <map>
#include <utility>
#include <vector>

#include "GameFile.h"
#include "modelheaders.h"
#include "quaternion.h"
#include "types.h"
#include "vec3d.h"

#include <QVector>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _ANIMATED_API_ __declspec(dllexport)
#    else
#        define _ANIMATED_API_ __declspec(dllimport)
#    endif
#else
#    define _ANIMATED_API_
#endif

class modelAnimData
{
public:
  std::map<uint, int16> animIndexToAnimId;
  std::map<int16, std::pair<GameFile *, GameFile *> > animfiles;
  QVector<uint32> globalSequences;
};

// interpolation functions
template<class T>
inline T interpolate(const float r, const T &v1, const T &v2)
{
  return static_cast<T>(v1*(1.0f - r) + v2 * r);
}

template<class T>
inline T interpolateHermite(const float r, const T &v1, const T &v2, const T &in, const T &out)
{
  // basis functions
  float h1 = 2.0f*r*r*r - 3.0f*r*r + 1.0f;
  float h2 = -2.0f*r*r*r + 3.0f*r*r;
  float h3 = r * r*r - 2.0f*r*r + r;
  float h4 = r * r*r - r * r;

  // interpolation
  return static_cast<T>(v1*h1 + v2 * h2 + in * h3 + out * h4);
}


template<class T>
inline T interpolateBezier(const float r, const T &v1, const T &v2, const T &in, const T &out)
{
  float InverseFactor = (1.0f - r);
  float FactorTimesTwo = r * r;
  float InverseFactorTimesTwo = InverseFactor * InverseFactor;
  // basis functions
  float h1 = InverseFactorTimesTwo * InverseFactor;
  float h2 = 3.0f * r * InverseFactorTimesTwo;
  float h3 = 3.0f * FactorTimesTwo * InverseFactor;
  float h4 = FactorTimesTwo * r;

  // interpolation
  return static_cast<T>(v1*h1 + v2 * h2 + in * h3 + out * h4);
}

// "linear" interpolation for quaternions should be slerp by default
template<>
inline Quaternion interpolate<Quaternion>(const float r, const Quaternion &v1, const Quaternion &v2)
{
  return Quaternion::slerp(r, v1, v2);
}


typedef std::pair<uint32, uint32> AnimRange;

// global time for global sequences
_ANIMATED_API_ extern uint32 globalTime;

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
  int16 x, y, z, w;
};

class Quat16ToQuat32 {
public:
  static const Quaternion conv(const PACK_QUATERNION t)
  {
    return Quaternion(
      float(t.x < 0 ? t.x + 32768 : t.x - 32767) / 32767.0f,
      float(t.y < 0 ? t.y + 32768 : t.y - 32767) / 32767.0f,
      float(t.z < 0 ? t.z + 32768 : t.z - 32767) / 32767.0f,
      float(t.w < 0 ? t.w + 32768 : t.w - 32767) / 32767.0f);
  }
};

// Convert opacity values stored as shorts to floating point
// I wonder why Blizzard decided to save 2 bytes by doing this
class ShortToFloat {
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

#define	MAX_ANIMATED	500
template <class T, class D = T, class Conv = Identity<T> >
class Animated {
public:

  int32 type, seq;
  QVector<uint32> globals;
  QVector<QVector<uint32>> times;
  QVector<QVector<T>> data;
  // for nonlinear interpolations:
  QVector<QVector<T>> in;
  QVector<QVector<T>> out;
  uint32 sizes; // for fix function

  bool uses(int32 anim) const
  {
    if (seq > -1)
      anim = 0;
    if (data.size() > 0 && anim > -1)
      return ((data[anim].size()) > 0);
    return false;
  }

  T getValue(int32 anim, uint32 time)
  {
    // obtain a time value and a data range
    if (seq >= 0 && seq < (int)globals.size()) {
      // TODO
      if (!globals[seq])
        return T();
      if (globals[seq] == 0)
        time = 0;
      else
        time = globalTime % globals[seq];
      anim = 0;
    }
    if (data.size() <= anim)
      return T();
    if (data[anim].size() > 1 && times[anim].size() > 1) {
      uint32 t1, t2;
      uint32 pos = 0;
      float r;
      uint32 max_time = times[anim][times[anim].size() - 1];
      //if (max_time > 0)
      //	time %= max_time; // I think this might not be necessary?
      if (time > max_time) {
        pos = times[anim].size() - 1;
        r = 1.0f;

        if (type == INTERPOLATION_NONE)
          return data[(int)anim][(int)pos];
        else if (type == INTERPOLATION_LINEAR)
          return interpolate<T>(r, data[anim][pos], data[anim][pos]);
        else if (type == INTERPOLATION_HERMITE) {
          // INTERPOLATION_HERMITE is only used in cameras afaik?
          return interpolateHermite<T>(r, data[anim][pos], data[anim][pos], in[anim][pos], out[anim][pos]);
        }
        else if (type == INTERPOLATION_BEZIER) {
          //Is this used ingame or only by custom models?
          return interpolateBezier<T>(r, data[anim][pos], data[anim][pos], in[anim][pos], out[anim][pos]);
        }
        else //this shouldn't appear!
          return data[anim][pos];
      }
      else {
        for (int32 i = 0; i < times[anim].size() - 1; i++) {
          if (time >= times[anim][i] && time < times[anim][i + 1]) {
            pos = i;
            break;
          }
        }
        t1 = times[anim][pos];
        t2 = times[anim][pos + 1];
        r = (time - t1) / (float)(t2 - t1);

        if (type == INTERPOLATION_NONE)
          return data[anim][pos];
        else if (type == INTERPOLATION_LINEAR)
          return interpolate<T>(r, data[anim][pos], data[anim][pos + 1]);
        else if (type == INTERPOLATION_HERMITE) {
          // INTERPOLATION_HERMITE is only used in cameras afaik?
          return interpolateHermite<T>(r, data[anim][pos], data[anim][pos + 1], in[anim][pos], out[anim][pos]);
        }
        else if (type == INTERPOLATION_BEZIER) {
          //Is this used ingame or only by custom models?
          return interpolateBezier<T>(r, data[anim][pos], data[anim][pos + 1], in[anim][pos], out[anim][pos]);
        }
        else //this shouldn't appear!
          return data[anim][pos];
      }
    }
    else {
      // default value
      if (data[anim].size() == 0)
        return T();
      else
        return data[anim][0];
    }
  }

  void init(AnimationBlock &b, GameFile * f, QVector<uint32> & gs)
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

    for (uint32 j = 0; j < b.nTimes; j++) {
      AnimationBlockHeader* pHeadTimes = (AnimationBlockHeader*)(f->getBuffer() + b.ofsTimes + j * sizeof(AnimationBlockHeader));

      unsigned int *ptimes = (unsigned int*)(f->getBuffer() + pHeadTimes->ofsEntrys);
      QVector<uint32> entries;
      for (uint32 i = 0; i < pHeadTimes->nEntrys; i++)
        entries.push_back(ptimes[i]);

      times.push_back(entries);
    }

    // keyframes
    for (uint32 j = 0; j < b.nKeys; j++) {
      AnimationBlockHeader* pHeadKeys = (AnimationBlockHeader*)(f->getBuffer() + b.ofsKeys + j * sizeof(AnimationBlockHeader));

      D *keys = (D*)(f->getBuffer() + pHeadKeys->ofsEntrys);

      QVector<T> entries;
      QVector<T> entriesIn;
      QVector<T> entriesOut;

      switch (type) {
      case INTERPOLATION_NONE:
      case INTERPOLATION_LINEAR:
        entries.clear();
        for (uint32 i = 0; i < pHeadKeys->nEntrys; i++)
          entries.push_back(Conv::conv(keys[i]));
        data.push_back(entries);
        break;
      case INTERPOLATION_HERMITE:
        entries.clear();
        entriesIn.clear();
        entriesOut.clear();
        for (uint32 i = 0; i < pHeadKeys->nEntrys; i++) {
          entries.push_back(Conv::conv(keys[i * 3]));
          entriesIn.push_back(Conv::conv(keys[i * 3 + 1]));
          entriesOut.push_back(Conv::conv(keys[i * 3 + 2]));
        }
        data.push_back(entries);
        in.push_back(entriesIn);
        out.push_back(entriesOut);
        break;
        //let's use same values like hermite?!?
      case INTERPOLATION_BEZIER:
        entries.clear();
        entriesIn.clear();
        entriesOut.clear();
        for (uint32 i = 0; i < pHeadKeys->nEntrys; i++) {
          entries.push_back(Conv::conv(keys[i * 3]));
          entriesIn.push_back(Conv::conv(keys[i * 3 + 1]));
          entriesOut.push_back(Conv::conv(keys[i * 3 + 2]));
        }
        data.push_back(entries);
        in.push_back(entriesIn);
        out.push_back(entriesOut);
        break;
      }
    }
  }

  void init(AnimationBlock &b, GameFile & f, const modelAnimData & modelData)
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

    for (uint32 j = 0; j < b.nTimes; j++)
    {
      uint32 *ptimes;
      AnimationBlockHeader* pHeadTimes;
      auto it = modelData.animfiles.find(modelData.animIndexToAnimId.at((uint)j));
      if (it != modelData.animfiles.end())
      {
        GameFile * animfile = it->second.first;
        GameFile * skelfile = it->second.second;
        skelfile->setChunk("SKB1");
        pHeadTimes = (AnimationBlockHeader*)(skelfile->getBuffer() + b.ofsTimes + j * sizeof(AnimationBlockHeader));
        ptimes = (uint32*)(animfile->getBuffer() + pHeadTimes->ofsEntrys);
        if (animfile->getSize() < pHeadTimes->ofsEntrys)
          continue;
      }
      else
      {
        pHeadTimes = (AnimationBlockHeader*)(f.getBuffer() + b.ofsTimes + j * sizeof(AnimationBlockHeader));
        ptimes = (uint32*)(f.getBuffer() + pHeadTimes->ofsEntrys);
        if (f.getSize() < pHeadTimes->ofsEntrys)
          continue;
      }

      QVector<uint32> entries;
      for (uint32 i = 0; i < pHeadTimes->nEntrys; i++)
        entries.push_back(ptimes[i]);
      times.push_back(entries);
    }

    // keyframes
    for (uint32 j = 0; j < b.nKeys; j++)
    {
      D *keys;
      AnimationBlockHeader* pHeadKeys;
      auto it = modelData.animfiles.find(modelData.animIndexToAnimId.at((uint)j));
      if (it != modelData.animfiles.end())
      {
        GameFile * animfile = it->second.first;
        GameFile * skelfile = it->second.second;
        skelfile->setChunk("SKB1");
        pHeadKeys = (AnimationBlockHeader*)(skelfile->getBuffer() + b.ofsKeys + j * sizeof(AnimationBlockHeader));
        keys = (D*)(animfile->getBuffer() + pHeadKeys->ofsEntrys);
        if (animfile->getSize() < pHeadKeys->ofsEntrys)
          continue;
      }
      else
      {
        pHeadKeys = (AnimationBlockHeader*)(f.getBuffer() + b.ofsKeys + j * sizeof(AnimationBlockHeader));
        keys = (D*)(f.getBuffer() + pHeadKeys->ofsEntrys);
        if (f.getSize() < pHeadKeys->ofsEntrys)
          continue;
      }

      QVector<T> entries;
      QVector<T> entriesIn;
      QVector<T> entriesOut;

      switch (type)
      {
      case INTERPOLATION_NONE:
      case INTERPOLATION_LINEAR:
        entries.clear();
        for (uint32 i = 0; i < pHeadKeys->nEntrys; i++)
          entries.push_back(Conv::conv(keys[i]));
        data.push_back(entries);
        break;
      case INTERPOLATION_HERMITE:
        entries.clear();
        entriesIn.clear();
        entriesOut.clear();
        for (uint32 i = 0; i < pHeadKeys->nEntrys; i++) {
          entries.push_back(Conv::conv(keys[i * 3]));
          entriesIn.push_back(Conv::conv(keys[i * 3 + 1]));
          entriesOut.push_back(Conv::conv(keys[i * 3 + 2]));
        }
        data.push_back(entries);
        in.push_back(entriesIn);
        out.push_back(entriesOut);
        break;
      case INTERPOLATION_BEZIER:
        entries.clear();
        entriesIn.clear();
        entriesOut.clear();
        for (uint32 i = 0; i < pHeadKeys->nEntrys; i++) {
          entries.push_back(Conv::conv(keys[i * 3]));
          entriesIn.push_back(Conv::conv(keys[i * 3 + 1]));
          entriesOut.push_back(Conv::conv(keys[i * 3 + 2]));
        }
        data.push_back(entries);
        in.push_back(entriesIn);
        out.push_back(entriesOut);
        break;
      }
    }
  }

  void fix(T fixfunc(const T))
  {
    switch (type) {
    case INTERPOLATION_NONE:
    case INTERPOLATION_LINEAR:
      for (uint32 i = 0; i < sizes; i++) {
        for (int32 j = 0; j < data[i].size(); j++) {
          data[i][j] = fixfunc(data[i][j]);
        }
      }
      break;
    case INTERPOLATION_HERMITE:
      for (uint32 i = 0; i < sizes; i++) {
        for (int32 j = 0; j < data[i].size(); j++) {
          data[i][j] = fixfunc(data[i][j]);
          in[i][j] = fixfunc(in[i][j]);
          out[i][j] = fixfunc(out[i][j]);
        }
      }
      break;
    case INTERPOLATION_BEZIER:
      for (uint32 i = 0; i < sizes; i++) {
        for (int32 j = 0; j < data[i].size(); j++) {
          data[i][j] = fixfunc(data[i][j]);
          in[i][j] = fixfunc(in[i][j]);
          out[i][j] = fixfunc(out[i][j]);
        }
      }
      break;
    }
  }
  friend std::ostream& operator<<(std::ostream& out, const Animated& v)
  {
    if (v.sizes == 0)
      return out;
    out << "      <type>" << v.type << "</type>" << endl;
    out << "      <seq>" << v.seq << "</seq>" << endl;
    out << "      <anims>" << endl;
    for (uint32 j = 0; j < v.sizes; j++) {
      if (j != 0) continue; // only output walk animation
      if (v.uses((unsigned int)j)) {
        out << "    <anim id=\"" << j << "\" size=\"" << v.data[j].size() << "\">" << endl;
        for (int32 k = 0; k < v.data[j].size(); k++) {
          out << "      <data time=\"" << v.times[j][k] << "\">" << v.data[j][k] << "</data>" << endl;
        }
        out << "    </anim>" << endl;
      }
      if (v.seq > -1 && j > 0)
        break;
    }
    out << "      </anims>" << endl;
    return out;
  }
};

typedef Animated<float, short, ShortToFloat> AnimatedShort;

float frand();
Vec3D fixCoordSystem(Vec3D v);
Vec3D fixCoordSystem2(Vec3D v);
Quaternion fixCoordSystemQuat(Quaternion v);

float randfloat(float lower, float upper);
_ANIMATED_API_ int randint(int lower, int upper);

#endif