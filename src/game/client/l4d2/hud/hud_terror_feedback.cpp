//========= Copyright Valve Corporation, All rights reserved. ============//
// C297040..298D12, C298D40..2991AC, C29A3C0..29A710, C2CADF0..2CB4F8.
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "view.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include "l4d2/terror_physics_math.h"
#include "voice_status.h"
#include "vgui_avatarimage.h"
#include "inputsystem/iinputsystem.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/MaterialSystemUtil.h"
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/ImagePanel.h>
#include "steam/isteaminput.h"
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
extern ConVar hud_zombieteam;
static ConVar peril_notice_timer( "peril_notice_timer", "5.0", 0x0000000A );

class CHudDamageIndicator : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudDamageIndicator, vgui::Panel );
public:
	CHudDamageIndicator( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudDamageIndicator" ) { SetHiddenBits( 8 ); m_Material.Init( "VGUI/damageindicator", TEXTURE_GROUP_VGUI ); }
	virtual void Init() OVERRIDE;
	virtual void Reset() OVERRIDE { m_Records.Purge(); }
	virtual bool ShouldDraw() OVERRIDE { return CHudElement::ShouldDraw() && m_Records.Count() != 0; }
	void MsgFunc_Damage( bf_read &message )
	{
		Record record;
		record.damage = message.ReadUBitLong( 7 ); if ( (float)record.damage > m_flMaximumDamage ) record.damage = TerrorHudCvttFloat( m_flMaximumDamage );
		const int index = message.ReadUBitLong( 11 ); C_BaseEntity *attacker = cl_entitylist->GetEnt( index ); record.attacker = attacker;
		Vector position = attacker ? Vector( attacker->GetAbsOrigin().x, attacker->GetAbsOrigin().y, attacker->EyePosition().z ) : MainViewOrigin(); position.z += 20.0f;
		record.start = gpGlobals->curtime; record.end = gpGlobals->curtime + RemapDamage( record.damage, m_flMinimumTime, m_flMaximumTime );
		if ( position == vec3_origin ) position = MainViewOrigin(); record.direction = position - MainViewOrigin(); TerrorPhysicsNormalize( record.direction );
		static ConVarRef haptics( "steam_controller_haptics" );
		ISteamInput *steamInput = steamapicontext ? steamapicontext->SteamInput() : NULL;
		if ( g_pInputSystem && g_pInputSystem->IsSteamControllerActive() && steamInput && haptics.GetBool() )
		{
			float x, y, angle; GetIndicatorPosition( record.direction, (float)( ScreenWidth() / 2 ), x, y, angle ); const float fraction = x / (float)ScreenWidth();
			const int preferredIndex = g_pInputSystem->GetSteamControllerIndexForSlot( 0 );
			InputHandle_t handles[STEAM_INPUT_MAX_COUNT];
			const int controllerCount = steamInput->GetConnectedControllers( handles );
			InputHandle_t controller = preferredIndex >= 0 && preferredIndex < controllerCount ? handles[preferredIndex] : 0;
			if ( !controller && controllerCount > 0 ) controller = handles[0];
			if ( controller )
			{
				steamInput->Legacy_TriggerHapticPulse( controller, (ESteamControllerPad)1, (unsigned short)TerrorHudCvttFloat( fraction * 2000.0f ) );
				steamInput->Legacy_TriggerHapticPulse( controller, (ESteamControllerPad)0, (unsigned short)TerrorHudCvttFloat( ( 1.0f - fraction ) * 2000.0f ) );
			}
		}
		// Exactly three ordered native RNG draws, including merged attacker rows.
		record.noise.x = RandomFloat( -m_flNoise, m_flNoise ); record.noise.y = RandomFloat( -m_flNoise, m_flNoise ); record.noise.z = RandomFloat( -m_flNoise, m_flNoise ); record.direction += record.noise; TerrorPhysicsNormalize( record.direction );
		if ( attacker ) for ( int i = 0; i < m_Records.Count(); ++i ) if ( m_Records[i].attacker.Get() == attacker ) { Record &old = m_Records[i]; old.start = gpGlobals->curtime; old.damage = MAX( old.damage, record.damage ); old.end = gpGlobals->curtime + RemapDamage( old.damage, m_flMinimumTime, m_flMaximumTime ); old.direction = record.direction; return; }
		m_Records.AddToTail( record );
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false ); SetBounds( 0, 0, ScreenWidth(), ScreenHeight() ); }
	virtual void Paint() OVERRIDE
	{
		for ( int i = m_Records.Count() - 1; i >= 0; --i )
		{
			Record &r = m_Records[i]; const int damage = TerrorHudCvttFloat( (float)r.damage > m_flMaximumDamage ? m_flMaximumDamage : (float)MAX( r.damage, 0 ) ); const int halfWide = TerrorHudCvttFloat( RemapDamage( damage, m_flMinimumWidth, m_flMaximumWidth ) * 0.5f ), halfTall = TerrorHudCvttFloat( RemapDamage( damage, m_flMinimumHeight, m_flMaximumHeight ) * 0.5f );
			C_BaseEntity *attacker = r.attacker.Get(); if ( attacker ) { Vector position( attacker->GetAbsOrigin().x, attacker->GetAbsOrigin().y, attacker->EyePosition().z + 20.0f ); r.direction = position - MainViewOrigin(); TerrorPhysicsNormalize( r.direction ); r.direction += r.noise; TerrorPhysicsNormalize( r.direction ); }
			const float elapsed = gpGlobals->curtime - r.start; float radius = m_flEndRadius; const float travel = m_flTravelTime > elapsed ? elapsed : m_flTravelTime; if ( m_flTravelTime != 0.0f ) radius = (float)( (float)( (float)( m_flEndRadius - m_flStartRadius ) * travel ) / m_flTravelTime ) + m_flStartRadius; else if ( !( travel >= 0.0f ) ) radius = m_flStartRadius;
			float x, y, angle; GetIndicatorPosition( r.direction, radius, x, y, angle );
			if ( !( r.end - gpGlobals->curtime > 0.0f ) ) { m_Records.Remove( i ); continue; }
			const float duration = r.end - r.start;
			const float fraction = elapsed / duration; float alpha = 1.0f; if ( !( m_flFadeOutPercentage >= fraction ) ) { float fade; if ( m_flFadeOutPercentage == 1.0f ) fade = !( fraction >= 1.0f ) ? 0.0f : 1.0f; else fade = ( fraction - m_flFadeOutPercentage ) / ( 1.0f - m_flFadeOutPercentage ); alpha = 1.0f - fade; }
			DrawIndicator( TerrorHudCvttFloat( x - halfWide ), TerrorHudCvttFloat( y - halfTall ), TerrorHudCvttFloat( x + halfWide ), TerrorHudCvttFloat( y + halfTall ), alpha, angle );
		}
	}
