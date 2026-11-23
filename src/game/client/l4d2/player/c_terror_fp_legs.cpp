//========= Copyright Valve Corporation, All rights reserved. ============//
//
// First-person body. See c_terror_fp_legs.h.
//
//=============================================================================//
#include "cbase.h"
#include "l4d2/player/c_terror_fp_legs.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "shadowmaps_dx12.h"
#include "bone_setup.h"
#include "model_types.h"
#include "tier0/memdbgon.h"

ConVar cl_fp_legs( "cl_fp_legs", "1", FCVAR_ARCHIVE, "Show your survivor's legs when looking down in first person." );
ConVar cl_fp_self_shadow( "cl_fp_self_shadow", "1", FCVAR_ARCHIVE,
	"In first person, your full body and weapon cast native shadowmap shadows (never into your own flashlight)." );
static ConVar cl_fp_legs_offset( "cl_fp_legs_offset", "0", 0,
	"Units the first-person legs and their shadow sit behind the camera along the view's facing." );
static ConVar cl_fp_legs_lean( "cl_fp_legs_lean", "45", 0,
	"Degrees the first-person legs' upper body leans back from cl_fp_legs_lean_bone, moving the collapsed waist behind the camera." );
static ConVar cl_fp_legs_tilt( "cl_fp_legs_tilt", "35", 0,
	"Degrees the first-person legs tip back about the feet when looking straight down (ramping in from cl_fp_legs_tilt_start), "
	"so the feet stay under the camera while the hips and the cut leave the bottom of the screen." );
static ConVar cl_fp_legs_tilt_start( "cl_fp_legs_tilt_start", "30", 0, "View pitch (degrees down) where cl_fp_legs_tilt starts ramping in." );
static int s_nBoneNamesGeneration = 0;
static void BoneNameChanged( IConVar *, const char *, float ) { ++s_nBoneNamesGeneration; }
static ConVar cl_fp_legs_hide_bones( "cl_fp_legs_hide_bones", "ValveBiped.Bip01_Spine1", 0,
	"Space-separated bones collapsed with all of their descendants on the first-person legs. Spine1: chest, arms and head; "
	"\"ValveBiped.Bip01_Neck1 ValveBiped.Bip01_L_Clavicle ValveBiped.Bip01_R_Clavicle\": head and arms only.", BoneNameChanged );
static ConVar cl_fp_legs_lean_bone( "cl_fp_legs_lean_bone", "ValveBiped.Bip01_Spine", 0,
	"Bone the first-person legs' upper body leans back from (cl_fp_legs_lean).", BoneNameChanged );

// Marks `root` and every descendant; studio bones are ordered parent before child.
static void MarkSubtree( CStudioHdr *studio, int root, CBitVec< MAXSTUDIOBONES > &bones )
{
	bones.ClearAll();
	if ( root < 0 )
		return;
	for ( int i = root; i < studio->numbones() && i < MAXSTUDIOBONES; ++i )
	{
		const int parent = studio->pBone( i )->parent;
		if ( i == root || ( parent >= 0 && bones.IsBitSet( parent ) ) )
			bones.Set( i );
	}
}

static bool InFeatureShadowDraw( int flags )
{
	return ( flags & STUDIO_SHADOWDEPTHTEXTURE ) && ShadowMapsDX12_InFeatureDepthView();
}

bool C_TerrorFirstPersonBody::LegsPose( C_TerrorPlayer *player )
{
	return player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsIncapacitated() && !player->IsHangingFromLedge() &&
		!player->IsDominated() && player->GetMoveType() != MOVETYPE_LADDER;
}

bool C_TerrorFirstPersonBody::CastsSelfShadow( C_TerrorPlayer *player )
{
	// A ledge-hanging local body draws itself in first person (C_CSPlayer::DrawModel) and casts its own shadow.
	return cl_fp_self_shadow.GetBool() && ShadowMapsDX12_ShadowsEnabled() && player->IsFirstPersonLocalBody() &&
		!player->IsHangingFromLedge();
}

C_TerrorFirstPersonBody *C_TerrorFirstPersonBody::Create( C_TerrorPlayer *player, Role role )
{
	const model_t *model = player->GetModel();
	if ( !model )
		return NULL;
	C_TerrorFirstPersonBody *body = new C_TerrorFirstPersonBody( player, role );
	if ( !body->InitializeAsClientEntity( modelinfo->GetModelName( model ), false ) )
	{
		body->Release();
		return NULL;
	}
	// The legs never cast (the shadow copy casts the whole silhouette); EF_NOSHADOW would make the shadow copy ineligible.
	if ( role == ROLE_LEGS )
		body->AddEffects( EF_NOSHADOW );
	body->SetNextClientThink( CLIENT_THINK_ALWAYS );
	body->SyncFromPlayer();
	return body;
}

C_TerrorFirstPersonBody::C_TerrorFirstPersonBody( C_TerrorPlayer *player, Role role ) :
	m_hPlayer( player ), m_Role( role ), m_nSyncFrame( -1 ), m_nFirstHiddenBone( MAXSTUDIOBONES ), m_nLeanBone( -1 ),
	m_nBoneNamesGeneration( s_nBoneNamesGeneration )
{
}

