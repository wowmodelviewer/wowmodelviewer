/*
 * types.h
 *
 *  Created on: 7 Aug. 2015
 *      Author: Jeromnimo
 */

#ifndef _TYPES_H_
#define _TYPES_H_

// STL headers
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif


#if defined(_WINDOWS)
  #include <windows.h>
	#define snprintf _snprintf
	typedef unsigned char uint8;
	typedef char int8;
	typedef unsigned __int16 uint16;
	typedef __int16 int16;
	typedef unsigned __int32 uint32;
	typedef __int32 int32;
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


#endif /* _TYPES_H_ */