private:
	struct Record { int damage; float end, start; Vector direction; EHANDLE attacker; Vector noise; };
	float RemapDamage( int damage, float minimum, float maximum ) const { if ( m_flMaximumDamage == 0.0f ) return (float)damage < 0.0f ? minimum : maximum; return (float)( (float)( (float)( maximum - minimum ) * (float)damage ) / m_flMaximumDamage ) + minimum; }
	void GetIndicatorPosition( const Vector &direction, float radius, float &x, float &y, float &angle ) const
	{
		Vector forward; AngleVectors( MainViewAngles(), &forward ); Vector horizontal( forward.x, forward.y, 0.0f ); TerrorPhysicsNormalize( horizontal ); const float dot = DotProduct( direction, horizontal ); Vector position;
		for ( int i = 0; i < 3; ++i ) { float component = (float)( forward[i] * 20.0f ) + (float)( direction[i] * 10.0f ); if ( dot <= 0.0f ) component += dot * ( i == 2 ? 72.0f : 0.0f ); position[i] = component + MainViewOrigin()[i]; }
		int screenX, screenY; if ( GetVectorInScreenSpace( position, screenX, screenY ) ) { screenX = TerrorHudSub32( screenX, ScreenWidth() / 2 ); screenY = TerrorHudSub32( screenY, ScreenHeight() / 2 ); const float sx = (float)screenX, sy = (float)screenY, length = sqrtf( (float)( sx * sx ) + (float)( sy * sy ) ); if ( length == 0.0f ) x = y = 0.0f; else { const float inverse = 1.0f / length; x = (float)( inverse * sx ) * radius; y = (float)( inverse * sy ) * radius; } }
		else { forward.z = 0.0f; TerrorPhysicsNormalize( forward ); x = -( DotProduct( direction, Vector( (float)( forward.z * 0.0f ) - forward.y, forward.x - (float)( forward.z * 0.0f ), (float)( forward.y * 0.0f ) - (float)( forward.x * 0.0f ) ) ) * radius ); y = -( DotProduct( direction, forward ) * radius ); }
		const double wideAngle = ( atan2( (double)x, (double)y ) + M_PI ) * 57.29577951308232; angle = (float)wideAngle; const float radians = (float)( M_PI * -wideAngle * 0.005555555555555556 ); x = (float)TerrorHudFtol( (double)( ScreenWidth() / 2 ) + sin( (double)radians ) * (double)radius ); y = (float)TerrorHudFtol( (double)( ScreenHeight() / 2 ) - cos( (double)radians ) * (double)radius );
	}
	void DrawIndicator( int x0, int y0, int x1, int y1, float alpha, float angle )
	{
		const double radians = -(double)angle * M_PI * 0.005555555555555556; const float cosine = cos( radians ), sine = sin( radians ), wide = (float)TerrorHudSub32( x1, x0 ), tall = (float)TerrorHudSub32( y1, y0 );
		const float left = (float)( (float)( tall * -0.5f ) * -sine ) + (float)( (float)( (float)( wide * -0.5f ) * cosine ) + (float)( wide * 0.5f + x0 ) );
		const float top = (float)( (float)( tall * -0.5f ) * cosine ) + (float)( (float)( (float)( wide * -0.5f ) * sine ) + (float)( tall * 0.5f + y0 ) );
		const float right = (float)( wide * cosine ) + left, lower = (float)( wide * sine ) + top;
		const float positions[4][2] = { { left, top }, { right, lower }, { (float)( tall * -sine ) + right, (float)( tall * cosine ) + lower }, { (float)( tall * -sine ) + left, (float)( tall * cosine ) + top } };
		const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }; CMatRenderContextPtr context( materials ); IMesh *mesh = context->GetDynamicMesh( true, NULL, NULL, m_Material ); CMeshBuilder builder; builder.Begin( mesh, MATERIAL_QUADS, 1 ); const unsigned char opacity = (unsigned char)TerrorHudCvttFloat( alpha * 255.0f ); const float z = vgui::surface()->GetZPos();
		for ( int i = 0; i < 4; ++i ) { builder.Color4ub( 255, 255, 255, opacity ); builder.TexCoord2f( 0, uv[i][0], uv[i][1] ); builder.Position3f( positions[i][0], positions[i][1], z ); builder.AdvanceVertex(); } builder.End(); mesh->Draw();
	}
	CUtlVector<Record> m_Records; CMaterialReference m_Material;
	CPanelAnimationVarAliasType( float, m_flMinimumWidth, "MinimumWidth", "10", "proportional_float" ); CPanelAnimationVarAliasType( float, m_flMaximumWidth, "MaximumWidth", "100", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flMinimumHeight, "MinimumHeight", "20", "proportional_float" ); CPanelAnimationVarAliasType( float, m_flMaximumHeight, "MaximumHeight", "100", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flStartRadius, "StartRadius", "140", "proportional_float" ); CPanelAnimationVarAliasType( float, m_flEndRadius, "EndRadius", "120", "proportional_float" );
	CPanelAnimationVar( float, m_flMaximumDamage, "MaximumDamage", "100" ); CPanelAnimationVar( float, m_flMinimumTime, "MinimumTime", "1" ); CPanelAnimationVar( float, m_flMaximumTime, "MaximumTime", "2" ); CPanelAnimationVar( float, m_flTravelTime, "TravelTime", ".1" ); CPanelAnimationVar( float, m_flFadeOutPercentage, "FadeOutPercentage", "0.7" ); CPanelAnimationVar( float, m_flNoise, "Noise", "0.1" );
};
DECLARE_HUDELEMENT( CHudDamageIndicator ); DECLARE_HUD_MESSAGE( CHudDamageIndicator, Damage );

