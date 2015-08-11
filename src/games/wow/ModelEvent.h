/*
 * ModelEvent.h
 *
 *  Created on: 22 oct. 2013
 *
 */

#ifndef _MODELEVENT_H_
#define _MODELEVENT_H_

#include "modelheaders.h"

class GameFile;

class ModelEvent {
	ModelEventDef def;
public:
	void init(GameFile *, ModelEventDef &mad, uint32 *global);

	friend std::ostream& operator<<(std::ostream& out, ModelEvent& v)
	{
		out << "		<id>" << v.def.id[0] << v.def.id[1] << v.def.id[2] << v.def.id[3] << "</id>" << std::endl;
		out << "		<dbid>" << v.def.dbid << "</dbid>" << std::endl;
		out << "		<bone>" << v.def.bone << "</bone>" << std::endl;
		out << "		<pos>" << v.def.pos << "</pos>" << std::endl;
		out << "		<type>" << v.def.type << "</type>" << std::endl;
		out << "		<seq>" << v.def.seq << "</seq>" << std::endl;
		out << "		<nTimes>" << v.def.nTimes << "</nTimes>" << std::endl;
		out << "		<ofsTimes>" << v.def.ofsTimes << "</ofsTimes>" << std::endl;
		return out;
	}
};


#endif /* _MODELEVENT_H_ */
