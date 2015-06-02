#ifndef GLOBALVARS_H
#define GLOBALVARS_H

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _GLOBALVARS_API_ __declspec(dllexport)
#    else
#        define _GLOBALVARS_API_ __declspec(dllimport)
#    endif
#else
#    define _GLOBALVARS_API_
#endif

class AnimControl;
class CharControl;
class FileControl;
class WoWModel;
class ModelCanvas;
class ModelViewer;
class WMO;

_GLOBALVARS_API_ extern AnimControl *g_animControl;
_GLOBALVARS_API_ extern CharControl *g_charControl;
_GLOBALVARS_API_ extern FileControl *g_fileControl;
_GLOBALVARS_API_ extern WoWModel *g_selModel;
_GLOBALVARS_API_ extern ModelCanvas *g_canvas;
_GLOBALVARS_API_ extern ModelViewer *g_modelViewer;
_GLOBALVARS_API_ extern WMO *g_selWMO;

#endif

