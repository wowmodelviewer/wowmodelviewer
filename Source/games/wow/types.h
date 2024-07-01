#pragma once

// STL headers
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#if defined(_WINDOWS)
typedef __int8 int8;
typedef __int16 int16;
typedef __int32 int32;
typedef __int64 int64;
typedef unsigned __int8 uint8;
typedef unsigned __int16 uint16;
typedef unsigned __int32 uint32;
typedef unsigned __int64 uint64;
typedef int ssize_t;
#else
  #include <stdint.h>
  typedef uint8_t uint8;
  typedef int8_t int8;
  typedef uint16_t uint16;
  typedef int16_t int16;
  typedef uint32_t uint32;
  typedef int32_t int32;
#endif
