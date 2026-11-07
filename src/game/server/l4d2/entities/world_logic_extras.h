#pragma once
#include "cbase.h"
#include "filters.h"
#include "lights.h"
#include "l4d2/player/terrorplayer.h"

class CFilterModel : public CBaseFilter
{
public:
	DECLARE_CLASS( CFilterModel, CBaseFilter );
	DECLARE_DATADESC();
protected:
	virtual bool PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity ) OVERRIDE;
private:
	string_t m_iFilterModel;
};

class CFilterContext : public CBaseFilter
{
public:
	DECLARE_CLASS( CFilterContext, CBaseFilter );
	DECLARE_DATADESC();
protected:
	virtual bool PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity ) OVERRIDE;
private:
	string_t m_iFilterContext;
};

class FilterInfectedClass : public CBaseFilter
{
public:
	DECLARE_CLASS( FilterInfectedClass, CBaseFilter );
	DECLARE_DATADESC();
protected:
	virtual bool PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity ) OVERRIDE;
private:
	int m_iFilterInfectedClass;
};

class FilterMeleeDamage : public CBaseFilter
{
public:
	DECLARE_CLASS( FilterMeleeDamage, CBaseFilter );
	DECLARE_DATADESC();
protected:
	virtual bool PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity ) OVERRIDE;
	virtual bool PassesDamageFilterImpl( const CTakeDamageInfo &info ) OVERRIDE;
private:
	int m_iDamageType;
};

class FilterHealth : public CBaseFilter
{
public:
	DECLARE_CLASS( FilterHealth, CBaseFilter );
	DECLARE_DATADESC();
protected:
	virtual bool PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity ) OVERRIDE;
	virtual bool PassesDamageFilterImpl( const CTakeDamageInfo &info ) OVERRIDE;
private:
	bool m_bAdrenalineActive;
	int m_iHealthMin, m_iHealthMax;
};

class CInfoParticleTarget : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoParticleTarget, CPointEntity );
	virtual int UpdateTransmitState() OVERRIDE;
};

class CSurroundTest : public CPointEntity
{
public:
	DECLARE_CLASS( CSurroundTest, CPointEntity );
	DECLARE_DATADESC();
	void FireCorrectOutput( inputdata_t &inputdata );
private:
	COutputEvent m_On2Speakers, m_On4Speakers, m_On51Speakers;
};
