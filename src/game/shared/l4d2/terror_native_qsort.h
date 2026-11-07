//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_NATIVE_QSORT_H
#define TERROR_NATIVE_QSORT_H
// Sole shared native CRT kernel: C47FB40/C47FAB0 == S55E490/S55E400.
// M6 evidence: only five E8 relocation displacements differ in qsort; shortsort
// is byte-identical. Index cursors avoid constructing before-array pointers.
inline void TerrorNativeSwap( unsigned char *a, unsigned char *b, int size )
{
	if ( a == b ) return;
	for ( int i = 0; i < size; ++i ) { unsigned char byte = a[i]; a[i] = b[i]; b[i] = byte; }
}
inline void TerrorNativeQSort( void *base, int count, int size, int ( *compare )( const void *, const void * ) )
{
	if ( count < 2 ) return;
	unsigned char *bytes = static_cast< unsigned char * >( base );
	int lowStack[30], highStack[30], stack = 0;
	int low = 0, high = count - 1;
	for ( ;; )
	{
		const int span = high - low + 1;
		if ( span <= 8 )
		{
			for ( int end = high; end > low; --end )
			{
				int maximum = low;
				for ( int item = low + 1; item <= end; ++item )
					if ( compare( bytes + item * size, bytes + maximum * size ) > 0 ) maximum = item;
				TerrorNativeSwap( bytes + maximum * size, bytes + end * size, size );
			}
		}
		else
		{
			int middle = low + span / 2;
			if ( compare( bytes + low * size, bytes + middle * size ) > 0 ) TerrorNativeSwap( bytes + low * size, bytes + middle * size, size );
			if ( compare( bytes + low * size, bytes + high * size ) > 0 ) TerrorNativeSwap( bytes + low * size, bytes + high * size, size );
			if ( compare( bytes + middle * size, bytes + high * size ) > 0 ) TerrorNativeSwap( bytes + middle * size, bytes + high * size, size );
			int left = low, right = high;
			for ( ;; )
			{
				if ( middle > left ) do { ++left; } while ( left < middle && compare( bytes + left * size, bytes + middle * size ) <= 0 );
				if ( middle <= left ) do { ++left; } while ( left <= high && compare( bytes + left * size, bytes + middle * size ) <= 0 );
				do { --right; } while ( right > middle && compare( bytes + right * size, bytes + middle * size ) > 0 );
				if ( left > right ) break;
				TerrorNativeSwap( bytes + left * size, bytes + right * size, size );
				if ( middle == right ) middle = left;
			}
			++right;
			if ( middle < right ) do { --right; } while ( right > middle && compare( bytes + right * size, bytes + middle * size ) == 0 );
			if ( middle >= right ) do { --right; } while ( right > low && compare( bytes + right * size, bytes + middle * size ) == 0 );
			if ( right - low < high - left )
			{
				if ( left < high ) { lowStack[stack] = left; highStack[stack++] = high; }
				if ( low < right ) { high = right; continue; }
			}
			else
			{
				if ( low < right ) { lowStack[stack] = low; highStack[stack++] = right; }
				if ( left < high ) { low = left; continue; }
			}
		}
		if ( --stack < 0 ) return;
		low = lowStack[stack]; high = highStack[stack];
	}
}
#endif
