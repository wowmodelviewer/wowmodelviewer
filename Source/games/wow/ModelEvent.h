/*
 * ModelEvent.h
 *
 *  Created on: 22 oct. 2013
 *
 */

#pragma once

#include "modelheaders.h"

#include <iostream>

class GameFile;

class ModelEvent
{
	ModelEventDef def;

public:
	void init(ModelEventDef& med);

	friend std::ostream& operator<<(std::ostream& out, const ModelEvent& v)
	{
		out << "    <id>" << v.def.id[0] << v.def.id[1] << v.def.id[2] << v.def.id[3] << "</id>" << std::endl;
		out << "    <dbid>" << v.def.dbid << "</dbid>" << std::endl;
		out << "    <bone>" << v.def.bone << "</bone>" << std::endl;
		out << "    <pos>" << v.def.pos.x << " " << v.def.pos.y << " " << v.def.pos.z << "</pos>" << std::endl;
		out << "    <type>" << v.def.type << "</type>" << std::endl;
		out << "    <seq>" << v.def.seq << "</seq>" << std::endl;
		out << "    <nTimes>" << v.def.nTimes << "</nTimes>" << std::endl;
		out << "    <ofsTimes>" << v.def.ofsTimes << "</ofsTimes>" << std::endl;
		return out;
	}
};