void CHudDamageIndicator::Init()
{
	HOOK_HUD_MESSAGE( CHudDamageIndicator, Damage );
	Reset();
}

class CHudInfectedVOIP : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudInfectedVOIP, vgui::EditablePanel );
public:
	CHudInfectedVOIP( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudInfectedVOIP" ), m_iPlayer( -1 ), m_pVoice( NULL ), m_iNameWide( 0 ) { SetHiddenBits( 64 ); SetProportional( true ); m_pHead = new vgui::ImagePanel( this, "Head" ); m_pName = new vgui::Label( this, "Name", "" ); m_pAvatar = new CAvatarImagePanel( this, "Avatar" ); m_Name[0] = 0; }
	virtual bool ShouldDraw() OVERRIDE
	{
		C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ); if ( !local || local->GetTeamNumber() != TEAM_INFECTED || hud_zombieteam.GetBool() ) return false;
		for ( int i = 0; i < 32; ++i ) { C_TerrorPlayer *p = ToTerrorPlayer( UTIL_PlayerByIndex( i ) ); if ( p && p != local && p->GetTeamNumber() == TEAM_INFECTED && GetClientVoiceMgr()->IsPlayerSpeaking( p->entindex() ) ) { m_iPlayer = p->entindex(); g_pVGuiLocalize->ConvertANSIToUnicode( p->GetPlayerName(), m_Name, sizeof( m_Name ) ); return CHudElement::ShouldDraw(); } } return false;
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { LoadControlSettings( "Resource/UI/HUD/InfectedVOIP.res", "GAME" ); m_pVoice = gHUD.GetIcon( "voice_teammate" ); m_iNameWide = m_pName->GetWide(); m_hNameFont = m_pName->GetFont(); BaseClass::ApplySchemeSettings( scheme ); }
	virtual void Paint() OVERRIDE
	{
		if ( !C_BasePlayer::GetLocalPlayer() || m_iPlayer == -1 || !GetClientVoiceMgr()->IsPlayerSpeaking( m_iPlayer ) ) return;
		player_info_t info; if ( engine->GetPlayerInfo( m_iPlayer, &info ) ) { if ( info.friendsID ) { m_pAvatar->SetPlayer( m_iPlayer ); m_pAvatar->SetFgColor( Color( 255, 255, 255, 255 ) ); m_pAvatar->SetVisible( true ); m_pHead->SetVisible( false ); } else { C_TerrorPlayer *p = ToTerrorPlayer( UTIL_PlayerByIndex( m_iPlayer ) ); const char *image = "../vgui/s_panel_dead"; if ( p && p->IsAlive() ) switch ( p->GetZombieClass() ) { case 1: image = "../vgui/pz_panel_smoker"; break; case 2: image = "../vgui/pz_panel_boomer"; break; case 3: image = "../vgui/pz_panel_hunter"; break; case 8: image = "../vgui/pz_panel_hulk"; break; } m_pHead->SetImage( image ); m_pHead->SetDrawColor( Color( 255, 255, 255, 255 ) ); m_pHead->SetVisible( true ); m_pAvatar->SetVisible( false ); } }
		if ( m_pVoice ) { m_pVoice->DrawSelf( vgui::scheme()->GetProportionalScaledValue( 80 ), 0, GetWide(), GetTall(), Color( 255, 255, 255, 255 ) ); m_pName->SetFgColor( Color( 255, 255, 255, 255 ) ); m_pName->SetVisible( true ); m_pName->SetFont( m_hNameFont ); m_pName->SetText( m_Name ); if ( m_pName->GetWide() > m_iNameWide ) m_pName->SetFont( m_hSmallFont ); }
	}
private:
	int m_iPlayer; wchar_t m_Name[32]; CHudTexture *m_pVoice; vgui::ImagePanel *m_pHead; vgui::Label *m_pName; CAvatarImagePanel *m_pAvatar; int m_iNameWide; vgui::HFont m_hNameFont;
	CPanelAnimationVar( vgui::HFont, m_hSmallFont, "player_name_font_small", "MenuSubTitle" );
};
DECLARE_HUDELEMENT( CHudInfectedVOIP );

