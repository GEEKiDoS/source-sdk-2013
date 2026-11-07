//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A base class for model-based doors. The exact movement required to
//			open or close the door is not dictated by this class, only that
//			the door has open, closed, opening, and closing states.
//
//			Doors must satisfy these requirements:
//
//			- Derived classes must support being opened by NPCs.
//			- Never autoclose in the face of a player.
//			- Never close into an NPC.
//
//=============================================================================//

#ifndef BASEPROPDOOR_H
#define BASEPROPDOOR_H
#ifdef _WIN32
#pragma once
#endif

#include "props.h"
#include "locksounds.h"
#include "entityoutput.h"
#include "nav.h"
#if defined( L4D2 )
#include "GameEventListener.h"
#include "l4d2/terror_shareddefs.h"
#include "util_shared.h"
#endif

extern ConVar g_debug_doors;

struct opendata_t
{
	Vector vecStandPos;		// Where the NPC should stand.
	Vector vecFaceDir;		// What direction the NPC should face.
	Activity eActivity;		// What activity the NPC should play.
};


abstract_class CBasePropDoor : public CDynamicProp
#if defined( L4D2 )
	, private CGameEventListener
#endif
{
public:

	DECLARE_CLASS( CBasePropDoor, CDynamicProp );
	DECLARE_SERVERCLASS();

	CBasePropDoor( void );
#if defined( L4D2 )
	virtual bool IsCheckpointDoor() const { return false; }
	virtual bool HasUnlockSequence() const { return false; }
	virtual bool ShouldCloseAreaPortals() const { return true; } // Native primary240 S196F20.
	virtual bool IsUsableByTeam( int team ) const { return !IsCheckpointDoor() || team == TEAM_SURVIVOR; } // S197530.
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	void PlayPoundSound();
	void SetRequired( bool required );
	bool IsRequired() const { return m_isRequired; }
	void SetRescueDoor( bool rescue );
	bool IsRescueDoor() const { return m_bIsRescueDoor; }
	virtual bool HandleDoorUse( CBaseEntity *activator, CBaseEntity *caller, int actions ); // S1AB2F0.
#endif

	void Spawn();
	void Precache();
	void Activate();
	int	ObjectCaps();

	void HandleAnimEvent( animevent_t *pEvent );

	// Base class services.
	// Do not make the functions in this block virtual!!
	// {
	inline bool IsDoorOpen();
	inline bool IsDoorAjar();
	inline bool IsDoorOpening();
	inline bool IsDoorClosed();
	inline bool IsDoorClosing();
	inline bool IsDoorLocked();
	inline bool IsDoorBlocked() const;
	inline bool IsNPCOpening(CAI_BaseNPC *pNPC);
	inline bool IsPlayerOpening();
	inline bool IsOpener(CBaseEntity *pEnt);

	bool NPCOpenDoor(CAI_BaseNPC *pNPC);
	bool TestCollision( const Ray_t &ray, unsigned int mask, trace_t& trace );
	// }

	// Implement these in your leaf class.
	// {
	virtual bool DoorCanClose( bool bAutoClose ) { return true; }
	virtual bool DoorCanOpen( void ) { return true; }

	virtual void GetNPCOpenData(CAI_BaseNPC *pNPC, opendata_t &opendata) = 0;
	virtual float GetOpenInterval(void) = 0;

	enum DoorExtent_t
	{
		DOOR_EXTENT_OPEN = 1,
		DOOR_EXTENT_CLOSED = 2,
	};
	virtual void ComputeDoorExtent( Extent *extent, unsigned int extentType ) = 0;	// extent contains the volume encompassing by the door in the specified states
	// }

protected:

	enum DoorState_t
	{
		DOOR_STATE_CLOSED = 0,
		DOOR_STATE_OPENING,
		DOOR_STATE_OPEN,
		DOOR_STATE_CLOSING,
		DOOR_STATE_AJAR,
	};

	// dvs: FIXME: make these private
	void DoorClose();

	CBasePropDoor *GetMaster( void ) { return m_hMaster; }
	bool HasSlaves( void ) { return ( m_hDoorList.Count() > 0 ); }

	inline void SetDoorState( DoorState_t eDoorState );

	float m_flAutoReturnDelay;	// How many seconds to wait before automatically closing, -1 never closes automatically.
	CUtlVector< CHandle< CBasePropDoor > >	m_hDoorList;	// List of doors linked to us

	inline CBaseEntity *GetActivator();

private:

	// Implement these in your leaf class.
	// {
	// Called when the door becomes fully open.
	virtual void OnDoorOpened() {}

	// Called when the door becomes fully closed.
#if defined( L4D2 )
protected:
	virtual void OnDoorClosed();
private:
#else
	virtual void OnDoorClosed() {}
#endif

	// Called to tell the door to start opening.
	virtual void BeginOpening(CBaseEntity *pOpenAwayFrom) = 0;

	// Called to tell the door to start closing.
	virtual void BeginClosing( void ) = 0;

	// Called when blocked to tell the door to stop moving.
	virtual void DoorStop( void ) = 0;

	// Called when blocked to tell the door to continue moving.
	virtual void DoorResume( void ) = 0;
	
	// Called to send the door instantly to its spawn positions.
	virtual void DoorTeleportToSpawnPosition() = 0;
	// }

private:

	// Main entry points for the door base behaviors.
	// SDK entry points remain nonvirtual; L4D2's native Unlock is declared separately below.
	// {
	bool DoorActivate();
	void DoorOpen( CBaseEntity *pOpenAwayFrom );
	void OpenIfUnlocked(CBaseEntity *pActivator, CBaseEntity *pOpenAwayFrom);

	void DoorOpenMoveDone();
	void DoorCloseMoveDone();
	void DoorAutoCloseThink();

#if defined( L4D2 )
	void DisableAreaPortalThink();
#endif

#if defined( L4D2 )
protected:
	void Lock();
public:
	virtual void Unlock( CBaseEntity *activator );
protected:
	void SetLockedState( bool locked, bool playSound );
	void LockWithoutSound() { SetLockedState( true, false ); }
	void UnlockWithoutSound() { SetLockedState( false, false ); }
	void ForceClosed() { m_bForceClosed = true; }
	void UpdateAreaPortals(bool bOpen);
private:
#else
	void Lock();
	void Unlock();
#endif

#if defined( L4D2 )
public:
#endif
	void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);
