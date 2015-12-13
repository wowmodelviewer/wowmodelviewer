/*
 * GameFolder.h
 *
 *  Created on: 12 dec. 2014
 *      Author: Jeromnimo
 */

#ifndef _GAMEFOLDER_H_
#define _GAMEFOLDER_H_

#include <stdio.h>
#include <map>

#include <QString>
#include <QStringList>

#include "metaclasses/Container.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _GAMEFOLDER_API_ __declspec(dllexport)
#    else
#        define _GAMEFOLDER_API_ __declspec(dllimport)
#    endif
#else
#    define _GAMEFOLDER_API_
#endif

class _GAMEFOLDER_API_ GameFolder : public Container<Component>
{
  public:
    GameFolder(QString & path);
    virtual ~GameFolder() {}

    void createChildren(QStringList &);

    void onChildAdded(Component *);

    GameFolder * getFolder(QString &name);

  private:
    std::map<QString, GameFolder *> m_subFolderMap;
};



#endif /* _GAMEFOLDER_H_ */
