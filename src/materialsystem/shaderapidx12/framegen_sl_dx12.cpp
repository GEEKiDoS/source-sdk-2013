//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Authenticode check for sl.interposer.dll before it is loaded. sl_security.h defines non-inline
//          functions and pulls in wintrust/wincrypt (resolved dynamically), so it lives in this single TU and is
//          included before anything else.
//
//=============================================================================//
#include <sl_security.h>

namespace shaderapidx12
{

//-----------------------------------------------------------------------------
// Purpose: True when `pszFullPath` carries a valid embedded signature issued to NVIDIA
//-----------------------------------------------------------------------------
bool VerifyStreamlineSignatureDX12( const wchar_t *pszFullPath )
{
	return sl::security::verifyEmbeddedSignature( pszFullPath );
}

} // namespace shaderapidx12
