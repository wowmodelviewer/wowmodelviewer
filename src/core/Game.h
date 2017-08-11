/*
 * Game.h
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#ifndef _GAME_H_
#define _GAME_H_

#include <QString>

#include "GameFolder.h"
#include "GameDatabase.h"

#define GAMEDIRECTORY core::Game::instance().folder()
#define GAMEDATABASE core::Game::instance().database()

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _GAME_API_ __declspec(dllexport)
#    else
#        define _GAME_API_ __declspec(dllimport)
#    endif
#else
#    define _GAME_API_
#endif

namespace core
{
  class _GAME_API_ Game
  {
    public:
      static Game & instance()
      {
        if (Game::m_instance == 0)
          Game::m_instance = new Game();
        return *m_instance;
      }

      void init(core::GameFolder * folder, core::GameDatabase * db);
      bool initDone() { return ((m_db != 0) && (m_folder != 0)); }
      void addCustomFiles(const QString &path, bool bypassOriginalFiles);

      core::GameFolder & folder() { return *m_folder; }
      core::GameDatabase & database() { return *m_db; }

      void setConfigFolder(const QString & folder) { m_configFolder = folder; }
      QString configFolder() { return m_configFolder; }

    private:

      // disable explicit construct and destruct
      Game();
      virtual ~Game() {}
      Game(const Game &);
      void operator=(const Game &);

      core::GameFolder * m_folder;
      core::GameDatabase * m_db;

      QString m_configFolder;

      static Game * m_instance;
  };
}


#endif /* _GAME_H_ */
