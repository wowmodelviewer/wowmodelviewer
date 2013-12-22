/*
 * ArmoryImporter.h
 *
 *  Created on: 9 déc. 2013
 *
 */

#ifndef _ARMORYIMPORTER_H_
#define _ARMORYIMPORTER_H_

#include "URLImporter.h"

class ArmoryImporter : public URLImporter
{
  public:
	ArmoryImporter();
	~ArmoryImporter();

	CharInfos * importChar(std::string url);
	ItemRecord * importItem(std::string url);
};




#endif /* _ARMORYIMPORTER_H_ */