#if defined( L4D2 )
private:
	bool ActivateDoorFromUse(); // S1AB240, not the unchanged SDK DoorActivate helper.
#else
	void OnUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
#endif

	inline bool WillAutoReturn() { return m_flAutoReturnDelay != -1; }

	void StartBlocked(CBaseEntity *pOther);
	void OnStartBlocked( CBaseEntity *pOther );
	void MasterStartBlocked( CBaseEntity *pOther );

	void Blocked(CBaseEntity *pOther);
	void EndBlocked(void);
	void OnEndBlocked( void );

#if !defined( L4D2 )
	void UpdateAreaPortals(bool bOpen);
#endif

	// Input handlers
	void InputClose(inputdata_t &inputdata);
	void InputLock(inputdata_t &inputdata);
	void InputOpen(inputdata_t &inputdata);
	void InputOpenAwayFrom(inputdata_t &inputdata);
	void InputToggle(inputdata_t &inputdata);
	void InputUnlock(inputdata_t &inputdata);
#if defined( L4D2 )
	void InputPlayerOpen( inputdata_t &inputdata );
	void InputPlayerClose( inputdata_t &inputdata );
#endif

	void SetDoorBlocker( CBaseEntity *pBlocker );

	void SetMaster( CBasePropDoor *pMaster ) { m_hMaster = pMaster; }

	void CalcDoorSounds();
	// }

	int		m_nHardwareType;
	
#if defined( L4D2 )
	CNetworkVar( DoorState_t, m_eDoorState );
#else
	DoorState_t m_eDoorState;
#endif

	locksound_t m_ls;			// The sounds the door plays when being locked, unlocked, etc.
	EHANDLE		m_hActivator;		
	
