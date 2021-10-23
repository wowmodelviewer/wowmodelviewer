/*
 * ModelEvent.h
 *
 *  Created on: 22 oct. 2013
 *
 */

#ifndef _MODELEVENT_H_
#define _MODELEVENT_H_

#include "modelheaders.h"

#include <iostream>

class GameFile;

class ModelEvent 
{
  M2Event def;
public:
  void init(M2Event &med);

  friend std::ostream& operator<<(std::ostream& out, const ModelEvent& v)
  {
    out << "    <id>" << v.def.id[0] << v.def.id[1] << v.def.id[2] << v.def.id[3] << "</id>" << std::endl;
    out << "    <data>" << v.def.data << "</data>" << std::endl;
    out << "    <bone>" << v.def.bone << "</bone>" << std::endl;
    out << "    <pos>" << v.def.position.x << " " << v.def.position.y << " " << v.def.position.z << "</pos>" << std::endl;
    out << "    <type>" << v.def.enabled.interpolation_type << "</type>" << std::endl;
    out << "    <seq>" << v.def.enabled.global_sequence << "</seq>" << std::endl;
    out << "    <nTimes>" << v.def.enabled.timestamps.number << "</nTimes>" << std::endl;
    out << "    <ofsTimes>" << v.def.enabled.timestamps.offset << "</ofsTimes>" << std::endl;
    return out;
  }
};


#endif /* _MODELEVENT_H_ */
