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
 * FBXAnimExporter.h
 *
 *  Created on: 14 may 2019
 *   Copyright: 2019 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _FBXANIMEXPORTER_H_
#define _FBXANIMEXPORTER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <vector>

// Qt
#include <QString>
#include <QRunnable>
#include <QMutex>

// Externals
#include "fbxsdk.h"

// Other libraries


// Current library


class WoWModel;
struct ModelAnimation;

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class FBXAnimExporter : public QRunnable
{

  public :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods
    void run() override;
    void setValues(FbxString fileVersion, QString fn, QString an, WoWModel *m, std::vector<FbxCluster*> bc, FbxNode* &meshnode, int aID, bool uan = false);

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

    // Members
    FbxString l_fileVersion;
    QString srcfileName;
    QString animationName;
    WoWModel *l_model;
    std::vector<FbxCluster*> l_boneClusters;
    FbxNode *l_meshNode;
    int animID;
    bool useAltNaming = false;
    mutable QMutex m_mutex;

    // friend class declarations

};

// static members definition
#ifdef _FBXANIMEXPORTER_CPP_

#endif

#endif /* _FBXANIMEXPORTER_H_ */