bool C_TerrorFirstPersonBody::ShouldDraw()
{
	C_TerrorPlayer *player = m_hPlayer.Get();
	if ( m_Role != ROLE_LEGS || !cl_fp_legs.GetBool() || !player || !player->IsFirstPersonLocalBody() || !LegsPose( player ) )
		return false;
	return BaseClass::ShouldDraw();
}

bool C_TerrorFirstPersonBody::ShouldCastShadowWhileHidden()
{
	C_TerrorPlayer *player = m_hPlayer.Get();
	return m_Role == ROLE_SHADOW && player && CastsSelfShadow( player );
}

void C_TerrorFirstPersonBody::ClientThink()
{
	// Keeps the leaf-system placement current; the draw and bone setup sync again for the frame they run in.
	SyncFromPlayer();
}

int C_TerrorFirstPersonBody::DrawModel( int flags )
{
	// Legs: the local main view only. Shadow copy: native shadowmaps only, never the flashlight's projected depth
	// (that view's lists skip it anyway: its local draw bit is clear).
	if ( m_Role == ROLE_LEGS ? ( flags & STUDIO_SHADOWDEPTHTEXTURE ) != 0 : !( InFeatureShadowDraw( flags ) && ShouldCastShadowWhileHidden() ) )
		return 0;
	SyncFromPlayer();
	return BaseClass::DrawModel( flags );
}

bool C_TerrorFirstPersonBody::SetupBones( matrix3x4_t *bones, int maximum, int mask, float time )
{
	SyncFromPlayer();
	return BaseClass::SetupBones( bones, maximum, mask, time );
}

void C_TerrorFirstPersonBody::SyncFromPlayer()
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
	if ( m_Role == ROLE_LEGS && m_nBoneNamesGeneration != s_nBoneNamesGeneration )
	{
		m_nBoneNamesGeneration = s_nBoneNamesGeneration;
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

	// The model faces the body yaw (the render angles follow the feet, lagging the view while standing). Offset and
	// tilt follow the view yaw instead, so the collapsed upper body goes behind the camera whichever way the body
	// faces. Both copies share the offset, so the shadow starts at the visible feet; the tilt pivots on the feet and
	// applies to the legs only, so the shadow keeps the upright body.
	const bool legsPose = cl_fp_legs.GetBool() && LegsPose( player );
	const float offset = legsPose ? cl_fp_legs_offset.GetFloat() : 0.0f;
	Vector viewForward;
	AngleVectors( QAngle( 0.0f, player->EyeAngles().y, 0.0f ), &viewForward );
	SetAbsOrigin( player->GetRenderOrigin() - viewForward * offset );
	QAngle angles( 0.0f, player->GetRenderAngles().y, 0.0f );
	float pitch = player->EyeAngles().x;
	if ( pitch > 180.0f )
		pitch -= 360.0f;
	const float start = cl_fp_legs_tilt_start.GetFloat();
	const float tilt = m_Role == ROLE_LEGS && legsPose && pitch > start ?
		cl_fp_legs_tilt.GetFloat() * MIN( ( pitch - start ) / MAX( 89.0f - start, 1.0f ), 1.0f ) : 0.0f;
	if ( tilt != 0.0f )
	{
		// Rotating about forward x up (the view's right axis) by a positive angle tips the model's up toward -forward.
		matrix3x4_t yaw, rotate, tilted;
		AngleMatrix( angles, yaw );
		MatrixBuildRotationAboutAxis( CrossProduct( viewForward, Vector( 0.0f, 0.0f, 1.0f ) ), tilt, rotate );
		ConcatTransforms( rotate, yaw, tilted );
		MatrixAngles( tilted, angles );
	}
	SetAbsAngles( angles );
}

CStudioHdr *C_TerrorFirstPersonBody::OnNewModel()
{
	CStudioHdr *studio = BaseClass::OnNewModel();
	FindHiddenBones();
	return studio;
}

void C_TerrorFirstPersonBody::FindHiddenBones()
{
	m_LeanBones.ClearAll();
	m_nLeanBone = -1;
	m_nFirstHiddenBone = MAXSTUDIOBONES;
	for ( int i = 0; i < MAXSTUDIOBONES; ++i )
		m_CollapseRoot[i] = -1;
	CStudioHdr *studio = GetModelPtr();
	if ( m_Role != ROLE_LEGS || !studio )
		return;
	const int bones = MIN( studio->numbones(), MAXSTUDIOBONES );
	CUtlStringList names;
	V_SplitString( cl_fp_legs_hide_bones.GetString(), " ", names );
	for ( int n = 0; n < names.Count(); ++n )
	{
		const int root = Studio_BoneIndexByName( studio, names[n] );
		if ( root < 0 || m_CollapseRoot[root] >= 0 )
			continue;
		m_nFirstHiddenBone = MIN( m_nFirstHiddenBone, root );
		// Studio bones are ordered parent before child.
		for ( int i = root; i < bones; ++i )
		{
			const int parent = studio->pBone( i )->parent;
			if ( i == root || ( parent >= 0 && m_CollapseRoot[parent] == root ) )
				m_CollapseRoot[i] = root;
		}
	}
	m_nLeanBone = Studio_BoneIndexByName( studio, cl_fp_legs_lean_bone.GetString() );
	MarkSubtree( studio, m_nLeanBone, m_LeanBones );
}

