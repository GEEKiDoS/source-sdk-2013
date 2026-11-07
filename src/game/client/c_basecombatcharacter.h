//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Defines the client-side representation of CBaseCombatCharacter.
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_BASECOMBATCHARACTER_H
#define C_BASECOMBATCHARACTER_H

#ifdef _WIN32
#pragma once
#endif

#include "shareddefs.h"
#include "c_baseflex.h"
#ifdef L4D2
#include "util_shared.h"
#include "particles_new.h"
#endif
#ifdef GLOWS_ENABLE
#include "glow_outline_effect.h"
#endif // GLOWS_ENABLE

class C_BaseCombatWeapon;
class C_WeaponCombatShield;

#define BCC_DEFAULT_LOOK_TOWARDS_TOLERANCE 0.9f

class C_BaseCombatCharacter : public C_BaseFlex
{
	DECLARE_CLASS( C_BaseCombatCharacter, C_BaseFlex );
public:
#if defined( L4D2 )
	virtual C_BaseCombatWeapon *Weapon_GetSlot( int slot ) const;
#endif

#if defined( L4D2 )
	static void RecvProxy_WeaponArray( const RecvProp *prop, void **out, void *data, int objectID );
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	void PlayTerrorFootstepSound( const Vector &origin, bool left, bool shallow, bool wading, bool force );
	virtual void OnTerrorFootstep( const Vector &origin, bool left, bool force );
	virtual surfacedata_t *GetTerrorFootstepSurface( int &surfaceIndex );
	virtual const char *GetTerrorFootstepSoundName( const char *sound, bool right, float speed, bool force );
	virtual bool ShouldPlayTerrorFootstepSound() const;
	virtual bool IsTerrorFootstepSurfaceValid( float speed, bool force ) const;
	virtual float GetTerrorFootstepRunThreshold() const;
#endif



#if defined( L4D2 )
	virtual bool Simulate() OVERRIDE;
#endif

	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

					C_BaseCombatCharacter( void );
	virtual			~C_BaseCombatCharacter( void );
#ifdef L4D2
#if defined( L4D2 )
	virtual void UpdateClientSideParticleEffects();
	virtual void OnNewParticleEffect( const char *name, CNewParticleEffect *effect ) OVERRIDE;
	virtual void OnParticleEffectDeleted( CNewParticleEffect *effect ) OVERRIDE;
#endif
	virtual int GetZombieClass() const { return 9; } // C1E0B0, native human/default class.
	virtual void GetGlowColor( float *r, float *g, float *b, float *a )
	{
		*r = 0.0f;
		*g = 0.0f;
		*b = 0.0f;
		*a = 0.0f;
	} // C1E0C0, the genuine combat-character default.
#endif

	virtual void	OnPreDataChanged( DataUpdateType_t updateType );
#if defined( L4D2 )
	virtual void	PostDataUpdate( DataUpdateType_t updateType ) OVERRIDE;
#else
	virtual void	OnDataChanged( DataUpdateType_t updateType );
#endif

	virtual bool	IsBaseCombatCharacter( void ) { return true; };
	virtual C_BaseCombatCharacter *MyCombatCharacterPointer( void ) { return this; }

	// -----------------------
	// Vision
	// -----------------------
	enum FieldOfViewCheckType { USE_FOV, DISREGARD_FOV };
	bool IsAbleToSee( const CBaseEntity *entity, FieldOfViewCheckType checkFOV );	// Visible starts with line of sight, and adds all the extra game checks like fog, smoke, camo...
	bool IsAbleToSee( C_BaseCombatCharacter *pBCC, FieldOfViewCheckType checkFOV );	// Visible starts with line of sight, and adds all the extra game checks like fog, smoke, camo...

	virtual bool IsLookingTowards( const CBaseEntity *target, float cosTolerance = BCC_DEFAULT_LOOK_TOWARDS_TOLERANCE ) const;	// return true if our view direction is pointing at the given target, within the cosine of the angular tolerance. LINE OF SIGHT IS NOT CHECKED.
	virtual bool IsLookingTowards( const Vector &target, float cosTolerance = BCC_DEFAULT_LOOK_TOWARDS_TOLERANCE ) const;	// return true if our view direction is pointing at the given target, within the cosine of the angular tolerance. LINE OF SIGHT IS NOT CHECKED.

	virtual bool IsInFieldOfView( CBaseEntity *entity ) const;	// Calls IsLookingAt with the current field of view.  
	virtual bool IsInFieldOfView( const Vector &pos ) const;

	enum LineOfSightCheckType
	{
		IGNORE_NOTHING,
		IGNORE_ACTORS
	};
	virtual bool IsLineOfSightClear( CBaseEntity *entity, LineOfSightCheckType checkType = IGNORE_NOTHING ) const;// strictly LOS check with no other considerations
	virtual bool IsLineOfSightClear( const Vector &pos, LineOfSightCheckType checkType = IGNORE_NOTHING, CBaseEntity *entityToIgnore = NULL ) const;


	// -----------------------
	// Ammo
	// -----------------------
	void				RemoveAmmo( int iCount, int iAmmoIndex );
	void				RemoveAmmo( int iCount, const char *szName );
	void				RemoveAllAmmo( );
	int					GetAmmoCount( int iAmmoIndex ) const;
	int					GetAmmoCount( char *szName ) const;

