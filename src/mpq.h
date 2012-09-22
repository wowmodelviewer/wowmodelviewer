
// libmpq based implementation by :wumpus:
// StormLib library
#if defined (_WINDOWS)
#include "mpq_stormlib.h"
#else
	#if defined (USE_STORM)
	#include "mpq_stormlib.h"
	#else
	#include "mpq_libmpq.h"
	#endif
#endif
