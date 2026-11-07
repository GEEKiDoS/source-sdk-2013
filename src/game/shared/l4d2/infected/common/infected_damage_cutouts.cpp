//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected_damage_cutouts.h"
#include "filesystem.h"
#include "igamesystem.h"
#include "tier1/KeyValues.h"
#include "tier1/strtools.h"

#include "weapon_parse.h"
#include "gamerules.h"
#include "tier0/memdbgon.h"

ConVar z_infected_damage_cutouts( "z_infected_damage_cutouts", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY
#endif
 );

InfectedDamageCutout_t::InfectedDamageCutout_t()
{
	Q_memset( this, 0, sizeof( *this ) );
	m_nSwingStart = m_nSwingEnd = -1;
	m_nParticleAttachmentHitbox = -1;
	for ( int i = 0; i < ARRAYSIZE( m_iExclusion ); ++i )
		m_iExclusion[i] = -1;
}

namespace
{
	enum CutoutSet_t
	{
		CUTOUT_COMMON,
		CUTOUT_L4D1,
		CUTOUT_FEMALE,
		CUTOUT_FEMALE_L4D1,
		CUTOUT_CEDA,
		CUTOUT_LAUGHRIOT,
		CUTOUT_JIMMY,
		CUTOUT_HOSPITAL_PATIENT,
		CUTOUT_COUNT
	};

	struct CutoutExclusions_t
	{
		char m_szModel[10][128];
	};

	InfectedDamageCutoutSet s_CutoutSet[CUTOUT_COUNT];

	// The source files use space-separated lists. Keep their order and limits.
	const char *CutoutToken( const char *pszText, char *pszToken, int nTokenSize )
	{
		while ( *pszText == ' ' )
			++pszText;
		if ( !*pszText )
		{
			pszToken[0] = 0;
			return NULL;
		}
		return nexttoken( pszToken, nTokenSize, pszText, ' ' );
	}

	int CutoutDamageType( const char *pszType )
	{
		int nFlags = 0;
		if ( Q_stristr( pszType, "gunshot" ) ) nFlags |= INFECTED_CUTOUT_GUNSHOT;
		if ( Q_stristr( pszType, "explosion" ) ) nFlags |= INFECTED_CUTOUT_EXPLOSION;
		if ( Q_stristr( pszType, "dismember" ) ) nFlags |= INFECTED_CUTOUT_DISMEMBER;
		if ( Q_stristr( pszType, "slash" ) ) nFlags |= INFECTED_CUTOUT_SLASH;
		if ( Q_stristr( pszType, "blunt" ) ) nFlags |= INFECTED_CUTOUT_BLUNT;
		if ( Q_stristr( pszType, "chainsaw" ) ) nFlags |= INFECTED_CUTOUT_CHAINSAW;
		if ( Q_stristr( pszType, "decapitate" ) ) nFlags |= INFECTED_CUTOUT_DECAPITATE;
		if ( Q_stristr( pszType, "headshot" ) ) nFlags |= INFECTED_CUTOUT_HEADSHOT;
		return nFlags;
	}