class CHudLeavingAreaWarning : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudLeavingAreaWarning, vgui::EditablePanel );
public:
	CHudLeavingAreaWarning( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudLeavingAreaWarning" ), m_bWarning( true ) { SetHiddenBits( 64 ); vgui::ivgui()->AddTickSignal( GetVPanel(), 100 ); }
	virtual bool ShouldDraw() OVERRIDE { C_TerrorPlayerResource *r = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); return r && r->IsSurvivorTeamReadyTime() && r->GetSurvivorTeamReadyTime() >= 0 && CHudElement::ShouldDraw(); }
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { LoadControlSettings( "resource/UI/LeavingAreaWarning.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); }
	virtual void OnTick() OVERRIDE { C_BasePlayer *p = C_BasePlayer::GetLocalPlayer(); C_TerrorPlayerResource *r = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); if ( !p || !r ) return; const Vector &center = p->m_vecConstraintCenter; const float radius = p->m_flConstraintRadius - p->m_flConstraintWidth; const bool warning = !r->IsTeamFrozen() && ( p->GetAbsOrigin() - center ).LengthSqr() > radius * radius && ( center.x <= -0.01f || center.x >= 0.01f || center.y <= -0.01f || center.y >= 0.01f || center.z <= -0.01f || center.z >= 0.01f ); if ( warning != m_bWarning ) { m_bWarning = warning; SetControlVisible( "Warning", warning ); } }
private:
	bool m_bWarning;
};
DECLARE_HUDELEMENT( CHudLeavingAreaWarning );

