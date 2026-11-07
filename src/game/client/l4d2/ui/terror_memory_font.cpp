//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Let the pinned native surface consume genuine, process-private
//          original memory fonts. No system font installation or disk copy.
//
//=============================================================================//

#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include "cbase.h"
#include "l4d2/ui/terror_memory_font.h"
#include "tier1/utlstring.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{

typedef int (WINAPI *EnumFontFamiliesExA_t)( HDC, LPLOGFONTA, FONTENUMPROCA, LPARAM, DWORD );

TerrorMemoryFonts_t *g_pFontState;
EnumFontFamiliesExA_t g_pEnumFontFamiliesExA;
void **g_pEnumImport;
const void *g_pFontEnumReturn;
FONTENUMPROCA g_pFontEnumCallback;

unsigned int ReadFontU16( const unsigned char *p )
{
	return ( static_cast<unsigned int>( p[0] ) << 8 ) | p[1];
}

unsigned int ReadFontU32( const unsigned char *p )
{
	return ( static_cast<unsigned int>( p[0] ) << 24 ) |
		( static_cast<unsigned int>( p[1] ) << 16 ) |
		( static_cast<unsigned int>( p[2] ) << 8 ) | p[3];
}

bool FontSpanValid( unsigned int offset, unsigned int count, unsigned int length )
{
	return offset <= length && count <= length - offset;
}

bool CollectFontFaces( const unsigned char *pData, unsigned int nBytes, CUtlVector<TerrorFontFace_t> &faces )
{
	if ( nBytes < 12 || ReadFontU32( pData ) != 0x00010000 )
		return false;

	const unsigned int nTables = ReadFontU16( pData + 4 );
	if ( !FontSpanValid( 12, nTables * 16, nBytes ) )
		return false;
	const unsigned char *pHead = NULL;
	const unsigned char *pNames = NULL;
	unsigned int nNameBytes = 0;
	for ( unsigned int i = 0; i < nTables; ++i )
	{
		const unsigned char *pTable = pData + 12 + 16 * i;
		const unsigned int offset = ReadFontU32( pTable + 8 );
		const unsigned int length = ReadFontU32( pTable + 12 );
		if ( !FontSpanValid( offset, length, nBytes ) )
			return false;
		if ( !memcmp( pTable, "head", 4 ) && length >= 54 )
			pHead = pData + offset;
		if ( !memcmp( pTable, "name", 4 ) )
		{
			pNames = pData + offset;
			nNameBytes = length;
		}
	}
	if ( !pHead || !pNames || nNameBytes < 6 )
		return false;

	const unsigned int nNames = ReadFontU16( pNames + 2 );
	const unsigned int nStrings = ReadFontU16( pNames + 4 );
	if ( !FontSpanValid( 6, nNames * 12, nNameBytes ) || nStrings > nNameBytes )
		return false;
	for ( unsigned int i = 0; i < nNames; ++i )
	{
		const unsigned char *pName = pNames + 6 + i * 12;
		const unsigned int platform = ReadFontU16( pName );
		const unsigned int length = ReadFontU16( pName + 8 );
		const unsigned int offset = ReadFontU16( pName + 10 );
		// Keep both genuine SFNT family and full-face names; the selected face
		// and original head bytes still pass through the same exact bridge.
		const unsigned int nameID = ReadFontU16( pName + 6 );
		if ( nameID != 1 && nameID != 4 )
			continue;
		if ( !FontSpanValid( offset, length, nNameBytes - nStrings ) )
			return false;
		const unsigned char *pString = pNames + nStrings + offset;
		char name[LF_FACESIZE];
		unsigned int nChars = length;
		if ( platform == 0 || platform == 3 )
		{
			if ( length & 1 )
				return false;
			nChars /= 2;
			if ( !nChars || nChars >= ARRAYSIZE( name ) )
				continue;
			wchar_t wide[LF_FACESIZE];
			for ( unsigned int j = 0; j < nChars; ++j )
				wide[j] = static_cast<wchar_t>( ReadFontU16( pString + j * 2 ) );
			wide[nChars] = L'\0';
			if ( !WideCharToMultiByte( CP_ACP, WC_NO_BEST_FIT_CHARS, wide, -1, name, sizeof( name ), NULL, NULL ) )
				continue;
		}
		else if ( platform == 1 && ReadFontU16( pName + 2 ) == 0 )
		{
			if ( !nChars || nChars >= ARRAYSIZE( name ) )
				continue;
			// Reached Macintosh family records contain ASCII only.
			bool bAscii = true;
			for ( unsigned int j = 0; j < nChars; ++j )
			{
				if ( pString[j] >= 0x80 )
					bAscii = false;
				name[j] = pString[j];
			}
			if ( !bAscii )
				continue;
			name[nChars] = '\0';
		}
		else
			continue;

		bool bExists = false;
		FOR_EACH_VEC( faces, j )
		{
			if ( !Q_stricmp( faces[j].name.Get(), name ) )
				bExists = true;
		}
		if ( !bExists )
		{
			TerrorFontFace_t face;
			face.name = name;
			memcpy( face.head, pHead, sizeof( face.head ) );
			face.bVerified = false;
			faces.AddToTail( face );
		}
	}
	return faces.Count() != 0;
}

