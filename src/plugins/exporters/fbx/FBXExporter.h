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
 * FBXExporter.h
 *
 *  Created on: 13 june 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _FBXEXPORTER_H_
#define _FBXEXPORTER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <map>
#include <string>

// Qt
#include <QObject>
#include <QtPlugin>

// Externals
#include "fbxsdk.h"

// Other libraries
class WoWModel;


#define _EXPORTERPLUGIN_CPP_ // to define interface
#include "ExporterPlugin.h"
#undef _EXPORTERPLUGIN_CPP_

// Current library


// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class FBXExporter : public QObject, public ExporterPlugin
{
    Q_INTERFACES(ExporterPlugin)
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "wowmodelviewer.exporters.FBXExporter" FILE "fbxexporter.json")

  public :
    // Constants / Enums

    // Constructors
    FBXExporter();

    // Destructors
    ~FBXExporter() {}

    // Methods
   std::string menuLabel() const;

   std::string fileSaveTitle() const;
   std::string fileSaveFilter() const;

   bool exportModel(Model *, std::string file);

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
    void createMaterials();
    void createMesh();
    void createSkeleton();
    void createAnimations();
    void reset();


    // Members
    FbxManager * m_p_manager;
    FbxScene   * m_p_scene;
    WoWModel   * m_p_model;
    FbxNode    * m_p_meshNode;

    std::string m_filename;
    std::map<int,FbxNode*> m_boneNodes;

    std::map<std::string, GLuint> m_texturesToExport;

    // friend class declarations

};

// static members definition
#ifdef _FBXEXPORTER_CPP_

#endif

#endif /* _FBXEXPORTER_H_ */
