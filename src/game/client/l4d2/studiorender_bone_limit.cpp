#include <windows.h>
#include "cbase.h"
#include "l4d2/studiorender_bone_limit.h"
#include "studio.h"
#include "tier0/memalloc.h"
#include "tier0/memdbgon.h"

namespace
{
constexpr int kUnpatchedBoneLimit = 128;
int s_nBoneLimit = kUnpatchedBoneLimit;

// Source SDK Base 2013 Multiplayer x64 studiorender.dll. These are instruction
// starts for signed vertex bone-index loads, not arbitrary movsx instructions.
constexpr DWORD kTimeDateStamp = 0x67B40F47;
constexpr DWORD kSizeOfImage = 0x43C000;
constexpr DWORD kBoneLoadRvas[] =
{
	0x8710, 0x87C1, 0x87F0, 0x8850, 0x8F36, 0x8F6A, 0x8F9C, 0xA643, 0xA6F0,
	0xA71A, 0xA754, 0xA78F, 0xA81B, 0xAF14, 0xAF70, 0xB0C0, 0xB14D, 0xB6C8,
	0xB70C, 0xB7A8, 0xB867, 0xB8F0, 0xB91D, 0xB95A, 0xB998, 0xBA22, 0xCA79,
	0xCA93, 0xCAA2, 0xCC8C, 0xCC9A, 0xCDDF, 0xFE70, 0xFEE2, 0xFF40, 0xFFE0,
	0x1004E, 0x1007B, 0x100C0, 0x20539, 0x55658, 0x55673, 0x5568C, 0x556A7, 0x556D6,
};
static_assert( ARRAYSIZE( kBoneLoadRvas ) == 45, "Incomplete studiorender bone load table" );
static_assert( sizeof( matrix3x4_t ) == 48, "Unexpected studiorender pose matrix size" );

constexpr DWORD kStudioRenderRva = 0x9A6C0;
constexpr size_t kPoseAlignment = 32;

// Only unpatched opcodes are included here, so rollback leaves already-patched
// opcodes exactly as they were. Both pose pointers are untouched on failure.
const char *PatchBoneLoads( BYTE *const *pOpcodes, size_t count )
{
	if ( count == 0 )
		return nullptr;

	BYTE *pStart = pOpcodes[0];
	const size_t size = pOpcodes[count - 1] - pStart + 1;
	MEMORY_BASIC_INFORMATION region;
	// VirtualProtect only reports the first page's old protection. Make sure
	// the whole span has the same protection before changing it in one call.
	if ( !VirtualQuery( pStart, &region, sizeof( region ) ) || region.State != MEM_COMMIT ||
		pStart + size > static_cast<BYTE *>( region.BaseAddress ) + region.RegionSize )
		return "bone load sites do not share one code protection region";

	DWORD oldProtection;
	if ( !VirtualProtect( pStart, size, PAGE_EXECUTE_READWRITE, &oldProtection ) )
		return "VirtualProtect could not make the bone loads writable";

	for ( size_t i = 0; i < count; ++i )
		*pOpcodes[i] = 0xB6;

	DWORD ignored;
	const bool restored = VirtualProtect( pStart, size, oldProtection, &ignored ) != FALSE;
	const HANDLE process = GetCurrentProcess();
	if ( restored && FlushInstructionCache( process, pStart, size ) )
		return nullptr;

	// These calls should not fail for this committed region. Do not continue
	// startup if Windows prevents us from rolling back a partial installation.
	if ( !VirtualProtect( pStart, size, PAGE_EXECUTE_READWRITE, &ignored ) )
		Error( "StudioRender bone limit: could not reopen the bone loads for rollback\n" );
	for ( size_t i = 0; i < count; ++i )
		*pOpcodes[i] = 0xBE;
	if ( !VirtualProtect( pStart, size, oldProtection, &ignored ) || !FlushInstructionCache( process, pStart, size ) )
		Error( "StudioRender bone limit: could not finish rolling back the bone loads\n" );

	return restored ? "FlushInstructionCache failed" : "VirtualProtect could not restore code protection";
}

const char *InstallBoneLimit( DWORD &failureRva )
{
	BYTE *pBase = reinterpret_cast<BYTE *>( GetModuleHandleA( "studiorender.dll" ) );
	if ( !pBase )
		return "studiorender.dll is not loaded";

	const auto *pDos = reinterpret_cast<const IMAGE_DOS_HEADER *>( pBase );
	if ( pDos->e_magic != IMAGE_DOS_SIGNATURE || pDos->e_lfanew < static_cast<LONG>( sizeof( *pDos ) ) ||
		pDos->e_lfanew > 4096 - static_cast<LONG>( sizeof( IMAGE_NT_HEADERS64 ) ) )
		return "invalid studiorender DOS header";
	const auto *pNt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>( pBase + pDos->e_lfanew );
	if ( pNt->Signature != IMAGE_NT_SIGNATURE || pNt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
		pNt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC )
		return "invalid studiorender x64 PE header";
	if ( pNt->FileHeader.TimeDateStamp != kTimeDateStamp )
		return "unexpected studiorender TimeDateStamp";
	if ( pNt->OptionalHeader.SizeOfImage != kSizeOfImage )
		return "unexpected studiorender SizeOfImage";

	BYTE *pOpcodes[ARRAYSIZE( kBoneLoadRvas )];
	size_t opcodeCount = 0;
	for ( DWORD rva : kBoneLoadRvas )
	{
		BYTE *p = pBase + rva;
		if ( ( *p & 0xF0 ) == 0x40 ) // Optional x64 REX prefix.
			++p;
		if ( p[0] != 0x0F || ( p[1] != 0xBE && p[1] != 0xB6 ) )
		{
			failureRva = rva;
			return "unexpected bone load opcode";
		}
		if ( p[1] == 0xBE )
			pOpcodes[opcodeCount++] = p + 1;
	}

	// The constructor allocates 128 * 48 bytes with tier0 aligned allocation
	// (31 bytes of alignment slack plus an 8-byte original-pointer slot).
	constexpr BYTE kAlloc[] = { 0xBA, 0x27, 0x18, 0x00, 0x00 };
	constexpr BYTE kStoreF0[] = { 0x48, 0x89, 0x87, 0xF0, 0x00, 0x00, 0x00 };
	constexpr BYTE kStoreF8[] = { 0x48, 0x89, 0x9F, 0xF8, 0x00, 0x00, 0x00 };
	const struct { DWORD rva; const BYTE *bytes; size_t size; } sites[] =
	{
		{ 0x5160D, kAlloc, sizeof( kAlloc ) },
		{ 0x516BB, kAlloc, sizeof( kAlloc ) },
		{ 0x516B4, kStoreF0, sizeof( kStoreF0 ) },
		{ 0x516E1, kStoreF8, sizeof( kStoreF8 ) },
	};
	for ( const auto &site : sites )
	{
		if ( memcmp( pBase + site.rva, site.bytes, site.size ) != 0 )
		{
			failureRva = site.rva;
			return "unexpected pose buffer constructor bytes";
		}
	}

	void **pPoseF0 = reinterpret_cast<void **>( pBase + kStudioRenderRva + 0xF0 );
	void **pPoseF8 = reinterpret_cast<void **>( pBase + kStudioRenderRva + 0xF8 );
	void *pOldF0 = *pPoseF0;
	void *pOldF8 = *pPoseF8;
	if ( !pOldF0 || ( reinterpret_cast<UINT_PTR>( pOldF0 ) & ( kPoseAlignment - 1 ) ) )
	{
		failureRva = kStudioRenderRva + 0xF0;
		return "null or misaligned first pose buffer";
	}
	if ( !pOldF8 || ( reinterpret_cast<UINT_PTR>( pOldF8 ) & ( kPoseAlignment - 1 ) ) || pOldF8 == pOldF0 )
	{
		failureRva = kStudioRenderRva + 0xF8;
		return "null, misaligned, or aliased second pose buffer";
	}

	constexpr size_t kPoseBytes = MAXSTUDIOBONES * sizeof( matrix3x4_t );
	void *pNewF0 = MemAlloc_AllocAligned( kPoseBytes, kPoseAlignment );
	if ( !pNewF0 )
		return "could not allocate the first expanded pose buffer";
	void *pNewF8 = MemAlloc_AllocAligned( kPoseBytes, kPoseAlignment );
	if ( !pNewF8 )
	{
		MemAlloc_FreeAligned( pNewF0 );
		return "could not allocate the second expanded pose buffer";
	}

	if ( const char *pFailure = PatchBoneLoads( pOpcodes, opcodeCount ) )
	{
		MemAlloc_FreeAligned( pNewF8 );
		MemAlloc_FreeAligned( pNewF0 );
		return pFailure;
	}

	*pPoseF0 = pNewF0;
	*pPoseF8 = pNewF8;
	MemAlloc_FreeAligned( pOldF0 );
	MemAlloc_FreeAligned( pOldF8 );
	return nullptr;
}

void InitBoneLimitOnce()
{
	DWORD failureRva = 0;
	const char *pFailure = InstallBoneLimit( failureRva );
	if ( !pFailure )
	{
		s_nBoneLimit = MAXSTUDIOBONES;
		Msg( "StudioRender bone limit: %d (patched)\n", s_nBoneLimit );
	}
	else if ( failureRva )
		Warning( "StudioRender bone limit: %d (%s at RVA 0x%lX)\n", s_nBoneLimit, pFailure, failureRva );
	else
		Warning( "StudioRender bone limit: %d (%s)\n", s_nBoneLimit, pFailure );
}
} // namespace

void StudioRender_InitBoneLimit()
{
	static const bool initialized = ( InitBoneLimitOnce(), true );
	(void)initialized;
}

int StudioRender_GetBoneLimit()
{
	return s_nBoneLimit;
}
