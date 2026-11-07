//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SPECIAL_INFECTED_TARGET_SCAN_H
#define L4D2_SPECIAL_INFECTED_TARGET_SCAN_H
#ifdef _WIN32
#pragma once
#endif
#include "SharedFunctorUtils.h"
class CTerrorPlayer;
bool TongueTraceIgnoresGhosts( IHandleEntity *entity, int contentsMask );
bool TongueTraceIgnoresInfectedOrGhosts( IHandleEntity *entity, int contentsMask );
class TongueTargetScan : public TargetScan<CTerrorPlayer>
{
public:
	TongueTargetScan( CTerrorPlayer *owner, float tolerance, float range );
	virtual bool operator()( CTerrorPlayer *target ) OVERRIDE;
protected:
	virtual bool IsPotentialTarget( CTerrorPlayer *target ) const OVERRIDE;
	virtual bool IsTargetVisible( CTerrorPlayer *target ) const OVERRIDE;
private:
	bool m_isBot;
	Vector m_eyePosition;
};
class ChargeTargetScan : public TargetScan<CTerrorPlayer>
{
public:
	ChargeTargetScan( CTerrorPlayer *owner, float range );
	virtual bool operator()( CTerrorPlayer *target ) OVERRIDE;
protected:
	virtual bool IsPotentialTarget( CTerrorPlayer *target ) const OVERRIDE;
	virtual bool IsTargetVisible( CTerrorPlayer *target ) const OVERRIDE;
private:
	Vector m_origin, m_chargeForward;
};
#endif
