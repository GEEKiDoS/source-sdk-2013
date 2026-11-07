//========= Copyright Valve Corporation, All rights reserved. ============//
// Game-owned normalization for the original L4D2 VMT quality grammar.
#ifndef TERROR_MATERIALS_QUALITY_H
#define TERROR_MATERIALS_QUALITY_H
#ifdef _WIN32
#pragma once
#endif
#include "tier1/KeyValues.h"
#include "tier1/strtools.h"
#include "tier1/utlvector.h"

namespace TerrorMaterialQuality
{
inline void CopyPatchValue( KeyValues *pDestination, KeyValues *pSource, const char *pName )
{
	switch ( pSource->GetDataType() )
	{
	case KeyValues::TYPE_STRING: pDestination->SetString( pName, pSource->GetString() ); break;
	case KeyValues::TYPE_INT: pDestination->SetInt( pName, pSource->GetInt() ); break;
	case KeyValues::TYPE_FLOAT: pDestination->SetFloat( pName, pSource->GetFloat() ); break;
	case KeyValues::TYPE_PTR: pDestination->SetPtr( pName, pSource->GetPtr() ); break;
	default: break;
	}
}

// Original materialsystem 0x5720: six complete, case-insensitive tokens, one
// optional '!'; signed gpu_level integer, not DXSupportLevel or CPU quality.
inline bool EvaluateGPU( const char *pTest, int nLength, int nGPU, bool &bEnabled )
{
	bool bInvert = nLength > 0 && *pTest == '!';
	if ( bInvert ) { ++pTest; --nLength; }
	if ( nLength == 6 && !Q_strnicmp( pTest, "GPU>=", 5 ) && pTest[5] >= '1' && pTest[5] <= '3' )
		bEnabled = nGPU >= pTest[5] - '0';
	else if ( nLength == 5 && !Q_strnicmp( pTest, "GPU<", 4 ) && pTest[4] >= '1' && pTest[4] <= '3' )
		bEnabled = nGPU < pTest[4] - '0';
	else return false;
	if ( bInvert ) bEnabled = !bEnabled;
	return true;
}

// Keep the native reader's conditional priority and order. Renaming a true GPU
// key to an unconditional parameter would be WRONG beside another conditional
// for the same parameter. On this PC target, 360 is false and !360 is true.
// Only the predicate spelling changes; node/type/value/order remain intact.
inline void ResolvePrefixes( KeyValues *pValues, int nGPU, bool &bChanged )
{
	for ( KeyValues *pValue=pValues->GetFirstSubKey(); pValue; pValue=pValue->GetNextKey() )
	{
		const char *pName=pValue->GetName();
		const char *pQuestion=Q_strstr(pName,"?");
		bool bEnabled;
		if ( pQuestion && EvaluateGPU(pName,pQuestion-pName,nGPU,bEnabled) )
		{
			CUtlVectorFixedGrowable<char,256> name;
			name.SetCount(Q_strlen(pQuestion+1)+6);
			V_snprintf(name.Base(),name.Count(),bEnabled?"!360?%s":"360?%s",pQuestion+1);
			pValue->SetName(name.Base()); bChanged=true;
		}
		// !srgb, hdr, ldr, lowfill, 360 and any unrecognized tests are left to
		// the native parser, including its native warnings/rejection behavior.
		if ( pValue->GetDataType()==KeyValues::TYPE_NONE ) ResolvePrefixes(pValue,nGPU,bChanged);
	}
}

static const char *const s_GPUFallbackNames[] = { "GPU<1", "GPU<2", "GPU>=1", "GPU>=2" };

inline bool HasGPUFallback( KeyValues *pValues )
{
	for ( int i = 0; i < ARRAYSIZE(s_GPUFallbackNames); ++i )
	{
		if ( pValues->FindKey(s_GPUFallbackNames[i]) )
			return true;
		char name[256];
		V_snprintf(name,sizeof(name),"%s_%s",pValues->GetName(),s_GPUFallbackNames[i]);
		if ( pValues->FindKey(name) )
			return true;
	}
	return false;
}

inline const char *UnsupportedShaderChainGPUFallback( KeyValues *pValues )
{
	for ( KeyValues *pChild = pValues->GetFirstSubKey(); pChild; pChild = pChild->GetNextKey() )
	{
		if ( pChild->GetDataType() != KeyValues::TYPE_NONE )
			continue;
		const char *pName = pChild->GetName();
		const int nLength = Q_strlen(pName);
		for ( int i = 0; i < ARRAYSIZE(s_GPUFallbackNames); ++i )
		{
			const int nCondition = Q_strlen(s_GPUFallbackNames[i]);
			if ( nLength <= nCondition || pName[nLength-nCondition-1] != '_' ||
				Q_stricmp(pName+nLength-nCondition,s_GPUFallbackNames[i]) )
				continue;
			char name[256];
			V_snprintf(name,sizeof(name),"%s_%s",pValues->GetName(),s_GPUFallbackNames[i]);
			if ( Q_stricmp(pName,name) )
				return pName;
		}
	}
	return NULL;
}

inline KeyValues *FindGPUFallback( KeyValues *pValues, int nGPU )
{
	// Original 0x3670 visits these four conditions in THIS order, each direct
	// name before <current shader>_<condition>. GPU>=3 is prefix-only there.
	const bool enabled[] = { nGPU<1, nGPU<2, nGPU>=1, nGPU>=2 };
	for ( int i=0; i<ARRAYSIZE(s_GPUFallbackNames); ++i )
	{
		if ( !enabled[i] ) continue;
		KeyValues *pBlock=pValues->FindKey(s_GPUFallbackNames[i]);
		if ( pBlock ) return pBlock;
		char name[256]; V_snprintf(name,sizeof(name),"%s_%s",pValues->GetName(),s_GPUFallbackNames[i]);
		pBlock=pValues->FindKey(name);
		if ( pBlock ) return pBlock;
	}
	return NULL;
}

inline bool IsNativeFallbackName( const char *pName )
{
	static const char *const names[] = { "<DX90", "<DX95", "<DX90_20b", ">=DX90_20b", "<=DX90", ">=DX90", ">DX90", "hdr_dx9", "hdr", "ldr", "srgb", "dx9" };
	const int nLength=Q_strlen(pName);
	for ( int i=0; i<ARRAYSIZE(names); ++i )
	{
		const int nCondition=Q_strlen(names[i]);
		if ( !Q_stricmp(pName,names[i]) || (nLength>nCondition && pName[nLength-nCondition-1]=='_' && !Q_stricmp(pName+nLength-nCondition,names[i])) ) return true;
	}
	return false;
}

inline bool Resolve( KeyValues *pValues, int nGPU, int nDXLevel, bool &bChanged, const char **ppUnsupported )
{
	*ppUnsupported = UnsupportedShaderChainGPUFallback(pValues);
	if ( *ppUnsupported )
		return false;
	ResolvePrefixes(pValues,nGPU,bChanged);
	// First admission must also undo an adapted IMaterial retained by another
	// owner across map shutdown. An inactive authored GPU block still needs a
	// fresh tree; native-only materials remain untouched at every quality.
	bChanged = HasGPUFallback(pValues) || bChanged;
	KeyValues *pSelected=FindGPUFallback(pValues,nGPU);
	if ( !pSelected ) return true;
	// Select a native branch which is true for the actual hardware DX level.
	// SDK2013 can also initialize at DX80; blindly using dx9 is not correct.
	// Reuse native chain/proxy/$fallbackmaterial semantics rather than merging
	// parameters ourselves. Original GPU selection precedes ALL native tests.
	for ( KeyValues *pChild=pValues->GetFirstSubKey(); pChild; )
	{
		KeyValues *pNext=pChild->GetNextKey();
		if ( pChild!=pSelected && IsNativeFallbackName(pChild->GetName()) )
		{
			pValues->RemoveSubKey(pChild); pChild->deleteThis();
		}
		pChild=pNext;
	}
	pSelected->SetName(nDXLevel>=90?"dx9":"<DX90"); bChanged=true;
	return true;
}
}
#endif
