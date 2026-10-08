//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_BYTE_BUFFER_H
#define RESTIR_BYTE_BUFFER_H
#pragma once
#include "tier1/utlvector.h"
#include <limits.h>

// CUtlMemory's signed geometric growth loops forever after doubling 1 GiB.
// Keep byte capacities within the signed BSP/pak limit, before SetCount/Grow.
inline bool ReSTIR_ByteCapacity( int allocated, uint64 required, int &capacity )
{
	if ( allocated < 0 || required > INT_MAX ) return false;
	const uint64 doubled = uint64( allocated ) * 2;
	const uint64 grown = doubled > uint64( INT_MAX ) ? uint64( INT_MAX ) : doubled;
	capacity = int( required > uint64( allocated ) ? ( required > grown ? required : grown ) : uint64( allocated ) );
	return true;
}
inline bool ReSTIR_EnsureByteCapacity( CUtlVector<byte> &buffer, uint64 required )
{
	int capacity;
	if ( !ReSTIR_ByteCapacity( buffer.NumAllocated(), required, capacity ) ) return false;
	buffer.EnsureCapacity( capacity ); // Exact allocation; never CUtlMemory::Grow.
	return true;
}
inline bool ReSTIR_AddByteSectionSize( uint64 &size, uint64 bytes )
{
	const uint64 aligned = ( size + 15 ) & ~uint64( 15 );
	if ( aligned > INT_MAX || bytes > uint64( INT_MAX ) - aligned ) return false;
	size = aligned + bytes;
	return true;
}
#endif
