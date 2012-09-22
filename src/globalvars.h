#ifndef GLOBALVARS_H
#define GLOBALVARS_H

#include "modelviewer.h"
#include "modelcanvas.h"
#include "animcontrol.h"
#include "charcontrol.h"
#include "filecontrol.h"

extern ModelViewer *g_modelViewer;
extern ModelCanvas *g_canvas;
extern AnimControl *g_animControl;
extern CharControl *g_charControl;
extern FileControl *g_fileControl;

extern Model *g_selModel;
extern WMO *g_selWMO;

#endif

