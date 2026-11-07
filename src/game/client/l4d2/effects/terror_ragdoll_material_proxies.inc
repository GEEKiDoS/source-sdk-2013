// Included once by the existing l4d2/terror_material_proxies.cpp owner.
class CProxyBurn : public CResultProxy
{
public:
	bool Init( IMaterial *material, KeyValues *values ) OVERRIDE { return CResultProxy::Init( material, values ); }
	void OnBind( void *renderable ) OVERRIDE
	{
		if ( !renderable ) return;
		C_BaseEntity *entity = BindArgToEntity( renderable );
		C_TerrorPlayer *player = ToTerrorPlayer( entity );
		if ( player ) { SetFloatResult( player->m_burnPercent ); return; }
		C_ClientRagdoll *ragdoll = dynamic_cast< C_ClientRagdoll * >( entity );
		SetFloatResult( ragdoll ? ragdoll->m_burnPercent : 0.0f );
	}
};
EXPOSE_INTERFACE( CProxyBurn, IMaterialProxy, "BurnLevel" IMATERIAL_PROXY_INTERFACE_VERSION );
class CProxyScreenSpaceBurn : public CResultProxy
{
public:
	bool Init( IMaterial *material, KeyValues *values ) OVERRIDE { return CResultProxy::Init( material, values ); }
	void OnBind( void *renderable ) OVERRIDE
	{
		if ( !renderable ) return;
		C_TerrorPlayer *player = ToTerrorPlayer( BindArgToEntity( renderable ) );
		if ( !player ) { SetFloatResult( 0.0f ); return; }
		const float burn = player->m_burnPercent * 10.0f;
		SetFloatResult( burn <= 1.0f ? burn : 1.0f ); // unordered ->1, no lower clamp
	}
};
EXPOSE_INTERFACE( CProxyScreenSpaceBurn, IMaterialProxy, "ScreenSpaceBurnLevel" IMATERIAL_PROXY_INTERFACE_VERSION );
class CProxyBBQ : public CResultProxy
{
public:
	bool Init( IMaterial *material, KeyValues *values ) OVERRIDE { return CResultProxy::Init( material, values ); }
	void OnBind( void *renderable ) OVERRIDE
	{
		if ( !renderable ) return;
		C_BaseEntity *entity = BindArgToEntity( renderable );
		C_TerrorPlayer *player = ToTerrorPlayer( entity );
		if ( player ) { SetFloatResult( player->m_bbqPercent ); return; }
		C_ClientRagdoll *ragdoll = dynamic_cast< C_ClientRagdoll * >( entity );
		SetFloatResult( ragdoll ? ragdoll->m_bbqPercent : 1.0f );
	}
};
EXPOSE_INTERFACE( CProxyBBQ, IMaterialProxy, "BBQLevel" IMATERIAL_PROXY_INTERFACE_VERSION );
