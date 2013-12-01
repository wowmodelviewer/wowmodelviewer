/*
 * NPCInfos.h
 *
 *  Created on: 26 nov. 2013
 *
 */

#ifndef _NPCINFOS_H_
#define _NPCINFOS_H_

#include <string>

class NPCInfos
{
  public:
	NPCInfos();
	~NPCInfos();

	int id;
	int displayId;
	int type;
	std::string name;
};

#endif /* _NPCINFOS_H_ */
