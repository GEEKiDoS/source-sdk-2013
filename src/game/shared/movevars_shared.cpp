//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//


#include "cbase.h"
#include "movevars_shared.h"

#if defined( TF_CLIENT_DLL ) || defined( TF_DLL )
#include "tf_gamerules.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "movevars_shared_console_server_cvars.inc"
#include "tier0/memdbgon.h"

// some cvars used by player movement system
#if defined( HL2_DLL ) || defined( HL2_CLIENT_DLL )
#define DEFAULT_GRAVITY_STRING	"600"
#else
#define DEFAULT_GRAVITY_STRING	"800"
#endif

float GetCurrentGravity( void )
{
#if defined( TF_CLIENT_DLL ) || defined( TF_DLL )
	if ( TFGameRules() )
	{
		return ( sv_gravity.GetFloat() * TFGameRules()->GetGravityMultiplier() );
	}
#endif 

	return sv_gravity.GetFloat();
}

ConVar	sv_gravity		( "sv_gravity", DEFAULT_GRAVITY_STRING, 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "World gravity." );

#if defined( DOD_DLL ) || defined( CSTRIKE_DLL ) || defined( HL1MP_DLL )
ConVar	sv_stopspeed	( "sv_stopspeed","100", FCVAR_NOTIFY | FCVAR_REPLICATED, "Minimum stopping speed when on ground." );
#elif defined( L4D2 )
ConVar	sv_stopspeed	( "sv_stopspeed","75", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "Minimum stopping speed when on ground." );
#else
ConVar	sv_stopspeed	( "sv_stopspeed","100", FCVAR_NOTIFY | FCVAR_REPLICATED, "Minimum stopping speed when on ground." );
#endif // DOD_DLL || CSTRIKE_DLL

ConVar	sv_noclipaccelerate( "sv_noclipaccelerate", "5", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00002188
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002184
#else
	FCVAR_NOTIFY | FCVAR_ARCHIVE | FCVAR_REPLICATED
#endif
);
ConVar	sv_noclipspeed	( "sv_noclipspeed", "5", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00002188
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002184
#else
	FCVAR_ARCHIVE | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
);
ConVar	sv_specaccelerate( "sv_specaccelerate", "5", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00002188
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002184
#else
	FCVAR_NOTIFY | FCVAR_ARCHIVE | FCVAR_REPLICATED
#endif
);
ConVar	sv_specspeed	( "sv_specspeed", "3", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00002188
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002184
#else
	FCVAR_ARCHIVE | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
);
ConVar	sv_specnoclip	( "sv_specnoclip", "1", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00002188
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002184
#else
	FCVAR_ARCHIVE | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
);

#if defined( CSTRIKE_DLL ) || defined( HL1MP_DLL )
ConVar	sv_maxspeed		( "sv_maxspeed", "320", FCVAR_NOTIFY | FCVAR_REPLICATED);
#else
ConVar	sv_maxspeed		( "sv_maxspeed", "320", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
);
#endif // CSTRIKE_DLL

#ifdef _XBOX
	ConVar	sv_accelerate	( "sv_accelerate", "7", FCVAR_NOTIFY | FCVAR_REPLICATED);
#else

#if defined( CSTRIKE_DLL ) || defined( HL1MP_DLL )
	ConVar	sv_accelerate	( "sv_accelerate", "10", FCVAR_NOTIFY | FCVAR_REPLICATED);
#elif defined( L4D2 )
	ConVar	sv_accelerate	( "sv_accelerate", "5", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
);
#else
	ConVar	sv_accelerate	( "sv_accelerate", "10", FCVAR_NOTIFY | FCVAR_REPLICATED);
#endif // CSTRIKE_DLL
	
#endif//_XBOX

