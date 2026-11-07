//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Restore reached L4D2 VMT quality tests before rendering.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/effects/terror_materials.h"
#include "filesystem.h"
#include "igamesystem.h"
#include "KeyValues.h"
#include "engine/ivmodelinfo.h"
#include "iefx.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "istudiorender.h"
#include "tier1/utlmap.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
#include "l4d2/effects/terror_materials_quality.h"
#include <limits.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Native client registration uses effective flags directly and shares the
// CPU/memory quality callback; this remains the sole gpu_level object.
void SystemLevelChanged( IConVar *, const char *, float );
static ConVar gpu_level( "gpu_level", "3", 0xa, "GPU Level - Default: High", SystemLevelChanged );

namespace
{
using TerrorMaterialQuality::CopyPatchValue;

void CollectPatchValues( KeyValues *pDestination, KeyValues *pSource )
{
	if ( !pSource )
		return;

	for ( KeyValues *pValue = pSource->GetFirstValue(); pValue; pValue = pValue->GetNextValue() )
	{
		CopyPatchValue( pDestination, pValue, pValue->GetName() );
	}
}

void ApplyPatchValues( KeyValues *pDestination, KeyValues *pSource, bool bReplace )
{
	for ( KeyValues *pValue = pSource->GetFirstSubKey(); pValue; pValue = pValue->GetNextKey() )
	{
		if ( !bReplace || pDestination->FindKey( pValue->GetName() ) )
		{
			CopyPatchValue( pDestination, pValue, pValue->GetName() );
		}
	}

	if ( !bReplace )
		return;

	for ( KeyValues *pSection = pDestination->GetFirstTrueSubKey(); pSection; pSection = pSection->GetNextTrueSubKey() )
	{
		KeyValues *pReplacement = pSource->FindKey( pSection->GetName() );
		if ( pReplacement && pReplacement->GetDataType() == KeyValues::TYPE_NONE )
		{
			ApplyPatchValues( pSection, pReplacement, true );
		}
	}
}

bool LoadMaterialValues( const char *pMaterialName, KeyValues *pValues )
{
	CUtlString filename;
	filename.Format( "materials/%s.vmt", pMaterialName );
	if ( !pValues->LoadFromFile( filesystem, filename.Get(), "GAME" ) )
	{
		Warning( "Unable to read reached L4D2 VMT %s from GAME.\n", filename.Get() );
		return false;
	}

	if ( Q_stricmp( pValues->GetName(), "patch" ) )
		return true;

	KeyValues::AutoDelete insert( new KeyValues( "insert" ) );
	KeyValues::AutoDelete replace( new KeyValues( "replace" ) );
	// Original RVAs 0x6C50/0x5040 collect outer-to-inner patch values, then
	// apply insert followed by replace to the final included shader tree.
	// Preserve that order even when a deeper patch defines the same key.
	int nDepth = 0;
	while ( !Q_stricmp( pValues->GetName(), "patch" ) )
	{
		if ( nDepth++ == 10 )
		{
			Warning( "Infinite recursion in reached L4D2 patch material %s.\n", pMaterialName );
			return false;
		}

		CollectPatchValues( insert, pValues->FindKey( "insert" ) );
		CollectPatchValues( replace, pValues->FindKey( "replace" ) );
		filename = pValues->GetString( "include" );
		if ( filename.IsEmpty() )
		{
			Warning( "Reached L4D2 patch material %s has no include key.\n", pMaterialName );
			return false;
		}

		pValues->Clear();
		if ( !pValues->LoadFromFile( filesystem, filename.Get(), "GAME" ) )
		{
			Warning( "Unable to read reached L4D2 VMT include %s from GAME.\n", filename.Get() );
			return false;
		}
	}

	ApplyPatchValues( pValues, insert, false );
	ApplyPatchValues( pValues, replace, true );
	return true;
}


void TerrorMaterialRelease();

class CTerrorMaterialPreparation : public CAutoGameSystemPerFrame
{
public:
	CTerrorMaterialPreparation() : CAutoGameSystemPerFrame( "CTerrorMaterialPreparation" ),
		m_Prepared( DefLessFunc( IMaterial * ) ), m_nGPU( gpu_level.GetInt() ),
		m_nDXBucket( INT_MIN ), m_nNativeReleaseSerial( 0 ), m_nRefreshedReleaseSerial( 0 ),
		m_nBatchDepth( 0 ), m_bReleaseRegistered( false ), m_bNetworkBatchActive( false )
	{
	}

