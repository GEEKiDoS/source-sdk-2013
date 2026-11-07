//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/commentary_entities.h"
#include "basecombatcharacter.h"
#include "basecombatweapon.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/director/infected_population.h"
#include "l4d2/director/zombiemanager.h"
#include "utldict.h"
#include "tier1/KeyValues.h"
#include "datacache/imdlcache.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( commentary_dummy, CCommentaryDummy );

BEGIN_DATADESC( CCommentaryDummy )
    DEFINE_KEYFIELD( m_iEyeHeight, FIELD_INTEGER, "EyeHeight" ),
    DEFINE_FIELD( m_flHeadYaw, FIELD_FLOAT ),
    DEFINE_FIELD( m_flHeadPitch, FIELD_FLOAT ),
    DEFINE_FIELD( m_flLastHeadUpdate, FIELD_FLOAT ),
    DEFINE_FIELD( m_headYawPoseParameter, FIELD_INTEGER ),
    // Native descriptor bug: duplicate name/offset with INTEGER; pitch-pose index is NOT saved.
    DEFINE_FIELD( m_flLastHeadUpdate, FIELD_INTEGER ),
    DEFINE_KEYFIELD( m_iszStartingAnim, FIELD_STRING, "StartingAnim" ),
    DEFINE_KEYFIELD( m_iszStartingWeapons, FIELD_STRING, "StartingWeapons" ),
    DEFINE_KEYFIELD( m_iszHeadYawPoseParam, FIELD_STRING, "HeadYawPoseParam" ),
    DEFINE_KEYFIELD( m_iszHeadPitchPoseParam, FIELD_STRING, "HeadPitchPoseParam" ),
    DEFINE_KEYFIELD( m_bLookAtPlayers, FIELD_BOOLEAN, "LookAtPlayers" ),
    DEFINE_INPUTFUNC( FIELD_STRING, "SetAnimation", InputSetAnimation ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( commentary_zombie_spawner, CCommentaryZombieSpawner );

BEGIN_DATADESC( CCommentaryZombieSpawner )
    DEFINE_INPUTFUNC( FIELD_STRING, "SpawnZombie", InputSpawnZombie ),
    DEFINE_OUTPUT( m_OnSpawnedZombieDeath, "OnSpawnedZombieDeath" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( info_infected_zoo_puppet, CInfectedZooPuppet );

LINK_ENTITY_TO_CLASS( info_infected_zoo_maker, CInfectedZooMaker );

BEGIN_DATADESC( CInfectedZooMaker )
    DEFINE_INPUTFUNC( FIELD_VOID, "SpawnAll", Input_SpawnAll ),
END_DATADESC()


void CCommentaryDummy::HandleAnimEvent( animevent_t *event )
{}

void CInfectedZooPuppet::Event_Killed( const CTakeDamageInfo &info )
{}

void CInfectedZooPuppet::Touch( CBaseEntity *entity )
{}

void CInfectedZooPuppet::HandleAnimEvent( animevent_t *event )
{}

void CInfectedZooPuppet::Ignite( float lifetime, bool npcOnly, float size, bool designer )
{}

int CInfectedZooPuppet::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{ return 0; }

int CInfectedZooPuppet::OnTakeDamage_Dying( const CTakeDamageInfo &info )
{ return 0; }

void CInfectedZooPuppet::OnNavAreaChanged( CNavArea *entered, CNavArea *left )
{}

void CInfectedZooPuppet::Ignite( float lifetime, CBaseEntity *attacker )
{}

CInfectedZooMaker::CInfectedZooMaker() : m_models( k_eDictCompareTypeCaseInsensitive, 0, 32 )
{}

void CCommentaryDummy::Spawn()
{
    BaseClass::Spawn();
    SetViewOffset( Vector( 0.0f, 0.0f, (float)m_iEyeHeight ) );
    const char *weapons = STRING( m_iszStartingWeapons );
    if ( weapons[0] )
    {
        const int length = Q_strlen( weapons ) + 1;
        char *text = (char *)stackalloc( length );
        Q_memcpy( text, weapons, length );
        char *token = text;
        for ( char *cursor = text; cursor < text + length; ++cursor )
        {
            if ( *cursor != ' ' && *cursor != ',' && *cursor != ';' && *cursor != 0 ) continue;
            *cursor = 0;
            CBaseEntity *entity = CreateEntityByName( token );
            if ( entity )
            {
                entity->SetLocalOrigin( EyePosition() );
                entity->AddSpawnFlags( 0x40000000 );
                DispatchSpawn( entity, true );
                CBaseCombatWeapon *weapon = dynamic_cast< CBaseCombatWeapon * >( entity );
                if ( weapon ) Weapon_Equip( weapon ); // Only unchecked-type-dependent native equip is skipped.
            }
            token = cursor + 1;
        }
    }
    const char *model = STRING( GetModelName() );
    PrecacheModel( model );
    Precache();
    SetModel( model );
    const int sequence = LookupSequence( STRING( m_iszStartingAnim ) );
    if ( sequence >= 0 ) ResetSequence( sequence );
}
void CCommentaryDummy::InputSetAnimation( inputdata_t &data )
{
    const int sequence = LookupSequence( data.value.String() );
    if ( sequence >= 0 ) ResetSequence( sequence );
}
void CCommentaryDummy::OnSequenceChanged( int oldSequence )
{
    // S036AB0 invokes slot204 with the old sequence; this is NOT OnNewModel.
    SetAnimTime( gpGlobals->curtime );
    ResetClientsideFrame();
    SetPlaybackRate( 1.0f );
    SetCycle( 0.0f );
    m_headYawPoseParameter = -1;
    m_headPitchPoseParameter = -1;
    if ( m_bLookAtPlayers )
    {
        m_headYawPoseParameter = LookupPoseParameter( STRING( m_iszHeadYawPoseParam ) );
        m_headPitchPoseParameter = LookupPoseParameter( STRING( m_iszHeadPitchPoseParam ) );
    }
    SetThink( &CCommentaryDummy::DummyThink );
    if ( gpGlobals->curtime >= GetNextThink() ) SetNextThink( gpGlobals->curtime + 0.1f );
}
#pragma float_control( precise, on, push )
Vector CCommentaryDummy::FindLookTarget()
{
    Vector forward;
    GetVectors( &forward, NULL, NULL );
    const Vector eye = EyePosition();
    float closest = FLT_MAX;
    CBasePlayer *winner = NULL;
    for ( int i = 1; i <= gpGlobals->maxClients; ++i )
    {
        CBasePlayer *player = UTIL_PlayerByIndex( i );
        if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() ) continue;
        CCSPlayer *cs = dynamic_cast< CCSPlayer * >( player );
        if ( cs && cs->State_Get() == STATE_PICKINGTEAM ) continue;
        if ( player->GetTeamNumber() != 2 ) continue;
        Vector delta = player->EyePosition() - eye;
        const float distance = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
        if ( !( closest > distance ) ) continue;
        VectorNormalize( delta );
        const float facing = ( forward.y * delta.y + delta.x * forward.x ) + forward.z * delta.z;
        if ( facing >= 0.2f ) { winner = player; closest = distance; }
    }
    return winner ? winner->EyePosition() : eye + forward * 64.0f;
}
void CCommentaryDummy::UpdateHead( const Vector &target )
{
    const Vector delta = target - EyePosition();
    double yaw = 0.0;
    if ( delta.y != 0.0f || delta.x != 0.0f )
    {
        yaw = atan2( (double)delta.y, (double)delta.x ) * (double)57.29578f;
        if ( yaw < 0.0 ) { const float spill = (float)yaw; yaw = spill + 360.0f; }
    }
    const double relativeYaw = yaw - GetLocalAngles().y;
    float desiredYaw = (float)relativeYaw;
    if ( relativeYaw > 180.0 ) desiredYaw -= 360.0f;
    if ( desiredYaw < -180.0f ) desiredYaw += 360.0f;
    float blend = ( gpGlobals->curtime - m_flLastHeadUpdate ) * 2.0f;
    m_flLastHeadUpdate = gpGlobals->curtime;
    if ( blend > 1.0f ) blend = 1.0f; // No lower clamp.
    m_flHeadYaw = ( desiredYaw - m_flHeadYaw ) * blend + m_flHeadYaw;
    SetPoseParameter( m_headYawPoseParameter, m_flHeadYaw );
    const float length = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
    // Native uses FULL 3D length in atan2(dz/length,1), not horizontal length or asin.
    const double pitch = -atan2( (double)delta.z / length, 1.0 ) * (double)57.29578f;
    m_flHeadPitch = (float)( (double)m_flHeadPitch + ( pitch - m_flHeadPitch ) * blend );
    SetPoseParameter( m_headPitchPoseParameter, m_flHeadPitch );
}
void CCommentaryDummy::DummyThink()
{
    if ( IsSequenceFinished() && !SequenceLoops() ) { SetCycle( 0.0f ); ResetSequenceInfo(); }
    const float cycle = GetCycle();
    const double remaining = (double)SequenceDuration( GetSequence() ) * ( 1.0 - cycle );
    const float spill = (float)remaining;
    const float interval = remaining > (double)0.1f ? 0.1f : spill;
    SetNextThink( gpGlobals->curtime + interval );
    StudioFrameAdvance();
    DispatchAnimEvents( this );
    const Vector target = FindLookTarget(); // Native computes this even with LookAtPlayers=false.
    if ( m_bLookAtPlayers ) { UpdateHead( target ); SetViewtarget( target ); }
}
#pragma float_control( pop )

void CCommentaryZombieSpawner::DeathNotice( CBaseEntity *victim )
{
    if ( victim && !victim->IsEFlagSet( EFL_KILLME ) ) m_OnSpawnedZombieDeath.FireOutput( victim, this );
}
void CCommentaryZombieSpawner::InputSpawnZombie( inputdata_t &data )
{
    const char *input = data.value.String();
    if ( !input || !input[0] ) return;
    const int length = Q_strlen( input ) + 1;
    char *type = (char *)stackalloc( length );
    Q_memcpy( type, input, length );
    char *name = type;
    while ( *name && *name != ' ' && *name != ',' && *name != ';' ) ++name;
    if ( *name ) *name++ = 0; // Remainder is raw: do not trim, tokenize, or skip extra separators.
    const Vector position = GetAbsOrigin();
    const QAngle angles( 0.0f, GetAbsAngles().y, 0.0f );
    g_bZombieSpawnBypass = true;
    CBaseEntity *spawned = NULL;
    int zombieClass = 0;
    if ( !Q_stricmp( type, "boomer" ) ) zombieClass = 2;
    else if ( !Q_stricmp( type, "charger" ) ) zombieClass = 6;
    else if ( !Q_stricmp( type, "hunter" ) ) zombieClass = 3;
    else if ( !Q_stricmp( type, "jockey" ) ) zombieClass = 5;
    else if ( !Q_stricmp( type, "smoker" ) ) zombieClass = 1;
    else if ( !Q_stricmp( type, "spitter" ) ) zombieClass = 4;
    if ( zombieClass )
    {
        if ( TheZombieManager ) spawned = TheZombieManager->SpawnSpecial( zombieClass, position, angles );
    }
    else if ( !Q_stricmp( type, "tank" ) )
    {
        if ( TheZombieManager ) spawned = TheZombieManager->SpawnTank( position, angles );
    }
    else if ( !Q_stricmp( type, "witch" ) )
    {
        if ( TheZombieManager ) spawned = TheZombieManager->SpawnWitch( position, angles );
    }
    else if ( !Q_strnicmp( type, "common_", 7 ) )
    {
        spawned = CreateEntityByName( "infected" );
        if ( spawned )
        {
            spawned->SetAbsAngles( angles );
            spawned->SetAbsOrigin( position );
            char model[256];
            Q_snprintf( model, sizeof( model ), "models/infected/%s.mdl", type );
            PrecacheModel( model );
            spawned->SetModel( model );
            DispatchSpawn( spawned, true );
        }
    }
    if ( spawned )
    {
        if ( name[0] ) spawned->SetName( AllocPooledString( name ) );
        spawned->SetOwnerEntity( this );
    }
    g_bZombieSpawnBypass = false; // Native unconditionally clears, never restores prior state.
}

static ConVar sv_zoo_spacing( "sv_zoo_spacing", "64", 0x00000006, "How much space between models in a row" );
static ConVar sv_zoo_spacing_wrap( "sv_zoo_spacing_wrap", "80", 0x00000006, "How much space between wrapped rows" );
static ConVar sv_zoo_spacing_rows( "sv_zoo_spacing_rows", "192", 0x00000006, "How much space between subsequent rows" );
static ConVar sv_zoo_tint_loop( "sv_zoo_tint_loop", "1", 0x00000006, "1: Tint colors will loop across model variations in a row, 0: will use sv_zoo_tint_clr" );
static ConVar sv_zoo_tint_clr( "sv_zoo_tint_clr", "153 153 153 255", 0x00000006, "Tint color to force on all model variations (needs sv_zoo_tint_loop 0)" );
static ConVar sv_zoo_wrap_skin( "sv_zoo_wrap_skin", "1", 0x00000006, "Whether model rows wrap when changing skins" );
static ConVar sv_zoo_model_filter( "sv_zoo_model_filter", "", 0x00000006, "Only models matching the substring will be displayed" );

struct InfectedZooSelection
{
    int skin;
    int tint;
    int head;
    int upper;
    int lower;
};
static InfectedZooSelection *s_pZooCounts;
static InfectedZooSelection *s_pZooSelection;
static bool IsZooBodyPart( const char *name )
{
    return name[0] != 0 && atoi( name + 1 ) != -1 && name[0] != 'D';
}
static int CountZooBodyParts( CBaseAnimating *entity, int group )
{
    int count = 0;
    for ( int i = 0; i < entity->GetBodygroupCount( group ); ++i )
        if ( IsZooBodyPart( entity->GetBodygroupPartName( group, i ) ) ) ++count;
    return count;
}
static int FindZooBodyPart( CBaseAnimating *entity, int group, int ordinal )
{
    int count = 0;
    for ( int i = 0; i < entity->GetBodygroupCount( group ); ++i )
    {
        if ( IsZooBodyPart( entity->GetBodygroupPartName( group, i ) ) ) ++count;
        if ( count == ordinal ) return i;
    }
    return -1;
}
void CInfectedZooPuppet::Spawn()
{
    BaseClass::Spawn();
    CMDLCacheCriticalSection lock( mdlcache );
    CStudioHdr *studio = GetModelPtr();
    if ( !studio || !studio->IsValid() ) return; // Only studio-dependent native computation is skipped; lock still exits.
    int tintCount;
    const unsigned int *palette = GetInfectedColorPalette( &tintCount );
    static const char *groups[] = { "Head", "UpperBody", "LowerBody" };
    if ( s_pZooCounts )
    {
        s_pZooCounts->skin = studio->numskinfamilies();
        s_pZooCounts->tint = tintCount;
        int *counts[] = { &s_pZooCounts->head, &s_pZooCounts->upper, &s_pZooCounts->lower };
        for ( int i = 0; i < 3; ++i )
        {
            *counts[i] = 0;
            const int group = FindBodygroupByName( groups[i] );
            if ( group >= 0 ) *counts[i] = CountZooBodyParts( this, group );
        }
    }
    if ( s_pZooSelection )
    {
        m_nSkin = s_pZooSelection->skin;
        if ( sv_zoo_tint_loop.GetBool() )
        {
            const unsigned int color = palette[s_pZooSelection->tint];
            SetRenderColor( color & 255, ( color >> 8 ) & 255, ( color >> 16 ) & 255, color >> 24 );
        }
        else
        {
            int values[4];
            const int count = sscanf( sv_zoo_tint_clr.GetString(), "%d %d %d %d", &values[0], &values[1], &values[2], &values[3] );
            unsigned char rgba[4] = { 255, 255, 255, 255 };
            for ( int i = 0; i < count; ++i ) rgba[i] = (unsigned char)values[i];
            SetRenderColor( rgba[0], rgba[1], rgba[2], rgba[3] );
        }
        const int selections[] = { s_pZooSelection->head, s_pZooSelection->upper, s_pZooSelection->lower };
        for ( int i = 0; i < 3; ++i )
        {
            const int group = FindBodygroupByName( groups[i] );
            if ( group < 0 ) continue;
            const int part = FindZooBodyPart( this, group, selections[i] + 1 );
            if ( part >= 0 ) SetBodygroup( group, part );
        }
    }
}
void CInfectedZooMaker::Spawn()
{
    static ConVarRef noBots( "director_no_survivor_bots" );
    static ConVarRef noSpecials( "director_no_specials" );
    static ConVarRef noCull( "z_no_cull" );
    if ( noBots.IsValid() ) noBots.SetValue( 1 );
    if ( noSpecials.IsValid() ) noSpecials.SetValue( 1 );
    if ( noCull.IsValid() ) noCull.SetValue( 1 );
    engine->ServerCommand( "director_stop\nnb_delete_all\n" );
    BaseClass::Spawn();
    Precache();
}
void CInfectedZooMaker::Precache()
{
    KeyValues *population = GetInfectedPopulationDefinition();
    if ( !population ) return;
    for ( KeyValues *group = population->GetFirstSubKey(); group; group = group->GetNextKey() )
    {
        for ( KeyValues *model = group->GetFirstSubKey(); model; model = model->GetNextKey() )
        {
            const char *name = model->GetName();
            if ( Q_strnicmp( name, "common_", 7 ) || m_models.Find( name ) != m_models.InvalidIndex() ) continue;
            CFmtStr path( "models/infected/%s.mdl", name );
            PrecacheModel( path );
            m_models.Insert( name, 1 );
        }
    }
    SetThink( &CInfectedZooMaker::SpawnAllThink );
    SetNextThink( gpGlobals->curtime + 1.0f );
}
void CInfectedZooMaker::Input_SpawnAll( inputdata_t &data )
{
    engine->ServerCommand( "director_stop\nnb_delete_all\n" );
    SetThink( &CInfectedZooMaker::SpawnAllThink );
    SetNextThink( gpGlobals->curtime + 0.5f );
}
void CInfectedZooMaker::SpawnAllThink()
{
    SetThink( NULL );
    int count = 0;
    int row = 0;
    // No deletion occurs; native dictionary numeric insertion indices, NOT sorted tree order.
    for ( unsigned int i = 0; i < m_models.Count(); ++i ) count += SpawnModel( m_models.GetElementName( i ), &row );
    Msg( "[Zoo] spawned a total of %d different infected.\n", count );
}
int CInfectedZooMaker::SpawnModel( const char *name, int *row )
{
    const char *filter = sv_zoo_model_filter.GetString();
    if ( filter[0] && !strstr( name, filter ) ) return 0;
    CFmtStr model( "models/infected/%s.mdl", name );
    Msg( "[Zoo] Spawning `%s`...\n", model.Access() );
    InfectedZooSelection counts = { 0, 0, 0, 0, 0 };
    InfectedZooSelection selection = { 0, 0, 0, 0, 0 };
    s_pZooCounts = &counts;
    s_pZooSelection = &selection;
    Vector position( (float)*row, 0.0f, 0.0f );
    int previousSkin = 0;
    int count = 0;
    g_bZombieSpawnBypass = true;
    for ( ;; )
    {
        if ( count && sv_zoo_wrap_skin.GetBool() && previousSkin != selection.skin )
        {
            position.x += sv_zoo_spacing_wrap.GetFloat();
            position.y = 0.0f;
        }
        previousSkin = selection.skin;
        CBaseEntity *puppet = CreateEntityByName( "info_infected_zoo_puppet" );
        ++count; // Native counts a failed creation attempt too.
        if ( !puppet ) break;
        puppet->SetAbsAngles( vec3_angle );
        puppet->SetAbsOrigin( position );
        puppet->SetModel( model );
        DispatchSpawn( puppet, true );
        s_pZooCounts = NULL;
        // Defined native bug: tint advances independently; it is NOT a carry gate for head.
        if ( ++selection.tint >= counts.tint ) selection.tint = 0;
        if ( ++selection.head >= counts.head )
        {
            selection.head = 0;
            if ( ++selection.upper >= counts.upper )
            {
                selection.upper = 0;
                if ( ++selection.lower >= counts.lower )
                {
                    selection.lower = 0;
                    selection.skin += 4;
                    if ( selection.skin >= counts.skin ) { selection.skin = 0; break; }
                }
            }
        }
        position.y += sv_zoo_spacing.GetFloat();
    }
    g_bZombieSpawnBypass = false;
    Msg( "[Zoo] `%s` spawned %d variations.\n", model.Access(), count );
    *row = _mm_cvttss_si32( _mm_set_ss( sv_zoo_spacing_rows.GetFloat() + position.x ) ); // Native S2996B0 CVTTSS2SI32.
    s_pZooCounts = NULL;
    s_pZooSelection = NULL;
    return count;
}
