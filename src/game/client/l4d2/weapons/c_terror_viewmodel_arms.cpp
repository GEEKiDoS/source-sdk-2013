#include "cbase.h"
#include "l4d2/weapons/terror_viewmodel.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/effects/terror_glow_render.h"
#include "view.h"
#include "collisionproperty.h"
#include "model_types.h"
#include "tier0/memdbgon.h"

static ConVar vm_draw_addon( "vm_draw_addon", "1", 0x0000000A ); // Native107C6478.

// Native287ED0 / primary105C0B44. Only InitializeAsClientEntity is overridden
// beyond the real destructor. The inherited animation/DrawModel owners remain.
class C_ViewmodelAttachmentModel : public C_BaseAnimating
{
	DECLARE_CLASS( C_ViewmodelAttachmentModel, C_BaseAnimating );
public:
	C_ViewmodelAttachmentModel() {}
	virtual bool InitializeAsClientEntity( const char *modelName, bool renderWithViewModels ) OVERRIDE
	{
		if ( !BaseClass::InitializeAsClientEntity( modelName, renderWithViewModels ) )
			return false;
		AddEffects( EF_BONEMERGE );
		AddEffects( EF_BONEMERGE_FASTCULL );
		AddEffects( EF_NODRAW );
		return true;
	}
	void SetViewModel( CTerrorViewModel *viewModel ) // Whole native2886D0.
	{
		m_hViewModel = viewModel;
	}
private:
	CHandle< CTerrorViewModel > m_hViewModel; // Native2392, normal INVALID ctor.
};

// Whole native287F40. Root Remove47140 changes actual removal state/partition;
// this is NOT Release, the direct BB020 queue operation, or a handle/cache clear.
void CTerrorViewModel::RemoveTerrorArms()
{
	C_ViewmodelAttachmentModel *arms = m_hTerrorArms.Get();
	if ( arms )
		arms->Remove();
}

// Whole native288700, Root OnDormancyChanged52(bool), NOT UpdateOnRemove().
// Root49060 calls this after visibility/particle dormancy and before entity-list
// notification. Base19590 is a genuine RET4 leaf; no extra base call is added.
void CTerrorViewModel::OnDormancyChanged( bool dormant )
{
	if ( dormant )
		RemoveTerrorArms();
}

// Whole native288750. Sole native caller259CC5 in Terror259790; NOT Draw/Simulate.
void CTerrorViewModel::UpdateTerrorArms()
{
	C_BaseEntity *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() || owner->GetTeamNumber() != TEAM_SURVIVOR )
	{
		RemoveTerrorArms();
		return;
	}
	CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( GetOwningWeapon() );
	if ( !weapon )
	{
		RemoveTerrorArms();
		return;
	}
	const CTerrorWeaponInfo &info = weapon->GetTerrorWpnData();
	const int character = static_cast< C_TerrorPlayer * >( owner )->GetSurvivorCharacter();
	// Native client406 reads actual3308 m_isDualWielding. The script lookup's
	// second argument is unused, but the virtual call and its order are retained.
	const bool dual = weapon->IsDualWielding();
	const char *characterName = GetSurvivorCharacterName( character );
	const char *modelName = info.GetCharacterViewmodelAddon( characterName, dual );
	if ( !modelName || !modelName[0] )
	{
		RemoveTerrorArms();
		return;
	}
	if ( m_hTerrorArms.Get() && !V_stricmp( m_szTerrorArmsModel, modelName ) )
		return; // Native does NOT refresh skin/parent/ID for the same model.
	RemoveTerrorArms();
	C_ViewmodelAttachmentModel *arms = new C_ViewmodelAttachmentModel;
	if ( !arms->InitializeAsClientEntity( modelName, true ) ) // Native28884D: render with viewmodels.
		return; // Proven native failed-init orphan; do NOT repair with Release.
	m_hTerrorArms = arms;
	V_strncpy( m_szTerrorArmsModel, modelName, sizeof( m_szTerrorArmsModel ) );
	arms->SetSkin( GetSkin() );
	arms->SetParent( this, 0 ); // Native4D3A0 is SetParent, NOT FollowEntity.
	arms->SetLocalOrigin( vec3_origin );
	arms->UpdatePartitionListEntry(); // Native445A0, NOT CreateShadow/Spawn.
	arms->CollisionProp()->MarkPartitionHandleDirty(); // NativeD78B0.
	arms->UpdateVisibility(); // Native488F0.
	arms->SetViewModel( this );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// Whole native288920. Parent-approved ordinary one-argument SDK bridge strips
// ONLY native0x800 and scopes skip-stencil metadata. Incoming per-pass render
// blend/color are already applied by the real leaf caller and remain untouched.
int CTerrorViewModel::DrawModel( int flags )
{
	const bool stencil = ( flags & STUDIO_RENDER ) != 0;
	if ( stencil )
		TerrorGlow_BeginStencilMask( 128, false, 128 );
	int baseFlags = flags;
	if ( flags != 0 )
	{
		if ( vm_draw_addon.GetInt() != 0 && m_hTerrorArms.Get() )
		{
			if ( render->GetBlend() <= 0.0f )
				return 0; // Proven native GPU-stencil128 leak; do NOT repair.
			TerrorModel_DrawModel( m_hTerrorArms.Get(), flags | 0x800 );
		}
		baseFlags = flags | 0x800;
	}
	const int result = BaseClass::DrawModel( baseFlags );
	if ( stencil )
		TerrorGlow_EndStencilMask();
	return result;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
