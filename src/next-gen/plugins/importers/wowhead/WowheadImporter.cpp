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
 * WowheadImporter.cpp
 *
 *  Created on: 1 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _WOWHEADIMPORTER_CPP_
#include "WowheadImporter.h"
#undef _WOWHEADIMPORTER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Irrlicht

// Externals
#include <wx/sstream.h>
#include <wx/url.h>
#include <wx/html/htmlpars.h>

// Other libraries
#include "database.h" // ItemRecord
#include "core/NPCInfos.h"
#include "util.h" // CSConv

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
bool WowheadImporter::acceptURL(std::string url) const
{
  return (url.find("wowhead") != std::string::npos);
}

NPCInfos * WowheadImporter::importNPC(std::string urlToGrab) const
{
  wxInitialize();
  NPCInfos * result = NULL;

  // TODO: rewrite this part with Qt to handle UTF8 correctly
  wxURL url(urlToGrab);
  if(url.GetError()==wxURL_NOERR)
  {
    wxString htmldata;
    wxInputStream *in = url.GetInputStream();

    if(in && in->IsOk())
    {
      wxStringOutputStream html_stream(&htmldata);
      in->Read(html_stream);

      std::string content(html_stream.GetString().ToAscii());

      // let's go : finding name
      // extract global infos
      std::string pattern("(g_npcs[");
      std::string patternEnd(";");
      std::size_t beginIndex = content.find(pattern);
      std::string infos = content.substr(beginIndex);
      std::size_t endIndex = infos.find(patternEnd);
      infos = infos.substr(0,endIndex);

      // finding name
      std::string NPCName = extractSubString(infos,"name\":\"","\",");

      // finding type
      std::string NPCType = extractSubString(infos,"type\":","}");

      // finding id
      std::string NPCId = extractSubString(infos,"id\":",",");

      // display id
      pattern = "ModelViewer.show({";
      std::string NPCDispId = content.substr(content.find(pattern)+pattern.length());
      NPCDispId = extractSubString(NPCDispId,"displayId: "," ");

      if(NPCDispId.find(",") != std::string::npos) // comma at end of id
        NPCDispId = NPCDispId.substr(0,NPCDispId.find(","));

      result = new NPCInfos();

      result->name = NPCName;
      result->type = atoi(NPCType.c_str());
      result->id = atoi(NPCId.c_str());
      result->displayId = atoi(NPCDispId.c_str());

    }
    delete in;
  }
  wxUninitialize();
  return result;
}

ItemRecord * WowheadImporter::importItem(std::string urlToGrab) const
{
  wxInitialize();
  ItemRecord * result = NULL;

  wxURL url(urlToGrab);
  if(url.GetError()==wxURL_NOERR)
  {
    wxString htmldata;
    wxInputStream *in = url.GetInputStream();

    if(in && in->IsOk())
    {
      wxStringOutputStream html_stream(&htmldata);
      in->Read(html_stream);

      std::string content(html_stream.GetString().ToAscii());

      // let's go : finding name
      // extract global infos
      std::string pattern("(g_items[");
      std::string patternEnd(";");
      std::size_t beginIndex = content.find(pattern);
      std::string infos = content.substr(beginIndex);
      std::size_t endIndex = infos.find(patternEnd);
      infos = infos.substr(0,endIndex);

      // finding name
      // due to specific stuff on index, name is treated here, not with method like others
      pattern = "name\":\"";
      patternEnd = "\",";
      std::string itemName = infos.substr(infos.find(pattern)+pattern.length());
      itemName = itemName.substr(1,itemName.find(patternEnd)-1); // first char is a number in name

      // finding type
      std::string itemType = extractSubString(infos,"slot\":", "}");

      // finding id
      std::string itemId = extractSubString(infos,"[", "]");

      // display id
      std::string itemDisplayId = extractSubString(infos,"displayid\":", "\",");

      // class
      // 3 sss it's not a typo (probably to avoid conflict with "class" keyword in javascript)
      std::string itemClass = extractSubString(infos,"classs\":", "\",");

      // subclass
      std::string idemSubClass = extractSubString(infos,"subclass\":", "\",");

      result = new ItemRecord();

      result->name = QString::fromStdString(itemName);
      result->type = atoi(itemType.c_str());
      result->id = atoi(itemId.c_str());
      result->model = atoi(itemDisplayId.c_str());
      result->itemclass = atoi(itemClass.c_str());
      result->subclass = atoi(idemSubClass.c_str());
    }
    delete in;
  }

  wxUninitialize();
  return result;
}


// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
std::string WowheadImporter::extractSubString(std::string & datas, std::string beginPattern, std::string endPattern) const
{
  std::string result;
  try
  {
    result = datas.substr(datas.find(beginPattern)+beginPattern.length());
    result = result.substr(0,result.find(endPattern));
  }
  catch(...)
  {
  }
  return result;
}
