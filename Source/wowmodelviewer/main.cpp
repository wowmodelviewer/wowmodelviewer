#include "app.h"

#include <cstdlib>
#include <string>
#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <sys/stat.h>
#endif

// tell wxwidgets which class is our app
IMPLEMENT_APP_NO_MAIN(WowModelViewApp)

int main(int argc, char *argv[])
{
#if defined(__WXGTK__)
  // The Vulkan viewport draws into an X11 window: under Wayland the viewer runs through XWayland (unless asked
  // otherwise with GDK_BACKEND).
  setenv("GDK_BACKEND", "x11", 0);
#endif
#if defined(__APPLE__)
  // The app bundle carries its own MoltenVK: use only that driver (not another one installed on the Mac, e.g. by
  // Homebrew, which would load a second MoltenVK), unless the user chose drivers themselves.
  if (!getenv("VK_DRIVER_FILES") && !getenv("VK_ICD_FILENAMES"))
  {
    char exe[4096];
    uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) == 0)
    {
      std::string icd(exe);
      icd = icd.substr(0, icd.rfind('/')) + "/../Resources/vulkan/icd.d/MoltenVK_icd.json";
      struct stat st;
      if (stat(icd.c_str(), &st) == 0)
        setenv("VK_DRIVER_FILES", icd.c_str(), 1);
    }
  }
#endif
  return wxEntry(argc, argv);   //Activates the wxApp version of WMV.
}