//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//

#include "cbase.h"
#include "c_basetempentity.h"
#include "iefx.h"
#include "tier1/KeyValues.h"
#include "toolframework_client.h"
#include "fx.h"
#include "decals.h"
#include "tier0/vprof.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: World Decal TE
//-----------------------------------------------------------------------------
class C_TEWorldDecal : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TEWorldDecal, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();

					C_TEWorldDecal( void );
	virtual			~C_TEWorldDecal( void );

	virtual void	PostDataUpdate( DataUpdateType_t updateType );

	virtual void	Precache( void );

public:
	Vector			m_vecOrigin;
	int				m_nIndex;
};


//-----------------------------------------------------------------------------
// Networking 
//-----------------------------------------------------------------------------
IMPLEMENT_CLIENTCLASS_EVENT_DT(C_TEWorldDecal, DT_TEWorldDecal, CTEWorldDecal)
	RecvPropVector( RECVINFO(m_vecOrigin)),
	RecvPropInt( RECVINFO(m_nIndex)),
END_RECV_TABLE()


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
C_TEWorldDecal::C_TEWorldDecal( void )
{
	m_vecOrigin.Init();
	m_nIndex = 0;
}

C_TEWorldDecal::~C_TEWorldDecal( void )
{
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_TEWorldDecal::Precache( void )
{
}


//-----------------------------------------------------------------------------
// Shared code
//-----------------------------------------------------------------------------
static inline void RecordWorldDecal( const Vector *pos, int index )
{
	if ( !ToolsEnabled() )
		return;

	if ( clienttools->IsInRecordingMode() )
	{
		KeyValues *msg = new KeyValues( "TempEntity" );

 		msg->SetInt( "te", TE_WORLD_DECAL );
 		msg->SetString( "name", "TE_WorldDecal" );
		msg->SetFloat( "time", gpGlobals->curtime );
		msg->SetFloat( "originx", pos->x );
		msg->SetFloat( "originy", pos->y );
		msg->SetFloat( "originz", pos->z );
		msg->SetString( "decalname", effects->Draw_DecalNameFromIndex( index ) );

		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_TEWorldDecal::PostDataUpdate( DataUpdateType_t updateType )
{
	VPROF( "C_TEWorldDecal::PostDataUpdate" );

	if ( r_decals.GetInt() )
	{
		C_BaseEntity *ent = cl_entitylist->GetEnt( 0 );
		if ( ent )
		{
			bool bNoBlood = UTIL_IsLowViolence();
			bool bIsBlood = false;

			if ( bNoBlood )
			{
				const char *pchDecalName = decalsystem->GetDecalNameForIndex( m_nIndex );
				if ( pchDecalName && V_stristr( pchDecalName, "blood" ) )
				{
					bIsBlood = true;
				}
			}

			if ( !( bNoBlood && bIsBlood ) )
			{
				effects->DecalShoot( m_nIndex, 0, ent->GetModel(), ent->GetAbsOrigin(), ent->GetAbsAngles(), m_vecOrigin, 0, 0 );
			}
		}
	}
	RecordWorldDecal( &m_vecOrigin, m_nIndex );
}


//-----------------------------------------------------------------------------
// Client-side effects
//-----------------------------------------------------------------------------
void TE_WorldDecal( IRecipientFilter& filter, float delay, const Vector* pos, int index )
{
	if ( r_decals.GetInt() )
	{
		C_BaseEntity *ent = cl_entitylist->GetEnt( 0 );
		if ( ent )
		{
			effects->DecalShoot( index, 0, ent->GetModel(), ent->GetAbsOrigin(), ent->GetAbsAngles(), *pos, 0, 0 );
		}
	}
	RecordWorldDecal( pos, index );
}


void TE_WorldDecal( IRecipientFilter& filter, float delay, KeyValues *pKeyValues )
{
	Vector vecOrigin;
	vecOrigin.x = pKeyValues->GetFloat( "originx" );
	vecOrigin.y = pKeyValues->GetFloat( "originy" );
	vecOrigin.z = pKeyValues->GetFloat( "originz" );
	const char *pDecalName = pKeyValues->GetString( "decalname" );

	TE_WorldDecal( filter, 0.0f, &vecOrigin, effects->Draw_DecalIndexFromName( (char*)pDecalName ) );
}

#if defined( L4D2 )
// Development fixture: emit real world impacts (not synthetic vertex data)
// across visible brush faces and several subrects sharing one material page.
CON_COMMAND_F( cl_test_world_decal_batches, "Project a grid of world decals, or place one at x y z", FCVAR_CHEAT )
{
	if ( args.ArgC() != 1 && args.ArgC() != 4 ) return;
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	C_BaseEntity *world = cl_entitylist->GetEnt( 0 );
	if ( !player || !world ) return;
	Vector origin = player->EyePosition();
	QAngle angles; engine->GetViewAngles( angles );
	Vector forward, right, up;
	AngleVectors( angles, &forward, &right, &up );
	const int ids[] = { effects->Draw_DecalIndexFromName( "decals/concrete/concrete1_subrect" ),
		effects->Draw_DecalIndexFromName( "decals/concrete/concrete2_subrect" ), effects->Draw_DecalIndexFromName( "decals/concrete/concrete3_subrect" ) };
	if ( args.ArgC() == 4 )
	{
		Vector pos( V_atof( args[1] ), V_atof( args[2] ), V_atof( args[3] ) );
		if ( !pos.IsValid() ) return;
		effects->DecalShoot( ids[0], 0, world->GetModel(), world->GetAbsOrigin(), world->GetAbsAngles(), pos, NULL, 0 );
		return;
	}
	int shots = 0;
	static unsigned sequence;
	for ( int k = 0; k < 96; ++k )
	{
		const unsigned s = ++sequence;
		Vector dir = forward + right * ( 2.f * ( ( s * 73u ) % 251 ) / 250.f - 1.f )
			+ up * ( 1.4f * ( ( s * 107u ) % 257 ) / 256.f - .7f );
		trace_t tr;
		UTIL_TraceLine( origin, origin + 3072.f * dir, MASK_SOLID_BRUSHONLY, player, COLLISION_GROUP_NONE, &tr );
		if ( !tr.DidHitWorld() || tr.startsolid || ( tr.surface.flags & ( SURF_SKY | SURF_NODECALS ) ) ) continue;
		effects->DecalShoot( ids[s % 3], 0, world->GetModel(), world->GetAbsOrigin(), world->GetAbsAngles(), tr.endpos, NULL, 0 );
		++shots;
	}
	Msg( "Decal batch fixture: %d world impacts\n", shots );
}
#endif
