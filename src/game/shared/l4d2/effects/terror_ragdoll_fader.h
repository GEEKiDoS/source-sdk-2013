#ifndef TERROR_RAGDOLL_FADER_H
#define TERROR_RAGDOLL_FADER_H
#ifdef _WIN32
#pragma once
#endif
#ifdef CLIENT_DLL
#define CFuncRagdollFader C_FuncRagdollFader
#endif
class CFuncRagdollFader : public CBaseEntity
{
public:
	DECLARE_CLASS( CFuncRagdollFader, CBaseEntity );
	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();
	CFuncRagdollFader();
	void Spawn() OVERRIDE;
	void UpdateOnRemove() OVERRIDE;
#ifdef CLIENT_DLL
	void PostDataUpdate( DataUpdateType_t type ) OVERRIDE;
	void TestRagdoll( CBaseAnimating *ragdoll );
#else
	void StartTouch( CBaseEntity *other ) OVERRIDE;
	void InputEnable( inputdata_t & );
	void InputDisable( inputdata_t & );
#endif
private:
	CNetworkVar( bool, m_bDisabled );
};
#endif