void C_TerrorFirstPersonBody::BuildTransformations( CStudioHdr *studio, Vector *pos, Quaternion q[], const matrix3x4_t &cameraTransform,
	int boneMask, CBoneBitList &computed )
{
	BaseClass::BuildTransformations( studio, pos, q, cameraTransform, boneMask, computed );
	const int bones = MIN( studio->numbones(), MAXSTUDIOBONES );

	// Lean the upper body back about the model's right axis, pivoting at the lean bone: whatever stays visible above
	// it and the collapse points move away from the camera while the hips and legs stay put.
	const float lean = cl_fp_legs_lean.GetFloat();
	if ( m_nLeanBone >= 0 && lean != 0.0f )
	{
		Vector forward, right, up;
		AngleVectors( GetAbsAngles(), &forward, &right, &up );
		Vector pivot;
		MatrixGetColumn( GetBone( m_nLeanBone ), 3, pivot );
		// Rotating about forward x up (the right axis) by a positive angle tips up toward -forward.
		matrix3x4_t rotate;
		MatrixBuildRotationAboutAxis( CrossProduct( forward, up ), lean, rotate );
		Vector rotatedPivot;
		VectorRotate( pivot, rotate, rotatedPivot );
		MatrixSetColumn( pivot - rotatedPivot, 3, rotate );
		for ( int i = m_nLeanBone; i < bones; ++i )
		{
			if ( !m_LeanBones.IsBitSet( i ) )
				continue;
			matrix3x4_t &bone = GetBoneForWrite( i );
			const matrix3x4_t original = bone;
			ConcatTransforms( rotate, original, bone );
		}
	}

	// Collapse each hidden subtree to its root joint: triangles skinned to it vanish, and the ones shared with visible
	// bones close into a cap at that joint, which the lean and tilt keep below the bottom of the screen.
	for ( int i = m_nFirstHiddenBone; i < bones; ++i )
	{
		const int root = m_CollapseRoot[i];
		if ( root < 0 )
			continue;
		Vector collapse;
		MatrixGetColumn( GetBone( root ), 3, collapse ); // a root keeps its position when collapsed
		matrix3x4_t &bone = GetBoneForWrite( i );
		SetIdentityMatrix( bone );
		MatrixScaleByZero( bone );
		MatrixSetColumn( collapse, 3, bone );
	}
}

void C_TerrorFirstPersonShadowWeapon::Maintain( CHandle< C_TerrorFirstPersonShadowWeapon > &handle, C_TerrorPlayer *player, C_TerrorFirstPersonBody *body )
{
	// Same visibility rules as the third-person C_ShadowWeapon (C_CSPlayer::UpdateShadowWeapon).
	CWeaponCSBase *weapon = static_cast< CWeaponCSBase * >( player->GetActiveWeapon() );
	int modelIndex = -1;
	if ( body && weapon && weapon->IsWeaponVisible() && weapon->m_iWorldModelIndex != 0 && player->GetTeamNumber() != TEAM_INFECTED &&
		!player->GetTerrorAnimState()->IsGrenadeProjectilePresentationActive() &&
		!player->GetTerrorAnimState()->IsGrenadePresentationSuppressed() )
		modelIndex = modelinfo->GetModelIndex( weapon->GetWorldModel() );

	C_TerrorFirstPersonShadowWeapon *current = handle.Get();
	if ( current && ( current->m_nWorldModelIndex != modelIndex || current->GetMoveParent() != body ) )
	{
		current->Release();
		current = NULL;
		handle.Term();
	}
	if ( current || modelIndex < 0 )
	{
		if ( current )
			current->SetSkin( weapon->GetSkin() );
		return;
	}
	current = new C_TerrorFirstPersonShadowWeapon( player );
	if ( !current->InitializeAsClientEntity( weapon->GetWorldModel(), false ) )
	{
		current->Release();
		return;
	}
	current->m_nWorldModelIndex = modelIndex;
	current->FollowEntity( body, true );
	current->SetSkin( weapon->GetSkin() );
	current->SetSolid( SOLID_NONE );
	current->RemoveEFlags( EFL_USE_PARTITION_WHEN_NOT_SOLID );
	handle = current;
}

bool C_TerrorFirstPersonShadowWeapon::ShouldCastShadowWhileHidden()
{
	C_TerrorPlayer *player = m_hPlayer.Get();
	return player && C_TerrorFirstPersonBody::CastsSelfShadow( player ) && !IsEffectActive( EF_NODRAW );
}

int C_TerrorFirstPersonShadowWeapon::DrawModel( int flags )
{
	if ( !InFeatureShadowDraw( flags ) || !ShouldCastShadowWhileHidden() )
		return 0;
	return BaseClass::DrawModel( flags );
}