class CHudTeamMateInPerilNotice : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudTeamMateInPerilNotice, vgui::Panel );
public:
	CHudTeamMateInPerilNotice( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), name ), m_flEventTime( 0.0f ) { SetHiddenBits( 16 ); m_pMessage = new vgui::Label( this, "lblPerilMessage", "#L4D_teammate_is_in_peril" ); m_pButton = new vgui::Label( this, "lblPerilButton", "" ); }
	virtual void Init() OVERRIDE { ListenForGameEvent( "tongue_grab" ); ListenForGameEvent( "lunge_pounce" ); ListenForGameEvent( "player_now_it" ); ListenForGameEvent( "player_ledge_grab" ); ListenForGameEvent( "player_incapacitated" ); }
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE { const int victim = event->GetInt( "victim", -1 ), user = event->GetInt( "userid", -1 ); C_TerrorPlayer *p = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ); if ( !p || p->GetTeamNumber() != TEAM_SURVIVOR ) return; const int local = p->GetUserID(); if ( victim != -1 ) { if ( victim == local ) return; m_flEventTime = Plat_FloatTime(); } else if ( user != -1 && user != local ) m_flEventTime = Plat_FloatTime(); }
	virtual bool ShouldDraw() OVERRIDE
	{
		if ( !CHudElement::ShouldDraw() ) return false;
		static ConVarRef focus( "survivor_teammatefocus_enabled" );
		C_TerrorPlayer *p = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
		if ( !focus.GetBool() || !p || p->GetTeamNumber() != TEAM_SURVIVOR ) return false;
		const float duration = peril_notice_timer.GetFloat();
		const double now = Plat_FloatTime();
		return (double)m_flEventTime + (double)duration > now;
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundEnabled( false ); int mw, mt, bw = 0, bt = 0; m_pMessage->GetContentSize( mw, mt ); const wchar_t *format = g_pVGuiLocalize->Find( "#L4D_teammate_is_in_peril_use" ), *word = g_pVGuiLocalize->Find( "#L4D_teammate_is_in_peril_use_word" ); if ( format && word ) { wchar_t text[128]; g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, word ); m_pButton->SetText( text ); } m_pButton->GetContentSize( bw, bt ); const int wide = MAX( mw, bw ), total = mt + 4 + bt; m_pButton->SetBounds( wide / 2 - bw / 2, mt + bt / 2, bw, bt ); m_pMessage->SetBounds( wide / 2 - mw / 2, 0, mw, mt ); int x, y; GetPos( x, y ); SetBounds( ScreenWidth() / 2 - wide / 2, y, wide, mt + total ); }
	virtual void OnThink() OVERRIDE { const float half = peril_notice_timer.GetFloat() * 0.5f, elapsed = (float)( Plat_FloatTime() - m_flEventTime ); float alpha = 1.0f; if ( elapsed > half ) alpha = 1.0f - ( elapsed - half ) / half; m_pMessage->SetAlpha( TerrorHudCvttFloat( alpha * 255.0f ) ); m_pButton->SetAlpha( TerrorHudCvttFloat( alpha * 255.0f ) ); }
private:
	float m_flEventTime; vgui::Label *m_pMessage, *m_pButton;
};
DECLARE_HUDELEMENT( CHudTeamMateInPerilNotice );
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