bool IsPinnedSurfaceFile( HMODULE module )
{
	wchar_t filename[MAX_PATH];
	const DWORD nChars = GetModuleFileNameW( module, filename, ARRAYSIZE( filename ) );
	if ( !nChars || nChars >= ARRAYSIZE( filename ) )
	{
		Warning( "HUD font runtime adaptation rejected vguimatsurface.dll: unable to resolve module path (Win32 %lu)\n", GetLastError() );
		return false;
	}
	HANDLE file = CreateFileW( filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( file == INVALID_HANDLE_VALUE )
	{
		Warning( "HUD font runtime adaptation rejected %ls: unable to read module (Win32 %lu)\n", filename, GetLastError() );
		return false;
	}

	BCRYPT_ALG_HANDLE algorithm = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	CUtlVector<unsigned char> object;
	unsigned char digest[32];
	bool bValid = false;
	do
	{
		if ( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0 ) < 0 )
			break;
		DWORD nObjectBytes = 0;
		DWORD nResult = 0;
		if ( BCryptGetProperty( algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>( &nObjectBytes ),
			sizeof( nObjectBytes ), &nResult, 0 ) < 0 )
			break;
		object.SetCount( nObjectBytes );
		if ( BCryptCreateHash( algorithm, &hash, object.Base(), nObjectBytes, NULL, 0, 0 ) < 0 )
			break;
		unsigned char buffer[16384];
		bool bRead = true;
		for ( ;; )
		{
			DWORD nRead = 0;
			if ( !ReadFile( file, buffer, sizeof( buffer ), &nRead, NULL ) )
			{
				bRead = false;
				break;
			}
			if ( !nRead )
				break;
			if ( BCryptHashData( hash, buffer, nRead, 0 ) < 0 )
			{
				bRead = false;
				break;
			}
		}
		if ( !bRead || BCryptFinishHash( hash, digest, sizeof( digest ), 0 ) < 0 )
			break;
		// Target vguimatsurface.dll, SHA256 a06ccdab...3a08b70.
		static const unsigned char expected[] =
		{
			0xA0,0x6C,0xCD,0xAB,0xEC,0xAD,0x54,0x50,0x7E,0xFF,0x84,0xA2,0x0B,0x44,0xB4,0x84,
			0xFC,0x51,0xFD,0xD8,0xDD,0x05,0x29,0x32,0x43,0x07,0x58,0xE7,0x93,0xA0,0x8B,0x70
		};
		bValid = !memcmp( digest, expected, sizeof( expected ) );
		char actual[65];
		for ( int i = 0; i < ARRAYSIZE( digest ); ++i )
			Q_snprintf( actual + i * 2, sizeof( actual ) - i * 2, "%02x", digest[i] );
		if ( !bValid )
			Warning( "HUD font runtime adaptation rejected %ls: expected SHA256 a06ccdabecad54507eff84a20b44b484fc51fdd8dd052932430758e793a08b70, actual %s\n", filename, actual );
	} while ( false );
	if ( hash )
		BCryptDestroyHash( hash );
	if ( algorithm )
		BCryptCloseAlgorithmProvider( algorithm, 0 );
	CloseHandle( file );
	return bValid;
}