	C_BaseCombatWeapon*	Weapon_OwnsThisType( const char *pszWeapon, int iSubType = 0 ) const;  // True if already owns a weapon of this class
	virtual	bool		Weapon_Switch( C_BaseCombatWeapon *pWeapon, int viewmodelindex = 0 );
	virtual bool		Weapon_CanSwitchTo(C_BaseCombatWeapon *pWeapon);
	
	// I can't use my current weapon anymore. Switch me to the next best weapon.
	bool SwitchToNextBestWeapon(C_BaseCombatWeapon *pCurrent);

	virtual C_BaseCombatWeapon	*GetActiveWeapon( void ) const;
	int					WeaponCount() const;
	C_BaseCombatWeapon	*GetWeapon( int i ) const;

	// This is a sort of hack back-door only used by physgun!
	void SetAmmoCount( int iCount, int iAmmoIndex );

	float				GetNextAttack() const { return m_flNextAttack; }
	void				SetNextAttack( float flWait ) { m_flNextAttack = flWait; }

	virtual int			BloodColor();

	// Blood color (see BLOOD_COLOR_* macros in baseentity.h)
	void SetBloodColor( int nBloodColor );

	virtual void		DoMuzzleFlash();

#ifdef GLOWS_ENABLE
	CGlowObject			*GetGlowObject( void ){ return m_pGlowEffect; }
	virtual void		GetGlowEffectColor( float *r, float *g, float *b );
//	void				EnableGlowEffect( float r, float g, float b );

	void				SetClientSideGlowEnabled( bool bEnabled ){ m_bClientSideGlowEnabled = bEnabled; UpdateGlowEffect(); }
	bool				IsClientSideGlowEnabled( void ){ return m_bClientSideGlowEnabled; }
#endif // GLOWS_ENABLE

public:

	float			m_flNextAttack;

protected:

#ifdef GLOWS_ENABLE	
	virtual void		UpdateGlowEffect( void );
	virtual void		DestroyGlowEffect( void );
#endif // GLOWS_ENABLE

	int			m_bloodColor;			// color of blood particless
#if defined( L4D2 )
	void SetWaterTrailMode( int mode, int onlyDrawForLocalUser );
	int m_leftFootAttachment;
	int m_rightFootAttachment;
	int m_nWaterTrailMode;
	float m_flWaterTrailSurfaceZ;
	float m_flWaterTrailNextProbeTime;
	bool m_bWeaponArrayChanged;
	unsigned char m_weaponIndexByID[56];
#endif

private:
	bool				ComputeLOS( const Vector &vecEyePosition, const Vector &vecTarget ) const;

	CNetworkArray( int, m_iAmmo, MAX_AMMO_TYPES );

	CHandle<C_BaseCombatWeapon>		m_hMyWeapons[MAX_WEAPONS];
	CHandle< C_BaseCombatWeapon > m_hActiveWeapon;
#ifdef L4D2
protected:
	CountdownTimer m_footstepTimer;
#if defined( L4D2 )
	// Real native4316 timer constructs BEFORE intrusive4328/4340/4352 refs.
	HPARTICLEFFECT m_hWaterTrailDirectional;
	HPARTICLEFFECT m_hWaterTrailWalk;
	HPARTICLEFFECT m_hWaterTrailIdle;
#endif
private:
#endif

#ifdef GLOWS_ENABLE
	bool				m_bClientSideGlowEnabled;	// client-side only value used for spectator
	bool				m_bGlowEnabled;				// networked value
	bool				m_bOldGlowEnabled;
	CGlowObject			*m_pGlowEffect;
#endif // GLOWS_ENABLE

private:
	C_BaseCombatCharacter( const C_BaseCombatCharacter & ); // not defined, not accessible


//-----------------------
#ifdef INVASION_CLIENT_DLL
public:
	virtual void	Release( void );
	virtual void	SetDormant( bool bDormant );
	virtual void	OnPreDataChanged( DataUpdateType_t updateType );
	virtual void	OnDataChanged( DataUpdateType_t updateType );
	virtual void	ClientThink( void );

	// TF2 Powerups
	virtual bool	CanBePoweredUp( void ) { return true; }
	bool			HasPowerup( int iPowerup ) { return ( m_iPowerups & (1 << iPowerup) ) != 0; };
	virtual void	PowerupStart( int iPowerup, bool bInitial );
	virtual void	PowerupEnd( int iPowerup );
	void			RemoveAllPowerups( void );

	// Powerup effects
	void			AddEMPEffect( float flSize );
	void			AddBuffEffect( float flSize );

	C_WeaponCombatShield		*GetShield( void );

public:
	int				m_iPowerups;
	int				m_iPrevPowerups;
#endif

};

inline C_BaseCombatCharacter *ToBaseCombatCharacter( C_BaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsBaseCombatCharacter() )
		return NULL;

#if _DEBUG
	return dynamic_cast<C_BaseCombatCharacter *>( pEntity );
#else
	return static_cast<C_BaseCombatCharacter *>( pEntity );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
inline int	C_BaseCombatCharacter::WeaponCount() const
{
	return MAX_WEAPONS;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : i - 
//-----------------------------------------------------------------------------
inline C_BaseCombatWeapon *C_BaseCombatCharacter::GetWeapon( int i ) const
{
	Assert( (i >= 0) && (i < MAX_WEAPONS) );
	return m_hMyWeapons[i].Get();
}

EXTERN_RECV_TABLE(DT_BaseCombatCharacter);

#endif // C_BASECOMBATCHARACTER_H
