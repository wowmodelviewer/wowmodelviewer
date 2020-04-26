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
 *  Copyright: 2020 , WoW Model Viewer (https://wowmodelviewer.net)
 */

#include "M2Track.h"


// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries
#include "quaternion.h"
#include "logger/Logger.h"

// Current library
// In WoW 2.0+ Blizzard are now storing rotation data in 16bit values instead of 32bit.
// I don't really understand why as its only a very minor saving in model sizes and adds extra overhead in
// processing the models.  Need this structure to read the data into.
struct Pack_quaternion {
  int16 x, y, z, w;
};


// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//====================================================================
template <>
std::vector<Vec3D> M2Track<Vec3D>::readValues(const uint32 index, Vec3D * vals, const uint32 nbVals) const
{
  std::vector<Vec3D> result(nbVals);

  for (uint32 i = 0; i < nbVals; i++)
    result.emplace_back(vals[i].x, vals[i].z, -vals[i].y);

  return result;
}

std::vector<Quaternion> M2Track<Quaternion>::readValues(const uint32 index, Quaternion * vals, const uint32 nbVals) const
{
  std::vector<Quaternion> result(nbVals);

  const auto packVals = reinterpret_cast<Pack_quaternion *>(vals);

  for (uint32 i = 0; i < nbVals; i++)
  {
    const auto packQuat = packVals[i];

    /*
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

    Quaternion fixCoordSystemQuat(Quaternion v)
    {
        return Quaternion(-v.x, -v.z, v.y, v.w);
    }
     */

    result.emplace_back(
        float(packQuat.x < 0 ? packQuat.x + 32768 : packQuat.x - 32767) / -32767.0f,
        float(packQuat.z < 0 ? packQuat.z + 32768 : packQuat.z - 32767) / -32767.0f,
        float(packQuat.y < 0 ? packQuat.y + 32768 : packQuat.y - 32767) / 32767.0f,
        float(packQuat.w < 0 ? packQuat.w + 32768 : packQuat.w - 32767) / 32767.0f
    );
  }

  return result;
}

