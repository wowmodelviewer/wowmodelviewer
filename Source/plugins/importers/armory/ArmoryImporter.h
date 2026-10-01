/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * ArmoryImporter.h
 *
 *  Created on: 9 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _ARMORYIMPORTER_H_
#define _ARMORYIMPORTER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QtPlugin>
#include <QVariantMap>

// Externals

// Other libraries
#define _IMPORTERPLUGIN_CPP_ // to define interface
#include "ImporterPlugin.h"
#undef _IMPORTERPLUGIN_CPP_

// Current library
#include "ArmoryProxy.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class ArmoryImporter final : public ImporterPlugin
{
    Q_INTERFACES(ImporterPlugin)
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "wowmodelviewer.importers.WowheadImporter" FILE "armoryimporter.json")

  public :
    // Constants / Enums

    // Constructors
    ArmoryImporter() = default;

    // Destructors
    ~ArmoryImporter() = default;

    // Methods
    bool acceptURL(QString url) const override;

    NPCInfos * importNPC(QString url) const override { return nullptr; };
    CharInfos * importChar(QString url) const override;
    ItemRecord * importItem(QString url) const override;

    // Reached from the viewer through Qt's meta-object system (QMetaObject::invokeMethod),
    // so neither the ImporterPlugin interface nor CharInfos changes shape, and an older
    // viewer or plugin simply does without them.
    //
    // What the last successful importChar() read about the character beyond what CharInfos
    // carries, for the import dialog's summary: name, realmName, realmSlug, region, raceName,
    // className, genderName. Empty after a failed import.
    Q_INVOKABLE QVariantMap lastCharacter() const;

    // A region's realm list from the proxy, for the import dialog's realm picker:
    // { ok, unsupported, message, realms: [ { slug, name, id }, ... ] }. unsupported is true
    // when the proxy answers but has no realm list route (an older deployment).
    Q_INVOKABLE QVariantMap realmList(const QString & region) const;

    // Members

  protected :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods

    // Members

  private :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods
    static ArmoryProxy::Result gatherCharacter(const QString & url, ArmoryProxy::Character & character);
    static QString proxyTemplate();
    static bool hasMember(const QJsonValueRef & check, const QString & lookfor);
    static bool hasTransmog(const QJsonValueRef & check);

    // Members
    mutable QVariantMap m_lastCharacter;

    // friend class declarations

};

// static members definition
#ifdef _ARMORYIMPORTER_CPP_

#endif

#endif /* _ARMORYIMPORTER_H_ */