	virtual void Shutdown() OVERRIDE
	{
		if ( m_bReleaseRegistered )
			materials->RemoveReleaseFunc( TerrorMaterialRelease );
		m_bReleaseRegistered = false;
		ReleaseMaterials( false );
		m_Prepared.Purge();
		m_Pending.Purge();
	}

	virtual void LevelInitPreEntity() OVERRIDE
	{
		BeginBatch();
		RefreshQuality();
		PrepareLoadedMaterials();
		EndBatch();
	}

	virtual void LevelShutdownPreEntity() OVERRIDE
	{
		// Finish colour uploads before entity/model teardown can retire their
		// colour meshes. The native render-queue drain does not finish these.
		filesystem->AsyncFinishAll( -1 );
	}

	virtual void LevelShutdownPostEntity() OVERRIDE
	{
		// A different owner can retain an adapted IMaterial after this release.
		// Admission reconstructs every authored GPU-bearing tree, including an
		// inactive GPU fallback; no campaign-long material pin is required.
		ReleaseMaterials( true );
		// An abandoned network cohort cannot retain its depth across maps.
		m_bNetworkBatchActive = false;
		m_nBatchDepth = 0;
	}

	virtual void Update( float ) OVERRIDE
	{
		BeginBatch();
		if ( RefreshQuality() )
			PrepareLoadedMaterials();
		EndBatch();
	}

	void BeginBatch()
	{
		++m_nBatchDepth;
	}

	bool EndBatch()
	{
		Assert( m_nBatchDepth > 0 );
		return --m_nBatchDepth ? true : ApplyPending();
	}

	void BeginNetworkUpdate()
	{
		Assert( !m_bNetworkBatchActive );
		m_bNetworkBatchActive = true;
		BeginBatch();
	}

	void EndNetworkUpdate()
	{
		if ( !m_bNetworkBatchActive )
			return;
		m_bNetworkBatchActive = false;
		EndBatch();
	}