#if defined( CSTRIKE_DLL ) || defined( HL1MP_DLL )
ConVar	sv_airaccelerate(  "sv_airaccelerate", "10", FCVAR_NOTIFY | FCVAR_REPLICATED);    
ConVar	sv_wateraccelerate(  "sv_wateraccelerate", "10", FCVAR_NOTIFY | FCVAR_REPLICATED);     
ConVar	sv_waterfriction(  "sv_waterfriction", "1", FCVAR_NOTIFY | FCVAR_REPLICATED);      
ConVar	sv_footsteps	( "sv_footsteps", "1", FCVAR_NOTIFY | FCVAR_REPLICATED, "Play footstep sound for players" );
ConVar	sv_rollspeed	( "sv_rollspeed", "200", FCVAR_NOTIFY | FCVAR_REPLICATED);
ConVar	sv_rollangle	( "sv_rollangle", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Max view roll angle");
#else
ConVar	sv_airaccelerate(  "sv_airaccelerate", "10", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );    
ConVar	sv_wateraccelerate(  "sv_wateraccelerate", "10", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );     
ConVar	sv_waterfriction(  "sv_waterfriction", "1", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );      
ConVar	sv_footsteps	( "sv_footsteps", "1", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "Play footstep sound for players" );
#if defined( CLIENT_DLL ) && defined( L4D2 )
ConVar sv_rollspeed( "sv_rollspeed", "200", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );
ConVar sv_rollangle( "sv_rollangle", "0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "Max view roll angle" );
#else
ConVar sv_rollspeed( "sv_rollspeed", "200", 
#if defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );
ConVar sv_rollangle( "sv_rollangle", "0", 
#if defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "Max view roll angle" );
#endif
#endif // CSTRIKE_DLL

#if defined( DOD_DLL ) || defined( CSTRIKE_DLL ) || defined( HL1MP_DLL )
ConVar	sv_friction		( "sv_friction","4", FCVAR_NOTIFY | FCVAR_REPLICATED, "World friction." );
#else
ConVar	sv_friction		( "sv_friction","4", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "World friction." );
#endif // DOD_DLL || CSTRIKE_DLL

#if defined( CSTRIKE_DLL ) || defined( HL1MP_DLL )
ConVar	sv_bounce		( "sv_bounce","0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Bounce multiplier for when physically simulated objects collide with other objects." );
ConVar	sv_maxvelocity	( "sv_maxvelocity","3500", FCVAR_REPLICATED, "Maximum speed any ballistically moving object is allowed to attain per axis." );
ConVar	sv_stepsize		( "sv_stepsize","18", FCVAR_NOTIFY | FCVAR_REPLICATED );
ConVar	sv_backspeed	( "sv_backspeed", "0.6", FCVAR_ARCHIVE | FCVAR_REPLICATED, "How much to slow down backwards motion" );
ConVar  sv_waterdist	( "sv_waterdist","12", FCVAR_REPLICATED, "Vertical view fixup when eyes are near water plane." );
#else
ConVar	sv_bounce		( "sv_bounce","0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "Bounce multiplier for when physically simulated objects collide with other objects." );
ConVar	sv_maxvelocity	( "sv_maxvelocity","3500", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000200A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, "Maximum speed any ballistically moving object is allowed to attain per axis." );
ConVar	sv_stepsize		( "sv_stepsize","18", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );
ConVar	sv_backspeed	( "sv_backspeed", "0.6", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000200A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002006
#else
	FCVAR_ARCHIVE | FCVAR_REPLICATED
#endif
, "How much to slow down backwards motion" );
ConVar  sv_waterdist	( "sv_waterdist","12", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000200A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, "Vertical view fixup when eyes are near water plane." );
#endif // CSTRIKE_DLL

ConVar	sv_skyname		( "sv_skyname", "sky_urb01", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00002088
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002084
#else
	FCVAR_ARCHIVE | FCVAR_REPLICATED
#endif
, "Current name of the skybox texture" );

// Vehicle convars
ConVar r_VehicleViewDampen( "r_VehicleViewDampen", "1", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00006108
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00006104
#else
	FCVAR_CHEAT | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );

// Jeep convars
ConVar r_JeepViewDampenFreq( "r_JeepViewDampenFreq", "7.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00006108
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00006104
#else
	FCVAR_CHEAT | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );
ConVar r_JeepViewDampenDamp( "r_JeepViewDampenDamp", "1.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00006108
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00006104
#else
	FCVAR_CHEAT | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
);
ConVar r_JeepViewZHeight( "r_JeepViewZHeight", "10.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00006108
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00006104
#else
	FCVAR_CHEAT | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );

// Airboat convars
ConVar r_AirboatViewDampenFreq( "r_AirboatViewDampenFreq", "7.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00006108
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00006104
#else
	FCVAR_CHEAT | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );
ConVar r_AirboatViewDampenDamp( "r_AirboatViewDampenDamp", "1.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00006108
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00006104
#else
	FCVAR_CHEAT | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
);
ConVar r_AirboatViewZHeight( "r_AirboatViewZHeight", "0.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00006108
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00006104
#else
	FCVAR_CHEAT | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
 );
