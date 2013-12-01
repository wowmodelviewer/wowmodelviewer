/*
 * WowheadImporter.cpp
 *
 *  Created on: 1 déc. 2013
 *
 */

#include "WowheadImporter.h"

#include "NPCInfos.h"
#include "util.h" // CSConv

#include <wx/sstream.h>
#include <wx/url.h>
#include <wx/html/htmlpars.h>

WowheadImporter::WowheadImporter()
{

}

WowheadImporter::~WowheadImporter()
{

}


NPCInfos * WowheadImporter::importNPC(std::string urlToGrab)
{
	NPCInfos * result = NULL;

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
			pattern = "name\":\"";
			patternEnd = "\",";
			std::string NPCName = infos.substr(infos.find(pattern)+pattern.length());
			NPCName = NPCName.substr(0,NPCName.find(patternEnd));

			// finding type
			pattern = "type\":";
			patternEnd = "}";
			std::string NPCType = infos.substr(infos.find(pattern)+pattern.length());
			NPCType = NPCType.substr(0,NPCType.find(patternEnd));

			// finding id
			pattern = "id\":";
			patternEnd = ",";
			std::string NPCId = infos.substr(infos.find(pattern)+pattern.length());
			NPCId = NPCId.substr(0,NPCId.find(patternEnd));

			// display id
			pattern = "ModelViewer.show({";
			std::string NPCDispId = content.substr(content.find(pattern)+pattern.length());
			pattern = "displayId: ";
			NPCDispId = NPCDispId.substr(NPCDispId.find(pattern)+pattern.length());
			patternEnd = " ";
			NPCDispId = NPCDispId.substr(0,NPCDispId.find(patternEnd));
			if(NPCDispId.find(",") != std::string::npos) // comma at end of id
				NPCDispId = NPCDispId.substr(0,NPCDispId.find(","));

			result = new NPCInfos();

			result->name = CSConv(NPCName).mb_str();
			result->type = atoi(NPCType.c_str());
			result->id = atoi(NPCId.c_str());
			result->displayId = atoi(NPCDispId.c_str());

		}
		delete in;
	}
	return result;
}