	void ReadCutoutRegion( KeyValues *pKV, InfectedDamageCutoutRegion_t &region )
	{
		UTIL_StringToVector( region.m_vecCenter.Base(), pKV->GetString( "Center" ) );
		UTIL_StringToVector( region.m_vecLookAt.Base(), pKV->GetString( "Lookat" ) );
		UTIL_StringToVector( region.m_vecUp.Base(), pKV->GetString( "Up" ) );
		UTIL_StringToVector( region.m_vecScale.Base(), pKV->GetString( "Scale" ) );
	}

static int CutoutSwingDirection( const char *direction )
{
    if ( !Q_stricmp( direction, "N" ) ) return 0;
    if ( !Q_stricmp( direction, "S" ) ) return 1;
    if ( !Q_stricmp( direction, "E" ) ) return 2;
    if ( !Q_stricmp( direction, "W" ) ) return 3;
    if ( !Q_stricmp( direction, "NW" ) ) return 7;
    if ( !Q_stricmp( direction, "NE" ) ) return 4;
    if ( !Q_stricmp( direction, "SW" ) ) return 6;
    if ( !Q_stricmp( direction, "SE" ) ) return 5;
    return -1;
}
	bool ReadCutoutSet( InfectedDamageCutoutSet &set, const char *pszFile,
		const unsigned char *pICEKey )
	{
		// S3523A0: retain successful nonempty sets across maps.
		if ( set.m_Cutout.Count() > 0 )
			return false;
		KeyValues *pRoot = ReadEncryptedKVFile( filesystem, pszFile, pICEKey );
		if ( !pRoot )
			return false;

		CUtlVector<CutoutExclusions_t> exclusions;
		for ( int nSection = 1; nSection < 50; ++nSection )
		{
			char szSection[32];
			Q_snprintf( szSection, sizeof( szSection ), "DamageCutout%d", nSection );
			KeyValues *pKV = pRoot->FindKey( szSection );
			if ( !pKV || !pKV->FindKey( "Model" ) || !pKV->FindKey( "HitBoxes" ) ||
				!pKV->FindKey( "ExclusionList" ) || !pKV->FindKey( "Type" ) ||
				!pKV->FindKey( "Fatal" ) || !pKV->FindKey( "Primary" ) )
				continue;

			InfectedDamageCutout_t cutout;
			CutoutExclusions_t names;
			Q_memset( &names, 0, sizeof( names ) );
			Q_snprintf( cutout.m_szModel, sizeof( cutout.m_szModel ), "models/infected/%s.mdl", pKV->GetString( "Model" ) );
			cutout.m_nDamageType = CutoutDamageType( pKV->GetString( "Type" ) );
			cutout.m_bFatal = pKV->GetInt( "Fatal", 0 ) != 0;
			cutout.m_bBack = pKV->GetInt( "Back", 0 ) != 0;
			cutout.m_nSwingStart = CutoutSwingDirection( pKV->GetString( "SwingStart", "" ) );
			cutout.m_nSwingEnd = CutoutSwingDirection( pKV->GetString( "SwingEnd", "" ) );
			Q_strncpy( cutout.m_szDeathActivity, pKV->GetString( "DeathThroes", "" ), sizeof( cutout.m_szDeathActivity ) );
			Q_strncpy( cutout.m_szParticleAttachment, pKV->GetString( "ParticleAttach", "" ), sizeof( cutout.m_szParticleAttachment ) );
			cutout.m_nParticleAttachmentHitbox = pKV->GetInt( "ParticleAttachHitbox", -1 );

			char szToken[128];
			const char *pszNext = pKV->GetString( "HitBoxes" );
			while ( pszNext && cutout.m_nHitboxCount < ARRAYSIZE( cutout.m_iHitbox ) )
			{
				pszNext = CutoutToken( pszNext, szToken, sizeof( szToken ) );
				if ( szToken[0] )
					cutout.m_iHitbox[cutout.m_nHitboxCount++] = atoi( szToken );
			}
			pszNext = pKV->GetString( "ExclusionList" );
			while ( pszNext && cutout.m_nExclusionCount < ARRAYSIZE( cutout.m_iExclusion ) )
			{
				pszNext = CutoutToken( pszNext, szToken, sizeof( szToken ) );
				if ( szToken[0] )
					Q_strncpy( names.m_szModel[cutout.m_nExclusionCount++], szToken, sizeof( names.m_szModel[0] ) );
			}
			pszNext = pKV->GetString( "Gibs", "" );
			while ( pszNext && cutout.m_nGibCount < ARRAYSIZE( cutout.m_szGibModel ) )
			{
				pszNext = CutoutToken( pszNext, szToken, sizeof( szToken ) );
				if ( szToken[0] )
				{
					Q_snprintf( cutout.m_szGibModel[cutout.m_nGibCount++], sizeof( cutout.m_szGibModel[0] ),
						"models/infected/limbs/%s.mdl", szToken );
				}
			}

			pszNext = pKV->GetString( "ParticleEffect", "" );
			// The original pair parser leaves the eighth array entry unused.
			while ( pszNext && cutout.m_nParticleEffectCount < ARRAYSIZE( cutout.m_szParticleEffect ) - 1 )
			{
				pszNext = CutoutToken( pszNext, szToken, sizeof( szToken ) );
				if ( !szToken[0] )
					break;
				int nEffect = cutout.m_nParticleEffectCount++;
				Q_strncpy( cutout.m_szParticleEffect[nEffect], szToken, sizeof( cutout.m_szParticleEffect[0] ) );
				szToken[0] = 0;
				if ( pszNext )
					pszNext = CutoutToken( pszNext, szToken, sizeof( szToken ) );
				cutout.m_nParticleWeight[nEffect] = atoi( szToken );
				cutout.m_nTotalParticleWeight += cutout.m_nParticleWeight[nEffect];
			}

			ReadCutoutRegion( pKV->FindKey( "Primary" ), cutout.m_Region[0] );
			cutout.m_nRegionCount = 1;
			if ( KeyValues *pSecondary = pKV->FindKey( "Secondary" ) )
			{
				ReadCutoutRegion( pSecondary, cutout.m_Region[1] );
				cutout.m_nRegionCount = 2;
			}
			if ( cutout.m_nDamageType & INFECTED_CUTOUT_EXPLOSION )
				set.m_ExplosionCutout.AddToTail( set.m_Cutout.Count() );
			set.m_Cutout.AddToTail( cutout );
			exclusions.AddToTail( names );
		}
		pRoot->deleteThis();

		for ( int i = 0; i < set.m_Cutout.Count(); ++i )
		{
			InfectedDamageCutout_t &cutout = set.m_Cutout[i];
			for ( int j = 0; j < cutout.m_nExclusionCount; ++j )
			{
				char szExcludedModel[128];
				Q_snprintf( szExcludedModel, sizeof( szExcludedModel ), "models/infected/%s.mdl", exclusions[i].m_szModel[j] );
				for ( int k = 0; k < set.m_Cutout.Count(); ++k )
				{
					if ( !Q_stricmp( set.m_Cutout[k].m_szModel, szExcludedModel ) )
					{
						cutout.m_iExclusion[j] = k;
						break;
					}
				}
				if ( cutout.m_iExclusion[j] == -1 )
					Warning( "Unable to find cutout model for '%s' on wound exclusion list!\n", exclusions[i].m_szModel[j] );
			}
		}
		return true;
	}

#ifdef GAME_DLL
	static void PrecacheDamageCutoutModels( const InfectedDamageCutoutSet &set )
	{
		FOR_EACH_VEC( set.m_Cutout, i )
		{
			const InfectedDamageCutout_t &cutout = set.m_Cutout[i];
			CBaseEntity::PrecacheModel( cutout.m_szModel );
			for ( int gib = 0; gib < cutout.m_nGibCount; ++gib )
				CBaseEntity::PrecacheModel( cutout.m_szGibModel[gib] );
		}
	}
#endif
	class CInfectedDamageCutoutSystem : public CAutoGameSystem
	{
	public:
		CInfectedDamageCutoutSystem() : CAutoGameSystem( "DamageCutoutManager" ) {}
		virtual void LevelInitPreEntity() OVERRIDE
		{
			// S352AB0 / C287B80: SIX separate key queries, then six reads.
			ReadCutoutSet( s_CutoutSet[CUTOUT_COMMON], "scripts/damagecutout", g_pGameRules->GetEncryptionKey() );
			ReadCutoutSet( s_CutoutSet[CUTOUT_L4D1], "scripts/damagecutout_l4d1", g_pGameRules->GetEncryptionKey() );
			ReadCutoutSet( s_CutoutSet[CUTOUT_FEMALE], "scripts/damagecutout_female", g_pGameRules->GetEncryptionKey() );
			ReadCutoutSet( s_CutoutSet[CUTOUT_FEMALE_L4D1], "scripts/damagecutout_female_l4d1", g_pGameRules->GetEncryptionKey() );
			ReadCutoutSet( s_CutoutSet[CUTOUT_CEDA], "scripts/damagecutout_ceda", g_pGameRules->GetEncryptionKey() );
			ReadCutoutSet( s_CutoutSet[CUTOUT_LAUGHRIOT], "scripts/damagecutout_laughriot", g_pGameRules->GetEncryptionKey() );
#ifdef GAME_DLL
			// Native CLIENT287B80 has NO model precache tail; SERVER352AB0 does.
			CBaseEntity::PrecacheModel( "models/infected/cim_faceplate.mdl" );
			CBaseEntity::PrecacheModel( "models/infected/cim_riot_faceplate.mdl" );
			PrecacheDamageCutoutModels( s_CutoutSet[CUTOUT_COMMON] );
			PrecacheDamageCutoutModels( s_CutoutSet[CUTOUT_L4D1] );
			PrecacheDamageCutoutModels( s_CutoutSet[CUTOUT_FEMALE] );
			PrecacheDamageCutoutModels( s_CutoutSet[CUTOUT_FEMALE_L4D1] );
			PrecacheDamageCutoutModels( s_CutoutSet[CUTOUT_CEDA] );
			PrecacheDamageCutoutModels( s_CutoutSet[CUTOUT_LAUGHRIOT] );
#endif
		}
		// Native LevelShutdownPre/Post are no-ops. The retained vectors own
		// their real DLL-lifetime storage/destruction; do not purge on map exit.
	};
	CInfectedDamageCutoutSystem s_InfectedDamageCutoutSystem;
}


const InfectedDamageCutoutSet *GetInfectedDamageCutouts( int nModelType )
{
	switch ( nModelType )
	{
	case 2: return &s_CutoutSet[CUTOUT_FEMALE];
	case 11: return &s_CutoutSet[CUTOUT_CEDA];
	case 15: return &s_CutoutSet[CUTOUT_LAUGHRIOT];
	case 17: return &s_CutoutSet[CUTOUT_JIMMY];
	case 18: return &s_CutoutSet[CUTOUT_HOSPITAL_PATIENT];
	case 21: return &s_CutoutSet[CUTOUT_L4D1];
	case 22: return &s_CutoutSet[CUTOUT_FEMALE_L4D1];
	default: return &s_CutoutSet[CUTOUT_COMMON];
	}
}
