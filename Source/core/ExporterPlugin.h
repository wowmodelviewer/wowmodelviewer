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
 * ImporterPlugin.h
 *
 *  Created on: 17 Feb. 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _EXPORTERPLUGIN_H_
#define _EXPORTERPLUGIN_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <string>
#include <vector>

// Qt

// Externals
#include "GL/glew.h"

// Other libraries

// Current library
class Model;
#include "Plugin.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _EXPORTERPLUGIN_API_ __declspec(dllexport)
#    else
#        define _EXPORTERPLUGIN_API_ __declspec(dllimport)
#    endif
#else
#    define _EXPORTERPLUGIN_API_
#endif

class _EXPORTERPLUGIN_API_ ExporterPlugin : public Plugin
{
  public :
    // Constants / Enums

    // Constructors
    ExporterPlugin() : m_canExportAnimation(false) {}

    // Destructors

    // Methods
    virtual std::wstring menuLabel() const = 0;
    virtual std::wstring fileSaveTitle() const = 0;
    virtual std::wstring fileSaveFilter() const = 0;

    virtual bool exportModel(Model *, std::wstring file) = 0;

    bool canExportAnimation() const { return m_canExportAnimation; }

    void setAnimationsToExport(std::vector<int> values) { m_animsToExport = values; }

    // Content selection for the export. Defaults are all-on so exporters that ignore these
    // (e.g. OBJ) and existing call sites keep their previous always-on behaviour. An exporter
    // is responsible for enforcing dependencies (skinning needs mesh+skeleton; animation needs
    // a skeleton) and for failing gracefully with lastError() set.
    void setExportOptions(bool mesh, bool skeleton, bool skinning, bool animation)
    {
      m_exportMesh = mesh;
      m_exportSkeleton = skeleton;
      m_exportSkinning = skinning;
      m_exportAnimation = animation;
    }

    // Opt-in "component / raw node-based (Blender)" export: emit a second UV set (UV2), the raw
    // per-unit textures (no combiner bake), and an expanded "version 2" material sidecar so the
    // Blender add-on can rebuild the item-component node graph (UV1/UV2 selection, alpha-as-spec vs
    // transparency, glow emission, animated UV scroll). Default off -> the legacy bake pipeline,
    // whose output stays byte-for-byte unchanged.
    void setComponentRawExport(bool v) { m_exportComponentRaw = v; }

    // Human-readable reason the most recent exportModel() returned false (empty if unknown).
    // Lets the UI surface an actionable message instead of a generic "export failed".
    const std::wstring & lastError() const { return m_lastError; }

    // Read-only forensic diagnostic: re-import an existing exported file and log per-mesh detail
    // (vertex count, weighted-vertex count, influencing bones, skin cluster count, parent node,
    // bind-pose). Default no-op; the FBX exporter overrides it. Drives the -fbxinspect harness.
    virtual void dumpForensics(const std::wstring & /*file*/) const {}

    // Read-only forensic diagnostic: pose the SOURCE skeleton for one named animation at
    // frame 0/mid/final and compare each bone's world position against the EXPORTED animation
    // evaluated at the same times, logging where they diverge. Default no-op; FBX overrides.
    // Drives the -animdump harness. (Takes a non-const model because posing mutates bone state.)
    virtual void dumpSourcePose(Model * /*model*/, const std::wstring & /*animName*/) const {}

    // Members

  protected :
    // Constants / Enums

    // Constructors

    // Destructors

    // How an additive pass's pixels are rewritten at save time so the FILE's RGB holds the
    // pass's true per-plane on-screen contribution (what GL adds to the framebuffer):
    //   PremultiplyAlpha : contribution = rgb * srcAlpha   (GL_SRC_ALPHA, GL_ONE -- blend 4)
    //   SquareColor      : contribution = rgb * rgb        (GL_SRC_COLOR, GL_ONE -- blend 3)
    // A DCC then renders the pass as pure emission over transparency and, because the mesh
    // contains the same stacked glow planes the game draws, accumulates them identically --
    // no artificial brightness boost needed or wanted.
    enum class AdditiveTransform { None, PremultiplyAlpha, SquareColor };

    // Methods
    // tintRGB, when non-null, is an {r,g,b} multiplier baked into the pixels before saving (see
    // FBXExporter::createMaterials() -- reproduces a WoW M2 "colors" track recolor that has no
    // FBX material property DCCs apply consistently, so the exported texture file already IS the
    // tinted result the viewport shows). Alpha is left untouched unless alphaFromLuminance is set:
    // then alpha is rewritten to max(r,g,b) per pixel (AFTER the additive transform) -- so a DCC
    // without our add-on can still treat black as transparent via the file's own alpha.
    void exportGLTexture(GLuint id, std::wstring filename, const float * tintRGB = nullptr,
                         bool alphaFromLuminance = false,
                         AdditiveTransform transform = AdditiveTransform::None) const;

    // Saves an already-in-memory RGBA pixel buffer (GL_BGRA_EXT byte order) instead of reading one
    // back from a live GL texture -- used for a texture computed on the fly (see FBXExporter's
    // multi-texture-combiner bake) rather than sampled directly from the GPU. Shares the same
    // optional tint-multiply + save logic as exportGLTexture().
    void saveRGBABuffer(unsigned char * pixelsBGRA, int width, int height, std::wstring filename,
                        const float * tintRGB = nullptr, bool alphaFromLuminance = false,
                        AdditiveTransform transform = AdditiveTransform::None) const;

    // Members
    bool m_canExportAnimation;
    std::vector<int> m_animsToExport;

    // Export content toggles (see setExportOptions). All-on by default for backward compat.
    bool m_exportMesh = true;
    bool m_exportSkeleton = true;
    bool m_exportSkinning = true;
    bool m_exportAnimation = true;

    // Opt-in raw/node-based item-component export (see setComponentRawExport). Off -> legacy bake.
    bool m_exportComponentRaw = false;

    // Set by exporters when exportModel() fails, read by the UI via lastError().
    std::wstring m_lastError;

  private :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods

    // Members

    // friend class declarations

};

// static members definition
#ifdef _EXPORTERPLUGIN_CPP_
Q_DECLARE_INTERFACE(ExporterPlugin,"wowmodelviewer.exporterplugin/1.0");

#endif

#endif /* _EXPORTERPLUGIN_H_ */
