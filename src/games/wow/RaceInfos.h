#ifndef _RACEINFOS_H_
#define _RACEINFOS_H_

#include <map>
#include <string>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _RACEINFOS_API_ __declspec(dllexport)
#    else
#        define _RACEINFOS_API_ __declspec(dllimport)
#    endif
#else
#    define _RACEINFOS_API_
#endif

class WoWModel;

class _RACEINFOS_API_ RaceInfos
{
  public:
    int raceid;
    int sexid; // 0 male / 1 female
    int textureLayoutID;
    bool isHD;
    std::string prefix;
    std::string customization[3];

    static bool getCurrent(WoWModel * model, RaceInfos & result);
    static void init();
    static int getHDModelForFileID(int);

  private:
    static std::map< int, RaceInfos> RACES;
};




#endif /* _RACEINFOS_H_ */