__declspec(noinline) int WINAPI TerrorEnumFontFamiliesExA( HDC dc, LPLOGFONTA request,
	FONTENUMPROCA callback, LPARAM parameter, DWORD flags )
{
	if ( _ReturnAddress() != g_pFontEnumReturn || callback != g_pFontEnumCallback || !request )
		return g_pEnumFontFamiliesExA( dc, request, callback, parameter, flags );

	TerrorFontFace_t *pFace = NULL;
	FOR_EACH_VEC( g_pFontState->faces, i )
	{
		if ( !Q_stricmp( g_pFontState->faces[i].name.Get(), request->lfFaceName ) )
		{
			pFace = &g_pFontState->faces[i];
			break;
		}
	}
	if ( !pFace )
		return g_pEnumFontFamiliesExA( dc, request, callback, parameter, flags );

	// AddFontMemResourceEx fonts are explicitly not enumerable. The original
	// glyph creator directly called CreateFontA; this target adds an enum gate.
	// Bridge only that gate with the actual GDI-selected original face/metadata.
	LOGFONTA query = {};
	query.lfCharSet = request->lfCharSet;
	Q_strncpy( query.lfFaceName, request->lfFaceName, sizeof( query.lfFaceName ) );
	HFONT font = CreateFontIndirectA( &query );
	if ( !font )
	{
		Warning( "Unable to create original HUD face %s\n", request->lfFaceName );
		return 0;
	}
	HGDIOBJ previous = SelectObject( dc, font );
	bool bSelected = previous && previous != HGDI_ERROR;
	char actual[LF_FACESIZE];
	unsigned char head[54];
	TEXTMETRICA metrics;
	bool bValid = bSelected && GetTextFaceA( dc, sizeof( actual ), actual ) &&
		!Q_stricmp( actual, request->lfFaceName ) &&
		GetFontData( dc, 0x64616568, 0, head, sizeof( head ) ) == sizeof( head ) &&
		!memcmp( head, pFace->head, sizeof( head ) ) && GetTextMetricsA( dc, &metrics );
	LOGFONTA selected;
	if ( bValid )
		bValid = GetObjectA( font, sizeof( selected ), &selected ) == sizeof( selected );
	if ( bSelected )
		SelectObject( dc, previous );
	DeleteObject( font );
	if ( !bValid )
	{
		Warning( "Native surface did not select the original HUD font bytes for %s\n", request->lfFaceName );
		return 0;
	}

	if ( !pFace->bVerified )
	{
		DevMsg( "HUD memory font verified: %s (native GDI face and 54-byte SFNT head match)\n", request->lfFaceName );
		pFace->bVerified = true;
	}
	return callback( &selected, &metrics, TRUETYPE_FONTTYPE, parameter );
}

} // namespace

bool InstallTerrorMemoryFontBridge( TerrorMemoryFonts_t &fonts )
{
	if ( g_pEnumImport )
	{
		return g_pFontState == &fonts;
	}
	HMODULE module = GetModuleHandleW( L"vguimatsurface.dll" );
	if ( !module )
	{
		Warning( "HUD font runtime adaptation rejected: vguimatsurface.dll is not loaded\n" );
		return false;
	}
	if ( !IsPinnedSurfaceFile( module ) )
	{
		Warning( "Original HUD font preparation stopped: pinned surface identity was not accepted\n" );
		return false;
	}
	unsigned char *base = reinterpret_cast<unsigned char *>( module );
	const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>( base );
	if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
		return false;
	const IMAGE_NT_HEADERS64 *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>( base + dos->e_lfanew );
	if ( nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
		nt->FileHeader.TimeDateStamp != 0x67B40F34 || nt->OptionalHeader.SizeOfImage != 0x16B000 )
		return false;
	static const unsigned char gate[] =
	{
		0xFF,0x15,0xA4,0xF3,0x0A,0x00,0x44,0x38,0x2D,0x7E,0xAF,0x10,0x00,0x75,0x0D,
		0x66,0xC7,0x43,0x28,0xFF,0xFF,0x32,0xC0,0xE9,0x95,0x01,0x00,0x00
	};
	static const unsigned char callbackCode[] = { 0xC6,0x05,0xA2,0xAD,0x10,0x00,0x01,0x33,0xC0,0xC3 };
	if ( memcmp( base + 0x17CBE, gate, sizeof( gate ) ) ||
		memcmp( base + 0x17EA0, callbackCode, sizeof( callbackCode ) ) )
		return false;

	void **pImport = NULL;
	const IMAGE_IMPORT_DESCRIPTOR *descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(
		base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress );
	for ( ; descriptor->Name; ++descriptor )
	{
		if ( Q_stricmp( reinterpret_cast<const char *>( base + descriptor->Name ), "GDI32.dll" ) )
			continue;
		if ( !descriptor->OriginalFirstThunk )
			return false;
		const IMAGE_THUNK_DATA64 *name = reinterpret_cast<const IMAGE_THUNK_DATA64 *>( base + descriptor->OriginalFirstThunk );
		IMAGE_THUNK_DATA64 *address = reinterpret_cast<IMAGE_THUNK_DATA64 *>( base + descriptor->FirstThunk );
		for ( ; name->u1.AddressOfData; ++name, ++address )
		{
			if ( IMAGE_SNAP_BY_ORDINAL64( name->u1.Ordinal ) )
				continue;
			const IMAGE_IMPORT_BY_NAME *entry = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>( base + name->u1.AddressOfData );
			if ( !Q_strcmp( reinterpret_cast<const char *>( entry->Name ), "EnumFontFamiliesExA" ) )
				pImport = reinterpret_cast<void **>( &address->u1.Function );
		}
	}
	if ( pImport != reinterpret_cast<void **>( base + 0xC7068 ) )
		return false;
	EnumFontFamiliesExA_t original = reinterpret_cast<EnumFontFamiliesExA_t>(
		GetProcAddress( GetModuleHandleW( L"gdi32.dll" ), "EnumFontFamiliesExA" ) );
	if ( !original || *pImport != reinterpret_cast<void *>( original ) )
		return false;

	DWORD protection = 0;
	if ( !VirtualProtect( pImport, sizeof( *pImport ), PAGE_READWRITE, &protection ) )
		return false;
	g_pEnumFontFamiliesExA = original;
	g_pFontEnumReturn = base + 0x17CC4;
	g_pFontEnumCallback = reinterpret_cast<FONTENUMPROCA>( base + 0x17EA0 );
	g_pFontState = &fonts;
	void *previous = InterlockedCompareExchangePointer( pImport,
		reinterpret_cast<void *>( TerrorEnumFontFamiliesExA ), reinterpret_cast<void *>( original ) );
	DWORD ignored;
	const bool bProtected = VirtualProtect( pImport, sizeof( *pImport ), protection, &ignored ) != FALSE;
	if ( previous != reinterpret_cast<void *>( original ) || !bProtected )
	{
		if ( previous == reinterpret_cast<void *>( original ) )
			InterlockedCompareExchangePointer( pImport, reinterpret_cast<void *>( original ),
				reinterpret_cast<void *>( TerrorEnumFontFamiliesExA ) );
		g_pFontState = NULL;
		Warning( "Unable to install original HUD memory-font acceptance bridge\n" );
		return false;
	}
	g_pEnumImport = pImport;
	Msg( "HUD font runtime adaptation installed: SHA256-validated vguimatsurface.dll EnumFontFamiliesExA IAT 0xC7068\n" );
	return true;
}

