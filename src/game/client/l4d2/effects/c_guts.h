//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_GUTS_H
#define C_GUTS_H
#ifdef _WIN32
#pragma once
#endif
#include "c_baseanimating.h"

// C82850: no extra instance state; the wound producer allocates this subtype.
class C_Guts : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_Guts, C_BaseAnimating );
	virtual void Release() OVERRIDE;
};
#endif
