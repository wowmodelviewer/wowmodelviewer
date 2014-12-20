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
		out << "		<id>" << v.def.id[0] << v.def.id[1] << v.def.id[2] << v.def.id[3] << "</id>" << endl;
		out << "		<dbid>" << v.def.dbid << "</dbid>" << endl;
		out << "		<bone>" << v.def.bone << "</bone>" << endl;
		out << "		<pos>" << v.def.pos << "</pos>" << endl;
		out << "		<type>" << v.def.type << "</type>" << endl;
		out << "		<seq>" << v.def.seq << "</seq>" << endl;
		out << "		<nTimes>" << v.def.nTimes << "</nTimes>" << endl;
		out << "		<ofsTimes>" << v.def.ofsTimes << "</ofsTimes>" << endl;
		return out;
	}
};


#endif /* _MODELEVENT_H_ */
