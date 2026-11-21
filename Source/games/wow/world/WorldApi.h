/*
 * WorldApi.h
 *
 * Export macro of the world-format parsers (Source/games/wow/world, namespace wmv::world), which are built into
 * the wow shared library. Same pattern as the rest of Source/games/wow.
 */

#ifndef _WMV_WORLD_API_H_
#define _WMV_WORLD_API_H_

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WMVWORLD_API_ __declspec(dllexport)
#    else
#        define _WMVWORLD_API_ __declspec(dllimport)
#    endif
#else
#    define _WMVWORLD_API_
#endif

#endif
