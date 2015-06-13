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
 * FBXExporter.cpp
 *
 *  Created on: 13 june 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _FBXEXPORTER_CPP_
#include "FBXExporter.h"
#undef _FBXEXPORTER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries

// Current library

// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------


// Constructors
//--------------------------------------------------------------------


// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------

std::string FBXExporter::menuLabel() const
{
  return "FBX...";
}

std::string FBXExporter::fileSaveTitle() const
{
  return "Save FBX file";
}

std::string FBXExporter::fileSaveFilter() const
{
  return "FBX files (*.fbx)|*.fbx";
}


bool FBXExporter::exportModel(WoWModel * model, std::string target) const
{
  if(!model)
    return false;

  return true;
}

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