	bool Prepare( IMaterial *pMaterial, const char *pShaderOverride = NULL )
	{
		if ( !pMaterial )
			return false;

		// DX affects the selected native alias. Query it before the cache fast
		// path, even when gpu_level and the material pointer have not changed.
		IMaterialSystemHardwareConfig *pHardware = materials->GetHardwareConfig(
			MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION, NULL );
		if ( !pHardware )
		{
			Warning( "Cannot query native hardware conditions for L4D2 material %s.\n", pMaterial->GetName() );
			return false;
		}
		const int nGPU = gpu_level.GetInt();
		const int nDXLevel = pHardware->GetDXSupportLevel();
		const int nDXBucket = nDXLevel >= 92 ? 92 : ( nDXLevel >= 90 ? 90 : 0 );
		int nIndex = m_Prepared.Find( pMaterial );
		if ( nIndex != m_Prepared.InvalidIndex() )
		{
			if ( m_Prepared[nIndex].m_nGPU == nGPU &&
				m_Prepared[nIndex].m_nDXBucket == nDXBucket &&
				m_Prepared[nIndex].m_nReleaseSerial == m_nNativeReleaseSerial &&
				( !pShaderOverride || m_Prepared[nIndex].m_bParticlePrepared ) )
				return m_Prepared[nIndex].m_bPrepared || m_Prepared[nIndex].m_pPendingValues != NULL;
			if ( m_Prepared[nIndex].m_bParticlePrepared )
				pShaderOverride = "TerrorSpriteCard";
		}
		else
		{
			pMaterial->IncrementReferenceCount();
			nIndex = m_Prepared.Insert( pMaterial, MaterialPreparation_t() );
		}
		m_Prepared[nIndex].m_nGPU = nGPU;
		m_Prepared[nIndex].m_nDXBucket = nDXBucket;
		m_Prepared[nIndex].m_nReleaseSerial = m_nNativeReleaseSerial;
		m_Prepared[nIndex].m_bPrepared = false;
		if ( pShaderOverride )
			m_Prepared[nIndex].m_bParticlePrepared = true;
		if ( pMaterial->IsErrorMaterial() )
		{
			Warning( "Cannot prepare reached L4D2 error material %s.\n", pMaterial->GetName() );
			return false;
		}

		KeyValues::AutoDelete values( new KeyValues( "vmt" ) );
		bool bChanged = false;
		if ( !LoadMaterialValues( pMaterial->GetName(), values ) )
			return false;
		const char *pUnsupported = NULL;
		if ( !TerrorMaterialQuality::Resolve( values, nGPU, nDXLevel, bChanged, &pUnsupported ) )
		{
			Warning( "Unrecovered shader-chain GPU fallback %s in L4D2 material %s; preparation rejected.\n",
				pUnsupported, pMaterial->GetName() );
			return false;
		}

		IMaterial *pModelCompanion = NULL;
		if ( !Q_stricmp( values->GetName(), "Subrect" ) )
		{
			const char *pModelMaterialName = values->GetString( "$modelmaterial" );
			if ( pModelMaterialName[0] )
			{
				pModelCompanion = materials->FindMaterial( pModelMaterialName, TEXTURE_GROUP_DECAL );
				if ( pModelCompanion && pModelCompanion->IsErrorMaterial() )
				{
					pModelCompanion = NULL;
				}
			}
		}

		// The target rope backend supplies four spline points, but no endpoint
		// normals. Admit only static, simple-colour splines using that packet.
		// Other native pixel/render-state families keep their existing owner.
		if ( nDXLevel >= 92 && !Q_stricmp( values->GetName(), "SpriteCard" ) &&
			values->GetInt( "$splinetype" ) != 0 &&
			clamp( values->GetInt( "$orientation" ), 0, 3 ) < 3 &&
			!values->FindKey( "Proxies" ) && !values->FindKey( "$ramptexture" ) &&
			!values->GetInt( "$dualsequence" ) &&
			!values->GetInt( "$maxlumframeblend1" ) && !values->GetInt( "$maxlumframeblend2" ) &&
			!values->GetInt( "$extractgreenalpha" ) && !values->GetInt( "$addoverblend" ) &&
			!values->GetInt( "$mod2x" ) && !values->GetInt( "$vertexcolorlerp" ) &&
			!values->GetInt( "$distancealpha" ) &&
			!values->GetInt( "$shadowdepth" ) && !values->GetInt( "$opaque" ) &&
			FloatAbsBits( values->GetFloat( "$addbasetexture2" ) ) == 0U &&
			FloatAbsBits( values->GetFloat( "$addself" ) ) == 0U )
		{
			values->SetName( "TerrorSpriteCard" );
			bChanged = true;
		}

		if ( pShaderOverride )
		{
			if ( Q_stricmp( pShaderOverride, "TerrorSpriteCard" ) )
			{
				Warning( "Unrecovered L4D2 particle shader replacement %s for %s.\n",
					pShaderOverride, pMaterial->GetName() );
				return false;
			}
			if ( !Q_stricmp( values->GetName(), "SpriteCard" ) &&
				!values->GetInt( "$splinetype" ) &&
				values->GetInt( "$distancealpha" ) && values->GetInt( "$outline" ) &&
				values->GetInt( "$perparticleoutline" ) && !values->GetInt( "$dualsequence" ) )
			{
				values->SetName( pShaderOverride );
				bChanged = true;
			}
		}

		if ( !Q_stricmp( values->GetName(), "Infected" ) )
		{
			KeyValues *pProxies = values->FindKey( "Proxies", true );
			if ( !pProxies->FindKey( "TerrorInfectedWounds" ) )
			{
				pProxies->AddSubKey( new KeyValues( "TerrorInfectedWounds" ) );
				bChanged = true;
			}
		}

		KeyValues *pQueuedValues = NULL;
		if ( bChanged )
		{
			// Coalesce first-use preparation until its consumer needs the tree.
			// ApplyPending drains queued draws without reloading the level's
			// meshes, lightmaps or client entities.
			if ( !m_Prepared[nIndex].m_pPendingValues )
				m_Pending.AddToTail( pMaterial );
			else
				m_Prepared[nIndex].m_pPendingValues->deleteThis();
			pQueuedValues = values;
			m_Prepared[nIndex].m_pPendingValues = values;
			values.Assign( NULL );
		}
		if ( pModelCompanion )
		{
			if ( !Prepare( pModelCompanion ) )
			{
				if ( pQueuedValues && m_Prepared[nIndex].m_pPendingValues == pQueuedValues )
				{
					pQueuedValues->deleteThis();
					m_Prepared[nIndex].m_pPendingValues = NULL;
					m_Pending.FindAndRemove( pMaterial );
				}
				return false;
			}
			if ( m_Pending.Count() && !ApplyPending() )
			{
				return false;
			}
			// Native Subrect setup fully precaches this companion before
			// a studio decal can consume it. The SDK omits that dependency.
			pModelCompanion->GetMappingWidth();
		}
		if ( bChanged )
		{
			if ( !m_nBatchDepth )
			{
				return ApplyPending();
			}
			return true;
		}
		m_Prepared[nIndex].m_bPrepared = !pMaterial->IsErrorMaterial();
		return m_Prepared[nIndex].m_bPrepared;
	}

