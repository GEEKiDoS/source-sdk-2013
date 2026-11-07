//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_GLOWPROPERTY_H
#define L4D2_GLOWPROPERTY_H
#ifdef _WIN32
#pragma once
#endif

#include "networkvar.h"
#include "Color.h"
#include "mathlib/vector.h"

// Color has no single-int constructor; initialize its network wrapper with
// the same packed zero written by native CGlowProperty's Color constructor.
template <>
FORCEINLINE void NetworkVarConstruct< Color >( Color &color )
{
	color.SetRawColor( 0 );
}

class CBaseEntity;

// S:105BE0 / C:109B10. This is the embedded entity property, not the
// unrelated SDK2013 outline-object manager or particle Glow operator.
class CGlowProperty
{
	DECLARE_CLASS_NOBASE( CGlowProperty );
	DECLARE_EMBEDDED_NETWORKVAR();
	DECLARE_DATADESC();

public:
#ifdef CLIENT_DLL
	// C109810, raw override only; unlike SetColor this leaves RGB cache alone.
	void SetColorOverride( int packed ) { m_glowColorOverride.GetForModify().SetRawColor( packed ); }
#endif

	CGlowProperty();
	void Init( CBaseEntity *pEntity );
	void Term();

	void SetType( int type );
	int GetType() const { return m_iGlowType; }
	void SetRange( int range ) { m_nGlowRange = range; }
	int GetRange() const { return m_nGlowRange; }
	void SetRangeMin( int range ) { m_nGlowRangeMin = range; }
	int GetRangeMin() const { return m_nGlowRangeMin; }
	void SetColor( const Color &color );
	const Color &GetColorOverride() const { return m_glowColorOverride.Get(); }
	void SetFlashing( bool flashing ) { m_bFlashing = flashing; }
	bool IsFlashing() const { return m_bFlashing; }
	CBaseEntity *GetOuter() const { return m_pOuter; }

#ifdef CLIENT_DLL
	void SetTeam( int team ) { m_iTeam = team; }
	int GetTeam() const { return m_iTeam; }
	void SetEnabled( bool enabled ) { m_bEnabled = enabled; }
	bool IsEnabled() const { return m_bEnabled; }
	void GetColor( float *r, float *g, float *b ) const;
#else
	// S:105B30. In/out parameters: an unset override preserves caller RGB.
	void GetColor( unsigned char *r, unsigned char *g, unsigned char *b ) const;
#endif

private:
	CNetworkVar( int, m_iGlowType );
	CNetworkVar( int, m_nGlowRange );
	CNetworkVar( int, m_nGlowRangeMin );
	CNetworkVar( Color, m_glowColorOverride );
	CNetworkVar( bool, m_bFlashing );
	// S:105CD0 and C:109970 intentionally do not initialize this RGB cache.
	Vector m_glowColor;
	int m_iTeam;
	bool m_bEnabled;
	CBaseEntity *m_pOuter;
};

#ifdef CLIENT_DLL
EXTERN_RECV_TABLE( DT_GlowProperty );
#else
EXTERN_SEND_TABLE( DT_GlowProperty );
#endif

// S:105B69-105B90. Preserve the native x87 FSIN/extended intermediates
// and single final float store on the x64 port; see glow_flash_x64.asm.
extern "C" float L4D2_GlowFlashScale( float time );
// C:109900 keeps the scale extended until each RGB float result is stored.
extern "C" void L4D2_GlowFlashColor( float time, float *r, float *g, float *b );

#endif // L4D2_GLOWPROPERTY_H
