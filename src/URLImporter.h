/*
 * URLImporter.h
 *
 *  Created on: 25 nov. 2013
 *
 */

#ifndef _URLIMPORTER_H_
#define _URLIMPORTER_H_

#include <string>

class ItemInfos;
class CharInfos;
class NPCInfos;

class URLImporter
{
  public:
	URLImporter();
	virtual  ~URLImporter();

	virtual NPCInfos * importNPC(std::string url);
	virtual ItemInfos * importItem(std::string url);
	virtual CharInfos * importChar(std::string url);
};


#endif /* _URLIMPORTER_H_ */