	bool PrepareImmediately( IMaterial *pMaterial, const char *pShaderOverride )
	{
		bool bPrepared = Prepare( pMaterial, pShaderOverride );
		// Shader-name and decal queries require the effective tree now.
		if ( bPrepared && m_nBatchDepth )
		{
			const int nIndex = m_Prepared.Find( pMaterial );
			if ( m_Prepared[nIndex].m_pPendingValues )
				bPrepared = ApplyPending();
		}
		return bPrepared;
	}

	// Device loss and explicit engine reloads invalidate the effective tree.
	// Reconstruct it on the main thread, never inside a release callback.
	void OnNativeRelease()
	{
		++m_nNativeReleaseSerial;
	}

private:
	bool ApplyPending()
	{
		if ( !m_Pending.Count() )
			return true;
		// Lock drains queued consumers before these individual materials change.
		// This is not a resource release: a full ReloadMaterials also rebuilds
		// lightmaps and requests a full game update, even on first-use gameplay
		// paths such as model assignment, decals and particle initialization.
		if ( !m_bReleaseRegistered )
		{
			materials->AddReleaseFunc( TerrorMaterialRelease );
			m_bReleaseRegistered = true;
		}
		MaterialLock_t lock = materials->Lock();
		bool bApplySucceeded = true;
		FOR_EACH_VEC( m_Pending, i )
		{
			const int nIndex = m_Prepared.Find( m_Pending[i] );
			MaterialPreparation_t &record = m_Prepared[nIndex];
			m_Pending[i]->SetShaderAndParams( record.m_pPendingValues );
			record.m_pPendingValues->deleteThis();
			record.m_pPendingValues = NULL;
			record.m_bPrepared = !m_Pending[i]->IsErrorMaterial();
			bApplySucceeded = record.m_bPrepared && bApplySucceeded;
		}
		m_Pending.RemoveAll();
		materials->Unlock( lock );
		return bApplySucceeded;
	}

	void PrepareLoadedMaterials()
	{
		for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
		{
			IMaterial *pMaterial = materials->GetMaterial( h );
			if ( !pMaterial || pMaterial->IsErrorMaterial() )
				continue;
			if ( m_Prepared.Find( pMaterial ) != m_Prepared.InvalidIndex() )
				continue;
			// Procedural materials have no mounted VMT. No warning or source
			// reconstruction is appropriate for __fontpage/render targets/etc.
			CUtlVectorFixedGrowable<char, MAX_PATH> filename;
			filename.SetCount( Q_strlen(pMaterial->GetName()) + sizeof("materials/.vmt") );
			V_snprintf( filename.Base(), filename.Count(), "materials/%s.vmt", pMaterial->GetName() );
			if ( filesystem->FileExists( filename.Base(), "GAME" ) )
				Prepare( pMaterial );
		}
	}

	bool RefreshQuality()
	{
		const int nGPU = gpu_level.GetInt();
		IMaterialSystemHardwareConfig *pHardware = materials->GetHardwareConfig(
			MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION, NULL );
		if ( !pHardware )
			return false;
		const int nDXLevel = pHardware->GetDXSupportLevel();
		const int nDXBucket = nDXLevel >= 92 ? 92 : ( nDXLevel >= 90 ? 90 : 0 );
		if ( nGPU == m_nGPU && nDXBucket == m_nDXBucket &&
			m_nRefreshedReleaseSerial == m_nNativeReleaseSerial )
			return false;
		m_nGPU = nGPU;
		m_nDXBucket = nDXBucket;
		m_nRefreshedReleaseSerial = m_nNativeReleaseSerial;
		FOR_EACH_MAP( m_Prepared, i )
		{
			Prepare( m_Prepared.Key(i), m_Prepared[i].m_bParticlePrepared ? "TerrorSpriteCard" : NULL );
		}
		return true;
	}

