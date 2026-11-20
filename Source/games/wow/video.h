#ifndef VIDEO_H
#define VIDEO_H

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _VIDEO_API_ __declspec(dllexport)
#    else
#        define _VIDEO_API_ __declspec(dllimport)
#    endif
#else
#    define _VIDEO_API_
#endif

// The viewport's view settings that the model code reads. (It used to hold the OpenGL context and its
// capabilities as well; the viewport is Vulkan now and keeps those itself.)
class _VIDEO_API_ VideoSettings
{
public:
  VideoSettings();

  bool render;        // the viewport draws (false while a load replaces what it shows)
  bool useMasking;    // draw silhouettes only: no textures, bounds or bones (image masks)
  bool useEnvMapping; // sphere-mapped environment reflections
  int xRes, yRes;     // the viewport's size in pixels
  float fov;          // vertical field of view, degrees
};

_VIDEO_API_ extern VideoSettings video;

#endif
