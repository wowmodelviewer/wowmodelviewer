#ifndef _RACEINFOS_H_
#define _RACEINFOS_H_

#include <map>
#include <string>

class RaceInfos
{
  public:
    int raceid;
    int sexid; // 0 male / 1 female
    int textureLayoutID;
    bool isHD;
    std::string prefix;
    std::string customization[3];

    static bool getCurrent(std::string modelName, RaceInfos & result);
    static void init();

  private:
    static std::map< std::string, RaceInfos> RACES;
};




#endif /* _RACEINFOS_H_ */
