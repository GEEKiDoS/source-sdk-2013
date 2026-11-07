//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_INFERNO_H
#define L4D2_C_INFERNO_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseentity.h"
#include "particles_new.h"

class C_Inferno : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_Inferno, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	C_Inferno();
	virtual ~C_Inferno();
	virtual void Spawn();
	virtual void ClientThink();
	virtual void OnDataChanged( DataUpdateType_t updateType );
	virtual void UpdateOnRemove();
	virtual bool ShouldDraw() { return true; }
	virtual bool IsTransparent() { return true; }
	virtual int ComputeModelTranslucencyType() OVERRIDE { return 1; }
	virtual int DrawModel( int flags );
	virtual void GetRenderBounds( Vector &mins, Vector &maxs );
	virtual void GetRenderBoundsWorldspace( Vector &mins, Vector &maxs );

protected:
	virtual const char *GetParticleEffectName() const;

private:
	// The original client reserves 100 entries; the server sends only 64.
	enum { MAX_DRAWABLES = 100, MAX_PARTICLE_FIRES = 64 };
	enum DrawableState { GROWING = 0, BURNING, FADING, DEAD, UNCLAIMED };
	struct Drawable
	{
		Vector m_pos;
		Vector m_normal;
		float m_flFrameInterval;
		bool m_bFlip;
		int m_nLight;
		DrawableState m_state;
		float m_flStateTime;
		float m_flHeight;
		float m_flMaxHeight;
	};

	Drawable *FindDrawable( const Vector &position );
	void SynchronizeDrawables();
	void RecomputeBounds();
	void NotifyBoundsChanged();
	void UpdateParticles();
	void StopParticles();
	void DrawFire( Drawable &fire, IMaterial *pMaterial );

	int m_fireXDelta[MAX_DRAWABLES];
	int m_fireYDelta[MAX_DRAWABLES];
	int m_fireZDelta[MAX_DRAWABLES];
	int m_fireCount;
	Drawable m_drawable[MAX_DRAWABLES];
	int m_nDrawables;
	float m_flFireHalfWidth;
	float m_flFireHeight;
	Vector m_mins;
	Vector m_maxs;
	HPARTICLEFFECT m_hFireEffect;
};

class C_FireCrackerBlast : public C_Inferno
{
public:
	DECLARE_CLASS( C_FireCrackerBlast, C_Inferno );
	DECLARE_CLIENTCLASS();
protected:
	virtual const char *GetParticleEffectName() const;
};

#endif // L4D2_C_INFERNO_H
