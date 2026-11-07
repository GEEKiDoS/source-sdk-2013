//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/env_weaponfire.h"
#include "filters.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "ammodef.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/NextBotInterface.h"
#include <math.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( env_weaponfire, CEnvWeaponfire );

IMPLEMENT_SERVERCLASS_ST( CEnvWeaponfire, DT_EnvWeaponfire )
    SendPropInt( SENDINFO( m_iWeaponType ), 3, SPROP_UNSIGNED ),
    SendPropInt( SENDINFO( m_iShotsTaken ), 4, SPROP_UNSIGNED ),
    SendPropVector( SENDINFO( m_vecShotDest ), -1, SPROP_COORD_MP ),
END_SEND_TABLE()

BEGIN_DATADESC( CEnvWeaponfire )
    DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "StartDisabled" ),
    DEFINE_KEYFIELD( m_bIgnorePlayers, FIELD_BOOLEAN, "IgnorePlayers" ),
    DEFINE_KEYFIELD( m_iWeaponType, FIELD_INTEGER, "WeaponType" ),
    DEFINE_KEYFIELD( m_flTargetArc, FIELD_FLOAT, "TargetArc" ),
    DEFINE_KEYFIELD( m_flTargetRange, FIELD_FLOAT, "TargetRange" ),
    DEFINE_KEYFIELD( m_flDamageMod, FIELD_FLOAT, "DamageMod" ),
    DEFINE_KEYFIELD( m_iFilterName, FIELD_STRING, "filtername" ),
    DEFINE_KEYFIELD( m_iEnemyTeam, FIELD_INTEGER, "TargetTeam" ),
    DEFINE_THINKFUNC( WeaponfireThink ),
    DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
    DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
END_DATADESC()


CEnvWeaponfire::FindTarget::FindTarget( CEnvWeaponfire *owner ) : m_owner( owner ), m_target( NULL )
{}

bool CEnvWeaponfire::FindTarget::operator()( INextBot *bot )
{
        CBaseCombatCharacter *entity = bot->GetEntity();
        if ( !m_owner->IsValidTarget( entity ) ) return true;
        m_target = entity;
        return false;
    }