#if defined( L4D2 )
	CNetworkVar( bool, m_bLocked );
	CNetworkVar( bool, m_bIsRescueDoor ); // Native5666, no datamap descriptor.
	bool m_isRequired; // Native5667, no datamap descriptor or constructor store.
#else
	bool m_bLocked;
#endif
	EHANDLE	m_hBlocker;				// Entity blocking the door currently
	bool	m_bFirstBlocked;		// Marker for being the first door (in a group) to be blocked (needed for motion control)

	bool m_bForceClosed;			// True if this door must close no matter what.
#if defined( L4D2 )
	CountdownTimer m_doorRetryTimer; // Native5668, blocked reversal; no SAVE/wire.
	int m_nCloseAttempts; // Native5716, constructor0, completed moves clear it.
	CountdownTimer m_doorReopenTimer; // Native5680, real closed callback; no SAVE/wire.
#endif

	string_t m_SoundMoving;
	string_t m_SoundOpen;
	string_t m_SoundClose;
#if defined( L4D2 )
	string_t m_SoundLock;
	string_t m_SoundUnlock;
	string_t m_SoundPound; // Model-derived native5712, no keyfield.
#endif

	// dvs: FIXME: can we remove m_flSpeed from CBaseEntity?
	//float m_flSpeed;			// Rotation speed when opening or closing in degrees per second.

	DECLARE_DATADESC();

	string_t m_SlaveName;

#if defined( L4D2 )
	CNetworkHandle( CBasePropDoor, m_hMaster );
#else
	CHandle< CBasePropDoor > m_hMaster;
#endif

#if !defined( L4D2 )
	static void RegisterPrivateActivities();
#endif

	// Outputs
	COutputEvent m_OnBlockedClosing;		// Triggered when the door becomes blocked while closing.
	COutputEvent m_OnBlockedOpening;		// Triggered when the door becomes blocked while opening.
	COutputEvent m_OnUnblockedClosing;		// Triggered when the door becomes unblocked while closing.
	COutputEvent m_OnUnblockedOpening;		// Triggered when the door becomes unblocked while opening.
	COutputEvent m_OnFullyClosed;			// Triggered when the door reaches the fully closed position.
	COutputEvent m_OnFullyOpen;				// Triggered when the door reaches the fully open position.
	COutputEvent m_OnClose;					// Triggered when the door is told to close.
	COutputEvent m_OnOpen;					// Triggered when the door is told to open.
	COutputEvent m_OnLockedUse;				// Triggered when the user tries to open a locked door.
};


void CBasePropDoor::SetDoorState( DoorState_t eDoorState )
{
	m_eDoorState = eDoorState;
}

bool CBasePropDoor::IsDoorOpen()
{
	return m_eDoorState == DOOR_STATE_OPEN;
}

bool CBasePropDoor::IsDoorAjar()
{
	return ( m_eDoorState == DOOR_STATE_AJAR );
}

bool CBasePropDoor::IsDoorOpening()
{
	return m_eDoorState == DOOR_STATE_OPENING;
}

bool CBasePropDoor::IsDoorClosed()
{
	return m_eDoorState == DOOR_STATE_CLOSED;
}

bool CBasePropDoor::IsDoorClosing()
{
	return m_eDoorState == DOOR_STATE_CLOSING;
}

bool CBasePropDoor::IsDoorLocked()
{
	return m_bLocked;
}

CBaseEntity *CBasePropDoor::GetActivator()
{
	return m_hActivator;
}

bool CBasePropDoor::IsDoorBlocked() const
{
	return ( m_hBlocker != NULL );
}

bool CBasePropDoor::IsNPCOpening( CAI_BaseNPC *pNPC )
{
	return ( pNPC == ( CAI_BaseNPC * )GetActivator() );
}

inline bool CBasePropDoor::IsPlayerOpening()
{
	return ( GetActivator() && GetActivator()->IsPlayer() );
}

inline bool CBasePropDoor::IsOpener(CBaseEntity *pEnt)
{
	return ( GetActivator() == pEnt );
}

#endif // BASEPROPDOOR_H
