//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_MEMZEROONNEW_H
#define L4D2_MEMZEROONNEW_H
#ifdef _WIN32
#pragma once
#endif

// Operator declarations must not be rewritten by the debug-new macro. Restore
// the caller's allocator-macro state after declaring the genuine native base.
#ifdef MEM_OVERRIDE_ON
#define L4D2_MEMZEROONNEW_RESTORE_MEMDBG
#include "tier0/memdbgoff.h"
#endif

#include "tier0/memalloc.h"
#include <string.h>

// Native CMemZeroOnNew: allocation is cleared BEFORE any base/member ctor runs.
// In particular, an unwritten scalar is not a fabricated constructor store.
class CMemZeroOnNew
{
public:
	void *operator new( size_t size )
	{
		void *memory = g_pMemAlloc->Alloc( size );
		memset( memory, 0, size );
		return memory;
	}

	void *operator new( size_t size, int blockUse, const char *file, int line )
	{
		void *memory = g_pMemAlloc->Alloc( size, file, line );
		memset( memory, 0, size );
		return memory;
	}

	void operator delete( void *memory )
	{
		g_pMemAlloc->Free( memory );
	}

	void operator delete( void *memory, int blockUse, const char *file, int line )
	{
		operator delete( memory );
	}
};

#ifdef L4D2_MEMZEROONNEW_RESTORE_MEMDBG
#undef L4D2_MEMZEROONNEW_RESTORE_MEMDBG
#include "tier0/memdbgon.h"
#endif

#endif // L4D2_MEMZEROONNEW_H
