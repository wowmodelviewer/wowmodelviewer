/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 *   Copyright: 2020 , WoW Model Viewer (https://wowmodelviewer.net)
 */

#ifndef M2TRACK_H
#define M2TRACK_H

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <vector>

// Qt

// Externals

// Other libraries
#include "GameFile.h"
#include "logger/Logger.h"

// Current library
#include "animated.h" // modelAnimData
#include "Interpolated.h"
#include "modelheaders.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
template <class T>
class M2Track
{
public:
  void init(GameFile & f, M2TrackDef & def, const modelAnimData & modelData)
  {
    interpolation_ = def.type;
    globalSequenceId_ = def.seq;
    globalSequences_ = modelData.globalSequences;

    for (uint32 t = 0; t < def.nTimestamps; t++)
    {
      GameFile * animfile = &f;
      GameFile * skelfile = &f;

      auto it = modelData.animfiles.find(modelData.animIndexToAnimId.at(t));
      if (it != modelData.animfiles.end())
      {
        animfile = it->second.first;
        skelfile = it->second.second;
        skelfile->setChunk("SKB1");
      }
      
      uint32 *ptimes;
      M2ArrayDef *pArrayDef = (M2ArrayDef*)(skelfile->getBuffer() + def.ofsTimestamps + t * sizeof(M2ArrayDef));
      ptimes = (uint32*)(animfile->getBuffer() + pArrayDef->ofsEntrys);

      std::vector<uint32> timestamps;
      timestamps.reserve(pArrayDef->nEntrys);

      for (uint32 i = 0; i < pArrayDef->nEntrys; i++)
        timestamps.push_back(ptimes[i]);

      pArrayDef = (M2ArrayDef*)(skelfile->getBuffer() + def.ofsKeys + t * sizeof(M2ArrayDef));

      values_.emplace_back(timestamps, readValues((T*)(animfile->getBuffer() + pArrayDef->ofsEntrys), pArrayDef->nEntrys), (InterpolationType)interpolation_);
    }

  }

  bool uses(ssize_t anim) const
  {
    if (globalSequenceId_ != -1)
      anim = 0;

    return ((anim >= 0) && (anim < (int)values_.size()) && !values_[anim].empty());
  }

  T getValue(ssize_t anim, size_t time)
  {
    T result;

    if (globalSequenceId_ >= 0 && globalSequenceId_ < (int)globalSequences_.size())
    {
      if (!globalSequences_[globalSequenceId_])
        return result;
      if (globalSequences_[globalSequenceId_] == 0)
        time = 0;
      else
        time = globalTime % globalSequences_[globalSequenceId_];

      anim = 0;
    }

    if((anim >= 0) && (anim < (int)values_.size()))
      result = values_[anim].getValue(time);

    return result;
  }

  int16 interpolation() const
  {
    return interpolation_;
  }

  void transform(const T & v)
  {
    for (auto & it : values_)
      it.transform(v);
  }

  void dump()
  {
    LOG_INFO << "==== M2Track ====";
    LOG_INFO << "globalSequenceId" << globalSequenceId_;
    LOG_INFO << "--- global sequences ---";
    LOG_INFO << "nb global sequences" << globalSequences_.size();
    for (uint i = 0; i < globalSequences_.size(); i++)
      LOG_INFO << i << globalSequences_[i];
    LOG_INFO << "------ values ------";
    LOG_INFO << "nb values" << values_.size();
    for (uint i = 0; i < values_.size(); i++)
    {
      LOG_INFO << "---- Interpolated" << i << "----";
      values_[i].dump();
    }
    LOG_INFO << "=================";
  }

private:
  std::vector<T> readValues(T * vals, const uint32 nbVals) const;

  int16 interpolation_;
  int16 globalSequenceId_;
  std::vector<uint32> globalSequences_;

  std::vector<Interpolated<T> > values_;

};
#endif