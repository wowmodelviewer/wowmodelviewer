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

#include "Interpolated.h"


// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries
#include "logger/Logger.h"

// Current library
#include "quaternion.h"
#include "vec3d.h"


// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//====================================================================
template <>
void Interpolated<Vec3D>::dump(size_t i, const Vec3D & val) const
{
  LOG_INFO << i << val.x << val.y << val.z;
}

template <>
void Interpolated<Quaternion>::dump(size_t i, const Quaternion & val) const
{
  LOG_INFO << i << val.x << val.y << val.z << val.w;
}
