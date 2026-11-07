//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef FUNC_SIMPLELADDER_H
#define FUNC_SIMPLELADDER_H
#ifdef _WIN32
#pragma once
#endif

#ifdef CLIENT_DLL
#define CFuncSimpleLadder C_FuncSimpleLadder
#endif

class CFuncSimpleLadder : public CBaseEntity
{
public:
	DECLARE_CLASS( CFuncSimpleLadder, CBaseEntity );
	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();
	CFuncSimpleLadder();
	const Vector &GetClimbableNormal() const { return m_climbableNormal; }
#ifndef CLIENT_DLL
	virtual void Activate() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual bool KeyValue( const char *key, const char *value ) OVERRIDE;
#else
	virtual int DrawModel( int flags ) OVERRIDE;
#endif
private:
	CNetworkVector( m_climbableNormal );
};

#endif // FUNC_SIMPLELADDER_H
