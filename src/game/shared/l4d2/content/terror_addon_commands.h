//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_ADDON_COMMANDS_H
#define TERROR_ADDON_COMMANDS_H
#ifdef _WIN32
#pragma once
#endif
#include "tier1/convar.h"
#include "icvar.h"

class CTerrorAddonCommand;

// The native commands live in engine/matchmaking, once per process. The target
// has separate DLL-local dictionaries. Each loaded DLL supplies one real peer;
// no peer or command pointer may survive that DLL's game-system Shutdown.
class ITerrorAddonCommandPeer
{
public:
	virtual void ReloadMissionData() = 0;
	virtual void RemountAddons() = 0;
	virtual void CreateAddonCommand( bool updatePaths ) = 0;
	virtual void SetAddonCommand( bool updatePaths, CTerrorAddonCommand *command ) = 0;
};

class CTerrorAddonCommand : public ConCommand
{
public:
	static const char *CommandName( bool updatePaths )
	{
		return updatePaths ? "update_addon_paths" : "mission_reload";
	}

	static void AttachModule( ITerrorAddonCommandPeer *peer, int module, bool updatePaths )
	{
		ConCommand *existing = g_pCVar->FindCommand( CommandName( updatePaths ) );
		if ( existing )
			static_cast< CTerrorAddonCommand * >( existing )->AttachPeer( peer, module );
		else
			peer->CreateAddonCommand( updatePaths );
	}

	static void Create( ITerrorAddonCommandPeer *peer, int module, bool updatePaths )
	{
		// Called only after ConVar_Register. Dynamic construction registers the
		// native flags 0 directly through s_pAccessor, without module flag ORs.
		// Do not leave this dynamic command in the pending static-object chain:
		// it may be destroyed or transferred before this DLL is initialized again.
		ConCommandBase *pending = s_pConCommandBases;
		new CTerrorAddonCommand( peer, module, updatePaths );
		s_pConCommandBases = pending;
	}

	virtual void Dispatch( const CCommand &command ) OVERRIDE
	{
		if ( m_bUpdatePaths )
		{
			// The filesystem is shared. A loaded client owns the native signon
			// gate; dedicated/no-client processes use the server's physical path.
			ITerrorAddonCommandPeer *peer = m_pPeers[1] ? m_pPeers[1] : m_pPeers[0];
			if ( peer ) peer->RemountAddons();
			return;
		}
		for ( int module = 0; module < 2; ++module )
			if ( m_pPeers[module] ) m_pPeers[module]->ReloadMissionData();
	}

	virtual void AttachPeer( ITerrorAddonCommandPeer *peer, int module )
	{
		m_pPeers[module] = peer;
		peer->SetAddonCommand( m_bUpdatePaths, this );
	}

	virtual void DetachPeer( ITerrorAddonCommandPeer *peer, int module )
	{
		m_pPeers[module] = NULL;
		peer->SetAddonCommand( m_bUpdatePaths, NULL );
		if ( peer != m_pOwner )
			return;

		ITerrorAddonCommandPeer *survivor = m_pPeers[1 - module];
		const bool updatePaths = m_bUpdatePaths;
		if ( survivor ) survivor->SetAddonCommand( updatePaths, NULL );
		// Unregister/delete while this object's vtable, name and help literals
		// still belong to a loaded DLL. The survivor's virtual factory then
		// constructs and publishes a replacement wholly inside THAT module.
		Shutdown();
		delete this;
		if ( survivor ) survivor->CreateAddonCommand( updatePaths );
	}

private:
	CTerrorAddonCommand( ITerrorAddonCommandPeer *peer, int module, bool updatePaths ) :
		ConCommand( CommandName( updatePaths ), static_cast< FnCommandCallback_t >( NULL ),
			updatePaths ? "Reloads the search paths for game addons." : "Reload mission metadata", 0 ),
		m_pOwner( peer ), m_bUpdatePaths( updatePaths )
	{
		m_pPeers[0] = m_pPeers[1] = NULL;
		AttachPeer( peer, module );
	}

	ITerrorAddonCommandPeer *m_pOwner;
	ITerrorAddonCommandPeer *m_pPeers[2]; // server 0, client 1
	bool m_bUpdatePaths;
};
#endif
