//========= Copyright Valve Corporation, All rights reserved. ============//
//
// First-person legs. See c_terror_fp_legs.h.
//
//=============================================================================//
#include "cbase.h"
#include "l4d2/player/c_terror_fp_legs.h"
#include "l4d2/player/c_terrorplayer.h"
#include "bone_setup.h"
#include "model_types.h"
#include "tier0/memdbgon.h"

ConVar cl_fp_legs( "cl_fp_legs", "1", FCVAR_ARCHIVE, "Show your survivor's legs when looking down in first person." );
static ConVar cl_fp_legs_offset( "cl_fp_legs_offset", "16", FCVAR_ARCHIVE,
	"Units the first-person legs sit behind the camera along the view's facing, so the upper-body cut stays out of view." );
static int s_nCutBoneGeneration = 0;
static void CutBoneChanged( IConVar *, const char *, float ) { ++s_nCutBoneGeneration; }
static ConVar cl_fp_legs_cut_bone( "cl_fp_legs_cut_bone", "ValveBiped.Bip01_Spine2", 0,
	"Bone collapsed with all of its descendants (chest, arms, head) on the first-person legs.", CutBoneChanged );

C_TerrorFirstPersonLegs *C_TerrorFirstPersonLegs::Create( C_TerrorPlayer *player )
{
	const model_t *model = player->GetModel();
	if ( !model )
		return NULL;
	C_TerrorFirstPersonLegs *legs = new C_TerrorFirstPersonLegs( player );
	if ( !legs->InitializeAsClientEntity( modelinfo->GetModelName( model ), false ) )
	{
		legs->Release();
		return NULL;
	}
	legs->AddEffects( EF_NOSHADOW );
	legs->SetNextClientThink( CLIENT_THINK_ALWAYS );
	legs->SyncFromPlayer();
	return legs;
}

C_TerrorFirstPersonLegs::C_TerrorFirstPersonLegs( C_TerrorPlayer *player ) :
	m_hPlayer( player ), m_nSyncFrame( -1 ), m_nCutBone( -1 ), m_nCutBoneGeneration( s_nCutBoneGeneration )
{
}

bool C_TerrorFirstPersonLegs::ShouldDraw()
{
	C_TerrorPlayer *player = m_hPlayer.Get();
	if ( !cl_fp_legs.GetBool() || !player || !player->IsFirstPersonLocalBody() )
		return false;
	// Poses whose lower body does not match the first-person camera: crawling, hanging, pinned, climbing.
	if ( player->GetTeamNumber() != TEAM_SURVIVOR || player->IsIncapacitated() || player->IsHangingFromLedge() ||
		player->IsDominated() || player->GetMoveType() == MOVETYPE_LADDER )
		return false;
	return BaseClass::ShouldDraw();
}

void C_TerrorFirstPersonLegs::ClientThink()
{
	// Keeps the leaf-system placement current; the draw and bone setup sync again for the frame they run in.
	SyncFromPlayer();
}

int C_TerrorFirstPersonLegs::DrawModel( int flags )
{
	// Never a shadow caster (EF_NOSHADOW); the hidden real body casts the full silhouette instead.
	if ( flags & STUDIO_SHADOWDEPTHTEXTURE )
		return 0;
	SyncFromPlayer();
	return BaseClass::DrawModel( flags );
}

bool C_TerrorFirstPersonLegs::SetupBones( matrix3x4_t *bones, int maximum, int mask, float time )
{
	SyncFromPlayer();
	return BaseClass::SetupBones( bones, maximum, mask, time );
}

void C_TerrorFirstPersonLegs::SyncFromPlayer()
{
	if ( m_nSyncFrame == gpGlobals->framecount )
		return;
	C_TerrorPlayer *player = m_hPlayer.Get();
	if ( !player )
		return;
	m_nSyncFrame = gpGlobals->framecount;

	if ( GetModelIndex() != player->GetModelIndex() )
		SetModelIndex( player->GetModelIndex() );
	SetSkin( player->GetSkin() );
	SetBody( player->GetBody() );
	CStudioHdr *studio = GetModelPtr();
	CStudioHdr *source = player->GetModelPtr();
	if ( !studio || !source )
		return;
	if ( m_nCutBoneGeneration != s_nCutBoneGeneration )
	{
		m_nCutBoneGeneration = s_nCutBoneGeneration;
		FindHiddenBones();
	}

	if ( GetSequence() != player->GetSequence() )
		SetSequence( player->GetSequence() );
	SetCycle( player->GetCycle() );
	SetPlaybackRate( player->GetPlaybackRate() );
	const float *poses = player->GetPoseParameterArray();
	for ( int i = 0; i < studio->GetNumPoseParameters(); ++i )
		SetPoseParameter( studio, i, Studio_GetPoseParameter( source, i, poses[i] ) );

	const int layers = player->GetNumAnimOverlays();
	SetNumAnimOverlays( layers );
	for ( int i = 0; i < layers; ++i )
		m_AnimOverlay[i] = player->m_AnimOverlay[i];

	// The model faces the body yaw (the render angles follow the feet, lagging the view while standing). It backs off
	// along the view yaw instead, so the collapsed upper body stays behind the camera whichever way the body faces.
	const QAngle angles( 0.0f, player->GetRenderAngles().y, 0.0f );
	Vector viewForward;
	AngleVectors( QAngle( 0.0f, player->EyeAngles().y, 0.0f ), &viewForward );
	SetAbsOrigin( player->GetRenderOrigin() - viewForward * cl_fp_legs_offset.GetFloat() );
	SetAbsAngles( angles );
}

CStudioHdr *C_TerrorFirstPersonLegs::OnNewModel()
{
	CStudioHdr *studio = BaseClass::OnNewModel();
	FindHiddenBones();
	return studio;
}

void C_TerrorFirstPersonLegs::FindHiddenBones()
{
	m_HiddenBones.ClearAll();
	CStudioHdr *studio = GetModelPtr();
	m_nCutBone = studio ? Studio_BoneIndexByName( studio, cl_fp_legs_cut_bone.GetString() ) : -1;
	if ( m_nCutBone < 0 )
		return;
	// Studio bones are ordered parent before child.
	for ( int i = m_nCutBone; i < studio->numbones() && i < MAXSTUDIOBONES; ++i )
	{
		const int parent = studio->pBone( i )->parent;
		if ( i == m_nCutBone || ( parent >= 0 && m_HiddenBones.IsBitSet( parent ) ) )
			m_HiddenBones.Set( i );
	}
}

void C_TerrorFirstPersonLegs::BuildTransformations( CStudioHdr *studio, Vector *pos, Quaternion q[], const matrix3x4_t &cameraTransform,
	int boneMask, CBoneBitList &computed )
{
	BaseClass::BuildTransformations( studio, pos, q, cameraTransform, boneMask, computed );
	if ( m_nCutBone < 0 )
		return;
	// Collapse the upper body to one point: triangles skinned to it vanish, and the ones shared with the waist
	// close into a cap at the cut bone instead of stretching toward each limb's own joint.
	Vector collapse;
	MatrixGetColumn( GetBone( m_nCutBone ), 3, collapse );
	for ( int i = m_nCutBone; i < studio->numbones() && i < MAXSTUDIOBONES; ++i )
	{
		if ( !m_HiddenBones.IsBitSet( i ) )
			continue;
		matrix3x4_t &bone = GetBoneForWrite( i );
		SetIdentityMatrix( bone );
		MatrixScaleByZero( bone );
		MatrixSetColumn( collapse, 3, bone );
	}
}
