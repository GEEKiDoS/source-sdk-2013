#ifndef TERROR_PHYSICSPROP_TE_H
#define TERROR_PHYSICSPROP_TE_H
#ifdef _WIN32
#pragma once
#endif
// Game-owned direct producer into the EXISTING DT_TEPhysicsProp singleton.
// Does not add a slot to the target's ITempEntsSystem engine-facing ABI.
void TE_PhysicsProp( IRecipientFilter &filter, float delay, int modelindex, int skin,
	const Vector &origin, const QAngle &angles, const Vector &velocity,
	int flags, int effects, int r, int g, int b );
#endif
