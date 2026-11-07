//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/c_env_weaponfire.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "ammodef.h"
#include "IEffects.h"
#include "fx.h"
#include "particle_parse.h"
#include <math.h>
#include "tier0/memdbgon.h"

// Native client spells its receive table EnvWeaponFire; server spells EnvWeaponfire.
IMPLEMENT_CLIENTCLASS_DT( C_EnvWeaponFire, DT_EnvWeaponFire, CEnvWeaponFire )
	RecvPropInt( RECVINFO( m_iWeaponType ) ),
	RecvPropInt( RECVINFO( m_iShotsTaken ) ),
	RecvPropVector( RECVINFO( m_vecShotDest ) ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_EnvWeaponFire )
	DEFINE_PRED_FIELD( m_iShotsTaken, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_vecShotDest, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()

C_EnvWeaponFire::C_EnvWeaponFire()
{
    m_iWeaponType = 1;
    m_iShotsTaken = 0;
    m_iLastShotsTaken = 0;
    m_nWeaponID = 5;
    m_nAmmoType = GetAmmoDef()->Index( "AMMO_TYPE_ASSAULTRIFLE" );
}
void C_EnvWeaponFire::SelectWeapon()
{
    const char *ammo = "AMMO_TYPE_ASSAULTRIFLE";
    switch ( m_iWeaponType )
    {
    case 2: m_nWeaponID = 6; ammo = "AMMO_TYPE_HUNTINGRIFLE"; break;
    case 3: m_nWeaponID = 4; ammo = "AMMO_TYPE_AUTOSHOTGUN"; break;
    default:
        if ( m_iWeaponType != 1 )
        {
            Warning( "client env_weaponfire has unknown weapon type %d\n", m_iWeaponType );
            m_iWeaponType = 1;
        }
        m_nWeaponID = 5;
        break;
    }
    m_nAmmoType = GetAmmoDef()->Index( ammo );
}
void C_EnvWeaponFire::OnDataChanged( DataUpdateType_t type )
{
    if ( type == DATA_UPDATE_CREATED ) SelectWeapon();
    SetNextClientThink( gpGlobals->curtime );
    BaseClass::OnDataChanged( type );
}
void C_EnvWeaponFire::ClientThink()
{
    if ( m_iShotsTaken > 0 && m_iShotsTaken != m_iLastShotsTaken ) FireEffects();
    m_iLastShotsTaken = m_iShotsTaken;
    BaseClass::ClientThink(); // Native does not re-arm here; each network update schedules a think.
}
#pragma float_control( precise, on, push )
void C_EnvWeaponFire::FireEffects()
{
    const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( m_nWeaponID );
    const Vector origin = GetAbsOrigin();
    const Vector delta = m_vecShotDest - origin;
    const float length = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
    const float reciprocal = 1.0f / length;
    const Vector direction( reciprocal * delta.x, reciprocal * delta.y, reciprocal * delta.z );
    QAngle aim;
    VectorAngles( direction, aim );
    g_pEffects->MuzzleFlash( origin, aim, 1.0f, 1 );
    // Metadata NULL only skips sound and metadata-dependent tracing, not the native flash.
    if ( !info ) return;
    EmitSound( info->aShootSounds[SINGLE], 0.0f );
    float ratio = 1.0f;
    if ( info->m_flPelletScatterYaw != 0.0f && info->m_flPelletScatterPitch != 0.0f )
        ratio = info->m_flPelletScatterPitch / info->m_flPelletScatterYaw;
    for ( int i = 0; i < info->m_iBullets; ++i )
    {
        QAngle pellet = aim;
        if ( i != 0 && info->m_iBullets > 1 )
        {
            const float radius = RandomFloat( -info->m_flPelletScatterYaw, info->m_flPelletScatterYaw );
            const double degrees = RandomFloat( 0.0f, 180.0f );
            const float theta = (float)( ( degrees + degrees ) * 3.141592653589793 * 0.005555555555555556 );
            const float cosine = (float)cos( (double)theta );
            const float sine = (float)sin( (double)theta );
            pellet.x = (float)( (double)sine * radius * ratio + pellet.x );
            pellet.y = (float)( (double)radius * cosine + pellet.y );
        }
        Vector pelletDirection;
        AngleVectors( pellet, &pelletDirection );
        trace_t trace;
        UTIL_TraceLine( origin, origin + pelletDirection * info->m_flRange, 0x46004003u, this, COLLISION_GROUP_NONE, &trace );
        UTIL_ParticleTracer( "weapon_tracers", origin, trace.endpos, 0, -1, true );
        if ( trace.fraction < 1.0f || trace.startsolid || trace.allsolid ) ImpactTrace( &trace, (int)0x80000002u, NULL );
    }
}
#pragma float_control( pop )
