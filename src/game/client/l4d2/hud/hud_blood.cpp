//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "particle_parse.h"
#include <vgui_controls/Panel.h>
#include "tier0/memdbgon.h"

static ConVar z_splatter_delay( "z_splatter_delay", "0.25", 0x0000000A );
static ConVar z_splatter_delay_melee( "z_splatter_delay_melee", "0.1", 0x0000000A );

// Native client251B50: real local player, except a real in-eye player target.
static C_TerrorPlayer *GetSplatterViewedPlayer()
{
    C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
    if ( local && local->GetObserverMode() == OBS_MODE_IN_EYE )
    {
        C_BaseEntity *target = local->GetObserverTarget();
        if ( target && target->IsPlayer() )
            return ToTerrorPlayer( target );
    }
    return ToTerrorPlayer( local );
}

class CHudBlood : public CHudElement, public vgui::Panel
{
    DECLARE_CLASS_SIMPLE( CHudBlood, vgui::Panel );
public:
    explicit CHudBlood( const char *name ) : CHudElement( name ), BaseClass( NULL, "HudBlood" )
    {
        SetParent( g_pClientMode->GetViewport() );
        SetHiddenBits( 0 );
        SetPaintBackgroundEnabled( false );
    }
    virtual void Init() OVERRIDE;
    virtual void Reset() OVERRIDE { m_queue.RemoveAll(); }
    void MsgFunc_Splatter( bf_read &msg ) { QueueSplatter( msg, 0 ); }
    void MsgFunc_MeleeSlashSplatter( bf_read &msg ) { QueueSplatter( msg, 1 ); }
    void MsgFunc_MeleeClubSplatter( bf_read &msg ) { QueueSplatter( msg, 2 ); }
    void MsgFunc_MudSplatter( bf_read &msg ) { QueueSplatter( msg, 3 ); }
    void MsgFunc_SplatterClear( bf_read &msg ) { m_queue.RemoveAll(); }
protected:
    virtual void OnThink() OVERRIDE
    {
        if ( GetSplatterViewedPlayer() )
            UpdateSplatter();
    }
private:
    struct Splatter_t
    {
        int amount;
        float due;
        int kind;
    };
    void QueueSplatter( bf_read &msg, int kind );
    void UpdateSplatter();
    CUtlVector< Splatter_t > m_queue;
};
DECLARE_HUDELEMENT( CHudBlood );
DECLARE_HUD_MESSAGE( CHudBlood, Splatter );
DECLARE_HUD_MESSAGE( CHudBlood, MeleeSlashSplatter );
DECLARE_HUD_MESSAGE( CHudBlood, MeleeClubSplatter );
DECLARE_HUD_MESSAGE( CHudBlood, MudSplatter );
DECLARE_HUD_MESSAGE( CHudBlood, SplatterClear );

void CHudBlood::Init()
{
    HOOK_HUD_MESSAGE( CHudBlood, Splatter );
    HOOK_HUD_MESSAGE( CHudBlood, MeleeSlashSplatter );
    HOOK_HUD_MESSAGE( CHudBlood, MeleeClubSplatter );
    HOOK_HUD_MESSAGE( CHudBlood, MudSplatter );
    HOOK_HUD_MESSAGE( CHudBlood, SplatterClear );
}

#pragma float_control( precise, on, push )
void CHudBlood::QueueSplatter( bf_read &msg, int kind )
{
    // Native2BFC70/2BFDC6/2BFF20/2C0070 reject BEFORE consuming ReadByte.
    if ( UTIL_IsLowViolence() || m_queue.Count() >= 16 )
        return;
    Splatter_t entry;
    entry.amount = msg.ReadByte();
    entry.due = gpGlobals->curtime + ( kind == 0 ? z_splatter_delay.GetFloat() : z_splatter_delay_melee.GetFloat() );
    entry.kind = kind;
    m_queue.AddToTail( entry );
}

void CHudBlood::UpdateSplatter()
{
    if ( m_queue.Count() == 0 || !( m_queue[0].due <= gpGlobals->curtime ) )
        return;
    const Splatter_t entry = m_queue[0];
    m_queue.Remove( 0 ); // Exactly one head, unsorted, pop BEFORE sound/particles.
    C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
    if ( local && local->IsPlayer() )
        local->EmitSound( "Blood.Splat" );
    static const char *effects[4] =
    {
        "screen_blood_splatter", "screen_blood_splatter_melee", "screen_blood_splatter_melee_blunt", "screen_mud_splatter"
    };
    for ( int i = 0; i < entry.amount; ++i )
        DispatchParticleEffect( effects[entry.kind], vec3_origin, vec3_angle );
}
#pragma float_control( pop )

// No message length or server format changes: all four 1-byte messages and 0-byte
// SplatterClear already exist in shared/l4d2/terror_usermessages.cpp. No extra
// queue, per-kind reorder, drain-all loop, spawned-particle cleanup or per-frame
// RNG. Reset/SplatterClear only clear pending count, preserving vector capacity
// and active screen particles exactly as native queuecount=0.
// Native consumer2BF740 uses the active split-screen slot guard around emission.
// Source SDK 2013 MP has one local view; consume through its actual local viewport
// above, not a fabricated second split-screen player/channel. Multi-view behavior
// must be exercised only if the sole viewport owner gains that genuine engine API.
