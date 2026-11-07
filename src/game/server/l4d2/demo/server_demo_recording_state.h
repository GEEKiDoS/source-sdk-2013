//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SERVER_DEMO_RECORDING_STATE_H
#define L4D2_SERVER_DEMO_RECORDING_STATE_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"

// Private SERVER recording state. This is NOT the public client/tool-framework
// BaseEntityRecordingState_t. Native65020 owns this cache; 05EBD0 writes it.
// The three words exposed by empty CEscapeRoute::GetPositionAtFlow295230 are
// the CACHED angles.y, angles.z, and follow-parent index representation.
struct ServerDemoBaseEntityState_t
{
	ServerDemoBaseEntityState_t();
	bool UpdateFrom( const ServerDemoBaseEntityState_t &state ); // S:1CE780

	unsigned int m_nFieldMask;
	float m_flTime;
	int m_nEntityIndex;
	unsigned short m_nModelName;
	bool m_bVisible;
	unsigned char m_nReserved15;
	int m_nOwner;
	int m_nEffects;
	bool m_bFinalVisibleSample;
	unsigned char m_nReserved25[3];
	Vector m_vecOrigin;
	QAngle m_angAngles;
	int m_nFollowParentIndex;
};


// Private fixed server-record payloads consumed by the genuine file owner.
// They are not casts of target actor layout or the public client tool structs.
struct ServerDemoBoneState_t
{
	unsigned short m_nNativePrefix;
	unsigned short m_nBoneCountAndOwnership;
	unsigned char m_BoneData[3588];
};

struct ServerDemoAnimatingState_t
{
	unsigned long long m_nFieldMask;
	unsigned int m_nNativeWord8;
	unsigned short m_nSkin;
	unsigned short m_nBody;
	unsigned short m_nSequence;
	unsigned char m_bClearIKTargets;
	unsigned char m_bRagdoll;
	unsigned char m_bCreateIKContext;
	unsigned char m_bNoInterpolation;
	unsigned short m_nReserved22;
	float m_flCycle;
	float m_flPlaybackRate;
	int m_nFrameCount;
	unsigned char m_nPoseParameters;
	unsigned char m_nReserved37[3];
	float m_PoseParameters[24];
	ServerDemoBoneState_t m_Bones;
};

struct ServerDemoAnimationLayer_t
{
	float m_flCycle;
	int m_nSequence;
	float m_flWeight;
	float m_flPlaybackRate;
	int m_nOrder;
	float m_flPreviousCycle;
	float m_flBlendIn;
	float m_flBlendOut;
};

struct ServerDemoAnimatingOverlayState_t
{
	unsigned char m_nLayers;
	unsigned char m_nReserved1[3];
	ServerDemoAnimationLayer_t m_Layers[15];
	unsigned int m_nNativeWord484;
	unsigned long long m_nFieldMask;
	unsigned int m_nNativeWord496;
	unsigned int m_nNativeWord500;
};
#endif // L4D2_SERVER_DEMO_RECORDING_STATE_H
