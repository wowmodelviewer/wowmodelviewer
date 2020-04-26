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

#ifndef INTERPOLATED_H
#define INTERPOLATED_H

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <vector>

// Qt

// Externals

// Other libraries

// Current library
#include "types.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
enum InterpolationType
{
  NONE,
  LINEAR,
  HERMITE,
  BEZIER
};

template <class T>
class Interpolated
{
public:
  Interpolated(const std::vector<uint32> & timestamps, const std::vector<T> & values, InterpolationType type)
    : timestamps_(timestamps), values_(values), interpolation_(type)
  {
    // if timestamps & values sizes are different, shrink to the smallest one
    if (timestamps_.size() < values_.size())
      values_.resize(timestamps_.size());
    if (timestamps_.size() > values_.size())
      timestamps_.resize(values_.size());
   }

  Interpolated() = delete;

  Interpolated(const Interpolated & in)
    : timestamps_(in.timestamps_), values_(in.values_), interpolation_(in.interpolation_)
  {
    
  }

  Interpolated& operator=(const Interpolated& in)
  {
    if (this != &in)
    {
      timestamps_ = in.timestamps_;
      values_ = in.values_;
      interpolation_ = in.interpolation_;
    }
    return *this;
  }

  T getValue(uint32 time) const
  {
    // if there is no value, return default value
    if (timestamps_.empty())
      return T();

    // if there is only one value, return this one
    if (timestamps_.size() == 1)
      return values_[0];

    // otherwise, interpolate
    // find corresponding index in timestamps
    if(time >= timestamps_[timestamps_.size()-1])
    {
      return values_[values_.size() - 1];
    }
    else
    {
      auto pos = 0;
     
      for (size_t i = 0; i < timestamps_.size() - 1; i++)
      {
        if (time >= timestamps_[i] && time < timestamps_[i + 1])
        {
          pos = i;
          break;
        }
      }

      return interpolate(values_[pos], values_[pos + 1],
                         (float)(time - timestamps_[pos]) / (float)(timestamps_[pos + 1] - timestamps_[pos]));
    }
  }

  bool empty() const
  {
    return timestamps_.empty();
  }

private:
  InterpolationType interpolation_;

  std::vector<uint32> timestamps_;
  std::vector<T> values_;

  template<class T>
  T interpolate(const T &v1, const T &v2, const float r) const
  {
    T result = v1;
    switch(interpolation_)
    {
      case NONE:
        break;
      case LINEAR:
        result = v1 * (1.0f - r) + v2 * r;
        break;
      case HERMITE:
      {
        /* TODO
        // basis functions
        float h1 = 2.0f*r*r*r - 3.0f*r*r + 1.0f;
        float h2 = -2.0f*r*r*r + 3.0f*r*r;
        float h3 = r * r*r - 2.0f*r*r + r;
        float h4 = r * r*r - r * r;

        // interpolation
        result = v1*h1 + v2 * h2 + in * h3 + out * h4;
        */
        break;
      }
      case BEZIER:
      {
        /* TODO
        float InverseFactor = (1.0f - r);
        float FactorTimesTwo = r * r;
        float InverseFactorTimesTwo = InverseFactor * InverseFactor;

        // basis functions
        float h1 = InverseFactorTimesTwo * InverseFactor;
        float h2 = 3.0f * r * InverseFactorTimesTwo;
        float h3 = 3.0f * FactorTimesTwo * InverseFactor;
        float h4 = FactorTimesTwo * r;

        // interpolation
        result = v1*h1 + v2 * h2 + in * h3 + out * h4;
        */
        break;
      }
    }
    return result;
  }
};

#endif