	void ReleaseMaterials( bool bKeepParticles )
	{
		for ( int i = m_Prepared.FirstInorder(); i != m_Prepared.InvalidIndex(); )
		{
			int nNext = m_Prepared.NextInorder( i );
			if ( !bKeepParticles || !m_Prepared[i].m_bParticlePrepared )
			{
				if ( m_Prepared[i].m_pPendingValues )
				{
					m_Prepared[i].m_pPendingValues->deleteThis();
					m_Pending.FindAndRemove( m_Prepared.Key(i) );
				}
				m_Prepared.Key(i)->DecrementReferenceCount();
				m_Prepared.RemoveAt( i );
			}
			i = nNext;
		}
	}

	struct MaterialPreparation_t
	{
		MaterialPreparation_t() : m_bPrepared( false ), m_bParticlePrepared( false ),
			m_nGPU( INT_MIN ), m_nDXBucket( INT_MIN ), m_nReleaseSerial( 0 ),
			m_pPendingValues( NULL ) {}
		bool m_bPrepared;
		bool m_bParticlePrepared;
		int m_nGPU;
		int m_nDXBucket;
		unsigned int m_nReleaseSerial;
		KeyValues *m_pPendingValues;
	};
	CUtlMap< IMaterial *, MaterialPreparation_t > m_Prepared;
	CUtlVector< IMaterial * > m_Pending;
	int m_nGPU;
	int m_nDXBucket;
	unsigned int m_nNativeReleaseSerial;
	unsigned int m_nRefreshedReleaseSerial;
	int m_nBatchDepth;
	bool m_bReleaseRegistered;
	bool m_bNetworkBatchActive;
};

CTerrorMaterialPreparation g_TerrorMaterialPreparation;

void TerrorMaterialRelease()
{
	g_TerrorMaterialPreparation.OnNativeRelease();
}
}

void Terror_BeginNetworkMaterialPreparation()
{
	g_TerrorMaterialPreparation.BeginNetworkUpdate();
}

void Terror_EndNetworkMaterialPreparation()
{
	g_TerrorMaterialPreparation.EndNetworkUpdate();
}

bool Terror_PrepareModelMaterials( const model_t *pModel )
{
	if ( !pModel )
		return false;

	studiohdr_t *pStudioHdr = modelinfo->GetStudiomodel( pModel );
	if ( !pStudioHdr )
		return false;
	const int nCount = pStudioHdr->numtextures;
	CUtlVectorFixedGrowable< IMaterial *, 32 > modelMaterials;
	modelMaterials.SetCount( nCount );
	if ( nCount )
	{
		const int nMaterials = g_pStudioRender->GetMaterialList( pStudioHdr, nCount, modelMaterials.Base() );
		if ( nMaterials != nCount )
			return false;
	}

	bool bPrepared = true;
	g_TerrorMaterialPreparation.BeginBatch();
	FOR_EACH_VEC( modelMaterials, i )
	{
		if ( !g_TerrorMaterialPreparation.Prepare( modelMaterials[i] ) )
			bPrepared = false;
	}
	bPrepared = g_TerrorMaterialPreparation.EndBatch() && bPrepared;
	return bPrepared;
}

bool Terror_PrepareDecalMaterial( int nDecalIndex )
{
	const char *pMaterialName = effects->Draw_DecalNameFromIndex( nDecalIndex );
	if ( !pMaterialName || !pMaterialName[0] )
	{
		return false;
	}
	IMaterial *pMaterial = materials->FindMaterial( pMaterialName, TEXTURE_GROUP_DECAL );
	return g_TerrorMaterialPreparation.PrepareImmediately( pMaterial, NULL );
}

bool Terror_PrepareParticleMaterial( IMaterial *pMaterial, const char *pShaderOverride )
{
	return g_TerrorMaterialPreparation.PrepareImmediately( pMaterial, pShaderOverride );
}
