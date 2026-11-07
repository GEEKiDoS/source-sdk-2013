//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client VoteController
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_VoteController_H
#define C_VoteController_H
#ifdef _WIN32
#pragma once
#endif

#include "shareddefs.h"
#include "GameEventListener.h"
#if defined( L4D2 )
class C_VoteController : public C_BaseEntity, public CGameEventListener
{
	DECLARE_CLASS( C_VoteController, C_BaseEntity );
public:
	DECLARE_CLIENTCLASS();
	C_VoteController();
	virtual ~C_VoteController();
	virtual void Spawn() OVERRIDE;
	virtual void ClientThink() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	static void RecvProxy_VoteType( const CRecvProxyData *data, void *structure, void *out );
	static void RecvProxy_VotesYes( const CRecvProxyData *data, void *structure, void *out );
	static void RecvProxy_VotesNo( const CRecvProxyData *data, void *structure, void *out );
private:
	void ResetData();
	int m_iActiveIssueIndex;
	int m_iOnlyTeamToVote;
	int m_nVotesYes;
	int m_nVotesNo;
	int m_nPotentialVotes;
	bool m_bVotesDirty;
	bool m_bTypeDirty;
};
#else

class C_VoteController : public C_BaseEntity, public CGameEventListener
{
	DECLARE_CLASS( C_VoteController, C_BaseEntity );
public:
	DECLARE_CLIENTCLASS();

	C_VoteController();
	virtual ~C_VoteController();

	virtual void	Spawn( void );
	virtual void	ClientThink( void );

	static void		RecvProxy_VoteType( const CRecvProxyData *pData, void *pStruct, void *pOut );
	static void		RecvProxy_VoteOption( const CRecvProxyData *pData, void *pStruct, void *pOut );

	void			FireGameEvent( IGameEvent *event );
protected:
	void			ResetData();

	int				m_iActiveIssueIndex;
	int				m_iOnlyTeamToVote;
	int				m_nVoteOptionCount[MAX_VOTE_OPTIONS];
	int				m_iVoteChoiceIndex;
	int				m_nPotentialVotes;
	bool			m_bVotesDirty;	// Received a vote, so remember to tell the Hud
	bool			m_bTypeDirty;	// Vote type changed, so show or hide the Hud
	bool			m_bIsYesNoVote;
	int				m_nVoteIdx;
};

#endif // L4D2 native / unchanged generic client vote controller
#endif // C_VoteController_H
