//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef C_BREAKABLEPROP_H
#define C_BREAKABLEPROP_H
#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class C_BreakableProp : public C_BaseAnimating
{
	typedef C_BaseAnimating BaseClass;
public:
	DECLARE_CLIENTCLASS();

	C_BreakableProp();
#if defined( L4D2 )
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // C6D580. Compose with paired1302, not a replacement.
#endif
#if defined( L4D2 )
	void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	unsigned int PhysicsSolidMaskForEntity() const OVERRIDE;
#endif
	
	virtual void SetFadeMinMax( float fademin, float fademax );

	//virtual bool	ShouldPredict( void ) OVERRIDE;
	//virtual C_BasePlayer *GetPredictionOwner( void ) OVERRIDE;
	virtual bool PredictionErrorShouldResetLatchedForAllPredictables( void ) OVERRIDE { return false; }

	// Copy fade from another breakable prop
	void CopyFadeFrom( C_BreakableProp *pSource );
#if defined( L4D2 )
protected:
	bool m_noGhostCollision;
	bool m_bClientPhysics;
#endif
};

#endif // C_BREAKABLEPROP_H