bool RegisterTerrorMemoryFont( TerrorMemoryFonts_t &fonts, const char *pFilename,
	void *pFontData, int nFontBytes )
{
	FOR_EACH_VEC( fonts.handles, i )
	{
		if ( !Q_stricmp( fonts.handles[i].filename.Get(), pFilename ) )
		{
			return true;
		}
	}
	CUtlVector<TerrorFontFace_t> faces;
	if ( nFontBytes <= 0 || !CollectFontFaces( static_cast<const unsigned char *>( pFontData ), nFontBytes, faces ) )
	{
		Warning( "Invalid original HUD SFNT face metadata in %s\n", pFilename );
		return false;
	}
	DWORD nFonts = 0;
	HANDLE handle = AddFontMemResourceEx( pFontData, nFontBytes, NULL, &nFonts );
	if ( !handle )
	{
		Warning( "Unable to register original HUD font %s\n", pFilename );
		return false;
	}
	TerrorFontHandle_t font;
	font.filename = pFilename;
	font.handle = handle;
	fonts.handles.AddToTail( font );
	FOR_EACH_VEC( faces, i )
	{
		fonts.faces.AddToTail( faces[i] );
	}
	return true;
}

void ShutdownTerrorMemoryFonts( TerrorMemoryFonts_t &fonts )
{
	if ( g_pEnumImport )
	{
		DWORD protection = 0;
		if ( !VirtualProtect( g_pEnumImport, sizeof( *g_pEnumImport ), PAGE_READWRITE, &protection ) )
			Error( "Unable to restore native HUD font import\n" );
		void *previous = InterlockedCompareExchangePointer( g_pEnumImport,
			reinterpret_cast<void *>( g_pEnumFontFamiliesExA ), reinterpret_cast<void *>( TerrorEnumFontFamiliesExA ) );
		DWORD ignored;
		if ( !VirtualProtect( g_pEnumImport, sizeof( *g_pEnumImport ), protection, &ignored ) ||
			previous != reinterpret_cast<void *>( TerrorEnumFontFamiliesExA ) )
			Error( "Native HUD font import changed while original memory fonts were active\n" );
		g_pEnumImport = NULL;
		Msg( "HUD font runtime adaptation restored: native EnumFontFamiliesExA IAT\n" );
	}
	g_pEnumFontFamiliesExA = NULL;
	g_pFontEnumReturn = NULL;
	g_pFontEnumCallback = NULL;
	g_pFontState = NULL;
	for ( int i = fonts.handles.Count() - 1; i >= 0; --i )
	{
		RemoveFontMemResourceEx( fonts.handles[i].handle );
	}
	fonts.handles.Purge();
	fonts.faces.Purge();
}
