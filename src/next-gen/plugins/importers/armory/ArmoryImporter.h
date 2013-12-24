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
 * ArmoryImporter.h
 *
 *  Created on: 9 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _ARMORYIMPORTER_H_
#define _ARMORYIMPORTER_H_

#include "next-gen/core/URLImporter.h"

#include "wx/jsonreader.h"

class ArmoryImporter : public URLImporter
{
  public:
    ArmoryImporter();
    ~ArmoryImporter();

    NPCInfos * importNPC(std::string url) {return NULL;};
    CharInfos * importChar(std::string url);
    ItemRecord * importItem(std::string url);

  private:
    enum ImportType
    {
      CHARACTER,
      ITEM
    };

    int readJSONValues(ImportType type, std::string url, wxJSONValue & result);


};




#endif /* _ARMORYIMPORTER_H_ */
