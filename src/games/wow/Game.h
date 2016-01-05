/*
 * Game.h
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#ifndef _GAME_H_
#define _GAME_H_

#include <QString>

#include "GameDatabase.h"
#include "GameFolder.h"


#define GAMEDIRECTORY Game::instance().gameFolder()
#define GAMEDATABASE Game::instance().gameDatabase()

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _GAME_API_ __declspec(dllexport)
#    else
#        define _GAME_API_ __declspec(dllimport)
#    endif
#else
#    define _GAME_API_
#endif

class _GAME_API_ Game
{
  public:
    static Game & instance()
    {
      if(Game::m_instance == 0)
        Game::m_instance = new Game();
      return *m_instance;
    }

    void init(const QString & path);
    void addCustomFiles(const QString &path, bool bypassOriginalFiles);


    GameFolder & gameFolder() { return m_gameFolder; }
    GameDatabase & gameDatabase() { return m_gameDatabase;}

  private:
    // disable explicit construct and destruct
    Game();
    virtual ~Game() {}
    Game(const Game &);
    void operator=(const Game &);

    GameFolder m_gameFolder;
    GameDatabase m_gameDatabase;

    static Game * m_instance;
};



#endif /* _GAME_H_ */
