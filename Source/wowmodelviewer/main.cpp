#include "app.h"

#include <cstdlib>

// tell wxwidgets which class is our app
IMPLEMENT_APP_NO_MAIN(WowModelViewApp)

int main(int argc, char *argv[])
{
#if defined(__WXGTK__)
  // The Vulkan viewport draws into an X11 window: under Wayland the viewer runs through XWayland (unless asked
  // otherwise with GDK_BACKEND).
  setenv("GDK_BACKEND", "x11", 0);
#endif
  return wxEntry(argc, argv);   //Activates the wxApp version of WMV.
}