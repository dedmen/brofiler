#pragma once

#include "Platform.h"
#include "Types.h"

#if BRO_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <stdint.h>

#include "Brofiler.h"


// MEMORY //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define BRO_CACHE_LINE_SIZE 64
#ifndef __linux__
#define BRO_ALIGN(N) __declspec( align( N ) )
#define BRO_ALIGN_CACHE BRO_ALIGN(BRO_CACHE_LINE_SIZE)
#else
#define BRO_ALIGN(N)
#define BRO_ALIGN_CACHE BRO_ALIGN(BRO_CACHE_LINE_SIZE)
#endif
#define BRO_ARRAY_SIZE(ARR) (sizeof(ARR)/sizeof((arr)[0]))
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define BRO_UNUSED(x) (void)(x)


#if BRO_PLATFORM_WINDOWS
#define IS_DEBUG_PRESENT ::IsDebuggerPresent() == TRUE
#endif

#define BRO_DEBUG_BREAK if (IS_DEBUG_PRESENT) { __debugbreak(); }

#ifdef _DEBUG
	#define BRO_ASSERT(arg, description) if (!(arg)) { BRO_DEBUG_BREAK; }
	#define BRO_VERIFY(arg, description, operation) if (!(arg)) { BRO_DEBUG_BREAK; operation; }
	#define BRO_FAILED(description) { BRO_DEBUG_BREAK; }
#else
	#define BRO_ASSERT(arg, description)
	#define BRO_VERIFY(arg, description, operation)
	#define BRO_FAILED(description)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// OVERRIDE keyword warning fix ////////////////////////////////////////////////////////////////////////////////////////////////
#if _MSC_VER >= 1600 // >= VS 2010 (VC10)
	#pragma warning (disable: 4481) //http://msdn.microsoft.com/en-us/library/ms173703.aspx
#else
	#define override
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