CEnvWeaponfire::CEnvWeaponfire()
{
    m_flTargetArc = 40.0f;
    m_flTargetRange = 3600.0f;
    m_bDisabled = false;
    m_bIgnorePlayers = false;
    m_iEnemyTeam = 3;
    m_iWeaponType = 1;
    m_iBurstSize = 1;
    m_iShotsTaken = 0;
    m_flDamageMod = 1.0f;
    m_iFilterName = NULL_STRING;
    // Handles construct invalid. Other scalar/vector storage retains native allocation zero.
}
void CEnvWeaponfire::Spawn()
{
    BaseClass::Spawn();
    SetSolid( SOLID_NONE );
    AddEffects( EF_NODRAW );
    // Explicit comparisons preserve native NaN acceptance, unlike a generic clamp.
    if ( m_flTargetArc > 179.0f ) m_flTargetArc = 179.0f;
    else if ( m_flTargetArc < 1.0f ) m_flTargetArc = 1.0f;
    m_angOriginal = GetAbsAngles();
    AngleVectors( m_angOriginal, &m_vecOriginalForward );
    const char *ammo = "AMMO_TYPE_ASSAULTRIFLE";
    switch ( m_iWeaponType )
    {
    case 2: m_nWeaponID = 6; ammo = "AMMO_TYPE_HUNTINGRIFLE"; break;
    case 3: m_nWeaponID = 4; ammo = "AMMO_TYPE_AUTOSHOTGUN"; break;
    default:
        if ( m_iWeaponType != 1 )
        {
            Warning( "env_weaponfire %s has unknown weapon type %d\n", STRING( GetEntityName() ), (int)m_iWeaponType );
            m_iWeaponType = 1;
        }
        m_nWeaponID = 5;
        break;
    }
    m_nAmmoType = GetAmmoDef()->Index( ammo );
    DispatchUpdateTransmitState();
}
int CEnvWeaponfire::UpdateTransmitState()
{
    return SetTransmitState( m_bDisabled ? FL_EDICT_DONTSEND : FL_EDICT_ALWAYS );
}
void CEnvWeaponfire::Activate()
{
    BaseClass::Activate();
    if ( !m_bDisabled )
    {
        SetThink( &CEnvWeaponfire::WeaponfireThink );
        SetNextThink( (float)( (double)RandomFloat( 0.5f, 1.0f ) + gpGlobals->curtime ) );
    }
    if ( m_iFilterName != NULL_STRING )
        m_hFilter = dynamic_cast< CBaseFilter * >( gEntList.FindEntityByName( NULL, m_iFilterName ) );
}
void CEnvWeaponfire::InputEnable( inputdata_t &data )
{
    m_bDisabled = false;
    SetThink( &CEnvWeaponfire::WeaponfireThink );
    SetNextThink( (float)( (double)RandomFloat( 0.0f, 0.5f ) + gpGlobals->curtime ) );
    DispatchUpdateTransmitState();
}
void CEnvWeaponfire::InputDisable( inputdata_t &data )
{
    m_bDisabled = true;
    SetThink( NULL );
    SetNextThink( TICK_NEVER_THINK );
    DispatchUpdateTransmitState();
}
#pragma float_control( precise, on, push )
bool CEnvWeaponfire::IsValidTarget( CBaseEntity *entity )
{
    if ( !entity || !entity->IsAlive() ) return false;
    CBasePlayer *player = entity->IsPlayer() ? ToBasePlayer( entity ) : NULL;
    CTerrorPlayer *terror = player ? dynamic_cast< CTerrorPlayer * >( player ) : NULL;
    if ( terror && terror->IsGhost() ) return false;
    if ( m_bIgnorePlayers && player && !player->IsFakeClient() ) return false;
    if ( m_iEnemyTeam != -1 && entity->GetTeamNumber() != m_iEnemyTeam ) return false;
    const Vector origin = GetAbsOrigin();
    const Vector target = entity->WorldSpaceCenter();
    const Vector delta = target - origin;
    const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
    if ( distance > m_flTargetRange ) return false;
    const float reciprocal = 1.0f / distance;
    const Vector direction( reciprocal * delta.x, reciprocal * delta.y, reciprocal * delta.z );
    const double dot = ( (double)m_vecOriginalForward.y * direction.y +
        (double)m_vecOriginalForward.x * direction.x ) + (double)m_vecOriginalForward.z * direction.z;
    // Do not normalize with epsilon, clamp dot, or invert this > comparison: NaN passes native arc gate.
    if ( acos( dot ) * (double)57.29578f > m_flTargetArc ) return false;
    if ( m_hFilter && !m_hFilter->PassesFilter( this, entity ) ) return false;
    trace_t trace;
    UTIL_TraceLine( origin, target, 0x06004003u, this, COLLISION_GROUP_NONE, &trace );
    return !trace.DidHit() || trace.m_pEnt == entity;
}
bool CEnvWeaponfire::FindEnemy()
{
    if ( IsValidTarget( m_hTarget.Get() ) ) return true;
    m_hTarget = NULL;
    // Native prioritizes ascending-index non-NextBot players, then the actual NextBot registration list.
    for ( int i = 1; i <= gpGlobals->maxClients; ++i )
    {
        CBasePlayer *player = UTIL_PlayerByIndex( i );
        if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() ) continue;
        CCSPlayer *cs = dynamic_cast< CCSPlayer * >( player );
        if ( cs && cs->State_Get() == STATE_PICKINGTEAM ) continue;
        if ( player->MyNextBotPointer() || !IsValidTarget( player ) ) continue;
        m_hTarget = player;
        return true;
    }
    FindTarget find( this );
    TheNextBots().ForEachBot( find );
    m_hTarget = find.m_target;
    return m_hTarget.Get() != NULL;
}
void CEnvWeaponfire::ResetBurst()
{
    m_iShotsTaken = 0;
    switch ( m_iWeaponType )
    {
    case 1: m_iBurstSize = RandomInt( 3, 6 ); break;
    case 2: m_iBurstSize = RandomInt( 1, 2 ); break;
    case 3: m_iBurstSize = RandomInt( 1, 3 ); break;
    default: m_iBurstSize = 1; break;
    }
}
void CEnvWeaponfire::FirePellet( const CTerrorWeaponInfo &info, const Vector &direction, Vector &destination )
{
    const Vector origin = GetAbsOrigin();
    destination = origin + direction * info.m_flRange;
    trace_t trace;
    UTIL_TraceLine( origin, destination, 0x06004003u, this, COLLISION_GROUP_NONE, &trace );
    if ( trace.fraction < 1.0f || trace.startsolid || trace.allsolid ) destination = trace.endpos;
    CBaseEntity *hit = trace.m_pEnt;
    if ( !hit || ( m_iEnemyTeam != -1 && hit->GetTeamNumber() != m_iEnemyTeam ) ||
        ( m_hFilter && !m_hFilter->PassesFilter( this, hit ) ) ) return;
    ClearMultiDamage();
    CTakeDamageInfo damage( this, this, (float)info.m_iDamage * m_flDamageMod, (int)0x80000002u );
    CalculateMeleeDamageForce( &damage, direction, trace.endpos, 1.0f );
    damage.SetAmmoType( m_nAmmoType );
    hit->DispatchTraceAttack( damage, direction, &trace );
    TraceAttackToTriggers( damage, trace.startpos, trace.endpos, direction );
    ApplyMultiDamage();
}
void CEnvWeaponfire::Fire()
{
    const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( m_nWeaponID );
    const Vector origin = GetAbsOrigin();
    const bool head = RandomInt( 0, 4 ) == 0;
    CBaseEntity *target = m_hTarget.Get();
    // Approved null-receiver adaptation: only aim/pellet-dependent work is omitted.
    if ( !target || !info ) return;
    Vector destination = head ? target->HeadTarget( origin ) : target->BodyTarget( origin, true );
    const Vector delta = destination - origin;
    const float length = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
    const float reciprocal = 1.0f / length;
    const Vector direction( reciprocal * delta.x, reciprocal * delta.y, reciprocal * delta.z );
    QAngle aim;
    VectorAngles( direction, aim );
    float ratio = 1.0f;
    if ( info->m_flPelletScatterYaw != 0.0f && info->m_flPelletScatterPitch != 0.0f )
        ratio = info->m_flPelletScatterPitch / info->m_flPelletScatterYaw;
    if ( info->m_iBullets == 1 )
    {
        const float spread = (float)(int)m_iShotsTaken * info->m_flSpreadPerShot;
        aim.y = (float)( (double)RandomFloat( -spread, spread ) + aim.y );
        aim.x = (float)( (double)RandomFloat( -spread, spread ) + aim.x );
    }
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
        FirePellet( *info, pelletDirection, destination );
        if ( i == 0 ) m_vecShotDest = destination;
    }
}
void CEnvWeaponfire::WeaponfireThink()
{
    if ( !FindEnemy() )
    {
        SetNextThink( (float)( (double)RandomFloat( 0.25f, 0.5f ) + gpGlobals->curtime ) );
        m_iShotsTaken = 0;
        return;
    }
    m_iShotsTaken = (int)( (unsigned int)(int)m_iShotsTaken + 1u );
    Fire();
    const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( m_nWeaponID );
    if ( m_iShotsTaken < m_iBurstSize )
    {
        if ( IsValidTarget( m_hTarget.Get() ) )
        {
            if ( info ) SetNextThink( (float)( (double)gpGlobals->curtime + info->m_flCycleTime ) );
        }
        else SetNextThink( (float)( (double)RandomFloat( 0.25f, 0.5f ) + gpGlobals->curtime ) );
    }
    else
    {
        ResetBurst();
        if ( info )
            SetNextThink( (float)( (double)RandomFloat( info->m_flCycleTime * 2.0f, 1.0f ) + gpGlobals->curtime ) );
    }
}
#pragma float_control( pop )
